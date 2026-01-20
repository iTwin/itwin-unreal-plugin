/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSequencerHelper.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <Kismet/BlueprintFunctionLibrary.h>
#include <CoreMinimal.h>
#include <Evaluation/Blending/MovieSceneBlendType.h>
#include <UObject/StrongObjectPtr.h>
#include <optional>
#include <variant>
#include "ITwinSequencerHelper.generated.h"

class ACameraActor;
class ULevelSequence;
class UMovieSceneSection;
class UMovieSceneTrack;
class UMovieSceneCameraCutTrack;
struct FFrameNumberRange;


UCLASS()
class USequencerHelper : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	//UENUM(BlueprintType)
	enum ETrackType //class ETrackType : uint8
	{
		TT_None,
		TT_Float,
		TT_Double,
		TT_Transform,
		TT_Vector,
	};

	//USTRUCT(BlueprintType)
	struct FTrackInfo
	{
		//GENERATED_BODY()
		FName sName;
		ETrackType eType;

		FTrackInfo()
		{}

		FTrackInfo(FName InName, ETrackType InType) : sName(InName), eType(InType) 
		{}

		bool operator==(const FName& Other) const
		{
			return sName == Other;
		}
	};

	typedef std::variant<float, double, FTransform, FVector> KFValueType;

	// Get/add/remove possessable actor (that is already present in the level)

	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static FGuid GetPActorGuidFromLevelSequence(AActor* pActor, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg);

	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static FGuid AddPActorToLevelSequence(AActor* pActor, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg);

	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static void RemovePActorFromLevelSequence(AActor* pActor, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg);

	// Get/add/remove spawnable actor (that is not yet present in the level)

	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static FGuid GetSActorGuidFromLevelSequence(FString spawnableName, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg);

	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static FGuid AddSActorToLevelSequence(FString spawnableName, FString assetPath, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg);

	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static void RemoveSActorFromLevelSequence(FString spawnableName, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg);


	// Get/add/remove track

	template<class TrackType>
	static TrackType* GetTrackFromActorInLevelSequence(AActor* pActor, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg);
	template<class TrackType>
	static TrackType* AddTrackToActorInLevelSequence(AActor* pActor, FString levelSequencePath, bool bOverwriteExisting, bool& bOutSuccess, FString& outInfoMsg);
	template<class TrackType>
	static void RemoveTrackFromActorInLevelSequence(AActor* pActor, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg);
	static void RemoveAllTracksFromLevelSequence(AActor* pActor, FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg);

	// Get/add/remove section

	template<class TrackType>
	static UMovieSceneSection* GetSectionFromActorInLevelSequence(AActor* pActor, FString levelSequencePath, int iSectionIdx, bool& bOutSuccess, FString& outInfoMsg);
	template<class TrackType>
	static UMovieSceneSection* AddSectionToActorInLevelSequence(AActor* pActor, FString levelSequencePath, FFrameNumber FrameNum, EMovieSceneBlendType eBlendType, bool& bOutSuccess, FString& outInfoMsg);
	template<class TrackType>
	static void RemoveSectionFromActorInLevelSequence(AActor* pActor, FString levelSequencePath, int iSectionIdx, bool& bOutSuccess, FString& outInfoMsg);

	// Add/remove key-frames

	template<typename T>
	static FFrameNumberRange AddKeyFrameToActor(AActor* pActor, FString levelSequencePath, int iSectionIdx, FFrameNumber iFrameNum, const T& Value, int iKeyInterp, bool& bOutSuccess, FString& outInfoMsg);
	template<typename T>
	static FFrameNumberRange AddKeyFrameToTrack(UMovieSceneTrack* pTrack, int iSectionIdx, FFrameNumber iFrameNum, const T& Value, int iKeyInterp, bool& bOutSuccess, FString& outInfoMsg);
	template<typename T>
	static FFrameNumberRange RemoveKeyFrameFromActor(AActor* pActor, FString levelSequencePath, int iSectionIdx, FFrameNumber iFrameNum, bool& bOutSuccess, FString& outInfoMsg);
	template<typename T>
	static FFrameNumberRange RemoveKeyFrameFromTrack(UMovieSceneTrack* pTrack, int iSectionIdx, FFrameNumber iFrameNum, bool& bOutSuccess, FString& outInfoMsg);
	static FFrameNumberRange RemoveKeyFrameFromAllTracks(int iSectionIdx, FFrameNumber iFrameNum, bool& bOutSuccess, FString& outInfoMsg);

	// Get interpolated value
	
	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static bool GetTransformAtTime(AActor* pActor, FString levelSequencePath, float fTime, FVector& pos, FRotator& rot);
	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static bool GetFloatValueAtTime(UMovieSceneTrack* pTrack, FString levelSequencePath, float fTime, float& OutValue);
	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static bool GetDoubleValueAtTime(UMovieSceneTrack* pTrack, FString levelSequencePath, float fTime, double& OutValue);
	template<typename T>
	static bool GetActorValueAtTime(AActor* pActor, FString levelSequencePath, float fTime, T& OutValue);
	template<typename T>
	static bool GetTrackValueAtTime(UMovieSceneTrack* pTrack, FString levelSequencePath, float fTime, T& OutValue);

	// The following functions are based on the transform track that is supposedly always present in the actor sequence

	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static bool HasTransformKeyFrame(AActor* pActor, FString levelSequencePath, int iSectionIdx, FFrameNumber iFrameNum, bool& bOutSuccess, FString& outInfoMsg);

	// Get duration from the first transform section range
	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static float GetDuration(AActor* pActor, FString levelSequencePath);

	// Get start time from the first transform section range
	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static float GetStartTime(AActor* pActor, FString levelSequencePath);

	// Get end time from the first transform section range
	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static float GetEndTime(AActor* pActor, FString levelSequencePath);

	// Init MovieScene playback range from the first transform section range
	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static void AdjustMoviePlaybackRange(ACameraActor* pCameraActor, FString levelSequencePath);
	
	// Get MovieScene playback range
	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static float GetPlaybackEndTime(FString levelSequencePath);

	// Camera cut track

	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static bool HasCameraCutTrack(FString levelSequencePath);
	static ACameraActor* GetCameraCutBoundCamera(FString levelSequencePath, FFrameTime PlaybackTime);
	static UMovieSceneCameraCutTrack* AddCameraCutTrackToLevelSequence(FString levelSequencePath, bool bOverwriteExisting, bool& bOutSuccess, FString& outInfoMsg);
	static void RemoveCameraCutTrackFromLevelSequence(FString levelSequencePath, bool& bOutSuccess, FString& outInfoMsg);
	static void LinkCameraToCameraCutTrack(ACameraActor* pCameraActor, FString levelSequencePath, float fStartTime, float fEndTime, bool& bOutSuccess, FString& outInfoMsg);

	// Add clip
	
	// deprecated function that creates a new clip with a transform and a double track
	static bool AddNewClipOld(ACameraActor* pCameraActor, FString levelSequencePath);	
	// adds camera to the sequencer animation and creates a separate track for each given parameter
	static bool AddNewClip(ACameraActor* pCameraActor, FString levelSequencePath, const TArray<USequencerHelper::FTrackInfo>& AnimParams, TArray<TStrongObjectPtr<UMovieSceneTrack> >& OutTracks);

	// Add/remove/move key-frame

	// deprecated function that adds a transform and a date key-frame for given time
	static float AddKeyFrameOld(ACameraActor* pCameraActor, FString levelSequencePath, FTransform mTransform, double DaysDelta, float fTime);
	// deprecated function that removes a transform and a date key-frame for given time
	static void RemoveKeyFrameOld(ACameraActor* pCameraActor, FString levelSequencePath, float fTime);
	// deprecated function to move all key-frames by the given delta time
	static void ShiftClipKFsOld(ACameraActor* pCameraActor, FString levelSequencePath, float fDeltaTime);
	// move key-frames in the given range by the given delta time
	static void ShiftClipKFsInRangeOld(ACameraActor* pCameraActor, FString levelSequencePath, float fStartTime, float fEndTime, float fDeltaTime);

	// add key-frame to the camera animation
	static float AddKeyFrame(TArray<TStrongObjectPtr<UMovieSceneTrack> >& Tracks, FString levelSequencePath, float fTime, TArray<std::optional<KFValueType> >& Values);
	// remove key-frame from the camera animation
	static void RemoveKeyFrame(TArray<TStrongObjectPtr<UMovieSceneTrack> >& Tracks, FString levelSequencePath, float fTime);
	// move all key-frames by the given delta time
	static void ShiftClipKFs(TArray<TStrongObjectPtr<UMovieSceneTrack> >& Tracks, FString levelSequencePath, float fDeltaTime);
	// move key-frames in the given range by the given delta time
	static void ShiftClipKFsInRange(TArray<TStrongObjectPtr<UMovieSceneTrack> >& Tracks, FString levelSequencePath, float fStartTime, float fEndTime, float fDeltaTime);

	// Save level sequence asset to a file (provided for debug purposes only)
	UFUNCTION(BlueprintCallable, Category = "SequencerHelper")
	static void SaveLevelSequenceAsAsset(FString levelSequencePath, const FString& PackagePath); // PackagePath should be relative to /Content/, f.e. TEXT("/Game/MySavedSequence")

private:
	template<class TrackType>
	static UMovieSceneTrack* CreateTrackForParameter(ACameraActor* pCameraActor, FString levelSequencePath, const TRange<FFrameNumber>& initialRange, FName sTrackName, bool& bRes, FString& outMsg);
};