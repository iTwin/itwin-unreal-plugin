/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSequencerHelper.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <CoreMinimal.h>
#include <Evaluation/Blending/MovieSceneBlendType.h>
#include <UObject/Object.h>
#include <UObject/StrongObjectPtr.h>
#include <optional>
#include <variant>
#include <vector>
#include <string>
#include "ITwinSequencerHelper.generated.h"

class ACameraActor;
class ULevelSequence;
class UMovieScene;
class UMovieSceneSection;
class UMovieSceneTrack;
class UMovieSceneCameraCutTrack;
struct FFrameNumberRange;


UCLASS()
class UITwinSequencerHelper : public UObject
{
	GENERATED_BODY()

#if WITH_TESTS
	// Test-only access to the private track/section/key-frame templates below.
	// See Private/Tests/ITwinSequencerHelperTestAccess.h.
	friend class FSequencerHelperTestAccess;
#endif

public:

	//UENUM(BlueprintType)
	enum ETrackType //class ETrackType : uint8
	{
		TT_None,
		TT_Float,
		TT_Double,
		TT_Transform,
		TT_Vector,
		TT_Bool,
		TT_String,
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

	typedef std::variant<float, double, FTransform, FVector, bool, std::string> KFValueType;


public:
	// --- Lifecycle -------------------------------------------------------

	/// Loads the given level sequence and keeps a hard reference to it. Must be called
	/// once before any other method. Returns false if the asset is missing (the predefined
	/// timeline sequence can only be created in editor mode).
	bool Initialize(const FString& InLevelSequencePath);

	const FString& GetLevelSequencePath() const { return LevelSequencePath; }

	bool IsInitialized() const { return LevelSequence != nullptr; }
	ULevelSequence* GetLevelSequence() const { return LevelSequence; }
	UMovieScene* GetMovieScene() const;

	// Get/add/remove possessable actor (that is already present in the level)
	FGuid GetPActorGuidFromLevelSequence(AActor* pActor, bool& bOutSuccess, FString& outInfoMsg) const;
	FGuid AddPActorToLevelSequence(AActor* pActor, bool& bOutSuccess, FString& outInfoMsg);
	void RemovePActorFromLevelSequence(AActor* pActor, bool& bOutSuccess, FString& outInfoMsg);

	// Get/add/remove spawnable actor (that is not yet present in the level)
	FGuid GetSActorGuidFromLevelSequence(const FString& spawnableName, bool& bOutSuccess, FString& outInfoMsg) const;
	FGuid AddSActorToLevelSequence(const FString& spawnableName, const FString& assetPath, bool& bOutSuccess, FString& outInfoMsg);
	void RemoveSActorFromLevelSequence(const FString& spawnableName, bool& bOutSuccess, FString& outInfoMsg);


	// Get/add/remove track
	void RemoveAllTracksFromLevelSequence(AActor* pActor, bool& bOutSuccess, FString& outInfoMsg);

	
	FFrameNumberRange RemoveKeyFrameFromAllTracks(int iSectionIdx, FFrameNumber iFrameNum, bool& bOutSuccess, FString& outInfoMsg);

	// Get interpolated value
	bool GetTransformAtTime(AActor* pActor, float fTime, FVector& pos, FRotator& rot) const;
	bool GetFloatValueAtTime(UMovieSceneTrack* pTrack, float fTime, float& OutValue) const;
	bool GetDoubleValueAtTime(UMovieSceneTrack* pTrack, float fTime, double& OutValue) const;
	bool GetStringValueAtTime(UMovieSceneTrack* pTrack, float fTime, std::string& OutValue) const;
	bool GetBoolValueAtTime(UMovieSceneTrack* pTrack, float fTime, bool& OutValue) const;


	// The following functions are based on the transform track that is supposedly always present in the actor sequence
	bool HasTransformKeyFrame(AActor* pActor, int iSectionIdx, FFrameNumber iFrameNum, bool& bOutSuccess, FString& outInfoMsg) const;

	// Get duration from the first transform section range
	float GetDuration(AActor* pActor) const;

	// Get start time from the first transform section range
	float GetStartTime(AActor* pActor) const;

	// Get end time from the first transform section range
	float GetEndTime(AActor* pActor) const;

	// Init MovieScene playback range from the first transform section range
	void AdjustMoviePlaybackRange(ACameraActor* pCameraActor);
	
	// Get MovieScene playback range
	float GetPlaybackEndTime() const;

	// Camera cut track

	bool HasCameraCutTrack() const;
	ACameraActor* GetCameraCutBoundCamera(FFrameTime PlaybackTime) const;
	UMovieSceneCameraCutTrack* AddCameraCutTrackToLevelSequence(bool bOverwriteExisting, bool& bOutSuccess, FString& outInfoMsg);
	void RemoveCameraCutTrackFromLevelSequence(bool& bOutSuccess, FString& outInfoMsg);
	void LinkCameraToCameraCutTrack(ACameraActor* pCameraActor, float fStartTime, float fEndTime, bool& bOutSuccess, FString& outInfoMsg);

	// Add clip
	
	// adds camera to the sequencer animation and creates a separate track for each given parameter
	bool AddNewClip(ACameraActor* pCameraActor, const TArray<FTrackInfo>& AnimParams, TArray<TStrongObjectPtr<UMovieSceneTrack> >& OutTracks);

	// Add/remove/move key-frame

	// add key-frame to the camera animation
	float AddKeyFrame(TArray<TStrongObjectPtr<UMovieSceneTrack> >& Tracks, float fTime, TArray<std::optional<KFValueType> >& Values);
	// remove key-frame from the camera animation
	void RemoveKeyFrame(TArray<TStrongObjectPtr<UMovieSceneTrack> >& Tracks, float fTime);
	// move all key-frames by the given delta time
	void ShiftClipKFs(TArray<TStrongObjectPtr<UMovieSceneTrack> >& Tracks, float fDeltaTime);
	// move key-frames in the given range by the given delta time
	void ShiftClipKFsInRange(TArray<TStrongObjectPtr<UMovieSceneTrack> >& Tracks, float fStartTime, float fEndTime, float fDeltaTime);
	//! Set the times of all key-frames in the given tracks
	void SetClipKFsTimes(TArray<TStrongObjectPtr<UMovieSceneTrack> >& Tracks, const std::vector<float>& vNewTimes);

	// Save level sequence asset to a file (provided for debug purposes only)
	void SaveLevelSequenceAsAsset(const FString& PackagePath); // PackagePath should be relative to /Content/, f.e. TEXT("/Game/MySavedSequence")


private:
	ULevelSequence& GetSeqChecked() { check(LevelSequence); return *LevelSequence; }
	const ULevelSequence& GetSeqChecked() const { check(LevelSequence); return *LevelSequence; }

	template<class TrackType>
	UMovieSceneTrack* CreateTrackForParameter(ACameraActor* pCameraActor,
		const TRange<FFrameNumber>& initialRange, const FName& sTrackName, bool& bRes, FString& outMsg);

	template<typename T>
	bool GetActorValueAtTime(AActor* pActor, float fTime, T& OutValue) const;
	template<typename T>
	bool GetTrackValueAtTime(UMovieSceneTrack* pTrack, float fTime, T& OutValue) const;

	// Get/add/remove section

	template<class TrackType>
	UMovieSceneSection* GetSectionFromActorInLevelSequence(AActor* pActor, int iSectionIdx, bool& bOutSuccess, FString& outInfoMsg) const;
	template<class TrackType>
	UMovieSceneSection* AddSectionToActorInLevelSequence(AActor* pActor, FFrameNumber FrameNum, EMovieSceneBlendType eBlendType, bool& bOutSuccess, FString& outInfoMsg);
	template<class TrackType>
	void RemoveSectionFromActorInLevelSequence(AActor* pActor, int iSectionIdx, bool& bOutSuccess, FString& outInfoMsg);

	// Add/remove key-frames


	template<typename T>
	FFrameNumberRange AddKeyFrameToActor(AActor* pActor, int iSectionIdx, FFrameNumber iFrameNum, const T& Value, int iKeyInterp, bool& bOutSuccess, FString& outInfoMsg);
	template<typename T>
	FFrameNumberRange AddKeyFrameToTrack(UMovieSceneTrack* pTrack, int iSectionIdx, FFrameNumber iFrameNum, const T& Value, int iKeyInterp, bool& bOutSuccess, FString& outInfoMsg);
	template<typename T>
	FFrameNumberRange RemoveKeyFrameFromActor(AActor* pActor, int iSectionIdx, FFrameNumber iFrameNum, bool& bOutSuccess, FString& outInfoMsg);
	template<typename T>
	FFrameNumberRange RemoveKeyFrameFromTrack(UMovieSceneTrack* pTrack, int iSectionIdx, FFrameNumber iFrameNum, bool& bOutSuccess, FString& outInfoMsg);

	// Get/add/remove track

	template<class TrackType>
	TrackType* GetTrackFromActorInLevelSequence(AActor* pActor, bool& bOutSuccess, FString& outInfoMsg) const;
	template<class TrackType>
	TrackType* AddTrackToActorInLevelSequence(AActor* pActor, bool bOverwriteExisting, bool& bOutSuccess, FString& outInfoMsg);
	template<class TrackType>
	void RemoveTrackFromActorInLevelSequence(AActor* pActor, bool& bOutSuccess, FString& outInfoMsg);


private:
	UPROPERTY(Transient)
	TObjectPtr<ULevelSequence> LevelSequence = nullptr;

	FString LevelSequencePath;

	/// Cache for GetPActorGuidFromLevelSequence: an actor's possessable binding ID is constant
	/// for a given level sequence, but resolving it means building a transient playback state and
	/// reverse-resolving every binding against the world, which is far too expensive to redo on
	/// every query (it is called several times per frame during playback).
	/// Invalidated whenever a binding is added/removed or the level sequence changes.
	mutable TMap<TWeakObjectPtr<AActor>, FGuid> PActorGuidCache;

	/// Drops the cached actor->binding GUIDs. Must be called whenever the set of bindings of the
	/// level sequence changes.
	void InvalidatePActorGuidCache() { PActorGuidCache.Reset(); }
};


#if WITH_TESTS

struct FMovieSceneChannel;

/// Test-only seam onto the helpers that live in ITwinSequencerHelper.cpp's anonymous
/// namespace. Deliberately exposes concrete instantiations rather than templates, so the
/// definitions can stay in that translation unit without explicit instantiation.
class FSequencerHelperTestAccess
{
public:
	static void SetKeyFrameTimesInChannel(FMovieSceneChannel* pChannel, const TArray<FFrameNumber>& NewFrameNumbers);

	// GetValueFromChannel<FMovieSceneStringChannel, ...>
	static bool GetStringFromChannel(UMovieSceneSection* pSection, int iChannelIdx, FFrameNumber iFrameNum, FString& OutValue);
	static bool GetStdStringFromChannel(UMovieSceneSection* pSection, int iChannelIdx, FFrameNumber iFrameNum, std::string& OutValue);

	// AddKeyFrameToChannel<...>
	static bool AddBoolKeyFrameToChannel(
		UMovieSceneSection* pSection, int iChannelIdx,
		FFrameNumber iFrameNum, bool Value, int iKeyInterp);
	static bool AddStdStringKeyFrameToChannel(
		UMovieSceneSection* pSection, int iChannelIdx,
		FFrameNumber iFrameNum, const std::string& Value, int iKeyInterp);

	// AddKeyFrameToSection<FVector>
	static bool AddVectorKeyFrameToSection(
		UMovieSceneSection* pSection,
		FFrameNumber iFrameNum, const FVector& Value, int iKeyInterp);

	// --- Track lifecycle (instantiated on UMovieScene3DTransformTrack / UMovieSceneDoubleTrack)

	static UMovieSceneTrack* GetTransformTrack(
		UITwinSequencerHelper const& Helper, AActor* pActor,
		bool& bOutSuccess, FString& outInfoMsg);
	static UMovieSceneTrack* AddTransformTrack(
		UITwinSequencerHelper& Helper, AActor* pActor,
		bool bOverwriteExisting, bool& bOutSuccess, FString& outInfoMsg);
	static void RemoveTransformTrack(
		UITwinSequencerHelper& Helper, AActor* pActor,
		bool& bOutSuccess, FString& outInfoMsg);
	static UMovieSceneTrack* AddDoubleTrack(
		UITwinSequencerHelper& Helper, AActor* pActor,
		bool bOverwriteExisting, bool& bOutSuccess, FString& outInfoMsg);

	// --- Section lifecycle

	static UMovieSceneSection* GetTransformSection(
		UITwinSequencerHelper const& Helper, AActor* pActor,
		int iSectionIdx, bool& bOutSuccess, FString& outInfoMsg);
	static UMovieSceneSection* AddTransformSection(
		UITwinSequencerHelper& Helper, AActor* pActor,
		FFrameNumber FrameNum, EMovieSceneBlendType eBlendType,
		bool& bOutSuccess, FString& outInfoMsg);
	static void RemoveTransformSection(
		UITwinSequencerHelper& Helper, AActor* pActor,
		int iSectionIdx, bool& bOutSuccess, FString& outInfoMsg);

	// --- Key-frames

	static FFrameNumberRange AddTransformKeyFrameToActor(
		UITwinSequencerHelper& Helper, AActor* pActor,
		int iSectionIdx, FFrameNumber iFrameNum, const FTransform& Value, int iKeyInterp,
		bool& bOutSuccess, FString& outInfoMsg);
	static FFrameNumberRange AddDoubleKeyFrameToTrack(
		UITwinSequencerHelper& Helper, UMovieSceneTrack* pTrack,
		int iSectionIdx, FFrameNumber iFrameNum, double Value, int iKeyInterp,
		bool& bOutSuccess, FString& outInfoMsg);
	static FFrameNumberRange RemoveTransformKeyFrameFromActor(
		UITwinSequencerHelper& Helper, AActor* pActor,
		int iSectionIdx, FFrameNumber iFrameNum, bool& bOutSuccess, FString& outInfoMsg);

	static UMovieSceneSection* AddDoubleSection(
		UITwinSequencerHelper& Helper, AActor* pActor,
		FFrameNumber FrameNum, EMovieSceneBlendType eBlendType,
		bool& bOutSuccess, FString& outInfoMsg);
	static FFrameNumberRange RemoveDoubleKeyFrameFromActor(
		UITwinSequencerHelper& Helper, AActor* pActor,
		int iSectionIdx, FFrameNumber iFrameNum, bool& bOutSuccess, FString& outInfoMsg);

	// --- Float / bool / string lifecycles.
	// Needed because GetFloatValueAtTime, GetBoolValueAtTime and GetStringValueAtTime are
	// public, but the only way to populate a track for them to read goes through the
	// private templates. Exposed as concrete instantiations, like everything above.

	static UMovieSceneTrack* AddFloatTrack(
		UITwinSequencerHelper& Helper, AActor* pActor,
		bool bOverwriteExisting, bool& bOutSuccess, FString& outInfoMsg);
	static UMovieSceneSection* AddFloatSection(
		UITwinSequencerHelper& Helper, AActor* pActor,
		FFrameNumber FrameNum, EMovieSceneBlendType eBlendType,
		bool& bOutSuccess, FString& outInfoMsg);
	static FFrameNumberRange AddFloatKeyFrameToTrack(
		UITwinSequencerHelper& Helper, UMovieSceneTrack* pTrack,
		int iSectionIdx, FFrameNumber iFrameNum, float Value, int iKeyInterp,
		bool& bOutSuccess, FString& outInfoMsg);

	static UMovieSceneTrack* AddBoolTrack(
		UITwinSequencerHelper& Helper, AActor* pActor,
		bool bOverwriteExisting, bool& bOutSuccess, FString& outInfoMsg);
	static UMovieSceneSection* AddBoolSection(
		UITwinSequencerHelper& Helper, AActor* pActor,
		FFrameNumber FrameNum, EMovieSceneBlendType eBlendType,
		bool& bOutSuccess, FString& outInfoMsg);
	static FFrameNumberRange AddBoolKeyFrameToTrack(
		UITwinSequencerHelper& Helper, UMovieSceneTrack* pTrack,
		int iSectionIdx, FFrameNumber iFrameNum, bool Value, int iKeyInterp,
		bool& bOutSuccess, FString& outInfoMsg);

	static UMovieSceneTrack* AddStringTrack(
		UITwinSequencerHelper& Helper, AActor* pActor,
		bool bOverwriteExisting, bool& bOutSuccess, FString& outInfoMsg);
	static UMovieSceneSection* AddStringSection(
		UITwinSequencerHelper& Helper, AActor* pActor,
		FFrameNumber FrameNum, EMovieSceneBlendType eBlendType,
		bool& bOutSuccess, FString& outInfoMsg);
	static FFrameNumberRange AddStringKeyFrameToTrack(
		UITwinSequencerHelper& Helper, UMovieSceneTrack* pTrack,
		int iSectionIdx, FFrameNumber iFrameNum, const std::string& Value, int iKeyInterp,
		bool& bOutSuccess, FString& outInfoMsg);
};

#endif // WITH_TESTS
