/*--------------------------------------------------------------------------------------+
|
|     $Source: SelectionOutlineTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS && WITH_EDITOR

#include <Tests/ITwinAutomationTestBaseNoLogs.h>

#include <ITwinSceneMapping.h>
#include <ITwinDynamicShadingProperty.inl>
#include <ITwinSelectionOutline.h>
#include <ITwinSelectionOutlineRenderer.h>

#include <Components/SceneCaptureComponent2D.h>
#include <Components/StaticMeshComponent.h>
#include <Editor.h>
#include <EditorViewportClient.h>
#include <Engine/StaticMesh.h>
#include <Engine/TextureRenderTarget2D.h>
#include <Engine/Texture2D.h>
#include <Engine/World.h>
#include <StaticMeshDescription.h>
#include <GameFramework/Actor.h>
#include <Materials/MaterialInstanceDynamic.h>
#include <Materials/MaterialInstanceConstant.h>
#include <Materials/MaterialInterface.h>
#include <Materials/Material.h>
#include <Materials/MaterialExpressionConstant.h>
#include <Materials/MaterialExpressionConstant3Vector.h>
#include <Materials/MaterialExpressionCustom.h>
#include <Materials/MaterialExpressionLinearInterpolate.h>
#include <Materials/MaterialExpressionScalarParameter.h>
#include <Materials/MaterialExpressionTextureObjectParameter.h>
#include <Materials/MaterialExpressionVectorParameter.h>
#include <Materials/MaterialFunction.h>
#include <Misc/AutomationTest.h>
#include <Misc/LowLevelTestAdapter.h>
#include <Misc/ScopeExit.h>
#include <RenderingThread.h>
#include <PreviewScene.h>
#include <RHICommandList.h>
#include <ShaderCompiler.h>
#include <Slate/SceneViewport.h>
#include <StaticMeshResources.h>
#include <TextureResource.h>
#include <UnrealClient.h>

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinSelectionOutlineTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.SelectionOutline", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinMaterialHoverTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.SelectionOutlineMaterialHover", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinMaterialHoverFormulaTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.SelectionOutlineMaterialHoverFormula", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinSelectionOutlineOverridesTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.SelectionOutlineOverrides", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinSelectionOutlineBoundsTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.SelectionOutlineBounds", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	ITwinElementID const kElem1(1001);
	ITwinElementID const kElem2(1002);
	ITwinRenderMaterialElementID const kMat1(2001);

	/// A transient actor holding one "Cesium-like" primitive: cube mesh + masked iTwin material instance.
	class [[nodiscard]] FOutlineFixture
	{
	public:
		explicit FOutlineFixture(UWorld* OverrideWorld = nullptr)
		{
			World = OverrideWorld ? OverrideWorld : (GEditor ? GEditor->GetEditorWorldContext().World() : GWorld);
			if (!World)
				return;
			auto* const Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			auto* const BaseMat = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/ITwinForUnreal/ITwin/Materials/MI_ITwinInstance"));
			if (!Cube || !BaseMat)
				return;
			Actor = World->SpawnActor<AActor>();
			if (!Actor)
				return;
			MeshComp = NewObject<UStaticMeshComponent>(Actor, TEXT("Primitive"), RF_Transient);
			MeshComp->SetMobility(EComponentMobility::Movable);
			MeshComp->SetStaticMesh(Cube);
			auto* Material = UMaterialInstanceDynamic::Create(BaseMat, MeshComp);
			Material->SetScalarParameterValueByInfo(FMaterialParameterInfo(TEXT("_FEATURE_ID_0"),
				EMaterialParameterAssociation::BlendParameter, 2), 0.f);
			TArray<FMaterialParameterInfo> Parameters;
			TArray<FGuid> ParameterIds;
			Material->GetAllScalarParameterInfo(Parameters, ParameterIds);
			for (auto const& Parameter : Parameters)
			{
				if (Parameter.Name == TEXT("FadePercentage"))
					Material->SetScalarParameterValueByInfo(Parameter, 1.f);
				else if (Parameter.Name == TEXT("FadingType"))
					Material->SetScalarParameterValueByInfo(Parameter, 0.f);
			}
			MeshComp->SetMaterial(0, Material);
			if (GShaderCompilingManager)
				GShaderCompilingManager->FinishAllCompilation();
			FlushRenderingCommands();
			Actor->SetRootComponent(MeshComp);
			MeshComp->RegisterComponent();
		}

		~FOutlineFixture()
		{
			if (::IsValid(Actor))
				Actor->Destroy();
		}

		FOutlineFixture(FOutlineFixture const&) = delete;
		FOutlineFixture& operator=(FOutlineFixture const&) = delete;

		bool IsValidFixture() const
		{
			return World && ::IsValid(Actor) && ::IsValid(MeshComp)
				&& MeshComp->GetMaterial(0) && MeshComp->GetMaterial(0)->GetBlendMode() == BLEND_Masked;
		}

		/// Tile with two Elements and one Material, all in mesh #0 (our only primitive)
		FITwinSceneTile MakeTile() const
		{
			FITwinSceneTile Tile(CesiumTileID{});
			Tile.MaxFeatureID = ITwinFeatureID(50);
			Tile.bVisible = true;
			auto& Feat1 = Tile.ElementFeaturesSLOW(kElem1);
			Feat1.Features.push_back(ITwinFeatureID(10));
			Feat1.Features.push_back(ITwinFeatureID(11));
			Feat1.Meshes.push_back(0);
			auto& Feat2 = Tile.ElementFeaturesSLOW(kElem2);
			Feat2.Features.push_back(ITwinFeatureID(20));
			Feat2.Meshes.push_back(0);
			auto& MatFeat = Tile.MaterialFeaturesSLOW(kMat1);
			MatFeat.Features.insert(ITwinFeatureID(10));
			MatFeat.Features.insert(ITwinFeatureID(30));
			MatFeat.Meshes.push_back(0);
			Tile.GltfMeshWrappers().emplace_back(*MeshComp, /*ITwinMaterialID*/1234);
			return Tile;
		}

		int32 NumStaticMeshComponents() const
		{
			TArray<UStaticMeshComponent*> Comps;
			Actor->GetComponents<UStaticMeshComponent>(Comps);
			return Comps.Num();
		}

		int32 NumCompanions() const { return MeshComp->GetAttachChildren().Num(); }

		UStaticMeshComponent* GetMeshComp() const { return MeshComp; }

	private:
		UWorld* World = nullptr;
		AActor* Actor = nullptr;
		UStaticMeshComponent* MeshComp = nullptr;
	};

	struct FMaterialViewportFixture
	{
		FPreviewScene Scene{ FPreviewScene::ConstructionValues() };
		FOutlineFixture Fixture{ Scene.GetWorld() };
		FEditorViewportClient Client{ nullptr, &Scene };
		FSceneViewport Viewport{ &Client, nullptr };

		FMaterialViewportFixture()
		{
			Client.Viewport = &Viewport;
			Client.SetViewportType(LVT_Perspective);
			Client.SetViewLocation(FVector(-300, 0, 0));
			Client.SetViewRotation(FRotator::ZeroRotator);
			Viewport.UpdateViewportRHI(false, 256, 256, EWindowMode::Windowed, PF_B8G8R8A8);
		}

		~FMaterialViewportFixture() { Client.Viewport = nullptr; }
	};

	void Select(FITwinSceneTile& Tile, ITwinElementID const& Elem, bool const bAddToSelection = false)
	{
		FITwinSceneTile::FTextureNeeds TextureNeeds;
		Tile.PickElement(Elem, TextureNeeds,
			FPickingOptions().MakeSelected(true).SkipResetSelection(bAddToSelection));
	}

	void SelectMaterial(FITwinSceneTile& Tile, ITwinRenderMaterialElementID const& Mat)
	{
		FITwinSceneTile::FTextureNeeds TextureNeeds;
		Tile.PickMaterial(Mat, TextureNeeds, FPickingOptions().MakeSelected(true));
	}

	void SyncMaterialHover(FITwinSceneTile& Tile)
	{
		constexpr int32 MaxUploadRetries = 8;
		for (int32 Retry = 0; Retry <= MaxUploadRetries; ++Retry)
		{
			Tile.SyncMaterialHover();
			if (!Tile.HoverTextureNeedsUploadForUnitTest())
				return;
			if (Retry == MaxUploadRetries)
				break;
			ENQUEUE_RENDER_COMMAND(FlushMaterialHoverUpload)([](FRHICommandListImmediate& CommandList)
			{
				CommandList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
			});
			FlushRenderingCommands();
		}
		ensureMsgf(false, TEXT("Material hover texture upload did not complete after %d retries"), MaxUploadRetries);
	}

	UMaterialInstanceDynamic* MakeGlassMaterial(UStaticMeshComponent& Mesh)
	{
		auto* Parent = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/ITwinForUnreal/ITwin/Materials/MI_ITwinInstanceTranslucent"));
		if (!Parent)
			return nullptr;
		auto* Glass = NewObject<UMaterialInstanceConstant>(GetTransientPackage(), NAME_None, RF_Transient);
		Glass->SetParentEditorOnly(Parent);
		Glass->BasePropertyOverrides.bOverride_BlendMode = true;
		Glass->BasePropertyOverrides.BlendMode = BLEND_Translucent;
		Glass->PostEditChange();
		auto* Material = UMaterialInstanceDynamic::Create(Glass, &Mesh);
		TArray<FMaterialParameterInfo> Parameters;
		TArray<FGuid> ParameterIds;
		Material->GetAllScalarParameterInfo(Parameters, ParameterIds);
		for (auto const& Parameter : Parameters)
		{
			if (Parameter.Name == TEXT("FadePercentage"))
				Material->SetScalarParameterValueByInfo(Parameter, 1.f);
			else if (Parameter.Name == TEXT("FadingType"))
				Material->SetScalarParameterValueByInfo(Parameter, 0.f);
		}
		FITwinSceneMapping::SetForcedOpacity(Material, -1.f);
		return Material;
	}

	UStaticMesh* MakeTwoSectionMesh(UObject* Outer, UMaterialInterface* FirstMaterial,
		UMaterialInterface* SecondMaterial)
	{
		if (!FirstMaterial || !SecondMaterial)
			return nullptr;
		auto* Mesh = NewObject<UStaticMesh>(Outer, NAME_None, RF_Transient);
		if (!Mesh)
			return nullptr;
		auto* Description = UStaticMesh::CreateStaticMeshDescription(Mesh);
		if (!Description)
			return nullptr;
		FName const FirstSlotName(TEXT("Solid"));
		FName const SecondSlotName(TEXT("Glass"));
		auto const FirstGroup = Description->CreatePolygonGroup();
		auto const SecondGroup = Description->CreatePolygonGroup();
		Description->SetPolygonGroupMaterialSlotName(FirstGroup, FirstSlotName);
		Description->SetPolygonGroupMaterialSlotName(SecondGroup, SecondSlotName);
		FPolygonID FirstCubeFaces[6];
		FPolygonID SecondCubeFaces[6];
		Description->CreateCube(FVector(-60.f, 0.f, 0.f), FVector(50.f), FirstGroup,
			FirstCubeFaces[0], FirstCubeFaces[1], FirstCubeFaces[2], FirstCubeFaces[3], FirstCubeFaces[4], FirstCubeFaces[5]);
		Description->CreateCube(FVector(60.f, 0.f, 0.f), FVector(50.f), SecondGroup,
			SecondCubeFaces[0], SecondCubeFaces[1], SecondCubeFaces[2], SecondCubeFaces[3], SecondCubeFaces[4], SecondCubeFaces[5]);
		Mesh->GetStaticMaterials().Add(FStaticMaterial(FirstMaterial, FirstSlotName, FirstSlotName));
		Mesh->GetStaticMaterials().Add(FStaticMaterial(SecondMaterial, SecondSlotName, SecondSlotName));
		TArray<UStaticMeshDescription*> MeshDescriptions{ Description };
		Mesh->BuildFromStaticMeshDescriptions(MeshDescriptions, /*bBuildSimpleCollision*/false,
			/*bFastBuild*/true);
		auto const* RenderData = Mesh->GetRenderData();
		if (!RenderData || RenderData->LODResources.IsEmpty()
			|| RenderData->LODResources[0].Sections.Num() != 2)
			return nullptr;
		return Mesh;
	}

	UTexture* SelectionTexture(UMaterialInterface& Material)
	{
		ITwinMatParamInfo::SetupSelectingAndHidingInfo();
		UTexture* Texture = nullptr;
		Material.GetTextureParameterValue(*ITwinMatParamInfo::SelectingAndHidingInfo, Texture);
		return Texture;
	}
}

bool FITwinMaterialHoverFormulaTest::RunTest(const FString& /*Parameters*/)
{
	if (GUsingNullRHI)
	{
		AddWarning(TEXT("Material hover formula readback requires a real RHI"));
		return true;
	}
	auto* Function = LoadObject<UMaterialFunction>(nullptr,
		TEXT("/ITwinForUnreal/ITwin/Materials/MLB_ITwinBlend"));
	if (!TestNotNull("Canonical blend function exists", Function))
		return false;
	UMaterialExpressionCustom* Production = nullptr;
	for (auto const& Expression : Function->GetExpressions())
		if (auto* Custom = Cast<UMaterialExpressionCustom>(Expression))
			if (Custom->Desc == TEXT("Additive linear material hover"))
				Production = Custom;
	if (!TestNotNull("Production additive graph node exists", Production))
		return false;
	UMaterialExpressionCustom* FeatureProduction = nullptr;
	for (auto const& Expression : Function->GetExpressions())
		if (auto* Custom = Cast<UMaterialExpressionCustom>(Expression))
			if (Custom->Code.Contains(TEXT("GetPerFeatureValues.ush")))
				FeatureProduction = Custom;
	if (!TestNotNull("Production per-feature helper exists", FeatureProduction))
		return false;
	FPreviewScene PreviewScene{ FPreviewScene::ConstructionValues() };
	FOutlineFixture Fixture(PreviewScene.GetWorld());
	if (!TestTrue("Formula fixture is valid", Fixture.IsValidFixture()))
		return false;
	auto* Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	Material->SetShadingModel(MSM_Unlit);
	auto* Color = NewObject<UMaterialExpressionVectorParameter>(Material);
	Color->ParameterName = TEXT("TestColor");
	auto* Mask = NewObject<UMaterialExpressionScalarParameter>(Material);
	Mask->ParameterName = TEXT("TestMask");
	auto* Feature = NewObject<UMaterialExpressionCustom>(Material);
	Feature->Code = FeatureProduction->Code;
	Feature->OutputType = FeatureProduction->OutputType;
	Feature->AdditionalOutputs = FeatureProduction->AdditionalOutputs;
	Feature->RebuildOutputs();
	int32 HoverOutput = INDEX_NONE;
	for (int32 Index = 0; Index < Feature->AdditionalOutputs.Num(); ++Index)
		if (Feature->AdditionalOutputs[Index].OutputName == TEXT("HoverMask"))
			HoverOutput = Index + 1;
	if (!TestTrue("Production helper exposes hover mask", HoverOutput != INDEX_NONE))
		return false;
	auto* FeatureID = NewObject<UMaterialExpressionScalarParameter>(Material);
	FeatureID->ParameterName = TEXT("TestFeatureID");
	auto* Blink = NewObject<UMaterialExpressionScalarParameter>(Material);
	Blink->ParameterName = TEXT("TestBlink");
	Blink->DefaultValue = 0.7f;
	Material->GetExpressionCollection().AddExpression(Feature);
	Material->GetExpressionCollection().AddExpression(FeatureID);
	Material->GetExpressionCollection().AddExpression(Blink);
	auto const MakeDataTexture = [](FLinearColor const& Value)
	{
		auto* Texture = UTexture2D::CreateTransient(2, 1, PF_A32B32G32R32F);
		Texture->SRGB = false;
		auto& Mip = Texture->GetPlatformData()->Mips[0];
		auto* Data = static_cast<FLinearColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		Data[0] = Data[1] = Value;
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	};
	for (auto const& Input : FeatureProduction->Inputs)
	{
		auto& Copy = Feature->Inputs.AddDefaulted_GetRef();
		Copy.InputName = Input.InputName;
		if (Input.InputName == TEXT("FeatureID") || Input.InputName == TEXT("BlinkingFactor"))
		{
			Copy.Input.Connect(0, Input.InputName == TEXT("FeatureID") ? FeatureID : Blink);
			continue;
		}
		auto* Texture = NewObject<UMaterialExpressionTextureObjectParameter>(Material);
		Texture->ParameterName = Input.InputName;
		Texture->Texture = MakeDataTexture(FLinearColor(0.f, 0.f, 0.f, 1.f));
		Texture->SamplerType = SAMPLERTYPE_LinearColor;
		Material->GetExpressionCollection().AddExpression(Texture);
		Copy.Input.Connect(0, Texture);
	}
	auto* UseFeature = NewObject<UMaterialExpressionScalarParameter>(Material);
	UseFeature->ParameterName = TEXT("TestUseFeatureMask");
	auto* EffectiveMask = NewObject<UMaterialExpressionLinearInterpolate>(Material);
	EffectiveMask->A.Connect(0, Mask);
	EffectiveMask->B.Connect(HoverOutput, Feature);
	EffectiveMask->Alpha.Connect(0, UseFeature);
	Material->GetExpressionCollection().AddExpression(UseFeature);
	Material->GetExpressionCollection().AddExpression(EffectiveMask);
	auto* Formula = NewObject<UMaterialExpressionCustom>(Material);
	Formula->Code = Production->Code;
	Formula->OutputType = CMOT_Float3;
	for (auto const& Input : Production->Inputs)
	{
		auto& Copy = Formula->Inputs.AddDefaulted_GetRef();
		Copy.InputName = Input.InputName;
		Copy.Input.Connect(0, Input.InputName == TEXT("BaseColor") ? static_cast<UMaterialExpression*>(Color)
			: static_cast<UMaterialExpression*>(EffectiveMask));
	}
	Material->GetExpressionCollection().AddExpression(Color);
	Material->GetExpressionCollection().AddExpression(Mask);
	Material->GetExpressionCollection().AddExpression(Formula);
	Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Formula);
	Material->PostEditChange();
	Material->ForceRecompileForRendering();
	if (GShaderCompilingManager)
		GShaderCompilingManager->FinishAllCompilation();
	FlushRenderingCommands();
	TestTrue("Formula material shader map is ready", Material->IsCompilingOrHadCompileError(GMaxRHIFeatureLevel) == false);
	auto* Mesh = Fixture.GetMeshComp();
	auto* Instance = UMaterialInstanceDynamic::Create(Material, Mesh);
	Mesh->SetMaterial(0, Instance);
	auto* Capture = NewObject<USceneCaptureComponent2D>(Mesh->GetOwner(), NAME_None, RF_Transient);
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->CaptureSource = SCS_SceneColorHDR;
	Capture->TextureTarget = NewObject<UTextureRenderTarget2D>(Capture, NAME_None, RF_Transient);
	Capture->TextureTarget->InitCustomFormat(64, 64, PF_FloatRGBA, false);
	Capture->SetWorldLocationAndRotation(Mesh->GetComponentLocation() - FVector(300, 0, 0), FRotator::ZeroRotator);
	Capture->RegisterComponent();
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetFog(false);
	auto const Check = [&](TCHAR const* Label, FLinearColor const& Base, float Hover,
		FLinearColor const& Expected)
	{
		Instance->SetVectorParameterValue(TEXT("TestColor"), Base);
		Instance->SetScalarParameterValue(TEXT("TestMask"), Hover);
		PreviewScene.GetWorld()->SendAllEndOfFrameUpdates();
		FlushRenderingCommands();
		Capture->CaptureScene();
		FlushRenderingCommands();
		TArray<FLinearColor> Pixels;
		TestTrue("Formula GPU readback", Capture->TextureTarget->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels));
		if (Pixels.IsValidIndex(32 * 64 + 32))
		{
			auto const Actual = Pixels[32 * 64 + 32];
			AddInfo(FString::Printf(TEXT("%s actual=%s expected=%s"), Label, *Actual.ToString(), *Expected.ToString()));
			TestTrue(Label, FMath::IsNearlyEqual(Actual.R, Expected.R, 0.003f)
				&& FMath::IsNearlyEqual(Actual.G, Expected.G, 0.003f)
				&& FMath::IsNearlyEqual(Actual.B, Expected.B, 0.003f));
		}
	};
	Check(TEXT("Fixed equal-channel addition"), FLinearColor(0.2f, 0.4f, 0.6f), 1.f, FLinearColor(0.4f, 0.6f, 0.8f));
	Check(TEXT("Near-white darkens with the fixed shift"), FLinearColor(0.9f, 0.95f, 1.f), 1.f, FLinearColor(0.7f, 0.75f, 0.8f));
	Check(TEXT("Near-white channels subtract the fixed shift"), FLinearColor(0.98f, 0.99f, 1.f), 1.f,
		FLinearColor(0.78f, 0.79f, 0.8f));
	Check(TEXT("No hover leaves color unchanged"), FLinearColor(0.2f, 0.4f, 1.2f), 0.f, FLinearColor(0.2f, 0.4f, 1.2f));
	Check(TEXT("Dark colors add the fixed shift"), FLinearColor(0.01f, 0.02f, 0.03f), 1.f, FLinearColor(0.21f, 0.22f, 0.23f));
	Check(TEXT("Mid colors add the fixed shift"), FLinearColor(0.2f, 0.4f, 0.6f), 1.f, FLinearColor(0.4f, 0.6f, 0.8f));
	Check(TEXT("Inclusive near-white threshold subtracts the fixed shift"), FLinearColor(0.8f, 0.9f, 1.f), 1.f, FLinearColor(0.6f, 0.7f, 0.8f));
	Check(TEXT("One channel below threshold stays additive"), FLinearColor(1.f, 0.799f, 0.9f), 1.f,
		FLinearColor(1.f, 0.999f, 1.f));
	Check(TEXT("Bright saturated yellow stays additive"), FLinearColor(1.f, 1.f, 0.f), 1.f, FLinearColor(1.f, 1.f, 0.2f));
	Check(TEXT("Bright saturated green stays additive"), FLinearColor(0.f, 1.f, 0.f), 1.f, FLinearColor(0.2f, 1.f, 0.2f));
	Check(TEXT("Near-white subtraction uses the fixed magnitude"), FLinearColor(0.8f, 0.9f, 1.f), 1.f,
		FLinearColor(0.6f, 0.7f, 0.8f));
	Check(TEXT("No-hover bypass leaves HDR color unchanged"), FLinearColor(0.9f, 1.f, 1.2f), 0.f, FLinearColor(0.9f, 1.f, 1.2f));
	Instance->SetScalarParameterValue(TEXT("TestUseFeatureMask"), 1.f);
	auto const CheckFeature = [&](TCHAR const* Label, FLinearColor const& Selection,
		FLinearColor const& Schedule, FLinearColor const& Expected)
	{
		Instance->SetTextureParameterValue(TEXT("Selection_RGBA_DATA"), MakeDataTexture(Selection));
		Instance->SetTextureParameterValue(TEXT("Synchro4D_RGBA_DATA"), MakeDataTexture(Schedule));
		Check(Label, FLinearColor(0.9f, 0.95f, 1.f), 1.f, Expected);
	};
	FLinearColor const Original(0.9f, 0.95f, 1.f);
	FLinearColor const Darkened(0.7f, 0.75f, 0.8f);
	float const HoverMarkerByte = static_cast<float>(ITwin::HOVER_MARKER_ALPHA);
	FLinearColor const HoverMarker(0.f, 0.f, 0.f, HoverMarkerByte / 255.f);
	FLinearColor const BelowHoverMarker(0.f, 0.f, 0.f, (HoverMarkerByte - 1.f) / 255.f);
	FLinearColor const AboveHoverMarker(0.f, 0.f, 0.f, (HoverMarkerByte + 1.f) / 255.f);
	FLinearColor const NeutralSchedule(0.f, 0.f, 0.f, 1.f);
	CheckFeature(TEXT("Production hover marker enables fixed darkening"), HoverMarker, NeutralSchedule, Darkened);
	CheckFeature(TEXT("Alpha byte below hover marker does not activate hover"), BelowHoverMarker, NeutralSchedule, Original);
	CheckFeature(TEXT("Alpha byte above hover marker does not activate hover"), AboveHoverMarker, NeutralSchedule, Original);
	CheckFeature(TEXT("Production unhovered feature cannot darken"), FLinearColor(0.f, 0.f, 0.f, 1.f), NeutralSchedule, Original);
	CheckFeature(TEXT("Production selected blinking feature cannot darken"), FLinearColor(1.f, 0.f, 0.f, 1.f), NeutralSchedule, Original);
	CheckFeature(TEXT("Production selected solid feature cannot darken"), FLinearColor(1.f, 0.f, 0.f, 150.f / 255.f), NeutralSchedule, Original);
	CheckFeature(TEXT("Production hidden feature cannot darken"), FLinearColor(0.f, 0.f, 0.f, 0.f), NeutralSchedule, Original);
	CheckFeature(TEXT("Production 4D color override cannot darken"), HoverMarker, FLinearColor(0.3f, 0.2f, 0.1f, 0.4f), Original);
	CheckFeature(TEXT("Production 4D opacity alone retains hover"), HoverMarker, FLinearColor(0.f, 0.f, 0.f, 0.4f), Darkened);
	Capture->DestroyComponent();
	return true;
}

bool FITwinSelectionOutlineOverridesTest::RunTest(const FString& /*Parameters*/)
{
	FOutlineFixture Fixture;
	if (!TestTrue("Override cache fixture is valid", Fixture.IsValidFixture()))
		return false;
	auto* Mesh = Fixture.GetMeshComp();
	auto* Original = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
	if (!TestNotNull("Original is a MID", Original))
		return false;
	FITwinSceneTile Tile = Fixture.MakeTile();
	auto& Outline = Tile.SelectionOutlineForUnitTest();
	Select(Tile, kElem1);
	Outline.Sync(Tile);
	if (!TestEqual("One companion", Outline.Companions.size(), size_t(1)))
		return false;
	auto* Companion = Outline.Companions[0].Component.Get();
	auto* Material = Outline.Companions[0].Materials[0].Get();
	if (!TestNotNull("Companion material exists", Material))
		return false;
	auto const Sync = [&]()
	{
		for (int32 Stage = 0; Stage < 3; ++Stage)
		{
			Outline.Sync(Tile);
			ENQUEUE_RENDER_COMMAND(FlushOutlineCacheUpload)([](FRHICommandListImmediate& CommandList)
			{
				CommandList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
			});
			FlushRenderingCommands();
		}
	};
	Sync();
	FMaterialParameterInfo const Sentinel(TEXT("OutlineCacheSentinel"));
	Material->SetScalarParameterValueByInfo(Sentinel, 17.f);
	Sync();
	TestEqual("Unchanged sync does not clear/copy companion overrides",
		Material->K2_GetScalarParameterValueByInfo(Sentinel), 17.f);
	TestTrue("Unchanged sync retains the component", Outline.Companions[0].Component.Get() == Companion);
	TestTrue("Initial cache matches original", Outline.Companions[0].Overrides[0].Matches(*Original));

	FMaterialParameterInfo const Scalar(TEXT("_FEATURE_ID_0"), EMaterialParameterAssociation::BlendParameter, 2);
	Original->SetScalarParameterValueByInfo(Scalar, 1.f);
	TestFalse("Same MID scalar change invalidates snapshot", Outline.Companions[0].Overrides[0].Matches(*Original));
	Sync();
	TestEqual("Same MID scalar reaches companion", Material->K2_GetScalarParameterValueByInfo(Scalar), 1.f);
	TestFalse("Refresh clears companion-only sentinel", Material->ScalarParameterValues.ContainsByPredicate(
		[&](FScalarParameterValue const& Value) { return Value.ParameterInfo == Sentinel; }));
	FMaterialParameterInfo const Vector(TEXT("OutlineCacheVector"), EMaterialParameterAssociation::LayerParameter, 0);
	FLinearColor const VectorValue(0.125f, 0.25f, 0.5f, 0.75f);
	Original->SetVectorParameterValueByInfo(Vector, VectorValue);
	TestFalse("Same MID vector change invalidates snapshot", Outline.Companions[0].Overrides[0].Matches(*Original));
	Sync();
	TestTrue("Layer vector reaches companion", Material->K2_GetVectorParameterValueByInfo(Vector) == VectorValue);
	Original->SetVectorParameterValueByInfo(Vector, FLinearColor::Green);
	Sync();
	TestTrue("Existing vector override mutation reaches companion",
		Material->K2_GetVectorParameterValueByInfo(Vector) == FLinearColor::Green);

	FMaterialParameterInfo const TextureInfo(TEXT("PROP_Synchro4D_RGBA"), EMaterialParameterAssociation::BlendParameter, 2);
	auto* FirstTexture = UTexture2D::CreateTransient(1, 1);
	auto* SecondTexture = UTexture2D::CreateTransient(1, 1);
	Original->SetTextureParameterValueByInfo(TextureInfo, FirstTexture);
	Sync();
	UTexture* CopiedTexture = nullptr;
	Material->GetTextureParameterValue(TextureInfo, CopiedTexture);
	TestEqual("4D texture reaches companion", CopiedTexture, static_cast<UTexture*>(FirstTexture));
	Original->SetTextureParameterValueByInfo(TextureInfo, SecondTexture);
	TestFalse("Same MID texture replacement invalidates snapshot", Outline.Companions[0].Overrides[0].Matches(*Original));
	Sync();
	Material->GetTextureParameterValue(TextureInfo, CopiedTexture);
	TestEqual("Replaced 4D texture reaches companion", CopiedTexture, static_cast<UTexture*>(SecondTexture));

	TArray<FMaterialParameterInfo> FadeInfos;
	TArray<FGuid> FadeIds;
	Original->GetAllScalarParameterInfo(FadeInfos, FadeIds);
	int32 NumFadeParameters = 0;
	for (auto const& Info : FadeInfos)
	{
		if (Info.Association == EMaterialParameterAssociation::LayerParameter
			&& (Info.Name == TEXT("FadePercentage") || Info.Name == TEXT("FadingType")))
		{
			++NumFadeParameters;
			Original->SetScalarParameterValueByInfo(Info, Info.Name == TEXT("FadePercentage") ? 0.25f : 1.f);
		}
	}
	TestTrue("Fixture exposes layered fade", NumFadeParameters >= 2);
	ITwinMatParamInfo::SetupSelectingAndHidingInfo();
	Original->SetTextureParameterValueByInfo(*ITwinMatParamInfo::SelectingAndHidingInfo, FirstTexture);
	Sync();
	for (auto const& Info : Outline.Companions[0].Overrides[0].FadeParameters)
		TestEqual("Refreshed companion stays fully opaque", Material->K2_GetScalarParameterValueByInfo(Info),
			Info.Name == TEXT("FadePercentage") ? 1.f : 0.f);
	TestTrue("Snapshot records original fade rather than forced fade", Outline.Companions[0].Overrides[0].Matches(*Original));
	if (!GUsingNullRHI)
	{
		TestNotNull("Outline selection mask is bound", SelectionTexture(*Material));
		TestNotEqual("Selection mask overrides copied original texture", SelectionTexture(*Material), static_cast<UTexture*>(FirstTexture));
		TestTrue("Successful bindings enable custom depth", Companion->bRenderCustomDepth);
	}
	Material->SetScalarParameterValueByInfo(Sentinel, 23.f);
	Sync();
	TestEqual("Forced fade does not invalidate source snapshot every sync",
		Material->K2_GetScalarParameterValueByInfo(Sentinel), 23.f);
	Original->ClearParameterValues();
	Sync();
	TestFalse("Removed original vector overrides are removed from companion", Material->VectorParameterValues.ContainsByPredicate(
		[&](FVectorParameterValue const& Value) { return Value.ParameterInfo == Vector; }));
	TestTrue("Cleared overrides update snapshot", Outline.Companions[0].Overrides[0].Matches(*Original));

	Outline.Companions[0].Materials[0].Reset();
	Companion->SetRenderCustomDepth(true);
	Outline.Sync(Tile);
	TestFalse("Failed binding disables previously enabled custom depth", Companion->bRenderCustomDepth);
	Outline.Companions[0].Materials[0] = Material;
	Sync();
	if (!GUsingNullRHI)
		TestTrue("Recovered binding re-enables custom depth", Companion->bRenderCustomDepth);

	TWeakObjectPtr<UStaticMeshComponent> const OldCompanion = Companion;
	auto* Replacement = UMaterialInstanceDynamic::Create(Original->Parent, Mesh);
	Replacement->SetScalarParameterValueByInfo(Scalar, 2.f);
	Mesh->SetMaterial(0, Replacement);
	Sync();
	TestFalse("MID replacement destroys old source companion", OldCompanion.IsValid());
	TestTrue("Replacement material is recorded by source guard", Outline.Sources[0].Materials[0].Get() == Replacement);
	Material = Outline.Companions[0].Materials[0].Get();
	TestEqual("Replacement MID parameters are copied", Material->K2_GetScalarParameterValueByInfo(Scalar), 2.f);
	TestTrue("Replacement starts with matching snapshot", Outline.Companions[0].Overrides[0].Matches(*Replacement));
	Outline.Sources[0].MaskedSlots.clear();
	Sync();
	TestEqual("Short masked-slot snapshot safely rebuilds", Outline.Sources[0].MaskedSlots.size(), size_t(1));
	auto const PreviousCompanion = Outline.Companions[0].Component;
	Outline.Companions[0].MaskedSlots.clear();
	Sync();
	TestFalse("Inconsistent companion slot state rebuilds component", PreviousCompanion.IsValid());
	if (!TestEqual("Rebuilt companion count", Outline.Companions.size(), size_t(1)))
		return false;
	TestEqual("Rebuilt companion material and slot counts match",
		Outline.Companions[0].Materials.size(), Outline.Companions[0].MaskedSlots.size());
	TestEqual("Rebuilt companion material and override counts match",
		Outline.Companions[0].Materials.size(), Outline.Companions[0].Overrides.size());
	auto const PreviousOverrideCompanion = Outline.Companions[0].Component;
	Outline.Companions[0].Overrides.clear();
	Sync();
	TestFalse("Inconsistent companion override state rebuilds component", PreviousOverrideCompanion.IsValid());
	if (!TestEqual("Rebuilt companion count after override mismatch", Outline.Companions.size(), size_t(1)))
		return false;
	TestEqual("Rebuilt companion material and override counts match after mismatch",
		Outline.Companions[0].Materials.size(), Outline.Companions[0].Overrides.size());
	FITwinTileSelectionOutline::DestroyCompanion(Outline.Companions[0]);
	TestTrue("Destroy clears companion material bindings", Outline.Companions[0].Materials.empty());
	TestTrue("Destroy clears companion slot state", Outline.Companions[0].MaskedSlots.empty());
	TestTrue("Destroy clears companion override snapshots", Outline.Companions[0].Overrides.empty());
	Tile.Unload();
	TestEqual("Unload releases companion", Fixture.NumCompanions(), 0);
	return true;
}

bool FITwinMaterialHoverTest::RunTest(const FString& /*Parameters*/)
{
	FOutlineFixture Fixture;
	if (!TestTrue("Material hover fixture is valid", Fixture.IsValidFixture()))
		return false;
	auto* Mesh = Fixture.GetMeshComp();
	auto* Solid = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
	auto* Glass = MakeGlassMaterial(*Mesh);
	if (!TestNotNull("Translucent parent material loads", Glass))
		return false;
	auto* MixedMesh = MakeTwoSectionMesh(Mesh->GetOwner(), Solid, Glass);
	if (!TestNotNull("Two-section test mesh builds", MixedMesh))
		return false;
	Mesh->SetStaticMesh(MixedMesh);
	Mesh->SetMaterial(0, Solid);
	Mesh->SetMaterial(1, Glass);
	TestEqual("Mixed test mesh has two sections", MixedMesh->GetRenderData()->LODResources[0].Sections.Num(), 2);
	FITwinSceneMapping::SetForcedOpacity(Glass, 0.35f);
	FITwinSceneTile Tile = Fixture.MakeTile();
	FITwinSceneTile::FTextureNeeds TextureNeeds;
	Select(Tile, kElem2);
	SyncMaterialHover(Tile);
	TestFalse("Selection alone allocates no derived hover texture", bool(Tile.HoverSelectingAndHiding));
	TestTrue("Selection alone caches no hover bindings", Tile.HoverMaterialBindings.empty());
	Tile.SetHover(ITwinElementID(9999), ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
	SyncMaterialHover(Tile);
	TestFalse("Unmatched hover allocates no derived texture", bool(Tile.HoverSelectingAndHiding));
	Tile.DeselectElements({ kElem2 }, TextureNeeds);
	Tile.SetHover(kElem1, kMat1, EITwinSelectionTintMode::Blinking, TextureNeeds);
	SyncMaterialHover(Tile);
	TestFalse("Initial hover upload completed", Tile.HoverSelectingAndHiding->NeedToWaitForAsyncUpdate());
	TestNotNull("Initial upload binds the material texture", SelectionTexture(*Solid));
	TestEqual("Material hover creates no companions", Fixture.NumCompanions(), 0);
	TestEqual("Element hover marker", Tile.SelectionPixelForUnitTest(ITwinFeatureID(11), true)[3], ITwin::HOVER_MARKER_ALPHA);
	TestEqual("Material hover marker", Tile.SelectionPixelForUnitTest(ITwinFeatureID(30), true)[3], ITwin::HOVER_MARKER_ALPHA);
	TestEqual("Unrelated feature remains neutral", Tile.SelectionPixelForUnitTest(ITwinFeatureID(20), true)[3], uint8(255));
	TestEqual("Source selection texture stays neutral", Tile.SelectionPixelForUnitTest(ITwinFeatureID(10))[3], uint8(255));
	TestEqual("Mixed slots share material routing", SelectionTexture(*Solid), SelectionTexture(*Glass));
	auto HasLegacyHoverParameter = [](UMaterialInstanceDynamic& Material)
	{
		TArray<FMaterialParameterInfo> Parameters;
		TArray<FGuid> ParameterIds;
		Material.GetAllScalarParameterInfo(Parameters, ParameterIds);
		return Parameters.ContainsByPredicate([](FMaterialParameterInfo const& Parameter)
		{
			return Parameter.Name == TEXT("HoverShift") || Parameter.Name == TEXT("HoverDarkenNearWhite");
		});
	};
	TestFalse("Masked material has no legacy hover scalar parameters", HasLegacyHoverParameter(*Solid));
	TestFalse("Glass material has no legacy hover scalar parameters", HasLegacyHoverParameter(*Glass));
	auto* Replacement = UMaterialInstanceDynamic::Create(Solid->Parent, Mesh);
	Mesh->SetMaterial(0, Replacement);
	SyncMaterialHover(Tile);
	TestEqual("Replacement MID receives stationary hover texture", SelectionTexture(*Replacement), SelectionTexture(*Glass));
	TestFalse("Replacement MID has no legacy hover scalar parameters", HasLegacyHoverParameter(*Replacement));
	Mesh->SetMaterial(0, Solid);
	ITwinMatParamInfo::SetupSelectingAndHidingInfo();
	for (int32 Stage = 0; Stage < 3; ++Stage)
	{
		Tile.SelectingAndHiding->UpdateTexture();
		ENQUEUE_RENDER_COMMAND(FlushHoverSourceUpload)([](FRHICommandListImmediate& CommandList)
		{
			CommandList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
		});
		FlushRenderingCommands();
	}
	TestTrue("Source rebinding fixture", Tile.SelectingAndHiding->SetupInMaterial(Solid, *ITwinMatParamInfo::SelectingAndHidingInfo));
	SyncMaterialHover(Tile);
	TestEqual("External source rebinding restores stationary hover", SelectionTexture(*Solid), SelectionTexture(*Glass));
	Select(Tile, kElem1);
	SyncMaterialHover(Tile);
	TestTrue("Selected tint wins over stationary material hover",
		Tile.SelectionPixelForUnitTest(ITwinFeatureID(10)) == Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true));
	TestEqual("Selected blink retains alpha 255", Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true)[3], uint8(255));
	Tile.SelectionOutlineForUnitTest().Sync(Tile);
	TestTrue("Material hover retains selection outline", Tile.SelectionOutlineForUnitTest().IsActive());
	Tile.DeselectElements({ kElem1 }, TextureNeeds);
	SyncMaterialHover(Tile);
	TestEqual("Stationary deselect restores material hover", Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true)[3], ITwin::HOVER_MARKER_ALPHA);
	Tile.HideElements({ kElem1 }, TextureNeeds, FShowHideOptions{});
	SyncMaterialHover(Tile);
	TestEqual("Material hover cannot unhide a feature", Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true)[3], uint8(0));
	Tile.HideElements({}, TextureNeeds, FShowHideOptions{});
	SyncMaterialHover(Tile);
	TestEqual("Showing feature restores material hover", Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true)[3], ITwin::HOVER_MARKER_ALPHA);
	TestEqual("Mixed routes remain shared", SelectionTexture(*Solid), SelectionTexture(*Glass));
	TestEqual("Material hover preserves glass opacity control",
		Glass->K2_GetScalarParameterValueByInfo(FITwinSceneMapping::GetExtractedElementForcedAlphaMaterialParameterInfo()), 0.35f);
	Tile.SetHover(ITwin::NOT_ELEMENT, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
	SyncMaterialHover(Tile);
	TestEqual("Clear restores neutral marker", Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true)[3], uint8(255));
	TestFalse("Clear releases the derived texture", bool(Tile.HoverSelectingAndHiding));
	TestTrue("Clear releases cached bindings", Tile.HoverMaterialBindings.empty());
	TestEqual("Clear restores source routing on all slots", SelectionTexture(*Solid), SelectionTexture(*Glass));
	TestEqual("Clear also restores a replaced MID for later reuse", SelectionTexture(*Replacement), SelectionTexture(*Solid));
	SyncMaterialHover(Tile);
	TestFalse("Idle sync does not recreate the texture", bool(Tile.HoverSelectingAndHiding));
	Tile.SetHover(kElem1, kMat1, EITwinSelectionTintMode::Blinking, TextureNeeds);
	SyncMaterialHover(Tile);
	TestEqual("Rehover recreates feature marker", Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true)[3], ITwin::HOVER_MARKER_ALPHA);
	Tile.Unload();
	Mesh->SetMaterial(0, Glass);
	Tile = Fixture.MakeTile();
	Select(Tile, kElem1);
	for (int32 Stage = 0; Stage < 3; ++Stage)
	{
		Tile.SelectionOutline.Sync(Tile);
		SyncMaterialHover(Tile);
	}
	TestFalse("Glass-only selection has no companion", Tile.SelectionOutline.IsActive());
	TestFalse("Glass mask initial upload is complete", Tile.SelectionOutline.MaskTexture->UpdateTexture());
	for (int32 Frame = 0; Frame < 3; ++Frame)
		Tile.SelectionOutline.Sync(Tile);
	TestFalse("Unchanged glass selection does not refill or rebuild the mask", Tile.SelectionOutline.MaskTexture->UpdateTexture());
	TestEqual("Unsupported selection retains one eligibility snapshot", Tile.SelectionOutline.Sources.size(), size_t(1));
	Mesh->SetMaterial(0, Solid);
	Tile.SelectionOutline.Sync(Tile);
	TestTrue("Glass-to-masked replacement retries selection companion creation", Tile.SelectionOutline.IsActive());
	auto const Companion = Mesh->GetAttachChildren()[0];
	Tile.SelectionOutline.Sync(Tile);
	TestTrue("Unchanged supported selection keeps the same companion", Mesh->GetAttachChildren()[0] == Companion);
	Mesh->SetMaterial(0, Glass);
	Tile.SelectionOutline.Sync(Tile);
	SyncMaterialHover(Tile);
	Tile.SelectionOutline.Sync(Tile);
	TestFalse("Masked-to-glass replacement removes the companion", Tile.SelectionOutline.IsActive());
	TestFalse("Unsupported replacement also stops refilling the mask", Tile.SelectionOutline.MaskTexture->UpdateTexture());
	Tile.Unload();
	return true;
}

bool FITwinSelectionOutlineTest::RunTest(const FString& /*Parameters*/)
{
	FOutlineFixture Fixture;
	if (!Fixture.IsValidFixture())
	{
		AddWarning(TEXT("Could not set up the test actor (missing editor world, Cube mesh or MI_ITwinInstance)"));
		return true;
	}
	int32 const BaseSources = ITwin::SelectionOutline::GetActiveSourceCount();

	SECTION("Material hover changes viewport BaseColor across screen percentages without selection outlines")
	{
		if (!GUsingNullRHI)
		{
			FMaterialViewportFixture Preview;
			auto* Viewport = &Preview.Viewport;
			auto* Client = &Preview.Client;
			auto* Mesh = Preview.Fixture.GetMeshComp();
			auto* Material = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
			Material->SetVectorParameterValueByInfo(FMaterialParameterInfo(TEXT("baseColorFactor"),
				EMaterialParameterAssociation::LayerParameter, 0), FLinearColor(0.2f, 0.4f, 0.6f));
			auto const SavedViewMode = Client->GetViewMode();
			FName const SavedBufferTarget = Client->CurrentBufferVisualizationMode;
			Client->ChangeBufferVisualizationMode(TEXT("BaseColor"));
			FTransform const SavedTransform = Mesh->GetComponentTransform();
			bool const bSavedPreview = Client->IsPreviewingScreenPercentage();
			int32 const SavedPercentage = Client->GetPreviewScreenPercentage();
			ON_SCOPE_EXIT
			{
				Client->ChangeBufferVisualizationMode(SavedBufferTarget);
				Client->SetViewMode(SavedViewMode);
				Mesh->SetWorldTransform(SavedTransform);
				Client->SetPreviewScreenPercentage(SavedPercentage);
				Client->SetPreviewingScreenPercentage(bSavedPreview);
			};
			Mesh->SetWorldLocation(Client->GetViewLocation() + Client->GetViewRotation().Vector() * 300.);
			Client->SetPreviewingScreenPercentage(true);
			auto const ProbeCenter = [&](FColor& CenterColor)
			{
				Mesh->GetWorld()->SendAllEndOfFrameUpdates();
				Viewport->Draw(false);
				FlushRenderingCommands();
				TArray<FColor> Pixels;
				if (!TestTrue("Center probe viewport readback", Viewport->ReadPixels(Pixels)))
					return false;
				if (!TestTrue("Center probe viewport dimensions", Viewport->GetSizeXY() == FIntPoint(256, 256))
					|| !TestEqual("Center probe pixel count", Pixels.Num(), 256 * 256)
					|| !TestTrue("Center probe pixel is valid", Pixels.IsValidIndex(128 * 256 + 128)))
					return false;
				CenterColor = Pixels[128 * 256 + 128];
				return true;
			};
			Mesh->SetVisibility(false);
			FColor Empty;
			if (!ProbeCenter(Empty))
				return false;
			Mesh->SetVisibility(true);
			Mesh->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial")));
			FColor Opaque;
			if (!ProbeCenter(Opaque))
				return false;
			auto* Constant = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
			Constant->SetShadingModel(MSM_DefaultLit);
			auto* ConstantColor = NewObject<UMaterialExpressionConstant3Vector>(Constant);
			ConstantColor->Constant = FLinearColor(0.8f, 0.1f, 0.05f);
			Constant->GetExpressionCollection().AddExpression(ConstantColor);
			Constant->GetEditorOnlyData()->BaseColor.Connect(0, ConstantColor);
			Constant->GetEditorOnlyData()->EmissiveColor.Connect(0, ConstantColor);
			Constant->PostEditChange();
			if (GShaderCompilingManager)
				GShaderCompilingManager->FinishAllCompilation();
			Mesh->SetMaterial(0, Constant);
			FColor RedControl;
			if (!ProbeCenter(RedControl))
				return false;
			Mesh->SetMaterial(0, Material);
			FColor Production;
			if (!ProbeCenter(Production))
				return false;
			AddInfo(FString::Printf(TEXT("Viewport constant red=%s materials=%d buffer=%d mode=%s"),
				*RedControl.ToString(), Client->EngineShowFlags.Materials, Client->EngineShowFlags.VisualizeBuffer,
				*Client->CurrentBufferVisualizationMode.ToString()));
			TestTrue("Viewport renders independent constant red material", RedControl.R > RedControl.G + 20);
			AddInfo(FString::Printf(TEXT("Viewport controls empty=%s opaque=%s production=%s camera=%s rotation=%s mesh=%s"),
				*Empty.ToString(), *Opaque.ToString(), *Production.ToString(),
				*Client->GetViewLocation().ToString(), *Client->GetViewRotation().ToString(), *Mesh->GetComponentLocation().ToString()));
			for (int32 const Percentage : { 50, 100, 150 })
			{
				Client->SetPreviewScreenPercentage(Percentage);
				FITwinSceneTile Tile = Preview.Fixture.MakeTile();
				Tile.ElementFeaturesSLOW(kElem1).Features.push_back(ITwinFeatureID(0));
				Tile.ElementFeaturesSLOW(kElem1).Features.push_back(ITwinFeatureID(1));
				TArray<FColor> Before, After;
				for (int32 Frame = 0; Frame < 3; ++Frame)
				{
					Mesh->GetWorld()->SendAllEndOfFrameUpdates();
					Viewport->Draw(false);
					FlushRenderingCommands();
				}
				TestTrue("Baseline viewport readback", Viewport->ReadPixels(Before));
				FITwinSceneTile::FTextureNeeds TextureNeeds;
				Tile.SetHover(kElem1, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
				SyncMaterialHover(Tile);
				Mesh->GetWorld()->SendAllEndOfFrameUpdates();
				Viewport->Draw(false);
				FlushRenderingCommands();
				TestTrue("Hovered viewport readback", Viewport->ReadPixels(After));
				Tile.SetHover(ITwin::NOT_ELEMENT, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
				SyncMaterialHover(Tile);
				Viewport->Draw(false);
				FlushRenderingCommands();
				TArray<FColor> Cleared;
				TestTrue("Cleared viewport readback", Viewport->ReadPixels(Cleared));
				int32 const Center = Viewport->GetSizeXY().Y / 2 * Viewport->GetSizeXY().X + Viewport->GetSizeXY().X / 2;
				TestTrue("BaseColor viewport sees the controlled cube", Before[Center].B > Before[Center].G
					&& Before[Center].G > Before[Center].R);
				AddInfo(FString::Printf(TEXT("Material center %d%% before=%s hover=%s clear=%s clear-marker=%d pso=%d"),
					Percentage, *Before[Center].ToString(), *After[Center].ToString(), *Cleared[Center].ToString(),
					Tile.SelectionPixelForUnitTest(ITwinFeatureID(0), true)[3], Mesh->IsPSOPrecaching()));
				Tile.Unload();
				int32 ChangedPixels = 0;
				int32 ResidualPixels = 0;
				for (int32 PixelIndex = 0; PixelIndex < FMath::Min(Before.Num(), After.Num()); ++PixelIndex)
				{
					if (FMath::Abs(int32(After[PixelIndex].R) - int32(Before[PixelIndex].R)) > 2
						&& FMath::Abs(int32(After[PixelIndex].G) - int32(Before[PixelIndex].G)) > 2)
						++ChangedPixels;
					if (Cleared.IsValidIndex(PixelIndex)
						&& (FMath::Abs(int32(Cleared[PixelIndex].R) - int32(Before[PixelIndex].R)) > 2
							|| FMath::Abs(int32(Cleared[PixelIndex].G) - int32(Before[PixelIndex].G)) > 2))
						++ResidualPixels;
				}
				AddInfo(FString::Printf(TEXT("Material hover at %d%%: %d changed pixels, %d residual after clear"),
					Percentage, ChangedPixels, ResidualPixels));
				TestTrue(FString::Printf(TEXT("Tile hover is visible at %d%% screen percentage"), Percentage), ChangedPixels > 100);
				TestTrue(FString::Printf(TEXT("Tile hover clears at %d%% screen percentage"), Percentage), ResidualPixels < 100);
			}
		}
	}

	SECTION("Material hover remains visible with deferred captures and no screen-space handoff")
	{
		if (!GUsingNullRHI)
		{
			FMaterialViewportFixture Preview;
			auto* Viewport = &Preview.Viewport;
			auto* Client = &Preview.Client;
			auto* Mesh = Preview.Fixture.GetMeshComp();
			auto* Material = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
			Material->SetVectorParameterValueByInfo(FMaterialParameterInfo(TEXT("baseColorFactor"),
				EMaterialParameterAssociation::LayerParameter, 0), FLinearColor(0.2f, 0.4f, 0.6f));
			auto const SavedViewMode = Client->GetViewMode();
			FName const SavedBufferTarget = Client->CurrentBufferVisualizationMode;
			Client->ChangeBufferVisualizationMode(TEXT("BaseColor"));
			FTransform const SavedTransform = Mesh->GetComponentTransform();
			Mesh->SetWorldLocation(Client->GetViewLocation() + Client->GetViewRotation().Vector() * 300.);
			auto* Capture = NewObject<USceneCaptureComponent2D>(Mesh->GetOwner(), NAME_None, RF_Transient);
			Capture->bCaptureEveryFrame = false;
			Capture->bCaptureOnMovement = false;
			Capture->CaptureSource = SCS_FinalColorLDR;
			Capture->TextureTarget = NewObject<UTextureRenderTarget2D>(Capture, NAME_None, RF_Transient);
			Capture->TextureTarget->InitAutoFormat(64, 64);
			Capture->SetWorldLocationAndRotation(Client->GetViewLocation(), Client->GetViewRotation());
			Capture->RegisterComponent();
			auto const DrawWithCapture = [&]()
			{
				Mesh->GetWorld()->SendAllEndOfFrameUpdates();
				Capture->CaptureSceneDeferred();
				Viewport->Draw(false);
				FlushRenderingCommands();
			};
			for (int32 Frame = 0; Frame < 3; ++Frame)
				DrawWithCapture();
			TArray<FColor> Before, After, CapturePixels;
			TestTrue("Capture-interleaved baseline readback", Viewport->ReadPixels(Before));
			FITwinSceneTile Tile = Preview.Fixture.MakeTile();
			Tile.ElementFeaturesSLOW(kElem1).Features.push_back(ITwinFeatureID(0));
			Tile.ElementFeaturesSLOW(kElem1).Features.push_back(ITwinFeatureID(1));
			FITwinSceneTile::FTextureNeeds TextureNeeds;
			Tile.SetHover(kElem1, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
			SyncMaterialHover(Tile);
			DrawWithCapture();
			TestTrue("Capture-interleaved hovered readback", Viewport->ReadPixels(After));
			TestTrue("Deferred capture produced its texture target",
				Capture->TextureTarget->GameThread_GetRenderTargetResource()->ReadPixels(CapturePixels));
			TestEqual("Capture target dimensions", CapturePixels.Num(), 64 * 64);
			int32 ChangedPixels = 0;
			for (int32 PixelIndex = 0; PixelIndex < FMath::Min(Before.Num(), After.Num()); ++PixelIndex)
			{
				if (FMath::Abs(int32(After[PixelIndex].R) - int32(Before[PixelIndex].R)) > 2
					&& FMath::Abs(int32(After[PixelIndex].G) - int32(Before[PixelIndex].G)) > 2)
					++ChangedPixels;
			}
			AddInfo(FString::Printf(TEXT("Material hover with deferred capture: %d changed pixels"), ChangedPixels));
			TestTrue("Tile hover survives deferred capture scheduling", ChangedPixels > Before.Num() / 100);
			Tile.SetHover(ITwin::NOT_ELEMENT, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
			SyncMaterialHover(Tile);
			Tile.Unload();
			Capture->DestroyComponent();
			Client->ChangeBufferVisualizationMode(SavedBufferTarget);
			Client->SetViewMode(SavedViewMode);
			Mesh->SetWorldTransform(SavedTransform);
		}
	}

	SECTION("No selection: nothing created")
	{
		FITwinSceneTile Tile = Fixture.MakeTile();
		Tile.SelectionOutlineForUnitTest().Sync(Tile);
		TestFalse("Outline should be inactive", Tile.SelectionOutlineForUnitTest().IsActive());
		TestEqual("No companion", Fixture.NumCompanions(), 0);
		TestEqual("No source registered", ITwin::SelectionOutline::GetActiveSourceCount(), BaseSources);
	}

	SECTION("Select -> companion created and configured, Sync is idempotent")
	{
		FITwinSceneTile Tile = Fixture.MakeTile();
		Select(Tile, kElem1);
		Tile.SelectionOutlineForUnitTest().Sync(Tile);

		TestTrue("Outline should be active", Tile.SelectionOutlineForUnitTest().IsActive());
		TestEqual("One companion attached to the primitive", Fixture.NumCompanions(), 1);
		TestEqual("Actor has the primitive plus one companion", Fixture.NumStaticMeshComponents(), 2);
		TestEqual("Exactly one source registered",
			ITwin::SelectionOutline::GetActiveSourceCount(), BaseSources + 1);

		if (Fixture.NumCompanions() == 1)
		{
			auto* Companion = Cast<UStaticMeshComponent>(Fixture.GetMeshComp()->GetAttachChildren()[0].Get());
			TestNotNull("Companion is a static mesh component", Companion);
			if (Companion)
			{
				TestEqual("Companion shares the static mesh",
					Companion->GetStaticMesh(), Fixture.GetMeshComp()->GetStaticMesh());
				TestEqual("Companion writes the outline stencil value",
					Companion->CustomDepthStencilValue, ITwin::SelectionOutline::StencilValue);
				TestFalse("Companion is not rendered in the main pass", Companion->bRenderInMainPass);
				TestFalse("Companion casts no shadow", Companion->CastShadow);
				TestNotNull("Companion has its own material instance",
					Cast<UMaterialInstanceDynamic>(Companion->GetMaterial(0)));
				TestNotEqual("Companion material differs from the original",
					Companion->GetMaterial(0), Fixture.GetMeshComp()->GetMaterial(0));
			}
		}

		// Same selection, same mesh => nothing recreated, no extra registration
		Tile.SelectionOutlineForUnitTest().Sync(Tile);
		Select(Tile, kElem2, /*bAddToSelection*/true);
		Tile.SelectionOutlineForUnitTest().Sync(Tile);
		TestEqual("Still one companion (both Elements are in the same mesh)", Fixture.NumCompanions(), 1);
		TestEqual("Still exactly one source registered",
			ITwin::SelectionOutline::GetActiveSourceCount(), BaseSources + 1);

		Tile.SelectionOutlineForUnitTest().Destroy();
	}

	SECTION("Deselect -> companion destroyed and source unregistered")
	{
		FITwinSceneTile Tile = Fixture.MakeTile();
		Select(Tile, kElem1);
		Tile.SelectionOutlineForUnitTest().Sync(Tile);
		TestEqual("Companion created", Fixture.NumCompanions(), 1);

		FITwinSceneTile::FTextureNeeds TextureNeeds;
		Tile.DeselectElements({ kElem1 }, TextureNeeds);
		Tile.SelectionOutlineForUnitTest().Sync(Tile);

		TestFalse("Outline should be inactive", Tile.SelectionOutlineForUnitTest().IsActive());
		TestEqual("Companion destroyed", Fixture.NumCompanions(), 0);
		TestEqual("Only the primitive remains on the actor", Fixture.NumStaticMeshComponents(), 1);
		TestEqual("Source unregistered exactly once",
			ITwin::SelectionOutline::GetActiveSourceCount(), BaseSources);
	}

	SECTION("Unload -> companion destroyed and source unregistered")
	{
		FITwinSceneTile Tile = Fixture.MakeTile();
		Select(Tile, kElem1);
		Tile.SelectionOutlineForUnitTest().Sync(Tile);
		TestEqual("Companion created", Fixture.NumCompanions(), 1);

		Tile.Unload();

		TestFalse("Outline should be inactive after Unload", Tile.SelectionOutlineForUnitTest().IsActive());
		TestEqual("Companion destroyed by Unload", Fixture.NumCompanions(), 0);
		TestEqual("Only the primitive remains on the actor", Fixture.NumStaticMeshComponents(), 1);
		TestEqual("Source unregistered exactly once",
			ITwin::SelectionOutline::GetActiveSourceCount(), BaseSources);

		// Destroying again (as Unload + Reset would) must not unbalance the counter
		Tile.SelectionOutlineForUnitTest().Destroy();
		TestEqual("Repeated Destroy is a no-op",
			ITwin::SelectionOutline::GetActiveSourceCount(), BaseSources);
	}

	SECTION("Selection outline remains enabled for selected geometry")
	{
		FITwinSceneTile Tile = Fixture.MakeTile();
		Select(Tile, kElem1);
		TestTrue("Selection outlines are always enabled", ITwin::SelectionOutline::IsEnabled());
		Tile.SelectionOutlineForUnitTest().Sync(Tile);
		TestTrue("Outline stays active after Sync", Tile.SelectionOutlineForUnitTest().IsActive());
		TestEqual("Companion remains created", Fixture.NumCompanions(), 1);
		TestEqual("Source remains registered", ITwin::SelectionOutline::GetActiveSourceCount(), BaseSources + 1);
		Tile.SelectionOutlineForUnitTest().Destroy();
	}

	SECTION("Select material -> companion created, deselect -> destroyed")
	{
		FITwinSceneTile Tile = Fixture.MakeTile();
		uint32 const Rev0 = Tile.GetSelectionRevision();
		SelectMaterial(Tile, kMat1);
		TestNotEqual("Material selection bumps SelectionRevision", Tile.GetSelectionRevision(), Rev0);
		Tile.SelectionOutlineForUnitTest().Sync(Tile);

		TestTrue("Outline should be active", Tile.SelectionOutlineForUnitTest().IsActive());
		TestEqual("One companion attached to the primitive", Fixture.NumCompanions(), 1);
		TestEqual("Exactly one source registered",
			ITwin::SelectionOutline::GetActiveSourceCount(), BaseSources + 1);

		// Selecting the same material again must not change anything
		uint32 const Rev1 = Tile.GetSelectionRevision();
		SelectMaterial(Tile, kMat1);
		Tile.SelectionOutlineForUnitTest().Sync(Tile);
		TestEqual("Still one companion", Fixture.NumCompanions(), 1);

		SelectMaterial(Tile, ITwin::NOT_IMODEL_MATERIAL);
		TestNotEqual("Material deselection bumps SelectionRevision", Tile.GetSelectionRevision(), Rev1);
		Tile.SelectionOutlineForUnitTest().Sync(Tile);
		TestFalse("Outline should be inactive", Tile.SelectionOutlineForUnitTest().IsActive());
		TestEqual("Companion destroyed", Fixture.NumCompanions(), 0);
		TestEqual("Source unregistered", ITwin::SelectionOutline::GetActiveSourceCount(), BaseSources);
	}

	SECTION("Hiding the highlight color keeps the outline")
	{
		FITwinSceneTile Tile = Fixture.MakeTile();
		SelectMaterial(Tile, kMat1);
		Tile.SelectionOutlineForUnitTest().Sync(Tile);
		TestEqual("Companion created", Fixture.NumCompanions(), 1);

		uint32 const Rev = Tile.GetSelectionRevision();
		FITwinSceneTile::FTextureNeeds TextureNeeds;
		Tile.SetSelectionTintMode(EITwinSelectionTintMode::Hidden, TextureNeeds);
		TestTrue("Selection texture was changed", TextureNeeds.bWasChanged);
		TestEqual("Revision unchanged (material still selected)", Tile.GetSelectionRevision(), Rev);
		Tile.SelectionOutlineForUnitTest().Sync(Tile);
		TestTrue("Outline still active", Tile.SelectionOutlineForUnitTest().IsActive());
		TestEqual("Companion kept", Fixture.NumCompanions(), 1);

		Tile.SelectionOutlineForUnitTest().Destroy();
	}

	SECTION("Deselecting Elements keeps the Material outline")
	{
		// Material edition (see UMaterialsItsProxy::BeforeMaterialEdition) deselects Elements and hides the
		// highlight color: the material must stay selected so that its outline remains.
		FITwinSceneTile Tile = Fixture.MakeTile();
		SelectMaterial(Tile, kMat1);
		Select(Tile, kElem2, /*bAddToSelection*/true);
		Tile.SelectionOutlineForUnitTest().Sync(Tile);
		TestEqual("Companion created", Fixture.NumCompanions(), 1);

		FITwinSceneTile::FTextureNeeds TextureNeeds;
		Tile.DeselectElements({ kElem2 }, TextureNeeds);
		Tile.SetSelectionTintMode(EITwinSelectionTintMode::Hidden, TextureNeeds);
		Tile.SelectionOutlineForUnitTest().Sync(Tile);
		TestTrue("Outline still active", Tile.SelectionOutlineForUnitTest().IsActive());
		TestEqual("Companion kept for the material", Fixture.NumCompanions(), 1);

		SelectMaterial(Tile, ITwin::NOT_IMODEL_MATERIAL);
		Tile.SelectionOutlineForUnitTest().Sync(Tile);
		TestEqual("Companion destroyed once the material is deselected", Fixture.NumCompanions(), 0);
	}

	SECTION("Hover does not affect the outline")
	{
		FITwinSceneTile Tile = Fixture.MakeTile();
		FITwinSceneTile::FTextureNeeds TextureNeeds;
		Tile.SetHover(kElem1, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
		TestTrue("Hover creates/changes the selection texture", TextureNeeds.bWasCreated && TextureNeeds.bWasChanged);
		uint32 const Rev0 = Tile.GetSelectionRevision();
		Tile.SelectionOutlineForUnitTest().Sync(Tile);
		TestFalse("Hover alone creates no outline", Tile.SelectionOutlineForUnitTest().IsActive());
		TestEqual("No companion for a hovered-only Element", Fixture.NumCompanions(), 0);

		Select(Tile, kElem1);
		Tile.SelectionOutlineForUnitTest().Sync(Tile);
		TestEqual("Selected (and hovered) Element is outlined", Fixture.NumCompanions(), 1);
		uint32 const Rev1 = Tile.GetSelectionRevision();

		TextureNeeds = {};
		Tile.SetHover(kElem2, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
		TestTrue("Hover change repaints", TextureNeeds.bWasChanged);
		TestEqual("Hover change does not bump SelectionRevision", Tile.GetSelectionRevision(), Rev1);
		Tile.SelectionOutlineForUnitTest().Sync(Tile);
		TestEqual("Still one companion", Fixture.NumCompanions(), 1);

		Tile.SelectionOutlineForUnitTest().Destroy();
	}

	SECTION("Glass uses additive hover without depth companions")
	{
		FOutlineFixture GlassFixture;
		auto* Glass = MakeGlassMaterial(*GlassFixture.GetMeshComp());
		if (!TestNotNull("Translucent parent material loads", Glass))
			return false;
		if (!TestEqual("Fixture uses translucent blending", Glass->GetBlendMode(), BLEND_Translucent))
			return false;
		GlassFixture.GetMeshComp()->SetMaterial(0, Glass);
		FITwinSceneTile Tile = GlassFixture.MakeTile();
		FITwinSceneTile::FTextureNeeds TextureNeeds;
		FITwinSceneMapping::SetForcedOpacity(Glass, 0.35f);
		Tile.SetHover(kElem1, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
		SyncMaterialHover(Tile);
		auto const Pixel = Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true);
		TestEqual("Glass hover has no white tint", Pixel[0], uint8(0));
		TestEqual("Glass hover uses additive marker", Pixel[3], ITwin::HOVER_MARKER_ALPHA);
		TestEqual("Unrelated glass feature is not tinted", Tile.SelectionPixelForUnitTest(ITwinFeatureID(20), true)[0], uint8(0));
		TestEqual("Solid texture is still neutral", Tile.SelectionPixelForUnitTest(ITwinFeatureID(10))[0], uint8(0));
		TestEqual("Glass hover has no depth companion", GlassFixture.NumCompanions(), 0);
		TestEqual("Hover preserves original opacity parameter",
			Glass->K2_GetScalarParameterValueByInfo(FITwinSceneMapping::GetExtractedElementForcedAlphaMaterialParameterInfo()), 0.35f);
		Tile.SetHover(ITwin::NOT_ELEMENT, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
		SyncMaterialHover(Tile);
		TestEqual("Glass hover clear restores original color", Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true)[0], uint8(0));
		TestEqual("Glass hover clear restores original mix", Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true)[3], uint8(255));
		Select(Tile, kElem1);
		SyncMaterialHover(Tile);
		auto const Selection = Tile.SelectionPixelForUnitTest(ITwinFeatureID(10));
		TestTrue("Glass retains existing selection tint", Selection == Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true));
		Tile.SelectionOutlineForUnitTest().Sync(Tile);
		TestFalse("Glass selection has no outline", Tile.SelectionOutlineForUnitTest().IsActive());
		Tile.SetHover(kElem1, kMat1, EITwinSelectionTintMode::Blinking, TextureNeeds);
		SyncMaterialHover(Tile);
		auto const SelectedHover = Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true);
		TestTrue("Selection takes priority over overlapping element and material hover", SelectedHover == Selection);
		TestEqual("Hovered selection retains material-driven blinking", SelectedHover[3], uint8(255));
		TestEqual("Unselected feature of hovered material keeps hover marker", Tile.SelectionPixelForUnitTest(ITwinFeatureID(30), true)[3], ITwin::HOVER_MARKER_ALPHA);
		Tile.SetHover(ITwin::NOT_ELEMENT, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
		SyncMaterialHover(Tile);
		TestTrue("Hover clear restores glass selection exactly", Selection == Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true));
		Tile.SetHover(kElem1, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
		Tile.DeselectElements({ kElem1 }, TextureNeeds);
		SyncMaterialHover(Tile);
		TestEqual("Deselect under stationary hover restores hover marker", Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true)[3], ITwin::HOVER_MARKER_ALPHA);
		Select(Tile, kElem1);
		Select(Tile, kElem2, true);
		SyncMaterialHover(Tile);
		TestTrue("Select under stationary hover restores blinking selection", Selection == Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true));
		TestTrue("Other selected element keeps its blinking selection", Selection == Tile.SelectionPixelForUnitTest(ITwinFeatureID(20), true));
		Tile.DeselectElements({ kElem2 }, TextureNeeds);
		SyncMaterialHover(Tile);
		TestEqual("Unrelated deselected feature is neutral", Tile.SelectionPixelForUnitTest(ITwinFeatureID(20), true)[0], uint8(0));
		Tile.SetHover(kElem1, kMat1, EITwinSelectionTintMode::Blinking, TextureNeeds);
		SelectMaterial(Tile, kMat1);
		SyncMaterialHover(Tile);
		TestTrue("Material selected under stationary hover keeps blinking", Selection == Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true));
		TestTrue("Material-only selected feature keeps blinking", Selection == Tile.SelectionPixelForUnitTest(ITwinFeatureID(30), true));
		TestEqual("Element-only hovered feature keeps hover marker", Tile.SelectionPixelForUnitTest(ITwinFeatureID(11), true)[3], ITwin::HOVER_MARKER_ALPHA);
		Tile.SetHover(ITwin::NOT_ELEMENT, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
		SyncMaterialHover(Tile);
		TestTrue("Unhover preserves material selection", Selection == Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true));
		Tile.SetHover(kElem1, kMat1, EITwinSelectionTintMode::Blinking, TextureNeeds);
		SelectMaterial(Tile, ITwin::NOT_IMODEL_MATERIAL);
		SyncMaterialHover(Tile);
		TestEqual("Material deselected under stationary hover restores hover marker", Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true)[3], ITwin::HOVER_MARKER_ALPHA);
		Tile.HideElements({ kElem1 }, TextureNeeds, FShowHideOptions{});
		Tile.SetHover(kElem1, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
		SyncMaterialHover(Tile);
		TestEqual("Hover cannot unhide glass", Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true)[3], uint8(0));
		Tile.HideElements({}, TextureNeeds, FShowHideOptions{});
		SyncMaterialHover(Tile);
		TestNotEqual("Glass can be shown again", Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true)[3], uint8(0));
		TestEqual("Selection preserves original opacity parameter",
			Glass->K2_GetScalarParameterValueByInfo(FITwinSceneMapping::GetExtractedElementForcedAlphaMaterialParameterInfo()), 0.35f);
		Tile.Unload();
		Tile = GlassFixture.MakeTile();
		Tile.SetHover(kElem2, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
		SyncMaterialHover(Tile);
		TestEqual("Reload has no stale hover", Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true)[0], uint8(0));
		TestEqual("Reload paints new glass hover", Tile.SelectionPixelForUnitTest(ITwinFeatureID(20), true)[3], ITwin::HOVER_MARKER_ALPHA);
		Tile.Unload();
	}

	SECTION("Rendered glass hover and selection change color without changing 4D opacity")
	{
		if (!GUsingNullRHI)
		{
			auto* OITMethod = IConsoleManager::Get().FindConsoleVariable(TEXT("r.OIT.SortedPixels.Method"));
			int32 const SavedOITMethod = OITMethod ? OITMethod->GetInt() : 0;
			if (OITMethod)
				OITMethod->Set(0, ECVF_SetByCode);
			ON_SCOPE_EXIT
			{
				FlushRenderingCommands();
				if (OITMethod)
					OITMethod->Set(SavedOITMethod, ECVF_SetByCode);
			};
			FPreviewScene PreviewScene{ FPreviewScene::ConstructionValues() };
			FOutlineFixture GlassFixture(PreviewScene.GetWorld());
			auto* Mesh = GlassFixture.GetMeshComp();
			auto* Glass = MakeGlassMaterial(*Mesh);
			if (!TestNotNull("Translucent parent material loads", Glass))
				return false;
			Mesh->SetMaterial(0, Glass);
			FITwinSceneTile Tile = GlassFixture.MakeTile();
			Tile.ElementFeaturesSLOW(kElem1).Features.push_back(ITwinFeatureID(0));
			FITwinDynamicShadingBGRA8Property::Create(Tile.HighlightsAndOpacities, Tile.MaxFeatureID,
				std::array<uint8, 4>{ 0, 0, 0, 89 });
			FMaterialParameterInfo const ColorInfo(TEXT("PROP_Synchro4D_RGBA"),
				EMaterialParameterAssociation::BlendParameter, 2);
			auto const Upload4D = [&]()
			{
				for (int32 Stage = 0; Stage < 3; ++Stage)
				{
					Tile.HighlightsAndOpacities->UpdateTexture();
					ENQUEUE_RENDER_COMMAND(FlushGlassColorUpload)([](FRHICommandListImmediate& CommandList)
					{
						CommandList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
					});
					FlushRenderingCommands();
				}
			};
			Upload4D();
			TestTrue("4D opacity-only texture is bound for capture", Tile.HighlightsAndOpacities->SetupInMaterial(Glass, ColorInfo));
			if (GShaderCompilingManager)
				GShaderCompilingManager->FinishAllCompilation();
			auto* Capture = NewObject<USceneCaptureComponent2D>(Mesh->GetOwner(), NAME_None, RF_Transient);
			Capture->bCaptureEveryFrame = false;
			Capture->bCaptureOnMovement = false;
			Capture->CaptureSource = SCS_SceneColorHDR;
			Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
			Capture->TextureTarget = NewObject<UTextureRenderTarget2D>(Capture, NAME_None, RF_Transient);
			Capture->TextureTarget->ClearColor = FLinearColor(0, 0, 0, 1);
			Capture->TextureTarget->InitCustomFormat(64, 64, PF_FloatRGBA, false);
			Capture->SetWorldLocationAndRotation(Mesh->GetComponentLocation() - FVector(300, 0, 0), FRotator::ZeroRotator);
			Capture->RegisterComponent();
			Capture->ShowFlags.SetTranslucency(true);
			Capture->ShowFlags.SetSeparateTranslucency(true);
			Capture->ShowFlags.SetLighting(false);
			Capture->ShowFlags.SetAtmosphere(false);
			Capture->ShowFlags.SetFog(false);
			auto const ReadCenter = [&]()
			{
				PreviewScene.GetWorld()->SendAllEndOfFrameUpdates();
				FlushRenderingCommands();
				Capture->CaptureScene();
				FlushRenderingCommands();
				TArray<FLinearColor> Pixels;
				TestTrue("Glass capture readback", Capture->TextureTarget->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels));
				FLinearColor Center(0, 0, 0, 0);
				for (int32 Row = 28; Row < 36; ++Row)
					for (int32 Column = 28; Column < 36; ++Column)
						if (Pixels.IsValidIndex(Row * 64 + Column))
							Center += Pixels[Row * 64 + Column] / 64.f;
				return Center;
			};
			Mesh->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial")));
			FLinearColor const OpaqueControl = ReadCenter();
			Mesh->SetVisibility(false);
			FLinearColor const EmptyControl = ReadCenter();
			Mesh->SetVisibility(true);
			auto* ConstantGlass = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
			ConstantGlass->BlendMode = BLEND_Translucent;
			ConstantGlass->TranslucencyPass = MTP_BeforeDOF;
			ConstantGlass->SetShadingModel(MSM_Unlit);
			auto* ControlColor = NewObject<UMaterialExpressionConstant3Vector>(ConstantGlass);
			ControlColor->Constant = FLinearColor::Red;
			auto* ControlOpacity = NewObject<UMaterialExpressionConstant>(ConstantGlass);
			ControlOpacity->R = 0.35f;
			ConstantGlass->GetExpressionCollection().AddExpression(ControlColor);
			ConstantGlass->GetExpressionCollection().AddExpression(ControlOpacity);
			ConstantGlass->GetEditorOnlyData()->EmissiveColor.Expression = ControlColor;
			ConstantGlass->GetEditorOnlyData()->Opacity.Expression = ControlOpacity;
			ConstantGlass->PostEditChange();
			if (GShaderCompilingManager)
				GShaderCompilingManager->FinishAllCompilation();
			Mesh->SetMaterial(0, ConstantGlass);
			FLinearColor const TranslucentControl = ReadCenter();
			Mesh->SetMaterial(0, Glass);
			AddInfo(FString::Printf(TEXT("Glass capture controls opaque=%s empty=%s"),
				*OpaqueControl.ToString(), *EmptyControl.ToString()));
			AddInfo(FString::Printf(TEXT("Glass capture constant translucent=%s translucency flag=%d separate=%d"),
				*TranslucentControl.ToString(), Capture->ShowFlags.Translucency, Capture->ShowFlags.SeparateTranslucency));
			TestTrue("Capture sees opaque control geometry", !OpaqueControl.Equals(EmptyControl, 0.01f));
			TestTrue("Capture sees constant translucent control geometry", !TranslucentControl.Equals(EmptyControl, 0.01f));
			FLinearColor const Before = ReadCenter();
			FITwinSceneTile::FTextureNeeds TextureNeeds;
			Tile.SetHover(kElem1, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
			SyncMaterialHover(Tile);
			FLinearColor const Hover = ReadCenter();
			Tile.SetHover(ITwin::NOT_ELEMENT, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
			SyncMaterialHover(Tile);
			FLinearColor const Cleared = ReadCenter();
			Tile.HighlightsAndOpacities->FillWith(std::array<uint8, 4>{ 32, 64, 96, 89 });
			Upload4D();
			FLinearColor const ColorOverride = ReadCenter();
			Tile.SetHover(kElem1, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
			SyncMaterialHover(Tile);
			FLinearColor const ColorOverrideHover = ReadCenter();
			TestTrue("4D color override excludes rendered hover", ColorOverride.Equals(ColorOverrideHover, 0.005f));
			TestTrue("4D color override hover preserves opacity", FMath::IsNearlyEqual(ColorOverrideHover.A, ColorOverride.A, 0.005f));
			Tile.SetHover(ITwin::NOT_ELEMENT, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
			SyncMaterialHover(Tile);
			Select(Tile, kElem1);
			Tile.SetSelectionTintMode(EITwinSelectionTintMode::Solid, TextureNeeds);
			SyncMaterialHover(Tile);
			FLinearColor const Selected = ReadCenter();
			AddInfo(FString::Printf(TEXT("Glass capture baseline=%s hover=%s clear=%s selected=%s"),
				*Before.ToString(), *Hover.ToString(), *Cleared.ToString(), *Selected.ToString()));
			TestTrue("Glass baseline is not the empty capture", !Before.Equals(EmptyControl, 0.01f));
			TestTrue("Glass capture contains partial transparency", Before.A > 0.05f && Before.A < 0.95f);
			TestTrue("Glass hover changes rendered color", Hover.R > Before.R + 0.02f);
			TestTrue("Glass hover preserves rendered opacity", FMath::IsNearlyEqual(Hover.A, Before.A, 0.005f));
			TestTrue("Glass clear restores rendered color and opacity", Before.Equals(Cleared, 0.005f));
			TestTrue("Glass selection changes rendered color", !FMath::IsNearlyEqual(Selected.G, ColorOverride.G, 0.02f));
			TestTrue("Glass selection preserves rendered opacity", FMath::IsNearlyEqual(Selected.A, ColorOverride.A, 0.005f));
			TestEqual("Rendered glass never has hover depth", GlassFixture.NumCompanions(), 0);
			Tile.SelectionOutlineForUnitTest().Sync(Tile);
			TestFalse("Rendered glass never has a selection outline", Tile.SelectionOutlineForUnitTest().IsActive());
			Capture->DestroyComponent();
			Tile.Unload();
		}
	}

	SECTION("Mixed slots retain masked hover and selection but discard glass companion sections")
	{
		FOutlineFixture MixedFixture;
		auto* Mesh = MixedFixture.GetMeshComp();
		auto* Solid = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
		auto* Glass = MakeGlassMaterial(*Mesh);
		if (!TestNotNull("Translucent parent material loads", Glass))
			return false;
		auto* MixedMesh = MakeTwoSectionMesh(Mesh->GetOwner(), Solid, Glass);
		if (!TestNotNull("Two-section test mesh builds", MixedMesh))
			return false;
		Mesh->SetStaticMesh(MixedMesh);
		Mesh->SetMaterial(0, Solid);
		Mesh->SetMaterial(1, Glass);
		TestEqual("Mixed fixture has two actual sections", MixedMesh->GetRenderData()->LODResources[0].Sections.Num(), 2);
		FITwinSceneTile Tile = MixedFixture.MakeTile();
		FITwinSceneTile::FTextureNeeds TextureNeeds;
		Tile.SetHover(kElem1, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
		SyncMaterialHover(Tile);
		TestEqual("Mixed mesh hover creates no companions", MixedFixture.NumCompanions(), 0);
		TestEqual("Mixed masked slot has no white fill", Tile.SelectionPixelForUnitTest(ITwinFeatureID(10))[0], uint8(0));
		TestEqual("Mixed glass slot has additive marker", Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true)[3], ITwin::HOVER_MARKER_ALPHA);
		TestNotNull("Glass texture is bound", SelectionTexture(*Glass));
		TestEqual("Each slot uses the same highlight route", SelectionTexture(*Solid), SelectionTexture(*Glass));
		Select(Tile, kElem1);
		for (int32 Stage = 0; Stage < 3; ++Stage)
		{
			Tile.SelectionOutlineForUnitTest().Sync(Tile);
			SyncMaterialHover(Tile);
		}
		TestTrue("Mixed selection retains supported outline", Tile.SelectionOutlineForUnitTest().IsActive());
		for (auto const& Child : Mesh->GetAttachChildren())
		{
			auto* Companion = Cast<UStaticMeshComponent>(Child.Get());
			if (!Companion)
				continue;
			auto* HiddenTexture = Cast<UTexture2D>(SelectionTexture(*Companion->GetMaterial(1)));
			TestNotNull("Glass companion slot has an all-hidden texture", HiddenTexture);
			if (!GUsingNullRHI && HiddenTexture && HiddenTexture->GetResource())
			{
				TArray<FColor> Pixels;
				ENQUEUE_RENDER_COMMAND(ReadHiddenSlotMask)([Texture = HiddenTexture->GetResource()->TextureRHI, &Pixels]
					(FRHICommandListImmediate& CommandList)
				{
					CommandList.ReadSurfaceData(Texture, FIntRect(0, 0, Texture->GetSizeX(), Texture->GetSizeY()),
						Pixels, FReadSurfaceDataFlags(RCM_UNorm));
				});
				FlushRenderingCommands();
				TestFalse("Hidden slot GPU mask was read", Pixels.IsEmpty());
				bool bAllHidden = !Pixels.IsEmpty();
				for (auto const& Pixel : Pixels)
					bAllHidden &= Pixel.A == 0;
				TestTrue("Glass never contributes companion depth", bAllHidden);
			}
		}
		Mesh->SetMaterial(0, Glass);
		Tile.SelectionOutlineForUnitTest().Sync(Tile);
		SyncMaterialHover(Tile);
		TestFalse("Masked-to-glass change removes selection companion", Tile.SelectionOutlineForUnitTest().IsActive());
		TestEqual("Masked-to-glass change leaves no companions", MixedFixture.NumCompanions(), 0);
		TestEqual("No stale source after material change", ITwin::SelectionOutline::GetActiveSourceCount(), BaseSources);
		Mesh->SetMaterial(0, Solid);
		Tile.SelectionOutlineForUnitTest().Sync(Tile);
		SyncMaterialHover(Tile);
		TestTrue("Glass-to-masked change restores supported selection", Tile.SelectionOutlineForUnitTest().IsActive());
		TestEqual("Glass-to-masked change restores only selection companion", MixedFixture.NumCompanions(), 1);
		Tile.Unload();
		TestEqual("Mixed unload removes all companions", MixedFixture.NumCompanions(), 0);
	}

	SECTION("Hover mask is independent, feature-masked, and cleared on hover removal and unload")
	{
		FITwinSceneTile Tile = Fixture.MakeTile();
		FITwinSceneTile::FTextureNeeds TextureNeeds;
		Tile.SetHover(kElem1, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
		auto const Neutral = Tile.SelectionPixelForUnitTest(ITwinFeatureID(10));
		TestEqual("No white blending on hover", Neutral[0], uint8(0));
		TestEqual("No hover alpha interpolation", Neutral[3], uint8(255));
		SyncMaterialHover(Tile);
		TestEqual("Material hover has no companions", Fixture.NumCompanions(), 0);
		TestEqual("Hovered feature marked", Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true)[3], ITwin::HOVER_MARKER_ALPHA);
		TestEqual("Other feature neutral", Tile.SelectionPixelForUnitTest(ITwinFeatureID(20), true)[3], uint8(255));
		SyncMaterialHover(Tile);
		TestEqual("Repeated sync creates no companions", Fixture.NumCompanions(), 0);
		if (!GUsingNullRHI && GEditor && GEditor->GetActiveViewport())
		{
			GEditor->GetActiveViewport()->Draw(false);
			FlushRenderingCommands();
		}
		else if (!GUsingNullRHI)
		{
			AddWarning(TEXT("No editor viewport: material-hover viewport smoke check was not executed"));
		}
		TestEqual("Hover did not register a selection source", ITwin::SelectionOutline::GetActiveSourceCount(), BaseSources);
		Select(Tile, kElem1);
		auto const SelectedPixel = Tile.SelectionPixelForUnitTest(ITwinFeatureID(10));
		Tile.SetHover(kElem2, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
		Tile.SetHover(kElem1, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
		TestTrue("Hover preserves selection tint", SelectedPixel == Tile.SelectionPixelForUnitTest(ITwinFeatureID(10)));
		SyncMaterialHover(Tile);
		Tile.SetHover(ITwin::NOT_ELEMENT, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
		SyncMaterialHover(Tile);
		TestEqual("Hover clear preserves selection", Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true), SelectedPixel);
		Tile.SetHover(kElem1, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
		SyncMaterialHover(Tile);
		Tile.Unload();
		TestEqual("Unload leaves no companions", Fixture.NumCompanions(), 0);
		Tile.Unload();
		TestEqual("Repeated cleanup is harmless", Fixture.NumCompanions(), 0);
	}

	SECTION("Hover material union and hidden features")
	{
		FITwinSceneTile Tile = Fixture.MakeTile();
		FITwinSceneTile::FTextureNeeds TextureNeeds;
		Tile.SetHover(kElem2, kMat1, EITwinSelectionTintMode::Blinking, TextureNeeds);
		SyncMaterialHover(Tile);
		TestEqual("Element feature included", Tile.SelectionPixelForUnitTest(ITwinFeatureID(20), true)[3], ITwin::HOVER_MARKER_ALPHA);
		TestEqual("Material feature included", Tile.SelectionPixelForUnitTest(ITwinFeatureID(30), true)[3], ITwin::HOVER_MARKER_ALPHA);
		Tile.HideElements({ kElem2 }, TextureNeeds, FShowHideOptions{});
		SyncMaterialHover(Tile);
		TestEqual("Hidden feature excluded", Tile.SelectionPixelForUnitTest(ITwinFeatureID(20), true)[3], uint8(0));
		Tile.Unload();
	}

	SECTION("Hover preserves overlapping material and element selection tint")
	{
		FITwinSceneTile Tile = Fixture.MakeTile();
		SelectMaterial(Tile, kMat1);
		auto const MaterialPixel = Tile.SelectionPixelForUnitTest(ITwinFeatureID(10));
		FITwinSceneTile::FTextureNeeds TextureNeeds;
		Tile.SetHover(kElem1, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
		TestTrue("Material tint survives element hover", MaterialPixel == Tile.SelectionPixelForUnitTest(ITwinFeatureID(10)));
		Tile.SetHover(ITwin::NOT_ELEMENT, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
		TestTrue("Material tint survives hover removal", MaterialPixel == Tile.SelectionPixelForUnitTest(ITwinFeatureID(10)));
		Tile.Unload();
	}

	SECTION("Destroyed source geometry does not leave a registered hover mask")
	{
		FOutlineFixture StreamFixture;
		if (StreamFixture.IsValidFixture())
		{
			FITwinSceneTile Tile = StreamFixture.MakeTile();
			FITwinSceneTile::FTextureNeeds TextureNeeds;
			Tile.SetHover(kElem1, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
			SyncMaterialHover(Tile);
			StreamFixture.GetMeshComp()->DestroyComponent();
			SyncMaterialHover(Tile);
			TestEqual("Destroyed source has no hover companion", StreamFixture.NumCompanions(), 0);
			TestEqual("Destroyed source registers no selection source", ITwin::SelectionOutline::GetActiveSourceCount(), BaseSources);
			Tile.Unload();
		}
	}

	SECTION("Hover remains independent of selection outlines")
	{
		FITwinSceneTile Tile = Fixture.MakeTile();
		FITwinSceneTile::FTextureNeeds TextureNeeds;
		Tile.SetHover(kElem1, ITwin::NOT_IMODEL_MATERIAL, EITwinSelectionTintMode::Blinking, TextureNeeds);
		SyncMaterialHover(Tile);
		Tile.SelectionOutlineForUnitTest().Sync(Tile);
		TestTrue("Selection outlines remain enabled", ITwin::SelectionOutline::IsEnabled());
		TestEqual("Material hover remains active", Tile.SelectionPixelForUnitTest(ITwinFeatureID(10), true)[3], ITwin::HOVER_MARKER_ALPHA);
		TestFalse("Hover alone creates no selection companion", Tile.SelectionOutlineForUnitTest().IsActive());
		Tile.Unload();
	}

	TestEqual("Counter balanced at the end", ITwin::SelectionOutline::GetActiveSourceCount(), BaseSources);
	return true;
}

bool FITwinSelectionOutlineBoundsTest::RunTest(const FString& /*Parameters*/)
{
	FOutlineFixture Fixture;
	if (!TestTrue("Bounds fixture is valid", Fixture.IsValidFixture()))
		return false;
	int32 const BaseSources = ITwin::SelectionOutline::GetActiveSourceCount();
	auto* Mesh = Fixture.GetMeshComp();
	auto const SameBounds = [](FBoxSphereBounds const& A, FBoxSphereBounds const& B)
	{
		return A.Origin.Equals(B.Origin) && A.BoxExtent.Equals(B.BoxExtent)
			&& FMath::IsNearlyEqual(A.SphereRadius, B.SphereRadius);
	};
	// Mimic Cesium primitives, whose CalcBounds (tile bounding volume) differs from the static mesh bounds
	Mesh->BoundsScale = 4.f;
	Mesh->UpdateBounds();
	FITwinSceneTile Tile = Fixture.MakeTile();
	Select(Tile, kElem1);
	Tile.SelectionOutlineForUnitTest().Sync(Tile);
	if (!TestEqual("One companion", Fixture.NumCompanions(), 1))
	{
		Tile.Unload();
		return false;
	}
	auto* Companion = Cast<UStaticMeshComponent>(Mesh->GetAttachChildren()[0].Get());
	if (!TestNotNull("Companion is a static mesh component", Companion))
	{
		Tile.Unload();
		return false;
	}
	TestTrue("Companion uses its attach parent's bounds", Companion->bUseAttachParentBound);
	TestFalse("Static mesh bounds differ from the parent's bounds",
		SameBounds(Companion->CalcBounds(Companion->GetComponentTransform()), Mesh->Bounds));
	TestTrue("Companion bounds equal the parent's bounds", SameBounds(Companion->Bounds, Mesh->Bounds));
	Mesh->SetWorldLocation(Mesh->GetComponentLocation() + FVector(1000., -2000., 500.));
	TestTrue("Companion bounds follow the moved parent", SameBounds(Companion->Bounds, Mesh->Bounds));
	Tile.Unload();
	TestEqual("Unload releases companion", Fixture.NumCompanions(), 0);
	TestEqual("Counter balanced at the end", ITwin::SelectionOutline::GetActiveSourceCount(), BaseSources);
	return true;
}

#endif // WITH_TESTS && WITH_EDITOR
