/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSequencerHelper.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <Kismet/BlueprintFunctionLibrary.h>
#include <CoreMinimal.h>
#include <Evaluation/Blending/MovieSceneBlendType.h>
//#include <Math/Range.h>
#include "ITwinSequencerHelper.generated.h"

class ACameraActor;
class ULevelSequence;
class UMovieSceneSection;
class UMovieScene3DTransformTrack;
class UMovieScene3DTransformSection;
class UMovieSceneCameraCutTrack;
struct FFrameNumberRange;


UCLASS()
class USequencerHelper : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Possessable actors (already present in the level)

	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static FGuid GetPActorGuidFromLevelSequence(AActor* pActor, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg);

	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static FGuid AddPActorToLevelSequence(AActor* pActor, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg);

	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static void RemovePActorFromLevelSequence(AActor* pActor, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg);

	//Spawnable actors (not present in the level)

	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static FGuid GetSActorGuidFromLevelSequence(FString spawnableName, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg);

	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static FGuid AddSActorToLevelSequence(FString spawnableName, FString assetPath, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg);

	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static void RemoveSActorFromLevelSequence(FString spawnableName, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg);

	// Add/remove a track to an actor in level sequence (only one track per type can be added)
	template<class TrackType>
	static TrackType* GetTrackFromActorInLevelSequence(AActor* pActor, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg);
	template<class TrackType>
	static TrackType* AddTrackToActorInLevelSequence(AActor* pActor, FString levelSequencePath, bool bOverwriteExisting, bool& bOutSuccess, FString& outInfoMsg);
	template<class TrackType>
	static void RemoveTrackFromActorInLevelSequence(AActor* pActor, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg);

	// Add/remove transform section to/from a track
	template<class TrackType, class SectionType>
	static SectionType* GetSectionFromActorInLevelSequence(AActor* pActor, FString levelSequencePath, int iSectionIdx, bool& bOutSuccess, FString& outInfoMsg);
	template<class TrackType, class SectionType>
	static SectionType* AddSectionToActorInLevelSequence(AActor* pActor, FString levelSequencePath, FFrameNumber FrameNum, EMovieSceneBlendType eBlendType, bool& bOutSuccess, FString& outInfoMsg);
	template<class TrackType, class SectionType>
	static void RemoveSectionFromActorInLevelSequence(AActor* pActor, FString levelSequencePath, int iSectionIdx, bool& bOutSuccess, FString& outInfoMsg);

	// Add/remove key-frames

	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static FFrameNumberRange AddTransformKeyFrame(AActor* pActor, FString levelSequencePath, int iSectionIdx, FFrameNumber iFrame, FTransform Transform, int iKeyInterp, bool& bOutSuccess, FString& outInfoMsg);
	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static FFrameNumberRange AddFloatKeyFrame(AActor* pActor, FString levelSequencePath, int iSectionIdx, FFrameNumber iFrameNum, float fValue, int iKeyInterp, bool& bOutSuccess, FString& outInfoMsg);
	
	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static bool HasTransformKeyFrame(AActor* pActor, FString levelSequencePath, int iSectionIdx, FFrameNumber iFrameNum, bool& bOutSuccess, FString& outInfoMsg);

	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static FFrameNumberRange RemoveTransformKeyFrame(AActor* pActor, FString levelSequencePath, int iSectionIdx, FFrameNumber iFrame, bool& bOutSuccess, FString& outInfoMsg);
	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static FFrameNumberRange RemoveFloatKeyFrame(AActor* pActor, FString levelSequencePath, int iSectionIdx, FFrameNumber iFrame, bool& bOutSuccess, FString& outInfoMsg);

	template<class ChannelType, typename T>
	static void AddKeyFrameToChannel(UMovieSceneSection* pSection, int iChannelIdx, FFrameNumber iFrame, T fValue, int iKeyInterp, bool& bOutSuccess, FString& outInfoMsg);
	template<class ChannelType>
	static void RemoveKeyFrameFromChannel(UMovieSceneSection* pSection, int iChannelIdx, FFrameNumber iFrame, bool& bOutSuccess, FString& outInfoMsg);

	// Camera cut track
	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static UMovieSceneCameraCutTrack* AddCameraCutTrackToLevelSequence(FString levelSequencePath, bool bOverwriteExisting, bool& bOutSuccess, FString& outInfoMsg);

	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static void RemoveCameraCutTrackFromLevelSequence(FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg);

	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static void LinkCameraToCameraCutTrack(ACameraActor* pCameraActor, FString levelSequencePath, float fStartTime, float fEndTime, bool& bOutSuccess, FString& outInfoMsg);
	
	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static bool AddNewClip(ACameraActor* pCameraActor, FString levelSequencePath);
	
	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static float AddKeyFrame(ACameraActor* pCameraActor, FString levelSequencePath, FTransform mTransform, float fDaysDelta, float fTime);
	
	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static void RemoveKeyFrame(ACameraActor* pCameraActor, FString levelSequencePath, float fTime);
	
	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static bool GetTransformAtTime(AActor* pActor, FString levelSequencePath, float fTime, FVector &pos, FRotator &rot);
	
	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static bool GetValueAtTime(AActor* pActor, FString levelSequencePath, float fTime, float& fOutValue);

	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static float GetDuration(AActor* pActor, FString levelSequencePath);
	
	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static float GetStartTime(AActor* pActor, FString levelSequencePath);
	
	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static float GetEndTime(AActor* pActor, FString levelSequencePath);
	
	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static void AdjustMoviePlaybackRange(ACameraActor* pCameraActor, FString levelSequencePath);

	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static void ShiftClipKFs(ACameraActor* pCameraActor, FString levelSequencePath, float fDeltaTime);

	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static void ShiftClipKFsInRange(ACameraActor* pCameraActor, FString levelSequencePath, float fStartTime, float fEndTime, float fDeltaTime);

	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	// PackagePath should be relative to /Content/, f.e. TEXT("/Game/MySavedSequence")
	static void SaveLevelSequenceAsAsset(FString levelSequencePath, const FString& PackagePath);
};