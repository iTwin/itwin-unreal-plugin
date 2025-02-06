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
#include <Engine/World.h>
#include <Kismet/GameplayStatics.h>
#include <LevelSequence.h>
#include <LevelSequencePlayer.h>
#include <LevelSequenceActor.h>
#include <MovieSceneSequencePlayer.h>
#include <Camera/CameraComponent.h>
#include <CineCameraActor.h>
#include <Misc/Paths.h>
#include <HAL/FileManager.h>
#include <GameFramework/Pawn.h>
#include <optional>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include "SDK/Core/Visualization/Timeline.h"
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

namespace ITwin
{

	float fDefaultTimeDelta = 2.f; // default delta time when appending key-frames

	FString GetTimelineDataPath()
	{
		FString RelativePath = FPaths::ProjectContentDir();
		FString FullPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*RelativePath);
		FString FinalPath = FPaths::Combine(*FullPath, TEXT("Timeline_export.json"));
		UE_LOG(LogTemp, Warning, TEXT("Using path %s to save timeline data"), *FinalPath);
		return FinalPath;
	}

	void SetCurrentView(UWorld* pWorld, FVector& pos, FRotator& rot)
	{
		//APlayerController* pController = UGameplayStatics::GetPlayerController(pWorld, 0);
		if (APlayerController* pController = pWorld->GetFirstPlayerController())
		{
	/*		auto pCamera = pController->GetViewTarget();
			pCamera->SetActorLocation(pos);
			//pCamera->SetActorRotation(rot);
			pController->SetControlRotation(rot);*/
			pController->GetPawnOrSpectator()->SetActorLocation(pos, false, nullptr, ETeleportType::TeleportPhysics);
			pController->SetControlRotation(rot);
			pController->GetPawnOrSpectator()->SetActorRotation(rot);
			pController->SetViewTargetWithBlend(pController->GetPawnOrSpectator());
		}
	}

	void GetCurrentView(UWorld* pWorld, FVector& pos, FRotator& rot)
	{
		if (APlayerController* pController = pWorld->GetFirstPlayerController())
		{
			//APlayerCameraManager* camManager = pWorld->GetFirstPlayerController()->PlayerCameraManager;
			//pos = camManager->GetCameraLocation();
			//rot = camManager->GetCameraRotation();
			if (APawn const* Pawn = pController->GetPawn())
			{
				pos = Pawn->GetActorLocation();
				rot = Pawn->GetActorRotation();
			}
		}
	}

	ACineCameraActor* SpawnCamera(UWorld* pWorld)
	{
		FVector pos;
		FRotator rot;
		GetCurrentView(pWorld, pos, rot);

		// hack that allows to load an existing non-empty level sequence for testing
		if (auto ExistingCam = Cast<ACineCameraActor>(UGameplayStatics::GetActorOfClass(pWorld, ACineCameraActor::StaticClass())))
			return ExistingCam;

		FActorSpawnParameters SpawnInfo;
		auto NewCamera = pWorld->SpawnActor<ACineCameraActor>(ACineCameraActor::StaticClass(), pos, rot, SpawnInfo);
		if (UCameraComponent* CameraComponent = NewCamera->GetCameraComponent())
			if (APlayerCameraManager* CamManager = pWorld->GetFirstPlayerController()->PlayerCameraManager)
				CameraComponent->SetFieldOfView(CamManager->GetFOVAngle());
		return NewCamera;

	}

	FTransform GetCurrentViewTransform(UWorld* pWorld)
	{
		FVector pos;
		FRotator rot;
		GetCurrentView(pWorld, pos, rot);
		UE_LOG(LogTemp, Warning, TEXT("Current view transform: Rotation (%f, %f, %f), Position (%f, %f, %f)"), rot.Yaw, rot.Pitch, rot.Roll, pos.X, pos.Y, pos.Z);
		return FTransform(rot, pos, FVector(1, 1, 1));
	}

	FDateTime GetBaseDate()
	{
		return FDateTime(2000, 1, 1);
	}

	float SynchroToTimeline(FDateTime Date)
	{
		return (float)((Date - GetBaseDate()).GetDays());
	}

	FDateTime TimelineToSynchro(float DaysDelta)
	{
		return GetBaseDate() + FTimespan::FromDays(DaysDelta);
	}
}

void GetFTransform(const SDK::Core::dmat3x4& srcMat, FTransform &f) {
	using namespace SDK::Core;
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

void GetSDKTransform(const FTransform& f, SDK::Core::dmat3x4& dstTransform)
{
	using namespace SDK::Core;
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
	FDateTime datetime;
	FString datestring = UTF8_TO_TCHAR(s.c_str());
	FDateTime::ParseIso8601(*datestring, datetime);
	return datetime;
}

void DateTimeToStr(const FDateTime& datetime, std::string& s)
{
	s = TCHAR_TO_UTF8(*datetime.ToIso8601());
}


class ClipData : public SDK::Core::TimelineClip, public SDK::Core::Tools::TypeId<ClipData>
{
private:
	ACameraActor* pCamera_ = nullptr;

public:
	ClipData()
	{}

	using SDK::Core::Tools::TypeId<ClipData>::GetTypeId;
	std::uint64_t GetDynTypeId() override { return GetTypeId(); }
	bool IsTypeOf(std::uint64_t i) override { return (i == GetTypeId()); }

	void InitCamera(UWorld* pWorld)
	{
		pCamera_ = ITwin::SpawnCamera(pWorld);
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

	std::shared_ptr<SDK::Core::ITimelineKeyframe> AddOrUpdateKeyFrame(float fTime, const SDK::Core::ITimelineKeyframe::KeyframeData& KF)
	{
		auto ret = GetKeyframe(fTime);
		if (ret)
			(*ret)->Update(KF);
		else
		{
			SDK::Core::ITimelineKeyframe::KeyframeData KF2(KF);
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
			SDK::Core::ITimelineKeyframe::KeyframeData KF((*ret)->GetData());
			RemoveKeyframe(*ret);
			KF.time = fNewTime;
			AddKeyframe(KF);
		}
	}

	void GetKeyFrameTimes(TArray<float>& vTimes)
	{
		vTimes.Empty();
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
		for (unsigned i = 0; i < GetKeyframeCount(); ++i)
		{
			auto kf = GetKeyframeByIndex(i);
			if (kf && (*kf)->GetData().synchro)
			{
				FDateTime datetime(StrToDateTime((*kf)->GetData().synchro->date));
				vDates.Add(datetime);
			}
		}
	}
};

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

class Timeline : public SDK::Core::Timeline, public SDK::Core::Tools::TypeId<Timeline>
{
public:
	Timeline() {}

	using SDK::Core::Tools::TypeId<Timeline>::GetTypeId;
	std::uint64_t GetDynTypeId() override { return GetTypeId(); }
	bool IsTypeOf(std::uint64_t i) override { return (i == GetTypeId()); }
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
	AITwinIModel* iModel_;

	typedef SDK::Core::Timeline BaseClass;

	std::shared_ptr<SDK::Core::ITimeline> timeline_; // for now, we have only one clip

	int curClip_ = -1.f; // index of the current clip in the clip array (for now, once created, always 0)
	float curTime_ = 0.f;
	bool isLooping_ = false;
	
	std::shared_ptr<SDK::Core::ITimelineKeyframe> copiedKF_; // used for copy-paste
	//SynchroData synchroData_; // for now, we simply assign current UI date to each new key-frame

	FImpl(AITwinTimelineActor& InOwner) : Owner(InOwner)
	{
		// default iModel that will be used for synchro animation; can be reassigned later
		iModel_ = Cast<AITwinIModel>(UGameplayStatics::GetActorOfClass(InOwner.GetWorld(), AITwinIModel::StaticClass()));

		// Find predefined timeline-related level sequence (its creation is only possible in editor mode)
		levelSequencePath_ = FString("/ITwinForUnreal/ITwin/AnimTimeline/ITwinLevelSequence");
		pLevelSeq_.Reset(Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *levelSequencePath_)));

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
		//else
		//{
		//	pPlayerActor_.Reset((ALevelSequenceActor*)UGameplayStatics::GetActorOfClass(Owner.GetWorld(), ALevelSequenceActor::StaticClass()));
		//	if (pPlayerActor_.IsValid())
		//	{
		//		ensure(pPlayerActor_->GetSequence() == pLevelSeq_.Get());
		//		pLevelSeq_.Reset(pPlayerActor_->GetSequence());
		//		pPlayer_.Reset(pPlayerActor_->GetSequencePlayer());
		//	}
		//}
		if (pPlayer_.IsValid())
		{
			pLevelSeq_->MovieScene->SetDisplayRate(FFrameRate(30, 1)); // Match desired playback frame rate
			pLevelSeq_->MovieScene->SetEvaluationType(EMovieSceneEvaluationType::WithSubFrames);
			pPlayer_->SetFrameRate(FFrameRate(30, 1)); // Set to match 30 FPS
			//pPlayer_->ForceUpdate();
			pPlayer_->OnPlay.AddDynamic(&Owner, &AITwinTimelineActor::OnPlaybackStarted);
		}

		timeline_.reset(SDK::Core::Timeline::New());
	}

	~FImpl()
	{
	}

	void OnLoad()
	{
		//finalize camera clip
		for (size_t i = 0; i < timeline_->GetClipCount(); ++i)
		{
			auto clip = GetClip(i);
			clip->InitCamera(Owner.GetWorld());
			USequencerHelper::AddNewClip(clip->GetCamera(), levelSequencePath_);
			for (size_t j = 0; j < clip->GetKeyframeCount(); ++j)
			{
				auto kf = clip->GetKeyframeByIndex(j);
				if (!kf)
					continue;
				const auto& KF = (*kf)->GetData();
				BE_ASSERT(KF.camera.has_value());
				BE_ASSERT(KF.synchro.has_value());
				FTransform transf;
				GetFTransform(KF.camera->transform, transf);
				auto synchro = ITwin::SynchroToTimeline(StrToDateTime(KF.synchro->date));
				USequencerHelper::AddKeyFrame(clip->GetCamera(), levelSequencePath_, transf, synchro, KF.time);
			}
		}
		SetCurrentClip(0);
	}

	bool IsReady() const
	{
		return pLevelSeq_.IsValid();
	}

	ClipData* GetCurrentClip()
	{
		return GetClip(curClip_);
	}

	bool SetCurrentClip(int clipIdx)
	{
		if (clipIdx >= 0 && clipIdx < (int)timeline_->GetClipCount() && clipIdx != curClip_)
		{
			curClip_ = clipIdx;
			curTime_ = 0.f;
			USequencerHelper::AdjustMoviePlaybackRange(GetCurrentCamera(), levelSequencePath_);
			return true;
		}
		return false;
	}

	std::shared_ptr<SDK::Core::ITimelineClip> AppendClip(FString sName = FString())
	{
		static int nextFreeClipID = 0;
		if (sName.IsEmpty())
		{
			do {
				sName = FString::Printf(TEXT("Clip_%d"), ++nextFreeClipID);
			} while (GetClipIndex(sName) >= 0);
		}
		auto clip = timeline_->AddClip(TCHAR_TO_UTF8(*sName));
		BE_ASSERT(clip);
		auto clipdata = SDK::Core::Tools::DynamicCast<ClipData>(clip.get());
		BE_ASSERT(clipdata);
		clipdata->InitCamera(Owner.GetWorld());
		USequencerHelper::AddNewClip(clipdata->GetCamera(), levelSequencePath_);
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
			USequencerHelper::RemoveTrackFromActorInLevelSequence<UMovieScene3DTransformTrack>(clip->GetCamera(), levelSequencePath_, bRes, outMsg);
			USequencerHelper::RemovePActorFromLevelSequence(clip->GetCamera(), levelSequencePath_, bRes, outMsg);
			//clip->GetCamera()->Destroy();

			timeline_->RemoveClip(curClip_);
			curClip_ = (timeline_->GetClipCount() > 0) ? 0 : -1;
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

			auto clipdata = SDK::Core::Tools::DynamicCast<ClipData>(*clip);
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

	void GetClipsNames(TArray<FString>& vClipNames) const
	{
		vClipNames.Empty();
		for (size_t i(0); i < timeline_->GetClipCount(); i++)
		{
			auto ret = timeline_->GetClipByIndex(i);
			BE_ASSERT(ret);
			FString name(UTF8_TO_TCHAR((*ret)->GetName().c_str()));
			vClipNames.Add(name);
		}
	}

	ACameraActor* GetCurrentCamera()
	{
		auto clip = GetCurrentClip();
		BE_ASSERT(clip);
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
			if (auto kf = clip->GetKeyframe(iKF))
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

	// Add/update the specified clip's key-frame with given parameters (if clipIdx < 0, current clip is used)
	std::shared_ptr<SDK::Core::ITimelineKeyframe> AddOrUpdateKeyFrame(float fTime, int clipIdx, const SDK::Core::ITimelineKeyframe::KeyframeData &KF)
	{
		auto clip = (clipIdx >= 0) ? GetClip(clipIdx) : GetCurrentClip();
		if (!clip)
			return nullptr;

		// Update Unreal level sequence (TODO: manage atmosphere settings?)
		BE_ASSERT(KF.camera.has_value());
		BE_ASSERT(KF.synchro.has_value());
		FTransform transf;
		GetFTransform(KF.camera->transform, transf);
		auto synchro = ITwin::SynchroToTimeline(StrToDateTime(KF.synchro->date));
		fTime = USequencerHelper::AddKeyFrame(clip->GetCamera(), levelSequencePath_, transf, synchro, fTime);
		return clip->AddOrUpdateKeyFrame(fTime, KF);
	}

	// Add/update the specified clip's key-frame with current scene state (if clipIdx < 0, current clip is used)
	std::shared_ptr<SDK::Core::ITimelineKeyframe> AddOrUpdateKeyFrame(float fTime, int clipIdx)
	{
		SDK::Core::ITimelineKeyframe::KeyframeData KF;
		KF.time = fTime;
		KF.camera.emplace();
		GetSDKTransform(ITwin::GetCurrentViewTransform(Owner.GetWorld()), KF.camera->transform);
		auto date = (iModel_ && iModel_->Synchro4DSchedules) ? iModel_->Synchro4DSchedules->ScheduleTime : ITwin::GetBaseDate();
		KF.synchro.emplace();
		DateTimeToStr(date, KF.synchro->date);

		return AddOrUpdateKeyFrame(fTime, clipIdx, KF);
	}

	void RemoveKeyFrame(float fTime)
	{
		auto clip = GetCurrentClip();
		if (!clip)
			return;

		USequencerHelper::RemoveKeyFrame(clip->GetCamera(), levelSequencePath_, fTime);
		auto key = clip->GetKeyframe(fTime);
		if (key)
			clip->RemoveKeyframe(*key);
	}

	void MoveKeyFrame(float fOldTime, float fNewTime)
	{
		auto clip = GetCurrentClip();
		if (!clip)
			return;

		USequencerHelper::ShiftClipKFsInRange(clip->GetCamera(), levelSequencePath_, fOldTime, fOldTime, fNewTime - fOldTime);
		clip->MoveKeyFrame(fOldTime, fNewTime);
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

	void UpdateCameraFromCurrentTime()
	{
		FVector pos;
		FRotator rot;
		bool bSuccess = USequencerHelper::GetTransformAtTime(GetCurrentCamera(), levelSequencePath_, curTime_, pos, rot);
		if (bSuccess)
		{
			UE_LOG(LogTemp, Warning, TEXT("Time set to %f, setting current view to: Rotation (%f, %f, %f), Position (%f, %f, %f)"), curTime_, rot.Yaw, rot.Pitch, rot.Roll, pos.X, pos.Y, pos.Z);
			ITwin::SetCurrentView(Owner.GetWorld(), pos, rot);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Failed to compute transform when setting time to %f"), curTime_);
		}
	}

	void UpdateSynchroDateFromCurrentTime()
	{
		if (iModel_ && iModel_->Synchro4DSchedules)
		{
			float daysDelta;
			if (USequencerHelper::GetValueAtTime(GetCurrentCamera(), levelSequencePath_, curTime_, daysDelta))
			{
				FDateTime curDate = ITwin::TimelineToSynchro(daysDelta);
				UE_LOG(LogTemp, Warning, TEXT("Time set to %f, setting current date to %s"), curTime_, *(curDate.ToFormattedString(TEXT("%d %b %Y"))));
				iModel_->Synchro4DSchedules->ScheduleTime = curDate;
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Failed to compute date when setting time to %f"), curTime_);
			}
		}
	}

	void UpdateAtmoFromCurrentTime()
	{
		// TODO
	}
};

AITwinTimelineActor::AITwinTimelineActor()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("root")));
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	static bool bNewInit = false;
	if (!bNewInit)
	{
		bNewInit = true;
		SDK::Core::Timeline::SetNewFct([]() {
			return static_cast<SDK::Core::Timeline*>(new Timeline);
			});

		SDK::Core::TimelineClip::SetNewFct([]() {
			return static_cast<SDK::Core::TimelineClip*>(new ClipData);
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

// Called every frame, used to update UI during playback
void AITwinTimelineActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (Impl->pPlayer_->IsPlaying())
	{
		Impl->curTime_ = Impl->pPlayer_->GetCurrentTime().AsSeconds();
		Impl->UpdateCameraFromCurrentTime();
		Impl->UpdateSynchroDateFromCurrentTime();
		if (Impl->curTime_ > GetDuration(Impl->curClip_)) // TODO: adapt to multiple clips
			Impl->pPlayer_->Stop();
	}
	else
		SetActorTickEnabled(false);
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
	DestroyAnimation();

	Impl->ImportFromJson();
}

void AITwinTimelineActor::DestroyAnimation()
{
	if (!Impl->IsReady())
		return;
	for (int i(Impl->GetClipsNum()-1); i >= 0; i--)
	{
		Impl->RemoveClip(i);
	}
}

void AITwinTimelineActor::AddClip()
{
	if (!Impl->IsReady())
		return;
	// Append a new clip and set it as current
	Impl->AppendClip();
	// Clip should always have at least one key-frame
	AddKeyFrame();
}

void AITwinTimelineActor::RemoveClip(int clipIdx)
{
	if (!Impl->IsReady())
		return;
	Impl->RemoveClip(clipIdx);
}

// TODO: add param for clip index
void AITwinTimelineActor::EnableClip(bool bEnable)
{
	if (!Impl->IsReady() || !Impl->GetCurrentClip())
		return;
	Impl->GetCurrentClip()->SetEnable(bEnable);
}

// TODO: add param for clip index
bool AITwinTimelineActor::IsClipEnabled() const
{
	if (!Impl->IsReady() || !Impl->GetCurrentClip())
		return false;
	return Impl->GetCurrentClip()->IsEnabled();
}

void AITwinTimelineActor::GetClipsNames(TArray<FString>& vClipNames) const
{
	Impl->GetClipsNames(vClipNames);
}

int AITwinTimelineActor::GetClipsNum() const
{
	return Impl->GetClipsNum();
}

bool AITwinTimelineActor::SetCurrentClip(FString clipName)
{
	return Impl->SetCurrentClip(Impl->GetClipIndex(clipName));
}

bool AITwinTimelineActor::SetCurrentClip(int clipIdx)
{
	return Impl->SetCurrentClip(clipIdx);
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
	if (!Impl->IsReady() || !Impl->GetCurrentClip() || Impl->GetCurrentClip()->HasKeyFrame(Impl->curTime_))
		return;
	Impl->AddOrUpdateKeyFrame(Impl->curTime_, -1);
}

void AITwinTimelineActor::AppendKeyFrame()
{
	if (!Impl->IsReady() || !Impl->GetCurrentClip())
		return;
	float fDuration = USequencerHelper::GetDuration(Impl->GetCurrentCamera(), Impl->levelSequencePath_);
	Impl->curTime_ = (int)(fDuration * 100.f) / 100.f + ITwin::fDefaultTimeDelta;
	Impl->AddOrUpdateKeyFrame(Impl->curTime_, -1);
}

void AITwinTimelineActor::UpdateKeyFrame(int iKF)
{
	if (!Impl->IsReady() || !Impl->GetCurrentClip())
		return;

	if (iKF >= 0)
	{
		if (Impl->GetCurrentClip()->HasKeyFrame(iKF))
			Impl->AddOrUpdateKeyFrame(Impl->GetCurrentClip()->GetKeyFrameTime(iKF), -1);
	}
	else
	{
		if (Impl->GetCurrentClip()->HasKeyFrame(Impl->curTime_))
			Impl->AddOrUpdateKeyFrame(Impl->curTime_, -1);
	}
}

void AITwinTimelineActor::RemoveKeyFrame(int iKF) // TODO doesn't work?
{
	if (!Impl->IsReady() || !Impl->GetCurrentClip() || Impl->GetCurrentClip()->GetKeyframeCount() <= 1)
		return;

	if (iKF >= 0)
	{
		if (Impl->GetCurrentClip()->HasKeyFrame(iKF))
			Impl->RemoveKeyFrame(Impl->GetCurrentClip()->GetKeyFrameTime(iKF));
	}
	else
	{
		if (Impl->GetCurrentClip()->HasKeyFrame(Impl->curTime_))
			Impl->RemoveKeyFrame(Impl->curTime_);
	}
	//SetCurrentTime(USequencerHelper::GetDuration(Impl->GetCurrentCamera(), Impl->levelSequencePath_));
}

int AITwinTimelineActor::GetKeyFrameNum() const
{
	auto clip = Impl->GetCurrentClip();
	return clip ? (int)clip->GetKeyframeCount() : 0;
}

void AITwinTimelineActor::GetKeyFrameTimes(TArray<float>& vTimes) const
{
	if (Impl->GetCurrentClip())
		Impl->GetCurrentClip()->GetKeyFrameTimes(vTimes);
}

void AITwinTimelineActor::GetKeyFrameDates(TArray<FDateTime>& vDates) const
{
	if (Impl->GetCurrentClip())
		Impl->GetCurrentClip()->GetKeyFrameDates(vDates);
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

void AITwinTimelineActor::SetCurrentTime(float fTime)
{
	if (!Impl->IsReady() || Impl->curTime_ == fTime)
		return;
	if (Impl->pPlayer_->IsPlaying())
		return; // handled in OnTick()

	Impl->curTime_ = fTime;
	Impl->UpdateCameraFromCurrentTime();
	Impl->UpdateSynchroDateFromCurrentTime();
	Impl->UpdateAtmoFromCurrentTime();
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

//void AITwinTimelineActor::SetKeyFramePause(int iKF, float fTime)
//{
//	if (!Impl->IsReady() || !Impl->GetCurrentClip())
//		return;
//	// TODO
//}
//
//void AITwinTimelineActor::SetKeyFrameDuration(int iKF, float fDuration)
//{
//	if (!Impl->IsReady() || !Impl->GetCurrentClip())
//		return;
//	float fTime = GetKeyFrameTime(iKF);
//	float fNextTime = GetKeyFrameTime(iKF + 1);
//	if (fTime < 0.f || fNextTime < 0.f)
//		return; // we don't set transition time on the last frame
//	MoveKeyFrame(fNextTime, fTime + fDuration);
//}
//
//float AITwinTimelineActor::GetKeyFrameDuration(int iKF)
//{
//	if (!Impl->IsReady() || !Impl->GetCurrentClip())
//		return 0.f;
//	float fTime = GetKeyFrameTime(iKF);
//	float fNextTime = GetKeyFrameTime(iKF + 1);
//	if (fTime < 0.f || fNextTime < 0.f)
//		return fDefaultTimeDelta; // we don't set transition time on the last frame
//	return fNextTime - fTime;
//}

void AITwinTimelineActor::MoveKeyFrame(float fOldTime, float fNewTime)
{
	if (fabsf(fOldTime-fNewTime) < 0.1)
		return;
	Impl->MoveKeyFrame(fOldTime, fNewTime);
}

void AITwinTimelineActor::CopyKeyFrame(int clipIdx, int iKF)
{
	Impl->CopyKeyFrame(clipIdx, iKF);
}

void AITwinTimelineActor::PasteKeyFrame(int clipIdx, int iKF)
{
	Impl->PasteKeyFrame(clipIdx, iKF);
}

float AITwinTimelineActor::GetDuration(int clipIdx)
{
	if (!Impl->IsReady())
		return 0.f;
	if (clipIdx >= 0)
	{
		if (auto clip = Impl->GetClip(clipIdx))
			return USequencerHelper::GetEndTime(clip->GetCamera(), Impl->levelSequencePath_);
	}
	else
	{
		float fTotalDuration(0.f);
		for (size_t i(0); i < Impl->GetClipsNum(); i++)
		{
			auto clip = Impl->GetClip(i);
			if (!clip->IsEnabled())
				continue;
			fTotalDuration += USequencerHelper::GetEndTime(clip->GetCamera(), Impl->levelSequencePath_);
		}
		return fTotalDuration;
	}
	return 0.f;
}

void AITwinTimelineActor::SetDuration(int clipIdx, float fDuration)
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
		MoveKeyFrame(vTimes[i], i * fPerFrameDuration);
	}
}

bool AITwinTimelineActor::LinkCameraToCutTrack(int clipIdx)
{
	auto clip = Impl->GetClip(clipIdx);
	if (!clip)
		return false;

	// Check or create Camera Cut
	bool bRes = false;
	FString outInfoMsg;
	USequencerHelper::AddCameraCutTrackToLevelSequence(Impl->levelSequencePath_, true, bRes, outInfoMsg);

	// Link clip camera to Camera Cut
	USequencerHelper::LinkCameraToCameraCutTrack(clip->GetCamera(), Impl->levelSequencePath_, 0.f, clip->GetDuration(), bRes, outInfoMsg);

	if (!bRes)
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to link clip %d to Camera Cuts: %s"), clipIdx, *outInfoMsg);
		return false;
	}
	return true;
}

bool AITwinTimelineActor::UninkCameraFromCutTrack(int clipIdx)
{
	auto clip = Impl->GetClip(clipIdx);
	if (!clip)
		return false;

	// Check or create Camera Cut
	bool bRes = false;
	FString outInfoMsg;
	USequencerHelper::RemoveCameraCutTrackFromLevelSequence(Impl->levelSequencePath_, bRes, outInfoMsg); // for testing only, need to implement properly


	return bRes;
}

bool AITwinTimelineActor::AssembleClips()
{
	if (!Impl->IsReady())
		return false;

	bool bRes;
	FString outMsg;

	float fTotalDuration(0.f);
	USequencerHelper::AddCameraCutTrackToLevelSequence(Impl->levelSequencePath_, true, bRes, outMsg);
	for (size_t i(0); i < Impl->GetClipsNum(); i++)
	{
		auto clip = Impl->GetClip(i);
		if (!clip->IsEnabled())
			continue;
		// shift clip key-frames so that it starts right after the previous clip
		if (fTotalDuration > 0.f)
			USequencerHelper::ShiftClipKFs(clip->GetCamera(), Impl->levelSequencePath_, fTotalDuration);
		// add clip to the camera cuts track
		float fTotalDurationNew(USequencerHelper::GetEndTime(clip->GetCamera(), Impl->levelSequencePath_));
		USequencerHelper::LinkCameraToCameraCutTrack(clip->GetCamera(), Impl->levelSequencePath_, fTotalDuration, fTotalDurationNew, bRes, outMsg);
		if (!bRes)
		{
			UE_LOG(LogTemp, Warning, TEXT("Failed to link clip %d to Camera Cuts: %s"), i, *outMsg);
			continue;
		}
		fTotalDuration = fTotalDurationNew;
	}

	return true;
}

bool AITwinTimelineActor::SplitClips()
{
	if (!Impl->IsReady())
		return false;

	bool bRes;
	FString outMsg;

	USequencerHelper::RemoveCameraCutTrackFromLevelSequence(Impl->levelSequencePath_, bRes, outMsg);
	for (size_t i(0); i < Impl->GetClipsNum(); i++)
	{
		auto clip = Impl->GetClip(i);
		if (!clip->IsEnabled())
			continue;
		float fTime = USequencerHelper::GetStartTime(clip->GetCamera(), Impl->levelSequencePath_);
		if (fTime > 0.f)
			USequencerHelper::ShiftClipKFs(clip->GetCamera(), Impl->levelSequencePath_, -fTime);
	}
	return true;
}

void AITwinTimelineActor::OnPlaybackStarted()
{
	SetActorTickEnabled(true);
}

void AITwinTimelineActor::SetDefaultIModel(AITwinIModel* InIModel)
{
	if (InIModel)
		Impl->iModel_ = InIModel;
}

std::shared_ptr<SDK::Core::ITimeline> AITwinTimelineActor::GetTimelineSDK()
{
	return Impl->timeline_;
}

void AITwinTimelineActor::SetTimelineSDK(std::shared_ptr<SDK::Core::ITimeline> &p)
{
	Impl->timeline_ = p;
}

void AITwinTimelineActor::OnLoad()
{
	Impl->OnLoad();
}