/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTimelineActor.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <AnimTimeline/ITwinTimelineActor.h>
#include <AnimTimeline/ITwinLookAtCameraModifier.h>
#include <AnimTimeline/ITwinLookAtTargetActor.h>
#include <AnimTimeline/ITwinSequencerHelper.h>
#include <AnimTimeline/ITwinTimelineClipMapping.h>
#include <AnimTimeline/ReadWriteHelper.h>
#include <ITwinIModel.h>
#include <ITwinSynchro4DSchedules.h>
#include <ITwinUtilityLibrary.h>
#include <Math/UEMathConversion.h>
#include <Population/ITwinPopulation.h>

#if WITH_EDITOR
#include <Helpers/WorldSingleton.h>
#endif

#include <Camera/CameraActor.h>
#include <Camera/CameraComponent.h>
#include <CineCameraActor.h>
#include <Engine/World.h>
#include <EngineUtils.h>
#include <GameFramework/Pawn.h>
#include <HAL/FileManager.h>
#include <Kismet/GameplayStatics.h>
#include <LevelSequence.h>
#include <LevelSequencePlayer.h>
#include <LevelSequenceActor.h>
#include <Misc/Paths.h>
#include <MovieSceneSequencePlayer.h>
#include <ProfilingDebugging/CpuProfilerTrace.h>
#include <Sections/MovieScene3DTransformSection.h>

#include <optional>
#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include "SDK/Core/Tools/Log.h"
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

//#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
//#	include "SDK/Core/Visualization/Timeline.h"
//#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

namespace ITwin
{
	float fDefaultTimeDelta = 5.f; // default delta time when appending key-frames

	ITWINRUNTIME_API void SetSynchroDateToSchedules(TMap<FString, UITwinSynchro4DSchedules*> const& SchedMap,
													const FDateTime& InDate);

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
		if (!pWorld)
			return nullptr;

		// Defaults matter: GetCurrentView leaves its outputs untouched when there is no
		// player controller (headless/automation runs), and FVector/FRotator do not
		// zero-initialize themselves.
		FVector pos(FVector::ZeroVector);
		FRotator rot(FRotator::ZeroRotator);
		ScreenUtils::GetCurrentView(pWorld, pos, rot);

		FActorSpawnParameters SpawnInfo;
		auto NewCamera = pWorld->SpawnActor<ACineCameraActor>(ACineCameraActor::StaticClass(), pos, rot, SpawnInfo);
		if (!NewCamera)
			return nullptr;

		if (UCameraComponent* CameraComponent = NewCamera->GetCameraComponent())
			if (APlayerController* pController = pWorld->GetFirstPlayerController())
				if (APlayerCameraManager* CamManager = pController->PlayerCameraManager)
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
		// Round to nearest second to avoid precision issues
		const int64 TicksPerSecond = ETimespan::TicksPerSecond; // 10000000

		int64 Ticks = (GetBaseDate() + FTimespan::FromSeconds(Delta * 100.0)).GetTicks();
		Ticks = ((Ticks + ETimespan::TicksPerSecond / 2) / ETimespan::TicksPerSecond) * ETimespan::TicksPerSecond;

		return FDateTime(Ticks);
	}
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
	//! Proxy actor following the tracked ISM instance, when this clip's camera tracks one.
	//! Weak: the actor lives in the world, we must not keep it alive past level teardown.
	TWeakObjectPtr<AITwinLookAtTargetActor> pTrackingProxy_;
	//! Camera modifier re-aiming the POV during playback, when this clip's camera tracks an
	//! instance. Owned by the player camera manager, hence weak: see ClearLookAtModifier().
	TWeakObjectPtr<UITwinLookAtCameraModifier> pLookAtModifier_;
	//! Proxy of the instance the camera rides on, when this clip follows one. Distinct from
	//! pTrackingProxy_: a camera may sit in a car (follow) while aiming at a building (look-at).
	TWeakObjectPtr<AITwinLookAtTargetActor> pFollowProxy_;

public:
	ClipData()
	{}

	AITwinLookAtTargetActor* GetTrackingProxy() const { return pTrackingProxy_.Get(); }
	void SetTrackingProxy(AITwinLookAtTargetActor* Proxy) { pTrackingProxy_ = Proxy; }

	UITwinLookAtCameraModifier* GetLookAtModifier() const { return pLookAtModifier_.Get(); }
	void SetLookAtModifier(UITwinLookAtCameraModifier* Modifier) { pLookAtModifier_ = Modifier; }

	//! The modifier lives on the player camera manager, not on the clip, so it must be removed
	//! explicitly: dropping our pointer would leave it re-aiming the view forever.
	//! Resolves the camera manager from the clip camera, so that callers (RemoveClip, EndPlay,
	//! ClearClipTracking) need no world/controller of their own.
	void ClearLookAtModifier()
	{
		UITwinLookAtCameraModifier* M = pLookAtModifier_.Get();
		pLookAtModifier_.Reset();
		if (!M)
			return;
		UWorld* World = pCamera_ ? pCamera_->GetWorld() : nullptr;
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (PC && PC->PlayerCameraManager)
			PC->PlayerCameraManager->RemoveCameraModifier(M);
	}

	//! Disables look-at tracking on the clip camera and destroys the proxy, if any.
	void ClearTracking()
	{
		if (UITwinLookAtCameraModifier* M = pLookAtModifier_.Get())
		{
			M->Proxy = nullptr;
			if (!M->FollowProxy.IsValid()) // follow may still need the modifier
				ClearLookAtModifier();
		}

		auto* CineCam = Cast<ACineCameraActor>(pCamera_);
		if (CineCam)
		{
			CineCam->LookatTrackingSettings.bEnableLookAtTracking = false;
			CineCam->LookatTrackingSettings.ActorToTrack = nullptr;
		}
		if (AITwinLookAtTargetActor* Proxy = pTrackingProxy_.Get())
		{
			if (pCamera_)
				pCamera_->RemoveTickPrerequisiteActor(Proxy);
			Proxy->Destroy();
		}
		pTrackingProxy_.Reset();
	}

	AITwinLookAtTargetActor* GetFollowProxy() const { return pFollowProxy_.Get(); }
	void SetFollowProxy(AITwinLookAtTargetActor* Proxy) { pFollowProxy_ = Proxy; }

	void ClearFollow()
	{
		if (UITwinLookAtCameraModifier* M = pLookAtModifier_.Get())
		{
			M->FollowProxy = nullptr;
			if (!M->Proxy.IsValid())
				ClearLookAtModifier();
		}
		if (AITwinLookAtTargetActor* Proxy = pFollowProxy_.Get())
		{
			if (pCamera_)
				pCamera_->RemoveTickPrerequisiteActor(Proxy);
			Proxy->Destroy();
		}
		pFollowProxy_.Reset();
	}

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

	int GetKeyFrameIndex(float fTime, bool bPrecise = false) const
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

	float GetKeyFrameTime(int iKF) const
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
		if (GetKeyframeCount() == 0)
			return 0.0f;
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
			// Moving a key-frame should not change its snapshot ID (if any).
			const std::string oldSnapshotId = (*ret)->GetSnapshotId();
			RemoveKeyframe(*ret);
			KF.time = fNewTime;
			auto newKF = AddKeyframe(KF);
			if (newKF && !oldSnapshotId.empty())
			{
				(*newKF)->SetSnapshotId(oldSnapshotId);
			}
		}
	}

	// Moves the key-frame at slot iKFSrc to slot iKFDst. The clip's time grid (and hence its
	// duration and per-frame transition times) is left untouched: only the key-frame contents
	// (camera, atmo, synchro, snapshot ID) are shifted from one slot to the next.
	bool ReorderKeyFrame(int iKFSrc, int iKFDst)
	{
		if (iKFSrc == iKFDst || !HasKeyFrame(iKFSrc) || !HasKeyFrame(iKFDst))
			return false;

		int const nKFs = (int)GetKeyframeCount();
		std::vector<AdvViz::SDK::ITimelineKeyframe::KeyframeData> vData(nKFs);
		std::vector<std::string> vSnapshotIds(nKFs);
		std::vector<float> vTimes(nKFs); // slot times, stay where they are

		for (int i = 0; i < nKFs; ++i)
		{
			auto kf = GetKeyframeByIndex(i);
			if (!kf)
				return false; // inconsistent clip, better do nothing than corrupt it
			vData[i] = (*kf)->GetData();
			vSnapshotIds[i] = (*kf)->GetSnapshotId();
			vTimes[i] = vData[i].time;
		}

		// Rotate the payloads of the [min, max] slot range, so that src ends up at dst
		// and the key-frames in between are shifted by one slot.
		if (iKFSrc < iKFDst)
		{
			std::rotate(vData.begin() + iKFSrc, vData.begin() + iKFSrc + 1, vData.begin() + iKFDst + 1);
			std::rotate(vSnapshotIds.begin() + iKFSrc, vSnapshotIds.begin() + iKFSrc + 1,
						vSnapshotIds.begin() + iKFDst + 1);
		}
		else
		{
			std::rotate(vData.begin() + iKFDst, vData.begin() + iKFSrc, vData.begin() + iKFSrc + 1);
			std::rotate(vSnapshotIds.begin() + iKFDst, vSnapshotIds.begin() + iKFSrc,
						vSnapshotIds.begin() + iKFSrc + 1);
		}

		// Write back, re-assigning each slot's own time to the payload it now holds.
		int const iFirst = std::min(iKFSrc, iKFDst);
		int const iLast = std::max(iKFSrc, iKFDst);
		for (int i = iFirst; i <= iLast; ++i)
		{
			auto kf = GetKeyframeByIndex(i);
			if (!kf)
				continue;
			vData[i].time = vTimes[i];
			(*kf)->Update(vData[i]);
			(*kf)->SetSnapshotId(vSnapshotIds[i]);
		}
		return true;
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

namespace
{
	/// Index of each parameter animated by the timeline, inside the (constant) track info table
	/// returned by GetAnimTracksInfo(), and hence inside each clip's track array (ClipData::GetTracks()).
	enum class EAnimTrack : uint8
	{
		Transform,
		Date,
		DateSun,
		Clouds,
		Fog,
		HeliodonLong,
		HeliodonLat,
		SunAzimuth,
		SunPitch,
		HDRIZRotation,
		SunIntensity,
		Exposure,
		UseHeliodon,
		HDRIImage,
		Count
	};

	constexpr int32 AnimTrackCount = static_cast<int32>(EAnimTrack::Count);

	constexpr int32 TrackIdx(EAnimTrack Track) { return static_cast<int32>(Track); }

	/// Description of all the parameters animated by the timeline (track names and types).
	/// This table is constant: its order defines EAnimTrack, and each clip's track array
	/// (ClipData::GetTracks()) is filled in the very same order by UITwinSequencerHelper::AddNewClip().
	/// WARNING: track names are persisted in the level sequence - do not "fix" them (see HRDIImage).
	TArray<UITwinSequencerHelper::FTrackInfo> const& GetAnimTracksInfo()
	{
		static TArray<UITwinSequencerHelper::FTrackInfo> const TracksInfo = []
			{
				using FTrackInfo = UITwinSequencerHelper::FTrackInfo;
				TArray<FTrackInfo> Info;
				Info.SetNum(AnimTrackCount);

				Info[TrackIdx(EAnimTrack::Transform)]     = FTrackInfo(TEXT("transform"),     UITwinSequencerHelper::TT_Transform);
				Info[TrackIdx(EAnimTrack::Date)]          = FTrackInfo(TEXT("date"),          UITwinSequencerHelper::TT_Double);
				Info[TrackIdx(EAnimTrack::DateSun)]       = FTrackInfo(TEXT("date_sun"),      UITwinSequencerHelper::TT_Double);
				Info[TrackIdx(EAnimTrack::Clouds)]        = FTrackInfo(TEXT("clouds"),        UITwinSequencerHelper::TT_Float);
				Info[TrackIdx(EAnimTrack::Fog)]           = FTrackInfo(TEXT("fog"),           UITwinSequencerHelper::TT_Float);
				Info[TrackIdx(EAnimTrack::HeliodonLong)]  = FTrackInfo(TEXT("HeliodonLong"),  UITwinSequencerHelper::TT_Float);
				Info[TrackIdx(EAnimTrack::HeliodonLat)]   = FTrackInfo(TEXT("HeliodonLat"),   UITwinSequencerHelper::TT_Float);
				Info[TrackIdx(EAnimTrack::SunAzimuth)]    = FTrackInfo(TEXT("sunAzimuth"),    UITwinSequencerHelper::TT_Float);
				Info[TrackIdx(EAnimTrack::SunPitch)]      = FTrackInfo(TEXT("sunPitch"),      UITwinSequencerHelper::TT_Float);
				Info[TrackIdx(EAnimTrack::HDRIZRotation)] = FTrackInfo(TEXT("HDRIZRotation"), UITwinSequencerHelper::TT_Float);
				Info[TrackIdx(EAnimTrack::SunIntensity)]  = FTrackInfo(TEXT("sunIntensity"),  UITwinSequencerHelper::TT_Float);
				Info[TrackIdx(EAnimTrack::Exposure)]      = FTrackInfo(TEXT("Exposure"),      UITwinSequencerHelper::TT_Float);
				Info[TrackIdx(EAnimTrack::UseHeliodon)]   = FTrackInfo(TEXT("UseHeliodon"),   UITwinSequencerHelper::TT_Bool);
				// Historical typo ("HRDI" instead of "HDRI"): kept as is, the name is stored in existing sequences.
				Info[TrackIdx(EAnimTrack::HDRIImage)]     = FTrackInfo(TEXT("HRDIImage"),     UITwinSequencerHelper::TT_String);

				// Every slot must have been assigned: FTrackInfo's default ctor leaves eType uninitialized.
				for (auto const& Track : Info)
				{
					checkf(!Track.sName.IsNone(), TEXT("GetAnimTracksInfo: an EAnimTrack entry is missing"));
				}
				return Info;
			}();
		return TracksInfo;
	}
}

class AITwinTimelineActor::FImpl
{
public:
	AITwinTimelineActor& Owner;
	FString levelSequencePath_;
	TStrongObjectPtr<ULevelSequencePlayer> pPlayer_;
	TStrongObjectPtr<ALevelSequenceActor> pPlayerActor_;
	TStrongObjectPtr<AActor> pSynchroActor_;
	std::function<bool(FDateTime& Out, FString& ScheduleIDOut)> GetSynchro4DScheduleTime;
	std::function<void(FDateTime const& Date)> SetSynchro4DScheduleTime;
	bool bHasBoundOnCameraCut = false;
	bool bHasAnyPlaybackOwnershipTargets_ = false;

	ACineCameraActor* CurrentCutTrackCamera_ = nullptr; 
	TArray<float> CurrentCutTrackStartTimes_;

	/// The time at which playback should stop. This is used to determine whether to stop playback.
	/// Only valid between LinkClipsToCutTrack() and UnlinkClipsFromCutTrack().
	/// Initialized to -1.f, which is an invalid time.
	float PlaybackEndTime_ = -1.f;

	/// map of all the parameters animated by the timeline to their type
	TArray<UITwinSequencerHelper::FTrackInfo> const& AnimTracksInfo_ = GetAnimTracksInfo();

	typedef AdvViz::SDK::Timeline BaseClass;

	std::shared_ptr<AdvViz::SDK::ITimeline> timeline_; // timeline data stored on the server

	int nextFreeClipID_ = 0;
	int curClip_ = -1; ///< index of the current clip in the clip array
	float curTime_ = 0.f; ///< current clip time in seconds (don't confuse with schedule time/date)
	
	std::shared_ptr<AdvViz::SDK::ITimelineKeyframe> copiedKF_; // used for copy-paste
	//SynchroData synchroData_; // for now, we simply assign current UI date to each new key-frame

	/// if true, camera key-frames are stored in GCS coordinates instead of local coordinates.
	/// This is now the default, but can be disabled when loading an old scene.
	bool bUseGCSForCameraKF_ = true;


	FImpl(AITwinTimelineActor& InOwner) : Owner(InOwner)
	{
		// Find predefined timeline-related level sequence (its creation is only possible in editor mode)
		levelSequencePath_ = FString("/ITwinForUnreal/ITwin/AnimTimeline/ITwinLevelSequence");

		Owner.SequencerHelper = NewObject<UITwinSequencerHelper>(&Owner);
		if (!Owner.SequencerHelper->Initialize(levelSequencePath_))
		{
			BE_LOGE("Timeline", "Failed to initialize sequencer helper");
		}
		CreatePlayer();

		timeline_.reset(AdvViz::SDK::Timeline::New());

		// Set reasonable default implementations for the synchro4D schedule time retrieval and setting functions
		// (replaced in Engage by custom implementations based on Engage's 4D controller)
		SetSynchro4DScheduleTime = [pOwner=&Owner](FDateTime const& Date)
			{
				if (IsValid(pOwner) && pOwner->GetWorld())
				{
					TMap<FString, UITwinSynchro4DSchedules*> SchedMap;
					for (TActorIterator<AITwinIModel> It(pOwner->GetWorld()); It; ++It)
					{
						auto* Sched = It->GetSynchro4DSchedules();
						if (IsValid(Sched) && Sched->GetDateRange() != FDateRange())
							SchedMap.Add(Sched->ScheduleId, Sched);
					}
					ITwin::SetSynchroDateToSchedules(SchedMap, Date);
				}
			};
		GetSynchro4DScheduleTime = [pOwner = &Owner](FDateTime& Out, FString& ScheduleIDOut)
			{
				if (IsValid(pOwner) && pOwner->GetWorld())
				{
					Out = FDateTime::MinValue();
					ScheduleIDOut = FString();
					for (TActorIterator<AITwinIModel> It(pOwner->GetWorld()); It; ++It)
					{
						auto* Sched = It->GetSynchro4DSchedules();
						if (!IsValid(Sched))
							continue;
						auto Timerange = Sched->GetDateRange();
						if (Timerange != FDateRange() && Sched->ScheduleTime >= Timerange.GetLowerBoundValue()
							&& Sched->ScheduleTime <= Timerange.GetUpperBoundValue())
						{
							Out = Sched->ScheduleTime;
							ScheduleIDOut = Sched->ScheduleId;
							return true;
						}
						else if (Out == FDateTime::MinValue() && Timerange != FDateRange())
						{
							Out = Timerange.GetUpperBoundValue();
							ScheduleIDOut = Sched->ScheduleId;
						}
					}
					if (Out != FDateTime::MinValue())
						return true;
				}
				return false;
			};
	}

	~FImpl()
	{
	}

	inline bool IsReady() const
	{
		return Owner.SequencerHelper && Owner.SequencerHelper->IsInitialized();
	}

	void CreatePlayer()
	{
		if (IsReady())
		{
			ALevelSequenceActor* pPlayerActor(nullptr);
			// Create player for the sequence
			FMovieSceneSequencePlaybackSettings s;
			s.bPauseAtEnd = false;
			s.bDisableCameraCuts = false; // Ensure camera cuts are processed
			pPlayer_.Reset(ULevelSequencePlayer::CreateLevelSequencePlayer(Owner.GetWorld(), Owner.GetLevelSequence(), s, pPlayerActor));
			pPlayerActor_.Reset(pPlayerActor);
		}

		if (pPlayer_.IsValid() && IsReady())
		{
			auto* MovieScene = Owner.GetLevelSequence()->GetMovieScene();
			if (MovieScene)
			{
				MovieScene->SetDisplayRate(FFrameRate(30, 1)); // Match desired playback frame rate
				MovieScene->SetEvaluationType(EMovieSceneEvaluationType::WithSubFrames);
			}
			pPlayer_->SetFrameRate(FFrameRate(30, 1)); // Set to match 30 FPS
			//pPlayer_->ForceUpdate();
			pPlayer_->OnPlay.AddDynamic(&Owner, &AITwinTimelineActor::OnPlaybackStarted);
		}
	}

	void ConvertToSequencer(const AdvViz::SDK::ITimelineKeyframe::KeyframeData & KF, TArray<std::optional<UITwinSequencerHelper::KFValueType> >& Out)
	{
		Out.Empty();
		for (int32 i = 0; i < AnimTrackCount; ++i)
		{
			Out.Add(std::nullopt);
			switch (static_cast<EAnimTrack>(i))
			{
			case EAnimTrack::Transform:
				if (KF.camera.has_value())
				{
					FTransform const transf = FITwinMathConversion::SDKtoUE(KF.camera->transform, UseGCSForCameraKF());
					Out.Last() = std::make_optional(transf);
				}
				break;
			case EAnimTrack::Date:
				if (KF.synchro.has_value())
				{
					auto synchro = ITwin::SynchroToTimeline(StrToDateTime(KF.synchro->date));
					Out.Last() = std::make_optional(synchro);
				}
				break;
			case EAnimTrack::DateSun:
				if (KF.atmo.has_value())
				{
					auto date = ITwin::SynchroToTimeline(StrToDateTime(KF.atmo->time));
					Out.Last() = std::make_optional(date);
				}
				break;
			case EAnimTrack::Clouds:
				if (KF.atmo.has_value())
				{
					Out.Last() = std::make_optional(KF.atmo->cloudCoverage);
				}
				break;
			case EAnimTrack::Fog:
				if (KF.atmo.has_value())
				{
					Out.Last() = std::make_optional(KF.atmo->fog);
				}
				break;
			case EAnimTrack::SunAzimuth:
				if (KF.atmo.has_value())
				{
					Out.Last() = std::make_optional(KF.atmo->sunAzimuth.get());
				}
				break;
			case EAnimTrack::SunPitch:
				if (KF.atmo.has_value())
				{
					Out.Last() = std::make_optional(KF.atmo->sunPitch.get());
				}
				break;
			case EAnimTrack::HeliodonLong:
				if (KF.atmo.has_value())
				{
					Out.Last() = std::make_optional(KF.atmo->heliodonLongitude.get());
				}
				break;
			case EAnimTrack::HeliodonLat:
				if (KF.atmo.has_value())
				{
					Out.Last() = std::make_optional(KF.atmo->heliodonLatitude.get());
				}
				break;
			case EAnimTrack::HDRIZRotation:
				if (KF.atmo.has_value())
				{
					Out.Last() = std::make_optional(KF.atmo->HDRIZRotation.get());
				}
				break;
			case EAnimTrack::SunIntensity:
				if (KF.atmo.has_value())
				{
					Out.Last() = std::make_optional(KF.atmo->sunIntensity.get());
				}
				break;
			case EAnimTrack::UseHeliodon:
				if (KF.atmo.has_value())
				{
					Out.Last() = std::make_optional(KF.atmo->useHeliodon.get());
				}
				break;
			case EAnimTrack::HDRIImage:
				if (KF.atmo.has_value())
				{
					Out.Last() = std::make_optional(KF.atmo->HDRIImage.get());
				}
				break;
			case EAnimTrack::Exposure:
				if (KF.atmo.has_value())
				{
					Out.Last() = std::make_optional(KF.atmo->exposure.get());
				}
				break;

			default:
				BE_LOGE("Timeline", "ConvertToSequencer: unhandled track index " << i);
				break;
			}
		}
	}
	
	bool UseGCSForCameraKF() const
	{
		return bUseGCSForCameraKF_;
	}

	void OnLoad(bool bUseGCS)
	{
		bUseGCSForCameraKF_ = bUseGCS;
		if (!IsReady())
		{
			BE_ISSUE("Cannot load timeline data: sequencer helper is not ready");
			return;
		}
		auto& SeqHelper = *Owner.SequencerHelper;
		// Finalize camera clip
		for (size_t i = 0; i < timeline_->GetClipCount(); ++i)
		{
			auto clip = GetClip(i);
			clip->InitCamera(Owner.GetWorld());
			SeqHelper.AddNewClip(clip->GetCamera(), AnimTracksInfo_, clip->GetTracks());
			for (size_t j = 0; j < clip->GetKeyframeCount(); ++j)
			{
				auto kf = clip->GetKeyframeByIndex(j);
				if (!kf)
					continue;
				const auto& KF = (*kf)->GetData();
				BE_ASSERT(KF.camera.has_value());
				BE_ASSERT(KF.synchro.has_value());
				TArray<std::optional<UITwinSequencerHelper::KFValueType> > ParamValues;
				ConvertToSequencer(KF, ParamValues);
				SeqHelper.AddKeyFrame(clip->GetTracks(), KF.time, ParamValues);
			}
		}
		SetCurrentClip(-1); // do not modify current scene state (camera, synchro, atmosphere) after loading the timeline data
	}

	ClipData* GetCurrentClip()
	{
		return GetClip(curClip_);
	}

	bool SetCurrentClip(int clipIdx, bool updateSceneFromTimeline = true)
	{
		ON_SCOPE_EXIT
		{
			UpdateTrackingViewOwnership();
		};

		if (clipIdx >= 0 && clipIdx < (int)timeline_->GetClipCount() && clipIdx != curClip_)
		{
			curClip_ = clipIdx;
			if (updateSceneFromTimeline)
				SetCurrentTime(0.f);
			else
				curTime_ = 0.f;
			//SeqHelper.AdjustMoviePlaybackRange(GetCurrentCamera()); // TODO: to check
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

	float GetClipDuration(int clipIdx) const
	{
		float fDuration(0.f);
		if (clipIdx >= 0 && clipIdx < timeline_->GetClipCount())
		{
			auto clip = GetClipConst(clipIdx);
			if (clip)
				fDuration = clip->GetDuration();
			// There are two ways to obtain clip duration: from SDK timeline's key frame times or from Unreal sequencer.
			// The result should be the same except for the case when the clip times are manually shifted to fit into a single camera cut track - see AssembleClips()
			// That's why we use the first way here, although the line below can be still useful to debug sequencer-related issues:
			//return SeqHelper.GetEndTime(clip->GetCamera());
		}
		return fDuration;
	}

	float GetTotalDuration() const
	{
		float fTotalDuration(0.f);
		for (size_t i(0); i < timeline_->GetClipCount(); i++)
		{
			auto clip = GetClipConst(i);
			if (clip && clip->IsEnabled())
				fTotalDuration += clip->GetDuration();
		}
		return fTotalDuration;
	}

	// Get starting time of the clip within the sequence of clips
	float GetClipStartTime(int clipIdx) const
	{
		if (clipIdx >= timeline_->GetClipCount())
			return 0.f;

		float fStartTime(0.f);
		for (size_t i(1); i <= clipIdx; i++)
		{
			auto clip = GetClipConst(i-1);
			if (clip && clip->IsEnabled())
				fStartTime += clip->GetDuration();
		}
		return fStartTime;
	}

	void GetClipsStartTimes(TArray<float> &vTimes, bool bAppendLastDuration = false) const
	{
		vTimes.Empty();
		if (timeline_->GetClipCount() == 0)
			return;

		float fAccumTime(0.f);
		for (size_t i(0); i < timeline_->GetClipCount(); i++)
		{
			if (auto clip = GetClipConst(i))
			{
				vTimes.Add(fAccumTime);
				if (clip->IsEnabled())
					fAccumTime += clip->GetDuration();
			}
		}
		if (bAppendLastDuration)
			vTimes.Add(fAccumTime); // append theoretical start time of the next clip
	}


	void SetClipKeyFrameTimes(ClipData& clip, const std::vector<float>& vNewTimes)
	{
		// Update the model first: if the new times are invalid, leave the sequencer tracks
		// untouched so that the UI stays in sync with the timeline data.
		auto const res = clip.SetKeyFrameTimes(vNewTimes);
		if (!res)
		{
			BE_LOGE("Timeline", "Could not retime clip '" << clip.GetName()
				<< "': " << res.error());
			return;
		}
		BE_ASSERT(IsReady());
		auto& SeqHelper = *Owner.SequencerHelper;
		SeqHelper.SetClipKFsTimes(clip.GetTracks(), vNewTimes);

		UpdateSceneFromTimeline(); // see comment in AddOrUpdateKeyFrame
	}

	void GetEnabledClipIndices(TArray<int>& vIndices, bool bSkipEmpty = false) const
	{
		vIndices.Empty();
		if (timeline_->GetClipCount() == 0)
			return;

		for (size_t i(0); i < timeline_->GetClipCount(); i++)
		{
			if (auto const clip = GetClipConst(i))
			{
				if (clip->IsEnabled() && (!bSkipEmpty || clip->GetKeyframeCount() > 1))
					vIndices.Add(i);
			}
		}
	}

	bool HasClipsToExport() const
	{
		for (size_t i(0); i < timeline_->GetClipCount(); i++)
		{
			if (auto clip = GetClipConst(i))
			{
				if (clip->IsEnabled() && clip->GetKeyframeCount() > 1)
					return true;
			}
		}
		return false;
	}

	FString GetNextFreeClipName()
	{
		FString sName;
		do {
			sName = FString::Printf(TEXT("Clip_%d"), ++nextFreeClipID_);
		} while (GetClipIndex(sName) >= 0);
		return sName;
	}

	std::shared_ptr<AdvViz::SDK::ITimelineClip> AppendClip(FString sName = FString())
	{
		if (sName.IsEmpty())
			sName = GetNextFreeClipName();
		auto clip = timeline_->AddClip(TCHAR_TO_UTF8(*sName));
		BE_ASSERT(clip);
		auto clipdata = AdvViz::SDK::Tools::DynamicCast<ClipData>(clip.get());
		BE_ASSERT(clipdata);
		clipdata->InitCamera(Owner.GetWorld());

		BE_ASSERT(IsReady());
		auto& SeqHelper = *Owner.SequencerHelper;
		SeqHelper.AddNewClip(clipdata->GetCamera(), AnimTracksInfo_, clipdata->GetTracks());

		curClip_ = timeline_->GetClipCount() - 1;
		curTime_ = 0.f;
		UpdateTrackingViewOwnership();
		return clip;
	}

	void DuplicateClip(int clipIdx)
	{
		if (clipIdx >= 0 && clipIdx < (int)timeline_->GetClipCount())
		{
			auto clip = GetClip(clipIdx);
			FString sName = clip->GetNameU() + FString(TEXT("_copy"));
			int ind(0);
			FString sNewName = sName;
			while (GetClipIndex(sNewName) >= 0)
			{
				sNewName = FString::Printf(TEXT("%s_%d"), *sName, ++ind);
			}

			int newClipIdx = timeline_->GetClipCount();
			AppendClip(sNewName);
			auto newClip = GetClip(newClipIdx);
			BE_ASSERT(newClip);
			for (int i(0); i < clip->GetKeyframeCount(); ++i)
			{
				if (auto kf = clip->GetKeyframeByIndex(i))
				{
					AddOrUpdateKeyFrame((*kf)->GetData().time, newClipIdx, (*kf)->GetData());
				}
			}

			newClip->bSynchroAnim = clip->bSynchroAnim;
			newClip->bAtmoAnim = clip->bAtmoAnim;
		}
	}

	void RemoveClip(int clipIdx)
	{
		BE_ASSERT(IsReady());
		auto& SeqHelper = *Owner.SequencerHelper;

		if (clipIdx < 0)
			clipIdx = curClip_;
		if (clipIdx >= 0 && clipIdx < (int)timeline_->GetClipCount())
		{
			auto clip = GetClip(clipIdx);

			bool bRes(false);
			FString outMsg;
			SeqHelper.RemoveAllTracksFromLevelSequence(clip->GetCamera(), bRes, outMsg);
			SeqHelper.RemovePActorFromLevelSequence(clip->GetCamera(), bRes, outMsg);
			//clip->GetCamera()->Destroy();
			clip->ClearTracking(); // the proxy actor, unlike the camera, must not outlive the clip

			timeline_->RemoveClip(clipIdx);

			auto const count = (int)timeline_->GetClipCount();
			
			// adjust currently selected clip index if needed
			if (curClip_ == clipIdx)
				curClip_ = (count > 0) ? 0 : -1;
			else if (curClip_ > clipIdx)
				curClip_--;
			
			// "free" new clip name IDs if the deleted clip was the last one in the list
			if (count >= 0 && clipIdx == count)
			{
				--nextFreeClipID_;
			}
			//nextFreeClipID_ = std::min(nextFreeClipID_, clipIdx); // always reuse indices of the deleted clips

			UpdateTrackingViewOwnership();
		}
	}

	void MoveClip(size_t indexSrc, size_t indexDst)
	{
		timeline_->MoveClip(indexSrc, indexDst);
	}

	bool HasAnyPlaybackOwnershipTargets() const
	{
		return bHasAnyPlaybackOwnershipTargets_;
	}

	void RefreshPlaybackOwnershipPresence()
	{
		bHasAnyPlaybackOwnershipTargets_ = false;
		const int ClipCount = static_cast<int>(GetClipsNum());
		for (int i = 0; i < ClipCount; ++i)
		{
			ClipData* Clip = GetClip(i);
			if (!Clip)
				continue;
			if (Clip->GetLookAtModifier() || Clip->GetTrackingProxy() || Clip->GetFollowProxy())
			{
				bHasAnyPlaybackOwnershipTargets_ = true;
				return;
			}
		}
	}

	//! The clip's camera modifier is shared by look-at and follow: both must override the POV
	//! during playback, and both are defeated if applied anywhere else (see the class doc of
	//! UITwinLookAtCameraModifier). Created on demand; removed by ClearLookAtModifier() once
	//! neither feature uses it any more.
	UITwinLookAtCameraModifier* EnsureLookAtModifier(ClipData& clip)
	{
		if (UITwinLookAtCameraModifier* M = clip.GetLookAtModifier())
			return M;
		auto* CineCam = Cast<ACineCameraActor>(clip.GetCamera());
		UWorld* World = Owner.GetWorld();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (!CineCam || !PC || !PC->PlayerCameraManager)
			return nullptr;
		auto* M = Cast<UITwinLookAtCameraModifier>(
			PC->PlayerCameraManager->AddNewCameraModifier(UITwinLookAtCameraModifier::StaticClass()));
		if (M)
		{
			M->CineCam = CineCam;
			clip.SetLookAtModifier(M);
		}
		return M;
	}

	//! Makes the clip camera ride an instance, keeping its current offset from it.
	//! Independent from SetClipTrackingTarget: the camera may sit in a car (follow) while
	//! aiming at a building (look-at). Runtime-only, nothing is persisted.
	bool SetClipFollowTarget(int clipIdx, AITwinPopulation* Population, int32 InstanceIndex,
							 bool bFollowRotation,
							 TOptional<FTransform> const& ExplicitOffset = {})
	{
		ON_SCOPE_EXIT
		{
			UpdateTrackingViewOwnership();
		};
		ClipData* clip = GetClip(clipIdx);
		if (!clip || !Population || InstanceIndex == INDEX_NONE
			|| InstanceIndex >= Population->GetNumberOfInstances())
		{
			return false;
		}

		AITwinLookAtTargetActor* Proxy = clip->GetFollowProxy();
		if (Proxy)
		{
			Proxy->SetTarget(*Population, InstanceIndex); // retarget, keep the same proxy
		}
		else
		{
			// Deliberately no camera: this proxy only *positions* the view from the instance
			// pose, it must never aim it. CameraToDrive is what makes a proxy apply the cine
			// camera's LookatTrackingSettings in AITwinLookAtTargetActor::Tick, so passing the
			// clip camera here would make the follow proxy re-aim the view right after placing
			// it - stealing the job of the look-at proxy, and breaking the "passenger" case
			// (ride a car, look at a building) by overriding the aim with the follow rotation.
			// The two roles are intentionally split across two proxies; see
			// UpdateTrackingViewOwnership(), which also orders them (follow first, look-at last).
			// Playback is likewise not a reason to pass it: the level sequence rewrites the
			// camera transform every frame, and the proxy ticks in TG_PrePhysics, before that
			// evaluation - so a camera here would achieve nothing during playback anyway.
			Proxy = AITwinLookAtTargetActor::Spawn(*Population, InstanceIndex, /*CameraToDrive*/nullptr);
			if (!Proxy)
				return false;
			clip->SetFollowProxy(Proxy);
		}

		// Seed the offset from wherever the user currently stands, so that enabling follow never
		// moves the view: "keep me here, relative to this instance". Avoids having to decide on
		// presets (driver seat, chase distance...) before the UX is defined.
		auto const Space = bFollowRotation
			? AITwinLookAtTargetActor::EFollowSpace::FullRelative
			: AITwinLookAtTargetActor::EFollowSpace::LocationOnly;
		// Without an explicit placement, seed from wherever the user currently stands ("keep me
		// here, relative to this instance"), so that enabling follow never moves the view.
		Proxy->SetFollowOffset(
			ExplicitOffset.IsSet()
			? *ExplicitOffset
			: Proxy->MakeFollowOffsetFromWorld(
				ScreenUtils::GetCurrentViewTransform(Owner.GetWorld()), Space),
			Space);

		// Playback follow goes through the same modifier as the look-at.
		if (UITwinLookAtCameraModifier* M = EnsureLookAtModifier(*clip))
		{
			M->FollowProxy = Proxy;
		}
		RefreshPlaybackOwnershipPresence();

		BE_LOGI("Timeline", "Clip " << clipIdx << " follows instance " << InstanceIndex
			<< " of population " << TCHAR_TO_UTF8(*Population->GetAssetPath())
			<< (bFollowRotation ? " (full relative)" : " (location only)"));
		return true;
	}

	void ClearClipFollowTarget(int clipIdx, bool bUpdateViewOwnership = true)
	{
		if (ClipData* clip = GetClip(clipIdx))
		{
			clip->ClearFollow();
			RefreshPlaybackOwnershipPresence();
		}
		if (bUpdateViewOwnership)
		{
			UpdateTrackingViewOwnership();
		}
	}

	void ClearAllClipFollow()
	{
		const int ClipCount = static_cast<int>(GetClipsNum());
		for (int i(0); i < ClipCount; i++)
			ClearClipFollowTarget(i, false);
		UpdateTrackingViewOwnership();
	}

	bool SetClipTrackingTarget(int clipIdx, AITwinPopulation* Population, int32 InstanceIndex)
	{
		ON_SCOPE_EXIT
		{
			UpdateTrackingViewOwnership();
		};
		ClipData* clip = GetClip(clipIdx);
		if (!clip || !Population || InstanceIndex == INDEX_NONE
			|| InstanceIndex >= Population->GetNumberOfInstances())
			return false;

		auto* CineCam = Cast<ACineCameraActor>(clip->GetCamera());
		if (!CineCam)
		{
			BE_LOGW("Timeline", "Clip " << clipIdx << " has no cine camera, cannot track an instance");
			return false;
		}

		AITwinLookAtTargetActor* Proxy = clip->GetTrackingProxy();
		if (Proxy)
		{
			Proxy->SetTarget(*Population, InstanceIndex); // retarget, keep the same proxy
		}
		else
		{
			Proxy = AITwinLookAtTargetActor::Spawn(*Population, InstanceIndex, CineCam);
			if (!Proxy)
				return false;
			clip->SetTrackingProxy(Proxy);
		}

		auto& Tracking = CineCam->LookatTrackingSettings;
		Tracking.bEnableLookAtTracking = true;
		Tracking.bAllowRoll = false;
		// Note: ActorToTrack is a TSoftObjectPtr, and it does resolve correctly for this
		// runtime-spawned transient actor (verified). If look-at ever aims at the world origin,
		// suspect a missing/static RootComponent on the proxy rather than this pointer: the
		// engine silently falls back to RelativeOffset when the target cannot be resolved.
		Tracking.ActorToTrack = Proxy;

		// Playback needs a camera modifier: the sequence's transform track overwrites the
		// camera rotation every frame, and no tick group can win that race (see the class doc
		// of UITwinLookAtCameraModifier).
		if (UITwinLookAtCameraModifier* M = EnsureLookAtModifier(*clip))
		{
			M->Proxy = Proxy;
		}
		RefreshPlaybackOwnershipPresence();

		BE_LOGI("Timeline", "Clip " << clipIdx << " tracks instance " << InstanceIndex
			<< " of population " << TCHAR_TO_UTF8(*Population->GetAssetPath()));
		return true;
	}

	void ClearClipTracking(int clipIdx, bool bUpdateViewOwnership = true)
	{
		if (ClipData* clip = GetClip(clipIdx))
		{
			clip->ClearTracking();
			RefreshPlaybackOwnershipPresence();
		}
		if (bUpdateViewOwnership)
		{
			UpdateTrackingViewOwnership();
		}
	}

	void ClearAllClipTracking()
	{
		const int ClipCount = static_cast<int>(GetClipsNum());
		for (int i(0); i < ClipCount; i++)
			ClearClipTracking(i, false);
		UpdateTrackingViewOwnership();
	}

	//! Takes every clip out of playback mode, handing the view back to the manual path.
	void DeactivateAllLookAtModifiers()
	{
		const int ClipCount = static_cast<int>(GetClipsNum());
		for (int i(0); i < ClipCount; i++)
		{
			ClipData* Clip = GetClip(i);
			if (!Clip)
				continue;
			if (UITwinLookAtCameraModifier* M = Clip->GetLookAtModifier())
				M->bActiveForPlayback = false;
			if (AITwinLookAtTargetActor* P = Clip->GetTrackingProxy())
				P->SetPlaybackActive(false);
			if (AITwinLookAtTargetActor* F = Clip->GetFollowProxy())
				F->SetPlaybackActive(false);
		}
	}

	//! Centralized playback ownership handoff for follow/look-at.
	//! Exactly one clip is "active" at a time (the one whose camera matches ActiveCamera):
	//! - its camera modifier is enabled for playback,
	//! - its follow/look-at proxies are put in playback mode,
	//! - all other clips are forced out of playback mode.
	//!
	//! This is shared by both activation paths:
	//! 1) event-driven camera cuts (__OnCameraCutHandler__), and
	//! 2) deterministic per-frame playback state (__Tick__) for cases where cut events are unreliable
	//!    (notably outside PIE).
	void ApplyPlaybackCameraOwnership(AActor* ActiveCamera)
	{
		const int ClipCount = static_cast<int>(GetClipsNum());
		for (int i(0); i < ClipCount; i++)
		{
			ClipData* Clip = GetClip(i);
			if (!Clip)
				continue;

			const bool bActive = (Clip->GetCamera() == ActiveCamera);

			if (UITwinLookAtCameraModifier* M = Clip->GetLookAtModifier())
				M->bActiveForPlayback = bActive;

			if (AITwinLookAtTargetActor* P = Clip->GetTrackingProxy())
				P->SetPlaybackActive(bActive);

			if (AITwinLookAtTargetActor* F = Clip->GetFollowProxy())
				F->SetPlaybackActive(bActive);
		}
	}

	//! Only the current clip's proxies may re-aim the player view: otherwise every tracking
	//! clip fights over the view rotation each frame. Must be called whenever curClip_ changes
	//! or the set of proxies changes.
	void UpdateTrackingViewOwnership()
	{
		const int ClipCount = static_cast<int>(GetClipsNum());
		for (int i(0); i < ClipCount; i++)
		{
			ClipData* clip = GetClip(i);
			if (!clip)
				continue;
			const bool bIsCurrent = (i == curClip_);
			AITwinLookAtTargetActor* Follow = clip->GetFollowProxy();
			AITwinLookAtTargetActor* LookAt = clip->GetTrackingProxy();
			if (Follow)
				Follow->SetDrivesPlayerView(bIsCurrent);
			if (LookAt)
			{
				LookAt->SetDrivesPlayerView(bIsCurrent);
				// Follow places the view, look-at then re-aims it: the order matters, and both
				// proxies are in the same tick group.
				if (Follow)
					LookAt->AddTickPrerequisiteActor(Follow);
			}
		}
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

	ClipData const* GetClipConst(int clipIdx) const
	{
		if (clipIdx >= 0 && clipIdx < timeline_->GetClipCount())
		{
			auto clip = timeline_->GetClipByIndex(clipIdx);
			BE_ASSERT(clip);
			if (!clip)
				return nullptr;

			auto clipdata = AdvViz::SDK::Tools::DynamicCast<ClipData const>(*clip);
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

	void DuplicateKeyFrame(int clipIdx, int iKF)
	{
		const int iClip = (clipIdx >= 0) ? clipIdx : curClip_;
		auto clip = GetClip(iClip);
		if (!clip)
			return;
		auto ret = clip->GetKeyframeByIndex(iKF);
		if (!ret)
			return;

		const int nKFs = (int)clip->GetKeyframeCount();
		const float fTime = (*ret)->GetData().time;

		// The duplicate takes over the duration (transition time) of the original key-frame: it is
		// inserted exactly one transition after it, and every later key-frame is shifted by that same
		// duration, so that no existing transition is modified - the clip simply gets longer by
		// fDuration. For the last key-frame, which has no transition of its own, reuse the incoming
		// one; fall back on the default delta for a single-key-frame clip.
		float fDuration = ITwin::fDefaultTimeDelta;
		if (iKF + 1 < nKFs)
			fDuration = clip->GetKeyFrameTime(iKF + 1) - fTime;
		else if (iKF > 0)
			fDuration = fTime - clip->GetKeyFrameTime(iKF - 1);
		if (fDuration <= 0.f)
			fDuration = ITwin::fDefaultTimeDelta;

		// Free the slot: push the next key-frame (and, with bMoveOneKFOnly = false, all those after
		// it) by fDuration. Must be done before adding the copy, otherwise the new key-frame would be
		// shifted as well.
		if (iKF + 1 < nKFs)
			MoveKeyFrame(clip->GetKeyFrameTime(iKF + 1), fTime + 2.f * fDuration, iClip, false);

		AddOrUpdateKeyFrame(fTime + fDuration, iClip, (*ret)->GetData());
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
		//FTransform const transf = FITwinMathConversion::SDKtoUE(KF.camera->transform, UseGCSForCameraKF());
		//auto synchro = ITwin::SynchroToTimeline(StrToDateTime(KF.synchro->date));
		if (ensure(IsReady()))
		{
			TArray<std::optional<UITwinSequencerHelper::KFValueType> > ParamValues;
			ConvertToSequencer(KF, ParamValues);
			auto& SeqHelper = *Owner.SequencerHelper;
			fTime = SeqHelper.AddKeyFrame(clip->GetTracks(), fTime, ParamValues);
		}
		auto ret = clip->AddOrUpdateKeyFrame(fTime, KF);
		// We may have changed the timeline in a way that affects the scene at the current time, no?
		// For example, when pasting over the currently selected keyframe, the scene was not updated, not even
		// when capturing the new snapshot after the paste (azdev#1608359, second fix)
		UpdateSceneFromTimeline();
		return ret;
	}

	bool GetSynchroDateFromAvailableSchedules(FDateTime& Out, FString& ScheduleIDOut)
	{
		if (!GetSynchro4DScheduleTime)
			return false;
		return GetSynchro4DScheduleTime(Out, ScheduleIDOut);
	}

	// Add/update the specified clip's key-frame with current scene state
	std::shared_ptr<AdvViz::SDK::ITimelineKeyframe> AddOrUpdateKeyFrame(float fTime, int clipIdx)
	{
		AdvViz::SDK::ITimelineKeyframe::KeyframeData KF;
		KF.time = fTime;

		KF.camera.emplace();
		KF.camera->transform = FITwinMathConversion::UEtoSDK(
			ScreenUtils::GetCurrentViewTransform(Owner.GetWorld()),
			UseGCSForCameraKF());
		// Lens settings, needed by the iTwin viewer to reproduce the framing
		// (stored in radians, like the View3d schema's "fov").
		if (auto const FovDegrees = UITwinUtilityLibrary::GetCameraFOV())
		{
			KF.camera->fov = FMath::DegreesToRadians(*FovDegrees);
		}
		if (auto const Aspect = UITwinUtilityLibrary::GetViewportAspectRatio())
		{
			KF.camera->aspectRatio = *Aspect;
		}

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
			KF.atmo->sunAzimuth = data.sunAzimuth;
			KF.atmo->sunPitch = data.sunPitch;
			KF.atmo->heliodonLongitude = data.heliodonLongitude;
			KF.atmo->heliodonLatitude = data.heliodonLatitude;
			KF.atmo->HDRIZRotation = data.HDRIZRotation;
			KF.atmo->HDRIImage = data.HDRIImage;
			KF.atmo->sunIntensity = data.sunIntensity;
			KF.atmo->useHeliodon = data.useHeliodon;
			KF.atmo->exposure = data.exposure;
		}

		return AddOrUpdateKeyFrame(fTime, clipIdx, KF);
	}

	void RemoveKeyFrame(int iKF, int clipIdx)
	{
		auto clip = GetClip(clipIdx);
		if (!clip)
			return;

		BE_ASSERT(IsReady());
		auto& SeqHelper = *Owner.SequencerHelper;
		float const fTime = clip->GetKeyFrameTime(iKF);
		SeqHelper.RemoveKeyFrame(clip->GetTracks(), fTime);
		auto key = clip->GetKeyframe(fTime);
		if (key)
			clip->RemoveKeyframe(*key);
		// When we remove a key-frame, we don't want to modify the duration of the preceding
		// key-frame (see ADO#2182741) so we need to update all times after the removed one.
		if (clip->GetKeyframeCount() >= 2 && iKF < clip->GetKeyframeCount())
			MoveKeyFrame(clip->GetKeyFrameTime(iKF), fTime, clipIdx, false);
		else
			UpdateSceneFromTimeline(); // see comment in AddOrUpdateKeyFrame
	}

	void MoveKeyFrame(float fOldTime, float fNewTime, int clipIdx, bool bMoveOneKFOnly)
	{
		auto clip = GetClip(clipIdx);
		if (!clip || AdvViz::SDK::RoundTime(fOldTime) == AdvViz::SDK::RoundTime(fNewTime))
			return;

		if (!IsReady())
			return;
		auto& SeqHelper = *Owner.SequencerHelper;

		float const fTimeDelta = fNewTime - fOldTime;
		float lastKFToMove =
			bMoveOneKFOnly ? fOldTime : SeqHelper.GetEndTime(clip->GetCamera());
		SeqHelper.ShiftClipKFsInRange(clip->GetTracks(), fOldTime, lastKFToMove,
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

	bool ReorderKeyFrame(int clipIdx, int iKFSrc, int iKFDst)
	{
		auto clip = GetClip(clipIdx);
		if (!clip)
			return false;
		if (!clip->ReorderKeyFrame(iKFSrc, iKFDst))
			return false;

		if (!IsReady())
			return false;
		auto& SeqHelper = *Owner.SequencerHelper;

		// Key-frame times are unchanged, but the values stored at those times in the level sequence
		// tracks now belong to different key-frames: overwrite them in place. Do NOT remove the keys
		// first: AddKeyFrameToChannel already replaces any existing key at the same frame, whereas
		// RemoveKeyFrame would shrink (and possibly empty) the section range.
		int const iFirst = std::min(iKFSrc, iKFDst);
		int const iLast = std::max(iKFSrc, iKFDst);
		TArray<std::optional<UITwinSequencerHelper::KFValueType> > ParamValues;
		for (int i = iFirst; i <= iLast; ++i)
		{
			auto kf = clip->GetKeyframeByIndex(i);
			if (!kf)
				continue;
			auto const& KF = (*kf)->GetData();
			ConvertToSequencer(KF, ParamValues);
			SeqHelper.AddKeyFrame(clip->GetTracks(), KF.time, ParamValues);
		}

		UpdateSceneFromTimeline(); // see comment in AddOrUpdateKeyFrame

		return true;
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
		using ESetViewMode = ScreenUtils::ESetViewMode;

		BE_ASSERT(IsReady());
		auto const& SeqHelper = *Owner.SequencerHelper;

		const bool bIsPlaying = (pPlayer_.IsValid() && pPlayer_->IsPlaying());
		// If the timeline is playing, playback view remains owned by camera cuts + modifier.
		// Only sync pawn/controller pose to ensure Cesium remains in sync.
		const ESetViewMode SetViewMode = bIsPlaying
			? ESetViewMode::KeepCurrentViewTarget
			: ESetViewMode::AdoptPawnAsViewTarget;

		// Follow mode overrides the keyframed transform entirely: the camera is placed from the
		// tracked instance's current pose.
		if (ClipData* clip = FindClipByCamera(pCamera))
		{
			if (AITwinLookAtTargetActor* Follow = clip->GetFollowProxy())
			{
				if (TOptional<FTransform> const FollowTM = Follow->GetFollowWorldTransform())
				{
					ScreenUtils::SetCurrentView(Owner.GetWorld(), FollowTM->GetLocation(),
												FollowTM->Rotator(), SetViewMode);
					return;
				}
			}
		}

		FVector pos;
		FRotator rot;
		bool bSuccess = SeqHelper.GetTransformAtTime(pCamera, fTime, pos, rot);
		if (bSuccess)
		{
			BE_LOGV("Timeline", "Time set to " << fTime << ", setting current view to: "
				<< "Rotation (" << rot.Yaw << ", " << rot.Pitch << ", " << rot.Roll
				<< "), Position (" << pos.X << ", " << pos.Y << ", " << pos.Z << ")");
			ScreenUtils::SetCurrentView(Owner.GetWorld(), pos, rot, SetViewMode);
		}
		else
		{
			BE_LOGW("Timeline", "Failed to compute transform when setting time to " << fTime);
		}
	}

	/// \param Out If there is no "current time" (ie no clip), the current schedule date found on any iModel
	///		with a schedule is used. If there is no schedule either, ITwin::GetBaseDate() is used.
	/// \return true if the date returned comes from the animation, false if any fallback value was used
	bool GetSynchroDateFromTime(ClipData* clip, float fTime, FDateTime& Out)
	{
		double dateDelta(0);
		if (clip)
		{
			constexpr int32 idx = TrackIdx(EAnimTrack::Date);
			if (idx < clip->GetTracks().Num() && ensure(IsReady()))
			{
				auto const& SeqHelper = *Owner.SequencerHelper;
				SeqHelper.GetDoubleValueAtTime(clip->GetTracks()[idx].Get(), fTime, dateDelta);
				Out = ITwin::TimelineToSynchro(dateDelta);
				return true;
			}
		}
		if (GetSynchro4DScheduleTime)
		{
			FString Unused;
			if (GetSynchro4DScheduleTime(Out, Unused))
				return false; // yes, false, see dox
		}
		Out = ITwin::GetBaseDate();
		return false;
	}

	void UpdateSynchroDateFromTime(ClipData* clip, float fTime)
	{
		if (!SetSynchro4DScheduleTime)
			return;

		FDateTime curDate;
		if (GetSynchroDateFromTime(clip, fTime, curDate))
		{
			BE_LOGV("Timeline", "Time set to " << fTime << ", setting current date to "
				<< TCHAR_TO_UTF8(*(curDate.ToFormattedString(TEXT("%d %b %Y")))));
			SetSynchro4DScheduleTime(curDate);
		}
		else
		{
			BE_LOGW("Timeline", "Failed to compute date when setting time to to " << fTime);
		}
	}

	void UpdateAtmoFromTime(ClipData* clip, float fTime)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ITwinTimeline::UpdateAtmoFromTime);
		if (!clip || !Owner.GetAtmoSettingsDelegate.IsBound() || !Owner.SetAtmoSettingsDelegate.IsBound())
			return;

		BE_ASSERT(IsReady());
		auto const& SeqHelper = *Owner.SequencerHelper;

		FAtmoAnimSettings data;
		Owner.GetAtmoSettingsDelegate.Execute(data);

		bool changed = false;

		int32 idx = TrackIdx(EAnimTrack::DateSun);
		if (idx < clip->GetTracks().Num())
		{
			double dateDelta;
			SeqHelper.GetDoubleValueAtTime(clip->GetTracks()[idx].Get(), fTime, dateDelta);
			if (fabs(dateDelta) > 1e-6)
			{
				auto date = ITwin::TimelineToSynchro(dateDelta);
				if (data.heliodonDate != date)
				{
					data.heliodonDate = date;
					changed = true;
				}
			}
		}
		idx = TrackIdx(EAnimTrack::Clouds);
		if (idx < clip->GetTracks().Num())
		{
			float Value;
			SeqHelper.GetFloatValueAtTime(clip->GetTracks()[idx].Get(), fTime, Value);
			if (data.cloudCoverage != Value)
			{
				data.cloudCoverage = Value;
				changed = true;
			}
		}
		idx = TrackIdx(EAnimTrack::Fog);
		if (idx < clip->GetTracks().Num())
		{
			float Value;
			SeqHelper.GetFloatValueAtTime(clip->GetTracks()[idx].Get(), fTime, Value);
			if (data.fog != Value)
			{
				data.fog = Value;
				changed = true;
			}
		}
		idx = TrackIdx(EAnimTrack::SunAzimuth);
		if (idx < clip->GetTracks().Num())
		{
			float Value;
			SeqHelper.GetFloatValueAtTime(clip->GetTracks()[idx].Get(), fTime, Value);
			if (data.sunAzimuth != Value)
			{
				data.sunAzimuth = Value;
				changed = true;
			}
		}
		idx = TrackIdx(EAnimTrack::SunPitch);
		if (idx < clip->GetTracks().Num())
		{
			float Value;
			SeqHelper.GetFloatValueAtTime(clip->GetTracks()[idx].Get(), fTime, Value);
			if (data.sunPitch != Value)
			{
				data.sunPitch = Value;
				changed = true;
			}
		}
		idx = TrackIdx(EAnimTrack::HeliodonLong);
		if (idx < clip->GetTracks().Num())
		{
			float Value;
			SeqHelper.GetFloatValueAtTime(clip->GetTracks()[idx].Get(), fTime, Value);
			if (data.heliodonLongitude != Value)
			{
				data.heliodonLongitude = Value;
				changed = true;
			}
		}
		idx = TrackIdx(EAnimTrack::HeliodonLat);
		if (idx < clip->GetTracks().Num())
		{
			float Value;
			SeqHelper.GetFloatValueAtTime(clip->GetTracks()[idx].Get(), fTime, Value);
			if (data.heliodonLatitude != Value)
			{
				data.heliodonLatitude = Value;
				changed = true;
			}
		}
		idx = TrackIdx(EAnimTrack::HDRIZRotation);
		if (idx < clip->GetTracks().Num())
		{
			float Value;
			SeqHelper.GetFloatValueAtTime(clip->GetTracks()[idx].Get(), fTime, Value);
			if (data.HDRIZRotation != Value)
			{
				data.HDRIZRotation = Value;
				changed = true;
			}
		}
		idx = TrackIdx(EAnimTrack::SunIntensity);
		if (idx < clip->GetTracks().Num())
		{
			float Value;
			SeqHelper.GetFloatValueAtTime(clip->GetTracks()[idx].Get(), fTime, Value);
			if (data.sunIntensity != Value)
			{
				data.sunIntensity = Value;
				changed = true;
			}
		}
		idx = TrackIdx(EAnimTrack::HDRIImage);
		if (idx < clip->GetTracks().Num())
		{
			std::string Value;
			SeqHelper.GetStringValueAtTime(clip->GetTracks()[idx].Get(), fTime, Value);
			if (data.HDRIImage != Value)
			{
				data.HDRIImage = Value;
				changed = true;
			}
		}
		idx = TrackIdx(EAnimTrack::UseHeliodon);
		if (idx < clip->GetTracks().Num())
		{
			bool Value = true;
			SeqHelper.GetBoolValueAtTime(clip->GetTracks()[idx].Get(), fTime, Value);
			if (data.useHeliodon != Value)
			{
				data.useHeliodon = Value;
				changed = true;
			}
		}
		idx = TrackIdx(EAnimTrack::Exposure);
		if (idx < clip->GetTracks().Num())
		{
			float Value;
			SeqHelper.GetFloatValueAtTime(clip->GetTracks()[idx].Get(), fTime, Value);
			if (data.exposure != Value)
			{
				data.exposure = Value;
				changed = true;
			}
		}
		if (changed)
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
		TRACE_CPUPROFILER_EVENT_SCOPE(ITwinTimeline::UpdateSceneFromTimeline);
		if (!IsReady())
		{
			BE_LOGW("Timeline", "Invalid sequence helper - cannot update scene from timeline at time " << curTime_);
			return;
		}
		auto const& SeqHelper = *Owner.SequencerHelper;
		bool isCameraCutActive = SeqHelper.HasCameraCutTrack();
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
					pClip = GetClip(i - 1);
					if (!pClip)
					{
						// Did happen in OnScreenshotBatchProcessed - see crash ADO#2185982
						// (Sentry ITWIN-ENGAGE-H1).
						BE_LOGW("Timeline", "Camera cut start times out of sync with clips (index "
							<< (i - 1) << "/" << GetClipsNum() << ") at time " << curTime_);
						return;
					}
					pCamera = pClip->GetCamera();
					ClipStartTime = CurrentCutTrackStartTimes_[i - 1];
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
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't
	// need it.
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

bool AITwinTimelineActor::IsImplReady() const
{
	// Note that Impl is null before BeginPlay and after EndPlay, so it's worth checking for null
	// before calling IsReady() on it.
	return Impl && Impl->IsReady();
}

ULevelSequencePlayer* AITwinTimelineActor::GetPlayer()
{
	return Impl ? Impl->pPlayer_.Get() : nullptr;
}

ULevelSequence* AITwinTimelineActor::GetLevelSequence()
{
	return SequencerHelper ? SequencerHelper->GetLevelSequence() : nullptr;
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
	if (Impl)
		Impl->ClearAllClipTracking();
	Impl.Reset();
	Super::EndPlay(EndPlayReason);
}

// Called every frame, used to update UI during playback
void AITwinTimelineActor::Tick(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(AITwinTimelineActor::Tick);
	Super::Tick(DeltaTime);

	const bool bIsPlaying = Impl->pPlayer_->IsPlaying();
	const float fPlayerTime = Impl->pPlayer_->GetCurrentTime().AsSeconds();
	// Do this even when !IsPlaying otherwise we keep (and show in the UI) the last time obtained
	// from the player before it stopped, which can be substantial depending on frame rate and
	// have visible effect on the 4D because task state often changes *just* before the end.
	// See ADO#2152665
	//
	// However, we should not reset the time if the user paused the animation manually!
	// See ADO#2164441, which is a regression introduced by this change: in such case, the player
	// time is reset to zero internally, but this should not be used as the current time, otherwise
	// the animation restarts from the start when the user resumes it (which is wrong as it
	// contradicts the "Pause" icon in iTwin Studio UI).
	if (bIsPlaying || fPlayerTime > 0.f)
	{
		Impl->SetCurrentTime(fPlayerTime); // update synchro, atmo etc. from current time
	}

	if (bIsPlaying)
	{
		if (Impl->HasAnyPlaybackOwnershipTargets())
		{
			// Keep playback ownership in sync every frame from timeline time.
			// We do this even though __OnCameraCutHandler__ also updates ownership:
			// camera-cut events are not guaranteed in all runtime modes (notably non-PIE),
			// so this deterministic path is the authoritative fallback.
			AActor* ActivePlaybackCamera = nullptr;
			const auto [ClipIdx, _TimeWithinClip] = GetClipIdxAndTimeWithinSequence(fPlayerTime);
			if (ClipIdx >= 0)
			{
				if (ClipData* Clip = Impl->GetClip(ClipIdx))
					ActivePlaybackCamera = Clip->GetCamera();
			}
			Impl->ApplyPlaybackCameraOwnership(ActivePlaybackCamera);
		}

		if (Impl->PlaybackEndTime_ >= 0.f && fPlayerTime > Impl->PlaybackEndTime_)
			Impl->pPlayer_->Stop();
	}
	else
	{
		if (Impl->bHasBoundOnCameraCut)
		{
			Impl->pPlayer_->OnCameraCut.RemoveDynamic(this, &AITwinTimelineActor::OnCameraCutHandler);
			Impl->bHasBoundOnCameraCut = false;
			// Playback is over: hand the view back to the manual path.
			Impl->DeactivateAllLookAtModifiers();
			Impl->CurrentCutTrackCamera_ = nullptr;
		}
		SetActorTickEnabled(false);
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
	if (!IsImplReady())
		return;
	Impl->ExportToJson();
}

void AITwinTimelineActor::ImportData()
{
	RemoveAllClips();

	if (!IsImplReady())
		return;
	Impl->ImportFromJson();
}

void AITwinTimelineActor::RemoveAllClips(bool bRemoveEmptyOnly/* = false*/)
{
	if (!IsImplReady())
		return;
	for (int i(Impl->GetClipsNum()-1); i >= 0; i--)
	{
		if (!bRemoveEmptyOnly || Impl->GetClip(i)->GetKeyframeCount() == 0)
			Impl->RemoveClip(i);
	}
}

void AITwinTimelineActor::RemoveAllKeyframes(int clipIdx)
{
	if (!IsImplReady())
		return;

	auto clip = Impl->GetCurrentClip();
	if (!clip)
		return;

	while (clip->GetKeyframeCount() > 0)
	{
		Impl->RemoveKeyFrame(clip->GetKeyframeCount()-1, clipIdx);
	}
	SetCurrentTime(0.f);
}

void AITwinTimelineActor::AddClip()
{
	if (!IsImplReady())
		return;
	// Append a new clip and set it as current
	Impl->AppendClip();
	// Clip should always have at least one key-frame (--> no, user may want to create his animation entirely from saved views)
	//AddKeyFrame();
}


void AITwinTimelineActor::DuplicateClip(int clipIdx)
{
	if (!IsImplReady())
		return;
	Impl->DuplicateClip(clipIdx);
}

void AITwinTimelineActor::RemoveClip(int clipIdx)
{
	if (!IsImplReady())
		return;
	Impl->RemoveClip(clipIdx);
}

void AITwinTimelineActor::MoveClip(size_t indexSrc, size_t indexDst)
{
	if (!IsImplReady())
		return;
	Impl->MoveClip(indexSrc, indexDst);
}

void AITwinTimelineActor::EnableClip(bool bEnable, int clipIdx)
{
	if (!IsImplReady())
		return;
	if (auto Clip = Impl->GetClip(clipIdx))
		return Clip->SetEnable(bEnable);
}

void AITwinTimelineActor::EnableAllClips(bool bEnable)
{
	if (!IsImplReady())
		return;

	for (int i(Impl->GetClipsNum() - 1); i >= 0; i--)
		Impl->GetClip(i)->SetEnable(bEnable);
}

bool AITwinTimelineActor::IsClipEnabled(int clipIdx) const
{
	if (!IsImplReady())
		return false;
	if (auto Clip = Impl->GetClip(clipIdx))
		return Clip->IsEnabled();
	return false;
}

void AITwinTimelineActor::SetClipName(int clipIdx, FString ClipName)
{
	auto clip = Impl ? Impl->GetClip(clipIdx) : nullptr;
	if (clip)
		clip->SetNameU(ClipName);
}

FString AITwinTimelineActor::GetClipName(int clipIdx) const
{
	auto clip = Impl ? Impl->GetClip(clipIdx) : nullptr;
	return clip ? clip->GetNameU() : FString();
}

void AITwinTimelineActor::GetClipsNames(TArray<FString>& vClipNames) const
{
	if (!Impl)
		return;
	Impl->GetClipsNames(vClipNames);
}

void AITwinTimelineActor::GetClipsStartTimes(TArray<float>& vTimes, bool bAppendLastDuration/* = false*/) const
{
	if (!Impl)
		return;
	Impl->GetClipsStartTimes(vTimes, bAppendLastDuration);
}

void AITwinTimelineActor::GetEnabledClipIndices(TArray<int>& vIndices, bool bSkipEmpty/* = false*/) const
{
	if (!Impl)
		return;
	Impl->GetEnabledClipIndices(vIndices, bSkipEmpty);
}

bool AITwinTimelineActor::HasClipsToExport() const
{
	return Impl && Impl->HasClipsToExport();
}

float AITwinTimelineActor::GetClipStartTime(int clipIdx) const
{
	return Impl ? Impl->GetClipStartTime(clipIdx) : 0.f;
}

int AITwinTimelineActor::GetClipsNum() const
{
	return Impl ? Impl->GetClipsNum() : 0;
}

bool AITwinTimelineActor::SetCurrentClip(FString clipName, bool updateSceneFromTimeline/* = true*/)
{
	if (!Impl)
		return false;
	return Impl->SetCurrentClip(Impl->GetClipIndex(clipName), updateSceneFromTimeline);
}

bool AITwinTimelineActor::SetCurrentClip(int clipIdx, bool updateSceneFromTimeline/* = true*/)
{
	if (!Impl)
		return false;
	return Impl->SetCurrentClip(clipIdx, updateSceneFromTimeline);
}

int AITwinTimelineActor::GetCurrentClipIndex() const
{
	return Impl ? Impl->curClip_ : -1;
}

ACameraActor* AITwinTimelineActor::GetClipCamera(int clipIdx)
{
	auto clip = Impl ? Impl->GetClip(clipIdx) : nullptr;
	return clip ? clip->GetCamera() : nullptr;
}

void AITwinTimelineActor::SetClipSnapshotID(int clipIdx, const std::string& Id)
{
	auto clip = Impl ? Impl->GetClip(clipIdx) : nullptr;
	if (clip)
		clip->SetSnapshotId(Id);
}

void AITwinTimelineActor::SetKeyFrameSnapshotID(int clipIdx, int iKF, const std::string& Id)
{
	auto clip = Impl ? Impl->GetClip(clipIdx) : nullptr;
	if (clip)
	{
		auto kf = clip->GetKeyframeByIndex(iKF);
		if (kf)
			(*kf)->SetSnapshotId(Id);
	}
}

std::string AITwinTimelineActor::GetClipSnapshotID(int clipIdx)
{
	auto clip = Impl ? Impl->GetClip(clipIdx) : nullptr;
	if (clip)
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
	auto clip = Impl ? Impl->GetClip(clipIdx) : nullptr;
	if (clip)
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
	auto clip = Impl ? Impl->GetClip(clipIdx) : nullptr;
	if (clip)
	{
		const int KFCount = clip->GetKeyframeCount();
		Ids.reserve(KFCount);
		for (int i(0); i < KFCount; i++)
			Ids.push_back(GetKeyFrameSnapshotID(clipIdx, i));
	}
}

//TRange<FDateTime> AITwinTimelineActor::GetSynchroRange() const
//{
//	if (!IsImplReady())
//		return TRange<FDateTime>::Empty();
//	return Impl->synchroData_.GetTotalRange();
//}

//void AITwinTimelineActor::SetSynchroRange(const TRange<FDateTime>& newRange)
//{
//	if (!IsImplReady())
//		return;
//	Impl->synchroData_.SetTotalRange(newRange);
//}

void AITwinTimelineActor::AddKeyFrame()
{
	if (!IsImplReady())
		return;

	auto clip = Impl->GetCurrentClip();
	if (!clip || clip->HasKeyFrame(Impl->curTime_))
		return;

	Impl->AddOrUpdateKeyFrame(Impl->curTime_, Impl->curClip_);
}

void AITwinTimelineActor::AppendKeyFrame()
{
	if (!IsImplReady())
		return;

	auto clip = Impl->GetCurrentClip();
	if (!clip)
		return;

	float fDuration = clip->GetDuration();// SeqHelper.GetDuration(Impl->GetCurrentCamera());
	Impl->curTime_ = clip->GetKeyframeCount() > 0 ? (int)(fDuration * 100.f) / 100.f + ITwin::fDefaultTimeDelta : 0.f;
	Impl->AddOrUpdateKeyFrame(Impl->curTime_, Impl->curClip_);
}

void AITwinTimelineActor::UpdateKeyFrame(int iKF)
{
	if (!IsImplReady())
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
	if (!IsImplReady())
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
	// When we remove the last key-frame, ensure the current time remains in the timeline range.
	if (GetCurrentTime() > 0.f && GetCurrentTime() >= clip->GetDuration())
		SetCurrentTime(clip->GetDuration());
}

int AITwinTimelineActor::GetKeyframeCount() const
{
	auto clip = Impl ? Impl->GetCurrentClip() : nullptr;
	return clip ? (int)clip->GetKeyframeCount() : 0;
}

int AITwinTimelineActor::GetTotalKeyframeCount() const
{
	int nKFTotal(0);
	const int nClips = GetClipsNum();
	for (size_t i(0); i < nClips; i++)
	{
		auto clip = Impl->GetClip(i);
		if (clip && clip->IsEnabled())
			nKFTotal += (int)clip->GetKeyframeCount();
	}
	return nKFTotal;
}

void AITwinTimelineActor::GetKeyFrameTimes(TArray<float>& vTimes) const
{
	auto clip = Impl ? Impl->GetCurrentClip() : nullptr;
	if (clip)
		clip->GetKeyFrameTimes(vTimes);
}

void AITwinTimelineActor::GetKeyFrameDates(TArray<FDateTime>& vDates) const
{
	auto clip = Impl ? Impl->GetCurrentClip() : nullptr;
	if (clip)
		clip->GetKeyFrameDates(vDates);
}

//void AITwinTimelineActor::GetKeyFrameTextures(TArray<UTexture2D*>& vTextures)
//{
//	if (Impl->GetCurrentClip())
//		Impl->GetCurrentClip()->GetKeyFrameTextures(vTextures, pSnapshotManager_);
//}

bool AITwinTimelineActor::HasKeyFrameToPaste() const
{
	return Impl && Impl->HasKeyFrameToPaste();
}

float AITwinTimelineActor::GetCurrentTime() const
{
	return Impl ? Impl->curTime_ : 0.f;
}

FDateTime AITwinTimelineActor::GetCurrentDate() const
{
	FDateTime curDate = FDateTime::MinValue();
	if (Impl)
		Impl->GetSynchroDateFromTime(Impl->FindClipByCamera(Impl->GetCurrentCamera()), Impl->curTime_, curDate);
	return curDate;
}

void AITwinTimelineActor::OnSceneFromTimelineUpdate()
{
	UpdateFromTimelineEvent.Broadcast();
}

void AITwinTimelineActor::SetCurrentTime(float fTime)
{
	if (!IsImplReady())// || Impl->curTime_ == fTime) // even if time is the same, clip index could have changed
		return;

	if (Impl->pPlayer_->IsPlaying())
		return; // handled in Tick()

	// Set the time and update scene parameters from the timeline
	Impl->SetCurrentTime(fTime);
}

int AITwinTimelineActor::GetKeyFrameIndexFromTime(float fTime, bool bPrecise/* = false*/) const
{
	if (!IsImplReady() || !Impl->GetCurrentClip())
		return -1;
	return Impl->GetCurrentClip()->GetKeyFrameIndex(fTime, bPrecise);
}

float AITwinTimelineActor::GetKeyFrameTime(int iKF) const
{
	if (!IsImplReady() || !Impl->GetCurrentClip())
		return -1.f;
	return Impl->GetCurrentClip()->GetKeyFrameTime(iKF);
}

void AITwinTimelineActor::MoveKeyFrame(int clipIdx, float fOldTime, float fNewTime, bool bMoveOneKFOnly)
{
	// No dead-zone here on purpose: FImpl::MoveKeyFrame already rejects no-op moves using
	// AdvViz::SDK::RoundTime, i.e. at the millisecond precision of the timeline data model.
	// A coarser threshold here would silently drop small but legitimate edits coming from
	// the setters (SetKFDuration, SetPerFrameDuration, SetClipDuration).
	Impl->MoveKeyFrame(fOldTime, fNewTime, clipIdx, bMoveOneKFOnly);
}


void AITwinTimelineActor::ReorderKeyFrame(int clipIdx, int iKFSrc, int iKFDst)
{
	if (!IsImplReady())
		return;
	Impl->ReorderKeyFrame(clipIdx, iKFSrc, iKFDst);
}

void AITwinTimelineActor::CopyKeyFrame(int clipIdx, int iKF)
{
	Impl->CopyKeyFrame(clipIdx, iKF);
}

void AITwinTimelineActor::PasteKeyFrame(int clipIdx, int iKF)
{
	Impl->PasteKeyFrame(clipIdx, iKF);
}

void AITwinTimelineActor::DuplicateKeyFrame(int clipIdx, int iKF)
{
	Impl->DuplicateKeyFrame(clipIdx, iKF);
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

bool AITwinTimelineActor::IsSynchroAnimEnabled(int clipIdx) const
{
	auto clip = Impl->GetClipConst(clipIdx);
	return clip ? clip->bSynchroAnim : false;
}

bool AITwinTimelineActor::IsAtmoAnimEnabled(int clipIdx) const
{
	auto clip = Impl->GetClipConst(clipIdx);
	return clip ? clip->bAtmoAnim : false;
}

float AITwinTimelineActor::GetClipDuration(int clipIdx) const
{
	if (!IsImplReady())
		return 0.f;
	return Impl->GetClipDuration(clipIdx);
}

float AITwinTimelineActor::GetTotalDuration() const
{
	if (!IsImplReady())
		return 0.f;
	return Impl->GetTotalDuration();
}

void AITwinTimelineActor::SetKFDuration(int KF, float fDuration)
{
	if (!IsImplReady())
		return;

	auto clip = Impl->GetCurrentClip();
	if (!clip || clip->GetKeyframeCount() < 2)
		return;

	if (KF < 0 || KF + 1 >= (int)clip->GetKeyframeCount())
		return;

	float curKFTime = clip->GetKeyFrameTime(KF);
	float nextKFTime = clip->GetKeyFrameTime(KF + 1);
	MoveKeyFrame(Impl->curClip_, nextKFTime, curKFTime + fDuration, false);
}

void AITwinTimelineActor::SetClipDuration(int clipIdx, float fDuration)
{
	if (!IsImplReady())
		return;
	if (!ensure(fDuration > 0.f))
		return;
	auto clip = Impl->GetClip(clipIdx);
	if (!clip || clip->GetKeyframeCount() < 2)
		return;
	
	const float fCurrentDuration = clip->GetDuration();
	if (!ensure(fCurrentDuration > 0.f))
		return;
	if (fabs(fCurrentDuration - fDuration) < 1e-3f)
		return;
	// Scale all times with a common factor so that each key-frame keeps its relative duration to the next
	// one, but the total duration is now fDuration
	const float fScaleDurationBy = fDuration / fCurrentDuration;
	const size_t nKFs = clip->GetKeyframeCount();

	std::vector<float> vNewTimes;
	vNewTimes.resize(nKFs);
	for (size_t i(0); i < nKFs; i++)
	{
		vNewTimes[i] = clip->GetKeyFrameTime(i) * fScaleDurationBy;
	}

	Impl->SetClipKeyFrameTimes(*clip, vNewTimes);

	// Also adjust the current time accordingly.
	if (Impl->curTime_ > 0.f)
		SetCurrentTime(Impl->curTime_ * fScaleDurationBy);
}

void AITwinTimelineActor::SetPerFrameDuration(int clipIdx, float fPerFrameDuration)
{
	if (!IsImplReady())
		return;
	auto clip = Impl->GetClip(clipIdx);
	if (!clip || clip->GetKeyframeCount() == 0)
		return;

	const size_t nKFs = clip->GetKeyframeCount();

	std::vector<float> vNewTimes;
	vNewTimes.resize(nKFs);
	for (size_t i(0); i < nKFs; i++)
	{
		vNewTimes[i] = i * fPerFrameDuration;
	}

	Impl->SetClipKeyFrameTimes(*clip, vNewTimes);
}

std::pair<int, float> AITwinTimelineActor::GetClipIdxAndTimeWithinSequence(float fSeqTime)
{
	if (!IsImplReady())
		return { -1, 0.f };

	// During playback/export, camera-cut linking defines the effective clip layout.
	// Use those start times first (they also correctly handle "single selected clip" mode).
	if (Impl->CurrentCutTrackStartTimes_.Num() >= 2)
	{
		return ITwin::Timeline::GetClipIdxAndLocalTimeFromStartTimes(
			Impl->CurrentCutTrackStartTimes_, fSeqTime);
	}

	// Fallback for non-linked contexts.
	TArray<float> vStartTimes;
	Impl->GetClipsStartTimes(vStartTimes, true);
	return ITwin::Timeline::GetClipIdxAndLocalTimeFromStartTimes(vStartTimes, fSeqTime);
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

	AActor* CutCamera = CameraComponent ? CameraComponent->GetOwner() : nullptr;
	if (IsImplReady())
	{
		Impl->ApplyPlaybackCameraOwnership(CutCamera);
	}
}

bool AITwinTimelineActor::LinkClipsToCutTrack(int clipIdx/* = -1*/)
{
	if (!IsImplReady())
		return false;

	auto& SeqHelper = *SequencerHelper;

	Impl->CurrentCutTrackStartTimes_.Empty();

	bool bRes(false);
	FString outMsg;
	SeqHelper.AddCameraCutTrackToLevelSequence(true, bRes, outMsg);

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
		float fStartTime = SeqHelper.GetStartTime(clip->GetCamera());
		float fDeltaTime = fTotalDuration - fStartTime;
		if (fDeltaTime != 0.f)
			SeqHelper.ShiftClipKFs(clip->GetTracks(), fDeltaTime);

		// add clip to the camera cuts track
		float fTotalDurationNew(SeqHelper.GetEndTime(clip->GetCamera()));
		SeqHelper.LinkCameraToCameraCutTrack(clip->GetCamera(), fTotalDuration, fTotalDurationNew, bRes, outMsg);
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

	Impl->PlaybackEndTime_ = SeqHelper.GetPlaybackEndTime();

	return true;
}

bool AITwinTimelineActor::UnlinkClipsFromCutTrack()
{
	if (!IsImplReady())
		return false;

	auto& SeqHelper = *SequencerHelper;

	bool bRes(false);
	FString outMsg;
	SeqHelper.RemoveCameraCutTrackFromLevelSequence(bRes, outMsg);

	for (size_t i(0); i < Impl->GetClipsNum(); i++)
	{
		auto clip = Impl->GetClip(i);
		float fStartTime = SeqHelper.GetStartTime(clip->GetCamera());
		if (fStartTime != 0.f)
			SeqHelper.ShiftClipKFs(clip->GetTracks(), -fStartTime);
	}

	Impl->DeactivateAllLookAtModifiers();
	Impl->CurrentCutTrackStartTimes_.Empty();
	Impl->PlaybackEndTime_ = -1.f;
	return true;
}

//bool AITwinTimelineActor::IsInMovieMode()
//{
//	return SeqHelper.HasCameraCutTrack();
//}

void AITwinTimelineActor::OnPlaybackStarted()
{
	Impl->pPlayer_->OnCameraCut.AddUniqueDynamic(this, &AITwinTimelineActor::OnCameraCutHandler);
	Impl->bHasBoundOnCameraCut = true;
	SetActorTickEnabled(true);
}

void AITwinTimelineActor::ConnectToSynchro(
	std::function<bool(FDateTime& Out, FString& ScheduleIDOut)>&& InGetSynchro4DScheduleTime,
	std::function<void(FDateTime const& Date)>&& InSetSynchro4DScheduleTime/*={}*/)
{
	Impl->GetSynchro4DScheduleTime = std::move(InGetSynchro4DScheduleTime);
	if (InSetSynchro4DScheduleTime)
		Impl->SetSynchro4DScheduleTime = std::move(InSetSynchro4DScheduleTime);
}

std::shared_ptr<AdvViz::SDK::ITimeline> AITwinTimelineActor::GetTimelineSDK()
{
	return Impl->timeline_;
}

void AITwinTimelineActor::SetTimelineSDK(const std::shared_ptr<AdvViz::SDK::ITimeline> &p)
{
	Impl->timeline_ = p;
}

void AITwinTimelineActor::OnLoad(bool bUseGCS)
{
	Impl->OnLoad(bUseGCS);
	OnTimelineLoaded.Broadcast();
	//when loading , the timeline is up to date
	Impl->timeline_->SetShouldSaveRecursive(false);
}

void AITwinTimelineActor::ReinitPlayer()
{
	Impl->CreatePlayer();
}

void AITwinTimelineActor::SetClipFollowTarget(int clipIdx, AITwinPopulation* Population, int32 InstanceIndex,
	bool bFollowRotation /*= true*/,
	TOptional<FTransform> const& ExplicitOffset /*= {}*/)
{
	if (!IsImplReady())
		return;
	Impl->SetClipFollowTarget(clipIdx, Population, InstanceIndex, bFollowRotation, ExplicitOffset);
}

void AITwinTimelineActor::ClearClipFollowTarget(int clipIdx)
{
	if (!IsImplReady())
		return;
	Impl->ClearClipFollowTarget(clipIdx);
}

bool AITwinTimelineActor::HasClipFollowTarget(int clipIdx) const
{
	if (!IsImplReady())
		return false;
	ClipData const* clip = Impl->GetClipConst(clipIdx);
	// GetClipConst is const; the proxy getter must be const too (it is above).
	return clip && clip->GetFollowProxy() != nullptr;
}

void AITwinTimelineActor::SetClipTrackingTarget(int clipIdx, AITwinPopulation* Population,
												int32 InstanceIndex)
{
	if (!IsImplReady())
		return;
	Impl->SetClipTrackingTarget(clipIdx, Population, InstanceIndex);
}

void AITwinTimelineActor::ClearClipTrackingTarget(int clipIdx)
{
	if (!IsImplReady())
		return;
	Impl->ClearClipTracking(clipIdx);
}

bool AITwinTimelineActor::HasClipTrackingTarget(int clipIdx) const
{
	if (!Impl)
		return false;
	ClipData const* clip = Impl->GetClipConst(clipIdx);
	// GetClipConst is const; the proxy getter must be const too (it is above).
	return clip && clip->GetTrackingProxy() != nullptr;
}

void AITwinTimelineActor::SetClipTrackingInterpSpeed(int clipIdx, float Speed)
{
	if (!IsImplReady())
		return;
	if (ClipData* clip = Impl->GetClip(clipIdx))
		if (auto* CineCam = Cast<ACineCameraActor>(clip->GetCamera()))
			CineCam->LookatTrackingSettings.LookAtTrackingInterpSpeed = Speed;
}

//===================================================================================
// TEMPORARY: FOR TESTING PURPOSES ONLY

namespace
{
	//! Quick fix for existing vehicles.
	//! Yaw bringing the vehicle meshes' authored forward axis onto the camera's. The population
	//! meshes face +Y, whereas an Unreal camera looks down its local +X, so a camera placed with
	//! an identity rotation inside the instance looks out of the side window.
	constexpr float InstanceForwardYawDegrees = 90.f;

	//! Camera placement inside an instance, in its own frame: roughly at eye height, slightly
	//! ahead of the centre, i.e. a driver/cockpit view. Derived from the instance's bounds so
	//! that it scales with the mesh instead of assuming car-sized metres.
	FTransform MakeCockpitOffset(AITwinPopulation& Pop, int32 InstanceIndex)
	{
		FTransform const InstTM = Pop.GetInstanceTransform(InstanceIndex);
		FBox const WorldBox = Pop.GetInstanceBoundingBox(InstanceIndex);
		if (!WorldBox.IsValid)
			return FTransform::Identity;

		// GetInstanceBoundingBox works in world space: bring the extents back into the
		// instance's frame, where X is forward and Z up.
		FVector const LocalExtent =
			InstTM.InverseTransformVector(WorldBox.GetExtent()).GetAbs();
		FVector const LocalCenter =
			InstTM.InverseTransformPosition(WorldBox.GetCenter());

		// The body runs along the mesh's forward axis (+Y here), so the "ahead of centre" shift
		// must use the Y extent, not X.
		FVector const Seat(
			LocalCenter.X,
			LocalCenter.Y + 0.33f * LocalExtent.Y,
			LocalCenter.Z + 0.40f * LocalExtent.Z);
		return FTransform(FRotator(0.f, InstanceForwardYawDegrees, 0.f), Seat);
	}

	//! First population flagged as path-animated (the moving cars), with at least one instance.
	AITwinPopulation* FindAnimatedPopulation(UWorld* World)
	{
		for (TActorIterator<AITwinPopulation> PopIter(World); PopIter; ++PopIter)
		{
			if ((*PopIter)->IsPartOfPathAnimation() && (*PopIter)->GetNumberOfInstances() > 0)
				return *PopIter;
		}
		return nullptr;
	}

	//! First population that is *not* path-animated, to be used as a static point of interest.
	//! Skips ExcludePop so that "follow the car and look at something else" cannot degenerate
	//! into the car looking at itself.
	AITwinPopulation* FindStaticPopulation(UWorld* World, AITwinPopulation const* ExcludePop)
	{
		for (TActorIterator<AITwinPopulation> PopIter(World); PopIter; ++PopIter)
		{
			if (*PopIter != ExcludePop && !(*PopIter)->IsPartOfPathAnimation()
				&& (*PopIter)->GetNumberOfInstances() > 0)
			{
				return *PopIter;
			}
		}
		return nullptr;
	}
}

void AITwinTimelineActor::TrackSelectedInstance()
{
	const int32 ClipIdx = GetCurrentClipIndex();
	if (ClipIdx < 0)
		return;
	for (TActorIterator<AITwinPopulation> PopIter(GetWorld()); PopIter; ++PopIter)
	{
		if ((*PopIter)->HasSelectedInstance())
		{
			SetClipTrackingTarget(ClipIdx, *PopIter, (*PopIter)->GetSelectedInstanceIndex());
			break;
		}
	}
}

void AITwinTimelineActor::UntrackInstance()
{
	const int32 ClipIdx = GetCurrentClipIndex();
	if (ClipIdx < 0)
		return;
	ClearClipTrackingTarget(ClipIdx);
}

void AITwinTimelineActor::DriveAnimatedInstance()
{
	const int32 ClipIdx = GetCurrentClipIndex();
	if (ClipIdx < 0)
		return;
	AITwinPopulation* AnimPop = FindAnimatedPopulation(GetWorld());
	if (!AnimPop)
	{
		BE_LOGW("Timeline", "No path-animated population with instances found");
		return;
	}
	ClearClipTrackingTarget(ClipIdx); // look straight ahead, no look-at override
	SetClipFollowTarget(ClipIdx, AnimPop, 0, /*bFollowRotation*/true,
		MakeCockpitOffset(*AnimPop, 0));
}

void AITwinTimelineActor::FollowAnimatedInstance(bool bFollowRotation)
{
	const int32 ClipIdx = GetCurrentClipIndex();
	if (ClipIdx < 0)
		return;
	AITwinPopulation* AnimPop = FindAnimatedPopulation(GetWorld());
	if (!AnimPop)
	{
		BE_LOGW("Timeline", "No path-animated population with instances found");
		return;
	}
	ClearClipTrackingTarget(ClipIdx); // pure third-person: no look-at override
	SetClipFollowTarget(ClipIdx, AnimPop, 0, bFollowRotation);
}

void AITwinTimelineActor::UnfollowInstance()
{
	const int32 ClipIdx = GetCurrentClipIndex();
	if (ClipIdx < 0)
		return;
	ClearClipFollowTarget(ClipIdx);
}
//===================================================================================


void ScreenUtils::SetCurrentView(UWorld* pWorld, const FVector& pos, const FRotator& rot,
	ESetViewMode Mode /*= AdoptPawnAsViewTarget*/)
{
	BE_ASSERT(pWorld != nullptr);
	if (!pWorld)
		return;
	if (APlayerController* pController = pWorld->GetFirstPlayerController())
	{
		if (APawn* Pawn = pController->GetPawnOrSpectator())
		{
			Pawn->SetActorLocation(pos, false, nullptr, ETeleportType::TeleportPhysics);
			pController->SetControlRotation(rot);
			Pawn->SetActorRotation(rot);

			if (Mode == ESetViewMode::AdoptPawnAsViewTarget)
				pController->SetViewTargetWithBlend(Pawn);
		}
		auto pCamManager = pController->PlayerCameraManager;
		if (pCamManager && Mode == ESetViewMode::AdoptPawnAsViewTarget)
		{
			pCamManager->UpdateCamera(pWorld->GetDeltaSeconds());
		}
	}
}

void ScreenUtils::SetCurrentView(UWorld* pWorld, const FTransform& ft, bool bVRMode,
	ESetViewMode Mode /*= AdoptPawnAsViewTarget*/)
{
	BE_ASSERT(pWorld != nullptr);
	if (!pWorld)
		return;

	auto rotation = FRotator(ft.GetRotation());
	if (bVRMode) // only use yaw rotation in VR mode, as pitch and roll are managed by the HMD
	{
		rotation.Pitch = 0.0f;
		rotation.Roll = 0.0f;
	}
	SetCurrentView(pWorld, FVector(ft.GetTranslation()), rotation, Mode);
}

void ScreenUtils::GetCurrentView(UWorld* pWorld, FVector& pos, FRotator& rot)
{
	BE_ASSERT(pWorld != nullptr);
	if (!pWorld)
		return;
	if (APlayerController* pController = pWorld->GetFirstPlayerController())
	{
		//pController->CalcCamera();
		if (auto pCamManager = pController->PlayerCameraManager)
		{
			rot = pCamManager->GetCameraRotation();
		}

		if (APawn const* Pawn = pController->GetPawn())
		{
			pos = Pawn->GetActorLocation();
		}
	}
}

FTransform ScreenUtils::GetCurrentViewTransform(UWorld* pWorld)
{
	BE_ASSERT(pWorld != nullptr);
	if (!pWorld)
		return FTransform::Identity;
	FVector pos = FVector::ZeroVector;
	FRotator rot = FRotator::ZeroRotator;
	ScreenUtils::GetCurrentView(pWorld, pos, rot);
	BE_LOGV("Timeline", "Current view transform: "
		<< "Rotation (" << rot.Yaw << ", " << rot.Pitch << ", " << rot.Roll
		<< "), Position (" << pos.X << ", " << pos.Y << ", " << pos.Z << ")");
	return FTransform(rot, pos, FVector(1, 1, 1));
}

void ScreenUtils::SetCurrentViewRotation(UWorld* World, const FRotator& Rot)
{
	// Unlike SetCurrentView(FTransform, bVRMode), this does not zero pitch/roll in VR, where
	// they are owned by the HMD: look-at tracking would fight the headset. Gate the caller on
	// VR mode if tracking is ever enabled there.
	if (!World)
		return;
	if (APlayerController* pController = World->GetFirstPlayerController())
	{
		// Deliberately does NOT touch the pawn location, nor re-set the view target:
		// doing so every frame fights the movement component (see SetCurrentView).
		pController->SetControlRotation(Rot);
		if (APawn* Pawn = pController->GetPawnOrSpectator())
			Pawn->SetActorRotation(Rot);
	}
}

#if WITH_EDITOR

// Dev-only: make clip 0 track the currently selected population instance.
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinTrackSelectedInstance(
	TEXT("cmd.ITwin_TrackSelectedInstance"),
	TEXT("Track the selected instance."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	if (auto* TimelineActor = TWorldSingleton<AITwinTimelineActor>().Get(World))
		TimelineActor->TrackSelectedInstance();
})
);

// Dev-only: counterpart of cmd.ITwin_TrackSelectedInstance, to test the cleanup path.
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinUntrackInstance(
	TEXT("cmd.ITwin_UntrackInstance"),
	TEXT("Stop the first timeline clip's camera from tracking an instance."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	if (auto* TimelineActor = TWorldSingleton<AITwinTimelineActor>().Get(World))
		TimelineActor->UntrackInstance();
})
);



// Dev-only: third-person camera riding the first instance of the first path-animated population.
// The offset is taken from the current view, so place yourself behind/inside the car first.
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinFollowAnimatedInstance(
	TEXT("cmd.ITwin_FollowAnimatedInstance"),
	TEXT("Make the first timeline clip's camera ride the first animated (path) instance. "
		 "Optional arg: 0 to keep the current heading instead of turning with the vehicle."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	if (auto* TimelineActor = TWorldSingleton<AITwinTimelineActor>().Get(World))
	{
		// Default to true: a camera embedded in the car is the common third-person case.
		const bool bFollowRotation = Args.Num() < 1 || Args[0] != TEXT("0");
		TimelineActor->FollowAnimatedInstance(bFollowRotation);
	}
})
);

// Dev-only: "passenger" case - the camera rides the car but keeps looking at a static instance.
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinFollowAnimatedAndLookAt(
	TEXT("cmd.ITwin_FollowAnimatedAndLookAt"),
	TEXT("Ride the first animated (path) instance while looking at a static instance."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	if (auto* TimelineActor = TWorldSingleton<AITwinTimelineActor>().Get(World))
	{
		TimelineActor->FollowAnimatedInstance(/*bFollowRotation*/true);
		TimelineActor->TrackSelectedInstance();
	}
})
);

// Dev-only: cockpit view - the camera sits inside the first animated instance and looks ahead,
// instead of keeping the offset it had when follow was enabled.
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinDriveAnimatedInstance(
	TEXT("cmd.ITwin_DriveAnimatedInstance"),
	TEXT("Put the first timeline clip's camera inside the first animated (path) instance."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	if (auto* TimelineActor = TWorldSingleton<AITwinTimelineActor>().Get(World))
		TimelineActor->DriveAnimatedInstance();
})
);

// Dev-only: counterpart of the two commands above, to test the cleanup path.
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinUnfollowInstance(
	TEXT("cmd.ITwin_UnfollowInstance"),
	TEXT("Stop the first timeline clip's camera from following an instance."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	if (auto* TimelineActor = TWorldSingleton<AITwinTimelineActor>().Get(World))
		TimelineActor->UnfollowInstance();
})
);

#endif // WITH_EDITOR
