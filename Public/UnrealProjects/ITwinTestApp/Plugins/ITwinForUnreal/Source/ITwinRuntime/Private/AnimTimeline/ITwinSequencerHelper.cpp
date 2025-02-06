/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSequencerHelper.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinSequencerHelper.h"
#include "SDK/Core/Tools/Log.h"

#include <Camera/CameraActor.h>
#include <LevelSequence.h>
#include <UObject/Package.h>
#include <MovieScene.h>
#include <MovieSceneSection.h>
#include <Tracks/MovieSceneFloatTrack.h>
#include <Tracks/MovieScene3DTransformTrack.h>
#include <Tracks/MovieSceneCameraCutTrack.h>
#include <Sections/MovieSceneFloatSection.h>
#include <Sections/MovieScene3DTransformSection.h>
#include <Sections/MovieSceneCameraCutSection.h>
#include <Channels/MovieSceneChannelProxy.h>
#include <Channels/MovieSceneDoubleChannel.h>
#include <Channels/MovieSceneFloatChannel.h>

#include "AssetRegistry/AssetRegistryModule.h"
//#include "Package.h"
//#include "FileHelpers.h"


namespace {
	FFrameNumber GetFrameNum(ULevelSequence* pLevelSeq, int iFrame)
	{
		return FFrameNumber(int(iFrame * pLevelSeq->MovieScene->GetTickResolution().AsDecimal() / pLevelSeq->MovieScene->GetDisplayRate().AsDecimal()));
	}

	FFrameNumber GetTimeFrameNum(ULevelSequence* pLevelSeq, float fTime)
	{
		return FFrameNumber(int(fTime * pLevelSeq->MovieScene->GetTickResolution().AsDecimal()));
	}

	TRange<FFrameNumber> GetFrameRange(ULevelSequence* pLevelSeq, int iFrameStart, int iFrameEnd)
	{
		return TRange<FFrameNumber>(GetFrameNum(pLevelSeq, iFrameStart), TRangeBound<FFrameNumber>::Inclusive(GetFrameNum(pLevelSeq, iFrameEnd)));
	}

	TRange<FFrameNumber> GetTimeFrameRange(ULevelSequence* pLevelSeq, float fTimeStart, float fTimeEnd)
	{
		return TRange<FFrameNumber>(GetTimeFrameNum(pLevelSeq, fTimeStart), TRangeBound<FFrameNumber>::Inclusive(GetTimeFrameNum(pLevelSeq, fTimeEnd)));
	}

	float GetPlaybackEndTime(ULevelSequence* pLevelSeq)
	{
		return pLevelSeq->MovieScene->GetPlaybackRange().GetUpperBoundValue().Value / pLevelSeq->MovieScene->GetTickResolution().AsDecimal();
	}

	TRange<FFrameNumber> ComputeRangeFromKFs(UMovieSceneSection* pSection)
	{
		TRange<FFrameNumber> retRange = TRange<FFrameNumber>::Empty();
		if (pSection == NULL)
			return retRange;
		for (int32 i(0); i < pSection->GetChannelProxy().NumChannels(); i++)
		{
			FMovieSceneChannel* pChannel = pSection->GetChannelProxy().GetChannel<FMovieSceneDoubleChannel>(i);
			if (pChannel == nullptr)
				pChannel = pSection->GetChannelProxy().GetChannel<FMovieSceneFloatChannel>(i);
			if (pChannel == nullptr)
				continue;
			TArray<FFrameNumber> vKeyTimes;
			TArray<FKeyHandle> vKeyHandles;
			pChannel->GetKeys(TRange<FFrameNumber>::All(), &vKeyTimes, &vKeyHandles);
			if (vKeyTimes.Num() == 0)
				continue;
			TRange<FFrameNumber> channelRange = TRange<FFrameNumber>(vKeyTimes[0], TRangeBound<FFrameNumber>::Inclusive(vKeyTimes.Last()));
			ensure(pChannel->ComputeEffectiveRange() == channelRange);
			if (retRange.IsEmpty())
				retRange = channelRange;
			else if (channelRange.GetLowerBoundValue() < retRange.GetLowerBoundValue())
				retRange.SetLowerBoundValue(channelRange.GetLowerBoundValue());
			else if (channelRange.GetUpperBoundValue() > retRange.GetUpperBoundValue())
				retRange.SetUpperBoundValue(channelRange.GetUpperBoundValue());
		}
		return retRange;
	}

	template<class ChannelType>
	void ShiftKeyFrameInChannel(UMovieSceneSection* pSection, int iChannelIdx, TRange<FFrameNumber>& FrameRange, FFrameNumber iDeltaFrameNum)
	{
		if (pSection == nullptr)
			return;
		FMovieSceneChannel* pChannel = pSection->GetChannelProxy().GetChannel<ChannelType>(iChannelIdx);
		if (pChannel == nullptr)
			return;
		ULevelSequence* pLevelSeq = Cast<ULevelSequence>(pSection->GetOutermostObject());
		TArray<FFrameNumber> vKeyTimes;
		TArray<FKeyHandle> vKeyHandles;
		pChannel->GetKeys(FrameRange, &vKeyTimes, &vKeyHandles);
		for (size_t i(0); i < vKeyTimes.Num(); i++)
			vKeyTimes[i] += iDeltaFrameNum;
		pChannel->SetKeyTimes(vKeyHandles, vKeyTimes);
	}

	template<class ChannelType>
	void ShiftSectionKFs(UMovieSceneSection* pSection, TRange<FFrameNumber> editedKFRange, FFrameNumber deltaFrameNum)
	{
		if (!pSection)
			return;
		auto ExtendedRange = pSection->GetRange();
		// extend frame range to include both old and new frame times
		if (deltaFrameNum > 0)
			ExtendedRange.SetUpperBoundValue(ExtendedRange.GetUpperBoundValue() + deltaFrameNum);
		//else
		//	ExtendedRange.SetLowerBoundValue(ExtendedRange.GetLowerBoundValue() + deltaFrameNum);
		pSection->SetRange(ExtendedRange);
		// shift frame times in each transform channel
		for (int i(0); i < pSection->GetChannelProxy().NumChannels(); i++)
			ShiftKeyFrameInChannel<ChannelType>(pSection, i, editedKFRange, deltaFrameNum);
		// adjust frame range to the shifted times
		pSection->SetRange(ComputeRangeFromKFs(pSection));
		pSection->Modify();
	}

	FString GetErrorMsg(FString fname, FString msg)
	{
		return FString::Printf(TEXT("'%s' error: '%s'"), *fname, *msg);
	}

	FString GetOutMsg(bool bSuccess, FString fname)
	{
		return bSuccess ? FString::Printf(TEXT("'%s' succeeded"), *fname) : FString::Printf(TEXT("'%s' failed"), *fname);
	}
}


FGuid USequencerHelper::GetPActorGuidFromLevelSequence(AActor* pActor, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "GetPActorGuidFromLevelSequence";
	bOutSuccess = false;

	if (pActor == nullptr)
	{
		BE_LOGW("Timeline", "Invalid actor pointer");
		//BE_LOGW("Timeline", GetErrorMsg(fname, "Invalid actor pointer"));
		return FGuid();
	}
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	if (pLevelSeq == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Level sequence not found");
		return FGuid();
	}
	FGuid guid;
	if (pActor && pActor->GetWorld())
		guid = pLevelSeq->FindBindingFromObject(pActor, pActor->GetWorld());
	bOutSuccess = guid.IsValid();
	outInfoMsg = GetOutMsg(bOutSuccess, fname);
	return guid;
}
		
FGuid USequencerHelper::AddPActorToLevelSequence(AActor* pActor, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "AddPActorToLevelSequence";
	bOutSuccess = false;

	FGuid guid = GetPActorGuidFromLevelSequence(pActor, levelSequencePath, bOutSuccess, outInfoMsg);
	if (guid.IsValid())
	{
		outInfoMsg = GetErrorMsg(fname, "Actor already exists in sequence");
		return guid;
	}
	if (pActor == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Invalid actor pointer");
		return FGuid();
	}
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	if (pLevelSeq == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Level sequence not found");
		return FGuid();
	}
	//guid = Cast<UMovieSceneSequence>(pLevelSeq)->CreatePossessable(pActor);

	UMovieScene* MovieScene = pLevelSeq->GetMovieScene();
	if (!MovieScene)
	{
		outInfoMsg = GetErrorMsg(fname, "Invalid movie scene");
		return FGuid();
	}
	// Add the actor as a possessable in the sequence
	guid = MovieScene->AddPossessable(pActor->GetName(), pActor->GetClass());

	//// Create a new possessable entry
	//FMovieScenePossessable Possessable;
	//Possessable.SetName(pActor->GetName());
	//Possessable.SetPossessedObjectClass(pActor->GetClass());
	//Possessable.SetGuid(FGuid::NewGuid());  // Manually create a new GUID

	//// Add the possessable to the movie scene manually
	//MovieScene->AddPossessable(Possessable);

	// Create GUID for the actor binding
	//FGuid PossessableGuid = FGuid::NewGuid();

	// Set BindingOverride for the newly added possessable actor
	pLevelSeq->BindPossessableObject(guid, *pActor, pActor->GetWorld());
	guid = GetPActorGuidFromLevelSequence(pActor, levelSequencePath, bOutSuccess, outInfoMsg);
	//if (LevelSequenceActor->BindingOverrides)
	//{
	//	FMovieSceneObjectBindingID BindingID(PossessableGuid);
	//	LevelSequenceActor->BindingOverrides->SetBinding(BindingID, { Actor });
	//}

	bOutSuccess = guid.IsValid();
	outInfoMsg = GetOutMsg(bOutSuccess, fname);
	return guid;
}
		
void USequencerHelper::RemovePActorFromLevelSequence(AActor* pActor, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "RemovePActorFromLevelSequence";
	bOutSuccess = false;

	FGuid guid = GetPActorGuidFromLevelSequence(pActor, levelSequencePath, bOutSuccess, outInfoMsg);
	if (!guid.IsValid())
	{
		outInfoMsg = GetErrorMsg(fname, "Actor doesn't exist in sequence");
		return;
	}
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	pLevelSeq->UnbindPossessableObjects(guid); // unbind track from actor
	bOutSuccess = pLevelSeq->MovieScene->RemovePossessable(guid);
	outInfoMsg = GetOutMsg(bOutSuccess, fname);
}



FGuid USequencerHelper::GetSActorGuidFromLevelSequence(FString spawnableName, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "GetSActorGuidFromLevelSequence";
	bOutSuccess = false;

	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	if (pLevelSeq == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Level sequence not found");
		return FGuid();
	}
	FGuid guid = FGuid();
	for (int i(0); i < pLevelSeq->MovieScene->GetSpawnableCount(); i++)
	{
		FMovieSceneSpawnable spawnable = pLevelSeq->MovieScene->GetSpawnable(i);
		if (pLevelSeq->MovieScene->GetObjectDisplayName(spawnable.GetGuid()).ToString() == spawnableName)
		{
			guid = spawnable.GetGuid();
			break;
		}
	}
	bOutSuccess = guid.IsValid();
	outInfoMsg = GetOutMsg(bOutSuccess, fname);
	return guid;
}

FGuid USequencerHelper::AddSActorToLevelSequence(FString spawnableName, FString assetPath, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "AddSActorToLevelSequence";
	bOutSuccess = false;

	FGuid guid = GetSActorGuidFromLevelSequence(spawnableName, levelSequencePath, bOutSuccess, outInfoMsg);
	if (guid.IsValid())
	{
		outInfoMsg = GetErrorMsg(fname, "Spawnable actor already exists in level sequence");
		return guid;
	}
	UObject* pObjTemplate = Cast<UObject>(StaticLoadObject(UObject::StaticClass(), nullptr, *assetPath));
	if (pObjTemplate == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Spawnable template not found");
		return FGuid();
	}
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	if (pLevelSeq == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Level sequence not found");
		return FGuid();
	}
	guid = Cast<UMovieSceneSequence>(pLevelSeq)->CreateSpawnable(pObjTemplate);
	bOutSuccess = guid.IsValid();
	outInfoMsg = GetOutMsg(bOutSuccess, fname);
	// TODO: the following function is only available in Editor
	//if (guid.IsValid())
	//	pLevelSeq->MovieScene->SetObjectDisplayName(guid, FText::FromString(spawnableName)); // rename the spawnable to be able to find it later
	return guid;
}

void USequencerHelper::RemoveSActorFromLevelSequence(FString spawnableName, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "RemoveSActorFromLevelSequence";
	bOutSuccess = false;

	FGuid guid = GetSActorGuidFromLevelSequence(spawnableName, levelSequencePath, bOutSuccess, outInfoMsg);
	if (!guid.IsValid())
	{
		outInfoMsg = GetErrorMsg(fname, "Spawnable actor not found in level sequence");
		return;
	}
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	bOutSuccess = pLevelSeq->MovieScene->RemoveSpawnable(guid);
	outInfoMsg = GetOutMsg(bOutSuccess, fname);
}


template<typename TrackType>
TrackType* USequencerHelper::GetTrackFromActorInLevelSequence(AActor* pActor, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "GetTrackFromActorInLevelSequence";
	bOutSuccess = false;

	FGuid guid = GetPActorGuidFromLevelSequence(pActor, levelSequencePath, bOutSuccess, outInfoMsg);
	if (!guid.IsValid())
	{
		outInfoMsg = GetErrorMsg(fname, "Actor not found in level sequence");
		return nullptr;
	}

	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	TrackType* pTrack = pLevelSeq->MovieScene->FindTrack<TrackType>(guid);
	bOutSuccess = pTrack != nullptr;
	outInfoMsg = GetOutMsg(bOutSuccess, fname);
	return pTrack;
}

template<class TrackType>
TrackType* USequencerHelper::AddTrackToActorInLevelSequence(AActor* pActor, FString levelSequencePath, bool bOverwriteExisting, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "AddTrackToActorInLevelSequence";
	bOutSuccess = false;

	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	if (pLevelSeq == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Level sequence not found");
		return nullptr;
	}

	TrackType* pTrack = GetTrackFromActorInLevelSequence<TrackType>(pActor, levelSequencePath, bOutSuccess, outInfoMsg);
	if (pTrack != nullptr)
	{
		if (bOverwriteExisting)
		{
			RemoveTrackFromActorInLevelSequence<TrackType>(pActor, levelSequencePath, bOutSuccess, outInfoMsg);
		}
		else
		{
			outInfoMsg = GetErrorMsg(fname, "Track of this type already exists for this actor");
			return pTrack;
		}
	}

	FGuid guid = GetPActorGuidFromLevelSequence(pActor, levelSequencePath, bOutSuccess, outInfoMsg);
	if (!guid.IsValid())
	{
		outInfoMsg = GetErrorMsg(fname, "Actor not found in level sequence");
		return nullptr;
	}

	pTrack = pLevelSeq->MovieScene->AddTrack<TrackType>(guid);
	bOutSuccess = pTrack != nullptr;
	outInfoMsg = GetOutMsg(bOutSuccess, fname);
	return pTrack;
}

template<class TrackType>
void USequencerHelper::RemoveTrackFromActorInLevelSequence(AActor* pActor, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "RemoveTrackFromActorInLevelSequence";
	bOutSuccess = false;

	TrackType* pTrack = GetTrackFromActorInLevelSequence<TrackType>(pActor, levelSequencePath, bOutSuccess, outInfoMsg);
	if (pTrack == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Track not found for the actor");
		return;
	}

	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	pLevelSeq->MovieScene->RemoveTrack(*pTrack);
	bOutSuccess = true;
	outInfoMsg = GetOutMsg(true, fname);
}

template<class TrackType, class SectionType>
SectionType* USequencerHelper::GetSectionFromActorInLevelSequence(AActor* pActor, FString levelSequencePath, int iSectionIdx, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "GetSectionFromActorInLevelSequence";
	bOutSuccess = false;

	TrackType* pTrack = GetTrackFromActorInLevelSequence<TrackType>(pActor, levelSequencePath, bOutSuccess, outInfoMsg);
	if (pTrack == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Track not found for the actor");
		return nullptr;
	}

	TArray<UMovieSceneSection*> vSections = pTrack->GetAllSections();
	if (iSectionIdx < 0 || iSectionIdx >= vSections.Num())
	{
		outInfoMsg = GetErrorMsg(fname, "Section index is incorrect");
		return nullptr;
	}

	bOutSuccess = true;
	outInfoMsg = GetOutMsg(true, fname);
	return Cast<SectionType>(vSections[iSectionIdx]);
}

template<class TrackType, class SectionType>
SectionType* USequencerHelper::AddSectionToActorInLevelSequence(AActor* pActor, FString levelSequencePath, FFrameNumber FrameNum, EMovieSceneBlendType eBlendType, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "AddSectionToActorInLevelSequence";
	bOutSuccess = false;

	TrackType* pTrack = GetTrackFromActorInLevelSequence<TrackType>(pActor, levelSequencePath, bOutSuccess, outInfoMsg);
	if (pTrack == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Track of this type not found for the actor");
		return nullptr;
	}

	SectionType* pSection = Cast<SectionType>(pTrack->CreateNewSection());
	if (pSection == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Failed to create transform section");
		return nullptr;
	}

	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	pSection->SetRange(TRange<FFrameNumber>((FFrameNumber)0, TRangeBound<FFrameNumber>::Inclusive(FrameNum)));
	pSection->SetBlendType(eBlendType);
	int iRowIdx = -1;
	for (UMovieSceneSection* section : pTrack->GetAllSections())
		iRowIdx = FMath::Max(iRowIdx, section->GetRowIndex());
	pSection->SetRowIndex(iRowIdx + 1);
	pTrack->AddSection(*pSection);
	pTrack->MarkAsChanged();
	pTrack->Modify();

	bOutSuccess = true;
	outInfoMsg = GetOutMsg(true, fname);
	return pSection;
}

template<class TrackType, class SectionType>
void USequencerHelper::RemoveSectionFromActorInLevelSequence(AActor* pActor, FString levelSequencePath, int iSectionIdx, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "RemoveSectionFromActorInLevelSequence";
	bOutSuccess = false;

	SectionType* pSection = GetSectionFromActorInLevelSequence<SectionType>(pActor, levelSequencePath, iSectionIdx, bOutSuccess, outInfoMsg);
	if (pSection == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Section not found in level sequence");
		return;
	}

	TrackType* pTrack = GetTrackFromActorInLevelSequence<TrackType>(pActor, levelSequencePath, bOutSuccess, outInfoMsg);
	pTrack->RemoveSection(*pSection);
	pTrack->MarkAsChanged();
	pTrack->Modify();

	bOutSuccess = true;
	outInfoMsg = GetOutMsg(true, fname);
}


FFrameNumberRange USequencerHelper::AddTransformKeyFrame(AActor* pActor, FString levelSequencePath, int iSectionIdx, FFrameNumber iFrameNum, FTransform Transform, int iKeyInterp, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "AddTransformKeyFrame";
	bOutSuccess = false;

	UMovieScene3DTransformSection* pTransSection = GetSectionFromActorInLevelSequence<UMovieScene3DTransformTrack, UMovieScene3DTransformSection>(pActor, levelSequencePath, iSectionIdx, bOutSuccess, outInfoMsg);
	if (pTransSection == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Section not found in level sequence");
		return FFrameNumberRange();
	}

	//DEBUG!!!
	//if (HasTransformKeyFrame(pActor, levelSequencePath, iSectionIdx, iFrameNum, bOutSuccess, outInfoMsg))
	//{
	//	bOutSuccess = true;
	//	return;
	//}

	RemoveTransformKeyFrame(pActor, levelSequencePath, iSectionIdx, iFrameNum, bOutSuccess, outInfoMsg);

	AddKeyFrameToChannel<FMovieSceneDoubleChannel, double>(pTransSection, 0, iFrameNum, Transform.GetLocation().X, iKeyInterp, bOutSuccess, outInfoMsg);
	AddKeyFrameToChannel<FMovieSceneDoubleChannel, double>(pTransSection, 1, iFrameNum, Transform.GetLocation().Y, iKeyInterp, bOutSuccess, outInfoMsg);
	AddKeyFrameToChannel<FMovieSceneDoubleChannel, double>(pTransSection, 2, iFrameNum, Transform.GetLocation().Z, iKeyInterp, bOutSuccess, outInfoMsg);
	
	AddKeyFrameToChannel<FMovieSceneDoubleChannel, double>(pTransSection, 3, iFrameNum, Transform.Rotator().Roll, iKeyInterp, bOutSuccess, outInfoMsg);
	AddKeyFrameToChannel<FMovieSceneDoubleChannel, double>(pTransSection, 4, iFrameNum, Transform.Rotator().Pitch, iKeyInterp, bOutSuccess, outInfoMsg);
	AddKeyFrameToChannel<FMovieSceneDoubleChannel, double>(pTransSection, 5, iFrameNum, Transform.Rotator().Yaw, iKeyInterp, bOutSuccess, outInfoMsg);

	AddKeyFrameToChannel<FMovieSceneDoubleChannel, double>(pTransSection, 6, iFrameNum, Transform.GetScale3D().X, iKeyInterp, bOutSuccess, outInfoMsg);
	AddKeyFrameToChannel<FMovieSceneDoubleChannel, double>(pTransSection, 7, iFrameNum, Transform.GetScale3D().Y, iKeyInterp, bOutSuccess, outInfoMsg);
	AddKeyFrameToChannel<FMovieSceneDoubleChannel, double>(pTransSection, 8, iFrameNum, Transform.GetScale3D().Z, iKeyInterp, bOutSuccess, outInfoMsg);

	if (iFrameNum > pTransSection->GetRange().GetUpperBoundValue())
	{
		auto newRange = FFrameNumberRange((FFrameNumber)0, TRangeBound<FFrameNumber>::Inclusive(iFrameNum));
		pTransSection->SetRange(newRange);
	}
	
	pTransSection->Modify();
	Cast<UMovieScene3DTransformTrack>(pTransSection->GetOuter())->MarkAsChanged();

	bOutSuccess = true;
	outInfoMsg = GetOutMsg(true, fname);

	return pTransSection->GetRange();
}

FFrameNumberRange USequencerHelper::AddFloatKeyFrame(AActor* pActor, FString levelSequencePath, int iSectionIdx, FFrameNumber iFrameNum, float fValue, int iKeyInterp, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "AddFloatKeyFrame";
	bOutSuccess = false;

	UMovieSceneFloatSection* pFloatSection = GetSectionFromActorInLevelSequence<UMovieSceneFloatTrack, UMovieSceneFloatSection>(pActor, levelSequencePath, iSectionIdx, bOutSuccess, outInfoMsg);
	if (pFloatSection == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Section not found in level sequence");
		return FFrameNumberRange();
	}

	RemoveFloatKeyFrame(pActor, levelSequencePath, iSectionIdx, iFrameNum, bOutSuccess, outInfoMsg);

	AddKeyFrameToChannel<FMovieSceneFloatChannel, float>(pFloatSection, 0, iFrameNum, fValue, iKeyInterp, bOutSuccess, outInfoMsg);

	if (iFrameNum > pFloatSection->GetRange().GetUpperBoundValue())
	{
		auto newRange = TRange<FFrameNumber>((FFrameNumber)0, TRangeBound<FFrameNumber>::Inclusive(iFrameNum));
		pFloatSection->SetRange(newRange);
	}

	pFloatSection->Modify();
	Cast<UMovieSceneFloatTrack>(pFloatSection->GetOuter())->MarkAsChanged();

	bOutSuccess = true;
	outInfoMsg = GetOutMsg(true, fname);

	return pFloatSection->GetRange();
}

bool USequencerHelper::HasTransformKeyFrame(AActor* pActor, FString levelSequencePath, int iSectionIdx, FFrameNumber iFrameNum, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "HasTransformKeyFrame";
	bOutSuccess = false;

	UMovieScene3DTransformSection* pTransSection = GetSectionFromActorInLevelSequence<UMovieScene3DTransformTrack, UMovieScene3DTransformSection>(pActor, levelSequencePath, iSectionIdx, bOutSuccess, outInfoMsg);
	if (pTransSection == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Section not found in level sequence");
		return false;
	}

	FMovieSceneDoubleChannel* pChannel = pTransSection->GetChannelProxy().GetChannel<FMovieSceneDoubleChannel>(0);
	if (pChannel == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Channel not found in the section");
		return false;
	}

	TArray<FFrameNumber> vKeyTimes;
	TArray<FKeyHandle> vKeyHandles;
	pChannel->GetKeys(TRange<FFrameNumber>(iFrameNum, iFrameNum), &vKeyTimes, &vKeyHandles);
	return vKeyHandles.Num() > 0;
}

FFrameNumberRange USequencerHelper::RemoveTransformKeyFrame(AActor* pActor, FString levelSequencePath, int iSectionIdx, FFrameNumber iFrameNum, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "RemoveTransformKeyFrame";
	bOutSuccess = false;

	UMovieScene3DTransformSection* pTransSection = GetSectionFromActorInLevelSequence<UMovieScene3DTransformTrack, UMovieScene3DTransformSection>(pActor, levelSequencePath, iSectionIdx, bOutSuccess, outInfoMsg);
	if (pTransSection == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Section not found in level sequence");
		return FFrameNumberRange();
	}

	for(int i(0); i < 9; i++)
		RemoveKeyFrameFromChannel<FMovieSceneDoubleChannel>(pTransSection, i, iFrameNum, bOutSuccess, outInfoMsg);

	auto newRange = ComputeRangeFromKFs(pTransSection);
	pTransSection->SetRange(newRange);
	pTransSection->Modify();

	bOutSuccess = true;
	outInfoMsg = GetOutMsg(true, fname);

	return newRange;
}

FFrameNumberRange USequencerHelper::RemoveFloatKeyFrame(AActor* pActor, FString levelSequencePath, int iSectionIdx, FFrameNumber iFrameNum, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "RemoveFloatKeyFrame";
	bOutSuccess = false;

	UMovieSceneFloatSection* pFloatSection = GetSectionFromActorInLevelSequence<UMovieSceneFloatTrack, UMovieSceneFloatSection>(pActor, levelSequencePath, iSectionIdx, bOutSuccess, outInfoMsg);
	if (pFloatSection == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Section not found in level sequence");
		return FFrameNumberRange();
	}

	RemoveKeyFrameFromChannel<FMovieSceneFloatChannel>(pFloatSection, 0, iFrameNum, bOutSuccess, outInfoMsg);

	auto newRange = ComputeRangeFromKFs(pFloatSection);
	pFloatSection->SetRange(newRange);
	pFloatSection->Modify();

	bOutSuccess = true;
	outInfoMsg = GetOutMsg(true, fname);

	return pFloatSection->GetRange();
}

template<class ChannelType, typename T>
void USequencerHelper::AddKeyFrameToChannel(UMovieSceneSection* pSection, int iChannelIdx, FFrameNumber iFrameNum, T fValue, int iKeyInterp, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "AddKeyFrameToChannel";
	bOutSuccess = false;

	if (pSection == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Invalid section pointer");
		return;
	}

	ChannelType* pChannel = pSection->GetChannelProxy().GetChannel<ChannelType>(iChannelIdx);
	if (pChannel == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Channel not found in the section");
		return;
	}

	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(pSection->GetOutermostObject());
	switch (iKeyInterp)
	{
	case 0: pChannel->AddCubicKey(iFrameNum, fValue); break;
	case 1: pChannel->AddLinearKey(iFrameNum, fValue); break;
	default: pChannel->AddConstantKey(iFrameNum, fValue); break;
	}
	pSection->Modify();

	bOutSuccess = true;
	outInfoMsg = GetOutMsg(true, fname);
}

template<class ChannelType>
void USequencerHelper::RemoveKeyFrameFromChannel(UMovieSceneSection* pSection, int iChannelIdx, FFrameNumber iFrameNum, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "RemoveKeyFrameFromChannel";
	bOutSuccess = false;

	if (pSection == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Invalid section pointer");
		return;
	}

	ChannelType* pChannel = pSection->GetChannelProxy().GetChannel<ChannelType>(iChannelIdx);
	if (pChannel == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Channel not found in the section");
		return;
	}

	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(pSection->GetOutermostObject());
	TArray<FFrameNumber> vKeyTimes;
	TArray<FKeyHandle> vKeyHandles;
	pChannel->GetKeys(TRange<FFrameNumber>(iFrameNum, iFrameNum), &vKeyTimes, &vKeyHandles);
	pChannel->DeleteKeys(vKeyHandles);
	pSection->Modify();

	bOutSuccess = true;
	outInfoMsg = GetOutMsg(true, fname);
}

namespace {
	template<class ChannelType, typename T>
	T GetValueFromChannel(UMovieSceneSection* pSection, int iChannelIdx, FFrameNumber iFrameNum)
	{
		ChannelType* pChannel = pSection->GetChannelProxy().GetChannel<ChannelType>(iChannelIdx);
		T outVal;
		pChannel->Evaluate(iFrameNum, outVal);
		return outVal;
	}
}

bool USequencerHelper::GetTransformAtTime(AActor* pActor, FString levelSequencePath, float fTime, FVector& pos, FRotator& rot)
{
	bool bRes;
	FString outMsg;
	UMovieScene3DTransformSection* pTransSection = GetSectionFromActorInLevelSequence<UMovieScene3DTransformTrack, UMovieScene3DTransformSection>(pActor, levelSequencePath, 0, bRes, outMsg);
	if (pTransSection == nullptr)
		return false;

	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	auto iFrameNum = GetTimeFrameNum(pLevelSeq, fTime);
	//if (!pLevelSeq->MovieScene->GetPlaybackRange().Contains(iFrameNum))
	//	return false;

	pos.X = GetValueFromChannel<FMovieSceneDoubleChannel, double>(pTransSection, 0, iFrameNum);
	pos.Y = GetValueFromChannel<FMovieSceneDoubleChannel, double>(pTransSection, 1, iFrameNum);
	pos.Z = GetValueFromChannel<FMovieSceneDoubleChannel, double>(pTransSection, 2, iFrameNum);
	rot.Roll = GetValueFromChannel<FMovieSceneDoubleChannel, double>(pTransSection, 3, iFrameNum);
	rot.Pitch = GetValueFromChannel<FMovieSceneDoubleChannel, double>(pTransSection, 4, iFrameNum);
	rot.Yaw = GetValueFromChannel<FMovieSceneDoubleChannel, double>(pTransSection, 5, iFrameNum);
	//FVector scale;
	//scale.X = GetValueFromDoubleChannel(pTransSection, 6, iFrameNum);
	//scale.Y = GetValueFromDoubleChannel(pTransSection, 7, iFrameNum);
	//scale.Z = GetValueFromDoubleChannel(pTransSection, 8, iFrameNum);

	return true;
}

bool USequencerHelper::GetValueAtTime(AActor* pActor, FString levelSequencePath, float fTime, float& fOutValue)
{
	bool bRes;
	FString outMsg;
	UMovieSceneFloatSection* pFloatSection = GetSectionFromActorInLevelSequence<UMovieSceneFloatTrack, UMovieSceneFloatSection>(pActor, levelSequencePath, 0, bRes, outMsg);
	if (pFloatSection == nullptr)
		return false;

	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	auto iFrameNum = GetTimeFrameNum(pLevelSeq, fTime);
	//if (!pLevelSeq->MovieScene->GetPlaybackRange().Contains(iFrameNum))
	//	return false;

	fOutValue = GetValueFromChannel<FMovieSceneFloatChannel, float>(pFloatSection, 0, iFrameNum);

	return true;
}

float USequencerHelper::GetDuration(AActor* pActor, FString levelSequencePath)
{
	bool bRes;
	FString outMsg;
	UMovieScene3DTransformSection* pTransSection = GetSectionFromActorInLevelSequence<UMovieScene3DTransformTrack, UMovieScene3DTransformSection>(pActor, levelSequencePath, 0, bRes, outMsg);
	if (pTransSection == nullptr)
		return 0.f;
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	auto rangeKFs = pTransSection->GetRange();// GetRangeFromKFs(pTransSection);
	return (float)(rangeKFs.GetUpperBoundValue().Value - rangeKFs.GetLowerBoundValue().Value) / (float)(pLevelSeq->MovieScene->GetTickResolution().AsDecimal());
}

float USequencerHelper::GetStartTime(AActor* pActor, FString levelSequencePath)
{
	bool bRes;
	FString outMsg;
	UMovieScene3DTransformSection* pTransSection = GetSectionFromActorInLevelSequence<UMovieScene3DTransformTrack, UMovieScene3DTransformSection>(pActor, levelSequencePath, 0, bRes, outMsg);
	if (pTransSection == nullptr)
		return 0.f;
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	auto rangeKFs = pTransSection->GetRange();// GetRangeFromKFs(pTransSection);
	return (float)(rangeKFs.GetLowerBoundValue().Value) / (float)(pLevelSeq->MovieScene->GetTickResolution().AsDecimal());
}

float USequencerHelper::GetEndTime(AActor* pActor, FString levelSequencePath)
{
	bool bRes;
	FString outMsg;
	UMovieScene3DTransformSection* pTransSection = GetSectionFromActorInLevelSequence<UMovieScene3DTransformTrack, UMovieScene3DTransformSection>(pActor, levelSequencePath, 0, bRes, outMsg);
	if (pTransSection == nullptr)
		return 0.f;
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	auto rangeKFs = pTransSection->GetRange();// GetRangeFromKFs(pTransSection);
	return (float)(rangeKFs.GetUpperBoundValue().Value) / (float)(pLevelSeq->MovieScene->GetTickResolution().AsDecimal());
}

UMovieSceneCameraCutTrack* USequencerHelper::AddCameraCutTrackToLevelSequence(FString levelSequencePath, bool bOverwriteExisting, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "AddCameraCutTrackToLevelSequence";
	bOutSuccess = false;

	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	if (pLevelSeq == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Level sequence not found");
		return nullptr;
	}

	UMovieSceneCameraCutTrack* pTrack = Cast<UMovieSceneCameraCutTrack>(pLevelSeq->MovieScene->GetCameraCutTrack());
	if (pTrack != nullptr)
	{
		if (!bOverwriteExisting)
		{
			outInfoMsg = GetErrorMsg(fname, "Camera cut track already exists in the sequence");
			return pTrack;
		}
		else
		{
			pTrack->RemoveAllAnimationData();
		}
	}
	else
	{
		pTrack = Cast<UMovieSceneCameraCutTrack>(pLevelSeq->MovieScene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));
	}

	bOutSuccess = pTrack != nullptr;
	outInfoMsg = GetOutMsg(bOutSuccess, fname);
	return pTrack;
}

void USequencerHelper::RemoveCameraCutTrackFromLevelSequence(FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "RemoveCameraCutTrackFromLevelSequence";
	bOutSuccess = false;

	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	if (pLevelSeq == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Level sequence not found");
		return;
	}

	pLevelSeq->MovieScene->RemoveCameraCutTrack();
	bOutSuccess = true;
	outInfoMsg = GetOutMsg(true, fname);
}

void USequencerHelper::LinkCameraToCameraCutTrack(ACameraActor* pCameraActor, FString levelSequencePath, float fStartTime, float fEndTime, bool &bOutSuccess, FString& outInfoMsg)
{
	FString fname = "LinkCameraToCameraCutTrack";
	bOutSuccess = false;

	if (pCameraActor == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Invalid camera actor pointer");
		return;
	}

	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	if (pLevelSeq == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Level sequence not found");
		return;
	}

	UMovieSceneCameraCutTrack* pTrack = Cast<UMovieSceneCameraCutTrack>(pLevelSeq->MovieScene->GetCameraCutTrack());
	if (pTrack == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Camera cut track not found in level sequence");
		return;
	}

	FGuid guid = GetPActorGuidFromLevelSequence(pCameraActor, levelSequencePath, bOutSuccess, outInfoMsg);
	if (!guid.IsValid())
	{
		outInfoMsg = GetErrorMsg(fname, "Camera actor not found in level sequence");
		return;
	}

	auto iStartFrameNum = GetTimeFrameNum(pLevelSeq, fStartTime);
	auto iEndFrameNum = GetTimeFrameNum(pLevelSeq, fEndTime);
	UMovieSceneCameraCutSection* pSection = pTrack->AddNewCameraCut(UE::MovieScene::FRelativeObjectBindingID(guid), iStartFrameNum);
	if (pSection == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Failed to add camera cut section");
		return;
	}
	pSection->SetRange(TRange<FFrameNumber>(iStartFrameNum, iEndFrameNum));
	pSection->Modify();
	pTrack->MarkAsChanged();

	pLevelSeq->MovieScene->SetPlaybackRange(TRange<FFrameNumber>(0, iEndFrameNum));

	bOutSuccess = true;
	outInfoMsg = GetOutMsg(true, fname);
}

bool USequencerHelper::AddNewClip(ACameraActor* pCameraActor, FString levelSequencePath)
{
	bool bRes;
	FString outMsg;

	FGuid guid = AddPActorToLevelSequence(pCameraActor, levelSequencePath, bRes, outMsg);
	if (!bRes)
		return false;

	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	auto initialRange = GetTimeFrameRange(pLevelSeq, 0, 1.f/*arbitrary small value to create a non-zero section*/);
	//pLevelSeq->MovieScene->SetPlaybackRange(initialRange);

	if (UMovieScene3DTransformTrack* pTrack = AddTrackToActorInLevelSequence<UMovieScene3DTransformTrack>(pCameraActor, levelSequencePath, true, bRes, outMsg))
	{
		auto pSection = AddSectionToActorInLevelSequence<UMovieScene3DTransformTrack, UMovieScene3DTransformSection>(pCameraActor, levelSequencePath, initialRange.GetUpperBoundValue().Value, EMovieSceneBlendType::Absolute, bRes, outMsg);
		ensure(pSection != nullptr && pSection->GetRowIndex() == 0);
	}

	if (UMovieSceneFloatTrack* pTrack = AddTrackToActorInLevelSequence<UMovieSceneFloatTrack>(pCameraActor, levelSequencePath, true, bRes, outMsg))
	{
	#if WITH_EDITOR
		pTrack->SetDisplayName(FText::FromString("DaysDelta"));
	#endif //WITH_EDITOR
		auto pSection = AddSectionToActorInLevelSequence<UMovieSceneFloatTrack, UMovieSceneFloatSection>(pCameraActor, levelSequencePath, initialRange.GetUpperBoundValue().Value, EMovieSceneBlendType::Absolute, bRes, outMsg);
		ensure(pSection != nullptr && pSection->GetRowIndex() == 0);
	}

	return true;
}

float USequencerHelper::AddKeyFrame(ACameraActor* pCameraActor, FString levelSequencePath, FTransform mTransform, float fDaysDelta, float fTime)
{
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	auto iFrameNum = GetTimeFrameNum(pLevelSeq, fTime);

	bool bRes;
	FString outMsg;

	AddTransformKeyFrame(pCameraActor, levelSequencePath, 0 /*iSectionIdx*/, iFrameNum, mTransform, 0 /*iKeyInterp*/, bRes, outMsg);
	AddFloatKeyFrame(pCameraActor, levelSequencePath, 0 /*iSectionIdx*/, iFrameNum, fDaysDelta, 0 /*iKeyInterp*/, bRes, outMsg);

	AdjustMoviePlaybackRange(pCameraActor, levelSequencePath);

	return fTime;
}

void USequencerHelper::RemoveKeyFrame(ACameraActor* pCameraActor, FString levelSequencePath, float fTime)
{
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	auto iFrameNum = GetTimeFrameNum(pLevelSeq, fTime);

	bool bRes;
	FString outMsg;

	RemoveTransformKeyFrame(pCameraActor, levelSequencePath, 0 /*iSectionIdx*/, iFrameNum, bRes, outMsg);
	RemoveFloatKeyFrame(pCameraActor, levelSequencePath, 0 /*iSectionIdx*/, iFrameNum, bRes, outMsg);
	
	AdjustMoviePlaybackRange(pCameraActor, levelSequencePath);
}

void USequencerHelper::AdjustMoviePlaybackRange(ACameraActor* pCameraActor, FString levelSequencePath)
{
	bool bRes;
	FString outMsg;

	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	UMovieScene3DTransformSection* pTransSection = GetSectionFromActorInLevelSequence<UMovieScene3DTransformTrack, UMovieScene3DTransformSection>(pCameraActor, levelSequencePath, 0, bRes, outMsg);
	pLevelSeq->MovieScene->SetPlaybackRange(pTransSection->GetRange());
}

void USequencerHelper::ShiftClipKFsInRange(ACameraActor* pCameraActor, FString levelSequencePath, float fStartTime, float fEndTime, float fDeltaTime)
{
	bool bRes;
	FString outMsg;
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	auto EditedKFRange = GetTimeFrameRange(pLevelSeq, fStartTime, fEndTime);
	auto DeltaFrameNum = GetTimeFrameNum(pLevelSeq, fDeltaTime);
	if (UMovieScene3DTransformSection* pTransSection = GetSectionFromActorInLevelSequence<UMovieScene3DTransformTrack, UMovieScene3DTransformSection>(pCameraActor, levelSequencePath, 0, bRes, outMsg))
		ShiftSectionKFs<FMovieSceneDoubleChannel>(pTransSection, EditedKFRange, DeltaFrameNum);
	if (UMovieSceneFloatSection* pFloatSection = GetSectionFromActorInLevelSequence<UMovieSceneFloatTrack, UMovieSceneFloatSection>(pCameraActor, levelSequencePath, 0, bRes, outMsg))
		ShiftSectionKFs<FMovieSceneFloatChannel>(pFloatSection, EditedKFRange, DeltaFrameNum);
}

void USequencerHelper::ShiftClipKFs(ACameraActor* pCameraActor, FString levelSequencePath, float fDeltaTime)
{
	bool bRes;
	FString outMsg;
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	auto DeltaFrameNum = GetTimeFrameNum(pLevelSeq, fDeltaTime);
	if (UMovieScene3DTransformSection* pTransSection = GetSectionFromActorInLevelSequence<UMovieScene3DTransformTrack, UMovieScene3DTransformSection>(pCameraActor, levelSequencePath, 0, bRes, outMsg))
	{
		auto EditedKFRange = pTransSection->GetRange();
		ShiftSectionKFs<FMovieSceneDoubleChannel>(pTransSection, EditedKFRange, DeltaFrameNum);
	}
	if (UMovieSceneFloatSection* pFloatSection = GetSectionFromActorInLevelSequence<UMovieSceneFloatTrack, UMovieSceneFloatSection>(pCameraActor, levelSequencePath, 0, bRes, outMsg))
	{
		auto EditedKFRange = pFloatSection->GetRange();
		ShiftSectionKFs<FMovieSceneFloatChannel>(pFloatSection, EditedKFRange, DeltaFrameNum);
	}
}

void USequencerHelper::SaveLevelSequenceAsAsset(FString levelSequencePath, const FString& PackagePath)
{
	FString outMsg;
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	if (!pLevelSeq)
	{
		UE_LOG(LogTemp, Warning, TEXT("Level sequence not found"));
		return;
	}

	// Create a new package
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create package."));
		return;
	}

	// Assign the LevelSequence to the package
	FString OldName(pLevelSeq->GetName());
	UObject* OldOuter(pLevelSeq->GetOuter());
	pLevelSeq->Rename(*pLevelSeq->GetName(), Package);

	// Mark the package dirty
	Package->MarkPackageDirty();

	// Save the package to a .uasset file
	FString FilePath = FPaths::ProjectContentDir() + PackagePath.Replace(TEXT("/Game/"), TEXT("")) + TEXT(".uasset");
	//FSavePackageArgs Args;
	//Args.
	bool bSaved = UPackage::SavePackage(
		Package,
		pLevelSeq,
		EObjectFlags::RF_Public | EObjectFlags::RF_Standalone,
		*FilePath,
		GError,
		nullptr,
		false,
		true,
		SAVE_NoError);

	if (bSaved)
	{
		UE_LOG(LogTemp, Log, TEXT("LevelSequence saved to: %s"), *FilePath);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to save LevelSequence"));
	}

	pLevelSeq->Rename(*OldName, OldOuter);
}

