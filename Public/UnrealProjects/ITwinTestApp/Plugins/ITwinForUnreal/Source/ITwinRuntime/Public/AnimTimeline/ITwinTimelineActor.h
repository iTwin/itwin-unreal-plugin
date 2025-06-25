/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTimelineActor.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CoreMinimal.h>
#include <Containers/Map.h>
#include <GameFramework/Actor.h>
#include <Misc/DateTime.h>
#include <UObject/StrongObjectPtr.h>

#include <memory>
#include <functional>
#include <vector>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include "SDK/Core/Visualization/Timeline.h"
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include "ITwinTimelineActor.generated.h"

class ULevelSequencePlayer;
class ULevelSequence;
class ACameraActor;
class AITwinIModel;


//namespace AdvViz::SDK {
//	class ITimeline;
//	class ITimelineKeyframe;
//	class ITimelineKeyframe::AtmoData;
//}

namespace ScreenUtils
{
	ITWINRUNTIME_API void SetCurrentView(UWorld* pWorld, const FTransform& ft);
	ITWINRUNTIME_API void SetCurrentView(UWorld* pWorld, const FVector& pos,const FRotator& rot);
	ITWINRUNTIME_API void GetCurrentView(UWorld* pWorld, FVector& pos, FRotator& rot);
	ITWINRUNTIME_API FTransform GetCurrentViewTransform(UWorld* pWorld);
}


// need a U-structure to use it in binding
USTRUCT()
struct FAtmoAnimSettings
{
	GENERATED_BODY()
	//float sunPitch = 0.f;
	//float sunAzimuth = 0.f;
	//bool useHeliodon = true;
	FDateTime heliodonDate;
	float cloudCoverage = 0.f;
	float fog = 0.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTimelineLoaded);

UCLASS()
class ITWINRUNTIME_API AITwinTimelineActor : public AActor
{
	GENERATED_BODY()

protected:
	// Called when the game starts or when spawned
	void BeginPlay() override;
	void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:	
	// Sets default values for this actor's properties
	AITwinTimelineActor();

	// atmosphere management is not part of the plugin but we can define a custom getter and setter for atmosphere settings if required
	DECLARE_DELEGATE_OneParam(FGetAtmoSettingsDelegate, FAtmoAnimSettings&);
	DECLARE_DELEGATE_OneParam(FSetAtmoSettingsDelegate, const FAtmoAnimSettings&);
	FGetAtmoSettingsDelegate GetAtmoSettingsDelegate;
	FSetAtmoSettingsDelegate SetAtmoSettingsDelegate;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FUpdateFromTimelineEvent);
	UPROPERTY()
	FUpdateFromTimelineEvent UpdateFromTimelineEvent;

	//static AITwinTimelineActor* Get();

	virtual void Tick(float DeltaTime) override;

	// Debug data import/export
	void ExportData();
	void ImportData();

	// Edit level sequence
	// Add a new clip
	void AddClip();
	// Remove given clip (if clipIdx = -1, applies to the current clip)
	void RemoveClip(int clipIdx);
	// Moves a clip from one position in the sequence to another
	void MoveClip(size_t indexSrc, size_t indexDst);
	// Add a new key-frame at the current time to the current clip
	void AddKeyFrame();
	// Add a new key-frame at the current clip's end
	void AppendKeyFrame();
	// Update given key-frame of the current clip from the current scene state: camera position, atmo settings, synchro date etc. (if iKF = -1, applies to the current key-frame)
	void UpdateKeyFrame(int iKF);
	// Removes given key-frame from the current clip (if iKF = -1, applies to the current key-frame)
	void RemoveKeyFrame(int iKF);
	// Change key-frame time (within the current clip)
	void MoveKeyFrame(int clipIdx, float fOldTime, float fNewTime, bool bMoveOneKFOnly);

	// Get duration of a clip
	float GetClipDuration(int clipIdx);
	// Get duration of all enabled clip
	float GetTotalDuration();
	// Set clip duration by adjusting transition time of all the key-frames
	void SetClipDuration(int clipIdx, float fDuration);
	// Set transition time of each key-frame to the given value
	void SetPerFrameDuration(int clipIdx, float fPerFrameDuration);
	// Set transition time of the given key-frame to the given value
	void SetKFDuration(int KF, float fDuration);

	float GetCurrentTime() const;
	FDateTime GetCurrentDate() const;
	void SetCurrentTime(float fTime);

	// Get/set clip properties
	int GetClipsNum() const;
	void SetClipName(int clipIdx, FString ClipName);
	FString GetClipName(int clipIdx) const;
	void GetClipsNames(TArray<FString>& vClipNames) const;
	void GetClipsStartTimes(TArray<float>& vTimes, bool bAppendLastDuration = false) const;
	float GetClipStartTime(int clipIdx) const;
	int GetCurrentClipIndex() const;
	bool SetCurrentClip(FString clipName, bool updateSceneFromTimeline = true);
	bool SetCurrentClip(int clipIdx, bool updateSceneFromTimeline = true);
	void EnableClip(bool bEnable, int clipIdx);
	void EnableAllClips(bool bEnable);
	bool IsClipEnabled(int clipIdx) const;
	ACameraActor* GetClipCamera(int clipIdx);

	void SetClipSnapshotID(int clipIdx, const std::string& Id);
	void SetKeyFrameSnapshotID(int clipIdx, int iKF, const std::string& Id);
	// Get unique snapshot IDs for clip/keyframe (initializing them if needed)
	std::string GetClipSnapshotID(int clipIdx);
	std::string GetKeyFrameSnapshotID(int clipIdx, int iKF);
	void GetKeyFrameSnapshotIDs(int clipIdx, std::vector<std::string>& Ids);

	// Get/set key-frame properties
	int GetKeyframeCount() const;
	int GetTotalKeyframeCount() const;
	void GetKeyFrameTimes(TArray<float>& vTimes) const;
	int GetKeyFrameIndexFromTime(float fTime, bool bPrecise = false) const;
	int GetCurrentKeyFrameIndex(bool bPrecise = false) const { return GetKeyFrameIndexFromTime(GetCurrentTime(), bPrecise); }
	float GetKeyFrameTime(int iKF) const;
	// Given global time within a sequence of clips, find the clip and its relative time it corresponds to
	std::pair<int, float> GetClipIdxAndTimeWithinSequence(float fSeqTime);

	/// \param vDates Output where to store keyframe Synchro4D schedule dates: when no date is available,
	///		the array will contain copies of FDateTime() (ie. zero-initialized).
	void GetKeyFrameDates(TArray<FDateTime>& vDates) const;

	bool HasKeyFrameToPaste() const;
	void CopyKeyFrame(int clipIdx, int iKF);
	void PasteKeyFrame(int clipIdx, int iKF);

	void EnableSynchroAnim(int clipIdx, bool bEnable);
	void EnableAtmoAnim(int clipIdx, bool bEnable);
	bool IsSynchroAnimEnabled(int clipIdx);
	bool IsAtmoAnimEnabled(int clipIdx);

	//TRange<FDateTime> GetSynchroRange() const;
	//void SetSynchroRange(const TRange<FDateTime>& newRange);
	// Sets pause duration of the given key-frame (or key-frame corresponding to current time if iKF = -1)
	//void SetKeyFramePause(int iKF, float fPause);
	// Sets duration (or transition time) of the given key-frame (or key-frame corresponding to current time if iKF = -1)
	//void SetKeyFrameDuration(int iKF, float fDuration);
	//float GetKeyFrameDuration(int iKF);
	
	// Helpers for animation playback & export from the timeline UI
	bool LinkClipsToCutTrack(int clipIdx = -1); // when -1, all enabled clips will be linked to the cut track
	bool UnlinkClipsFromCutTrack();

	UFUNCTION()
	void OnCameraCutHandler(UCameraComponent* CameraComponent);
	void OnSceneFromTimelineUpdate();

	void RemoveAllClips(bool bRemoveEmptyOnly = false);
	void RemoveAllKeyframes(int clipIdx);

	ULevelSequencePlayer* GetPlayer();
	ULevelSequence* GetLevelSequence();
	
	UFUNCTION(BlueprintCallable, Category = "iTwin")
	void OnPlaybackStarted();

	void SetSynchroIModels(
		std::function<TMap<FString, class UITwinSynchro4DSchedules*> const&()> InGetSchedules);

	std::shared_ptr<AdvViz::SDK::ITimeline> GetTimelineSDK();
	void SetTimelineSDK(const std::shared_ptr<AdvViz::SDK::ITimeline>& p);
	void OnLoad();

	/// Notified when the timeline has been loaded from the scene
	FOnTimelineLoaded OnTimelineLoaded;

	void ReinitPlayer();

	// Returns true when all the active clips of the timeline have been assembled in a single movie for playback or video export
	// (= shifted in time to form a sequence and linked to a camera cut track)
	//bool IsInMovieMode();

	class FImpl;
private:
	TPimplPtr<FImpl> Impl;

	//TSharedPtr<AITwinTimelineActor::FImpl> GetImpl();
	//const TSharedPtr<AITwinTimelineActor::FImpl> GetImpl() const;
};