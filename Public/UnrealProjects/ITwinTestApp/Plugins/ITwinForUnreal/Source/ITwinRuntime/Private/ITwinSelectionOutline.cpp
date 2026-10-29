/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSelectionOutline.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinSelectionOutline.h"

#include <ITwinDynamicShadingProperty.inl>
#include <ITwinSceneMapping.h>
#include <ITwinSelectionOutlineRenderer.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/Tools/Log.h>
#	include <array>
#	include <unordered_set>
#	include <vector>
#include <Compil/AfterNonUnrealIncludes.h>

#include <Async/Async.h>
#include <Components/StaticMeshComponent.h>
#include <Engine/StaticMesh.h>
#include <GameFramework/Actor.h>
#include <Materials/MaterialInstanceDynamic.h>
#include <Materials/MaterialInterface.h>

namespace
{
	/// Alpha is the only channel that matters: the masked material discards fragments with zero alpha
	constexpr std::array<uint8, 4> MASK_HIDDEN_BGRA = { 0, 0, 0, 0 };
	constexpr std::array<uint8, 4> MASK_VISIBLE_BGRA = { 0, 0, 0, 255 };

	// Parameters of Cesium's "DitherFade" material layer (see UCesiumGltfComponent::UpdateFade)
	static const FName FadePercentageParamName = "FadePercentage";
	static const FName FadingTypeParamName = "FadingType";

	/// Cesium's LOD transitions fade tiles in/out with a dithered opacity mask, which the custom depth pass
	/// would reproduce as scattered dots. Cesium only updates the fade of its own primitives, so the
	/// companion must be forced fully opaque whatever the state of the original material when copied.
	void ForceFullyOpaqueFade(UMaterialInstanceDynamic& Mat, TArray<FMaterialParameterInfo> const& ParamInfos)
	{
		for (FMaterialParameterInfo const& Info : ParamInfos)
		{
			if (Info.Association != EMaterialParameterAssociation::LayerParameter)
				continue;
			if (Info.Name == FadePercentageParamName)
				Mat.SetScalarParameterValueByInfo(Info, 1.f);
			else if (Info.Name == FadingTypeParamName)
				Mat.SetScalarParameterValueByInfo(Info, 0.f); // 0 = fading in, ie. fully visible at 100%
		}
	}
}

bool FITwinTileSelectionOutline::FMaterialOverrides::Matches(UMaterialInstanceDynamic const& Material) const
{
	return Scalars == Material.ScalarParameterValues
		&& Vectors == Material.VectorParameterValues
		&& DoubleVectors == Material.DoubleVectorParameterValues
		&& Textures == Material.TextureParameterValues
		&& Fonts == Material.FontParameterValues;
}

void FITwinTileSelectionOutline::FMaterialOverrides::Capture(UMaterialInstanceDynamic const& Material)
{
	Scalars = Material.ScalarParameterValues;
	Vectors = Material.VectorParameterValues;
	DoubleVectors = Material.DoubleVectorParameterValues;
	Textures = Material.TextureParameterValues;
	Fonts = Material.FontParameterValues;
}

void FITwinTileSelectionOutline::Sync(FITwinSceneTile& SceneTile)
{
	check(IsInGameThread());
	if (!ITwin::SelectionOutline::IsEnabled() || !SceneTile.IsLoaded()
		|| (SceneTile.SelectedElements.empty() && SceneTile.SelectedMaterial == ITwin::NOT_MATERIAL)
		|| SceneTile.MaxFeatureID == ITwin::NOT_FEATURE)
	{
		Destroy();
		return;
	}
	// Companions attached to a mesh component destroyed by Cesium are useless: force a rebuild
	bool bOrphaned = false;
	for (auto const& Source : Sources)
	{
		auto* Original = Source.MeshIndex >= 0 && Source.MeshIndex < (int32)SceneTile.GltfMeshes.size()
			? SceneTile.GltfMeshes[Source.MeshIndex].MeshComponent() : nullptr;
		if (Original != Source.Component.Get()
			|| (Original && (Original->GetStaticMesh() != Source.Mesh.Get()
				|| Original->IsRegistered() != Source.bRegistered
				|| Original->GetNumMaterials() != (int32)Source.Materials.size()
				|| Original->GetNumMaterials() != (int32)Source.MaskedSlots.size())))
		{
			bOrphaned = true;
			break;
		}
		if (!Original)
			continue;
		for (int32 Slot = 0; Slot < Original->GetNumMaterials(); ++Slot)
		{
			UMaterialInterface const* Material = Original->GetMaterial(Slot);
			bOrphaned |= Material != Source.Materials[Slot].Get()
				|| (Material && (Material->GetBlendMode() == BLEND_Masked) != Source.MaskedSlots[Slot]);
		}
	}
	for (auto const& Companion : Companions)
	{
		if (Companion.Materials.size() != Companion.MaskedSlots.size()
			|| Companion.Materials.size() != Companion.Overrides.size()
			|| !Companion.Component.IsValid() || !Companion.Component->GetAttachParent())
		{
			bOrphaned = true;
			break;
		}
	}
	if (bOrphaned)
	{
		for (auto& Companion : Companions)
			DestroyCompanion(Companion);
		Companions.clear();
	}
	if (bOrphaned || !MaskTexture || OutlinedRevision != SceneTile.SelectionRevision)
	{
		Rebuild(SceneTile);
	}
	if (Companions.empty() && bRegisteredAsSource)
	{
		ITwin::SelectionOutline::RemoveActiveSource();
		bRegisteredAsSource = false;
	}
	if (!MaskTexture)
		return;
	// Upload the mask (no-op when not dirty), then bind it to the companion materials as soon as the first
	// upload has been processed (see FITwinDynamicShadingProperty::SetupInMaterial), and only then allow
	// rendering in the custom depth pass (otherwise the whole mesh would be drawn for a few frames).
	MaskTexture->UpdateTexture();
	if (HiddenSlotsTexture)
		HiddenSlotsTexture->UpdateTexture();
	for (auto& Companion : Companions)
	{
		if (!Companion.Component.IsValid())
			continue;
		ITwinMatParamInfo::SetupSelectingAndHidingInfo();
		bool bAllBound = !Companion.Materials.empty();
		for (size_t MaterialIndex = 0; MaterialIndex < Companion.Materials.size(); ++MaterialIndex)
		{
			auto const& Mat = Companion.Materials[MaterialIndex];
			if (Companion.MaskedSlots[MaterialIndex] && Mat.IsValid())
			{
				auto* Original = Cast<UStaticMeshComponent>(Companion.Component->GetAttachParent());
				auto* OriginalMID = Original
					? Cast<UMaterialInstanceDynamic>(Original->GetMaterial((int32)MaterialIndex)) : nullptr;
				auto& Overrides = Companion.Overrides[MaterialIndex];
				if (OriginalMID && !Overrides.Matches(*OriginalMID))
				{
					Mat->CopyParameterOverrides(OriginalMID);
					Overrides.Capture(*OriginalMID);
					ForceFullyOpaqueFade(*Mat, Overrides.FadeParameters);
				}
			}
			auto const& SlotTexture = Companion.MaskedSlots[MaterialIndex] ? MaskTexture : HiddenSlotsTexture;
			if (!Mat.IsValid() || !SlotTexture
				|| !SlotTexture->SetupInMaterial(Mat, *ITwinMatParamInfo::SelectingAndHidingInfo))
			{
				bAllBound = false;
				break;
			}
		}
		Companion.Component->SetRenderCustomDepth(bAllBound);
	}
	if (!bRegisteredAsSource && !Companions.empty())
	{
		ITwin::SelectionOutline::AddActiveSource();
		bRegisteredAsSource = true;
	}
}

void FITwinTileSelectionOutline::Rebuild(FITwinSceneTile& SceneTile)
{
	OutlinedRevision = SceneTile.SelectionRevision;
	if (!MaskTexture)
	{
		FITwinDynamicShadingBGRA8Property::Create(MaskTexture, SceneTile.MaxFeatureID, MASK_HIDDEN_BGRA);
	}
	else
	{
		MaskTexture->FillWith(MASK_HIDDEN_BGRA);
	}
	std::unordered_set<int32> MeshesNeeded;
	for (auto const& ElemID : SceneTile.SelectedElements)
	{
		auto const* ElemFeatures = SceneTile.FindElementFeaturesSLOW(ElemID);
		if (!ElemFeatures || ElemFeatures->Features.empty())
			continue;
		MaskTexture->SetPixels(ElemFeatures->Features, MASK_VISIBLE_BGRA);
		for (int32 const MeshIndex : ElemFeatures->Meshes)
			MeshesNeeded.insert(MeshIndex);
	}
	if (SceneTile.SelectedMaterial != ITwin::NOT_MATERIAL)
	{
		auto const* MatFeatures = SceneTile.FindMaterialFeaturesSLOW(
			ITwinRenderMaterialElementID{ SceneTile.SelectedMaterial.getValue() });
		if (MatFeatures && !MatFeatures->Features.empty())
		{
			MaskTexture->SetPixels(MatFeatures->Features, MASK_VISIBLE_BGRA);
			for (int32 const MeshIndex : MatFeatures->Meshes)
				MeshesNeeded.insert(MeshIndex);
		}
	}
	Sources.clear();
	for (int32 const MeshIndex : MeshesNeeded)
	{
		FSource Source;
		Source.MeshIndex = MeshIndex;
		auto* Original = MeshIndex >= 0 && MeshIndex < (int32)SceneTile.GltfMeshes.size()
			? SceneTile.GltfMeshes[MeshIndex].MeshComponent() : nullptr;
		Source.Component = Original;
		if (Original)
		{
			Source.Mesh = Original->GetStaticMesh();
			Source.bRegistered = Original->IsRegistered();
			for (int32 Slot = 0; Slot < Original->GetNumMaterials(); ++Slot)
			{
				auto* Material = Original->GetMaterial(Slot);
				Source.Materials.emplace_back(Material);
				Source.MaskedSlots.push_back(Material && Material->GetBlendMode() == BLEND_Masked);
			}
		}
		Sources.emplace_back(std::move(Source));
	}
	// Keep companions still needed, destroy the others
	for (auto It = Companions.begin(); It != Companions.end(); )
	{
		if (It->Component.IsValid() && It->Component->GetAttachParent()
			&& It->Component->GetAttachParent()->IsRegistered()
			&& It->MeshIndex >= 0 && It->MeshIndex < (int32)SceneTile.GltfMeshes.size()
			&& It->Component->GetAttachParent() == SceneTile.GltfMeshes[It->MeshIndex].MeshComponent()
			&& MeshesNeeded.erase(It->MeshIndex) > 0)
		{
			++It;
		}
		else
		{
			DestroyCompanion(*It);
			It = Companions.erase(It);
		}
	}
	for (int32 const MeshIndex : MeshesNeeded)
	{
		CreateCompanion(SceneTile, MeshIndex);
	}
}

void FITwinTileSelectionOutline::CreateCompanion(FITwinSceneTile& SceneTile, int32 const MeshIndex)
{
	if (MeshIndex < 0 || MeshIndex >= (int32)SceneTile.GltfMeshes.size())
		return;
	UStaticMeshComponent* OrigComp = SceneTile.GltfMeshes[MeshIndex].MeshComponent();
	if (!OrigComp || !OrigComp->GetStaticMesh() || !OrigComp->GetOwner())
		return;
	int32 const NumMats = OrigComp->GetNumMaterials();
	if (NumMats == 0)
		return;
	UMaterialInterface* MaskedMaterial = nullptr;
	for (int32 m = 0; m < NumMats; ++m)
	{
		auto* Material = OrigComp->GetMaterial(m);
		if (Material && Material->GetBlendMode() == BLEND_Masked)
			MaskedMaterial = Material;
	}
	if (!MaskedMaterial)
	{
		// The per-pixel discard relies on the opacity mask of the original material: nothing can be done for
		// opaque or translucent materials (the latter are not rendered in the custom depth pass anyway).
		BE_LOGV("SelectionOutline", "Skipping mesh " << TCHAR_TO_UTF8(*OrigComp->GetName())
			<< ": no BLEND_Masked material");
		return;
	}

	FCompanion Companion;
	Companion.MeshIndex = MeshIndex;
	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(OrigComp->GetOwner(), NAME_None,
		RF_Transient | RF_DuplicateTransient | RF_TextExportTransient);
	Comp->SetStaticMesh(OrigComp->GetStaticMesh());
	Comp->SetMobility(OrigComp->Mobility);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetGenerateOverlapEvents(false);
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetCastShadow(false);
	Comp->bReceivesDecals = false;
	Comp->bVisibleInReflectionCaptures = false;
	Comp->bVisibleInRealTimeSkyCaptures = false;
	Comp->bVisibleInRayTracing = false;
	Comp->SetAffectDistanceFieldLighting(false);
	// Only the custom depth pass must see this component (enabled once the mask texture is bound)
	Comp->SetRenderInMainPass(false);
	Comp->SetRenderInDepthPass(false);
	Comp->SetRenderCustomDepth(false);
	Comp->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_Default);
	Comp->SetCustomDepthStencilValue(ITwin::SelectionOutline::StencilValue);
	for (int32 m = 0; m < NumMats; ++m)
	{
		UMaterialInterface* OrigMat = OrigComp->GetMaterial(m);
		bool const bMasked = OrigMat && OrigMat->GetBlendMode() == BLEND_Masked;
		Companion.MaskedSlots.push_back(bMasked);
		if (!bMasked)
		{
			if (!HiddenSlotsTexture)
				FITwinDynamicShadingBGRA8Property::Create(HiddenSlotsTexture, SceneTile.MaxFeatureID, MASK_HIDDEN_BGRA);
			OrigMat = MaskedMaterial;
		}
		// Same base material and same parameters (feature-ID UV index, 4D textures, clipping...) as the
		// original, the selection texture being overridden by our mask in Sync. A MID cannot be the parent
		// of another MID, hence the copy of its overrides.
		UMaterialInstanceDynamic* Mat = nullptr;
		auto& Overrides = Companion.Overrides.emplace_back();
		if (auto* OrigMID = Cast<UMaterialInstanceDynamic>(OrigMat); OrigMID && OrigMID->Parent)
		{
			Mat = UMaterialInstanceDynamic::Create(OrigMID->Parent, Comp);
			Mat->CopyParameterOverrides(OrigMID);
			Overrides.Capture(*OrigMID);
		}
		else
		{
			Mat = UMaterialInstanceDynamic::Create(OrigMat, Comp);
		}
		TArray<FGuid> ParamIds;
		Mat->GetAllScalarParameterInfo(Overrides.FadeParameters, ParamIds);
		Overrides.FadeParameters.RemoveAll([](FMaterialParameterInfo const& Info)
		{
			return Info.Association != EMaterialParameterAssociation::LayerParameter
				|| (Info.Name != FadePercentageParamName && Info.Name != FadingTypeParamName);
		});
		ForceFullyOpaqueFade(*Mat, Overrides.FadeParameters);
		Comp->SetMaterial(m, Mat);
		Companion.Materials.emplace_back(Mat);
	}
	Comp->SetupAttachment(OrigComp);
	// Cesium primitives override CalcBounds with the tile bounding volume, so the static mesh bounds are
	// unreliable (they can be offset from the actual geometry): cull the companion exactly like the mesh
	// it outlines, otherwise it can be culled while its parent is visible (=> no stencil, no outline).
	Comp->bUseAttachParentBound = true;
	Comp->RegisterComponent();
	Companion.Component = Comp;
	Companions.emplace_back(std::move(Companion));
}

/*static*/
void FITwinTileSelectionOutline::DestroyCompanion(FCompanion& Companion)
{
	if (Companion.Component.IsValid())
	{
		Companion.Component->DestroyComponent();
	}
	Companion.Component.Reset();
	Companion.Materials.clear();
	Companion.MaskedSlots.clear();
	Companion.Overrides.clear();
}

void FITwinTileSelectionOutline::Destroy()
{
	if (Companions.empty() && !MaskTexture && !bRegisteredAsSource && Sources.empty() && !HiddenSlotsTexture)
		return;
	if (IsInGameThread())
	{
		for (auto& Companion : Companions)
			DestroyCompanion(Companion);
		if (bRegisteredAsSource)
			ITwin::SelectionOutline::RemoveActiveSource();
	}
	else
	{
		std::vector<TWeakObjectPtr<UStaticMeshComponent>> Comps;
		for (auto& Companion : Companions)
			Comps.push_back(Companion.Component);
		AsyncTask(ENamedThreads::GameThread, [Comps = std::move(Comps), bUnregister = bRegisteredAsSource]()
		{
			for (auto const& Comp : Comps)
			{
				if (Comp.IsValid())
					Comp->DestroyComponent();
			}
			if (bUnregister)
				ITwin::SelectionOutline::RemoveActiveSource();
		});
	}
	bRegisteredAsSource = false;
	Sources.clear();
	Companions.clear();
	OutlinedRevision = 0;
	MaskTexture.reset();
	HiddenSlotsTexture.reset();
}
