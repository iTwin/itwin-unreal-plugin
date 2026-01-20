/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSequencerHelper.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinSequencerHelper.h"

#include <Compil/BeforeNonUnrealIncludes.h>
#	include "SDK/Core/Tools/Log.h"
#include <Compil/AfterNonUnrealIncludes.h>
#include <optional>
#include <Camera/CameraActor.h>
#include <LevelSequence.h>
#include <UObject/Package.h>
#include <MovieScene.h>
#include <MovieSceneSection.h>
#include <MovieSceneSpawnable.h>
#include <Tracks/MovieSceneFloatTrack.h>
#include <Tracks/MovieSceneDoubleTrack.h>
#include <Tracks/MovieScene3DTransformTrack.h>
#include <Tracks/MovieSceneVectorTrack.h>
#include <Tracks/MovieSceneCameraCutTrack.h>
#include <Sections/MovieSceneFloatSection.h>
#include <Sections/MovieSceneDoubleSection.h>
#include <Sections/MovieScene3DTransformSection.h>
#include <Sections/MovieSceneVectorSection.h>
#include <Sections/MovieSceneCameraCutSection.h>
#include <Channels/MovieSceneChannelProxy.h>
#include <Channels/MovieSceneFloatChannel.h>
#include <Channels/MovieSceneDoubleChannel.h>
#include <Systems/MovieSceneQuaternionBlenderSystem.h>

#include "AssetRegistry/AssetRegistryModule.h"
#include <UObject/SavePackage.h>
//#include "Package.h"
//#include "FileHelpers.h"


namespace {
	
	template<typename T>
	//using SequencerTypeTraits = void;
	struct SequencerTypeTraits
	{
	};

	template<>
	struct SequencerTypeTraits<FVector>
	{
		using TrackType = UMovieSceneFloatVectorTrack;
		using SectionType = UMovieSceneFloatVectorSection;
		using ChannelType = FMovieSceneFloatChannel;
		using ValueType = float;
	};
	template<>
	struct SequencerTypeTraits<FTransform>
	{
		using TrackType = UMovieScene3DTransformTrack;
		using SectionType = UMovieScene3DTransformSection;
		using ChannelType = FMovieSceneDoubleChannel;
		using ValueType = double;
	};
	template<>
	struct SequencerTypeTraits<double>
	{
		using TrackType = UMovieSceneDoubleTrack;
		using SectionType = UMovieSceneDoubleSection;
		using ChannelType = FMovieSceneDoubleChannel;
		using ValueType = double;
	};
	template<>
	struct SequencerTypeTraits<float>
	{
		using TrackType = UMovieSceneFloatTrack;
		using SectionType = UMovieSceneFloatSection;
		using ChannelType = FMovieSceneFloatChannel;
		using ValueType = float;
	};

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

	void ShiftKeyFrameInChannel(FMovieSceneChannel* pChannel, TRange<FFrameNumber>& FrameRange, FFrameNumber iDeltaFrameNum)
	{
		if (pChannel == nullptr)
			return;
		//ULevelSequence* pLevelSeq = Cast<ULevelSequence>(pSection->GetOutermostObject());
		TArray<FFrameNumber> vKeyTimes;
		TArray<FKeyHandle> vKeyHandles;
		pChannel->GetKeys(FrameRange, &vKeyTimes, &vKeyHandles);
		for (size_t i(0); i < vKeyTimes.Num(); i++)
			vKeyTimes[i] += iDeltaFrameNum;
		pChannel->SetKeyTimes(vKeyHandles, vKeyTimes);
	}

	template<class T>
	void ShiftSectionKFs(UMovieSceneSection* pSection, TRange<FFrameNumber> editedKFRange, FFrameNumber deltaFrameNum)
	{
		using Traits = SequencerTypeTraits<T>;

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
			ShiftKeyFrameInChannel(pSection->GetChannelProxy().GetChannel<typename Traits::ChannelType>(i), editedKFRange, deltaFrameNum);
		// adjust frame range to the shifted times
		pSection->SetRange(ComputeRangeFromKFs(pSection));
		pSection->Modify();
	}

	// Find the closest next/previous times to the current time inside a list of keyframe times.
	void GetClosestFrames(const FFrameTime FrameTime, const TArrayView<const FFrameNumber>& InTimes, TRange<FFrameNumber>& OutFrameRange)
	{
		int32 Index1, Index2;
		Index2 = 0;
		Index2 = Algo::UpperBound(InTimes, FrameTime.FrameNumber);
		Index1 = Index2 - 1;
		Index1 = Index1 >= 0 ? Index1 : INDEX_NONE;
		Index2 = Index2 < InTimes.Num() ? Index2 : INDEX_NONE;
		if (Index1 != INDEX_NONE && Index2 != INDEX_NONE)
		{
			if (InTimes[Index1] > OutFrameRange.GetLowerBoundValue())
			{
				OutFrameRange.SetLowerBoundValue(InTimes[Index1]);
			}	
			if (InTimes[Index2] != FrameTime.FrameNumber && InTimes[Index2] < OutFrameRange.GetUpperBoundValue())
			{
				OutFrameRange.SetUpperBoundValue(InTimes[Index2]);
			}
		}
	}

	bool GetRotationValue(UMovieScene3DTransformSection* pSection, FFrameNumber iFrameNum, FRotator &OutValue)
	{
		const FMovieSceneDoubleChannel* RotationX = pSection->GetChannelProxy().GetChannel<FMovieSceneDoubleChannel>(3); //Roll
		const FMovieSceneDoubleChannel* RotationY = pSection->GetChannelProxy().GetChannel<FMovieSceneDoubleChannel>(4); //Pitch
		const FMovieSceneDoubleChannel* RotationZ = pSection->GetChannelProxy().GetChannel<FMovieSceneDoubleChannel>(5); //Yaw
		FFrameTime FrameTime(iFrameNum);
		// Find the closest keyframes before/after the current time
		TRange<FFrameNumber> FrameRange(TNumericLimits<FFrameNumber>::Min(), TNumericLimits<FFrameNumber>::Max());
		if (RotationX)
		{
			GetClosestFrames(FrameTime, RotationX->GetTimes(), FrameRange);
		}
		if (RotationY)
		{
			GetClosestFrames(FrameTime, RotationY->GetTimes(), FrameRange);
		}
		if (RotationZ)
		{
			GetClosestFrames(FrameTime, RotationZ->GetTimes(), FrameRange);
		}

		FVector OutResult(0.0f, 0.0f, 0.0f);
		const FFrameNumber LowerBound = FrameRange.GetLowerBoundValue();
		const FFrameNumber UpperBound = FrameRange.GetUpperBoundValue();
		if (LowerBound != TNumericLimits<FFrameNumber>::Min() && UpperBound != TNumericLimits<FFrameNumber>::Max())
		{
			double Value;
			FVector FirstRot(0.0f, 0.0f, 0.0f);
			FVector SecondRot(0.0f, 0.0f, 0.0f);
			double U = (FrameTime.AsDecimal() - (double)FrameRange.GetLowerBoundValue().Value) /
				double(FrameRange.GetUpperBoundValue().Value - FrameRange.GetLowerBoundValue().Value);
			U = FMath::Clamp(U, 0.0, 1.0);
			if (RotationX)
			{
				if (RotationX->Evaluate(LowerBound, Value))
					FirstRot[0] = Value;
				if (RotationX->Evaluate(UpperBound, Value))
					SecondRot[0] = Value;
			}
			if (RotationY)
			{
				if (RotationY->Evaluate(LowerBound, Value))
					FirstRot[1] = Value;
				if (RotationY->Evaluate(UpperBound, Value))
					SecondRot[1] = Value;
			}
			if (RotationZ)
			{
				if (RotationZ->Evaluate(LowerBound, Value))
					FirstRot[2] = Value;
				if (RotationZ->Evaluate(UpperBound, Value))
					SecondRot[2] = Value;
			}

			const FQuat Key1Quat = FQuat::MakeFromEuler(FirstRot);
			const FQuat Key2Quat = FQuat::MakeFromEuler(SecondRot);
			const FQuat SlerpQuat = FQuat::Slerp(Key1Quat, Key2Quat, U);
			FVector Euler = FRotator(SlerpQuat).Euler();
			if (RotationX)
			{
				OutResult[0] = Euler[0];
			}
			if (RotationY)
			{
				OutResult[1] = Euler[1];
			}
			if (RotationZ)
			{
				OutResult[2] = Euler[2];
			}
		}
		else  // no range found: default to regular, but still do RotToQuat
		{
			double Value;
			FVector CurrentRot(0.0f, 0.0f, 0.0f);
			if (RotationX && RotationX->Evaluate(FrameTime, Value))
			{
				CurrentRot[0] = Value;
			}
			if (RotationY && RotationY->Evaluate(FrameTime, Value))
			{
				CurrentRot[1] = Value;
			}
			if (RotationZ && RotationZ->Evaluate(FrameTime, Value))
			{
				CurrentRot[2] = Value;
			}
			FQuat Quat = FQuat::MakeFromEuler(CurrentRot);
			FVector Euler = FRotator(Quat).Euler();
			if (RotationX)
			{
				OutResult[0] = Euler[0];
			}
			if (RotationY)
			{
				OutResult[1] = Euler[1];
			}
			if (RotationZ)
			{
				OutResult[2] = Euler[2];
			}
		}

		OutValue = FRotator();
		OutValue.Roll = OutResult[0];
		OutValue.Pitch = OutResult[1];
		OutValue.Yaw = OutResult[2];

		return true;
	}

	UMovieSceneSection* GetSectionFromTrack(UMovieSceneTrack* pTrack, int iSectionIdx)
	{
		if (pTrack == nullptr)
			return nullptr;

		auto vSections = pTrack->GetAllSections();
		return (iSectionIdx >= 0 && iSectionIdx < vSections.Num()) ? vSections[iSectionIdx] : nullptr;
	}

	UMovieSceneSection* AddSectionToTrack(UMovieSceneTrack* pTrack, EMovieSceneBlendType InBlendType, FFrameNumber InStartFrame, FFrameNumber InEndFrame)
	{
		if (pTrack == nullptr)
			return nullptr;

		// need to initialize channel count before creating the section, otherwise it will assert and crash
		if (auto pVectorTrack = Cast<UMovieSceneFloatVectorTrack>(pTrack))
			pVectorTrack->SetNumChannelsUsed(3);

		UMovieSceneSection* pSection = pTrack->CreateNewSection();
		if (pSection == nullptr)
			return nullptr;

		pSection->SetRange(TRange<FFrameNumber>((FFrameNumber)InStartFrame, TRangeBound<FFrameNumber>::Inclusive(InEndFrame)));
		pSection->SetBlendType(InBlendType);
		if (auto pTransSection = Cast<UMovieScene3DTransformSection>(pSection))
			pTransSection->SetUseQuaternionInterpolation(true);
		//else if (auto pVectorSection = Cast<UMovieSceneFloatVectorSection>(pSection))
		//	pVectorSection->SetChannelsUsed(3);

		int iRowIdx = -1;
		for (UMovieSceneSection* section : pTrack->GetAllSections())
			iRowIdx = FMath::Max(iRowIdx, section->GetRowIndex());
		pSection->SetRowIndex(iRowIdx + 1);

		pTrack->AddSection(*pSection);
		pTrack->MarkAsChanged();
		pTrack->Modify();

		return pSection;
	}

	bool RemoveSectionFromTrack(UMovieSceneTrack* pTrack, int iSectionIdx)
	{
		auto pSection = GetSectionFromTrack(pTrack, iSectionIdx);
		if (pSection == nullptr)
			return false;

		pTrack->RemoveSection(*pSection);
		pTrack->MarkAsChanged();
		pTrack->Modify();

		return true;
	}

	template<class ChannelType, typename ValueType>
	bool GetValueFromChannel(UMovieSceneSection* pSection, int iChannelIdx, FFrameNumber iFrameNum, ValueType& OutValue)
	{
		if (pSection == nullptr)
			return false;

		auto pChannel = pSection->GetChannelProxy().GetChannel<ChannelType>(iChannelIdx);
		if (pChannel == nullptr)
			return false;

		return pChannel->Evaluate(iFrameNum, OutValue);
	}
	
	template<class ChannelType>
	bool HasKeyFrameInChannel(UMovieSceneSection* pSection, int iChannelIdx, FFrameNumber iFrameNum)
	{
		if (pSection == nullptr)
			return false;

		auto pChannel = pSection->GetChannelProxy().GetChannel<ChannelType>(iChannelIdx);
		if (pChannel == nullptr)
			return false;

		TArray<FFrameNumber> vKeyTimes;
		TArray<FKeyHandle> vKeyHandles;
		pChannel->GetKeys(TRange<FFrameNumber>(iFrameNum, iFrameNum), &vKeyTimes, &vKeyHandles);
		return vKeyHandles.Num() > 0;
	}

	template<class ChannelType, typename ValueType>
	bool AddKeyFrameToChannel(UMovieSceneSection* pSection, int iChannelIdx, FFrameNumber iFrameNum, const ValueType& Value, int iKeyInterp)
	{
		if (pSection == nullptr)
			return false;

		auto pChannel = pSection->GetChannelProxy().GetChannel<ChannelType>(iChannelIdx);
		if (pChannel == nullptr)
			return false;
		
		// if a key-frame already exists at the given time, delete it
		TArray<FFrameNumber> vKeyTimes;
		TArray<FKeyHandle> vKeyHandles;
		pChannel->GetKeys(TRange<FFrameNumber>(iFrameNum, iFrameNum), &vKeyTimes, &vKeyHandles);
		pChannel->DeleteKeys(vKeyHandles);

		switch (iKeyInterp)
		{
		case 0: pChannel->AddCubicKey(iFrameNum, Value); break;
		case 1: pChannel->AddLinearKey(iFrameNum, Value); break;
		default: pChannel->AddConstantKey(iFrameNum, Value); break;
		}
		pSection->Modify();

		return true;
	}

	template<typename T>
	bool AddKeyFrameToSection(UMovieSceneSection* pSection, FFrameNumber iFrameNum, const T& Value, int iKeyInterp)
	{
		using Traits = SequencerTypeTraits<T>;
		return AddKeyFrameToChannel<typename Traits::ChannelType, typename Traits::ValueType>(pSection, 0, iFrameNum, Value, iKeyInterp);
	}

	template<>
	bool AddKeyFrameToSection<FTransform>(UMovieSceneSection* pSection, FFrameNumber iFrameNum, const FTransform& Value, int iKeyInterp)
	{
		using Traits = SequencerTypeTraits<FTransform>;

		int outRes(0);

		outRes += AddKeyFrameToChannel<typename Traits::ChannelType, typename Traits::ValueType>(pSection, 0, iFrameNum, Value.GetLocation().X, iKeyInterp);
		outRes += AddKeyFrameToChannel<typename Traits::ChannelType, typename Traits::ValueType>(pSection, 1, iFrameNum, Value.GetLocation().Y, iKeyInterp);
		outRes += AddKeyFrameToChannel<typename Traits::ChannelType, typename Traits::ValueType>(pSection, 2, iFrameNum, Value.GetLocation().Z, iKeyInterp);

		outRes += AddKeyFrameToChannel<typename Traits::ChannelType, typename Traits::ValueType>(pSection, 3, iFrameNum, Value.Rotator().Roll, iKeyInterp);
		outRes += AddKeyFrameToChannel<typename Traits::ChannelType, typename Traits::ValueType>(pSection, 4, iFrameNum, Value.Rotator().Pitch, iKeyInterp);
		outRes += AddKeyFrameToChannel<typename Traits::ChannelType, typename Traits::ValueType>(pSection, 5, iFrameNum, Value.Rotator().Yaw, iKeyInterp);

		outRes += AddKeyFrameToChannel<typename Traits::ChannelType, typename Traits::ValueType>(pSection, 6, iFrameNum, Value.GetScale3D().X, iKeyInterp);
		outRes += AddKeyFrameToChannel<typename Traits::ChannelType, typename Traits::ValueType>(pSection, 7, iFrameNum, Value.GetScale3D().Y, iKeyInterp);
		outRes += AddKeyFrameToChannel<typename Traits::ChannelType, typename Traits::ValueType>(pSection, 8, iFrameNum, Value.GetScale3D().Z, iKeyInterp);

		return outRes >= 9;
	}

	template<>
	bool AddKeyFrameToSection<FVector>(UMovieSceneSection* pSection, FFrameNumber iFrameNum, const FVector& Value, int iKeyInterp)
	{
		using Traits = SequencerTypeTraits<FVector>;

		int outRes(0);

		outRes += AddKeyFrameToChannel<typename Traits::ChannelType, typename Traits::ValueType>(pSection, 0, iFrameNum, Value.X, iKeyInterp);
		outRes += AddKeyFrameToChannel<typename Traits::ChannelType, typename Traits::ValueType>(pSection, 1, iFrameNum, Value.Y, iKeyInterp);
		outRes += AddKeyFrameToChannel<typename Traits::ChannelType, typename Traits::ValueType>(pSection, 2, iFrameNum, Value.Z, iKeyInterp);

		return outRes >= 3;
	}
	
	template<class ChannelType>
	bool RemoveKeyFrameFromChannel(UMovieSceneSection* pSection, int iChannelIdx, FFrameNumber iFrameNum)
	{
		if (pSection == nullptr)
			return false;

		auto pChannel = pSection->GetChannelProxy().GetChannel<ChannelType>(iChannelIdx);
		if (pChannel == nullptr)
			return false;

		TArray<FFrameNumber> vKeyTimes;
		TArray<FKeyHandle> vKeyHandles;
		pChannel->GetKeys(TRange<FFrameNumber>(iFrameNum, iFrameNum), &vKeyTimes, &vKeyHandles);
		pChannel->DeleteKeys(vKeyHandles);
		pSection->Modify();

		return true;
	}
	
	template<typename T>
	bool RemoveKeyFrameFromSection(UMovieSceneSection* pSection, FFrameNumber iFrameNum)
	{
		using Traits = SequencerTypeTraits<T>;
		
		if (pSection == nullptr)
			return false;

		int outRes(0);
		for (int32 i(0); i < pSection->GetChannelProxy().NumChannels(); i++)
			outRes += RemoveKeyFrameFromChannel<typename Traits::ChannelType>(pSection, i, iFrameNum);
		return outRes >= pSection->GetChannelProxy().NumChannels();
	}
	
	template<typename T>
	bool GetKeyFrameValue(UMovieSceneSection* pSection, FFrameNumber iFrameNum, T& OutValue)
	{
		using Traits = SequencerTypeTraits<T>;
		return GetValueFromChannel<typename Traits::ChannelType, typename Traits::ValueType>(pSection, 0, iFrameNum, OutValue);
	}
	
	template<>
	bool GetKeyFrameValue<FVector>(UMovieSceneSection* pSection, FFrameNumber iFrameNum, FVector& OutValue)
	{
		using Traits = SequencerTypeTraits<FVector>;

		int outRes(0);

		float x,y,z;
		outRes += GetValueFromChannel<typename Traits::ChannelType, float>(pSection, 0, iFrameNum, x);//OutValue.X);
		outRes += GetValueFromChannel<typename Traits::ChannelType, typename Traits::ValueType>(pSection, 1, iFrameNum, y);//OutValue.Y);
		outRes += GetValueFromChannel<typename Traits::ChannelType, typename Traits::ValueType>(pSection, 2, iFrameNum, z);//OutValue.Z);

		return outRes >= 3;
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
		outInfoMsg = GetErrorMsg(fname, "Invalid actor pointer");
		return FGuid();
	}
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	if (pLevelSeq == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Level sequence not found");
		return FGuid();
	}
	FGuid guid;
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (pActor && pActor->GetWorld())
		guid = pLevelSeq->FindBindingFromObject(pActor, pActor->GetWorld());
PRAGMA_ENABLE_DEPRECATION_WARNINGS
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


template<class TrackType>
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
	TrackType* pTrack = pLevelSeq->MovieScene->FindTrack<TrackType>(guid);//, TrackName);
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

	if (auto pTransTrack = Cast<UMovieScene3DTransformTrack>((UMovieScenePropertyTrack*)pTrack))
		pTransTrack->SetBlenderSystem(UMovieSceneQuaternionBlenderSystem::StaticClass());

	bOutSuccess = (pTrack != nullptr);
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

void USequencerHelper::RemoveAllTracksFromLevelSequence(AActor* pActor, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "RemoveAllTracksFromLevelSequence";
	bOutSuccess = false;

	FGuid guid = GetPActorGuidFromLevelSequence(pActor, levelSequencePath, bOutSuccess, outInfoMsg);
	if (!guid.IsValid())
	{
		outInfoMsg = GetErrorMsg(fname, "Actor not found in level sequence");
		return;
	}

	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	auto Tracks = pLevelSeq->MovieScene->FindTracks(UMovieSceneTrack::StaticClass(), guid);
	for (auto Track : Tracks)
	{
		pLevelSeq->MovieScene->RemoveTrack(*Track);
	}

	bOutSuccess = true;
	outInfoMsg = GetOutMsg(true, fname);
}


template<class TrackType>
UMovieSceneSection* USequencerHelper::GetSectionFromActorInLevelSequence(AActor* pActor, FString levelSequencePath, int iSectionIdx, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "GetSectionFromActorInLevelSequence";
	bOutSuccess = false;

	auto pTrack = GetTrackFromActorInLevelSequence<TrackType>(pActor, levelSequencePath, bOutSuccess, outInfoMsg);
	if (pTrack == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Track of this type not found for the actor");
		return nullptr;
	}

	auto pSection = GetSectionFromTrack(pTrack, iSectionIdx);
	if (pSection == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Section with given type and index not found in the track");
		return nullptr;
	}

	bOutSuccess = true;
	outInfoMsg = GetOutMsg(true, fname);
	return pSection;
}

template<class TrackType>
UMovieSceneSection* USequencerHelper::AddSectionToActorInLevelSequence(AActor* pActor, FString levelSequencePath, FFrameNumber FrameNum, EMovieSceneBlendType eBlendType, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "AddSectionToActorInLevelSequence";
	bOutSuccess = false;

	auto pTrack = GetTrackFromActorInLevelSequence<TrackType>(pActor, levelSequencePath, bOutSuccess, outInfoMsg);
	if (pTrack == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Track of this type not found for the actor");
		return nullptr;
	}

	auto pSection = AddSectionToTrack(pTrack, eBlendType, 0, FrameNum);
	if (pSection == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Failed to create new section");
		return nullptr;
	}

	bOutSuccess = true;
	outInfoMsg = GetOutMsg(true, fname);
	return pSection;
}

template<class TrackType>
void USequencerHelper::RemoveSectionFromActorInLevelSequence(AActor* pActor, FString levelSequencePath, int iSectionIdx, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "RemoveSectionFromActorInLevelSequence";
	bOutSuccess = false;

	auto pTrack = GetTrackFromActorInLevelSequence<TrackType>(pActor, levelSequencePath, bOutSuccess, outInfoMsg);
	if (pTrack == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Track of this type not found for the actor");
		return;
	}

	if (!RemoveSectionFromTrack(pTrack, iSectionIdx))
	{
		outInfoMsg = GetErrorMsg(fname, "Failed to remove section");
		return;
	}

	bOutSuccess = true;
	outInfoMsg = GetOutMsg(true, fname);
}


template<typename T>
FFrameNumberRange USequencerHelper::AddKeyFrameToActor(AActor* pActor, FString levelSequencePath, int iSectionIdx, FFrameNumber iFrameNum, const T &Value, int iKeyInterp, bool& bOutSuccess, FString& outInfoMsg)
{
	using Traits = SequencerTypeTraits<T>;

	FString fname = "AddKeyFrameToActor";
	bOutSuccess = false;

	auto pTrack = GetTrackFromActorInLevelSequence<typename Traits::TrackType>(pActor, levelSequencePath, bOutSuccess, outInfoMsg);
	if (pTrack == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Track of this type not found for the actor");
		return FFrameNumberRange();
	}

	return AddKeyFrameToTrack<T>(pTrack, iSectionIdx, iFrameNum, Value, iKeyInterp, bOutSuccess, outInfoMsg);
}

template<typename T>
FFrameNumberRange USequencerHelper::AddKeyFrameToTrack(UMovieSceneTrack* pTrack, int iSectionIdx, FFrameNumber iFrameNum, const T &Value, int iKeyInterp, bool& bOutSuccess, FString& outInfoMsg)
{
	using Traits = SequencerTypeTraits<T>;

	FString fname = "AddKeyFrameToTrack";
	bOutSuccess = false;

	auto pSection = GetSectionFromTrack(pTrack, iSectionIdx);
	if (pSection == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Section not found in the track");
		return FFrameNumberRange();
	}

	if (!AddKeyFrameToSection<T>(pSection, iFrameNum, Value, iKeyInterp))
	{
		outInfoMsg = GetErrorMsg(fname, "Failed to add key-frame");
		return FFrameNumberRange();
	}

	if (iFrameNum > pSection->GetRange().GetUpperBoundValue())
	{
		auto newRange = TRange<FFrameNumber>((FFrameNumber)0, TRangeBound<FFrameNumber>::Inclusive(iFrameNum));
		pSection->SetRange(newRange);
	}

	pSection->Modify();
	pTrack->MarkAsChanged(); //Cast<TrackType>(pSection->GetOuter())->MarkAsChanged();

	bOutSuccess = true;
	outInfoMsg = GetOutMsg(true, fname);

	return pSection->GetRange();
}

template<typename T>
FFrameNumberRange USequencerHelper::RemoveKeyFrameFromActor(AActor* pActor, FString levelSequencePath, int iSectionIdx, FFrameNumber iFrameNum, bool& bOutSuccess, FString& outInfoMsg)
{
	using Traits = SequencerTypeTraits<T>;

	FString fname = "RemoveKeyFrameFromActor";
	bOutSuccess = false;

	auto pTrack = GetTrackFromActorInLevelSequence<typename Traits::TrackType>(pActor, levelSequencePath, bOutSuccess, outInfoMsg);
	if (pTrack == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Track of this type not found for the actor");
		return FFrameNumberRange();
	}

	return RemoveKeyFrameFromTrack<T>(pTrack, iSectionIdx, iFrameNum, bOutSuccess, outInfoMsg);
}

template<typename T>
FFrameNumberRange USequencerHelper::RemoveKeyFrameFromTrack(UMovieSceneTrack* pTrack, int iSectionIdx, FFrameNumber iFrameNum, bool& bOutSuccess, FString& outInfoMsg)
{
	using Traits = SequencerTypeTraits<T>;

	FString fname = "RemoveKeyFrameFromTrack";
	bOutSuccess = false;

	auto pSection = GetSectionFromTrack(pTrack, iSectionIdx);
	if (pSection == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Section not found in the track");
		return FFrameNumberRange();
	}

	if (!RemoveKeyFrameFromSection<T>(pSection, iFrameNum))
	{
		outInfoMsg = GetErrorMsg(fname, "Failed to remove key-frame");
		return FFrameNumberRange();
	}

	auto newRange = ComputeRangeFromKFs(pSection);
	pSection->SetRange(newRange);
	pSection->Modify();

	bOutSuccess = true;
	outInfoMsg = GetOutMsg(true, fname);

	return pSection->GetRange();
}


bool USequencerHelper::GetTransformAtTime(AActor* pActor, FString levelSequencePath, float fTime, FVector& pos, FRotator& rot)
{
	bool bRes;
	FString outMsg;
	auto pTransSection = Cast<UMovieScene3DTransformSection>(GetSectionFromActorInLevelSequence<UMovieScene3DTransformTrack>(pActor, levelSequencePath, 0, bRes, outMsg));
	if (pTransSection == nullptr)
		return false;

	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	auto iFrameNum = GetTimeFrameNum(pLevelSeq, fTime);
	//if (!pLevelSeq->MovieScene->GetPlaybackRange().Contains(iFrameNum))
	//	return false;

	// When interpolating rotation angles, there's an issue due to angle clamping to (-180,180): despite referring to the same angle,
	// -180 -> 180 cause camera to make a full round (see UE::MovieScene::Interpolation::FCubicInterpolation::Evaluate()).
	// There's a special interpolation system based on quaternions in Unreal that solves the issue (see UE::MovieScene::FEvaluateQuaternionInterpolationRotationChannels),
	// it is activated via TransformSection->SetUseQuaternionInterpolation(true) and TransformTrack->SetBlenderSystem(UMovieSceneQuaternionBlenderSystem::StaticClass()).
	// However it only works with animation playback and video export, i didn't find a built-in way to get the correctly interpolated rotation value for a given time.
	// Therefore we use a special method for rotation here, inspired by FEvaluateQuaternionInterpolationRotationChannels.
	GetValueFromChannel<FMovieSceneDoubleChannel, double>(pTransSection, 0, iFrameNum, pos.X);
	GetValueFromChannel<FMovieSceneDoubleChannel, double>(pTransSection, 1, iFrameNum, pos.Y);
	GetValueFromChannel<FMovieSceneDoubleChannel, double>(pTransSection, 2, iFrameNum, pos.Z);
	GetRotationValue(pTransSection, iFrameNum, rot);
	//FVector scale;
	//scale.X = GetValueFromDoubleChannel(pTransSection, 6, iFrameNum);
	//scale.Y = GetValueFromDoubleChannel(pTransSection, 7, iFrameNum);
	//scale.Z = GetValueFromDoubleChannel(pTransSection, 8, iFrameNum);

	return true;
}

template<typename T>
bool USequencerHelper::GetActorValueAtTime(AActor* pActor, FString levelSequencePath, float fTime, T& OutValue)
{
	using Traits = SequencerTypeTraits<T>;

	bool bRes;
	FString outMsg;

	auto pTrack = GetTrackFromActorInLevelSequence<typename Traits::TrackType>(pActor, levelSequencePath, bRes, outMsg);
	if (pTrack == nullptr)
		return false;

	return GetTrackValueAtTime<T>(pTrack, levelSequencePath, fTime, OutValue);
}

template<typename T>
bool USequencerHelper::GetTrackValueAtTime(UMovieSceneTrack* pTrack, FString levelSequencePath, float fTime, T& OutValue)
{
	using Traits = SequencerTypeTraits<T>;

	auto pSection = GetSectionFromTrack(pTrack, 0);
	if (pSection == nullptr)
		return false;

	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	auto iFrameNum = GetTimeFrameNum(pLevelSeq, fTime);
	//if (!pLevelSeq->MovieScene->GetPlaybackRange().Contains(iFrameNum))
	//	return false;

	return GetKeyFrameValue<T>(pSection, iFrameNum, OutValue);
}

bool USequencerHelper::GetFloatValueAtTime(UMovieSceneTrack* pTrack, FString levelSequencePath, float fTime, float& OutValue)
{
	return GetTrackValueAtTime<float>(pTrack, levelSequencePath, fTime, OutValue);
}

bool USequencerHelper::GetDoubleValueAtTime(UMovieSceneTrack* pTrack, FString levelSequencePath, float fTime, double& OutValue)
{
	return GetTrackValueAtTime<double>(pTrack, levelSequencePath, fTime, OutValue);
}

bool USequencerHelper::HasTransformKeyFrame(AActor* pActor, FString levelSequencePath, int iSectionIdx, FFrameNumber iFrameNum, bool& bOutSuccess, FString& outInfoMsg)
{
	FString fname = "ActorHasTransformKeyFrame";
	bOutSuccess = false;

	auto pTrack = GetTrackFromActorInLevelSequence<UMovieScene3DTransformTrack>(pActor, levelSequencePath, bOutSuccess, outInfoMsg);
	if (pTrack == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Track of this type not found for the actor");
		return false;
	}

	auto pSection = GetSectionFromTrack(pTrack, iSectionIdx);
	if (pSection == nullptr)
	{
		outInfoMsg = GetErrorMsg(fname, "Section not found in the track");
		return false;
	}

	return HasKeyFrameInChannel<FMovieSceneDoubleChannel>(pSection, 0, iFrameNum);
}

float USequencerHelper::GetDuration(AActor* pActor, FString levelSequencePath)
{
	bool bRes;
	FString outMsg;
	auto pTransSection = GetSectionFromActorInLevelSequence<UMovieScene3DTransformTrack>(pActor, levelSequencePath, 0, bRes, outMsg);
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
	auto pTransSection = GetSectionFromActorInLevelSequence<UMovieScene3DTransformTrack>(pActor, levelSequencePath, 0, bRes, outMsg);
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
	auto pTransSection = GetSectionFromActorInLevelSequence<UMovieScene3DTransformTrack>(pActor, levelSequencePath, 0, bRes, outMsg);
	if (pTransSection == nullptr)
		return 0.f;
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	auto rangeKFs = pTransSection->GetRange();// GetRangeFromKFs(pTransSection);
	return (float)(rangeKFs.GetUpperBoundValue().Value) / (float)(pLevelSeq->MovieScene->GetTickResolution().AsDecimal());
}

void USequencerHelper::AdjustMoviePlaybackRange(ACameraActor* pCameraActor, FString levelSequencePath)
{
	bool bRes;
	FString outMsg;

	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	auto pTransSection = GetSectionFromActorInLevelSequence<UMovieScene3DTransformTrack>(pCameraActor, levelSequencePath, 0, bRes, outMsg);
	pLevelSeq->MovieScene->SetPlaybackRange(pTransSection->GetRange());
}

float USequencerHelper::GetPlaybackEndTime(FString levelSequencePath)
{
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	return pLevelSeq ? pLevelSeq->MovieScene->GetPlaybackRange().GetUpperBoundValue().Value / pLevelSeq->MovieScene->GetTickResolution().AsDecimal() : 0.f;
}


bool USequencerHelper::HasCameraCutTrack(FString levelSequencePath)
{
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	return (pLevelSeq != nullptr) && (Cast<UMovieSceneCameraCutTrack>(pLevelSeq->MovieScene->GetCameraCutTrack()) != nullptr);
}

ACameraActor* USequencerHelper::GetCameraCutBoundCamera(FString levelSequencePath, FFrameTime PlaybackTime)
{
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	if (auto CameraCutTrack = Cast<UMovieSceneCameraCutTrack>(pLevelSeq->MovieScene->GetCameraCutTrack()))
	{
		TArray<UMovieSceneSection*> Sections = CameraCutTrack->GetAllSections();
		for (UMovieSceneSection* Section : Sections)
		{
			if (Section->IsActive() && Section->IsTimeWithinSection(PlaybackTime.FrameNumber))
			{
				if (UMovieSceneCameraCutSection* CameraCutSection = Cast<UMovieSceneCameraCutSection>(Section))
				{
					// Return the camera bound to this section
					if (FMovieSceneSpawnable* pSpawnable = pLevelSeq->MovieScene->FindSpawnable(CameraCutSection->GetCameraBindingID().GetGuid()))
						return Cast<ACameraActor>(pSpawnable->GetObjectTemplate());
					//return CameraCutSection->GetCameraBindingID().GetGuid().ResolveBoundObjects();
				}
			}
		}
	}

	return nullptr;
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


bool USequencerHelper::AddNewClipOld(ACameraActor* pCameraActor, FString levelSequencePath)
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
		pTrack->SetBlenderSystem(UMovieSceneQuaternionBlenderSystem::StaticClass());
		auto pSection = AddSectionToActorInLevelSequence<UMovieScene3DTransformTrack>(pCameraActor, levelSequencePath, initialRange.GetUpperBoundValue().Value, EMovieSceneBlendType::Absolute, bRes, outMsg);
		ensure(pSection != nullptr && pSection->GetRowIndex() == 0);
	}

	if (UMovieSceneDoubleTrack* pTrack = AddTrackToActorInLevelSequence<UMovieSceneDoubleTrack>(pCameraActor, levelSequencePath, true, bRes, outMsg))
	{
	#if WITH_EDITOR
		pTrack->SetDisplayName(FText::FromString("DateDelta"));
	#endif //WITH_EDITOR
		auto pSection = AddSectionToActorInLevelSequence<UMovieSceneDoubleTrack>(pCameraActor, levelSequencePath, initialRange.GetUpperBoundValue().Value, EMovieSceneBlendType::Absolute, bRes, outMsg);
		ensure(pSection != nullptr && pSection->GetRowIndex() == 0);
	}

	return true;
}

template<class TrackType>
UMovieSceneTrack* USequencerHelper::CreateTrackForParameter(ACameraActor* pCameraActor, FString levelSequencePath, const TRange<FFrameNumber>& initialRange, FName sTrackName, bool& bRes, FString& outMsg)
{
	if (auto pTrack = AddTrackToActorInLevelSequence<TrackType>(pCameraActor, levelSequencePath, true /*bOverwriteExisting*/, bRes, outMsg))
	{
		auto pSection = AddSectionToTrack(pTrack, EMovieSceneBlendType::Absolute, (FFrameNumber)0, initialRange.GetUpperBoundValue().Value);
		ensure(pSection != nullptr && pSection->GetRowIndex() == 0);
#if WITH_EDITOR
		pTrack->SetDisplayName(FText::FromName(sTrackName));
#endif //WITH_EDITOR
		return pTrack;
	}
	return nullptr;
}

bool USequencerHelper::AddNewClip(ACameraActor* pCameraActor, FString levelSequencePath, const TArray<USequencerHelper::FTrackInfo> &AnimParams, TArray<TStrongObjectPtr<UMovieSceneTrack> >& OutTracks)
{
	bool bRes;
	FString outMsg;

	FGuid guid = AddPActorToLevelSequence(pCameraActor, levelSequencePath, bRes, outMsg);
	if (!bRes)
		return false;

	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	auto initialRange = GetTimeFrameRange(pLevelSeq, 0, 1.f/*arbitrary small value to create a non-zero section*/);
	//pLevelSeq->MovieScene->SetPlaybackRange(initialRange);

	OutTracks.Empty();
	for (size_t i(0); i < AnimParams.Num(); i++)
	{
		auto &Info = AnimParams[i];
		UMovieSceneTrack* pTrack(NULL);
		switch (Info.eType)
		{
		case ETrackType::TT_Transform:
			pTrack = CreateTrackForParameter<UMovieScene3DTransformTrack>(pCameraActor, levelSequencePath, initialRange, Info.sName, bRes, outMsg);
			break;
		case ETrackType::TT_Vector:
			pTrack = CreateTrackForParameter<UMovieSceneFloatVectorTrack>(pCameraActor, levelSequencePath, initialRange, Info.sName, bRes, outMsg);
			break;
		case ETrackType::TT_Double:
			pTrack = CreateTrackForParameter<UMovieSceneDoubleTrack>(pCameraActor, levelSequencePath, initialRange, Info.sName, bRes, outMsg);
			break;
		case ETrackType::TT_Float:
			pTrack = CreateTrackForParameter<UMovieSceneFloatTrack>(pCameraActor, levelSequencePath, initialRange, Info.sName, bRes, outMsg);
			break;
		default:
			break;
		}

		OutTracks.Add(TStrongObjectPtr<UMovieSceneTrack>(pTrack));
	}

	return true;
}

float USequencerHelper::AddKeyFrameOld(ACameraActor* pCameraActor, FString levelSequencePath, FTransform mTransform, double DaysDelta, float fTime)
{
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	auto iFrameNum = GetTimeFrameNum(pLevelSeq, fTime);

	bool bRes;
	FString outMsg;

	AddKeyFrameToActor<FTransform>(pCameraActor, levelSequencePath, 0 /*iSectionIdx*/, iFrameNum, mTransform, 0 /*iKeyInterp*/, bRes, outMsg);
	AddKeyFrameToActor<double>(pCameraActor, levelSequencePath, 0 /*iSectionIdx*/, iFrameNum, DaysDelta, 0 /*iKeyInterp*/, bRes, outMsg);

	AdjustMoviePlaybackRange(pCameraActor, levelSequencePath);

	return fTime;
}

void USequencerHelper::RemoveKeyFrameOld(ACameraActor* pCameraActor, FString levelSequencePath, float fTime)
{
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	auto iFrameNum = GetTimeFrameNum(pLevelSeq, fTime);

	bool bRes;
	FString outMsg;

	RemoveKeyFrameFromActor<FTransform>(pCameraActor, levelSequencePath, 0 /*iSectionIdx*/, iFrameNum, bRes, outMsg);
	RemoveKeyFrameFromActor<double>(pCameraActor, levelSequencePath, 0 /*iSectionIdx*/, iFrameNum, bRes, outMsg);
	
	AdjustMoviePlaybackRange(pCameraActor, levelSequencePath);
}

float USequencerHelper::AddKeyFrame(TArray<TStrongObjectPtr<UMovieSceneTrack> >& Tracks, FString levelSequencePath, float fTime, TArray<std::optional<USequencerHelper::KFValueType> > &Values)
{
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	auto iFrameNum = GetTimeFrameNum(pLevelSeq, fTime);

	bool bRes;
	FString outMsg;

	int iSectionIdx(0); // we use only one section in each track
	int iKeyInterp(0);

	ensure(Tracks.Num() == Values.Num());

	for (size_t i(0); i < Tracks.Num(); i++)
	{
		if (!Tracks[i] || !Values[i].has_value())
			continue;
		UMovieSceneTrack* pTrack = Tracks[i].Get();
		if  (pTrack->GetClass()->GetFName().Compare(TEXT("MovieScene3DTransformTrack")) == 0)
			AddKeyFrameToTrack<FTransform>(pTrack, iSectionIdx, iFrameNum, std::get<FTransform>(Values[i].value()), iKeyInterp, bRes, outMsg);
		else if (pTrack->GetClass()->GetFName().Compare(TEXT("MovieSceneFloatVectorTrack")) == 0)
			AddKeyFrameToTrack<FVector>(pTrack, iSectionIdx, iFrameNum, std::get<FVector>(Values[i].value()), iKeyInterp, bRes, outMsg);
		else if (pTrack->GetClass()->GetFName().Compare(TEXT("MovieSceneDoubleTrack")) == 0)
			AddKeyFrameToTrack<double>(pTrack, iSectionIdx, iFrameNum, std::get<double>(Values[i].value()), iKeyInterp, bRes, outMsg);
		else if (pTrack->GetClass()->GetFName().Compare(TEXT("MovieSceneFloatTrack")) == 0)
			AddKeyFrameToTrack<float>(pTrack, iSectionIdx, iFrameNum, std::get<float>(Values[i].value()), iKeyInterp, bRes, outMsg);
	}

	return fTime;
}

void USequencerHelper::RemoveKeyFrame(TArray<TStrongObjectPtr<UMovieSceneTrack> >& Tracks, FString levelSequencePath, float fTime)
{
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	auto iFrameNum = GetTimeFrameNum(pLevelSeq, fTime);

	bool bRes;
	FString outMsg;

	int iSectionIdx(0); // we use only one section in each track

	for (size_t i(0); i < Tracks.Num(); i++)
	{
		if (!Tracks[i])
			continue;
		UMovieSceneTrack* pTrack = Tracks[i].Get();
		if (pTrack->GetClass()->GetFName().Compare(TEXT("MovieScene3DTransformTrack")) == 0)
			RemoveKeyFrameFromTrack<FTransform>(pTrack, iSectionIdx, iFrameNum, bRes, outMsg);
		else if (pTrack->GetClass()->GetFName().Compare(TEXT("MovieSceneFloatVectorTrack")) == 0)
			RemoveKeyFrameFromTrack<FVector>(pTrack, iSectionIdx, iFrameNum, bRes, outMsg);
		else if (pTrack->GetClass()->GetFName().Compare(TEXT("MovieSceneDoubleTrack")) == 0)
			RemoveKeyFrameFromTrack<double>(pTrack, iSectionIdx, iFrameNum, bRes, outMsg);
		else if (pTrack->GetClass()->GetFName().Compare(TEXT("MovieSceneFloatTrack")) == 0)
			RemoveKeyFrameFromTrack<float>(pTrack, iSectionIdx, iFrameNum, bRes, outMsg);
	}
}

void USequencerHelper::ShiftClipKFsInRangeOld(ACameraActor* pCameraActor, FString levelSequencePath, float fStartTime, float fEndTime, float fDeltaTime)
{
	bool bRes;
	FString outMsg;
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	auto EditedKFRange = GetTimeFrameRange(pLevelSeq, fStartTime, fEndTime);
	auto DeltaFrameNum = GetTimeFrameNum(pLevelSeq, fDeltaTime);
	if (auto pTransSection = GetSectionFromActorInLevelSequence<UMovieScene3DTransformTrack>(pCameraActor, levelSequencePath, 0, bRes, outMsg))
		ShiftSectionKFs<FTransform>(pTransSection, EditedKFRange, DeltaFrameNum);
	if (auto pDateSection = GetSectionFromActorInLevelSequence<UMovieSceneDoubleTrack>(pCameraActor, levelSequencePath, 0, bRes, outMsg))
		ShiftSectionKFs<double>(pDateSection, EditedKFRange, DeltaFrameNum);
}

void USequencerHelper::ShiftClipKFsOld(ACameraActor* pCameraActor, FString levelSequencePath, float fDeltaTime)
{
	bool bRes;
	FString outMsg;
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	auto DeltaFrameNum = GetTimeFrameNum(pLevelSeq, fDeltaTime);
	if (auto pTransSection = GetSectionFromActorInLevelSequence<UMovieScene3DTransformTrack>(pCameraActor, levelSequencePath, 0, bRes, outMsg))
		ShiftSectionKFs<FTransform>(pTransSection, pTransSection->GetRange(), DeltaFrameNum);
	if (auto pDateSection = GetSectionFromActorInLevelSequence<UMovieSceneDoubleTrack>(pCameraActor, levelSequencePath, 0, bRes, outMsg))
		ShiftSectionKFs<double>(pDateSection, pDateSection->GetRange(), DeltaFrameNum);
}

namespace {
	void DoShiftClipKFs(TArray<TStrongObjectPtr<UMovieSceneTrack> >& Tracks, const FFrameNumber &InDeltaFrameNum, std::optional< TRange<FFrameNumber> > InKFRange = std::nullopt)
	{
		for (size_t i(0); i < Tracks.Num(); i++)
		{
			if (!Tracks[i])
				continue;
			UMovieSceneTrack* pTrack = Tracks[i].Get();
			auto pSection = GetSectionFromTrack(pTrack, 0);
			TRange<FFrameNumber> KFRange = InKFRange.has_value() ? *InKFRange : pSection->GetRange();
			if (pTrack->GetClass()->GetFName().Compare(TEXT("MovieScene3DTransformTrack")) == 0)
				ShiftSectionKFs<FTransform>(pSection, KFRange, InDeltaFrameNum);
			else if (pTrack->GetClass()->GetFName().Compare(TEXT("MovieSceneFloatVectorTrack")) == 0)
				ShiftSectionKFs<FVector>(pSection, KFRange, InDeltaFrameNum);
			else if (pTrack->GetClass()->GetFName().Compare(TEXT("MovieSceneDoubleTrack")) == 0)
				ShiftSectionKFs<double>(pSection, KFRange, InDeltaFrameNum);
			else if (pTrack->GetClass()->GetFName().Compare(TEXT("MovieSceneFloatTrack")) == 0)
				ShiftSectionKFs<float>(pSection, KFRange, InDeltaFrameNum);
		}
	}
}

void USequencerHelper::ShiftClipKFsInRange(TArray<TStrongObjectPtr<UMovieSceneTrack> >& Tracks, FString levelSequencePath, float fStartTime, float fEndTime, float fDeltaTime)
{
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	auto EditedKFRange = GetTimeFrameRange(pLevelSeq, fStartTime, fEndTime);
	auto DeltaFrameNum = GetTimeFrameNum(pLevelSeq, fDeltaTime);

	DoShiftClipKFs(Tracks, DeltaFrameNum, EditedKFRange);
}

void USequencerHelper::ShiftClipKFs(TArray<TStrongObjectPtr<UMovieSceneTrack> >& Tracks, FString levelSequencePath, float fDeltaTime)
{
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	auto DeltaFrameNum = GetTimeFrameNum(pLevelSeq, fDeltaTime);

	DoShiftClipKFs(Tracks, DeltaFrameNum);
}


void USequencerHelper::SaveLevelSequenceAsAsset(FString levelSequencePath, const FString& PackagePath)
{
	FString outMsg;
	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath));
	if (!pLevelSeq)
	{
		BE_LOGW("Timeline", "Level sequence not found");
		return;
	}

	// Create a new package
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		BE_LOGE("Timeline", "Failed to create package.");
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

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = EObjectFlags::RF_Public | EObjectFlags::RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;

	bool bSaved = UPackage::SavePackage(Package, pLevelSeq, *FilePath, SaveArgs);
	if (bSaved)
	{
		BE_LOGI("Timeline", "LevelSequence saved to: " << TCHAR_TO_UTF8(*FilePath));
	}
	else
	{
		BE_LOGE("Timeline", "Failed to save LevelSequence");
	}

	pLevelSeq->Rename(*OldName, OldOuter);
}
