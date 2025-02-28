/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinGoogle3DTileset.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#pragma once

// Include SanitizedPlatformHeaders.h before ITwinCesium3DTileset.h, or the packaging fails
// (error min/max in CesiumGltf/AccessorUtility.h...)
#include <ITwinRuntime/Private/Compil/SanitizedPlatformHeaders.h>

#include <ITwinCesium3DTileset.h>

#include <Containers/Array.h>
#include <Templates/PimplPtr.h>

#include <ITwinGoogle3DTileset.generated.h>


class UITwinCesiumPolygonRasterOverlay;


UCLASS()
class ITWINRUNTIME_API AITwinGoogle3DTileset : public AITwinCesium3DTileset
{
	GENERATED_BODY()
public:
	UPROPERTY(Category = "iTwin",
		EditAnywhere,
		Transient)
	FString GoogleKey;

	//! Fill the default Google key used to access the Google tileset API.
	//! This key is private to the user account.
	static void SetDefaultKey(FString const& DefaultGoogleKey);

	static AITwinGoogle3DTileset* MakeInstance(UWorld& World);

	AITwinGoogle3DTileset();

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

	void InitPolygonRasterOverlay();
	UITwinCesiumPolygonRasterOverlay* GetPolygonRasterOverlay() const;

private:
	class FImpl;
	TPimplPtr<FImpl> Impl;
};


namespace ITwin
{
	/// Return true if the given tileset uses an URL corresponding to Google API. It includes both instances
	/// of class AITwinGoogle3DTileset (by construction), but also any tileset created manually or coming
	/// from the time when AITwinGoogle3DTileset did not exist yet.
	ITWINRUNTIME_API bool IsGoogle3DTileset(const AITwinCesium3DTileset* tileset);

	/// Gather all Google3D tilesets in given world, ie. tilesets matching the predicate #IsGoogle3DTileset.
	ITWINRUNTIME_API void GatherGoogle3DTilesets(const UObject* WorldContextObject,
		TArray<AITwinCesium3DTileset*>& Out3DMapTilesets);
}
