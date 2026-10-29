/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSelectionOutline.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include <ITwinElementID.h>
#include <ITwinDynamicShadingProperty.h>
#include <Materials/MaterialInstance.h>
#include <UObject/WeakObjectPtr.h>

#include <memory>
#include <vector>

class FITwinSceneTile;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/// Per-tile "companion" geometry feeding the custom stencil buffer with the selected Elements (and/or the
/// features of the selected Material) only, so that a screen-space outline can be drawn around them (see
/// ITwinSelectionOutlineRenderer.h in ITwinRendering).
///
/// For each glTF mesh of the tile containing a selected Element or Material, a UStaticMeshComponent sharing
/// the very same UStaticMesh is created (no geometry is copied), rendered only in the custom depth pass, with
/// a copy of the original (masked) material instance where the selection texture is replaced by a mask
/// texture hiding all features but the selected ones. The custom depth pass thus only receives the selected
/// fragments.
class FITwinTileSelectionOutline
{
public:
	/// Make the companion geometry match the tile's current selection (creating, updating or destroying it
	/// as needed). Game thread only.
	void Sync(FITwinSceneTile& SceneTile);
	/// Destroy all companion geometry. Must be called before the tile is unloaded.
	void Destroy();
	[[nodiscard]] bool IsActive() const { return !Companions.empty(); }

private:
	friend class FITwinMaterialHoverTest;
	friend class FITwinSelectionOutlineOverridesTest;
	struct FMaterialOverrides
	{
		TArray<FScalarParameterValue> Scalars;
		TArray<FVectorParameterValue> Vectors;
		TArray<FDoubleVectorParameterValue> DoubleVectors;
		TArray<FTextureParameterValue> Textures;
		TArray<FFontParameterValue> Fonts;
		TArray<FMaterialParameterInfo> FadeParameters;

		bool Matches(UMaterialInstanceDynamic const& Material) const;
		void Capture(UMaterialInstanceDynamic const& Material);
	};
	struct FSource
	{
		int32 MeshIndex = -1;
		TWeakObjectPtr<UStaticMeshComponent> Component;
		TWeakObjectPtr<UStaticMesh> Mesh;
		std::vector<TWeakObjectPtr<UMaterialInterface>> Materials;
		std::vector<bool> MaskedSlots;
		bool bRegistered = false;
	};
	struct FCompanion
	{
		int32 MeshIndex = -1; ///< Index in FITwinSceneTile::GltfMeshes
		TWeakObjectPtr<UStaticMeshComponent> Component;
		std::vector<TWeakObjectPtr<UMaterialInstanceDynamic>> Materials;
		std::vector<bool> MaskedSlots;
		std::vector<FMaterialOverrides> Overrides;
	};
	/// FITwinSceneTile::SelectionRevision reflected in MaskTexture and Companions (0: none)
	uint32 OutlinedRevision = 0;
	/// Alpha is zero for all features except those of the selected Elements
	std::shared_ptr<FITwinDynamicShadingBGRA8Property> MaskTexture;
	std::shared_ptr<FITwinDynamicShadingBGRA8Property> HiddenSlotsTexture;
	std::vector<FSource> Sources;
	std::vector<FCompanion> Companions;
	bool bRegisteredAsSource = false;

	void Rebuild(FITwinSceneTile& SceneTile);
	void CreateCompanion(FITwinSceneTile& SceneTile, int32 const MeshIndex);
	static void DestroyCompanion(FCompanion& Companion);
};
