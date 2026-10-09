/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinGoogle3DTilesController.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <GameFramework/Actor.h>
#include <array>
#include <optional>
#include <ITwinGoogle3DTilesController.generated.h>


class AITwinGoogle3DTileset;
class AITwinIModel;

UCLASS()
class ITWINRUNTIME_API AITwinGoogle3DTilesController : public AActor
{
	GENERATED_BODY()
public:
	static AITwinGoogle3DTilesController* GetInstance(UWorld* World);
	static void SetInstance(AITwinGoogle3DTilesController* InInstance);

	AITwinGoogle3DTilesController();

	UFUNCTION()
	void Toggle3DMap(bool IsToggled); // Add/Remove Google tiles from the scene
	UFUNCTION()
	void Toggle3DMapVisibility(bool IsToggled); // Show/Hide Google tiles
	UFUNCTION()
	void Set3DMapQuality(float Quality);
	UFUNCTION()
	void Set3DMapLocation(double Latitude, double Longitude, double Elevation);

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void OnModelLoaded(AITwinIModel const* IModel);

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void Save();

	void UpdateAfterGeoLocSet(bool fromscene);

	bool HasValidGoogleTileset() const;

	virtual std::optional<bool> DisplayGoogleTiles() const PURE_VIRTUAL(AITwinGoogle3DTilesController::DisplayGoogleTiles, return std::nullopt; );

	// Update UI quality on global quality change.
	virtual void UpdateQualityUIFromTileset() { }

#if WITH_TESTS
	//! Used in automated tests, to enable mocking of google elevation service.
	//! \param ServerUrl The url of the mock server to use (eg "http://localhost:1234").
	void SetElevationTestMode(FString const& ServerUrl, const FString& InGoogleElevationKey);
#endif


protected:
	virtual void BeginPlay() override;
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UFUNCTION()
	void OnSceneLoaded(bool success);

	std::optional<float> GetQualityFromTileset() const;


private:
	AITwinGoogle3DTileset* AddNew3DMap();
	void UpdateFromSceneSettings();
	virtual void Update3DMapUI(bool IsVisible, float Quality) PURE_VIRTUAL(AITwinGoogle3DTilesController::Update3DMapUI);

	void UpdateGeolocUIFromTileset(AITwinIModel const* IModel);
	virtual void On3DMapLocationSet(double Latitude, double Longitude, double Elevation);
	virtual void OnGoogleTilesLoaded() {}

	virtual std::optional<std::array<double, 3>> GetGeolocationFromUI() const { return std::nullopt; }
	virtual void SetGeolocationInUI(double latitude, double longitude, double elevation, bool bEnable) {
		// Default implementation does nothing. Derived classes can override this to update the UI with the
		// new geolocation.
	}

	virtual float GetDPIScale() const PURE_VIRTUAL(AITwinGoogle3DTilesController::GetDPIScale, return 1.0f; );

#if WITH_EDITORONLY_DATA
	//! Google Elevation API key to use for elevation requests.
	//! Made available in Editor, to let the user fill it before starting PIE.
	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	FString GoogleElevationKey;
#endif

	class FImpl;
	TPimplPtr<FImpl> Impl;

	static AITwinGoogle3DTilesController* Instance;
};
