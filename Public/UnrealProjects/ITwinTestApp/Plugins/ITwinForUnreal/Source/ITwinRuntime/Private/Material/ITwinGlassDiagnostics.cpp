/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinGlassDiagnostics.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <CoreMinimal.h>

#if !UE_BUILD_SHIPPING
#include <ITwinIModel.h>
#include <ITwinIModelInternals.h>
#include <Components/StaticMeshComponent.h>
#include <Engine/Engine.h>
#include <Engine/StaticMesh.h>
#include <Engine/Texture.h>
#include <EngineUtils.h>
#include <HAL/IConsoleManager.h>
#include <Materials/Material.h>
#include <Materials/MaterialInstanceDynamic.h>
#include <Scalability.h>
#include <StereoRendering.h>
#include <StaticMeshComponentLODInfo.h>
#include <StaticMeshResources.h>
#if WITH_TESTS
#include <Engine/Texture2D.h>
#include <MaterialDomain.h>
#include <Misc/AutomationTest.h>
#endif

#include <Compil/BeforeNonUnrealIncludes.h>
# include <SDK/Core/ITwinAPI/ITwinMaterial.h>
# include <SDK/Core/Tools/Log.h>
#include <Compil/AfterNonUnrealIncludes.h>

namespace
{
constexpr int32 MaxGlassMaterials = 64;
constexpr int32 MaxMIDsPerMaterial = 8;

void DumpRenderingState()
{
	auto const Quality = Scalability::GetQualityLevels();
	BE_LOGI("ITwinGlass", "Scalability resolution=" << Quality.ResolutionQuality
		<< " viewDistance=" << Quality.ViewDistanceQuality << " AA=" << Quality.AntiAliasingQuality
		<< " shadow=" << Quality.ShadowQuality << " GI=" << Quality.GlobalIlluminationQuality
		<< " reflection=" << Quality.ReflectionQuality << " post=" << Quality.PostProcessQuality
		<< " texture=" << Quality.TextureQuality << " effects=" << Quality.EffectsQuality
		<< " foliage=" << Quality.FoliageQuality << " shading=" << Quality.ShadingQuality);
	bool const bXRSystem = GEngine && GEngine->XRSystem.IsValid();
	bool const bStereo = GEngine && GEngine->StereoRenderingDevice.IsValid()
		&& GEngine->StereoRenderingDevice->IsStereoEnabled();
	BE_LOGI("ITwinGlass", "XRSystemPresent=" << bXRSystem << " stereoEnabled=" << bStereo);

	// Fixed list rather than enumerating the entire translucency CVar namespace.
	TCHAR const* const CVarNames[] = {
		TEXT("r.Lumen.TranslucencyReflections.FrontLayer.Allow"),
		TEXT("r.Lumen.TranslucencyReflections.FrontLayer.Enable"),
		TEXT("r.Lumen.TranslucencyReflections.FrontLayer.EnableForProject"),
		TEXT("r.Lumen.TranslucencyVolume.TraceFromVolume"),
		TEXT("r.Lumen.HardwareRayTracing"), TEXT("r.RayTracing"),
		TEXT("r.RayTracing.ExcludeTranslucent"), TEXT("r.Lumen.Reflections.Allow"),
		TEXT("r.ReflectionMethod"), TEXT("r.DynamicGlobalIlluminationMethod"),
		TEXT("r.SpecularScale"), TEXT("r.Roughness.Min"),
		TEXT("r.TranslucencyLightingVolumeDim"),
		TEXT("r.TranslucencyLightingVolumeInnerDistance"),
		TEXT("r.TranslucencyLightingVolumeOuterDistance"),
		TEXT("r.TranslucencyVolumeBlur"), TEXT("r.TranslucentLightingVolume"),
		TEXT("r.Translucency.LightingVolume.UpsampleMode"),
	};
	for (TCHAR const* Name : CVarNames)
	{
		auto const* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
		FString const Value = CVar ? CVar->GetString() : TEXT("<missing>");
		BE_LOGI("ITwinGlass", TCHAR_TO_UTF8(Name) << "=" << TCHAR_TO_UTF8(*Value));
	}
}

FString DescribeTexture(UTexture const* Texture)
{
	if (!Texture)
		return TEXT("<null>");
	return FString::Printf(TEXT("%s objectId=%u class=%s size=%gx%g sRGB=%d compression=%d"),
		*Texture->GetName(), Texture->GetUniqueID(), *Texture->GetClass()->GetName(),
		Texture->GetSurfaceWidth(), Texture->GetSurfaceHeight(), Texture->SRGB ? 1 : 0,
		static_cast<int32>(Texture->CompressionSettings));
}

FString DescribeTextureBinding(UMaterialInstanceDynamic const& MID, FMaterialParameterInfo const& Info)
{
	UTexture* EffectiveTexture = nullptr;
	bool const bFound = MID.GetTextureParameterValue(Info, EffectiveTexture);
	UTexture* OverrideTexture = nullptr;
	bool const bOverride = MID.GetTextureParameterValue(Info, OverrideTexture, true);
	return FString::Printf(TEXT("texture=%s association=%d index=%d resolved=%d effective={%s} MIDOverride=%d override={%s}"),
		*Info.Name.ToString(), static_cast<int32>(Info.Association), Info.Index, bFound ? 1 : 0,
		bFound ? *DescribeTexture(EffectiveTexture) : TEXT("<missing>"), bOverride ? 1 : 0,
		bOverride ? *DescribeTexture(OverrideTexture) : TEXT("<missing>"));
}

FString DescribeVertexColors(FColorVertexBuffer const& Colors)
{
	if (Colors.GetNumVertices() == 0)
		return TEXT("colors=absent (implicit white)");
	if (!Colors.GetVertexData())
		return FString::Printf(TEXT("colors=%u CPUData=unavailable"), Colors.GetNumVertices());
	uint32 const SampleCount = FMath::Min(Colors.GetNumVertices(), 256u);
	FColor Minimum(255, 255, 255, 255), Maximum(0, 0, 0, 0);
	for (uint32 Vertex = 0; Vertex < SampleCount; ++Vertex)
	{
		FColor const Color = Colors.VertexColor(Vertex);
		Minimum.R = FMath::Min(Minimum.R, Color.R);
		Minimum.G = FMath::Min(Minimum.G, Color.G);
		Minimum.B = FMath::Min(Minimum.B, Color.B);
		Minimum.A = FMath::Min(Minimum.A, Color.A);
		Maximum.R = FMath::Max(Maximum.R, Color.R);
		Maximum.G = FMath::Max(Maximum.G, Color.G);
		Maximum.B = FMath::Max(Maximum.B, Color.B);
		Maximum.A = FMath::Max(Maximum.A, Color.A);
	}
	return FString::Printf(TEXT("colors=%u sampled=%u RGBA8min=(%u,%u,%u,%u) RGBA8max=(%u,%u,%u,%u) scope=mesh-wide"),
		Colors.GetNumVertices(), SampleCount, Minimum.R, Minimum.G, Minimum.B, Minimum.A,
		Maximum.R, Maximum.G, Maximum.B, Maximum.A);
}

void DumpMeshColors(UWorld* World, TSet<UMaterialInstanceDynamic*> const& MIDs)
{
	int32 Printed = 0;
	for (TObjectIterator<UStaticMeshComponent> It; It && Printed < MaxMIDsPerMaterial; ++It)
	{
		if (It->GetWorld() != World || !It->GetStaticMesh())
			continue;
		bool bMatches = false;
		for (int32 Slot = 0; Slot < It->GetNumMaterials(); ++Slot)
			bMatches |= MIDs.Contains(Cast<UMaterialInstanceDynamic>(It->GetMaterial(Slot)));
		if (!bMatches)
			continue;
		auto const* RenderData = It->GetStaticMesh()->GetRenderData();
		if (!RenderData || RenderData->LODResources.IsEmpty())
			continue;
		auto const* Colors = &RenderData->LODResources[0].VertexBuffers.ColorVertexBuffer;
		bool const bOverride = It->LODData.IsValidIndex(0) && It->LODData[0].OverrideVertexColors;
		if (bOverride)
			Colors = It->LODData[0].OverrideVertexColors;
		FString const Description = DescribeVertexColors(*Colors);
		BE_LOGI("ITwinGlass", "  mesh=" << TCHAR_TO_UTF8(*It->GetName())
			<< " LOD=0 overrideColors=" << bOverride << " " << TCHAR_TO_UTF8(*Description));
		++Printed;
	}
}

void DumpMID(UMaterialInstanceDynamic const& MID)
{
	FString Chain = MID.GetPathName();
	UMaterialInterface const* Current = &MID;
	TSet<UMaterialInterface const*> Seen;
	UMaterial const* Base = nullptr;
	while (Current && !Seen.Contains(Current) && Seen.Num() < 16)
	{
		Seen.Add(Current);
		if (auto const* Instance = Cast<UMaterialInstance>(Current))
		{
			Current = Instance->Parent;
			Chain += TEXT(" -> ") + (Current ? Current->GetPathName() : TEXT("<null>"));
		}
		else
		{
			Base = Cast<UMaterial>(Current);
			break;
		}
	}
	BE_LOGI("ITwinGlass", "  MID=" << TCHAR_TO_UTF8(*MID.GetName())
		<< " parentChain=" << TCHAR_TO_UTF8(*Chain));
	if (Base)
	{
		BE_LOGI("ITwinGlass", "    baseBlend=" << TCHAR_TO_UTF8(*StaticEnum<EBlendMode>()->GetNameStringByValue(Base->GetBlendMode()))
			<< " baseTranslucencyLightingMode=" << static_cast<int32>(Base->TranslucencyLightingMode.GetValue())
			<< " effectiveMIDBlend=" << TCHAR_TO_UTF8(*StaticEnum<EBlendMode>()->GetNameStringByValue(MID.GetBlendMode())));
	}
	else
	{
		BE_LOGI("ITwinGlass", "    base=<missing> (null, cyclic or capped parent chain)");
	}

	// Runtime updates write both global parameters and layer 0. They may differ.
	for (bool const bLayer : { false, true })
	{
		auto const Association = bLayer ? EMaterialParameterAssociation::LayerParameter
			: EMaterialParameterAssociation::GlobalParameter;
		int32 const Index = bLayer ? 0 : INDEX_NONE;
		FString Values = bLayer ? TEXT("layer[0]") : TEXT("global");
		TCHAR const* const ScalarNames[] = {
			TEXT("baseColorTextureFactor"), TEXT("metallicFactor"), TEXT("roughnessFactor"),
			TEXT("specularFactor"), TEXT("normalFlatness"), TEXT("occlusionTextureStrength")
		};
		for (TCHAR const* Name : ScalarNames)
		{
			float Value = 0.f;
			bool const bFound = MID.GetScalarParameterValue(FMaterialParameterInfo(Name, Association, Index), Value);
			Values += FString::Printf(TEXT(" %s=%s"), Name,
				bFound ? *FString::SanitizeFloat(Value) : TEXT("<missing>"));
		}
		FLinearColor Color;
		if (MID.GetVectorParameterValue(FMaterialParameterInfo(TEXT("baseColorFactor"), Association, Index), Color))
		{
			Values += FString::Printf(TEXT(" baseColorFactor=(%g,%g,%g,%g) opacity(baseColorFactor.A)=%g"),
				Color.R, Color.G, Color.B, Color.A, Color.A);
		}
		else
		{
			Values += TEXT(" baseColorFactor=<missing> opacity(baseColorFactor.A)=<missing>");
		}
		BE_LOGI("ITwinGlass", "    " << TCHAR_TO_UTF8(*Values));
	}
	TArray<FMaterialParameterInfo> TextureInfos;
	TArray<FGuid> TextureIDs;
	MID.GetAllTextureParameterInfo(TextureInfos, TextureIDs);
	for (auto const& Override : MID.TextureParameterValues)
		TextureInfos.AddUnique(Override.ParameterInfo);
	for (auto const& Info : TextureInfos)
	{
		FString const Binding = DescribeTextureBinding(MID, Info);
		BE_LOGI("ITwinGlass", "    " << TCHAR_TO_UTF8(*Binding));
	}
	BE_LOGI("ITwinGlass", "    textureBindings=" << TextureInfos.Num()
		<< " association: global=" << static_cast<int32>(EMaterialParameterAssociation::GlobalParameter)
		<< " layer=" << static_cast<int32>(EMaterialParameterAssociation::LayerParameter)
		<< " blend=" << static_cast<int32>(EMaterialParameterAssociation::BlendParameter)
		<< "; objectId is session-local");
}

void DumpGlass(UWorld* World)
{
	if (!IsInGameThread())
	{
		BE_LOGI("ITwinGlass", "ITwin.Glass.Dump requires the game thread.");
		return;
	}
	BE_LOGI("ITwinGlass", "ITwin.Glass.Dump begin (read-only; loaded scene materials only)");
	DumpRenderingState();
	if (!World)
	{
		BE_LOGI("ITwinGlass", "World=<missing>; no iModels inspected.");
		return;
	}
	int32 ModelCount = 0;
	for (TActorIterator<AITwinIModel> It(World); It; ++It)
	{
		++ModelCount;
		AITwinIModel& Model = **It;
		auto const Materials = Model.GetITwinMaterialMap();
		TArray<uint64> IDs;
		Materials.GetKeys(IDs);
		IDs.Sort();
		int32 GlassCount = 0;
		int32 PrintedMaterials = 0;
		BE_LOGI("ITwinGlass", "iModel=" << TCHAR_TO_UTF8(*Model.GetPathName())
			<< " id=" << TCHAR_TO_UTF8(*Model.IModelId));
		for (uint64 const ID : IDs)
		{
			if (Model.GetMaterialKind(ID) != AdvViz::SDK::EMaterialKind::Glass)
				continue;
			++GlassCount;
			if (PrintedMaterials >= MaxGlassMaterials)
				continue;
			++PrintedMaterials;
			AdvViz::SDK::EMaterialKind EffectiveKind = AdvViz::SDK::EMaterialKind::PBR;
			bool bRequiresTranslucency = false;
			bool const bCustom = Model.GetMaterialCustomRequirements(ID, EffectiveKind, bRequiresTranslucency);
			BE_LOGI("ITwinGlass", " materialId=" << ID << " name=" << TCHAR_TO_UTF8(*Model.GetMaterialName(ID))
				<< " kind=Glass effectiveKind=" << (EffectiveKind == AdvViz::SDK::EMaterialKind::Glass ? "Glass" : "PBR")
				<< " custom=" << bCustom << " requiresTranslucency=" << bRequiresTranslucency);
			TSet<UMaterialInstanceDynamic*> MIDs;
			{
				// Follow the scene mapping's lock order. The visitor only reads material pointers.
				auto Mapping = GetInternals(Model).SceneMapping->GetRAutoLock();
				Mapping->ForEachKnownTile([&](TITwinSceneTilePtr const& Tile)
				{
					auto TileLock = Tile->GetAutoLock();
					TileLock->ForEachMaterialInstanceMatchingID(ID, [&](UMaterialInstanceDynamic& MID)
					{
						MIDs.Add(&MID);
					});
				});
			}
			int32 PrintedMIDs = 0;
			for (auto const* MID : MIDs)
			{
				if (PrintedMIDs >= MaxMIDsPerMaterial)
					break;
				DumpMID(*MID);
				++PrintedMIDs;
			}
			BE_LOGI("ITwinGlass", "  distinctLoadedMIDs=" << MIDs.Num() << " printed=" << PrintedMIDs
				<< " omitted=" << MIDs.Num() - PrintedMIDs);
			DumpMeshColors(World, MIDs);
		}
		BE_LOGI("ITwinGlass", "iModel summary glass=" << GlassCount << " totalKnownMaterials=" << Materials.Num()
			<< " printedGlass=" << PrintedMaterials << " omittedGlass=" << GlassCount - PrintedMaterials);
	}
	BE_LOGI("ITwinGlass", "ITwin.Glass.Dump end iModels=" << ModelCount);
}

static FAutoConsoleCommandWithWorld GITwinGlassDump(
	TEXT("ITwin.Glass.Dump"),
	TEXT("Read-only glass baseline: effective rendering CVars, material kinds, MID parents, parameters and texture bindings. Logs to ITwinGlass."),
	FConsoleCommandWithWorldDelegate::CreateStatic(&DumpGlass));

#if WITH_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FITwinGlassTextureDiagnosticsTest,
	"Bentley.ITwinForUnreal.ITwinRuntime.Glass.TextureDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinGlassTextureDiagnosticsTest::RunTest(const FString& Parameters)
{
	FColorVertexBuffer Colors;
	TestTrue(TEXT("Missing vertex colors are explicit"), DescribeVertexColors(Colors).Contains(TEXT("implicit white")));
	Colors.InitFromSingleColor(FColor(10, 20, 30, 40), 300);
	FString const ColorDescription = DescribeVertexColors(Colors);
	TestTrue(TEXT("Vertex sampling is bounded"), ColorDescription.Contains(TEXT("colors=300 sampled=256")));
	TestTrue(TEXT("RGB and alpha are reported independently"),
		ColorDescription.Contains(TEXT("RGBA8min=(10,20,30,40) RGBA8max=(10,20,30,40)")));
	TestEqual(TEXT("Null textures are distinct from missing parameters"),
		DescribeTexture(nullptr), FString(TEXT("<null>")));
	auto* Texture = NewObject<UTexture2D>();
	FString const Description = DescribeTexture(Texture);
	TestTrue(TEXT("Texture identity is included"), Description.Contains(Texture->GetName()));
	TestTrue(TEXT("Texture state is included"), Description.Contains(TEXT("sRGB=")));
	TestFalse(TEXT("Texture outer path is not logged"), Description.Contains(TEXT("/Engine/Transient")));
	auto* MID = UMaterialInstanceDynamic::Create(UMaterial::GetDefaultMaterial(EMaterialDomain::MD_Surface), GetTransientPackage());
	FString const Binding = DescribeTextureBinding(*MID,
		FMaterialParameterInfo(TEXT("ITwinGlassMissingTexture"), EMaterialParameterAssociation::LayerParameter, 0));
	TestTrue(TEXT("Association and layer index are included"), Binding.Contains(FString::Printf(
		TEXT("association=%d index=0"), static_cast<int32>(EMaterialParameterAssociation::LayerParameter))));
	TestTrue(TEXT("Missing effective binding is explicit"), Binding.Contains(TEXT("resolved=0 effective={<missing>}")));
	TestTrue(TEXT("Absent MID override is explicit"), Binding.Contains(TEXT("MIDOverride=0 override={<missing>}")));
	return true;
}
#endif
}
#endif // !UE_BUILD_SHIPPING
