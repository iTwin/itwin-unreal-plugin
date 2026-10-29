/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSequencerHelper.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinSequencerHelper.h"
#include "ITwinSequencerHelperInternals.h"

#include <Compil/BeforeNonUnrealIncludes.h>
#	include "SDK/Core/Tools/Log.h"
#include <Core/Tools/Tools.h>
#include <Compil/AfterNonUnrealIncludes.h>
#include <optional>
#include <Camera/CameraActor.h>
#include <LevelSequence.h>
#include <UObject/Package.h>
#include <MovieScene.h>
#include <MovieSceneCommonHelpers.h>
#include <MovieSceneSection.h>
#include <MovieSceneSpawnable.h>
#include <Tracks/MovieSceneFloatTrack.h>
#include <Tracks/MovieSceneDoubleTrack.h>
#include <Tracks/MovieSceneBoolTrack.h>
#include <Tracks/MovieSceneStringTrack.h>
#include <Tracks/MovieScene3DTransformTrack.h>
#include <Tracks/MovieSceneVectorTrack.h>
#include <Tracks/MovieSceneCameraCutTrack.h>
#include <Sections/MovieSceneFloatSection.h>
#include <Sections/MovieSceneDoubleSection.h>
#include <Sections/MovieSceneBoolSection.h>
#include <Sections/MovieSceneStringSection.h>
#include <Sections/MovieScene3DTransformSection.h>
#include <Sections/MovieSceneVectorSection.h>
#include <Sections/MovieSceneCameraCutSection.h>
#include <Channels/MovieSceneChannelProxy.h>
#include <Channels/MovieSceneFloatChannel.h>
#include <Channels/MovieSceneDoubleChannel.h>
#include <Channels/MovieSceneBoolChannel.h>
#include <Channels/MovieSceneStringChannel.h>
#include <Systems/MovieSceneQuaternionBlenderSystem.h>
#include "AssetRegistry/AssetRegistryModule.h"
#include <UObject/SavePackage.h>
//#include "Package.h"
//#include "FileHelpers.h"


namespace ITwin::SequencerHelper
{
	TArray<int32> ComputeEvenlySpacedIndices(int32 Count, int32 Size)
	{
		TArray<int32> Indices;
		if (Count <= 0 || Size <= 0)
			return Indices;

		Indices.Reserve(Count);
		if (Count == 1)
		{
			Indices.Add(0);
			return Indices;
		}

		for (int32 i = 0; i < Count; ++i)
		{
			const double Ratio = (double)i / (double)(Count - 1);
			int32 Index = FMath::RoundToInt(Ratio * (double)(Size - 1));
			// Rounding can repeat an index when Count is close to Size; force strict growth
			// so that no two keys are ever assigned the same frame.
			if (i > 0 && Index <= Indices.Last())
				Index = FMath::Min(Indices.Last() + 1, Size - 1);
			Indices.Add(Index);
		}
		return Indices;
	}
}

namespace
{

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

	template<>
	struct SequencerTypeTraits<bool>
	{
		using TrackType = UMovieSceneBoolTrack;
		using SectionType = UMovieSceneBoolSection;
		using ChannelType = FMovieSceneBoolChannel;
		using ValueType = bool;
	};
	template<>
	struct SequencerTypeTraits<std::string>
	{
		using TrackType = UMovieSceneStringTrack;
		using SectionType = UMovieSceneStringSection;
		using ChannelType = FMovieSceneStringChannel;
		using ValueType = std::string;
	};


	// Frame/range conversion helpers

	inline FFrameNumber GetFrameNum(const UMovieScene& MovieScene, int iFrame)
	{
		return FFrameNumber(int(iFrame * MovieScene.GetTickResolution().AsDecimal() / MovieScene.GetDisplayRate().AsDecimal()));
	}

	inline FFrameNumber GetTimeFrameNum(const UMovieScene& MovieScene, float fTime)
	{
		return FFrameNumber(int(fTime * MovieScene.GetTickResolution().AsDecimal()));
	}

	inline TRange<FFrameNumber> GetFrameRange(const UMovieScene& MovieScene, int iFrameStart, int iFrameEnd)
	{
		return TRange<FFrameNumber>(GetFrameNum(MovieScene, iFrameStart), TRangeBound<FFrameNumber>::Inclusive(GetFrameNum(MovieScene, iFrameEnd)));
	}

	inline TRange<FFrameNumber> GetTimeFrameRange(const UMovieScene& MovieScene, float fTimeStart, float fTimeEnd)
	{
		return TRange<FFrameNumber>(GetTimeFrameNum(MovieScene, fTimeStart), TRangeBound<FFrameNumber>::Inclusive(GetTimeFrameNum(MovieScene, fTimeEnd)));
	}

	TRange<FFrameNumber> ComputeRangeFromKFs(UMovieSceneSection* pSection)
	{
		TRange<FFrameNumber> retRange = TRange<FFrameNumber>::Empty();
		if (pSection == nullptr)
			return retRange;
		for (const auto& entries : pSection->GetChannelProxy().GetAllEntries())
		{ 
			for (auto pChannel : entries.GetChannels())
			{
				TArray<FFrameNumber> vKeyTimes;
				pChannel->GetKeys(TRange<FFrameNumber>::All(), &vKeyTimes, nullptr);
				if (vKeyTimes.Num() == 0)
					continue; // channel not animated
				const TRange<FFrameNumber> channelRange(
					vKeyTimes[0], TRangeBound<FFrameNumber>::Inclusive(vKeyTimes.Last()));
				// Note: deliberately *not* ensure()ing against ComputeEffectiveRange() -
				// channels with a default value can report an infinite effective range.
				//		ensure(pChannel->ComputeEffectiveRange() == channelRange);

				// Hull rather than a chain of else-ifs: a channel can extend the running
				// range at both ends at once, and the previous code applied only whichever
				// branch matched first - silently dropping the other bound.
				retRange = retRange.IsEmpty()
					? channelRange
					: TRange<FFrameNumber>::Hull(retRange, channelRange);
			 }
		}
		return retRange;
	}

	void ShiftKeyFrameInChannel(FMovieSceneChannel* pChannel, TRange<FFrameNumber> const& FrameRange, FFrameNumber iDeltaFrameNum)
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

	template <class T>
	void ShiftSectionKFs(UMovieSceneSection* pSection, TRange<FFrameNumber> const& editedKFRange, FFrameNumber deltaFrameNum)
	{
		using Traits = SequencerTypeTraits<T>;

		if (!pSection)
			return;

		// The proxy indexes per channel type, so this is also the correct loop bound:
		// NumChannels() is the global count and would include e.g. ManualWeight.
		auto Channels = pSection->GetChannelProxy().GetChannels<typename Traits::ChannelType>();
		if (Channels.Num() == 0)
		{
			// No channel of the expected type means T does not match this section - a
			// programming error, not the "channel present but unanimated" case. Bail out
			// here, before the range is widened below: returning later would leave the
			// section extended with nothing shifted into the new space.
			BE_LOGW("Timeline", "ShiftSectionKFs: section has no channel of the expected type");
			return;
		}

		auto ExtendedRange = pSection->GetRange();
		// extend frame range to include both old and new frame times
		if (deltaFrameNum > 0)
			ExtendedRange.SetUpperBoundValue(ExtendedRange.GetUpperBoundValue() + deltaFrameNum);
		//else
		//	ExtendedRange.SetLowerBoundValue(ExtendedRange.GetLowerBoundValue() + deltaFrameNum);
		pSection->SetRange(ExtendedRange);

		// shift frame times in each transform channel
		for (int32 i(0); i < Channels.Num(); i++)
			ShiftKeyFrameInChannel(Channels[i], editedKFRange, deltaFrameNum);

		// adjust frame range to the shifted times
		pSection->SetRange(ComputeRangeFromKFs(pSection));
		pSection->Modify();
	}

	void SetKeyFrameTimesInChannel(FMovieSceneChannel* pChannel, const TArray<FFrameNumber>& NewFrameNumbers)
	{
		if (pChannel == nullptr || NewFrameNumbers.Num() == 0)
			return;
		TArray<FFrameNumber> vKeyTimes;
		TArray<FKeyHandle> vKeyHandles;
		pChannel->GetKeys(TRange<FFrameNumber>::All(), &vKeyTimes, &vKeyHandles);
		if (vKeyTimes.Num() == 0)
			return; // channel not animated (e.g. an atmo track on a clip without atmo data)

		if (vKeyTimes.Num() == NewFrameNumbers.Num())
		{
			pChannel->SetKeyTimes(vKeyHandles, NewFrameNumbers);
		}
		else
		{
			// The channel is out of sync with the clip's key-frame list. This is not expected, so
			// stay conservative: never add or remove keys here, only retime existing ones, and
			// only with times taken from NewFrameNumbers (the clip's time grid stays authoritative).
			if (vKeyTimes.Num() > NewFrameNumbers.Num())
			{
				// More keys than times: every key would need a distinct frame, and two keys cannot
				// share one. Retiming only part of them would leave the remaining keys at their old
				// times, possibly out of order. Leave the channel alone rather than corrupt it.
				BE_LOGW("Timeline", "Channel holds " << vKeyTimes.Num() << " key(s) but only "
					<< NewFrameNumbers.Num() << " new time(s) were provided; leaving it unchanged");
				return;
			}

			// Fewer keys than times: use an evenly spaced subset of the new times, keeping the
			// first and the last so the channel still spans the whole clip.
			BE_LOGW("Timeline", "Channel holds " << vKeyTimes.Num() << " key(s) but "
				<< NewFrameNumbers.Num() << " new time(s) were provided; remapping onto a subset");

			const TArray<int32> Picked = ITwin::SequencerHelper::ComputeEvenlySpacedIndices(
				vKeyTimes.Num(), NewFrameNumbers.Num());
			TArray<FFrameNumber> Times;
			Times.Reserve(Picked.Num());
			for (int32 Index : Picked)
				Times.Add(NewFrameNumbers[Index]);

			pChannel->SetKeyTimes(vKeyHandles, Times);
		}
	}

	template <class T>
	void SetSectionKFs(UMovieSceneSection* pSection, const TArray<FFrameNumber>& NewFrameNumbers)
	{
		BE_ASSERT(NewFrameNumbers.Num() > 0);

		using Traits = SequencerTypeTraits<T>;

		if (!pSection || NewFrameNumbers.Num() == 0)
			return;

		// The proxy indexes per channel type, so this is also the correct loop bound:
		// NumChannels() is the global count and would include e.g. ManualWeight.
		auto Channels = pSection->GetChannelProxy().GetChannels<typename Traits::ChannelType>();
		if (Channels.Num() == 0)
		{
			BE_LOGW("Timeline", "SetSectionKFs: section has no channel of the expected type");
			return;
		}

		auto ExtendedRange = pSection->GetRange();
		// Extend frame range to include new frame times
		if (NewFrameNumbers.Last() > ExtendedRange.GetUpperBoundValue())
		{
			ExtendedRange.SetUpperBoundValue(NewFrameNumbers.Last());
			pSection->SetRange(ExtendedRange);
		}

		// Set the new times in each transform channel
		for (int32 i(0); i < Channels.Num(); i++)
		{
			SetKeyFrameTimesInChannel(Channels[i], NewFrameNumbers);
		}
		// Adjust frame range to the modified times
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

	inline FString Utf8ToFString(const std::string& In) { return FString(UTF8_TO_TCHAR(In.c_str())); }
	inline std::string FStringToUtf8(const FString& In) { return std::string(TCHAR_TO_UTF8(*In)); }

	template<>
	bool GetValueFromChannel<FMovieSceneStringChannel,FString>(UMovieSceneSection* pSection, int iChannelIdx, FFrameNumber iFrameNum, FString& OutValue)
	{
		if (pSection == nullptr)
			return false;

		auto pChannel = pSection->GetChannelProxy().GetChannel<FMovieSceneStringChannel>(iChannelIdx);
		if (pChannel == nullptr)
			return false;

		return EvaluateChannel(pChannel, iFrameNum, OutValue);
	}

	template<>
	bool GetValueFromChannel<FMovieSceneStringChannel, std::string>(UMovieSceneSection* pSection, int iChannelIdx, FFrameNumber iFrameNum, std::string& OutValue)
	{
		if (pSection == nullptr)
			return false;

		auto pChannel = pSection->GetChannelProxy().GetChannel<FMovieSceneStringChannel>(iChannelIdx);
		if (pChannel == nullptr)
			return false;
		FString tempValue;
		if (EvaluateChannel(pChannel, iFrameNum, tempValue))
		{
			OutValue = FStringToUtf8(tempValue);
			return true;
		}
		return false;
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

	template<>
	bool AddKeyFrameToChannel<FMovieSceneBoolChannel, bool>(UMovieSceneSection* pSection, int iChannelIdx, FFrameNumber iFrameNum, const bool& Value, int iKeyInterp)
	{
		if (pSection == nullptr)
			return false;

		auto pChannel = pSection->GetChannelProxy().GetChannel<FMovieSceneBoolChannel>(iChannelIdx);
		if (pChannel == nullptr)
			return false;

		// UpdateOrAddKey, not DeleteKeys + AddKeys: AddKeys appends without preserving
		// ordering, and Evaluate() binary-searches Times, so a key added before an
		// existing one silently corrupted every later evaluation on this channel.
		pChannel->GetData().UpdateOrAddKey(iFrameNum, Value);
		pSection->Modify();

		return true;
	}

	template<>
	bool AddKeyFrameToChannel<FMovieSceneStringChannel, std::string>(UMovieSceneSection* pSection, int iChannelIdx, FFrameNumber iFrameNum, const std::string& Value, int iKeyInterp)
	{
		if (pSection == nullptr)
			return false;

		auto pChannel = pSection->GetChannelProxy().GetChannel<FMovieSceneStringChannel>(iChannelIdx);
		if (pChannel == nullptr)
			return false;

		// Same remark as for FMovieSceneBoolChannel specialization.
		pChannel->GetData().UpdateOrAddKey(iFrameNum, Utf8ToFString(Value));
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

		// The channel proxy indexes per channel type, so the loop must be bounded by the
		// number of channels of *this* type. NumChannels() is the global count: for a
		// transform section it also counts the ManualWeight float channel, which made the
		// last iteration look up a non-existent double channel and report failure even
		// though every key had in fact been removed.
		auto Channels = pSection->GetChannelProxy().GetChannels<typename Traits::ChannelType>();
		if (Channels.Num() == 0)
			return false;

		int outRes(0);
		for (int32 i(0); i < Channels.Num(); i++)
			outRes += RemoveKeyFrameFromChannel<typename Traits::ChannelType>(pSection, i, iFrameNum);
		return outRes >= Channels.Num();
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

	FString GetErrorMsg(const TCHAR* fname, const TCHAR* msg)
	{
		return FString::Printf(TEXT("'%s' error: '%s'"), fname, msg);
	}
}


bool UITwinSequencerHelper::Initialize(const FString& InLevelSequencePath)
{
	// Already initialized with the same asset: nothing to do.
	if (LevelSequence != nullptr && LevelSequencePath == InLevelSequencePath)
		return true;

	ULevelSequence* pLevelSeq = Cast<ULevelSequence>(
		StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *InLevelSequencePath));
	if (pLevelSeq == nullptr)
	{
		BE_LOGW("Timeline", "Level sequence not found at '"
			<< TCHAR_TO_UTF8(*InLevelSequencePath) << "'");
		return false;
	}
	LevelSequence = pLevelSeq;
	LevelSequencePath = InLevelSequencePath;
	InvalidatePActorGuidCache(); // bindings belong to the previous sequence
	return true;
}

UMovieScene* UITwinSequencerHelper::GetMovieScene() const
{
	return LevelSequence ? LevelSequence->GetMovieScene() : nullptr;
}

/// Fails the current call if the movie scene is unavailable, distinguishing an
/// uninitialized helper from a loaded sequence with no movie scene.
/// Requires FuncName, bOutSuccess and outInfoMsg in scope; member functions only.
#define ITWIN_CHECK_MOVIE_SCENE_OR_EXEC(_MovieVar, _Code)                      \
	do {                                                                       \
		if (!(_MovieVar))                                                      \
		{                                                                      \
			bOutSuccess = false;                                               \
			outInfoMsg = GetErrorMsg(FuncName, IsInitialized()                 \
				? TEXT("Invalid movie scene")                                  \
				: TEXT("Level sequence not initialized"));                     \
			_Code;                                                             \
		}                                                                      \
	} while (0)

#define ITWIN_CHECK_MOVIE_SCENE_OR_RETURN(_MovieVar, _ReturnValue)	\
	ITWIN_CHECK_MOVIE_SCENE_OR_EXEC(_MovieVar, return _ReturnValue)

#define ITWIN_CHECK_MOVIE_SCENE_OR_RETURN_VOID(_MovieVar)	\
	ITWIN_CHECK_MOVIE_SCENE_OR_EXEC(_MovieVar, return )


FGuid UITwinSequencerHelper::GetPActorGuidFromLevelSequence(AActor* pActor, bool& bOutSuccess, FString& outInfoMsg) const
{
	const TCHAR* FuncName = TEXT("GetPActorGuidFromLevelSequence");
	bOutSuccess = false;
	if (!IsInitialized())
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Invalid level sequence"));
		return FGuid();
	}
	if (pActor == nullptr)
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Invalid actor pointer"));
		return FGuid();
	}
	FGuid guid;
	if (pActor->GetWorld())
	{
		// The binding ID of an actor is constant for a given level sequence, but resolving it
		// requires a transient playback state and a reverse lookup over all bindings, so the
		// result is cached (see InvalidatePActorGuidCache for the invalidation points).
		if (FGuid const* CachedGuid = PActorGuidCache.Find(pActor))
		{
			guid = *CachedGuid;
		}
		else
		{
			TSharedRef<UE::MovieScene::FSharedPlaybackState> SharedPlaybackState =
				MovieSceneHelpers::CreateTransientSharedPlaybackState(pActor->GetWorld(), LevelSequence);
			guid = LevelSequence->FindBindingFromObject(pActor, SharedPlaybackState);
			if (guid.IsValid())
				PActorGuidCache.Add(pActor, guid);
		}
	}
	bOutSuccess = guid.IsValid();
	if (bOutSuccess)
		outInfoMsg.Reset();
	else
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Actor GUID not found"));
	return guid;
}

FGuid UITwinSequencerHelper::AddPActorToLevelSequence(AActor* pActor, bool& bOutSuccess, FString& outInfoMsg)
{
	const TCHAR* FuncName = TEXT("AddPActorToLevelSequence");
	bOutSuccess = false;
	if (!IsInitialized())
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Invalid level sequence"));
		return FGuid();
	}
	if (pActor == nullptr)
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Invalid actor pointer"));
		return FGuid();
	}
	FGuid guid = GetPActorGuidFromLevelSequence(pActor, bOutSuccess, outInfoMsg);
	if (guid.IsValid())
	{
		// This is not an error, but the caller should know that the actor is already in the sequence.
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Actor already exists in sequence"));
		return guid;
	}
	UMovieScene* MovieScene = GetMovieScene();
	ITWIN_CHECK_MOVIE_SCENE_OR_RETURN(MovieScene, FGuid());

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
	LevelSequence->BindPossessableObject(guid, *pActor, pActor->GetWorld());
	InvalidatePActorGuidCache(); // a new binding was added
	guid = GetPActorGuidFromLevelSequence(pActor, bOutSuccess, outInfoMsg);
	//if (LevelSequenceActor->BindingOverrides)
	//{
	//	FMovieSceneObjectBindingID BindingID(PossessableGuid);
	//	LevelSequenceActor->BindingOverrides->SetBinding(BindingID, { Actor });
	//}

	bOutSuccess = guid.IsValid();

	if (bOutSuccess)
		outInfoMsg.Reset();
	else
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Actor GUID not found"));

	return guid;
}
		
void UITwinSequencerHelper::RemovePActorFromLevelSequence(AActor* pActor, bool& bOutSuccess, FString& outInfoMsg)
{
	const TCHAR* FuncName = TEXT("RemovePActorFromLevelSequence");
	bOutSuccess = false;
	if (!IsInitialized())
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Invalid level sequence"));
		return;
	}
	FGuid guid = GetPActorGuidFromLevelSequence(pActor, bOutSuccess, outInfoMsg);
	if (!guid.IsValid())
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Actor doesn't exist in sequence"));
		return;
	}
	UMovieScene* MovieScene = GetMovieScene();
	ITWIN_CHECK_MOVIE_SCENE_OR_RETURN_VOID(MovieScene);

	LevelSequence->UnbindPossessableObjects(guid); // unbind track from actor
	bOutSuccess = MovieScene->RemovePossessable(guid);
	InvalidatePActorGuidCache(); // a binding was removed
	if (bOutSuccess)
		outInfoMsg.Reset();
	else
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Failed to remove actor from sequence"));
}


FGuid UITwinSequencerHelper::GetSActorGuidFromLevelSequence(const FString& spawnableName, bool& bOutSuccess, FString& outInfoMsg) const
{
	const TCHAR* FuncName = TEXT("GetSActorGuidFromLevelSequence");
	bOutSuccess = false;

	UMovieScene* MovieScene = GetMovieScene();
	ITWIN_CHECK_MOVIE_SCENE_OR_RETURN(MovieScene, FGuid());

	FGuid guid = FGuid();
	for (int i(0); i < MovieScene->GetSpawnableCount(); i++)
	{
		FMovieSceneSpawnable const& Spawnable = MovieScene->GetSpawnable(i);
		if (MovieScene->GetObjectDisplayName(Spawnable.GetGuid()).ToString() == spawnableName)
		{
			guid = Spawnable.GetGuid();
			break;
		}
	}
	bOutSuccess = guid.IsValid();
	if (bOutSuccess)
		outInfoMsg.Reset();
	else
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Actor GUID not found"));
	return guid;
}

FGuid UITwinSequencerHelper::AddSActorToLevelSequence(const FString& spawnableName, const FString& assetPath, bool& bOutSuccess, FString& outInfoMsg)
{
	const TCHAR* FuncName = TEXT("AddSActorToLevelSequence");
	bOutSuccess = false;
	if (!IsInitialized())
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Invalid level sequence"));
		return FGuid();
	}
	FGuid guid = GetSActorGuidFromLevelSequence(spawnableName, bOutSuccess, outInfoMsg);
	if (guid.IsValid())
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Spawnable actor already exists in level sequence"));
		return guid;
	}
	UObject* pObjTemplate = StaticLoadObject(UObject::StaticClass(), nullptr, *assetPath);
	if (pObjTemplate == nullptr)
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Spawnable template not found"));
		return FGuid();
	}
	// The cast is needed here just because ULevelSequence overrides CreateSpawnable() as protected :-(
	guid = Cast<UMovieSceneSequence>(LevelSequence.Get())->CreateSpawnable(pObjTemplate);
	bOutSuccess = guid.IsValid();
	if (bOutSuccess)
		outInfoMsg.Reset();
	else
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Failed to add spawnable actor to level sequence"));
	// TODO: the following function is only available in Editor
	//if (guid.IsValid())
	//	LevelSequence->MovieScene->SetObjectDisplayName(guid, FText::FromString(spawnableName)); // rename the spawnable to be able to find it later
	return guid;
}

void UITwinSequencerHelper::RemoveSActorFromLevelSequence(const FString& spawnableName, bool& bOutSuccess, FString& outInfoMsg)
{
	const TCHAR* FuncName = TEXT("RemoveSActorFromLevelSequence");
	bOutSuccess = false;

	FGuid guid = GetSActorGuidFromLevelSequence(spawnableName, bOutSuccess, outInfoMsg);
	if (!guid.IsValid())
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Spawnable actor not found in level sequence"));
		return;
	}
	UMovieScene* MovieScene = GetMovieScene();
	ITWIN_CHECK_MOVIE_SCENE_OR_RETURN_VOID(MovieScene);

	bOutSuccess = MovieScene->RemoveSpawnable(guid);
	if (bOutSuccess)
		outInfoMsg.Reset();
	else
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Failed to remove spawnable actor from level sequence"));
}


template<class TrackType>
TrackType* UITwinSequencerHelper::GetTrackFromActorInLevelSequence(AActor* pActor, bool& bOutSuccess, FString& outInfoMsg) const
{
	const TCHAR* FuncName = TEXT("GetTrackFromActorInLevelSequence");
	bOutSuccess = false;

	FGuid guid = GetPActorGuidFromLevelSequence(pActor, bOutSuccess, outInfoMsg);
	if (!guid.IsValid())
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Actor not found in level sequence"));
		return nullptr;
	}

	UMovieScene const* MovieScene = GetMovieScene();
	ITWIN_CHECK_MOVIE_SCENE_OR_RETURN(MovieScene, nullptr);

	TrackType* pTrack = MovieScene->FindTrack<TrackType>(guid);//, TrackName);
	bOutSuccess = (pTrack != nullptr);
	if (bOutSuccess)
		outInfoMsg.Reset();
	else
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Track not found for the actor"));
	return pTrack;
}

template<class TrackType>
TrackType* UITwinSequencerHelper::AddTrackToActorInLevelSequence(AActor* pActor, bool bOverwriteExisting, bool& bOutSuccess, FString& outInfoMsg)
{
	const TCHAR* FuncName = TEXT("AddTrackToActorInLevelSequence");
	bOutSuccess = false;

	UMovieScene* MovieScene = GetMovieScene();
	ITWIN_CHECK_MOVIE_SCENE_OR_RETURN(MovieScene, nullptr);

	TrackType* pTrack = GetTrackFromActorInLevelSequence<TrackType>(pActor, bOutSuccess, outInfoMsg);
	if (pTrack != nullptr)
	{
		if (bOverwriteExisting)
		{
			RemoveTrackFromActorInLevelSequence<TrackType>(pActor, bOutSuccess, outInfoMsg);
		}
		else
		{
			bOutSuccess = false;
			outInfoMsg = GetErrorMsg(FuncName, TEXT("Track of this type already exists for this actor"));
			return pTrack;
		}
	}

	FGuid guid = GetPActorGuidFromLevelSequence(pActor, bOutSuccess, outInfoMsg);
	if (!guid.IsValid())
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Actor not found in level sequence"));
		return nullptr;
	}

	pTrack = MovieScene->AddTrack<TrackType>(guid);

	if (auto pTransTrack = Cast<UMovieScene3DTransformTrack>((UMovieScenePropertyTrack*)pTrack))
		pTransTrack->SetBlenderSystem(UMovieSceneQuaternionBlenderSystem::StaticClass());

	bOutSuccess = (pTrack != nullptr);
	if (bOutSuccess)
		outInfoMsg.Reset();
	else
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Failed to add track to actor in level sequence"));
	return pTrack;
}

template<class TrackType>
void UITwinSequencerHelper::RemoveTrackFromActorInLevelSequence(AActor* pActor, bool& bOutSuccess, FString& outInfoMsg)
{
	const TCHAR* FuncName = TEXT("RemoveTrackFromActorInLevelSequence");
	bOutSuccess = false;

	TrackType* pTrack = GetTrackFromActorInLevelSequence<TrackType>(pActor, bOutSuccess, outInfoMsg);
	if (pTrack == nullptr)
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Track not found for the actor"));
		return;
	}

	UMovieScene* MovieScene = GetMovieScene();
	ITWIN_CHECK_MOVIE_SCENE_OR_RETURN_VOID(MovieScene);

	bOutSuccess = MovieScene->RemoveTrack(*pTrack);

	if (bOutSuccess)
		outInfoMsg.Reset();
	else
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Failed to remove track from actor in level sequence"));
}

void UITwinSequencerHelper::RemoveAllTracksFromLevelSequence(AActor* pActor, bool& bOutSuccess, FString& outInfoMsg)
{
	const TCHAR* FuncName = TEXT("RemoveAllTracksFromLevelSequence");
	bOutSuccess = false;

	FGuid guid = GetPActorGuidFromLevelSequence(pActor, bOutSuccess, outInfoMsg);
	if (!guid.IsValid())
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Actor not found in level sequence"));
		return;
	}

	UMovieScene* MovieScene = GetMovieScene();
	ITWIN_CHECK_MOVIE_SCENE_OR_RETURN_VOID(MovieScene);

	auto Tracks = MovieScene->FindTracks(UMovieSceneTrack::StaticClass(), guid);
	bool bAnythingRemoved = false;
	for (auto Track : Tracks)
	{
		bAnythingRemoved |= MovieScene->RemoveTrack(*Track);
	}

	bOutSuccess = bAnythingRemoved;
	if (bOutSuccess)
		outInfoMsg.Reset();
	else
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Failed to remove any tracks from actor in level sequence"));
}


template<class TrackType>
UMovieSceneSection* UITwinSequencerHelper::GetSectionFromActorInLevelSequence(AActor* pActor,
	int iSectionIdx, bool& bOutSuccess, FString& outInfoMsg) const
{
	const TCHAR* FuncName = TEXT("GetSectionFromActorInLevelSequence");
	bOutSuccess = false;

	auto pTrack = GetTrackFromActorInLevelSequence<TrackType>(pActor, bOutSuccess, outInfoMsg);
	if (pTrack == nullptr)
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Track of this type not found for the actor"));
		return nullptr;
	}

	auto pSection = GetSectionFromTrack(pTrack, iSectionIdx);
	if (pSection == nullptr)
	{
		bOutSuccess = false; // beware bOutSuccess is set to true in GetTrackFromActorInLevelSequence!
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Section with given type and index not found in the track"));
		return nullptr;
	}

	bOutSuccess = true;
	outInfoMsg.Reset();
	return pSection;
}

template<class TrackType>
UMovieSceneSection* UITwinSequencerHelper::AddSectionToActorInLevelSequence(AActor* pActor,
	FFrameNumber FrameNum, EMovieSceneBlendType eBlendType, bool& bOutSuccess, FString& outInfoMsg)
{
	const TCHAR* FuncName = TEXT("AddSectionToActorInLevelSequence");
	bOutSuccess = false;

	auto pTrack = GetTrackFromActorInLevelSequence<TrackType>(pActor, bOutSuccess, outInfoMsg);
	if (pTrack == nullptr)
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Track of this type not found for the actor"));
		return nullptr;
	}

	auto pSection = AddSectionToTrack(pTrack, eBlendType, 0, FrameNum);
	if (pSection == nullptr)
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Failed to create new section"));
		return nullptr;
	}

	bOutSuccess = true;
	outInfoMsg.Reset();
	return pSection;
}

template<class TrackType>
void UITwinSequencerHelper::RemoveSectionFromActorInLevelSequence(AActor* pActor, int iSectionIdx, bool& bOutSuccess, FString& outInfoMsg)
{
	const TCHAR* FuncName = TEXT("RemoveSectionFromActorInLevelSequence");
	bOutSuccess = false;

	auto pTrack = GetTrackFromActorInLevelSequence<TrackType>(pActor, bOutSuccess, outInfoMsg);
	if (pTrack == nullptr)
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Track of this type not found for the actor"));
		return;
	}

	if (!RemoveSectionFromTrack(pTrack, iSectionIdx))
	{
		bOutSuccess = false; // beware bOutSuccess is set to true in GetTrackFromActorInLevelSequence!
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Failed to remove section"));
		return;
	}

	bOutSuccess = true;
	outInfoMsg.Reset();
}


template<typename T>
FFrameNumberRange UITwinSequencerHelper::AddKeyFrameToActor(AActor* pActor,
															int iSectionIdx,
															FFrameNumber iFrameNum,
															const T &Value,
															int iKeyInterp,
															bool& bOutSuccess,
															FString& outInfoMsg)
{
	using Traits = SequencerTypeTraits<T>;

	const TCHAR* FuncName = TEXT("AddKeyFrameToActor");
	bOutSuccess = false;

	auto pTrack = GetTrackFromActorInLevelSequence<typename Traits::TrackType>(pActor, bOutSuccess, outInfoMsg);
	if (pTrack == nullptr)
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Track of this type not found for the actor"));
		return FFrameNumberRange();
	}

	return AddKeyFrameToTrack<T>(pTrack, iSectionIdx, iFrameNum, Value, iKeyInterp, bOutSuccess, outInfoMsg);
}

template<typename T>
FFrameNumberRange UITwinSequencerHelper::AddKeyFrameToTrack(UMovieSceneTrack* pTrack,
															int iSectionIdx,
															FFrameNumber iFrameNum,
															const T &Value,
															int iKeyInterp,
															bool& bOutSuccess,
															FString& outInfoMsg)
{
	using Traits = SequencerTypeTraits<T>;

	const TCHAR* FuncName = TEXT("AddKeyFrameToTrack");
	bOutSuccess = false;

	auto pSection = GetSectionFromTrack(pTrack, iSectionIdx);
	if (pSection == nullptr)
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Section not found in the track"));
		return FFrameNumberRange();
	}

	if (!AddKeyFrameToSection<T>(pSection, iFrameNum, Value, iKeyInterp))
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Failed to add key-frame"));
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
	outInfoMsg.Reset();

	return pSection->GetRange();
}

template<typename T>
FFrameNumberRange UITwinSequencerHelper::RemoveKeyFrameFromActor(AActor* pActor,
	int iSectionIdx, FFrameNumber iFrameNum, bool& bOutSuccess, FString& outInfoMsg)
{
	using Traits = SequencerTypeTraits<T>;

	const TCHAR* FuncName = TEXT("RemoveKeyFrameFromActor");
	bOutSuccess = false;

	auto pTrack = GetTrackFromActorInLevelSequence<typename Traits::TrackType>(pActor, bOutSuccess, outInfoMsg);
	if (pTrack == nullptr)
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Track of this type not found for the actor"));
		return FFrameNumberRange();
	}

	return RemoveKeyFrameFromTrack<T>(pTrack, iSectionIdx, iFrameNum, bOutSuccess, outInfoMsg);
}

template<typename T>
FFrameNumberRange UITwinSequencerHelper::RemoveKeyFrameFromTrack(UMovieSceneTrack* pTrack,
	int iSectionIdx, FFrameNumber iFrameNum, bool& bOutSuccess, FString& outInfoMsg)
{
	using Traits = SequencerTypeTraits<T>;

	const TCHAR* FuncName = TEXT("RemoveKeyFrameFromTrack");
	bOutSuccess = false;

	auto pSection = GetSectionFromTrack(pTrack, iSectionIdx);
	if (pSection == nullptr)
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Section not found in the track"));
		return FFrameNumberRange();
	}

	if (!RemoveKeyFrameFromSection<T>(pSection, iFrameNum))
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Failed to remove key-frame"));
		return FFrameNumberRange();
	}

	auto newRange = ComputeRangeFromKFs(pSection);
	pSection->SetRange(newRange);
	pSection->Modify();

	bOutSuccess = true;
	outInfoMsg.Reset();

	return pSection->GetRange();
}


bool UITwinSequencerHelper::GetTransformAtTime(AActor* pActor, float fTime, FVector& pos, FRotator& rot) const
{
	bool bRes(false);
	FString outMsg;
	auto pTransSection = Cast<UMovieScene3DTransformSection>(GetSectionFromActorInLevelSequence<UMovieScene3DTransformTrack>(pActor, 0, bRes, outMsg));
	if (pTransSection == nullptr)
		return false;

	UMovieScene const* MovieScene = GetMovieScene();
	if (MovieScene == nullptr)
		return false;

	auto iFrameNum = GetTimeFrameNum(*MovieScene, fTime);
	//if (!MovieScene->GetPlaybackRange().Contains(iFrameNum))
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
bool UITwinSequencerHelper::GetActorValueAtTime(AActor* pActor, float fTime, T& OutValue) const
{
	using Traits = SequencerTypeTraits<T>;

	bool bRes(false);
	FString outMsg;

	auto pTrack = GetTrackFromActorInLevelSequence<typename Traits::TrackType>(pActor, bRes, outMsg);
	if (pTrack == nullptr)
		return false;

	return GetTrackValueAtTime<T>(pTrack, fTime, OutValue);
}

template<typename T>
bool UITwinSequencerHelper::GetTrackValueAtTime(UMovieSceneTrack* pTrack, float fTime, T& OutValue) const
{
	using Traits = SequencerTypeTraits<T>;

	auto pSection = GetSectionFromTrack(pTrack, 0);
	if (pSection == nullptr)
		return false;

	UMovieScene const* MovieScene = GetMovieScene();
	if (MovieScene == nullptr)
		return false;
	auto iFrameNum = GetTimeFrameNum(*MovieScene, fTime);
	//if (!MovieScene->GetPlaybackRange().Contains(iFrameNum))
	//	return false;

	return GetKeyFrameValue<T>(pSection, iFrameNum, OutValue);
}

bool UITwinSequencerHelper::GetFloatValueAtTime(UMovieSceneTrack* pTrack, float fTime, float& OutValue) const
{
	return GetTrackValueAtTime<float>(pTrack, fTime, OutValue);
}

bool UITwinSequencerHelper::GetDoubleValueAtTime(UMovieSceneTrack* pTrack, float fTime, double& OutValue) const
{
	return GetTrackValueAtTime<double>(pTrack, fTime, OutValue);
}

bool UITwinSequencerHelper::GetStringValueAtTime(UMovieSceneTrack* pTrack, float fTime, std::string& OutValue) const
{
	return GetTrackValueAtTime<std::string>(pTrack, fTime, OutValue);
}
bool UITwinSequencerHelper::GetBoolValueAtTime(UMovieSceneTrack* pTrack, float fTime, bool& OutValue) const
{
	return GetTrackValueAtTime<bool>(pTrack, fTime, OutValue);
}

bool UITwinSequencerHelper::HasTransformKeyFrame(AActor* pActor, int iSectionIdx, FFrameNumber iFrameNum, bool& bOutSuccess, FString& outInfoMsg) const
{
	const TCHAR* FuncName = TEXT("ActorHasTransformKeyFrame");
	bOutSuccess = false;

	auto pTrack = GetTrackFromActorInLevelSequence<UMovieScene3DTransformTrack>(pActor, bOutSuccess, outInfoMsg);
	if (pTrack == nullptr)
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Track of this type not found for the actor"));
		return false;
	}

	auto pSection = GetSectionFromTrack(pTrack, iSectionIdx);
	if (pSection == nullptr)
	{
		bOutSuccess = false; // beware bOutSuccess is set to true in GetTrackFromActorInLevelSequence!
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Section not found in the track"));
		return false;
	}

	return HasKeyFrameInChannel<FMovieSceneDoubleChannel>(pSection, 0, iFrameNum);
}

float UITwinSequencerHelper::GetDuration(AActor* pActor) const
{
	bool bRes(false);
	FString outMsg;
	auto pTransSection = GetSectionFromActorInLevelSequence<UMovieScene3DTransformTrack>(pActor, 0, bRes, outMsg);
	if (pTransSection == nullptr)
		return 0.f;
	UMovieScene const* MovieScene = GetMovieScene();
	if (MovieScene == nullptr)
		return 0.f;
	auto const SectionRange = pTransSection->GetRange();// GetRangeFromKFs(pTransSection);
	if (!SectionRange.HasLowerBound() || !SectionRange.HasUpperBound())
		return 0.f;
	return (float)(SectionRange.GetUpperBoundValue().Value - SectionRange.GetLowerBoundValue().Value) / (float)(MovieScene->GetTickResolution().AsDecimal());
}

float UITwinSequencerHelper::GetStartTime(AActor* pActor) const
{
	bool bRes(false);
	FString outMsg;
	auto pTransSection = GetSectionFromActorInLevelSequence<UMovieScene3DTransformTrack>(pActor, 0, bRes, outMsg);
	if (pTransSection == nullptr)
		return 0.f;
	UMovieScene const* MovieScene = GetMovieScene();
	if (MovieScene == nullptr)
		return 0.f;
	auto const SectionRange = pTransSection->GetRange();// GetRangeFromKFs(pTransSection);
	if (!SectionRange.HasLowerBound())
		return 0.f;
	return (float)(SectionRange.GetLowerBoundValue().Value) / (float)(MovieScene->GetTickResolution().AsDecimal());
}

float UITwinSequencerHelper::GetEndTime(AActor* pActor) const
{
	bool bRes(false);
	FString outMsg;
	auto pTransSection = GetSectionFromActorInLevelSequence<UMovieScene3DTransformTrack>(pActor, 0, bRes, outMsg);
	if (pTransSection == nullptr)
		return 0.f;
	UMovieScene const* MovieScene = GetMovieScene();
	if (MovieScene == nullptr)
		return 0.f;
	auto const SectionRange = pTransSection->GetRange();// GetRangeFromKFs(pTransSection);
	if (!SectionRange.HasUpperBound())
		return 0.f;
	return (float)(SectionRange.GetUpperBoundValue().Value) / (float)(MovieScene->GetTickResolution().AsDecimal());
}

void UITwinSequencerHelper::AdjustMoviePlaybackRange(ACameraActor* pCameraActor)
{
	UMovieScene* MovieScene = GetMovieScene();
	if (MovieScene == nullptr)
	{
		return;
	}
	bool bRes(false);
	FString outMsg;
	auto pTransSection = GetSectionFromActorInLevelSequence<UMovieScene3DTransformTrack>(pCameraActor, 0, bRes, outMsg);
	if (pTransSection)
	{
		auto const SectionRange = pTransSection->GetRange();
		// Both predicates are needed and neither implies the other: an open-bounded range is
		// never "empty" (so IsEmpty alone would not protect GetXxxBoundValue's check()), and a
		// closed-but-inverted range like [10,5] has both bounds yet no elements.
		if (SectionRange.HasLowerBound() && SectionRange.HasUpperBound() && !SectionRange.IsEmpty())
		{
			MovieScene->SetPlaybackRange(SectionRange);
		}
	}
}

float UITwinSequencerHelper::GetPlaybackEndTime() const
{
	UMovieScene const* MovieScene = GetMovieScene();
	if (MovieScene == nullptr)
	{
		return 0.f;
	}
	return MovieScene->GetPlaybackRange().GetUpperBoundValue().Value / MovieScene->GetTickResolution().AsDecimal();
}


bool UITwinSequencerHelper::HasCameraCutTrack() const
{
	UMovieScene const* MovieScene = GetMovieScene();
	return (MovieScene != nullptr) && (Cast<UMovieSceneCameraCutTrack>(MovieScene->GetCameraCutTrack()) != nullptr);
}

ACameraActor* UITwinSequencerHelper::GetCameraCutBoundCamera(FFrameTime PlaybackTime) const
{
	UMovieScene* MovieScene = GetMovieScene();
	if (MovieScene == nullptr)
	{
		return nullptr;
	}
	if (auto CameraCutTrack = Cast<UMovieSceneCameraCutTrack>(MovieScene->GetCameraCutTrack()))
	{
		TArray<UMovieSceneSection*> Sections = CameraCutTrack->GetAllSections();
		for (UMovieSceneSection* Section : Sections)
		{
			if (Section->IsActive() && Section->IsTimeWithinSection(PlaybackTime.FrameNumber))
			{
				if (UMovieSceneCameraCutSection* CameraCutSection = Cast<UMovieSceneCameraCutSection>(Section))
				{
					// Return the camera bound to this section
					if (FMovieSceneSpawnable* pSpawnable = MovieScene->FindSpawnable(CameraCutSection->GetCameraBindingID().GetGuid()))
						return Cast<ACameraActor>(pSpawnable->GetObjectTemplate());
					//return CameraCutSection->GetCameraBindingID().GetGuid().ResolveBoundObjects();
				}
			}
		}
	}

	return nullptr;
}

UMovieSceneCameraCutTrack* UITwinSequencerHelper::AddCameraCutTrackToLevelSequence(bool bOverwriteExisting, bool& bOutSuccess, FString& outInfoMsg)
{
	const TCHAR* FuncName = TEXT("AddCameraCutTrackToLevelSequence");
	bOutSuccess = false;

	UMovieScene* MovieScene = GetMovieScene();
	ITWIN_CHECK_MOVIE_SCENE_OR_RETURN(MovieScene, nullptr);

	UMovieSceneCameraCutTrack* pTrack = Cast<UMovieSceneCameraCutTrack>(MovieScene->GetCameraCutTrack());
	if (pTrack != nullptr)
	{
		if (!bOverwriteExisting)
		{
			bOutSuccess = false;
			outInfoMsg = GetErrorMsg(FuncName, TEXT("Camera cut track already exists in the sequence"));
			return pTrack;
		}
		else
		{
			pTrack->RemoveAllAnimationData();
		}
	}
	else
	{
		pTrack = Cast<UMovieSceneCameraCutTrack>(MovieScene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));
	}

	bOutSuccess = (pTrack != nullptr);
	if (bOutSuccess)
		outInfoMsg.Reset();
	else
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Failed to add camera cut track to level sequence"));
	return pTrack;
}

void UITwinSequencerHelper::RemoveCameraCutTrackFromLevelSequence(bool& bOutSuccess, FString& outInfoMsg)
{
	const TCHAR* FuncName = TEXT("RemoveCameraCutTrackFromLevelSequence");
	bOutSuccess = false;

	UMovieScene* MovieScene = GetMovieScene();
	ITWIN_CHECK_MOVIE_SCENE_OR_RETURN_VOID(MovieScene);

	MovieScene->RemoveCameraCutTrack();
	bOutSuccess = true;
	outInfoMsg.Reset();
}

void UITwinSequencerHelper::LinkCameraToCameraCutTrack(ACameraActor* pCameraActor, float fStartTime, float fEndTime, bool &bOutSuccess, FString& outInfoMsg)
{
	const TCHAR* FuncName = TEXT("LinkCameraToCameraCutTrack");
	bOutSuccess = false;

	if (pCameraActor == nullptr)
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Invalid camera actor pointer"));
		return;
	}
	UMovieScene* MovieScene = GetMovieScene();
	ITWIN_CHECK_MOVIE_SCENE_OR_RETURN_VOID(MovieScene);

	UMovieSceneCameraCutTrack* pTrack = Cast<UMovieSceneCameraCutTrack>(MovieScene->GetCameraCutTrack());
	if (pTrack == nullptr)
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Camera cut track not found in level sequence"));
		return;
	}

	FGuid guid = GetPActorGuidFromLevelSequence(pCameraActor, bOutSuccess, outInfoMsg);
	if (!guid.IsValid())
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Camera actor not found in level sequence"));
		return;
	}

	auto iStartFrameNum = GetTimeFrameNum(*MovieScene, fStartTime);
	auto iEndFrameNum = GetTimeFrameNum(*MovieScene, fEndTime);
	UMovieSceneCameraCutSection* pSection = pTrack->AddNewCameraCut(UE::MovieScene::FRelativeObjectBindingID(guid), iStartFrameNum);
	if (pSection == nullptr)
	{
		outInfoMsg = GetErrorMsg(FuncName, TEXT("Failed to add camera cut section"));
		return;
	}
	pSection->SetRange(TRange<FFrameNumber>(iStartFrameNum, iEndFrameNum));
	pSection->Modify();
	pTrack->MarkAsChanged();

	MovieScene->SetPlaybackRange(TRange<FFrameNumber>(0, iEndFrameNum));

	bOutSuccess = true;
	outInfoMsg.Reset();
}

template<class TrackType>
UMovieSceneTrack* UITwinSequencerHelper::CreateTrackForParameter(ACameraActor* pCameraActor,
	const TRange<FFrameNumber>& initialRange, const FName& sTrackName, bool& bRes, FString& outMsg)
{
	if (auto pTrack = AddTrackToActorInLevelSequence<TrackType>(pCameraActor, true /*bOverwriteExisting*/, bRes, outMsg))
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

bool UITwinSequencerHelper::AddNewClip(ACameraActor* pCameraActor,
	const TArray<FTrackInfo> &AnimParams,
	TArray<TStrongObjectPtr<UMovieSceneTrack> >& OutTracks)
{
	bool bRes(false);
	FString outMsg;

	FGuid guid = AddPActorToLevelSequence(pCameraActor, bRes, outMsg);
	if (!bRes)
		return false;

	UMovieScene* MovieScene = GetMovieScene();
	if (MovieScene == nullptr)
		return false;
	auto initialRange = GetTimeFrameRange(*MovieScene, 0, 1.f/*arbitrary small value to create a non-zero section*/);
	//MovieScene->SetPlaybackRange(initialRange);

	OutTracks.Empty();
	for (size_t i(0); i < AnimParams.Num(); i++)
	{
		auto &Info = AnimParams[i];
		UMovieSceneTrack* pTrack(nullptr);
		switch (Info.eType)
		{
		case ETrackType::TT_Transform:
			pTrack = CreateTrackForParameter<UMovieScene3DTransformTrack>(pCameraActor, initialRange, Info.sName, bRes, outMsg);
			break;
		case ETrackType::TT_Vector:
			pTrack = CreateTrackForParameter<UMovieSceneFloatVectorTrack>(pCameraActor, initialRange, Info.sName, bRes, outMsg);
			break;
		case ETrackType::TT_Double:
			pTrack = CreateTrackForParameter<UMovieSceneDoubleTrack>(pCameraActor, initialRange, Info.sName, bRes, outMsg);
			break;
		case ETrackType::TT_Float:
			pTrack = CreateTrackForParameter<UMovieSceneFloatTrack>(pCameraActor, initialRange, Info.sName, bRes, outMsg);
			break;
		case ETrackType::TT_Bool:
			pTrack = CreateTrackForParameter<UMovieSceneBoolTrack>(pCameraActor, initialRange, Info.sName, bRes, outMsg);
			break;
		case ETrackType::TT_String:
			pTrack = CreateTrackForParameter<UMovieSceneStringTrack>(pCameraActor, initialRange, Info.sName, bRes, outMsg);
			break;
		default:
			break;
		}
		OutTracks.Add(TStrongObjectPtr<UMovieSceneTrack>(pTrack));
	}

	return true;
}

float UITwinSequencerHelper::AddKeyFrame(TArray<TStrongObjectPtr<UMovieSceneTrack> >& Tracks,
									float fTime,
									TArray<std::optional<UITwinSequencerHelper::KFValueType> > &Values)
{
	UMovieScene* MovieScene = GetMovieScene();
	if (MovieScene == nullptr)
		return 0.f;

	auto iFrameNum = GetTimeFrameNum(*MovieScene, fTime);

	bool bRes(false);
	FString outMsg;

	int iSectionIdx(0); // we use only one section in each track
	int iKeyInterp(0);

	ensure(Tracks.Num() == Values.Num());

	for (size_t i(0); i < Tracks.Num(); i++)
	{
		if (!Tracks[i] || !Values[i].has_value())
			continue;
		UMovieSceneTrack* pTrack = Tracks[i].Get();
		if (pTrack->GetClass()->GetFName().Compare(TEXT("MovieScene3DTransformTrack")) == 0)
			AddKeyFrameToTrack<FTransform>(pTrack, iSectionIdx, iFrameNum, std::get<FTransform>(Values[i].value()), iKeyInterp, bRes, outMsg);
		else if (pTrack->GetClass()->GetFName().Compare(TEXT("MovieSceneFloatVectorTrack")) == 0)
			AddKeyFrameToTrack<FVector>(pTrack, iSectionIdx, iFrameNum, std::get<FVector>(Values[i].value()), iKeyInterp, bRes, outMsg);
		else if (pTrack->GetClass()->GetFName().Compare(TEXT("MovieSceneDoubleTrack")) == 0)
			AddKeyFrameToTrack<double>(pTrack, iSectionIdx, iFrameNum, std::get<double>(Values[i].value()), iKeyInterp, bRes, outMsg);
		else if (pTrack->GetClass()->GetFName().Compare(TEXT("MovieSceneFloatTrack")) == 0)
			AddKeyFrameToTrack<float>(pTrack, iSectionIdx, iFrameNum, std::get<float>(Values[i].value()), iKeyInterp, bRes, outMsg);
		else if (pTrack->GetClass()->GetFName().Compare(TEXT("MovieSceneBoolTrack")) == 0)
			AddKeyFrameToTrack<bool>(pTrack, iSectionIdx, iFrameNum, std::get<bool>(Values[i].value()), iKeyInterp, bRes, outMsg);
		else if (pTrack->GetClass()->GetFName().Compare(TEXT("MovieSceneStringTrack")) == 0)
			AddKeyFrameToTrack<std::string>(pTrack, iSectionIdx, iFrameNum, std::get<std::string>(Values[i].value()), iKeyInterp, bRes, outMsg);
		else
		{
			BE_ISSUE("Unsupported type of track",
				TCHAR_TO_UTF8(*pTrack->GetClass()->GetFName().ToString()));
		}
	}

	return fTime;
}

void UITwinSequencerHelper::RemoveKeyFrame(TArray<TStrongObjectPtr<UMovieSceneTrack> >& Tracks, float fTime)
{
	UMovieScene* MovieScene = GetMovieScene();
	if (MovieScene == nullptr)
	{
		return;
	}
	auto iFrameNum = GetTimeFrameNum(*MovieScene, fTime);

	FString outMsg;
	int iSectionIdx(0); // we use only one section in each track

	for (int32 i(0); i < Tracks.Num(); i++)
	{
		if (!Tracks[i])
			continue;
		UMovieSceneTrack* pTrack = Tracks[i].Get();

		// Reset per track: a failure on one track must not be masked by a previous success.
		bool bRes = false;
		if (pTrack->GetClass()->GetFName().Compare(TEXT("MovieScene3DTransformTrack")) == 0)
			RemoveKeyFrameFromTrack<FTransform>(pTrack, iSectionIdx, iFrameNum, bRes, outMsg);
		else if (pTrack->GetClass()->GetFName().Compare(TEXT("MovieSceneFloatVectorTrack")) == 0)
			RemoveKeyFrameFromTrack<FVector>(pTrack, iSectionIdx, iFrameNum, bRes, outMsg);
		else if (pTrack->GetClass()->GetFName().Compare(TEXT("MovieSceneDoubleTrack")) == 0)
			RemoveKeyFrameFromTrack<double>(pTrack, iSectionIdx, iFrameNum, bRes, outMsg);
		else if (pTrack->GetClass()->GetFName().Compare(TEXT("MovieSceneFloatTrack")) == 0)
			RemoveKeyFrameFromTrack<float>(pTrack, iSectionIdx, iFrameNum, bRes, outMsg);
		else if (pTrack->GetClass()->GetFName().Compare(TEXT("MovieSceneBoolTrack")) == 0)
			RemoveKeyFrameFromTrack<bool>(pTrack, iSectionIdx, iFrameNum, bRes, outMsg);
		else if (pTrack->GetClass()->GetFName().Compare(TEXT("MovieSceneStringTrack")) == 0)
			RemoveKeyFrameFromTrack<std::string>(pTrack, iSectionIdx, iFrameNum, bRes, outMsg);
		else
		{
			BE_ISSUE("Unsupported type of track",
				TCHAR_TO_UTF8(*pTrack->GetClass()->GetFName().ToString()));
			continue;
		}

		// Silently discarding this is what let the channel-indexing bug hide: the keys
		// were deleted but the section range was never updated, and nobody noticed.
		if (!bRes)
			BE_LOGW("Timeline", "RemoveKeyFrame failed on track '"
				<< TCHAR_TO_UTF8(*pTrack->GetName()) << "' ("
				<< TCHAR_TO_UTF8(*pTrack->GetClass()->GetFName().ToString()) << "): " << TCHAR_TO_UTF8(*outMsg));
	}
}


namespace {

	/// Carries a track's value type as a tag, so a generic lambda can recover it via
	/// decltype(Tag)::Type. Avoids repeating the track-class dispatch for every operation.
	template <typename T>
	struct TTrackValueTag { using Type = T; };

	/// Dispatches on the track's concrete class and invokes Func with the matching tag.
	/// Returns false (and asserts) for unsupported track classes.
	template <typename FuncT>
	bool VisitTrackValueType(const UMovieSceneTrack* pTrack, FuncT&& Func)
	{
		if (!pTrack)
			return false;

		const FName ClassName = pTrack->GetClass()->GetFName();
		if (ClassName == TEXT("MovieScene3DTransformTrack")) { Func(TTrackValueTag<FTransform>{});   return true; }
		if (ClassName == TEXT("MovieSceneFloatVectorTrack")) { Func(TTrackValueTag<FVector>{});      return true; }
		if (ClassName == TEXT("MovieSceneDoubleTrack"))      { Func(TTrackValueTag<double>{});       return true; }
		if (ClassName == TEXT("MovieSceneFloatTrack"))       { Func(TTrackValueTag<float>{});        return true; }
		if (ClassName == TEXT("MovieSceneBoolTrack"))        { Func(TTrackValueTag<bool>{});         return true; }
		if (ClassName == TEXT("MovieSceneStringTrack"))      { Func(TTrackValueTag<std::string>{});  return true; }

		BE_ISSUE("Unsupported type of track");
		return false;
	}

	/// Iterates a clip's tracks, resolving each one's section and value type.
	/// Func is called as Func(Tag, TrackIndex, pTrack, pSection).
	template <typename FuncT>
	void ForEachTrackSection(TArray<TStrongObjectPtr<UMovieSceneTrack> >& Tracks, FuncT&& Func)
	{
		for (int32 i = 0; i < Tracks.Num(); ++i)
		{
			if (!Tracks[i])
				continue;
			UMovieSceneTrack* pTrack = Tracks[i].Get();
			UMovieSceneSection* pSection = GetSectionFromTrack(pTrack, 0); // we use only one section per track
			if (!pSection)
				continue;
			VisitTrackValueType(pTrack, [&](auto Tag) { Func(Tag, i, pTrack, pSection); });
		}
	}

	void DoShiftClipKFs(TArray<TStrongObjectPtr<UMovieSceneTrack> >& Tracks, const FFrameNumber &InDeltaFrameNum,
						std::optional< TRange<FFrameNumber> > InKFRange = std::nullopt)
	{
		ForEachTrackSection(Tracks,
			[&](auto Tag, int32 /*i*/, UMovieSceneTrack* /*pTrack*/, UMovieSceneSection* pSection)
		{
			using T = typename decltype(Tag)::Type;
			const TRange<FFrameNumber> KFRange = InKFRange.has_value() ? *InKFRange : pSection->GetRange();
			ShiftSectionKFs<T>(pSection, KFRange, InDeltaFrameNum);
		});
	}

	void DoSetClipKFsTimes(TArray<TStrongObjectPtr<UMovieSceneTrack> >& Tracks, const TArray<FFrameNumber>& NewFrameNumbers)
	{
		BE_ASSERT(NewFrameNumbers.Num() > 0);

		ForEachTrackSection(Tracks,
			[&](auto Tag, int32 /*i*/, UMovieSceneTrack* /*pTrack*/, UMovieSceneSection* pSection)
		{
			using T = typename decltype(Tag)::Type;
			SetSectionKFs<T>(pSection, NewFrameNumbers);
		});
	}
}

void UITwinSequencerHelper::ShiftClipKFsInRange(TArray<TStrongObjectPtr<UMovieSceneTrack> >& Tracks, float fStartTime, float fEndTime, float fDeltaTime)
{
	UMovieScene const* MovieScene = GetMovieScene();
	if (MovieScene == nullptr)
	{
		return;
	}
	auto EditedKFRange = GetTimeFrameRange(*MovieScene, fStartTime, fEndTime);
	auto DeltaFrameNum = GetTimeFrameNum(*MovieScene, fDeltaTime);

	DoShiftClipKFs(Tracks, DeltaFrameNum, EditedKFRange);
}

void UITwinSequencerHelper::ShiftClipKFs(TArray<TStrongObjectPtr<UMovieSceneTrack> >& Tracks, float fDeltaTime)
{
	UMovieScene const* MovieScene = GetMovieScene();
	if (MovieScene == nullptr)
	{
		return;
	}
	auto DeltaFrameNum = GetTimeFrameNum(*MovieScene, fDeltaTime);

	DoShiftClipKFs(Tracks, DeltaFrameNum);
}

void UITwinSequencerHelper::SetClipKFsTimes(TArray<TStrongObjectPtr<UMovieSceneTrack> >& Tracks, const std::vector<float>& vNewTimes)
{
	if (!ensure(vNewTimes.size() > 0))
		return;
	UMovieScene const* MovieScene = GetMovieScene();
	if (MovieScene == nullptr)
	{
		return;
	}
	TArray<FFrameNumber> NewFrameNumbers;
	NewFrameNumbers.Reserve(vNewTimes.size());
	for (float Time : vNewTimes)
	{
		NewFrameNumbers.Add(GetTimeFrameNum(*MovieScene, Time));
	}
	DoSetClipKFsTimes(Tracks, NewFrameNumbers);
}


void UITwinSequencerHelper::SaveLevelSequenceAsAsset(const FString& PackagePath)
{
	if (!IsInitialized())
	{
		return;
	}
	ULevelSequence& LevelSeq = GetSeqChecked();
	// Create a new package
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		BE_LOGE("Timeline", "Failed to create package.");
		return;
	}

	// Assign the LevelSequence to the package
	FString OldName(LevelSeq.GetName());
	UObject* OldOuter(LevelSeq.GetOuter());
	LevelSeq.Rename(*LevelSeq.GetName(), Package);

	// Mark the package dirty
	Package->MarkPackageDirty();

	// Save the package to a .uasset file
	FString FilePath = FPaths::ProjectContentDir() + PackagePath.Replace(TEXT("/Game/"), TEXT("")) + TEXT(".uasset");

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = EObjectFlags::RF_Public | EObjectFlags::RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;

	bool bSaved = UPackage::SavePackage(Package, &LevelSeq, *FilePath, SaveArgs);
	if (bSaved)
	{
		BE_LOGI("Timeline", "LevelSequence saved to: " << TCHAR_TO_UTF8(*FilePath));
	}
	else
	{
		BE_LOGE("Timeline", "Failed to save LevelSequence");
	}

	LevelSeq.Rename(*OldName, OldOuter);
}

#if WITH_TESTS

void FSequencerHelperTestAccess::SetKeyFrameTimesInChannel(
	FMovieSceneChannel* pChannel, const TArray<FFrameNumber>& NewFrameNumbers)
{
	::SetKeyFrameTimesInChannel(pChannel, NewFrameNumbers);
}

bool FSequencerHelperTestAccess::GetStringFromChannel(
	UMovieSceneSection* pSection, int iChannelIdx, FFrameNumber iFrameNum, FString& OutValue)
{
	return ::GetValueFromChannel<FMovieSceneStringChannel, FString>(
		pSection, iChannelIdx, iFrameNum, OutValue);
}

bool FSequencerHelperTestAccess::GetStdStringFromChannel(
	UMovieSceneSection* pSection, int iChannelIdx, FFrameNumber iFrameNum, std::string& OutValue)
{
	return ::GetValueFromChannel<FMovieSceneStringChannel, std::string>(
		pSection, iChannelIdx, iFrameNum, OutValue);
}

bool FSequencerHelperTestAccess::AddBoolKeyFrameToChannel(
	UMovieSceneSection* pSection, int iChannelIdx, FFrameNumber iFrameNum, bool Value, int iKeyInterp)
{
	return ::AddKeyFrameToChannel<FMovieSceneBoolChannel, bool>(
		pSection, iChannelIdx, iFrameNum, Value, iKeyInterp);
}

bool FSequencerHelperTestAccess::AddStdStringKeyFrameToChannel(
	UMovieSceneSection* pSection, int iChannelIdx, FFrameNumber iFrameNum,
	const std::string& Value, int iKeyInterp)
{
	return ::AddKeyFrameToChannel<FMovieSceneStringChannel, std::string>(
		pSection, iChannelIdx, iFrameNum, Value, iKeyInterp);
}

bool FSequencerHelperTestAccess::AddVectorKeyFrameToSection(
	UMovieSceneSection* pSection, FFrameNumber iFrameNum, const FVector& Value, int iKeyInterp)
{
	return ::AddKeyFrameToSection<FVector>(pSection, iFrameNum, Value, iKeyInterp);
}

UMovieSceneTrack* FSequencerHelperTestAccess::GetTransformTrack(const UITwinSequencerHelper& Helper,
	AActor* pActor, bool& bOutSuccess, FString& outInfoMsg)
{
	return Helper.GetTrackFromActorInLevelSequence<UMovieScene3DTransformTrack>(
		pActor, bOutSuccess, outInfoMsg);
}

UMovieSceneTrack* FSequencerHelperTestAccess::AddTransformTrack(UITwinSequencerHelper& Helper,
	AActor* pActor, bool bOverwriteExisting, bool& bOutSuccess, FString& outInfoMsg)
{
	return Helper.AddTrackToActorInLevelSequence<UMovieScene3DTransformTrack>(
		pActor, bOverwriteExisting, bOutSuccess, outInfoMsg);
}

void FSequencerHelperTestAccess::RemoveTransformTrack(UITwinSequencerHelper& Helper,
	AActor* pActor, bool& bOutSuccess, FString& outInfoMsg)
{
	Helper.RemoveTrackFromActorInLevelSequence<UMovieScene3DTransformTrack>(
		pActor, bOutSuccess, outInfoMsg);
}

UMovieSceneTrack* FSequencerHelperTestAccess::AddDoubleTrack(UITwinSequencerHelper& Helper,
	AActor* pActor, bool bOverwriteExisting, bool& bOutSuccess, FString& outInfoMsg)
{
	return Helper.AddTrackToActorInLevelSequence<UMovieSceneDoubleTrack>(
		pActor, bOverwriteExisting, bOutSuccess, outInfoMsg);
}

UMovieSceneSection* FSequencerHelperTestAccess::GetTransformSection(const UITwinSequencerHelper& Helper,
	AActor* pActor, int iSectionIdx, bool& bOutSuccess, FString& outInfoMsg)
{
	return Helper.GetSectionFromActorInLevelSequence<UMovieScene3DTransformTrack>(
		pActor, iSectionIdx, bOutSuccess, outInfoMsg);
}

UMovieSceneSection* FSequencerHelperTestAccess::AddTransformSection(UITwinSequencerHelper& Helper,
	AActor* pActor, FFrameNumber FrameNum, EMovieSceneBlendType eBlendType,
	bool& bOutSuccess, FString& outInfoMsg)
{
	return Helper.AddSectionToActorInLevelSequence<UMovieScene3DTransformTrack>(
		pActor, FrameNum, eBlendType, bOutSuccess, outInfoMsg);
}

void FSequencerHelperTestAccess::RemoveTransformSection(UITwinSequencerHelper& Helper,
	AActor* pActor, int iSectionIdx, bool& bOutSuccess, FString& outInfoMsg)
{
	Helper.RemoveSectionFromActorInLevelSequence<UMovieScene3DTransformTrack>(
		pActor, iSectionIdx, bOutSuccess, outInfoMsg);
}

FFrameNumberRange FSequencerHelperTestAccess::AddTransformKeyFrameToActor(UITwinSequencerHelper& Helper,
	AActor* pActor, int iSectionIdx, FFrameNumber iFrameNum,
	const FTransform& Value, int iKeyInterp, bool& bOutSuccess, FString& outInfoMsg)
{
	return Helper.AddKeyFrameToActor<FTransform>(
		pActor, iSectionIdx, iFrameNum, Value, iKeyInterp, bOutSuccess, outInfoMsg);
}

FFrameNumberRange FSequencerHelperTestAccess::AddDoubleKeyFrameToTrack(UITwinSequencerHelper& Helper,
	UMovieSceneTrack* pTrack, int iSectionIdx, FFrameNumber iFrameNum, double Value,
	int iKeyInterp, bool& bOutSuccess, FString& outInfoMsg)
{
	return Helper.AddKeyFrameToTrack<double>(
		pTrack, iSectionIdx, iFrameNum, Value, iKeyInterp, bOutSuccess, outInfoMsg);
}

FFrameNumberRange FSequencerHelperTestAccess::RemoveTransformKeyFrameFromActor(UITwinSequencerHelper& Helper,
	AActor* pActor, int iSectionIdx, FFrameNumber iFrameNum,
	bool& bOutSuccess, FString& outInfoMsg)
{
	return Helper.RemoveKeyFrameFromActor<FTransform>(
		pActor, iSectionIdx, iFrameNum, bOutSuccess, outInfoMsg);
}

UMovieSceneSection* FSequencerHelperTestAccess::AddDoubleSection(UITwinSequencerHelper& Helper,
	AActor* pActor, FFrameNumber FrameNum, EMovieSceneBlendType eBlendType,
	bool& bOutSuccess, FString& outInfoMsg)
{
	return Helper.AddSectionToActorInLevelSequence<UMovieSceneDoubleTrack>(
		pActor, FrameNum, eBlendType, bOutSuccess, outInfoMsg);
}

FFrameNumberRange FSequencerHelperTestAccess::RemoveDoubleKeyFrameFromActor(UITwinSequencerHelper& Helper,
	AActor* pActor, int iSectionIdx, FFrameNumber iFrameNum,
	bool& bOutSuccess, FString& outInfoMsg)
{
	return Helper.RemoveKeyFrameFromActor<double>(
		pActor, iSectionIdx, iFrameNum, bOutSuccess, outInfoMsg);
}

UMovieSceneTrack* FSequencerHelperTestAccess::AddFloatTrack(UITwinSequencerHelper& Helper,
	AActor* pActor, bool bOverwriteExisting, bool& bOutSuccess, FString& outInfoMsg)
{
	return Helper.AddTrackToActorInLevelSequence<UMovieSceneFloatTrack>(
		pActor, bOverwriteExisting, bOutSuccess, outInfoMsg);
}

UMovieSceneSection* FSequencerHelperTestAccess::AddFloatSection(UITwinSequencerHelper& Helper,
	AActor* pActor, FFrameNumber FrameNum, EMovieSceneBlendType eBlendType,
	bool& bOutSuccess, FString& outInfoMsg)
{
	return Helper.AddSectionToActorInLevelSequence<UMovieSceneFloatTrack>(
		pActor, FrameNum, eBlendType, bOutSuccess, outInfoMsg);
}

FFrameNumberRange FSequencerHelperTestAccess::AddFloatKeyFrameToTrack(UITwinSequencerHelper& Helper,
	UMovieSceneTrack* pTrack, int iSectionIdx, FFrameNumber iFrameNum, float Value,
	int iKeyInterp, bool& bOutSuccess, FString& outInfoMsg)
{
	return Helper.AddKeyFrameToTrack<float>(
		pTrack, iSectionIdx, iFrameNum, Value, iKeyInterp, bOutSuccess, outInfoMsg);
}

UMovieSceneTrack* FSequencerHelperTestAccess::AddBoolTrack(UITwinSequencerHelper& Helper,
	AActor* pActor, bool bOverwriteExisting, bool& bOutSuccess, FString& outInfoMsg)
{
	return Helper.AddTrackToActorInLevelSequence<UMovieSceneBoolTrack>(
		pActor, bOverwriteExisting, bOutSuccess, outInfoMsg);
}

UMovieSceneSection* FSequencerHelperTestAccess::AddBoolSection(UITwinSequencerHelper& Helper,
	AActor* pActor, FFrameNumber FrameNum, EMovieSceneBlendType eBlendType,
	bool& bOutSuccess, FString& outInfoMsg)
{
	return Helper.AddSectionToActorInLevelSequence<UMovieSceneBoolTrack>(
		pActor, FrameNum, eBlendType, bOutSuccess, outInfoMsg);
}

FFrameNumberRange FSequencerHelperTestAccess::AddBoolKeyFrameToTrack(UITwinSequencerHelper& Helper,
	UMovieSceneTrack* pTrack, int iSectionIdx, FFrameNumber iFrameNum, bool Value,
	int iKeyInterp, bool& bOutSuccess, FString& outInfoMsg)
{
	return Helper.AddKeyFrameToTrack<bool>(
		pTrack, iSectionIdx, iFrameNum, Value, iKeyInterp, bOutSuccess, outInfoMsg);
}

UMovieSceneTrack* FSequencerHelperTestAccess::AddStringTrack(UITwinSequencerHelper& Helper,
	AActor* pActor, bool bOverwriteExisting, bool& bOutSuccess, FString& outInfoMsg)
{
	return Helper.AddTrackToActorInLevelSequence<UMovieSceneStringTrack>(
		pActor, bOverwriteExisting, bOutSuccess, outInfoMsg);
}

UMovieSceneSection* FSequencerHelperTestAccess::AddStringSection(UITwinSequencerHelper& Helper,
	AActor* pActor, FFrameNumber FrameNum, EMovieSceneBlendType eBlendType,
	bool& bOutSuccess, FString& outInfoMsg)
{
	return Helper.AddSectionToActorInLevelSequence<UMovieSceneStringTrack>(
		pActor, FrameNum, eBlendType, bOutSuccess, outInfoMsg);
}

FFrameNumberRange FSequencerHelperTestAccess::AddStringKeyFrameToTrack(UITwinSequencerHelper& Helper,
	UMovieSceneTrack* pTrack, int iSectionIdx, FFrameNumber iFrameNum,
	const std::string& Value, int iKeyInterp, bool& bOutSuccess, FString& outInfoMsg)
{
	return Helper.AddKeyFrameToTrack<std::string>(
		pTrack, iSectionIdx, iFrameNum, Value, iKeyInterp, bOutSuccess, outInfoMsg);
}

#endif // WITH_TESTS

#undef ITWIN_CHECK_MOVIE_SCENE_OR_RETURN_VOID
#undef ITWIN_CHECK_MOVIE_SCENE_OR_RETURN
#undef ITWIN_CHECK_MOVIE_SCENE_OR_EXEC
