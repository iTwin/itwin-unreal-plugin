/*--------------------------------------------------------------------------------------+
|
|     $Source: Google3DTilesControllerTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS

#include <Tests/ITwinAutomationTestBaseNoLogs.h>
#include <Tests/TestWorldHelpers.h>
#include <Tests/TestableGoogle3DTilesController.h>

#include <ITwinGoogle3DTileset.h>

#include <Engine/World.h>
#include <EngineUtils.h>
#include <Misc/AutomationTest.h>

namespace
{
	using FScopedTestWorld = ITwinTests::FScopedTestWorld;

	int32 CountGoogleTilesets(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<AITwinGoogle3DTileset> It(World); It; ++It)
		{
			if (IsValid(*It))
				++Count;
		}
		return Count;
	}

	/// Spawns the controller and registers it as the singleton instance.
	/// Registering matters: GetInstance() would otherwise spawn a *base* controller through
	/// TWorldSingleton, and the base class asserts in its PURE_VIRTUAL hooks.
	ATestableGoogle3DTilesController* SpawnController(FScopedTestWorld const& TestWorld)
	{
		auto* Controller = TestWorld.SpawnActorOfClass<ATestableGoogle3DTilesController>();
		AITwinGoogle3DTilesController::SetInstance(Controller);
		return Controller;
	}

	/// The singleton is a raw static pointer: always clear it, so that a test cannot leave a
	/// dangling instance behind for the next one (the actors die with the scoped world).
	struct [[nodiscard]] FScopedControllerInstance
	{
		~FScopedControllerInstance() { AITwinGoogle3DTilesController::SetInstance(nullptr); }
	};
}


//-----------------------------------------------------------------------------
// Singleton accessors.
//-----------------------------------------------------------------------------

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FGoogle3DTilesControllerInstanceTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.Google3DTilesController.Instance", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGoogle3DTilesControllerInstanceTest::RunTest(FString const& /*Parameters*/)
{
	FScopedControllerInstance const ScopedInstance;
	FScopedTestWorld TestWorld;

	auto* Controller = SpawnController(TestWorld);
	if (!TestNotNull(TEXT("Controller spawned"), Controller))
		return false;

	TestEqual(TEXT("GetInstance returns the registered controller"),
		AITwinGoogle3DTilesController::GetInstance(TestWorld.Get()),
		static_cast<AITwinGoogle3DTilesController*>(Controller));

	AITwinGoogle3DTilesController::SetInstance(nullptr);
	// Note: we do not call GetInstance() again here - it would spawn a base controller,
	// whose PURE_VIRTUAL hooks assert.

	return true;
}


//-----------------------------------------------------------------------------
// Toggle3DMapVisibility / Toggle3DMap / HasValidGoogleTileset.
//-----------------------------------------------------------------------------

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FGoogle3DTilesControllerToggleTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.Google3DTilesController.Toggle", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGoogle3DTilesControllerToggleTest::RunTest(FString const& /*Parameters*/)
{
	FScopedControllerInstance const ScopedInstance;
	FScopedTestWorld TestWorld;

	auto* Controller = SpawnController(TestWorld);
	if (!TestNotNull(TEXT("Controller spawned"), Controller))
		return false;

	TestFalse(TEXT("No tileset in a brand new world"), Controller->HasValidGoogleTileset());
	TestEqual(TEXT("No tileset actor initially"), CountGoogleTilesets(TestWorld.Get()), 0);

	// --- Enabling with no existing tileset creates exactly one ---
	Controller->Toggle3DMapVisibility(true);
	if (!TestEqual(TEXT("One tileset created"), CountGoogleTilesets(TestWorld.Get()), 1))
		return false;
	TestTrue(TEXT("HasValidGoogleTileset after creation"), Controller->HasValidGoogleTileset());
	TestTrue(TEXT("Geolocation UI filled on creation"), Controller->SetGeolocationInUICount > 0);

	// --- Enabling again reuses the existing tileset ---
	Controller->Toggle3DMapVisibility(true);
	TestEqual(TEXT("Still a single tileset"), CountGoogleTilesets(TestWorld.Get()), 1);

	// --- Disabling only hides, it must not destroy ---
	Controller->Toggle3DMapVisibility(false);
	TestEqual(TEXT("Tileset is not destroyed when hidden"), CountGoogleTilesets(TestWorld.Get()), 1);
	for (TActorIterator<AITwinGoogle3DTileset> It(TestWorld.Get()); It; ++It)
	{
		TestTrue(TEXT("Tileset is hidden in game"), It->IsHidden());
	}

	// --- Toggle3DMap(false) destroys them ---
	Controller->Toggle3DMap(false);
	TestEqual(TEXT("Tilesets destroyed by Toggle3DMap(false)"),
		CountGoogleTilesets(TestWorld.Get()), 0);
	TestFalse(TEXT("No valid tileset left"), Controller->HasValidGoogleTileset());

	// --- Toggle3DMap(true) re-creates one (it delegates to Toggle3DMapVisibility) ---
	Controller->Toggle3DMap(true);
	TestEqual(TEXT("Toggle3DMap(true) re-creates a tileset"),
		CountGoogleTilesets(TestWorld.Get()), 1);

	return true;
}


//-----------------------------------------------------------------------------
// Quality.
//-----------------------------------------------------------------------------

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FGoogle3DTilesControllerQualityTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.Google3DTilesController.Quality", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGoogle3DTilesControllerQualityTest::RunTest(FString const& /*Parameters*/)
{
	FScopedControllerInstance const ScopedInstance;
	FScopedTestWorld TestWorld;

	auto* Controller = SpawnController(TestWorld);
	if (!TestNotNull(TEXT("Controller spawned"), Controller))
		return false;

	// Without any tileset there is nothing to read the quality from.
	TestFalse(TEXT("No quality without a tileset"), Controller->GetQualityForTest().has_value());

	Controller->Toggle3DMapVisibility(true);
	if (!TestEqual(TEXT("Tileset created"), CountGoogleTilesets(TestWorld.Get()), 1))
		return false;

	Controller->Set3DMapQuality(0.25f);
	auto Quality = Controller->GetQualityForTest();
	if (!TestTrue(TEXT("Quality readable from the tileset"), Quality.has_value()))
		return false;
	TestEqual(TEXT("Quality round-trip (0.25)"), *Quality, 0.25f, 1e-3f);

	Controller->Set3DMapQuality(0.75f);
	Quality = Controller->GetQualityForTest();
	if (!TestTrue(TEXT("Quality still readable"), Quality.has_value()))
		return false;
	TestEqual(TEXT("Quality round-trip (0.75)"), *Quality, 0.75f, 1e-3f);

	return true;
}


//-----------------------------------------------------------------------------
// Set3DMapLocation.
//-----------------------------------------------------------------------------

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FGoogle3DTilesControllerLocationTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.Google3DTilesController.Location", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGoogle3DTilesControllerLocationTest::RunTest(FString const& /*Parameters*/)
{
	FScopedControllerInstance const ScopedInstance;
	FScopedTestWorld TestWorld;

	auto* Controller = SpawnController(TestWorld);
	if (!TestNotNull(TEXT("Controller spawned"), Controller))
		return false;

	Controller->Toggle3DMapVisibility(true);

	// Eiffel Tower.
	Controller->Set3DMapLocation(48.8584, 2.2945, 79.07);

	TestEqual(TEXT("On3DMapLocationSet called once"), Controller->On3DMapLocationSetCount, 1);
	TestEqual(TEXT("Latitude forwarded"), Controller->LastLocationSet[0], 48.8584, 1e-9);
	TestEqual(TEXT("Longitude forwarded"), Controller->LastLocationSet[1], 2.2945, 1e-9);
	TestEqual(TEXT("Elevation forwarded"), Controller->LastLocationSet[2], 79.07, 1e-9);

	// The hook is notified even when there is no tileset at all.
	Controller->Toggle3DMap(false);
	Controller->Set3DMapLocation(0., 0., 0.);
	TestEqual(TEXT("On3DMapLocationSet called again without tileset"),
		Controller->On3DMapLocationSetCount, 2);

	return true;
}


//-----------------------------------------------------------------------------
// OnModelLoaded, with no AITwinDecorationHelper in the world.
//-----------------------------------------------------------------------------

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FGoogle3DTilesControllerOnModelLoadedTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.Google3DTilesController.OnModelLoaded", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGoogle3DTilesControllerOnModelLoadedTest::RunTest(FString const& /*Parameters*/)
{
	FScopedControllerInstance const ScopedInstance;
	FScopedTestWorld TestWorld;

	auto* Controller = SpawnController(TestWorld);
	if (!TestNotNull(TEXT("Controller spawned"), Controller))
		return false;

	TestEqual(TEXT("No UI update before OnModelLoaded"), Controller->Update3DMapUICount, 0);

	// No persistence manager in this world => the "default" branch: always create the map,
	// push the quality to the UI, then refresh the geolocation fields.
	Controller->OnModelLoaded(nullptr);

	TestEqual(TEXT("Tileset created by OnModelLoaded"), CountGoogleTilesets(TestWorld.Get()), 1);
	if (!TestTrue(TEXT("UI was updated"), Controller->Update3DMapUICount >= 1))
		return false;
	TestTrue(TEXT("UI reports the map as visible"), Controller->bLastUIVisible);
	TestTrue(TEXT("UI received a plausible quality"),
		Controller->LastUIQuality > 0.f && Controller->LastUIQuality <= 1.f);
	TestTrue(TEXT("Geolocation UI updated"), Controller->SetGeolocationInUICount > 0);

	// With a null iModel there is nothing geo-located, so geo-reference edition stays enabled.
	TestTrue(TEXT("Geolocation edition enabled without a geo-located iModel"),
		Controller->bLastGeolocEditionEnabled);

	// Calling it again must not create a second tileset.
	int32 const UICountAfterFirst = Controller->Update3DMapUICount;
	Controller->OnModelLoaded(nullptr);
	TestEqual(TEXT("Still a single tileset after a second OnModelLoaded"),
		CountGoogleTilesets(TestWorld.Get()), 1);
	TestTrue(TEXT("UI refreshed again"), Controller->Update3DMapUICount >= UICountAfterFirst);

	return true;
}


//-----------------------------------------------------------------------------
// Save() must be a safe no-op when no persistence manager exists.
//-----------------------------------------------------------------------------

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FGoogle3DTilesControllerSaveTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.Google3DTilesController.SaveWithoutPersistence", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGoogle3DTilesControllerSaveTest::RunTest(FString const& /*Parameters*/)
{
	FScopedControllerInstance const ScopedInstance;
	FScopedTestWorld TestWorld;

	auto* Controller = SpawnController(TestWorld);
	if (!TestNotNull(TEXT("Controller spawned"), Controller))
		return false;

	Controller->Toggle3DMapVisibility(true);
	Controller->GeolocationFromUIValue = std::array<double, 3>{ 1., 2., 3. };

	// There is no AITwinDecorationHelper in this world: Save() bails out immediately and must
	// not even query the UI.
	Controller->Save();

	TestEqual(TEXT("UI geolocation not queried without persistence"),
		Controller->GetGeolocationFromUICount, 0);
	TestTrue(TEXT("Tileset untouched by Save"), Controller->HasValidGoogleTileset());

	return true;
}

#endif // WITH_TESTS
