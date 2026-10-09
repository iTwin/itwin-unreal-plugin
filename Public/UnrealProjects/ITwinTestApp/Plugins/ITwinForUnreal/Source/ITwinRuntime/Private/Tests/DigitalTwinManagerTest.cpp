/*--------------------------------------------------------------------------------------+
|
|     $Source: DigitalTwinManagerTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS

#include <ITwinDigitalTwinManager.h>

#include <ITwinGoogle3DTilesController.h>
#include <ITwinRealityData.h>
#include <ITwinTilesetAccess.h>
#include <IncludeCesium3DTileset.h>
#include <Decoration/ITwinDecorationHelper.h>

#include <Tests/DigitalTwinManagerTestHelper.h>
#include <Tests/ITwinAutomationTestBaseNoLogs.h>
#include <Tests/IModelTestHelperImpl.h>
#include <Tests/ITwinFileBasedMockServer.h>

#include <Misc/LowLevelTestAdapter.h>




class UDigitalTwinManagerTestHelper::FImpl : public FIModelTestHelperImpl
{
public:
	FImpl(UDigitalTwinManagerTestHelper& InOwner) : Owner(InOwner) {}
	~FImpl() { Cleanup(); }

	virtual void FillTestInfo(FITwinExportInfo& OutExportInfo,
		FString& OutSceneId,
		std::filesystem::path& OutRelativeCacheFolder,
		FInitOptions const& InOptions) const override;

	virtual void BindEvents() override;
	virtual void UnBindEvents() override;
	virtual int32 ExpectedLoadEvents() const override;
	virtual void DetectCustomEvents() override;

	virtual TUniquePtr<FITwinTilesetAccess> MakeTilesetAccess() const override;

	FString GetITwinID() const { return TEXT("36ff2262-0867-4da9-8a20-ab9b343b23e8"); }
	FString GetRealityDataToLoad() const { return TEXT("d40db151-9fe4-4935-b615-d7cef5cb08e1"); }
	FString GetIModelToLoad() const { return TEXT("0746e910-e8e9-4ded-a896-3a00aae1c910"); }

	bool IsSceneLoadingEnabled() const { return bEnableSceneLoading; }
	//! In Scene Loading test, we will also check the order of atmosphere vs geo-location events.
	void EnableSceneLoading(bool bEnable) { bEnableSceneLoading = bEnable; }

	bool HasLoadedAtmosphere() const { return bHasLoadedAtmosphere; }
	void SetHasLoadedAtmosphere(bool bInHasLoadedAtmosphere);

	bool HasWrongLoadOrderDetected() const { return bWrongLoadOrderDetected; }
	void SetWrongLoadOrderDetected(bool bInWrongLoadOrderDetected) { bWrongLoadOrderDetected = bInWrongLoadOrderDetected; }

private:
	UDigitalTwinManagerTestHelper& Owner;
	bool bEnableSceneLoading = false;
	bool bHasFinishedAttachedRealityDataRequest = false;
	bool bHasLoadedAtmosphere = false;
	bool bWrongLoadOrderDetected = false;
};



void UDigitalTwinManagerTestHelper::FImpl::FillTestInfo(FITwinExportInfo& OutExportInfo,
	FString& OutSceneId,
	std::filesystem::path& OutRelativeCacheFolder,
	FInitOptions const& InOptions) const
{
	// Use the same iModel as in IModelRenderTest.
	OutExportInfo.iTwinId = GetITwinID();

	// Use the default scene of this iTwin.
	OutSceneId = InOptions.bDiscoverDefaultScene ? TEXT("") : TEXT("7a263dce-b6b5-4bd1-9f76-3b85b44e2788");

	OutRelativeCacheFolder = "FunctionalTests/ITwinManager";
}

void UDigitalTwinManagerTestHelper::FImpl::BindEvents()
{
	if (!Owner.ITwinManager)
		return;
	Owner.ITwinManager->ITwinInfoReceivedEvent.AddDynamic(&Owner, &UDigitalTwinManagerTestHelper::OnITwinInfoRetrieved);
	Owner.ITwinManager->ComponentLoadedEvent.AddDynamic(&Owner, &UDigitalTwinManagerTestHelper::OnLoadComponent);
	Owner.ITwinManager->ComponentWillBeRemovedEvent.AddDynamic(&Owner,
		&UDigitalTwinManagerTestHelper::OnWillRemoveComponent);
	Owner.ITwinManager->ComponentRemovedEvent.AddDynamic(&Owner, &UDigitalTwinManagerTestHelper::OnRemoveComponent);
	Owner.ITwinManager->GeoLocationSetEvent.AddDynamic(&Owner, &UDigitalTwinManagerTestHelper::OnGeoLocationSet);
	Owner.ITwinManager->GeoLocationGapInMetersEvent.AddDynamic(&Owner, &UDigitalTwinManagerTestHelper::OnGeoLocationGapInMeters);

	if (AITwinDecorationHelper* DecoHelper = GetDecorationHelper())
	{
		DecoHelper->OnAtmosphereLoaded.AddDynamic(&Owner, &UDigitalTwinManagerTestHelper::OnAtmosphereLoaded);
	}
}

void UDigitalTwinManagerTestHelper::FImpl::UnBindEvents()
{
	if (!Owner.ITwinManager)
		return;
	Owner.ITwinManager->ITwinInfoReceivedEvent.RemoveDynamic(&Owner, &UDigitalTwinManagerTestHelper::OnITwinInfoRetrieved);
	Owner.ITwinManager->ComponentLoadedEvent.RemoveDynamic(&Owner, &UDigitalTwinManagerTestHelper::OnLoadComponent);
	Owner.ITwinManager->ComponentWillBeRemovedEvent.RemoveDynamic(&Owner,
		&UDigitalTwinManagerTestHelper::OnWillRemoveComponent);
	Owner.ITwinManager->ComponentRemovedEvent.RemoveDynamic(&Owner, &UDigitalTwinManagerTestHelper::OnRemoveComponent);
	Owner.ITwinManager->GeoLocationSetEvent.RemoveDynamic(&Owner, &UDigitalTwinManagerTestHelper::OnGeoLocationSet);
	Owner.ITwinManager->GeoLocationGapInMetersEvent.RemoveDynamic(&Owner, &UDigitalTwinManagerTestHelper::OnGeoLocationGapInMeters);

	if (AITwinDecorationHelper* DecoHelper = GetDecorationHelper())
	{
		DecoHelper->OnAtmosphereLoaded.RemoveDynamic(&Owner, &UDigitalTwinManagerTestHelper::OnAtmosphereLoaded);
	}
}

int32 UDigitalTwinManagerTestHelper::FImpl::ExpectedLoadEvents() const
{
	return 4 /*iTwin Info + Load Reality Data + Load IModel + Attached reality data request */;
}

void UDigitalTwinManagerTestHelper::FImpl::DetectCustomEvents()
{
	if (Owner.ITwinManager && !bHasFinishedAttachedRealityDataRequest)
	{
		if (Owner.ITwinManager->CountProcessedIModelAttachmentRequests() == 1)
		{
			bHasFinishedAttachedRealityDataRequest = true;
			OnLoadEventReceived();
		}
	}
}

TUniquePtr<FITwinTilesetAccess> UDigitalTwinManagerTestHelper::FImpl::MakeTilesetAccess() const
{
	if (Owner.ITwinManager)
	{
		return Owner.ITwinManager->GetTilesetAccessFromId(GetRealityDataToLoad());
	}
	return {};
}

void UDigitalTwinManagerTestHelper::FImpl::SetHasLoadedAtmosphere(bool bInHasLoadedAtmosphere)
{
	ensure(!bInHasLoadedAtmosphere || bEnableSceneLoading);
	bHasLoadedAtmosphere = bInHasLoadedAtmosphere;
}

UDigitalTwinManagerTestHelper::UDigitalTwinManagerTestHelper()
	: Super()
	, Impl(MakePimpl<FImpl>(*this))
{

}

void UDigitalTwinManagerTestHelper::OnReset()
{
	Impl->Cleanup();

	if (ITwinManager)
	{
		ITwinManager->ResetITwin();
		ITwinManager->Destroy();
		ITwinManager = {};
	}
}

bool UDigitalTwinManagerTestHelper::Init(FIModelTestOptions const& Options)
{
	return Impl->Init(Options);
}

bool UDigitalTwinManagerTestHelper::PostCondition() const
{
	return !Impl->HasWrongLoadOrderDetected();
}


void UDigitalTwinManagerTestHelper::OnITwinInfoRetrieved()
{
	Impl->OnLoadEventReceived();
}

void UDigitalTwinManagerTestHelper::OnLoadComponent(AActor* LoadedObject, EITwinModelType ModelType, const FString& LayerId)
{
	Impl->OnLoadEventReceived();

	if (ModelType == EITwinModelType::RealityData)
	{
		BE_ASSERT(LoadedObject != nullptr);
		BE_ASSERT(Cast<AITwinRealityData>(LoadedObject) != nullptr);

		// Once the reality data is loaded, start loading the iModel.
		ITwinManager->LoadComponent(Impl->GetIModelToLoad(), EITwinLoadContext::Single);
	}
	else if (ModelType == EITwinModelType::IModel)
	{
		BE_ASSERT(LoadedObject != nullptr);
		BE_ASSERT(Cast<AITwinIModel>(LoadedObject) != nullptr);
	}
}

void UDigitalTwinManagerTestHelper::OnWillRemoveComponent(AActor* ComponentWillBeRemoved, EITwinModelType ModelType)
{
	if (ModelType == EITwinModelType::RealityData)
	{
		BE_ASSERT(IsValid(ComponentWillBeRemoved));
		auto* RealityData = Cast<AITwinRealityData>(ComponentWillBeRemoved);
		BE_ASSERT(RealityData != nullptr);
		if (RealityData)
		{
			BE_ASSERT(ITwinManager->IsComponentLoaded(RealityData->RealityDataId));
		}
	}
	else if (ModelType == EITwinModelType::IModel)
	{
		BE_ASSERT(IsValid(ComponentWillBeRemoved));
		auto* IModel = Cast<AITwinIModel>(ComponentWillBeRemoved);
		BE_ASSERT(IModel != nullptr);
		if (IModel)
		{
			BE_ASSERT(ITwinManager->IsComponentLoaded(IModel->IModelId));
		}
	}
}

void UDigitalTwinManagerTestHelper::OnRemoveComponent(FString ModelId, EITwinModelType /*ModelType*/)
{
	BE_ASSERT(!ITwinManager->IsComponentLoaded(ModelId));
}

void UDigitalTwinManagerTestHelper::OnGeoLocationSet(bool bFromScene, bool bFromElevationRequest)
{
	// Atmosphere should be loaded before the geolocation is set!
	// Test added following a regression. See ADO#2139590
	if (Impl->IsSceneLoadingEnabled() && !Impl->HasLoadedAtmosphere())
	{
		BE_ISSUE("Regression detected: Atmosphere should be loaded before the geolocation is set!");
		Impl->SetWrongLoadOrderDetected(true);
	}
}

void UDigitalTwinManagerTestHelper::OnGeoLocationGapInMeters(double DistanceMeters, const FString& LayerId)
{

}


void UDigitalTwinManagerTestHelper::OnAtmosphereLoaded(bool bSuccess)
{
	Impl->SetHasLoadedAtmosphere(bSuccess);
}


DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FNUTWaitForAsyncITwinManagerTest, FITwinIOAsyncCallbackPtr, ITwinManagerAsyncCallback);

bool FNUTWaitForAsyncITwinManagerTest::Update()
{
	if (!ITwinManagerAsyncCallback || ITwinManagerAsyncCallback->IsDone())
	{
		UDigitalTwinManagerTestHelper::ResetInstance();
		return true;
	}
	else
	{
		return false;
	}
}

#if WITH_EDITOR

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDigitalTwinManagerTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.ITwinManager", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDigitalTwinManagerTest::RunTest(const FString& /*Parameters*/)
{
	UDigitalTwinManagerTestHelper& HelperObject = UDigitalTwinManagerTestHelper::Instance();

	auto& Helper = HelperObject.GetImpl();

	if (!Helper.Init())
	{
		return false;
	}
	UWorld* const World = FITwinAPITestHelperBase::GetTestWorld();

	FString const MockServerUrl = Helper.GetMockServerUrl();

	// Instantiate the iTwin manager, configured in test mode.
	HelperObject.ITwinManager = World->SpawnActor<AITwinDigitalTwinManager>();
	HelperObject.ITwinManager->SetTestMode(MockServerUrl);

	// Configure the Google 3D Tiles controller to use the test server and key.
	AITwinGoogle3DTilesController* GoogleTilesController = AITwinGoogle3DTilesController::GetInstance(World);
	if (ensure(GoogleTilesController))
	{
		if (!MockServerUrl.IsEmpty())
		{
			GoogleTilesController->SetElevationTestMode(MockServerUrl, TEXT("test_google_elevation_key"));
		}
	}

	FITwinIOAsyncCallbackPtr AsyncCallback = Helper.GetAsyncCallback();

	SECTION("ITwinManager Load and Test")
	{
		UTEST_TRUE(TEXT("Check world"), World != nullptr);
		UTEST_TRUE(TEXT("Check iTwin manager"), HelperObject.ITwinManager != nullptr);

		// First we will load a (geo-located) reality data.
		const FString RealityDataToLoad = Helper.GetRealityDataToLoad();
		UTEST_FALSE(TEXT("IsComponentLoaded - before"), HelperObject.ITwinManager->IsComponentLoaded(RealityDataToLoad));
		UTEST_FALSE(TEXT("IsComponentBeingLoaded - before"), HelperObject.ITwinManager->IsComponentBeingLoaded(RealityDataToLoad));
		UTEST_EQUAL(TEXT("GetComponentLoadContext - before"), HelperObject.ITwinManager->GetComponentLoadContext(RealityDataToLoad), EITwinLoadContext::Unknown);
		UTEST_FALSE(TEXT("IsRealityData - before"), HelperObject.ITwinManager->IsRealityData(RealityDataToLoad));
		UTEST_NULL(TEXT("GetRealityData - before"), HelperObject.ITwinManager->GetRealityData(RealityDataToLoad));
		TSet<FString> const LoadedReality = HelperObject.ITwinManager->GetLoadedLayers(EITwinModelType::RealityData, false);
		UTEST_TRUE(TEXT("GetLoadedLayers - before"), LoadedReality.IsEmpty());
		UTEST_TRUE(TEXT("GetComponentName - before"), HelperObject.ITwinManager->GetComponentName(RealityDataToLoad).IsEmpty());

		HelperObject.ITwinManager->LoadComponent(RealityDataToLoad, EITwinLoadContext::Single);
		UTEST_TRUE(TEXT("HasLoadingPending"), HelperObject.ITwinManager->HasLoadingPending());
		UTEST_EQUAL(TEXT("GetComponentLoadContext - just started"), HelperObject.ITwinManager->GetComponentLoadContext(RealityDataToLoad), EITwinLoadContext::Single);

		Helper.BindEvents();

		HelperObject.ITwinManager->Init(Helper.GetITwinID(), TEXT("Digital Twin Manager Test"));

		Helper.SetTestCallback(
			[this, AsyncCallback, RealityDataToLoad, IModelToLoad = Helper.GetIModelToLoad(),
			ITwinManager = TStrongObjectPtr<AITwinDigitalTwinManager>(HelperObject.ITwinManager)]()
		{
			auto const ValidateITwinManager = [&]() -> bool
			{
				UTEST_EQUAL(TEXT("GetITwinName"), ITwinManager->GetITwinName(), TEXT("DEMO_Building"));

				UTEST_NULL(TEXT("GetSkyLight"), ITwinManager->GetSkyLight());

				UTEST_FALSE(TEXT("IsComponentBeingLoaded - when done"), ITwinManager->IsComponentBeingLoaded(RealityDataToLoad));
				UTEST_TRUE(TEXT("IsComponentLoaded - when done"), ITwinManager->IsComponentLoaded(RealityDataToLoad));
				UTEST_EQUAL(TEXT("GetComponentLoadContext - when done"), ITwinManager->GetComponentLoadContext(RealityDataToLoad), EITwinLoadContext::Single);
				UTEST_TRUE(TEXT("IsRealityData - when done"), ITwinManager->IsRealityData(RealityDataToLoad));
				UTEST_FALSE(TEXT("IsIModel - for reality data"), ITwinManager->IsIModel(RealityDataToLoad));

				TSet<FString> LoadedReality = ITwinManager->GetLoadedLayers(EITwinModelType::RealityData, true);
				UTEST_EQUAL(TEXT("GetLoadedLayers - when done"), LoadedReality.Num(), 1);
				UTEST_TRUE(TEXT("GetLoadedLayers - when done"), LoadedReality.Contains(RealityDataToLoad));

				{
					auto Access = ITwinManager->GetTilesetAccess(EITwinModelType::RealityData, RealityDataToLoad);
					UTEST_TRUE(TEXT("Has tileset access"), Access.IsValid());
					UTEST_TRUE(TEXT("Has tileset"), Access->HasTileset());
					UTEST_EQUAL(TEXT("Model Link"), Access->GetModelLink(), std::make_pair(EITwinModelType::RealityData, RealityDataToLoad));
					const ACesium3DTileset* Tileset = Access->GetTileset();
					auto const GeoRef = Tileset ? Tileset->GetGeoreference() : nullptr;
					UTEST_NOT_NULL(TEXT("Has georeference"), GeoRef.Get());
					if (GeoRef)
					{
						UTEST_EQUAL(TEXT("Origin Placement"), GeoRef->GetOriginPlacement(), EOriginPlacement::CartographicOrigin);
						UTEST_EQUAL_TOLERANCE(TEXT("Origin Latitude"), GeoRef->GetOriginLatitude(), 28.542089, 0.00001);
						UTEST_EQUAL_TOLERANCE(TEXT("Origin Longitude"), GeoRef->GetOriginLongitude(), -81.378958, 0.00001);
						UTEST_EQUAL_TOLERANCE(TEXT("Origin Height"), GeoRef->GetOriginHeight(), 31.9135608, 0.00001);
					}
				}

				UTEST_NULL(TEXT("GetIModel"), ITwinManager->GetIModel(RealityDataToLoad));

				AITwinRealityData const* const RealityData = ITwinManager->GetRealityData(RealityDataToLoad);
				UTEST_NOT_NULL(TEXT("GetRealityData"), RealityData);
				if (RealityData)
				{
					UTEST_TRUE(TEXT("IsGeolocated"), RealityData->IsGeolocated());
					UTEST_EQUAL(TEXT("RealityData.ITwinId"), RealityData->ITwinId, ITwinManager->GetITwinId());
				}

				UTEST_TRUE(TEXT("HasRetrievedITwinInfo"), ITwinManager->HasRetrievedITwinInfo());
				UTEST_TRUE(TEXT("IsGeoLocationSet"), ITwinManager->IsGeoLocationSet());

				UTEST_EQUAL(TEXT("GetComponentName - when done"), ITwinManager->GetComponentName(RealityDataToLoad), TEXT("Orlando_CesiumDraco_LAT"));

				// Also check the iModel, which should have been loaded after the reality data.
				TSet<FString> LoadedIModels = ITwinManager->GetLoadedLayers(EITwinModelType::IModel, true);
				UTEST_EQUAL(TEXT("GetLoadedIModels"), LoadedIModels.Num(), 1);
				UTEST_TRUE(TEXT("GetLoadedIModels"), LoadedIModels.Contains(IModelToLoad));

				UTEST_TRUE(TEXT("IsComponentLoaded - imodel"), ITwinManager->IsComponentLoaded(IModelToLoad));
				UTEST_EQUAL(TEXT("GetComponentLoadContext - imodel"), ITwinManager->GetComponentLoadContext(IModelToLoad), EITwinLoadContext::Single);
				UTEST_FALSE(TEXT("IsRealityData - imodel"), ITwinManager->IsRealityData(IModelToLoad));
				UTEST_TRUE(TEXT("IsIModel - imodel"), ITwinManager->IsIModel(IModelToLoad));
				UTEST_EQUAL(TEXT("GetComponentName - imodel"), ITwinManager->GetComponentName(IModelToLoad), TEXT("SYNCHRO Training Project"));
				UTEST_EQUAL(TEXT("AreComponentSavedViewsLoaded"), ITwinManager->AreComponentSavedViewsLoaded(IModelToLoad),
					AITwinIModel::AreSavedViewsUpdatesEnabled());
				return true;
			};

			auto const TestITwinManager = [&]() -> bool
			{
				AITwinIModel* const IModel = ITwinManager->GetIModel(IModelToLoad);
				ITwinManager->RemoveComponent(IModelToLoad);

				UTEST_FALSE(TEXT("IsComponentLoaded - after removal"), ITwinManager->IsComponentLoaded(IModelToLoad));
				UTEST_TRUE(TEXT("IsIModel - still true after removal"), ITwinManager->IsIModel(IModelToLoad));

				TSet<FString> LoadedIModels = ITwinManager->GetLoadedLayers(EITwinModelType::IModel, true);
				UTEST_EQUAL(TEXT("GetLoadedIModels"), LoadedIModels.Num(), 0);

				return true;
			};

			if (ValidateITwinManager())
			{
				// Perform a few tests modifying the manager.
				TestITwinManager();
			}

			AsyncCallback->OnRequestDone();
		});

		AsyncCallback->OnRequestStarted();
	}

	ADD_LATENT_AUTOMATION_COMMAND(FNUTWaitForAsyncITwinManagerTest(AsyncCallback));

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FDigitalTwinManagerLoadSceneTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.ITwinManager_LoadScene", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDigitalTwinManagerLoadSceneTest::RunTest(const FString& /*Parameters*/)
{
	UDigitalTwinManagerTestHelper& HelperObject = UDigitalTwinManagerTestHelper::Instance();

	auto& Helper = HelperObject.GetImpl();

	if (!Helper.Init())
	{
		return false;
	}
	UWorld* const World = FITwinAPITestHelperBase::GetTestWorld();

	FString const MockServerUrl = Helper.GetMockServerUrl();

	// Instantiate the iTwin manager, configured in test mode.
	HelperObject.ITwinManager = World->SpawnActor<AITwinDigitalTwinManager>();
	HelperObject.ITwinManager->SetTestMode(MockServerUrl);

	// Configure the Google 3D Tiles controller to use the test server and key.
	AITwinGoogle3DTilesController* GoogleTilesController = AITwinGoogle3DTilesController::GetInstance(World);
	if (ensure(GoogleTilesController))
	{
		if (!MockServerUrl.IsEmpty())
		{
			GoogleTilesController->SetElevationTestMode(MockServerUrl, TEXT("test_google_elevation_key"));
		}
	}

	FITwinIOAsyncCallbackPtr AsyncCallback = Helper.GetAsyncCallback();

	SECTION("ITwinManager Load and Test")
	{
		UTEST_TRUE(TEXT("Check world"), World != nullptr);
		UTEST_TRUE(TEXT("Check iTwin manager"), HelperObject.ITwinManager != nullptr);

		// Activate check of loading step orders.
		Helper.EnableSceneLoading(true);

		Helper.BindEvents();
		HelperObject.ITwinManager->Init(Helper.GetITwinID(), TEXT("Digital Twin Manager LoadScene Test"));
		if (auto* DecoHelper = Helper.GetDecorationHelper())
		{
			DecoHelper->SetITwinManager(HelperObject.ITwinManager);
		}

		// Load the scene defined in the test.
		HelperObject.ITwinManager->LoadDecoration();

		Helper.SetTestCallback(
			[this, AsyncCallback, RealityDataToLoad = Helper.GetRealityDataToLoad(), IModelToLoad = Helper.GetIModelToLoad(),
			ITwinManager = TStrongObjectPtr<AITwinDigitalTwinManager>(HelperObject.ITwinManager)]()
		{
			auto const ValidateAfterSceneLoading = [&]() -> bool
			{
				UTEST_EQUAL(TEXT("GetITwinName"), ITwinManager->GetITwinName(), TEXT("DEMO_Building"));

				UTEST_TRUE(TEXT("IsComponentLoaded"), ITwinManager->IsComponentLoaded(RealityDataToLoad));
				UTEST_TRUE(TEXT("IsRealityData"), ITwinManager->IsRealityData(RealityDataToLoad));
				{
					auto Access = ITwinManager->GetTilesetAccess(EITwinModelType::RealityData, RealityDataToLoad);
					UTEST_TRUE(TEXT("Has tileset access"), Access.IsValid());
					const ACesium3DTileset* Tileset = Access->GetTileset();
					auto const GeoRef = Tileset ? Tileset->GetGeoreference() : nullptr;
					UTEST_NOT_NULL(TEXT("Has georeference"), GeoRef.Get());
					if (GeoRef)
					{
						UTEST_EQUAL(TEXT("Origin Placement"), GeoRef->GetOriginPlacement(), EOriginPlacement::CartographicOrigin);
						UTEST_EQUAL_TOLERANCE(TEXT("Origin Latitude"), GeoRef->GetOriginLatitude(), 28.542089, 0.00001);
						UTEST_EQUAL_TOLERANCE(TEXT("Origin Longitude"), GeoRef->GetOriginLongitude(), -81.378958, 0.00001);
						UTEST_EQUAL_TOLERANCE(TEXT("Origin Height"), GeoRef->GetOriginHeight(), 31.9135608, 0.00001);
					}
				}

				UTEST_TRUE(TEXT("HasRetrievedITwinInfo"), ITwinManager->HasRetrievedITwinInfo());
				UTEST_TRUE(TEXT("IsGeoLocationSet"), ITwinManager->IsGeoLocationSet());
				UTEST_EQUAL(TEXT("GetComponentName - reality data"), ITwinManager->GetComponentName(RealityDataToLoad), TEXT("Orlando_CesiumDraco_LAT"));

				// Also check the iModel, which should have been loaded within the scene.
				UTEST_TRUE(TEXT("IsIModel"), ITwinManager->IsIModel(IModelToLoad));
				{
					auto Access = ITwinManager->GetTilesetAccess(EITwinModelType::IModel, IModelToLoad);
					UTEST_TRUE(TEXT("Has tileset"), Access.IsValid() && Access->HasTileset());
				}
				UTEST_EQUAL(TEXT("GetComponentName - imodel"), ITwinManager->GetComponentName(IModelToLoad), TEXT("SYNCHRO Training Project"));
				return true;
			};

			ValidateAfterSceneLoading();

			AsyncCallback->OnRequestDone();
		});

		AsyncCallback->OnRequestStarted();
	}

	ADD_LATENT_AUTOMATION_COMMAND(FNUTWaitForAsyncITwinManagerTest(AsyncCallback));

	return true;
}

#endif // WITH_EDITOR

#endif // WITH_TESTS
