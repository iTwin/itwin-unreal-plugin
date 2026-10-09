/*--------------------------------------------------------------------------------------+
|
|     $Source: IModelTestHelperImpl.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS

#include <Tests/IModelTestHelperImpl.h>

#include <ITwinTilesetAccess.h>
#include <IncludeCesium3DTileset.h>

#include <CesiumCameraManager.h>

#include <Tests/GenericHelpers.h>
#include <Tests/IModelHeadlessTestsHelper.h>
#include <Tests/ITwinFileBasedMockServer.h>


FIModelTestHelperImpl::FIOAsyncCallback::FIOAsyncCallback(FIModelTestHelperImpl& InOwner)
	: Owner(InOwner)
{}

bool FIModelTestHelperImpl::FIOAsyncCallback::IsDone() const
{
	Owner.SetupCesiumCameraManager();
	Owner.DetectFilledSceneMapping();
	Owner.DetectCustomEvents();
	return FITwinIOAsyncCallback::IsDone();
}


FIModelTestHelperImpl::~FIModelTestHelperImpl()
{
#if WITH_EDITOR
	// Cleanup cannot be called here as it calls virtual functions (BindEvents/UnBindEvents) that may have
	// been overridden in derived classes.
	BE_ASSERT(!IModelLoadingResult.MockServer && !TestCallback
		&& !IModelLoadingResult.IModel
		&& !bHasPushedAllowTickInEditor,
		"Cleanup() should have been called before destroying this object.");
#endif
}

bool FIModelTestHelperImpl::Init(FIModelTestOptions const& Options /*= {}*/)
{
#if WITH_EDITOR

	CurrentOptions = Options;
	// Override some options:
	// Disable rendering for headless tests.
	CurrentOptions.bEnableRendering = false;
	// For now, disable 4D queries here (same comment as in IModelRenderTest about the 4D schedule).
	// However, it is worth testing the error case (see regular expression used in
	// FPaginatedIModelRowsQueries::QueryNextPage). But it seems to induce long delays in the test, which we
	// should investigate later - TODO_JDE.
	// See regular expression used in FPaginatedIModelRowsQueries::QueryNextPage.
	CurrentOptions.bDisableSynchro4DAutoLoad = true;


	// We need actors to tick in Editor (IModel, Cesium tileset...), otherwise the test will time out because
	// the tileset will not be loaded.
	pushAllowTickInEditor();
	bHasPushedAllowTickInEditor = true;

	AsyncCallback = std::make_shared<FIOAsyncCallback>(*this);

	FITwinExportInfo ExportInfo;
	FString SceneId;
	std::filesystem::path RelativeCacheFolder;
	FillTestInfo(ExportInfo, SceneId, RelativeCacheFolder, CurrentOptions);

	UWorld* const World = FITwinAPITestHelperBase::GetTestWorld();

	IModelLoadingResult = FIModelTestHelper::InitiateIModelLoading(World,
		ExportInfo,
		RelativeCacheFolder,
		SceneId,
		CurrentOptions);

	// Beware the same iTwin can be used in multiple tests, so we need to check if the scene should be loaded
	// for this test or not.
	bShouldLoadScene = IModelLoadingResult.bShouldLoadScene;

	if (CurrentOptions.bDiscoverExport && IModelLoadingResult.IModel)
	{
		// In automatic mode, the iTwin ID should be left empty (to check that the iTwin ID is correctly
		// retrieved from the server).
		ensure(IModelLoadingResult.IModel->ITwinId.IsEmpty());
	}

	return (bool)IModelLoadingResult.DecoHelper;

#else
	return false;
#endif
}

FString FIModelTestHelperImpl::GetMockServerUrl() const
{
#if WITH_EDITOR
	return IModelLoadingResult.MockServer ? IModelLoadingResult.MockServer->GetUrl() : TEXT("");
#else
	return {};
#endif
}

void FIModelTestHelperImpl::Cleanup()
{
	UnBindEvents();

#if WITH_EDITOR
	IModelLoadingResult.CleanupGuard.reset();

	TestCallback = {};

	IModelLoadingResult.IModel = {};

	// Stop the mock server now that we are done with the test, to avoid conflicts with other tests
	// that may be running in parallel (see FITwinMockServerBase::HasRunningInstance).
	IModelLoadingResult.MockServer.reset();

	if (bHasPushedAllowTickInEditor)
	{
		popAllowTickInEditor();
		bHasPushedAllowTickInEditor = false;
	}
#endif
}

int32 FIModelTestHelperImpl::ExpectedLoadEvents() const
{
	return 2 /*iModel loaded + scene mapping filled*/
		+ (bShouldLoadScene ? 1 : 0)
		+ (CurrentOptions.bAutoLoadSavedViews ? 1 : 0);
}

void FIModelTestHelperImpl::OnLoadEventReceived()
{
	LoadEventsReceived++;
	if (LoadEventsReceived == ExpectedLoadEvents())
	{
		ExecuteTestCallback();

		// We may have to wait for a remaining request (element properties)...
		CleanupIfDone();
	}
}

void FIModelTestHelperImpl::ExecuteTestCallback()
{
	if (TestCallback)
	{
		TestCallback();
		// Reset the callback to avoid calling it again. It also prevents GC issues if the callback captures
		// a UObject through a TStrongObjectPtr (see UDigitalTwinManagerTestHelper).
		TestCallback = {};
	}
}

void FIModelTestHelperImpl::CleanupIfDone()
{
	if (!AsyncCallback || AsyncCallback->IsDone())
	{
		Cleanup();
	}
}

TUniquePtr<FITwinTilesetAccess> FIModelTestHelperImpl::MakeTilesetAccess() const
{
	// By default, work with the iModel's tileset access, but this can be overridden in derived classes.
	if (AITwinIModel* IModel = GetIModel())
	{
		return IModel->MakeTilesetAccess();
	}
	return nullptr;
}

void FIModelTestHelperImpl::SetupCesiumCameraManager()
{
	// We need to add a (dummy) camera to the CesiumCameraManager, otherwise the tileset will not be loaded.
	if (bHasSetupCesiumCameraManager)
		return;

	TUniquePtr<FITwinTilesetAccess> Access = MakeTilesetAccess();
	if (Access)
	{
		// Refresh the tileset load status, which is used in the test to check if the tileset
		// has been loaded successfully.
		auto* Tileset = Access->GetMutableTileset();
		if (Tileset)
		{
			ACesiumCameraManager* CameraManager = Tileset->ResolveCameraManager();
			if (!CameraManager)
			{
				BE_LOGW("ITwinAdvViz", "CesiumCameraManager not found, cannot add dummy camera.");
				return;
			}
			CameraManager->UsePlayerCameras = false;
			FCesiumCamera auxCamera(
				FVector2D(1080.0, 768.0),
				FVector(0, 0, 7240000.0),
				FRotator(-90.0, 0.0, 0.0),
				60.0f);
			CameraManager->AdditionalCameras.Add(auxCamera);
			bHasSetupCesiumCameraManager = true;
		}
	}
}

void FIModelTestHelperImpl::DetectFilledSceneMapping()
{
	if (bHasFilledSceneMapping)
		return;
	if (!bHasSetupCesiumCameraManager)
		return; // Cesium tileset cannot be loaded without a camera...

	if (AITwinIModel* IModel = GetIModel())
	{
		// Check if the iModel contains a specific element ID.
		if (IModel->HasElementWithID(TEXT("0x5d")))
		{
			bHasFilledSceneMapping = true;
			OnLoadEventReceived();
		}
	}
}


void FIModelTestHelperImpl::OnIModelLoaded(bool bSuccess, FString StringId)
{
	OnLoadEventReceived();
}

#endif // WITH_TESTS
