/*--------------------------------------------------------------------------------------+
|
|     $Source: IModelHeadlessTests.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS

#include <ITwinElementID.h>
#include <ITwinGeolocation.h>
#include <ITwinIModel.h>
#include <ITwinIModelEventHandler.h>
#include <ITwinIModel3DInfo.h>
#include <ITwinSavedView.h>
#include <ITwinTilesetAccess.h>
#include <ITwinUtilityLibrary.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <IncludeCesium3DTileset.h>
#include <Decoration/ITwinDecorationHelper.h>

#include <Tests/IModelHeadlessTestsHelper.h>
#include <Tests/IModelTestHelperImpl.h>
#include <Tests/ITwinAutomationTestBaseNoLogs.h>
#include <Tests/ITwinFileBasedMockServer.h>

#include <Engine/Texture2D.h>
#include <HAL/FileManager.h>
#include <HAL/PlatformFile.h>
#include <HAL/PlatformFileManager.h>
#include <Misc/LowLevelTestAdapter.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/ITwinAPI/ITwinMaterial.h>
#include <Compil/AfterNonUnrealIncludes.h>


class UIModelHeadlessTestsHelper::FImpl : public FIModelTestHelperImpl
{
public:
	static bool bInGeoLocatedIModelTest;

	FImpl(UIModelHeadlessTestsHelper& InOwner);
	~FImpl() { Cleanup(); }
	
	virtual void FillTestInfo(FITwinExportInfo& OutExportInfo,
		FString& OutSceneId,
		std::filesystem::path& OutRelativeCacheFolder,
		FInitOptions const& InOptions) const override;

	virtual void BindEvents() override;
	virtual void UnBindEvents() override;
	virtual int32 ExpectedLoadEvents(size_t SubTestIndex) const override;

private:
	UIModelHeadlessTestsHelper& Owner;
};

/*static*/
bool UIModelHeadlessTestsHelper::FImpl::bInGeoLocatedIModelTest = false;

UIModelHeadlessTestsHelper::FImpl::FImpl(UIModelHeadlessTestsHelper& InOwner)
	: Owner(InOwner)
{
	// List all elements used in the test (see the SelectElement section).
	if (bInGeoLocatedIModelTest)
	{
		ElementsToTest = { TEXT("0x5000000007c") };
	}
	else
	{
		ElementsToTest = { TEXT("0x59"), TEXT("0x5b"), TEXT("0x5d") };
	}
}

void UIModelHeadlessTestsHelper::FImpl::FillTestInfo(FITwinExportInfo& OutExportInfo,
	FString& OutSceneId,
	std::filesystem::path& OutRelativeCacheFolder,
	FInitOptions const& InOptions) const
{
	if (bInGeoLocatedIModelTest)
	{
		OutExportInfo.iTwinId = TEXT("cc04c77c-9da5-4699-873b-49dfaa074b5a");
		OutExportInfo.iModelId = TEXT("e9464c5d-f40a-4443-8652-47677a7a1089");
		OutExportInfo.DisplayName = TEXT("iModel for geo located test") + FString(InOptions.bUseLatestChangeset ? TEXT(" (latest changeset)") : TEXT(""));
		OutSceneId = TEXT("9714861e-56cb-4bfb-a0a7-ce5b5ead6c6b");

		OutRelativeCacheFolder = "FunctionalTests/GeoLocatedStation";
	}
	else
	{
		// Use the same iModel as in IModelRenderTest.
		OutExportInfo.Id = InOptions.bDiscoverExport ? TEXT("") : TEXT("a4feb6ee-bb1c-40d3-8659-a1765b1d179d");
		OutExportInfo.iTwinId = TEXT("5e15184e-6d3c-43fd-ad04-e28b4b39485e");
		OutExportInfo.iModelId = TEXT("b53cebea-451f-4433-942f-eabda9c11d21");
		OutExportInfo.DisplayName = TEXT("iModel for headless tests") + FString(InOptions.bUseLatestChangeset ? TEXT(" (latest changeset)") : TEXT(""));

		// Use the default scene of this iTwin.
		OutSceneId = InOptions.bDiscoverDefaultScene ? TEXT("") : TEXT("7e2059e4-8a48-4671-a67d-6da36d88d9bf");

		OutRelativeCacheFolder = "FunctionalTests/IModelRender";
	}
	OutExportInfo.ChangesetId = InOptions.bUseLatestChangeset ? TEXT("latest") : TEXT("");
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


int32 UIModelHeadlessTestsHelper::FImpl::ExpectedLoadEvents(size_t SubTestIndex) const
{
	switch (SubTestIndex)
	{
	case 0:
		return 2 /*iModel loaded + scene mapping filled*/
			+ (bShouldLoadScene ? 1 : 0)
			+ (CurrentOptions.bAutoLoadSavedViews ? 1 : 0);
	case 1:
		// new changeset detection: just the iModel loading event is waited.
		return 1;
	default:
		BE_ISSUE("unexpected sub-test index", SubTestIndex);
		return 0;
	}
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

void UIModelHeadlessTestsHelper::OnExtraRequestResponseReceived()
{
	FITwinIOAsyncCallbackPtr AsyncCallback = Impl->GetAsyncCallback();
	if (AsyncCallback)
	{
		AsyncCallback->OnRequestDone();
	}
	Impl->CleanupIfDone();
}

void UIModelHeadlessTestsHelper::OnSavedViewInfoRetrieved(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo)
{
	BE_ASSERT(bSuccess);

	BE_ASSERT(SavedView.Origin.Equals(FVector(-155.70294122338959, 144.47916100846396, 108.36823390983045)));
	BE_ASSERT(SavedView.Extents.Equals(FVector(414.95568847656250, 403.87248403521090, 207.47784423828125)));
	BE_ASSERT(SavedView.Angles.Equals(FRotator(-53.023407775510790, 125.55997527224881, 29.730651049277785)));

	BE_ASSERT(SavedViewInfo.Id == TEXT("ABIyuxws235Ku90nw9sCS05OGBVePG39Q60E4otLOUhe6us8tR9FM0SUL-q9qcEdIQ"));
	BE_ASSERT(SavedViewInfo.DisplayName == TEXT("view 001"));

	OnExtraRequestResponseReceived();
}

void UIModelHeadlessTestsHelper::OnSavedViewThumbnailRetrieved(FString const& SavedViewId, UTexture2D* Tex2D)
{
	BE_ASSERT(SavedViewId == TEXT("ABIyuxws235Ku90nw9sCS05OGBVePG39Q60E4otLOUhe6us8tR9FM0SUL-q9qcEdIQ"));
	BE_ASSERT(Tex2D != nullptr);
	if (Tex2D)
	{
		Tex2D->WaitForStreaming();
		BE_ASSERT(Tex2D->GetSizeX() == 300);
		BE_ASSERT(Tex2D->GetSizeY() == 169);
	}

	OnExtraRequestResponseReceived();
}

void UIModelHeadlessTestsHelper::OnSavedViewThumbnailUpdated(bool bSuccess, FString const& SavedViewId)
{
	BE_ASSERT(bSuccess);
	BE_ASSERT(SavedViewId == TEXT("ABIyuxws235Ku90nw9sCS05OGBVePG39Q60E4otLOUhe6us8tR9FM0SUL-q9qcEdIQ"));

	OnExtraRequestResponseReceived();
}

void UIModelHeadlessTestsHelper::OnSavedViewDeleted(bool bSuccess, FString SavedViewId, FString Response)
{
	BE_ASSERT(bSuccess);
	BE_ASSERT(SavedViewId == TEXT("ABIyuxws235Ku90nw9sCS05OGBVePG39Q60E4otLOUhe6us8tR9FM0SUL-q9qcEdIQ"));

	OnExtraRequestResponseReceived();
}

void UIModelHeadlessTestsHelper::OnElementPropertiesRetrieved(bool bSuccess, const FElementProperties& ElementProps, const FString& ElementId)
{
	BE_ASSERT(bSuccess);
	if (ElementId == TEXT("0x5000000007c"))
	{
		BE_ASSERT(ElementProps.Properties.Num() == 4
			&& ElementProps.Properties[3].Attributes.Num() == 9
			&& ElementProps.Properties[3].Attributes[4].Name == TEXT("Code Value")
			&& ElementProps.Properties[3].Attributes[4].Value == TEXT("fc9f936f-6b87-4047-a182-11aef78aa03a"));
	}
	else if (ElementId == TEXT("0x59"))
	{
		BE_ASSERT(ElementProps.Properties.Num() == 1
			&& ElementProps.Properties[0].Attributes.Num() == 4
			&& ElementProps.Properties[0].Attributes[3].Name == TEXT("Category")
			&& ElementProps.Properties[0].Attributes[3].Value == TEXT("zoupiLayer0"));
	}
	else
	{
		BE_ISSUE("unexpected Element id in OnElementPropertiesRetrieved", TCHAR_TO_ANSI(*ElementId));
	}
	OnExtraRequestResponseReceived();
}

void UIModelHeadlessTestsHelper::OnConfirmLoadNewChangeset(FString IModelId, FString IModelName, FString NewChangesetId, FString NewChangesetName)
{
	BE_ASSERT(IModelId == TEXT("b53cebea-451f-4433-942f-eabda9c11d21"));
	BE_ASSERT(IModelName == TEXT("UTF8_ADE_InstancesTest"));
	BE_ASSERT(NewChangesetId == TEXT("c0e4e57d54bf87ed664ea49d1a45a93d3e92c0c6"));
	BE_ASSERT(NewChangesetName == TEXT("#New beautiful changeset - Synchronized geometry and animation sequence."));

	AITwinIModel* IModel = Impl->GetIModel();
	if (ensure(IModel))
	{
		// Load the new changeset - here we first disable 4D querying, to avoid having to duplicate
		// all responses.
		IModel->SetSynchro4DAutoLoadSchedule(false);
		IModel->OnLoadNewChangesetConfirmation(NewChangesetId, true);
	}
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

namespace
{
	class FTestIModelEventHandler : public FITwinIModelEventHandler
	{
	public:
		int32 OffsetDetectedCount = 0;

		FTestIModelEventHandler() = default;

		virtual bool HandleOffsetDetected() override
		{
			OffsetDetectedCount++;
			return true;
		}
	};
}

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
		UTEST_NOT_NULL(TEXT("Check world"), World);
		UTEST_NOT_NULL(TEXT("Check decoration helper"), DecoHelper);
		UTEST_NOT_NULL(TEXT("Check iModel"), IModel);

		Helper.BindEvents();

		Helper.AddTestCallback(
			[this, AsyncCallback,
			IModel = TStrongObjectPtr<AITwinIModel>(IModel),
			DecoHelper = TStrongObjectPtr<AITwinDecorationHelper>(DecoHelper)]()
		{
			auto const ValidateLoadedIModel = [&]() -> bool
			{
				// Customize some events: typically, we don't want to reload the tileset when
				// changing the offset (that would reset the scene mapping used in other tests).
				std::shared_ptr<FITwinIModelEventHandler> EventHandler = std::make_shared<FTestIModelEventHandler>();
				FTestIModelEventHandler* TestEventHandler = static_cast<FTestIModelEventHandler*>(EventHandler.get());
				IModel->SetCustomEventHandler(EventHandler);

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

				// Add a few tests on ITwinTilesetAccess
				UWorld* const World = FITwinAPITestHelperBase::GetTestWorld();
				UTEST_NOT_NULL(TEXT("Check world on scene loaded"), World);
				int32 const NumIModelTilesets = ITwin::CountTilesetsOfModelType(EITwinModelType::IModel, World);
				UTEST_TRUE(TEXT("CountTilesetsOfModelType"), NumIModelTilesets > 0);
				{
					TUniquePtr<FITwinTilesetAccess> Access = IModel->MakeTilesetAccess();
					UTEST_TRUE(TEXT("Has tileset access"), Access.IsValid());
					UTEST_TRUE(TEXT("Has tileset"), Access->HasTileset());
					UTEST_EQUAL(TEXT("Model Link"), Access->GetModelLink(), std::make_pair(EITwinModelType::IModel, FString(TEXT("b53cebea-451f-4433-942f-eabda9c11d21"))));
					UTEST_EQUAL(TEXT("Model Type"), Access->GetModelType(), EITwinModelType::IModel);
					UTEST_FALSE(TEXT("IsTilesetHidden"), Access->IsTilesetHidden());
					auto const Quality = Access->GetTilesetQuality();
					UTEST_TRUE(TEXT("Tileset quality"), Quality > 0.f && Quality < 1.f);

					Access->SetTilesetQuality(1.2 * Quality);
					auto const NewQuality = Access->GetTilesetQuality();
					UTEST_TRUE(TEXT("New Tileset quality"), NewQuality > Quality);

					FVector Pos(-1.0), Rot(-1.0);
					Access->GetModelOffset(Pos, Rot);
					UTEST_EQUAL(TEXT("Offset Pos"), Pos, FVector::ZeroVector);
					UTEST_EQUAL(TEXT("Offset Rot"), Rot, FVector::ZeroVector);

					// Offset is now consistent between get and set...
					// Both are in meters and degrees, respectively.
					Access->SetModelOffset(FVector(1., 2., 3.), FVector(45., 0., 90.));
					UTEST_EQUAL(TEXT("New Offset Detected"), TestEventHandler->OffsetDetectedCount, 1);
					Access->GetModelOffset(Pos, Rot);
					UTEST_EQUAL(TEXT("New Offset Pos"), Pos, FVector(1., 2., 3.));
					UTEST_EQUAL(TEXT("New Offset Rot"), Rot, FVector(45., 0., 90.));

					// GetTilesetAccess should work with both the iModel actor or the tileset actor.
					ACesium3DTileset* Tileset = Access->GetMutableTileset();
					auto Access2 = ITwin::GetTilesetAccess(IModel.Get());
					bool const bIsSameAccess2 = Access2 && Access2->GetTileset() == Tileset;
					UTEST_TRUE(TEXT("Tileset access from iModel actor is the same"), bIsSameAccess2);

					auto Access3 = ITwin::GetTilesetAccess(Tileset);
					bool const bIsSameAccess3 = Access3 && Access3->GetTileset() == Tileset;
					UTEST_TRUE(TEXT("Tileset access from tileset actor is the same"), bIsSameAccess3);

					auto Access4 = ITwin::GetTilesetAccessFromModelLink(IModel->GetModelLink(), World);
					bool const bIsSameAccess4 = Access4 && Access4->GetTileset() == Tileset;
					UTEST_TRUE(TEXT("Tileset access from model link is the same"), bIsSameAccess4);

					FBox AABB = UITwinUtilityLibrary::GetUnrealAxisAlignBoundingBox(Tileset);
					UTEST_TRUE(TEXT("GetUnrealAxisAlignBoundingBox"), (bool)AABB.IsValid);
					UTEST_EQUAL_TOLERANCE(TEXT("AABB Min"), AABB.Min, FVector(-51210.0402796, -51200.0402704, -51210.0805421), 1e-7f);
					UTEST_EQUAL_TOLERANCE(TEXT("AABB Max"), AABB.Max, FVector(51200.0402717, 51210.0402783, 51200.0805578), 1e-7f);
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

				FTransform const IModelTsf = UITwinUtilityLibrary::GetIModelToUnrealTransform(IModel.Get());
				{
					UTEST_TRUE(TEXT("GetIModelToUnrealTransform"), IModelTsf.IsValid());
					auto const Location = IModelTsf.GetLocation();
					auto const Rotator = IModelTsf.Rotator();
					auto const Scale = IModelTsf.GetScale3D();
					UTEST_EQUAL_TOLERANCE(TEXT("IModel Location"), Location, FVector(-6667.5975038, 7939.4499217, -9044.4565289), 1e-7f);
					UTEST_EQUAL_TOLERANCE(TEXT("IModel Pitch"), Rotator.Pitch, 0.0, 1e-7);
					UTEST_EQUAL_TOLERANCE(TEXT("IModel Yaw"), Rotator.Yaw, -90.0, 1e-7);
					UTEST_EQUAL_TOLERANCE(TEXT("IModel Roll"), Rotator.Roll, -45.0, 1e-7);
					UTEST_EQUAL_TOLERANCE(TEXT("IModel Scale"), Scale, FVector(-100., 100., 100.), 1e-7f);
				}

				IModel->ShowConstructionData(false);
				IModel->HideElements({ "0x59", "0x5d", "0x5b" }, false);
				IModel->ShowElements({ "0x59", "0x5d", "0x5b" }, false);
				IModel->HideCategories({ "0x31" }, false);
				IModel->HideModels({ "0x1c" }, false);
				IModel->HideCategoriesPerModel({ "0x1c" }, { "0x33" }, false);
				IModel->ShowCategoriesPerModel({ "0x1c" }, { "0x33" }, false);

				FSavedView SV;
				SV.HiddenCategories.Add(TEXT("0x31"));
				SV.HiddenElements.Add(TEXT("0x59"));
				SV.AlwaysDrawnCategoriesPerModel.Add(FPerModelCategoryVisibilityProps{
					.ModelId = TEXT("0x1c"),
					.CategoryId = TEXT("0x33")
				});
				SV.AlwaysDrawnElements.Add(TEXT("0x5d"));
				AITwinSavedView::HideElements(IModel.Get(), SV);

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

				IModel->GetElementProperties(TEXT("0x59"));
				AsyncCallback->OnRequestStarted();

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

				const uint64 Id38 = 38;
				IModel->HighlightMaterial(Id38);
				UTEST_EQUAL(TEXT("GetSelectedMaterial"), IModel->GetSelectedMaterial(), Id38);
				IModel->DeSelectMaterials();
				UTEST_EQUAL(TEXT("GetSelectedMaterial"), IModel->GetSelectedMaterial(), ITwin::NOT_MATERIAL.getValue());

				// Both element and material selected.
				IModel->HighlightMaterial(Id38);
				IModel->AddElementsToSelection({ TEXT("0x59") });
				UTEST_TRUE(TEXT("SelectElement"), IModel->IsElementSelected(TEXT("0x59")));
				UTEST_EQUAL(TEXT("GetSelectedMaterial"), IModel->GetSelectedMaterial(), Id38);
				IModel->DeSelectAll();
				UTEST_FALSE(TEXT("SelectElement"), IModel->IsElementSelected(TEXT("0x59")));
				UTEST_EQUAL(TEXT("GetSelectedMaterial"), IModel->GetSelectedMaterial(), ITwin::NOT_MATERIAL.getValue());

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
		.bAutoLoadSavedViews = true,
		.bAutoRefreshChangesets = true }))
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

		Helper.AddTestCallback(
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
				AITwinSavedView* SV_001 = IModel->GetITwinSavedViewActor(TEXT("ABIyuxws235Ku90nw9sCS05OGBVePG39Q60E4otLOUhe6us8tR9FM0SUL-q9qcEdIQ"));
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

				if (SV_001)
				{
					// Test requests on a saved view. Each one will add an additional request to the queue,
					// so we need to call OnRequestStarted.

					UIModelHeadlessTestsHelper& HelperObject = UIModelHeadlessTestsHelper::Instance();
					SV_001->RetrievedInfoEvent.AddUniqueDynamic(&HelperObject, &UIModelHeadlessTestsHelper::OnSavedViewInfoRetrieved);
					SV_001->UpdateSavedView();
					AsyncCallback->OnRequestStarted();

					SV_001->RetrievedThumbnail.AddUniqueDynamic(&HelperObject, &UIModelHeadlessTestsHelper::OnSavedViewThumbnailRetrieved);
					SV_001->GetThumbnail();
					AsyncCallback->OnRequestStarted();

					// Replace the thumbnail with a new one.
					// Note that in practice, we should not be replacing the thumbnail while a request to get
					// the thumbnail is in progress, but the mock server ignores the order here :-)
					const FString TexturePath = FPaths::ProjectDir() / "../../../Public/SDK/Core/Visualization/Tests/UT_TextureToUpload.png";
					UTEST_TRUE(TEXT("texture exists"), FPlatformFileManager::Get().GetPlatformFile().FileExists(*TexturePath));
					SV_001->UpdatedThumbnailEvent.AddUniqueDynamic(&HelperObject, &UIModelHeadlessTestsHelper::OnSavedViewThumbnailUpdated);
					SV_001->UpdateThumbnail(TexturePath);
					AsyncCallback->OnRequestStarted();

					// Same for deletion. In practice, we should not be deleting the saved view while
					// requests to get/modify it are in progress...
					// Note that the event is forwarded to the parent iModel.
					IModel->GetMutableWebServices()->OnDeleteSavedViewComplete
						.AddUniqueDynamic(&HelperObject, &UIModelHeadlessTestsHelper::OnSavedViewDeleted);
					SV_001->DeleteSavedView();
					AsyncCallback->OnRequestStarted();
				}

				return true;
			};

			if (ValidateLoadedIModel())
			{
				UIModelHeadlessTestsHelper& HelperObject = UIModelHeadlessTestsHelper::Instance();

				// Bind the new changeset event to the helper object, so that it can be handled in the test.
				IModel->ConfirmLoadNewChangeset.AddUniqueDynamic(&HelperObject, &UIModelHeadlessTestsHelper::OnConfirmLoadNewChangeset);

				// Modify the mock server so that the auto-refresh detects a new changeset.
				HelperObject.GetImpl().RedirectMockServerResponse(
					"56d8aa50361e7894", "new_changeset");
				// Wait for the auto-refresh to detect the new changeset.
				AsyncCallback->OnRequestStarted();
			}

			AsyncCallback->OnRequestDone();
		});

		// Add a second callback to test the automatic refresh of the latest changeset.
		Helper.AddTestCallback(
			[this, AsyncCallback,
			IModel = TStrongObjectPtr<AITwinIModel>(IModel),
			DecoHelper = TStrongObjectPtr<AITwinDecorationHelper>(DecoHelper)]()
		{
			auto const ValidateNewChangeset = [&]() -> bool
			{
				UTEST_EQUAL(TEXT("GetSelectedChangeset"), IModel->GetSelectedChangeset(), FString(TEXT("c0e4e57d54bf87ed664ea49d1a45a93d3e92c0c6")));
				UTEST_EQUAL(TEXT("GetExportID"), IModel->GetExportID(), FString(TEXT("b4feeeee-bb1c-40d3-8659-a1765b1d179d")));
				return true;
			};
			ValidateNewChangeset();

			AsyncCallback->OnRequestDone();
		});

		AsyncCallback->OnRequestStarted();
	}

	ADD_LATENT_AUTOMATION_COMMAND(FNUTWaitForAsyncIModelHeadlessTest(AsyncCallback));

	return true;
}


IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FIModelGeoLocatedTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.IModelGeoLocated", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIModelGeoLocatedTest::RunTest(const FString& /*Parameters*/)
{
	UIModelHeadlessTestsHelper::FImpl::bInGeoLocatedIModelTest = true;
	ON_SCOPE_EXIT
	{
		UIModelHeadlessTestsHelper::FImpl::bInGeoLocatedIModelTest = false;
	};

	UWorld* const World = FITwinAPITestHelperBase::GetTestWorld();
	if (!World)
	{
		BE_ISSUE("Test world is not valid");
		return false;
	}
	FITwinGeolocation::UnlockLocation(*World); // avoid conflicts with other tests loading geo-located models.

	UIModelHeadlessTestsHelper& HelperObject = UIModelHeadlessTestsHelper::Instance();

	auto& Helper = HelperObject.GetImpl();

	if (!Helper.Init(FIModelTestOptions{
		.bUseLatestChangeset = true,
		.bDiscoverExport = true,
		.bDiscoverDefaultScene = false,
		.bAutoLoadSavedViews = false }))
	{
		return false;
	}

	auto* DecoHelper = Helper.GetDecorationHelper();
	auto* IModel = Helper.GetIModel();
	FITwinIOAsyncCallbackPtr AsyncCallback = Helper.GetAsyncCallback();


	SECTION("Load geo-located IModel")
	{
		UTEST_TRUE(TEXT("Check decoration helper"), DecoHelper != nullptr);
		UTEST_TRUE(TEXT("Check iModel"), IModel != nullptr);

		Helper.BindEvents();

		Helper.AddTestCallback(
			[this, AsyncCallback,
			IModel = TStrongObjectPtr<AITwinIModel>(IModel),
			DecoHelper = TStrongObjectPtr<AITwinDecorationHelper>(DecoHelper)]()
		{
			auto const ValidateLoadedIModel = [&]() -> bool
			{
				UTEST_NOT_NULL(TEXT("Check Synchro4D schedules"), IModel->GetSynchro4DSchedules());

				TUniquePtr<FITwinTilesetAccess> Access = IModel->MakeTilesetAccess();
				UTEST_TRUE(TEXT("Has tileset access"), Access.IsValid());
				if (Access.IsValid())
				{
					UTEST_TRUE(TEXT("Has tileset"), Access->HasTileset());
					auto AccessClone = Access->Clone();

					auto Box = AccessClone->GetBoundingBox();
					UTEST_EQUAL(TEXT("Bounding box min"), Box.Min, FVector(-8368.3425673942329, -77641.821777780657, 3533.1325028092297));
					UTEST_EQUAL(TEXT("Bounding box max"), Box.Max, FVector(8368.3425668130876, 77641.821742852335, 16466.867517238526));

					auto CartographicProps = AccessClone->GetNativeGeoreference();
					UTEST_TRUE(TEXT("Has georeference"), CartographicProps.has_value());
					UTEST_EQUAL(TEXT("Georeference Longitude"), CartographicProps->Longitude, -122.13104950739952);
					UTEST_EQUAL(TEXT("Georeference Latitude"), CartographicProps->Latitude, 47.641538442597998);
					UTEST_EQUAL(TEXT("Georeference Height"), CartographicProps->Height, 64.664427093607856);
				}

				// The geo-location singleton should have been created and initialized by the iModel.
				UWorld* const World = FITwinAPITestHelperBase::GetTestWorld();
				UTEST_NOT_NULL(TEXT("Check world"), World);
				auto&& Geoloc = FITwinGeolocation::Get(*World);
				UTEST_TRUE(TEXT("Check geolocation"), Geoloc->GeoReference.IsValid());
				UTEST_FALSE(TEXT("geolocation is locked"), Geoloc->bCanBypassCurrentLocation);
				UTEST_EQUAL(TEXT("geolocation coordinates"), Geoloc->GeoReference->GetOriginLongitudeLatitudeHeight(),
					FVector(-122.13104950739952, 47.641538442597998, 42.472044957053953));
				return true;
			};

			auto const TestIModel = [&]() -> bool
			{
				// Select an element with a timeline.
				if (IModel->SelectElement(TEXT("0x5000000007c")))
				{
					// SelectElement triggers a request to describe the element, so we should wait for the
					// request to be processed before exiting the test.
					// (see bLogPropertiesUponSelectElement in FITwinIModelInternals::DescribeElement).
					AsyncCallback->OnRequestStarted();
				}

				UTEST_TRUE(TEXT("SelectElement"), IModel->IsElementSelected(TEXT("0x5000000007c")));

				// Reset destroys the tileset.
				IModel->Reset();
				UTEST_FALSE(TEXT("Has tileset"), IModel->MakeTilesetAccess()->HasTileset());

				return true;
			};

			if (ValidateLoadedIModel())
			{
				TestIModel();
			}

			AsyncCallback->OnRequestDone();
		});

		AsyncCallback->OnRequestStarted();
	}

	ADD_LATENT_AUTOMATION_COMMAND(FNUTWaitForAsyncIModelHeadlessTest(AsyncCallback));

	return true;
}
#endif // WITH_EDITOR

#endif // WITH_TESTS
