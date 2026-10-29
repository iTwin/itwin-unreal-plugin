/*--------------------------------------------------------------------------------------+
|
|     $Source: TestableGoogle3DTilesController.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CoreMinimal.h>
#include <ITwinGoogle3DTilesController.h>

#include <array>
#include <optional>

#include "TestableGoogle3DTilesController.generated.h"

/// Concrete controller for the automated tests.
/// AITwinGoogle3DTilesController declares Update3DMapUI / GetDPIScale / DisplayGoogleTiles as
/// PURE_VIRTUAL, so a derived class is required to exercise it; this one also records every UI
/// callback so that the tests can assert on them.
///
/// NOTE: deliberately NOT wrapped in #if WITH_TESTS - UHT generates its reflection code
/// unconditionally, so guarding the UCLASS causes link errors. The tests themselves are
/// guarded in the .cpp instead.
UCLASS(NotBlueprintable, Transient, Hidden)
class ATestableGoogle3DTilesController : public AITwinGoogle3DTilesController
{
	GENERATED_BODY()

public:
	// --- Recorded UI calls ---
	int32 Update3DMapUICount = 0;
	bool bLastUIVisible = false;
	float LastUIQuality = -1.f;

	mutable int32 GetGeolocationFromUICount = 0;
	int32 SetGeolocationInUICount = 0;
	bool bLastGeolocEditionEnabled = false;

	int32 On3DMapLocationSetCount = 0;
	std::array<double, 3> LastLocationSet = { 0., 0., 0. };

	// --- Values fed back to the controller ---
	std::optional<bool> DisplayGoogleTilesValue = std::make_optional(true);
	std::optional<std::array<double, 3>> GeolocationFromUIValue;

	virtual std::optional<bool> DisplayGoogleTiles() const override
	{
		return DisplayGoogleTilesValue;
	}

	/// Public forwarder: GetQualityFromTileset is protected on the base class.
	std::optional<float> GetQualityForTest() const { return GetQualityFromTileset(); }

	/// Public forwarder: OnSceneLoaded is protected on the base class.
	void OnSceneLoadedForTest(bool bSuccess) { OnSceneLoaded(bSuccess); }

private:
	virtual void Update3DMapUI(bool IsVisible, float Quality) override
	{
		++Update3DMapUICount;
		bLastUIVisible = IsVisible;
		LastUIQuality = Quality;
	}

	virtual void On3DMapLocationSet(double Latitude, double Longitude, double Elevation) override
	{
		++On3DMapLocationSetCount;
		LastLocationSet = { Latitude, Longitude, Elevation };
	}

	virtual std::optional<std::array<double, 3>> GetGeolocationFromUI() const override
	{
		++GetGeolocationFromUICount;
		return GeolocationFromUIValue;
	}

	virtual void SetGeolocationInUI(double, double, double, bool bEnable) override
	{
		++SetGeolocationInUICount;
		bLastGeolocEditionEnabled = bEnable;
	}

	/// The real implementation queries the viewport; tests run headless.
	virtual float GetDPIScale() const override { return 1.f; }
};
