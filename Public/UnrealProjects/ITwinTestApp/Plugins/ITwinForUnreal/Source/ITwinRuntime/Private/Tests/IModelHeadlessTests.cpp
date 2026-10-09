/*--------------------------------------------------------------------------------------+
|
|     $Source: IModelHeadlessTests.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS

#include <ITwinIModel.h>
#include <ITwinIModel3DInfo.h>
#include <ITwinSavedView.h>
#include <ITwinTilesetAccess.h>
#include <ITwinWebServices/ITwinWebServices_Info.h>
#include <IncludeCesium3DTileset.h>
#include <Decoration/ITwinDecorationHelper.h>

#include <Tests/IModelHeadlessTestsHelper.h>
#include <Tests/IModelTestHelperImpl.h>
#include <Tests/ITwinAutomationTestBaseNoLogs.h>
#include <Tests/ITwinFileBasedMockServer.h>

#include <Misc/LowLevelTestAdapter.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/ITwinAPI/ITwinMaterial.h>
#include <Compil/AfterNonUnrealIncludes.h>


class UIModelHeadlessTestsHelper::FImpl : public FIModelTestHelperImpl
{
public:
	FImpl(UIModelHeadlessTestsHelper& InOwner) : Owner(InOwner) {}
	~FImpl() { Cleanup(); }
	
	virtual void FillTestInfo(FITwinExportInfo& OutExportInfo,
		FString& OutSceneId,
		std::filesystem::path& OutRelativeCacheFolder,
		FInitOptions const& InOptions) const override;

	virtual void BindEvents() override;
	virtual void UnBindEvents() override;
	virtual int32 ExpectedLoadEvents() const override;

private:
	UIModelHeadlessTestsHelper& Owner;
};


void UIModelHeadlessTestsHelper::FImpl::FillTestInfo(FITwinExportInfo& OutExportInfo,
	FString& OutSceneId,
	std::filesystem::path& OutRelativeCacheFolder,
	FInitOptions const& InOptions) const
{
	// Use the same iModel as in IModelRenderTest.
	OutExportInfo.Id = InOptions.bDiscoverExport ? TEXT("") : TEXT("a4feb6ee-bb1c-40d3-8659-a1765b1d179d");
	OutExportInfo.iTwinId = TEXT("5e15184e-6d3c-43fd-ad04-e28b4b39485e");
	OutExportInfo.iModelId = TEXT("b53cebea-451f-4433-942f-eabda9c11d21");
	OutExportInfo.ChangesetId = InOptions.bUseLatestChangeset ? TEXT("latest") : TEXT("");
	OutExportInfo.DisplayName = TEXT("iModel for headless tests") + FString(InOptions.bUseLatestChangeset ? TEXT(" (latest changeset)") : TEXT(""));

	// Use the default scene of this iTwin.
	OutSceneId = InOptions.bDiscoverDefaultScene ? TEXT("") : TEXT("7e2059e4-8a48-4671-a67d-6da36d88d9bf");

	OutRelativeCacheFolder = "FunctionalTests/IModelRender";
}

void UIModelHeadlessTestsHelper::FImpl::BindEvents()
{
	if (AITwinDecorationHelper* DecoHelper = GetDecorationHelper())
	{
		OnDecorationLoadedHandle = DecoHelper->OnDecorationLoaded.AddUObject(&Owner, &UIModelHeadlessTestsHelper::OnDecorationLoaded);
	}
	if (AITwinIModel* IModel = GetIModel())
	{
		IModel->OnIModelLoaded.AddUniqueDynamic(&Owner, &UIModelHeadlessTestsHelper::OnIModelLoaded);

		IModel->ElementPropertiesRetrieved.AddUniqueDynamic(&Owner, &UIModelHeadlessTestsHelper::OnElementPropertiesRetrieved);

		if (CurrentOptions.bAutoLoadSavedViews)
		{
			IModel->FinishedLoadingSavedViews.AddUniqueDynamic(&Owner, &UIModelHeadlessTestsHelper::OnSavedViewsRetrieved);
		}
	}
}

void UIModelHeadlessTestsHelper::FImpl::UnBindEvents()
{
	if (AITwinDecorationHelper* DecoHelper = GetDecorationHelper())
	{
		DecoHelper->OnDecorationLoaded.Remove(OnDecorationLoadedHandle);
		OnDecorationLoadedHandle.Reset();
	}
	if (AITwinIModel* IModel = GetIModel())
	{
		IModel->OnIModelLoaded.RemoveDynamic(&Owner, &UIModelHeadlessTestsHelper::OnIModelLoaded);
		IModel->ElementPropertiesRetrieved.RemoveDynamic(&Owner, &UIModelHeadlessTestsHelper::OnElementPropertiesRetrieved);
	}
}


int32 UIModelHeadlessTestsHelper::FImpl::ExpectedLoadEvents() const
{
	return 2 /*iModel loaded + scene mapping filled*/
		+ (bShouldLoadScene ? 1 : 0)
		+ (CurrentOptions.bAutoLoadSavedViews ? 1 : 0);
}


UIModelHeadlessTestsHelper::UIModelHeadlessTestsHelper()
	: Super()
	, Impl(MakePimpl<FImpl>(*this))
{

}

void UIModelHeadlessTestsHelper::OnReset()
{
	// Important: Reset the handle to ensure the TStrongObjectPtr it contains will be reset, and thus
	// will not prevent IModel and DecoHelper from being destroyed (which blocks the destruction
	// of the world by the GC).
	Impl->Cleanup();
}

bool UIModelHeadlessTestsHelper::Init(FIModelTestOptions const& Options)
{
	return Impl->Init(Options);
}

bool UIModelHeadlessTestsHelper::PostCondition() const
{
	return true;
}

void UIModelHeadlessTestsHelper::RegisterEvents()
{
	Impl->BindEvents();
}

void UIModelHeadlessTestsHelper::OnIModelLoaded(bool bSuccess, FString StringId)
{
	Impl->OnIModelLoaded(bSuccess, StringId);
}

void UIModelHeadlessTestsHelper::OnDecorationLoaded()
{
	Impl->OnLoadEventReceived();
}

void UIModelHeadlessTestsHelper::OnSavedViewsRetrieved(const FString& ID)
{
	Impl->OnLoadEventReceived();
}

void UIModelHeadlessTestsHelper::OnElementPropertiesRetrieved(bool bSuccess, const FElementProperties& ElementProps, const FString& ElementId)
{
	BE_ASSERT(bSuccess);
	BE_ASSERT(ElementId == TEXT("0x59"));
	BE_ASSERT(ElementProps.Properties.Num() == 1
		&& ElementProps.Properties[0].Attributes.Num() == 4
		&& ElementProps.Properties[0].Attributes[3].Name == TEXT("Category")
		&& ElementProps.Properties[0].Attributes[3].Value == TEXT("zoupiLayer0"));

	FITwinIOAsyncCallbackPtr AsyncCallback = Impl->GetAsyncCallback();
	if (AsyncCallback)
	{
		AsyncCallback->OnRequestDone();
	}
	Impl->CleanupIfDone();
}


DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FNUTWaitForAsyncIModelHeadlessTest, FITwinIOAsyncCallbackPtr, IModelHeadlessAsyncCallback);

bool FNUTWaitForAsyncIModelHeadlessTest::Update()
{
	if (!IModelHeadlessAsyncCallback || IModelHeadlessAsyncCallback->IsDone())
	{
		UIModelHeadlessTestsHelper::ResetInstance();
		return true;
	}
	else
	{
		return false;
	}
}

#if WITH_EDITOR

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FIModelHeadlessTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.IModelHeadless", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIModelHeadlessTest::RunTest(const FString& /*Parameters*/)
{
	UIModelHeadlessTestsHelper& HelperObject = UIModelHeadlessTestsHelper::Instance();

	auto& Helper = HelperObject.GetImpl();

	if (!Helper.Init())
	{
		return false;
	}

	auto* DecoHelper = Helper.GetDecorationHelper();
	auto* IModel = Helper.GetIModel();
	FITwinIOAsyncCallbackPtr AsyncCallback = Helper.GetAsyncCallback();

	UWorld* const World = FITwinAPITestHelperBase::GetTestWorld();

	SECTION("IModel Load and Test")
	{
		UTEST_TRUE(TEXT("Check world"), World != nullptr);
		UTEST_TRUE(TEXT("Check decoration helper"), DecoHelper != nullptr);
		UTEST_TRUE(TEXT("Check iModel"), IModel != nullptr);

		Helper.BindEvents();

		Helper.SetTestCallback(
			[this, AsyncCallback,
			IModel = TStrongObjectPtr<AITwinIModel>(IModel),
			DecoHelper = TStrongObjectPtr<AITwinDecorationHelper>(DecoHelper)]()
		{
			auto const ValidateLoadedIModel = [&]() -> bool
			{
				FBox BoundingBox;
				UTEST_TRUE(TEXT("Get BoundingBox"), IModel->GetBoundingBox(BoundingBox, true, AITwinIModel::EBBoxMethod::ProjectExtents));
				UTEST_TRUE(TEXT("BoundingBox is valid"), static_cast<bool>(BoundingBox.IsValid));

				UTEST_EQUAL(TEXT("GetSelectedChangeset"), IModel->GetSelectedChangeset(), FString(TEXT("")));
				UTEST_EQUAL(TEXT("GetExportID"), IModel->GetExportID(), FString(TEXT("a4feb6ee-bb1c-40d3-8659-a1765b1d179d")));
				UTEST_EQUAL(TEXT("GetActorLabel"), IModel->GetActorLabel(), FString(TEXT("iModel for headless tests")));

				UTEST_TRUE(TEXT("Has loaded tileset"), IModel->HasLoadedTileset());
				UTEST_FALSE(TEXT("Has no load failure"), IModel->HasTilesetLoadFailure());

				UTEST_EQUAL(TEXT("Model Link"), IModel->GetModelLink(), std::make_pair(EITwinModelType::IModel, FString(TEXT("b53cebea-451f-4433-942f-eabda9c11d21"))));

				FITwinIModel3DInfo IModel3DInfo;
				IModel->GetModel3DInfoInCoordSystem(IModel3DInfo, EITwinCoordSystem::ITwin);
				UTEST_EQUAL(TEXT("3DInfo center"), IModel3DInfo.ModelCenter, FVector(-77.394499, 113.9294266, 18.2211449));

				FITwinIModel3DInfo IModel3DInfo2;
				IModel->GetModel3DInfo(IModel3DInfo2);
				UTEST_EQUAL(TEXT("3DInfo bbox Min"), IModel3DInfo.BoundingBoxMin, IModel3DInfo2.BoundingBoxMin);
				UTEST_EQUAL(TEXT("3DInfo bbox Max"), IModel3DInfo.BoundingBoxMax, IModel3DInfo2.BoundingBoxMax);

				FBox const InfoBox(IModel3DInfo.BoundingBoxMin, IModel3DInfo.BoundingBoxMax);
				auto const InfoBoxSize = InfoBox.GetSize();
				UTEST_EQUAL(TEXT("3D BoundingBox size"), InfoBoxSize, FVector(592.984962646, 506.2365579, 76.4424898));

				const FITwinLoadInfo LoadInfo = IModel->GetModelLoadInfo();
				UTEST_EQUAL(TEXT("LoadInfo exportID"), LoadInfo.ExportId, TEXT("a4feb6ee-bb1c-40d3-8659-a1765b1d179d"));
				UTEST_EQUAL(TEXT("LoadInfo iTwinID"), LoadInfo.ITwinId, TEXT("5e15184e-6d3c-43fd-ad04-e28b4b39485e"));
				UTEST_EQUAL(TEXT("LoadInfo iModelID"), LoadInfo.IModelId, TEXT("b53cebea-451f-4433-942f-eabda9c11d21"));
				UTEST_EQUAL(TEXT("LoadInfo changeset"), LoadInfo.ChangesetId, TEXT(""));
				UTEST_TRUE(TEXT("LoadInfo modelType"), LoadInfo.ModelType == EITwinModelType::IModel);
				UTEST_TRUE(TEXT("LoadInfo reality data"), LoadInfo.RealityDataId.IsEmpty());

				{
					TUniquePtr<FITwinTilesetAccess> Access = IModel->MakeTilesetAccess();
					UTEST_TRUE(TEXT("Has tileset access"), Access.IsValid());
					UTEST_TRUE(TEXT("Has tileset"), Access->HasTileset());
					UTEST_EQUAL(TEXT("Model Link"), Access->GetModelLink(), std::make_pair(EITwinModelType::IModel, FString(TEXT("b53cebea-451f-4433-942f-eabda9c11d21"))));
					UTEST_FALSE(TEXT("IsTilesetHidden"), Access->IsTilesetHidden());
					auto const Quality = Access->GetTilesetQuality();
					UTEST_TRUE(TEXT("Tileset quality"), Quality > 0.f && Quality < 1.f);
					FVector Pos, Rot;
					Access->GetModelOffset(Pos, Rot);
					UTEST_EQUAL(TEXT("Offset Pos"), Pos, FVector::ZeroVector);
					UTEST_EQUAL(TEXT("Offset Rot"), Rot, FVector::ZeroVector);
				}

				const FProjectExtents* Extents = IModel->GetProjectExtents();
				UTEST_NOT_NULL(TEXT("Project Extents"), Extents);
				if (Extents)
				{
					UTEST_EQUAL(TEXT("Project Extents Low"), Extents->Low, FVector(-373.88698, -139.1888523, -20.0001));
					UTEST_EQUAL(TEXT("Project Extents High"), Extents->High, FVector(219.0979821, 367.0477056, 56.4423898));
					UTEST_EQUAL(TEXT("Project Extents GlobalOrigin"), Extents->GlobalOrigin, FVector::ZeroVector);
				}
				const FEcefLocation* EcefLocation = IModel->GetEcefLocation();
				UTEST_NULL(TEXT("Ecef Location"), EcefLocation);

				UTEST_EQUAL(TEXT("GetSceneID"), DecoHelper->GetSceneID(), FString(TEXT("7e2059e4-8a48-4671-a67d-6da36d88d9bf")));
				auto const MaterialMap = IModel->GetITwinMaterialMap();
				UTEST_EQUAL(TEXT("MaterialMap size"), MaterialMap.Num(), 8);
				const uint64 Id42 = 42;
				FString const* Mat42 = MaterialMap.Find(Id42);
				UTEST_NOT_NULL(TEXT("Material 42"), Mat42);
				if (Mat42)
				{
					UTEST_EQUAL(TEXT("Material 42 name"), *Mat42, FString(TEXT("5_0056_Yellow")));
				}
				const uint64 Id38 = 38;
				UTEST_EQUAL(TEXT("Material 38 display name"), IModel->GetMaterialName(Id38, true), FString(TEXT("Vanilla Sky")));
				UTEST_EQUAL(TEXT("Material 38 initial name"), IModel->GetMaterialName(Id38, false), FString(TEXT("1_0096_SkyBlue")));
				// For nmaterial #42, the display name was not customized.
				UTEST_EQUAL(TEXT("Material 42 display name"), IModel->GetMaterialName(Id42, true), FString(TEXT("5_0056_Yellow")));

				auto const Color = IModel->GetMaterialChannelColor(Id38, AdvViz::SDK::EChannelType::Color).ToRGBE();
				UTEST_EQUAL(TEXT("Material 42 color"), Color, FColor(7, 109, 252, 128));

				UTEST_EQUAL(TEXT("Material 38 roughness"), IModel->GetMaterialChannelIntensity(Id38, AdvViz::SDK::EChannelType::Roughness), 1.0);

				return true;
			};

			auto const TestIModel = [&]() -> bool
			{
				if (IModel->SelectElement(TEXT("0x59")))
				{
					// SelectElement triggers a request to describe the element, so we should wait for the
					// request to be processed before exiting the test.
					// (see bLogPropertiesUponSelectElement in FITwinIModelInternals::DescribeElement).
					AsyncCallback->OnRequestStarted();
				}

				UTEST_TRUE(TEXT("SelectElement"), IModel->IsElementSelected(TEXT("0x59")));

				IModel->SelectElements({ TEXT("0x5d"), TEXT("0x5b") });
				UTEST_TRUE(TEXT("SelectElements"), IModel->IsElementSelected(TEXT("0x5d")));
				UTEST_FALSE(TEXT("SelectElement"), IModel->IsElementSelected(TEXT("0x59")));

				IModel->AddElementsToSelection({ TEXT("0x59") });
				UTEST_TRUE(TEXT("AddElementsToSelection"), IModel->IsElementSelected(TEXT("0x5d")));
				UTEST_TRUE(TEXT("AddElementsToSelection"), IModel->IsElementSelected(TEXT("0x59")));

				IModel->RemoveElementsFromSelection({ TEXT("0x5d"), TEXT("0x5b") });
				UTEST_FALSE(TEXT("SelectElement"), IModel->IsElementSelected(TEXT("0x5d")));
				UTEST_TRUE(TEXT("SelectElement"), IModel->IsElementSelected(TEXT("0x59")));

				IModel->DeSelectElements();
				UTEST_FALSE(TEXT("SelectElement"), IModel->IsElementSelected(TEXT("0x59")));
				return true;
			};

			if (ValidateLoadedIModel())
			{
				// Perform a few tests on iModel.
				TestIModel();
			}

			AsyncCallback->OnRequestDone();
		});

		AsyncCallback->OnRequestStarted();
	}

	ADD_LATENT_AUTOMATION_COMMAND(FNUTWaitForAsyncIModelHeadlessTest(AsyncCallback));

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FIModelLatestChangesetTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.IModelLatestChangeset", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIModelLatestChangesetTest::RunTest(const FString& /*Parameters*/)
{
	UIModelHeadlessTestsHelper& HelperObject = UIModelHeadlessTestsHelper::Instance();

	auto& Helper = HelperObject.GetImpl();

	if (!Helper.Init(FIModelTestOptions{
		.bUseLatestChangeset = true,
		.bDiscoverExport = true,
		.bDiscoverDefaultScene = true,
		.bAutoLoadSavedViews = true }))
	{
		return false;
	}

	auto* DecoHelper = Helper.GetDecorationHelper();
	auto* IModel = Helper.GetIModel();
	FITwinIOAsyncCallbackPtr AsyncCallback = Helper.GetAsyncCallback();

	UWorld* const World = FITwinAPITestHelperBase::GetTestWorld();

	SECTION("Load IModel with Latest Changeset")
	{
		UTEST_TRUE(TEXT("Check world"), World != nullptr);
		UTEST_TRUE(TEXT("Check decoration helper"), DecoHelper != nullptr);
		UTEST_TRUE(TEXT("Check iModel"), IModel != nullptr);

		Helper.BindEvents();

		Helper.SetTestCallback(
			[this, AsyncCallback,
			IModel = TStrongObjectPtr<AITwinIModel>(IModel),
			DecoHelper = TStrongObjectPtr<AITwinDecorationHelper>(DecoHelper)]()
		{
			auto const ValidateLoadedIModel = [&]() -> bool
			{
				UTEST_EQUAL(TEXT("GetSelectedChangeset"), IModel->GetSelectedChangeset(), FString(TEXT("")));
				UTEST_EQUAL(TEXT("GetExportID"), IModel->GetExportID(), FString(TEXT("a4feb6ee-bb1c-40d3-8659-a1765b1d179d")));
				// The iTwin ID should have been filled while retrieving the export.
				UTEST_EQUAL(TEXT("GetITwinID"), IModel->ITwinId, FString(TEXT("5e15184e-6d3c-43fd-ad04-e28b4b39485e")));
				// Name retrieved from the server.
				UTEST_EQUAL(TEXT("GetActorLabel"), IModel->GetActorLabel(), FString(TEXT("UTF8_ADE_InstancesTest")));
				// Scene ID retrieved by looking for the default scene name.
				UTEST_EQUAL(TEXT("GetSceneID"), DecoHelper->GetSceneID(), FString(TEXT("b0814356-cbf0-4c4f-8f05-02a636cefa2d")));

				// Saved views
				UTEST_TRUE(TEXT("Has loaded saved views"), IModel->AreSavedViewsLoaded());
				AITwinSavedView const* SV_001 = IModel->GetITwinSavedViewActor(TEXT("ABIyuxws235Ku90nw9sCS05OGBVePG39Q60E4otLOUhe6us8tR9FM0SUL-q9qcEdIQ"));
				UTEST_NOT_NULL(TEXT("Saved view 001"), SV_001);
				AITwinSavedView const* SV_002 = IModel->GetITwinSavedViewActor(TEXT("APlW_hOBzBlGhjGUPAwNZgpOGBVePG39Q60E4otLOUhe6us8tR9FM0SUL-q9qcEdIQ"));
				UTEST_NOT_NULL(TEXT("Saved view 002"), SV_002);
				AITwinSavedView const* SV_003 = IModel->GetITwinSavedViewActor(TEXT("AP2Wy7qvxYVFr0ltr7IPEuBOGBVePG39Q60E4otLOUhe6us8tR9FM0SUL-q9qcEdIQ"));
				UTEST_NOT_NULL(TEXT("Saved view 003"), SV_003);

				if (SV_001 && SV_002 && SV_003)
				{
					UTEST_EQUAL(TEXT("Saved view 001 Name"), SV_001->DisplayName, FString(TEXT("view 001")));
					UTEST_EQUAL(TEXT("Saved view 002 Name"), SV_002->DisplayName, FString(TEXT("view 002")));
					UTEST_EQUAL(TEXT("Saved view 003 Name"), SV_003->DisplayName, FString(TEXT("view 003")));
				}

				return true;
			};

			ValidateLoadedIModel();

			AsyncCallback->OnRequestDone();
		});

		AsyncCallback->OnRequestStarted();
	}

	ADD_LATENT_AUTOMATION_COMMAND(FNUTWaitForAsyncIModelHeadlessTest(AsyncCallback));

	return true;
}
#endif // WITH_EDITOR

#endif // WITH_TESTS
