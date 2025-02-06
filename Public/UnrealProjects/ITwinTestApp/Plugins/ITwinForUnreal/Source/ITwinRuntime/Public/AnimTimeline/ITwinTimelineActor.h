/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTimelineActor.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CoreMinimal.h>
#include <GameFramework/Actor.h>
#include <memory>

#include "ITwinTimelineActor.generated.h"

class ULevelSequencePlayer;
class ULevelSequence;
class ACameraActor;
class AITwinIModel;

namespace SDK::Core {
	class ITimeline;
}

UCLASS()
class ITWINRUNTIME_API AITwinTimelineActor : public AActor
{
	GENERATED_BODY()

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Sets default values for this actor's properties
	AITwinTimelineActor();

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
	// Add new key-frame at current time
	void AddKeyFrame();
	// Add new key-frame at the end
	void AppendKeyFrame();
	// Update given key-frame from the current scene state: camera position, atmo settings, synchro date etc. (if iKF = -1, applies to the current key-frame)
	void UpdateKeyFrame(int iKF);
	// Removes given key-frame from the clip (if iKF = -1, applies to the current key-frame)
	void RemoveKeyFrame(int iKF);
	// Change key-frame time
	void MoveKeyFrame(float fOldTime, float fNewTime);

	// Get duration of a clip (or all clips if clipIdx = -1)
	float GetDuration(int clipIdx);
	// Set clip duration by adjusting transition time of all the key-frames
	void SetDuration(int clipIdx, float fDuration);
	// Set transition time of each key-frame to the given value
	void SetPerFrameDuration(int clipIdx, float fPerFrameDuration);

	float GetCurrentTime() const;
	void SetCurrentTime(float fTime);

	// Get/set clip properties
	void EnableClip(bool bEnable);
	bool IsClipEnabled() const;
	int GetClipsNum() const;
	void GetClipsNames(TArray<FString>& vClipNames) const;
	int GetCurrentClipIndex() const;
	bool SetCurrentClip(FString clipName);
	bool SetCurrentClip(int clipIdx);
	ACameraActor* GetClipCamera(int clipIdx);

	// Get/set key-frame properties
	int GetKeyFrameNum() const;
	void GetKeyFrameTimes(TArray<float>& vTimes) const;
	int GetKeyFrameIndexFromTime(float fTime, bool bPrecise = false) const;
	int GetCurrentKeyFrameIndex(bool bPrecise = false) const { return GetKeyFrameIndexFromTime(GetCurrentTime(), bPrecise); }
	float GetKeyFrameTime(int iKF) const;

	void GetKeyFrameDates(TArray<FDateTime>& vDates) const;

	bool HasKeyFrameToPaste() const;
	void CopyKeyFrame(int clipIdx, int iKF);
	void PasteKeyFrame(int clipIdx, int iKF);

	//TRange<FDateTime> GetSynchroRange() const;
	//void SetSynchroRange(const TRange<FDateTime>& newRange);
	// Sets pause duration of the given key-frame (or key-frame corresponding to current time if iKF = -1)
	//void SetKeyFramePause(int iKF, float fPause);
	// Sets duration (or transition time) of the given key-frame (or key-frame corresponding to current time if iKF = -1)
	//void SetKeyFrameDuration(int iKF, float fDuration);
	//float GetKeyFrameDuration(int iKF);
	
	// Helpers for animation playback & export from the timeline UI
	bool LinkCameraToCutTrack(int clipIdx);
	bool UninkCameraFromCutTrack(int clipIdx);
	bool AssembleClips();
	bool SplitClips();

	void DestroyAnimation();

	ULevelSequencePlayer* GetPlayer();
	ULevelSequence* GetLevelSequence();
	
	UFUNCTION(BlueprintCallable, Category = "iTwin")
	void OnPlaybackStarted();

	void SetDefaultIModel(AITwinIModel* InIModel);

	std::shared_ptr<SDK::Core::ITimeline> GetTimelineSDK();
	void SetTimelineSDK(std::shared_ptr<SDK::Core::ITimeline>& p);
	void OnLoad();

	class FImpl;
private:
	TPimplPtr<FImpl> Impl;

	//TSharedPtr<AITwinTimelineActor::FImpl> GetImpl();
	//const TSharedPtr<AITwinTimelineActor::FImpl> GetImpl() const;
};