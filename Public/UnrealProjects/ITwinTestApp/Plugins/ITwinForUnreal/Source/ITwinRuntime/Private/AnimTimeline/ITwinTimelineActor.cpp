/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTimelineActor.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <AnimTimeline/ITwinTimelineActor.h>
#include <AnimTimeline/ITwinSequencerHelper.h>
#include <AnimTimeline/ReadWriteHelper.h>
#include <ITwinIModel.h>
#include <ITwinSynchro4DSchedules.h>

#include <Camera/CameraActor.h>
#include <Camera/CameraComponent.h>
#include <CineCameraActor.h>
#include <Engine/World.h>
#include <GameFramework/Pawn.h>
#include <HAL/FileManager.h>
#include <Kismet/GameplayStatics.h>
#include <LevelSequence.h>
#include <LevelSequencePlayer.h>
#include <LevelSequenceActor.h>
#include <Misc/Paths.h>
#include <MovieSceneSequencePlayer.h>

#include <optional>
#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include "SDK/Core/Tools/Log.h"
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

//#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
//#	include "SDK/Core/Visualization/Timeline.h"
//#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

namespace ITwin
{
	float fDefaultTimeDelta = 2.f; // default delta time when appending key-frames

	ITWINRUNTIME_API bool GetSynchroDateFromSchedules(TMap<FString, UITwinSynchro4DSchedules*> const& SchedMap, FDateTime& Out, FString& ScheduleIDOut);
	ITWINRUNTIME_API void SetSynchroDateToSchedules(TMap<FString, UITwinSynchro4DSchedules*> const& SchedMap, const FDateTime& InDate);

	FString GetTimelineDataPath()
	{
		FString RelativePath = FPaths::ProjectContentDir();
		FString FullPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*RelativePath);
		FString FinalPath = FPaths::Combine(*FullPath, TEXT("Timeline_export.json"));
		BE_LOGI("Timeline", "Using path "	<< TCHAR_TO_UTF8(*FinalPath) << " to save timeline data");
		return FinalPath;
	}

	ACineCameraActor* SpawnCamera(UWorld* pWorld)
	{
		FVector pos;
		FRotator rot;
		ScreenUtils::GetCurrentView(pWorld, pos, rot);

		// hack that allows to load an existing non-empty level sequence for testing
		//if (auto ExistingCam = Cast<ACineCameraActor>(UGameplayStatics::GetActorOfClass(pWorld, ACineCameraActor::StaticClass())))
		//	return ExistingCam;

		FActorSpawnParameters SpawnInfo;
		auto NewCamera = pWorld->SpawnActor<ACineCameraActor>(ACineCameraActor::StaticClass(), pos, rot, SpawnInfo);
		if (UCameraComponent* CameraComponent = NewCamera->GetCameraComponent())
			if (APlayerCameraManager* CamManager = pWorld->GetFirstPlayerController()->PlayerCameraManager)
				CameraComponent->SetFieldOfView(CamManager->GetFOVAngle());
		return NewCamera;

	}

	// To store and interpolate date in the sequencer, we use its timespan from an arbitrary base date 01-01-2000.
	// The timespan is then converted to seconds. (Using days may cause issues with schedules that contain time information
	// along with date : in this case SynchroToTimeline/TimelineToSynchro conversion can falsely increment or decrement date value.)
	FDateTime GetBaseDate()
	{
		return FDateTime(2000, 1, 1);
	}

	double SynchroToTimeline(FDateTime Date)
	{
		return (double)((Date - GetBaseDate()).GetTotalSeconds())/100.0;
	}

	FDateTime TimelineToSynchro(double Delta)
	{
		return GetBaseDate() + FTimespan::FromSeconds(Delta*100.0);
	}
}

void GetFTransform(const AdvViz::SDK::dmat3x4& srcMat, FTransform &f)
{
	using namespace AdvViz::SDK;
	FMatrix dstMat(FMatrix::Identity);
	FVector dstPos;
	for (unsigned i = 0; i < 3; ++i)
		for (unsigned j = 0; j < 3; ++j)
			dstMat.M[j][i] = ColRow3x4(srcMat, i, j);

	dstPos.X = ColRow3x4(srcMat, 0, 3);
	dstPos.Y = ColRow3x4(srcMat, 1, 3);
	dstPos.Z = ColRow3x4(srcMat, 2, 3);

	f.SetFromMatrix(dstMat);
	f.SetTranslation(dstPos);
}

void GetSDKTransform(const FTransform& f, AdvViz::SDK::dmat3x4& dstTransform)
{
	using namespace AdvViz::SDK;
	FMatrix srcMat = f.ToMatrixWithScale();
	FVector srcPos = f.GetTranslation();
	for (int32 i = 0; i < 3; ++i)
		for (int32 j = 0; j < 3; ++j)
			ColRow3x4(dstTransform, j, i) = srcMat.M[i][j];
	ColRow3x4(dstTransform, 0, 3) = srcPos.X;
	ColRow3x4(dstTransform, 1, 3) = srcPos.Y;
	ColRow3x4(dstTransform, 2, 3) = srcPos.Z;
}

FDateTime StrToDateTime(const std::string& s)
{
	if (s.empty())
		return FDateTime();
	FDateTime datetime;
	FString datestring = UTF8_TO_TCHAR(s.c_str());
	FDateTime::ParseIso8601(*datestring, datetime);
	return datetime;
}

void DateTimeToStr(const FDateTime& datetime, std::string& s)
{
	s = TCHAR_TO_UTF8(*datetime.ToIso8601());
}


class ClipData : public AdvViz::SDK::TimelineClip, public AdvViz::SDK::Tools::TypeId<ClipData>
{
private:
	ACameraActor* pCamera_ = nullptr;
	TArray<TStrongObjectPtr<UMovieSceneTrack> > Tracks_; // maps all the parameters animated by the timeline to their respective tracks

public:
	ClipData()
	{}

	bool bSynchroAnim = true;
	bool bAtmoAnim = true;

	using AdvViz::SDK::Tools::TypeId<ClipData>::GetTypeId;
	std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
	bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || AdvViz::SDK::TimelineClip::IsTypeOf(i); }

	void InitCamera(UWorld* pWorld)
	{
		pCamera_ = ITwin::SpawnCamera(pWorld);
	}

	TArray<TStrongObjectPtr<UMovieSceneTrack> >& GetTracks()
	{
		return Tracks_;
	}

	ACameraActor* GetCamera() const
	{
		return pCamera_;
	}

	FString GetNameU() const
	{
		return FString((UTF8CHAR*)GetName().c_str());
	}

	void SetNameU(FString sName)
	{
		SetName(std::string(TCHAR_TO_UTF8(*sName)));
	}

	bool HasKeyFrame(float fTime) const
	{
		auto ret = GetKeyframe(fTime);
		return (bool)ret.has_value();
	}

	bool HasKeyFrame(int iKF) const
	{
		return iKF >= 0 && iKF < GetKeyframeCount();
	}

	int GetKeyFrameIndex(float fTime, bool bPrecise = false)
	{
		if (bPrecise)
		{
			auto ret = GetKeyframeIndex(fTime);
			if (ret)
				return *ret;
			return -1;
		}

		int32 idx(0);
		for (;idx < (int32)GetKeyframeCount(); ++idx)
		{
			auto kf = GetKeyframeByIndex(idx);
			if (kf && (*kf)->GetData().time > fTime)
				break;
		}
		return idx - 1;
	}

	float GetKeyFrameTime(int iKF)
	{
		if (iKF >= 0 && iKF < GetKeyframeCount())
		{
			auto ret = GetKeyframeByIndex(iKF);
			if (ret)
				return (*ret)->GetData().time;
		}
		return -1.f;
	}

	float GetDuration() const
	{
		auto ret = GetKeyframeByIndex(GetKeyframeCount() - 1);
		if (ret)
			return (*ret)->GetData().time;
		return 0.0f;
	}

	std::shared_ptr<AdvViz::SDK::ITimelineKeyframe> AddOrUpdateKeyFrame(float fTime, const AdvViz::SDK::ITimelineKeyframe::KeyframeData& KF)
	{
		auto ret = GetKeyframe(fTime);
		if (ret)
			(*ret)->Update(KF);
		else
		{
			AdvViz::SDK::ITimelineKeyframe::KeyframeData KF2(KF);
			KF2.time = fTime;
			ret = AddKeyframe(KF2);
		}
		return *ret;
	}

	void MoveKeyFrame(float fOldTime, float fNewTime)
	{
		if (fOldTime == fNewTime)
			return;

		auto ret = GetKeyframe(fOldTime);
		if (ret)
		{
			AdvViz::SDK::ITimelineKeyframe::KeyframeData KF((*ret)->GetData());
			RemoveKeyframe(*ret);
			KF.time = fNewTime;
			AddKeyframe(KF);
		}
	}

	void GetKeyFrameTimes(TArray<float>& vTimes)
	{
		vTimes.Empty();
		vTimes.Reserve(GetKeyframeCount());
		for (unsigned i = 0; i < GetKeyframeCount(); ++i)
		{
			auto kf = GetKeyframeByIndex(i);
			if (kf)
			{
				vTimes.Add((*kf)->GetData().time);
			}
		}
	}

	void GetKeyFrameDates(TArray<FDateTime> &vDates)
	{
		vDates.Empty();
		vDates.Reserve(GetKeyframeCount());
		for (unsigned i = 0; i < GetKeyframeCount(); ++i)
		{
			auto kf = GetKeyframeByIndex(i);
			if (kf && (*kf)->GetData().synchro)
			{
				vDates.Emplace(StrToDateTime((*kf)->GetData().synchro->date));
			}
		}
	}
}; // class ClipData

/*
class SynchroData
{
public:
	TRange<FDateTime> TotalRange_;
	TMap<float, TRange<FDateTime> > KFs_;

	SynchroData() : TotalRange_(TRange<FDateTime>(FDateTime::Now(), FDateTime::Now()))
	{}

	TRange<FDateTime> GetTotalRange() const
	{
		return TotalRange_;
	}

	void SetTotalRange(const TRange<FDateTime>& newRange)
	{
		TotalRange_ = newRange;
		KFs_.Empty();
		KFs_.Add(0.f, TotalRange_);
	}

	FDateTime GetSynchroTime(float fTime, float fTotalDuration)
	{
		TArray<float> vTimes;
		KFs_.GenerateKeyArray(vTimes);
		int32 idx(0);
		while (idx < vTimes.Num() && vTimes[idx] <= fTime)
			idx++;
		float TimeStart = vTimes[idx];
		float TimeEnd = (idx + 1 < vTimes.Num()) ? vTimes[idx + 1] : fTotalDuration;
		FDateTime DateStart = KFs_[TimeStart].GetLowerBoundValue();
		FDateTime DateEnd = KFs_[TimeStart].GetUpperBoundValue();
		if (DateStart == DateEnd)
			return DateStart;
		//time_h = self._synchro_duration
		//	date_l = self._date_start if not keys[0] == 0.0 else KFs[keys[0]]
		//	date_h = self._date_end if not keys[-1] == self._synchro_duration else KFs[keys[-1]]

		//	# Find surrounding KFs
		//	ih = 0
		//	while ih < len(keys) and time > keys[ih]:
		//ih += 1
		//	il = ih - 1
		//	if il < len(keys) :
		//		time_l = keys[il]
		//		date_l = KFs[keys[il]]
		//		if ih < len(keys) :
		//			time_h = keys[ih]
		//			date_h = KFs[keys[ih]]
		//			if time_l == time_h :
		//				if time_h < self._synchro_duration :
		//					time_h = self._synchro_duration
		//					date_h = self._date_end if not keys[-1] == self._synchro_duration else KFs[keys[-1]]
		//					elif time_l > 0.0 :
		//					time_l = 0.0
		//					date_l = self._date_start if not keys[0] == 0.0 else KFs[keys[0]]
		//					self._log_fn(f"Synchro time conversion: t={time}, {il}: {time_l}/{date_l}, {ih}: {time_h}/{date_h}, {KFs}")
		//					coef = self._synchro_duration / (self._date_end - self._date_start)
		//					tl = coef * (date_l - self._date_start)
		//					th = coef * (date_h - self._date_start)
		//					return tl + (th - tl) * (time - time_l) / (time_h - time_l)
		return TotalRange_.GetLowerBoundValue(); //todo
	}

	void BreakSynchroTimeline(float fTime)
	{

	}
};
*/

class Timeline : public AdvViz::SDK::Timeline, public AdvViz::SDK::Tools::TypeId<Timeline>
{
public:
	Timeline() {}

	using AdvViz::SDK::Tools::TypeId<Timeline>::GetTypeId;
	std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
	bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || AdvViz::SDK::Timeline::IsTypeOf(i); }
};

class AITwinTimelineActor::FImpl
{
public:
	AITwinTimelineActor& Owner;
	FString levelSequencePath_;
	TStrongObjectPtr<ULevelSequence> pLevelSeq_;
	TStrongObjectPtr<ULevelSequencePlayer> pPlayer_;
	TStrongObjectPtr<ALevelSequenceActor> pPlayerActor_;
	TStrongObjectPtr<AActor> pSynchroActor_;
	std::function<TMap<FString, UITwinSynchro4DSchedules*> const& ()> GetSchedules;

	ACineCameraActor* CurrentCutTrackCamera_ = nullptr; 
	TArray<float> CurrentCutTrackStartTimes_;

	TArray<USequencerHelper::FTrackInfo> AnimTracksInfo_; // map of all the parameters animated by the timeline to their type

	typedef AdvViz::SDK::Timeline BaseClass;

	std::shared_ptr<AdvViz::SDK::ITimeline> timeline_; // timeline data stored on the server

	int nextFreeClipID_ = 0;
	int curClip_ = -1.f; ///< index of the current clip in the clip array
	float curTime_ = 0.f; ///< current clip time in seconds (don't confuse with schedule time/date)
	bool isLooping_ = false;
	
	std::shared_ptr<AdvViz::SDK::ITimelineKeyframe> copiedKF_; // used for copy-paste
	//SynchroData synchroData_; // for now, we simply assign current UI date to each new key-frame

	FImpl(AITwinTimelineActor& InOwner) : Owner(InOwner)
	{
		// Find predefined timeline-related level sequence (its creation is only possible in editor mode)
		levelSequencePath_ = FString("/ITwinForUnreal/ITwin/AnimTimeline/ITwinLevelSequence");
		pLevelSeq_.Reset(Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath_)));
		CreatePlayer();

		timeline_.reset(AdvViz::SDK::Timeline::New());

		// Fill up description of all the parameters animated by the timeline (names and track types they use)
		AnimTracksInfo_.Add(USequencerHelper::FTrackInfo(TEXT("transform"), USequencerHelper::TT_Transform));
		AnimTracksInfo_.Add(USequencerHelper::FTrackInfo(TEXT("date"), USequencerHelper::TT_Double));
		AnimTracksInfo_.Add(USequencerHelper::FTrackInfo(TEXT("date_sun"), USequencerHelper::TT_Double));
		AnimTracksInfo_.Add(USequencerHelper::FTrackInfo(TEXT("clouds"), USequencerHelper::TT_Float));
		AnimTracksInfo_.Add(USequencerHelper::FTrackInfo(TEXT("fog"), USequencerHelper::TT_Float)); // = Fog
		//std::optional<bool> UseHeliodon;
		//std::optional<FDateTime> HeliodonDate;
		//std::optional<float> HeliodonLong;
		//std::optional<float> HeliodonLat;
		//std::optional<float> WindOrientation;
		//std::optional<float> WindForce;
		//std::optional<float> Exposure;
	}

	~FImpl()
	{
	}

	void CreatePlayer()
	{
		if (pLevelSeq_.IsValid())
		{
			ALevelSequenceActor* pPlayerActor;
			// Create player for the sequence
			FMovieSceneSequencePlaybackSettings s;
			s.bPauseAtEnd = false;
			s.bDisableCameraCuts = false; // Ensure camera cuts are processed
			pPlayer_.Reset(ULevelSequencePlayer::CreateLevelSequencePlayer(Owner.GetWorld(), pLevelSeq_.Get(), s, pPlayerActor));
			pPlayerActor_.Reset(pPlayerActor);
		}

		if (pPlayer_.IsValid())
		{
			pLevelSeq_->MovieScene->SetDisplayRate(FFrameRate(30, 1)); // Match desired playback frame rate
			pLevelSeq_->MovieScene->SetEvaluationType(EMovieSceneEvaluationType::WithSubFrames);
			pPlayer_->SetFrameRate(FFrameRate(30, 1)); // Set to match 30 FPS
			//pPlayer_->ForceUpdate();
			pPlayer_->OnPlay.AddDynamic(&Owner, &AITwinTimelineActor::OnPlaybackStarted);
		}
	}

	void ConvertToSequencer(const AdvViz::SDK::ITimelineKeyframe::KeyframeData & KF, TArray<std::optional<USequencerHelper::KFValueType> >& Out)
	{
		Out.Empty();
		for (auto Track : AnimTracksInfo_)
		{
			Out.Add(std::nullopt);
			if (Track.sName.Compare(TEXT("transform")) == 0)
			{
				if (KF.camera.has_value())
				{
					FTransform transf;
					GetFTransform(KF.camera->transform, transf);
					Out.Last() = std::make_optional(transf);
				}
			}
			else if (Track.sName.Compare(TEXT("date")) == 0)
			{
				if (KF.synchro.has_value())
				{
					auto synchro = ITwin::SynchroToTimeline(StrToDateTime(KF.synchro->date));
					Out.Last() = std::make_optional(synchro);
				}
			}
			else if (Track.sName.Compare(TEXT("date_sun")) == 0)
			{
				if (KF.atmo.has_value())
				{
					auto date = ITwin::SynchroToTimeline(StrToDateTime(KF.atmo->time));
					Out.Last() = std::make_optional(date);
				}
			}
			else if (Track.sName.Compare(TEXT("clouds")) == 0)
			{
				if (KF.atmo.has_value())
				{
					Out.Last() = std::make_optional(KF.atmo->cloudCoverage);
				}
			}
			else if (Track.sName.Compare(TEXT("fog")) == 0)
			{
				if (KF.atmo.has_value())
				{
					Out.Last() = std::make_optional(KF.atmo->fog);
				}
			}
		}
	}
	
	void OnLoad()
	{
		//finalize camera clip
		for (size_t i = 0; i < timeline_->GetClipCount(); ++i)
		{
			auto clip = GetClip(i);
			clip->InitCamera(Owner.GetWorld());
			USequencerHelper::AddNewClip(clip->GetCamera(), levelSequencePath_, AnimTracksInfo_, clip->GetTracks());
			for (size_t j = 0; j < clip->GetKeyframeCount(); ++j)
			{
				auto kf = clip->GetKeyframeByIndex(j);
				if (!kf)
					continue;
				const auto& KF = (*kf)->GetData();
				BE_ASSERT(KF.camera.has_value());
				BE_ASSERT(KF.synchro.has_value());
				TArray<std::optional<USequencerHelper::KFValueType> > ParamValues;
				ConvertToSequencer(KF, ParamValues);
				USequencerHelper::AddKeyFrame(clip->GetTracks(), levelSequencePath_, KF.time, ParamValues);
			}
		}
		SetCurrentClip(-1); // do not modify current scene state (camera, synchro, atmosphere) after loading the timeline data
	}

	bool IsReady() const
	{
		return pLevelSeq_.IsValid();
	}

	ClipData* GetCurrentClip()
	{
		return GetClip(curClip_);
	}

	bool SetCurrentClip(int clipIdx, bool updateSceneFromTimeline = true)
	{
		if (clipIdx >= 0 && clipIdx < (int)timeline_->GetClipCount() && clipIdx != curClip_)
		{
			curClip_ = clipIdx;
			if (updateSceneFromTimeline)
				SetCurrentTime(0.f);
			else
				curTime_ = 0.f;
			//USequencerHelper::AdjustMoviePlaybackRange(GetCurrentCamera(), levelSequencePath_); // TODO: to check
			return true;
		}
		else if (clipIdx < 0)
		{
			curClip_ = -1;
			curTime_ = 0.f;
			return true;
		}
		return false;
	}

	float GetClipDuration(int clipIdx)
	{
		float fDuration(0.f);
		if (clipIdx >= 0 && clipIdx < timeline_->GetClipCount())
		{
			auto clip = GetClip(clipIdx);
			if (clip)
				fDuration = clip->GetDuration();
			// There are two ways to obtain clip duration: from SDK timeline's key frame times or from Unreal sequencer.
			// The result should be the same except for the case when the clip times are manually shifted to fit into a single camera cut track - see AssembleClips()
			// That's why we use the first way here, although the line below can be still useful to debug sequencer-related issues:
			//return USequencerHelper::GetEndTime(clip->GetCamera(), levelSequencePath_);
		}
		return fDuration;
	}

	float GetTotalDuration()
	{
		float fTotalDuration(0.f);
		for (size_t i(0); i < timeline_->GetClipCount(); i++)
		{
			auto clip = GetClip(i);
			if (clip && clip->IsEnabled())
				fTotalDuration += clip->GetDuration();
		}
		return fTotalDuration;
	}

	// Get starting time of the clip within the sequence of clips
	float GetClipStartTime(int clipIdx)
	{
		if (clipIdx >= timeline_->GetClipCount())
			return 0.f;

		float fStartTime(0.f);
		for(size_t i(1); i <= clipIdx; i++)
		{
			auto clip = GetClip(i-1);
			if (clip && clip->IsEnabled())
				fStartTime += clip->GetDuration();
		}
		return fStartTime;
	}

	void GetClipsStartTimes(TArray<float> &vTimes, bool bAppendLastDuration = false)
	{
		vTimes.Empty();
		if (timeline_->GetClipCount() == 0)
			return;

		float fAccumTime(0.f);
		for(size_t i(0); i < timeline_->GetClipCount(); i++)
		{
			auto clip = GetClip(i);
			if (clip)
			{
				vTimes.Add(fAccumTime);
				if (clip->IsEnabled())
					fAccumTime += clip->GetDuration();
			}
		}
		if (bAppendLastDuration)
			vTimes.Add(fAccumTime); // append theoretical start time of the next clip
	}

	std::shared_ptr<AdvViz::SDK::ITimelineClip> AppendClip(FString sName = FString())
	{
		if (sName.IsEmpty())
		{
			do {
				sName = FString::Printf(TEXT("Clip_%d"), ++nextFreeClipID_);
			} while (GetClipIndex(sName) >= 0);
		}
		auto clip = timeline_->AddClip(TCHAR_TO_UTF8(*sName));
		BE_ASSERT(clip);
		auto clipdata = AdvViz::SDK::Tools::DynamicCast<ClipData>(clip.get());
		BE_ASSERT(clipdata);
		clipdata->InitCamera(Owner.GetWorld());
		USequencerHelper::AddNewClip(clipdata->GetCamera(), levelSequencePath_, AnimTracksInfo_, clipdata->GetTracks());
		curClip_ = timeline_->GetClipCount() - 1;
		curTime_ = 0.f;
		return clip;
	}

	void RemoveClip(int clipIdx)
	{
		if (clipIdx < 0)
			clipIdx = curClip_;
		if (clipIdx >= 0 && clipIdx < (int)timeline_->GetClipCount())
		{
			auto clip = GetClip(clipIdx);

			bool bRes;
			FString outMsg;
			USequencerHelper::RemoveAllTracksFromLevelSequence(clip->GetCamera(), levelSequencePath_, bRes, outMsg);
			USequencerHelper::RemovePActorFromLevelSequence(clip->GetCamera(), levelSequencePath_, bRes, outMsg);
			//clip->GetCamera()->Destroy();

			timeline_->RemoveClip(curClip_);
			auto const count = (int)timeline_->GetClipCount();
			curClip_ = (count > 0) ? 0 : -1;
			if (count >= 0 && clipIdx == count)
			{
				--nextFreeClipID_;
			}
		}
	}

	void MoveClip(size_t indexSrc, size_t indexDst)
	{
		timeline_->MoveClip(indexSrc, indexDst);
	}

	ClipData* GetClip(int clipIdx)
	{
		if (clipIdx >= 0 && clipIdx < timeline_->GetClipCount())
		{
			auto clip = timeline_->GetClipByIndex(clipIdx);
			BE_ASSERT(clip);
			if (!clip)
				return nullptr;

			auto clipdata = AdvViz::SDK::Tools::DynamicCast<ClipData>(*clip);
			BE_ASSERT(clipdata);
			return clipdata.get();
		}
		return nullptr;
	}

	int GetClipIndex(FString sName) const
	{
		std::string s(TCHAR_TO_UTF8(*sName));
		for (size_t i(0); i < timeline_->GetClipCount(); i++)
		{
			auto ret = timeline_->GetClipByIndex(i);
			if ((*ret)->GetName() == s)
			{
				return (int)i;
			}
		}
		return -1;
	}

	size_t GetClipsNum() const
	{
		return timeline_->GetClipCount();
	}

	ClipData* FindClipByCamera(AActor* pCamera)
	{
		for (size_t i(0); i < GetClipsNum(); i++)
		{
			auto clip = GetClip(i);
			if (clip->GetCamera() == pCamera)
				return clip;
		}
		return nullptr;
	}

	void GetClipsNames(TArray<FString>& vClipNames)
	{
		vClipNames.Empty();
		vClipNames.Reserve(GetClipsNum());
		for (size_t i(0); i < GetClipsNum(); i++)
		{
			auto clip = GetClip(i);
			vClipNames.Add(clip->GetNameU());
		}
	}

	ACameraActor* GetCurrentCamera()
	{
		auto clip = GetCurrentClip();
		// BE_ASSERT(clip); // A default scene has no clip, and this case must be handled!
		return clip ? clip->GetCamera() : nullptr;
	}

	bool HasKeyFrameToPaste() const
	{
		return (bool)copiedKF_;
	}

	void CopyKeyFrame(int clipIdx, int iKF)
	{
		copiedKF_.reset();
		if (auto clip = GetClip(clipIdx))
		{
			if (auto kf = clip->GetKeyframeByIndex(iKF))
			{
				copiedKF_ = *(kf);
			}
		}
	}

	void PasteKeyFrame(int clipIdx, int iKF)
	{
		if (!copiedKF_)
			return;

		auto clip = (clipIdx >= 0) ? GetClip(clipIdx) : GetCurrentClip();
		if (!clip)
			return;
		auto ret = clip->GetKeyframeByIndex(iKF);
		if (ret)
			AddOrUpdateKeyFrame((*ret)->GetData().time, clipIdx, copiedKF_->GetData());
	}

	// Add/update the specified clip's key-frame with given parameters
	std::shared_ptr<AdvViz::SDK::ITimelineKeyframe> AddOrUpdateKeyFrame(float fTime, int clipIdx, const AdvViz::SDK::ITimelineKeyframe::KeyframeData &KF)
	{
		auto clip = GetClip(clipIdx);
		if (!clip)
			return nullptr;

		// Update Unreal level sequence (TODO: manage atmosphere settings?)
		BE_ASSERT(KF.camera.has_value());
		BE_ASSERT(KF.synchro.has_value());
		//FTransform transf;
		//GetFTransform(KF.camera->transform, transf);
		//auto synchro = ITwin::SynchroToTimeline(StrToDateTime(KF.synchro->date));
		TArray<std::optional<USequencerHelper::KFValueType> > ParamValues;
		ConvertToSequencer(KF, ParamValues);
		fTime = USequencerHelper::AddKeyFrame(clip->GetTracks(), levelSequencePath_, fTime, ParamValues);
		auto ret = clip->AddOrUpdateKeyFrame(fTime, KF);
		// We may have changed the timeline in a way that affects the scene at the current time, no?
		// For example, when pasting over the currently selected keyframe, the scene was not updated, not even
		// when capturing the new snapshot after the paste (azdev#1608359, second fix)
		UpdateSceneFromTimeline();
		return ret;
	}

	bool GetSynchroDateFromAvailableSchedules(FDateTime& Out, FString& ScheduleIDOut)
	{
		if (!GetSchedules)
			return false;
		return ITwin::GetSynchroDateFromSchedules(GetSchedules(), Out, ScheduleIDOut);
	}

	// Add/update the specified clip's key-frame with current scene state
	std::shared_ptr<AdvViz::SDK::ITimelineKeyframe> AddOrUpdateKeyFrame(float fTime, int clipIdx)
	{
		AdvViz::SDK::ITimelineKeyframe::KeyframeData KF;
		KF.time = fTime;

		KF.camera.emplace();
		GetSDKTransform(ScreenUtils::GetCurrentViewTransform(Owner.GetWorld()), KF.camera->transform);
		
		KF.synchro.emplace();
		FDateTime date;
		FString scheduleID;
		if (GetSynchroDateFromAvailableSchedules(date, scheduleID))
		{
			DateTimeToStr(date, KF.synchro->date);
			KF.synchro->scheduleId = TCHAR_TO_UTF8(*scheduleID);
		}

		if (Owner.GetAtmoSettingsDelegate.IsBound())
		{
			FAtmoAnimSettings data;
			Owner.GetAtmoSettingsDelegate.Execute(data);
			KF.atmo.emplace();
			DateTimeToStr(data.heliodonDate, KF.atmo->time);
			KF.atmo->cloudCoverage = data.cloudCoverage;
			KF.atmo->fog = data.fog;
		}

		return AddOrUpdateKeyFrame(fTime, clipIdx, KF);
	}

	void RemoveKeyFrame(int iKF, int clipIdx)
	{
		auto clip = GetClip(clipIdx);
		if (!clip)
			return;

		float const fTime = clip->GetKeyFrameTime(iKF);
		USequencerHelper::RemoveKeyFrame(clip->GetTracks(), levelSequencePath_, fTime);
		auto key = clip->GetKeyframe(fTime);
		if (key)
			clip->RemoveKeyframe(*key);
		// If we delete the first or last keyframe, we change the total duration, whereas in other cases,
		// we actually sum the durations before and after the deleted keyframe.
		// For the last keyframe, there's nothing particular to do, but for the first, let's shift all
		// keyframes so that the new first is at time 0: this assumption is made everywhere and it seems
		// safest to keep it, even though it means modifying *all* keyframes just to erase one :/
		if (0 == iKF && clip->GetKeyframeCount() >= 2)
			MoveKeyFrame(clip->GetKeyFrameTime(0/*former 1, now 0*/), fTime, clipIdx, false);
		else
			UpdateSceneFromTimeline(); // see comment in AddOrUpdateKeyFrame
	}

	void MoveKeyFrame(float fOldTime, float fNewTime, int clipIdx, bool bMoveOneKFOnly)
	{
		auto clip = GetClip(clipIdx);
		if (!clip || AdvViz::SDK::RoundTime(fOldTime) == AdvViz::SDK::RoundTime(fNewTime))
			return;

		float const fTimeDelta = fNewTime - fOldTime;
		float lastKFToMove =
			bMoveOneKFOnly ? fOldTime : USequencerHelper::GetEndTime(clip->GetCamera(), levelSequencePath_);
		USequencerHelper::ShiftClipKFsInRange(clip->GetTracks(), levelSequencePath_, fOldTime, lastKFToMove,
											  fTimeDelta);
		if (bMoveOneKFOnly)
			clip->MoveKeyFrame(fOldTime, fNewTime);
		else
		{
			TArray<float> vTimes;
			clip->GetKeyFrameTimes(vTimes);
			if (fTimeDelta > 0)
				for (int i = vTimes.Num() - 1; i >= 0 && vTimes[i] >= fOldTime; i--)
					clip->MoveKeyFrame(vTimes[i], vTimes[i] + fTimeDelta);
			else
				for (int i = 0; i < vTimes.Num(); i++)
					if (vTimes[i] >= fOldTime)
						clip->MoveKeyFrame(vTimes[i], vTimes[i] + fTimeDelta);
		}
		UpdateSceneFromTimeline(); // see comment in AddOrUpdateKeyFrame
	}

	void ImportFromJson()
	{
#if 0
		FTimelineData data = UJsonUtils::ReadStructFromJsonFile(ITwin::GetTimelineDataPath());
		if (data.vClips.Num() > 0)
		{
			vClips_.Empty();
			for (auto &clip : data.vClips)
			{
				auto newClip = AppendClip(clip.sName);
				newClip->SetEnabled(clip.isEnabled);
				for (auto &kf : clip.vKFs)
				{
					AddOrUpdateKeyFrame(kf.fTime, GetClipsNum()-1, kf);
				}
			}
		}
#endif
	}

	void ExportToJson()
	{
#if 0
		FTimelineData data;
		for (auto &clip : vClips_)
		{
			FClipData clipData;
			clipData.sName = clip->GetName();
			clipData.isEnabled = clip->IsEnabled();
			TArray<float> vTimes;
			clip->GetKeyFrameTimes(vTimes);
			for (auto t : vTimes)
			{
				FKeyFrameData kfData = *(clip->GetKeyFrame(t));
				kfData.fTime = t;
				clipData.vKFs.Add(kfData);
			}
			data.vClips.Add(clipData);
		}
		if (data.vClips.Num() > 0)
			UJsonUtils::WriteStructToJsonFile(ITwin::GetTimelineDataPath(), data);
#endif
	}

	void UpdateCameraFromTime(AActor* pCamera, float fTime)
	{
		FVector pos;
		FRotator rot;
		bool bSuccess = USequencerHelper::GetTransformAtTime(pCamera, levelSequencePath_, fTime, pos, rot);
		if (bSuccess)
		{
			BE_LOGV("Timeline", "Time set to " << fTime << ", setting current view to: "
				<< "Rotation (" << rot.Yaw << ", " << rot.Pitch << ", " << rot.Roll
				<< "), Position (" << pos.X << ", " << pos.Y << ", " << pos.Z << ")");
			ScreenUtils::SetCurrentView(Owner.GetWorld(), pos, rot);
		}
		else
		{
			BE_LOGW("Timeline", "Failed to compute transform when setting time to " << fTime);
		}
	}

	/// \param Out If there is no "current time" (ie no clip), the current schedule date found on any iModel
	///		with a schedule is used. If there is on schedule either, ITwin::GetBaseDate() is used.
	/// \return true if the date returned comes from the animation, false if any fallback value was used
	bool GetSynchroDateFromTime(ClipData* clip, float fTime, FDateTime& Out)
	{
		double dateDelta(0);
		if (clip)
		{
			auto idx = AnimTracksInfo_.IndexOfByKey(TEXT("date"));
			if (idx != INDEX_NONE && idx < clip->GetTracks().Num())
			{
				USequencerHelper::GetDoubleValueAtTime(clip->GetTracks()[idx].Get(), levelSequencePath_, fTime, dateDelta);
				Out = ITwin::TimelineToSynchro(dateDelta);
				return true;
			}
		}
		
		if (GetSchedules)
		{
			FString scheduleId;
			if (GetSynchroDateFromAvailableSchedules(Out, scheduleId))
				return false; // yes, false, see dox
		}

		Out = ITwin::GetBaseDate();
		return false;
	}

	void UpdateSynchroDateFromTime(ClipData* clip, float fTime)
	{
		if (!GetSchedules)
			return;

		FDateTime curDate;
		if (GetSynchroDateFromTime(clip, fTime, curDate))
		{
			BE_LOGV("Timeline", "Time set to " << fTime << ", setting current date to "
				<< TCHAR_TO_UTF8(*(curDate.ToFormattedString(TEXT("%d %b %Y")))));
			ITwin::SetSynchroDateToSchedules(GetSchedules(), curDate);
		}
		else
		{
			BE_LOGW("Timeline", "Failed to compute date when setting time to to " << fTime);
		}
	}

	void UpdateAtmoFromTime(ClipData* clip, float fTime)
	{
		if (!clip || !Owner.GetAtmoSettingsDelegate.IsBound() || !Owner.SetAtmoSettingsDelegate.IsBound())
			return;

		FAtmoAnimSettings data;
		Owner.GetAtmoSettingsDelegate.Execute(data);

		auto idx = AnimTracksInfo_.IndexOfByKey(TEXT("date_sun"));
		if (idx != INDEX_NONE && idx < clip->GetTracks().Num())
		{
			double dateDelta;
			USequencerHelper::GetDoubleValueAtTime(clip->GetTracks()[idx].Get(), levelSequencePath_, fTime, dateDelta);
			if(fabs(dateDelta) > 1e-6)
				data.heliodonDate = ITwin::TimelineToSynchro(dateDelta);
		}
		idx = AnimTracksInfo_.IndexOfByKey(TEXT("clouds"));
		if (idx != INDEX_NONE && idx < clip->GetTracks().Num())
		{
			float Value;
			USequencerHelper::GetFloatValueAtTime(clip->GetTracks()[idx].Get(), levelSequencePath_, fTime, Value);
			data.cloudCoverage = Value;
		}
		idx = AnimTracksInfo_.IndexOfByKey(TEXT("fog"));
		if (idx != INDEX_NONE && idx < clip->GetTracks().Num())
		{
			float Value;
			USequencerHelper::GetFloatValueAtTime(clip->GetTracks()[idx].Get(), levelSequencePath_, fTime, Value);
			data.fog = Value;
		}
		Owner.SetAtmoSettingsDelegate.Execute(data);
	}

	void SetCurrentTime(float fTime)
	{
		curTime_ = fTime;
		if (curTime_ >= 0)//-1 is special temporary stu
			UpdateSceneFromTimeline();
	}

	void UpdateSceneFromTimeline()
	{
		bool isCameraCutActive = USequencerHelper::HasCameraCutTrack(levelSequencePath_);
		APlayerController* pController = UGameplayStatics::GetPlayerController(Owner.GetWorld(), 0);
		// In PIE playback or export are managed by the sequencer via the camera cut track: the switch between cameras happens automatically.
		// In Game-only however this doesn't happen, so the active cameras are neither accessible as controller view target, nor via OnCameraCutEvent,
		// and the commented lines below would not work. Instead we can either obtain active camera from the current section of the camera cut track,
		// or get active clip index from the clip start times.
		//auto pCamera = (isCameraCutActive && pController) ? pController->GetViewTarget() : GetCurrentCamera();
		//auto pCamera = (isCameraCutActive && CurrentCutTrackCamera_) ? CurrentCutTrackCamera_ : GetCurrentCamera();
		ClipData* pClip = nullptr;
		AActor* pCamera = nullptr;
		float ClipStartTime(0.f);
		if (isCameraCutActive)
		{
			// All clips are combined in one camera cut track -> find the clip that corresponds to the current playback time
			for (size_t i(1); i < CurrentCutTrackStartTimes_.Num(); i++)
			{
				if (curTime_ < CurrentCutTrackStartTimes_[i])
				{
					pClip = GetClip(i-1);
					pCamera = pClip->GetCamera();
					ClipStartTime = CurrentCutTrackStartTimes_[i-1];
					break;
				}
			}
		}
		else
		{
			// Use current clip
			pClip = GetCurrentClip();
			pCamera = GetCurrentCamera();
		}

		if (!pClip || !pCamera || pClip->GetKeyframeCount() == 0 || curTime_ > ClipStartTime + pClip->GetDuration())
			return;

		//if (!isCameraCutActive) // for same reason as above, this would only work in PIE mode
		UpdateCameraFromTime(pCamera, curTime_);
		if (pClip->bSynchroAnim)
			UpdateSynchroDateFromTime(pClip, curTime_);
		if (pClip->bAtmoAnim)
			UpdateAtmoFromTime(pClip, curTime_);

		Owner.OnSceneFromTimelineUpdate();
	}

}; // class AITwinTimelineActor::FImpl

AITwinTimelineActor::AITwinTimelineActor()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("root")));
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	static bool bNewInit = false;
	if (!bNewInit)
	{
		bNewInit = true;
		AdvViz::SDK::Timeline::SetNewFct([]() {
			return static_cast<AdvViz::SDK::Timeline*>(new Timeline);
			});

		AdvViz::SDK::TimelineClip::SetNewFct([]() {
			return static_cast<AdvViz::SDK::TimelineClip*>(new ClipData);
			});
	}
}

ULevelSequencePlayer* AITwinTimelineActor::GetPlayer()
{
	return Impl->pPlayer_.Get();
}

ULevelSequence* AITwinTimelineActor::GetLevelSequence()
{
	return Impl->pLevelSeq_.Get();
}

//AITwinTimelineActor* AITwinTimelineActor::Get()
//{
//	auto pWorld = GEngine->GetWorldContextFromGameViewport(GEngine->GameViewport)->World();
//	auto pActor = Cast<AITwinTimelineActor>(UGameplayStatics::GetActorOfClass(pWorld, AITwinTimelineActor::StaticClass()));
//	if (!pActor)
//	{
//		FActorSpawnParameters param;
//		param.Name = FName("iTwinTimelineActor");
//		pActor = pWorld->SpawnActor<AITwinTimelineActor>(AITwinTimelineActor::StaticClass(), param);
//	}
//	return pActor;
//}

// Called when the game starts or when spawned
void AITwinTimelineActor::BeginPlay()
{
	Super::BeginPlay();

	SetActorTickEnabled(false);

	Impl = MakePimpl<FImpl>(*this);
}

void AITwinTimelineActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Impl.Reset();
	Super::EndPlay(EndPlayReason);
}

// Called every frame, used to update UI during playback
void AITwinTimelineActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (Impl->pPlayer_->IsPlaying())
	{
		float fPlayerTime = Impl->pPlayer_->GetCurrentTime().AsSeconds();
		//auto [clipIdx, clipTime] = GetClipIdxAndTimeWithinSequence(fPlayerTime);
		//Impl->curClip_ = clipIdx;
		//Impl->SetCurrentTime(clipTime);
		Impl->SetCurrentTime(fPlayerTime); // update synchro, atmo etc. from current time
		if (fPlayerTime > USequencerHelper::GetPlaybackEndTime(Impl->levelSequencePath_))
			Impl->pPlayer_->Stop();
	}
	else
	{
		SetActorTickEnabled(false);
		Impl->pPlayer_->OnCameraCut.RemoveDynamic(this, &AITwinTimelineActor::OnCameraCutHandler);
	}
}

//TSharedPtr<AITwinTimelineActor::FImpl> AITwinTimelineActor::GetImpl()
//{
//	if (!Impl)
//		Impl = TSharedPtr<FImpl>(new FImpl(GetWorld()));
//	return Impl;
//}

//const TSharedPtr<AITwinTimelineActor::FImpl> AITwinTimelineActor::GetImpl() const
//{
//	return Impl;
//}

void AITwinTimelineActor::ExportData()
{
	Impl->ExportToJson();
}

void AITwinTimelineActor::ImportData()
{
	RemoveAllClips();

	Impl->ImportFromJson();
}

void AITwinTimelineActor::RemoveAllClips(bool bRemoveEmptyOnly/* = false*/)
{
	if (!Impl->IsReady())
		return;
	for (int i(Impl->GetClipsNum()-1); i >= 0; i--)
	{
		if (!bRemoveEmptyOnly || Impl->GetClip(i)->GetKeyframeCount() == 0)
			Impl->RemoveClip(i);
	}
}

void AITwinTimelineActor::RemoveAllKeyframes(int clipIdx)
{
	if (!Impl->IsReady())
		return;

	auto clip = Impl->GetCurrentClip();
	if (!clip)
		return;

	while(clip->GetKeyframeCount() > 0)
	{
		Impl->RemoveKeyFrame(clip->GetKeyframeCount()-1, clipIdx);
	}
}

void AITwinTimelineActor::AddClip()
{
	if (!Impl->IsReady())
		return;
	// Append a new clip and set it as current
	Impl->AppendClip();
	// Clip should always have at least one key-frame (--> no, user may want to create his animation entirely from saved views)
	//AddKeyFrame();
}

void AITwinTimelineActor::RemoveClip(int clipIdx)
{
	if (!Impl->IsReady())
		return;
	Impl->RemoveClip(clipIdx);
}

void AITwinTimelineActor::MoveClip(size_t indexSrc, size_t indexDst)
{
	if (!Impl->IsReady())
		return;
	Impl->MoveClip(indexSrc, indexDst);
}

void AITwinTimelineActor::EnableClip(bool bEnable, int clipIdx)
{
	if (!Impl->IsReady())
		return;
	if (auto Clip = Impl->GetClip(clipIdx))
		return Clip->SetEnable(bEnable);
}

void AITwinTimelineActor::EnableAllClips(bool bEnable)
{
	if (!Impl->IsReady())
		return;

	for (int i(Impl->GetClipsNum() - 1); i >= 0; i--)
		Impl->GetClip(i)->SetEnable(bEnable);
}

bool AITwinTimelineActor::IsClipEnabled(int clipIdx) const
{
	if (!Impl->IsReady())
		return false;
	if (auto Clip = Impl->GetClip(clipIdx))
		return Clip->IsEnabled();
	return false;
}

void AITwinTimelineActor::SetClipName(int clipIdx, FString ClipName)
{
	auto clip = Impl->GetClip(clipIdx);
	if (clip)
		clip->SetNameU(ClipName);
}

FString AITwinTimelineActor::GetClipName(int clipIdx) const
{
	auto clip = Impl->GetClip(clipIdx);
	return clip ? clip->GetNameU() : FString();
}

void AITwinTimelineActor::GetClipsNames(TArray<FString>& vClipNames) const
{
	Impl->GetClipsNames(vClipNames);
}

void AITwinTimelineActor::GetClipsStartTimes(TArray<float>& vTimes, bool bAppendLastDuration/* = false*/) const
{
	Impl->GetClipsStartTimes(vTimes, bAppendLastDuration);
}

float AITwinTimelineActor::GetClipStartTime(int clipIdx) const
{
	return Impl->GetClipStartTime(clipIdx);
}

int AITwinTimelineActor::GetClipsNum() const
{
	return Impl->GetClipsNum();
}

bool AITwinTimelineActor::SetCurrentClip(FString clipName, bool updateSceneFromTimeline/* = true*/)
{
	return Impl->SetCurrentClip(Impl->GetClipIndex(clipName), updateSceneFromTimeline);
}

bool AITwinTimelineActor::SetCurrentClip(int clipIdx, bool updateSceneFromTimeline/* = true*/)
{
	return Impl->SetCurrentClip(clipIdx, updateSceneFromTimeline);
}

int AITwinTimelineActor::GetCurrentClipIndex() const
{
	return Impl->curClip_;
}

ACameraActor* AITwinTimelineActor::GetClipCamera(int clipIdx)
{
	auto clip = Impl->GetClip(clipIdx);
	return clip ? clip->GetCamera() : nullptr;
}

void AITwinTimelineActor::SetClipSnapshotID(int clipIdx, const std::string& Id)
{
	auto clip = Impl->GetClip(clipIdx);
	if (clip)
		clip->SetSnapshotId(Id);
}

void AITwinTimelineActor::SetKeyFrameSnapshotID(int clipIdx, int iKF, const std::string& Id)
{
	auto clip = Impl->GetClip(clipIdx);
	if (clip)
	{
		auto kf = clip->GetKeyframeByIndex(iKF);
		if (kf)
			(*kf)->SetSnapshotId(Id);
	}
}

std::string AITwinTimelineActor::GetClipSnapshotID(int clipIdx)
{
	if (auto clip = Impl->GetClip(clipIdx))
	{
		auto snapshotId = clip->GetSnapshotId();
		if (snapshotId.empty())
		{
			snapshotId = std::string(TCHAR_TO_UTF8(*(FGuid::NewGuid().ToString().Left(10))));
			SetClipSnapshotID(clipIdx, snapshotId);
		}
		return snapshotId;
	}
	return std::string();
}

std::string AITwinTimelineActor::GetKeyFrameSnapshotID(int clipIdx, int iKF)
{
	if (auto clip = Impl->GetClip(clipIdx))
	{
		if (auto kf = clip->GetKeyframeByIndex(iKF))
		{
			auto snapshotId = (*kf)->GetSnapshotId();
			if (snapshotId.empty())
			{
				snapshotId = std::string(TCHAR_TO_UTF8(*(FGuid::NewGuid().ToString().Left(10))));
				SetKeyFrameSnapshotID(clipIdx, iKF, snapshotId);
			}
			return snapshotId;
		}
	}
	return std::string();
}

void AITwinTimelineActor::GetKeyFrameSnapshotIDs(int clipIdx, std::vector<std::string>& Ids)
{
	Ids.clear();
	if (auto clip = Impl->GetClip(clipIdx))
	{
		//clip->GetKeyFrameSnapshotIds(Ids);
		for(int i(0); i < clip->GetKeyframeCount(); i++)
			Ids.push_back(GetKeyFrameSnapshotID(clipIdx, i));
	}
}

//TRange<FDateTime> AITwinTimelineActor::GetSynchroRange() const
//{
//	if (!Impl || !Impl->IsReady())
//		return TRange<FDateTime>::Empty();
//	return Impl->synchroData_.GetTotalRange();
//}

//void AITwinTimelineActor::SetSynchroRange(const TRange<FDateTime>& newRange)
//{
//	if (!Impl || !Impl->IsReady())
//		return;
//	Impl->synchroData_.SetTotalRange(newRange);
//}

void AITwinTimelineActor::AddKeyFrame()
{
	if (!Impl->IsReady())
		return;

	auto clip = Impl->GetCurrentClip();
	if (!clip || clip->HasKeyFrame(Impl->curTime_))
		return;

	Impl->AddOrUpdateKeyFrame(Impl->curTime_, Impl->curClip_);
}

void AITwinTimelineActor::AppendKeyFrame()
{
	if (!Impl->IsReady())
		return;

	auto clip = Impl->GetCurrentClip();
	if (!clip)
		return;

	float fDuration = clip->GetDuration();// USequencerHelper::GetDuration(Impl->GetCurrentCamera(), Impl->levelSequencePath_);
	Impl->curTime_ = clip->GetKeyframeCount() > 0 ? (int)(fDuration * 100.f) / 100.f + ITwin::fDefaultTimeDelta : 0.f;
	Impl->AddOrUpdateKeyFrame(Impl->curTime_, Impl->curClip_);
}

void AITwinTimelineActor::UpdateKeyFrame(int iKF)
{
	if (!Impl->IsReady())
		return;

	auto clip = Impl->GetCurrentClip();
	if (!clip)
		return;

	if (iKF >= 0)
	{
		if (clip->HasKeyFrame(iKF))
			Impl->AddOrUpdateKeyFrame(clip->GetKeyFrameTime(iKF), Impl->curClip_);
	}
	else
	{
		if (clip->HasKeyFrame(Impl->curTime_))
			Impl->AddOrUpdateKeyFrame(Impl->curTime_, Impl->curClip_);
	}
}

void AITwinTimelineActor::RemoveKeyFrame(int iKF)
{
	if (!Impl->IsReady())
		return;

	auto clip = Impl->GetCurrentClip();
	if (!clip)// || clip->GetKeyframeCount() <= 1)
		return;

	if (iKF >= 0)
	{
		if (clip->HasKeyFrame(iKF))
			Impl->RemoveKeyFrame(iKF, Impl->curClip_);
	}
	else
	{
		if (clip->HasKeyFrame(Impl->curTime_))
			Impl->RemoveKeyFrame(clip->GetKeyFrameIndex(Impl->curTime_), Impl->curClip_);
	}
	//SetCurrentTime(USequencerHelper::GetDuration(Impl->GetCurrentCamera(), Impl->levelSequencePath_));
}

int AITwinTimelineActor::GetKeyframeCount() const
{
	auto clip = Impl->GetCurrentClip();
	return clip ? (int)clip->GetKeyframeCount() : 0;
}

int AITwinTimelineActor::GetTotalKeyframeCount() const
{
	int nKFTotal(0);
	for (size_t i(0); i < Impl->GetClipsNum(); i++)
	{
		auto clip = Impl->GetClip(i);
		if (clip && clip->IsEnabled())
			nKFTotal += (int)clip->GetKeyframeCount();
	}
	return nKFTotal;
}

void AITwinTimelineActor::GetKeyFrameTimes(TArray<float>& vTimes) const
{
	if (auto clip = Impl->GetCurrentClip())
		clip->GetKeyFrameTimes(vTimes);
}

void AITwinTimelineActor::GetKeyFrameDates(TArray<FDateTime>& vDates) const
{
	if (auto clip = Impl->GetCurrentClip())
		clip->GetKeyFrameDates(vDates);
}

//void AITwinTimelineActor::GetKeyFrameTextures(TArray<UTexture2D*>& vTextures)
//{
//	if (Impl->GetCurrentClip())
//		Impl->GetCurrentClip()->GetKeyFrameTextures(vTextures, pSnapshotManager_);
//}

bool AITwinTimelineActor::HasKeyFrameToPaste() const
{
	return Impl->HasKeyFrameToPaste();
}

float AITwinTimelineActor::GetCurrentTime() const
{
	return Impl->curTime_;
}

FDateTime AITwinTimelineActor::GetCurrentDate() const
{
	FDateTime curDate;
	Impl->GetSynchroDateFromTime(Impl->FindClipByCamera(Impl->GetCurrentCamera()), Impl->curTime_, curDate);
	return curDate;
}

void AITwinTimelineActor::OnSceneFromTimelineUpdate()
{
	UpdateFromTimelineEvent.Broadcast();
}

void AITwinTimelineActor::SetCurrentTime(float fTime)
{
	if (!Impl->IsReady())// || Impl->curTime_ == fTime) // even if time is the same, clip index could have changed
		return;

	if (Impl->pPlayer_->IsPlaying())
		return; // handled in Tick()

	// Set the time and update scene parameters from the timeline
	Impl->SetCurrentTime(fTime);
}

int AITwinTimelineActor::GetKeyFrameIndexFromTime(float fTime, bool bPrecise/* = false*/) const
{
	if (!Impl->IsReady() || !Impl->GetCurrentClip())
		return -1;
	return Impl->GetCurrentClip()->GetKeyFrameIndex(fTime, bPrecise);
}

float AITwinTimelineActor::GetKeyFrameTime(int iKF) const
{
	if (!Impl->IsReady() || !Impl->GetCurrentClip())
		return -1.f;
	return Impl->GetCurrentClip()->GetKeyFrameTime(iKF);
}

void AITwinTimelineActor::MoveKeyFrame(int clipIdx, float fOldTime, float fNewTime, bool bMoveOneKFOnly)
{
	if (fabsf(fOldTime-fNewTime) < 0.1)
		return;
	Impl->MoveKeyFrame(fOldTime, fNewTime, clipIdx, bMoveOneKFOnly);
}

void AITwinTimelineActor::CopyKeyFrame(int clipIdx, int iKF)
{
	Impl->CopyKeyFrame(clipIdx, iKF);
}

void AITwinTimelineActor::PasteKeyFrame(int clipIdx, int iKF)
{
	Impl->PasteKeyFrame(clipIdx, iKF);
}

void AITwinTimelineActor::EnableSynchroAnim(int clipIdx, bool bEnable)
{
	auto clip = Impl->GetClip(clipIdx);
	if (clip)
		clip->bSynchroAnim = bEnable;
}

void AITwinTimelineActor::EnableAtmoAnim(int clipIdx, bool bEnable)
{
	auto clip = Impl->GetClip(clipIdx);
	if (clip)
		clip->bAtmoAnim = bEnable;
}

bool AITwinTimelineActor::IsSynchroAnimEnabled(int clipIdx)
{
	auto clip = Impl->GetClip(clipIdx);
	return clip ? clip->bSynchroAnim : false;
}

bool AITwinTimelineActor::IsAtmoAnimEnabled(int clipIdx)
{
	auto clip = Impl->GetClip(clipIdx);
	return clip ? clip->bAtmoAnim : false;
}

float AITwinTimelineActor::GetClipDuration(int clipIdx)
{
	if (!Impl->IsReady())
		return 0.f;
	return Impl->GetClipDuration(clipIdx);
}

float AITwinTimelineActor::GetTotalDuration()
{
	if (!Impl->IsReady())
		return 0.f;
	return Impl->GetTotalDuration();
}

void AITwinTimelineActor::SetKFDuration(int KF, float fDuration)
{
	if (!Impl->IsReady())
		return;

	auto clip = Impl->GetCurrentClip();
	if (!clip || clip->GetKeyframeCount() < 2)
		return;

	float curKFTime = clip->GetKeyFrameTime(KF);
	float nextKFTime = clip->GetKeyFrameTime(KF+1);
	MoveKeyFrame(Impl->curClip_, nextKFTime, curKFTime + fDuration, false);
}

void AITwinTimelineActor::SetClipDuration(int clipIdx, float fDuration)
{
	if (!Impl->IsReady())
		return;
	auto clip = Impl->GetClip(clipIdx);
	if (!clip || clip->GetKeyframeCount() < 2)
		return;
	
	float fPerFrameDuration = fDuration / (clip->GetKeyframeCount()-1);
	SetPerFrameDuration(clipIdx, fPerFrameDuration);
}

void AITwinTimelineActor::SetPerFrameDuration(int clipIdx, float fPerFrameDuration)
{
	if (!Impl->IsReady())
		return;
	auto clip = Impl->GetClip(clipIdx);
	if (!clip || clip->GetKeyframeCount() == 0)
		return;

	TArray<float> vTimes;
	int nKFs = clip->GetKeyframeCount();
	for (int i(nKFs-1); i >= 1; i--)
	{
		clip->GetKeyFrameTimes(vTimes);
		MoveKeyFrame(clipIdx, vTimes[i], i * fPerFrameDuration, true);
	}
}

std::pair<int, float> AITwinTimelineActor::GetClipIdxAndTimeWithinSequence(float fSeqTime)
{
	std::pair<int, float> InvalidResult = { -1, 0.f };
	if (!Impl->IsReady() || fSeqTime < 0)
		return InvalidResult;
	TArray<float> vStartTimes;
	Impl->GetClipsStartTimes(vStartTimes, true);
	for (size_t i(1); i < vStartTimes.Num(); i++)
	{
		if (fSeqTime < vStartTimes[i])
			return { i-1, fSeqTime - vStartTimes[i-1] };
	}
	return InvalidResult;
}

void AITwinTimelineActor::OnCameraCutHandler(UCameraComponent* CameraComponent)
{
	// Detect camera switch during multi-clip playback
	// (Used for PIE debugging purposes only as doesn't work as expected in non-PIE mode)
	APlayerController* pController = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (CameraComponent)
	{
		AActor* OwningActor = CameraComponent->GetOwner();
		if (ACineCameraActor* CineCamera = Cast<ACineCameraActor>(OwningActor))
		{
			if (Impl->CurrentCutTrackCamera_ != CineCamera)
			{
				BE_LOGW("Timeline", "Received camera cut event for new camera " << TCHAR_TO_UTF8(*CineCamera->GetName()));
				pController->SetViewTarget(CineCamera);
				Impl->CurrentCutTrackCamera_ = CineCamera;
			}
		}
		else
		{
			BE_LOGW("Timeline", "Received camera cut event for actor " << TCHAR_TO_UTF8(*OwningActor->GetName()));
			pController->SetViewTarget(OwningActor);
			Impl->CurrentCutTrackCamera_ = nullptr;
		}
	}
}

bool AITwinTimelineActor::LinkClipsToCutTrack(int clipIdx/* = -1*/)
{
	if (!Impl->IsReady())
		return false;

	Impl->CurrentCutTrackStartTimes_.Empty();

	bool bRes;
	FString outMsg;
	USequencerHelper::AddCameraCutTrackToLevelSequence(Impl->levelSequencePath_, true, bRes, outMsg);

	float fTotalDuration(0.f);
	for (size_t i(0); i < Impl->GetClipsNum(); i++)
	{
		Impl->CurrentCutTrackStartTimes_.Add(fTotalDuration);

		// Skip all other clips if a valid clip index was provided
		if (clipIdx >= 0 && i != clipIdx)
			continue;

		auto clip = Impl->GetClip(i);

		// Skip disabled clips
		if (clipIdx < 0 && !clip->IsEnabled())
			continue;

		// shift clip key-frames so that it starts right after the previous clip
		float fStartTime = USequencerHelper::GetStartTime(clip->GetCamera(), Impl->levelSequencePath_);
		float fDeltaTime = fTotalDuration - fStartTime;
		if (fDeltaTime != 0.f)
			USequencerHelper::ShiftClipKFs(clip->GetTracks(), Impl->levelSequencePath_, fDeltaTime);

		// add clip to the camera cuts track
		float fTotalDurationNew(USequencerHelper::GetEndTime(clip->GetCamera(), Impl->levelSequencePath_));
		USequencerHelper::LinkCameraToCameraCutTrack(clip->GetCamera(), Impl->levelSequencePath_, fTotalDuration, fTotalDurationNew, bRes, outMsg);
		if (!bRes)
		{
			BE_LOGW("Timeline", "Failed to link clip " << i << " to Camera Cuts: " << TCHAR_TO_UTF8(*outMsg));
			continue;
		}
		fTotalDuration = fTotalDurationNew;
	}
	Impl->CurrentCutTrackStartTimes_.Add(fTotalDuration);

	if (clipIdx >= 0)
		Impl->SetCurrentClip(clipIdx);

	return true;
}

bool AITwinTimelineActor::UnlinkClipsFromCutTrack()
{
	if (!Impl->IsReady())
		return false;

	bool bRes;
	FString outMsg;

	USequencerHelper::RemoveCameraCutTrackFromLevelSequence(Impl->levelSequencePath_, bRes, outMsg);

	for (size_t i(0); i < Impl->GetClipsNum(); i++)
	{
		auto clip = Impl->GetClip(i);
		float fStartTime = USequencerHelper::GetStartTime(clip->GetCamera(), Impl->levelSequencePath_);
		if (fStartTime != 0.f)
			USequencerHelper::ShiftClipKFs(clip->GetTracks(), Impl->levelSequencePath_, -fStartTime);
	}

	Impl->CurrentCutTrackStartTimes_.Empty();
	return true;
}

//bool AITwinTimelineActor::IsInMovieMode()
//{
//	return USequencerHelper::HasCameraCutTrack(Impl->levelSequencePath_);
//}

void AITwinTimelineActor::OnPlaybackStarted()
{
	Impl->pPlayer_->OnCameraCut.AddUniqueDynamic(this, &AITwinTimelineActor::OnCameraCutHandler);
	SetActorTickEnabled(true);
}

void AITwinTimelineActor::SetSynchroIModels(
	std::function<TMap<FString, UITwinSynchro4DSchedules*> const&()> InGetSchedules)
{
	Impl->GetSchedules = InGetSchedules;
}

std::shared_ptr<AdvViz::SDK::ITimeline> AITwinTimelineActor::GetTimelineSDK()
{
	return Impl->timeline_;
}

void AITwinTimelineActor::SetTimelineSDK(const std::shared_ptr<AdvViz::SDK::ITimeline> &p)
{
	Impl->timeline_ = p;
}

void AITwinTimelineActor::OnLoad()
{
	Impl->OnLoad();
	OnTimelineLoaded.Broadcast();
}

void AITwinTimelineActor::ReinitPlayer()
{
	Impl->CreatePlayer();
}

void ScreenUtils::SetCurrentView(UWorld* pWorld, const FVector& pos, const FRotator& rot)
{
	if (APlayerController* pController = pWorld->GetFirstPlayerController())
	{
		pController->GetPawnOrSpectator()->SetActorLocation(pos, false, nullptr, ETeleportType::TeleportPhysics);
		pController->SetControlRotation(rot);
		pController->GetPawnOrSpectator()->SetActorRotation(rot);
		pController->SetViewTargetWithBlend(pController->GetPawnOrSpectator());
	}
}
void ScreenUtils::SetCurrentView(UWorld* pWorld, const FTransform& ft)
{
	SetCurrentView(pWorld, FVector(ft.GetTranslation()), FRotator(ft.GetRotation()));
}

void ScreenUtils::GetCurrentView(UWorld* pWorld, FVector& pos, FRotator& rot)
{
	if (APlayerController* pController = pWorld->GetFirstPlayerController())
	{
		if (APawn const* Pawn = pController->GetPawn())
		{
			pos = Pawn->GetActorLocation();
			rot = Pawn->GetActorRotation();
		}
	}
}

FTransform ScreenUtils::GetCurrentViewTransform(UWorld* pWorld)
{
	FVector pos;
	FRotator rot;
	ScreenUtils::GetCurrentView(pWorld, pos, rot);
	BE_LOGV("Timeline", "Current view transform: "
		<< "Rotation (" << rot.Yaw << ", " << rot.Pitch << ", " << rot.Roll
		<< "), Position (" << pos.X << ", " << pos.Y << ", " << pos.Z << ")");
	return FTransform(rot, pos, FVector(1, 1, 1));
}

