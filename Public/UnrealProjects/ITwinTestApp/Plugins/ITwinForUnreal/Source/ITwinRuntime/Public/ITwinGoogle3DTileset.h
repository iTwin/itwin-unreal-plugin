/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinGoogle3DTileset.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#pragma once

#include <IncludeCesium3DTileset.h>

#include <Containers/Array.h>
#include <Templates/PimplPtr.h>
#include <functional>
#include <optional>

#include <ITwinGoogle3DTileset.generated.h>

class FITwinTilesetAccess;
namespace AdvViz::SDK
{
	struct ITwinGoogleCuratedContentAccess;
	struct ITwinGeolocationInfo;
}

UCLASS()
class ITWINRUNTIME_API AITwinGoogle3DTileset : public ACesium3DTileset
{
	GENERATED_BODY()
public:
	UPROPERTY(Category = "iTwin",
		EditAnywhere,
		Transient)
	FString GoogleKey;

	//! Fill the default Google key used to access the Google tileset API.
	//! This key is private to the user account.
	static void SetDefaultKey(FString const& DefaultGoogleKey, UWorld* World = nullptr);

	//! Initiates the access token and url to use to access Google 3D tileset.
	static void SetContentAccess(AdvViz::SDK::ITwinGoogleCuratedContentAccess const& ContentAccess, UWorld* World);

	//! Fill the key used to access the Google elevation API.
	static void SetElevationtKey(std::string const& GoogleElevationKey);
	//! Start a request to retrieve an evaluation of the elevation at given location.
	//! Returns true if the elevation key is available, and thus the request could be started.
	static bool RequestElevationtAtGeolocation(AdvViz::SDK::ITwinGeolocationInfo const& GeolocationInfo,
		std::function<void(std::optional<double> const& elevationOpt)>&& Callback);

	static AITwinGoogle3DTileset* MakeInstance(UWorld& World, bool bGeneratePhysicsMeshes = false);

	AITwinGoogle3DTileset();
	~AITwinGoogle3DTileset();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetActorHiddenInGame(bool bNewHidden) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UFUNCTION()
	void OnSceneLoaded(bool bSuccess);

	void SetTilesetQuality(float Value);
	void SetGeoLocation(std::array<double, 3> const& latLongHeight);

	//! Forbid editing geo-location, when the loaded iTwin data is geo-located.
	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void LockGeoLocation(bool bLockEdition);

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	bool IsGeoLocationLocked() const;

	class UITwinClippingCustomPrimitiveDataHelper* GetClippingHelper() const;
	bool MakeClippingHelper();

	//! Creates a helper to perform some requests/modifications on the tileset.
	TUniquePtr<FITwinTilesetAccess> MakeTilesetAccess();

private:
	class FImpl;
	TPimplPtr<FImpl> Impl;

	class FTilesetAccess;

	static std::string ElevationtKey;
};


namespace ITwin
{
	/// Return true if the given tileset uses an URL corresponding to Google API. It includes both instances
	/// of class AITwinGoogle3DTileset (by construction), but also any tileset created manually or coming
	/// from the time when AITwinGoogle3DTileset did not exist yet.
	ITWINRUNTIME_API bool IsGoogle3DTileset(const ACesium3DTileset* tileset);

	/// Gather all Google3D tilesets in given world, ie. tilesets matching the predicate #IsGoogle3DTileset.
	ITWINRUNTIME_API void GatherGoogle3DTilesets(const UObject* WorldContextObject,
		TArray<ACesium3DTileset*>& Out3DMapTilesets);
}
