/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingRenderer.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CoreMinimal.h>
#include <Clipping/ITwinClippingEnums.h>
#include <ITwinModelType.h>
#include <Templates/PimplPtr.h>
#include <optional>

#include "ITwinClippingRenderer.generated.h"


class UMaterialParameterCollection;
class UMaterialParameterCollectionInstance;
class ACesium3DTileset;
class UCesiumPolygonRasterOverlay;

class FITwinTilesetAccess;
class UITwinClippingEffectManager;
class UITwinClippingMPCHolder;
struct FITwinRayTraceInput;
struct FITwinClippingBoxInfo;
struct FITwinClippingPlaneInfo;


/// Responsible for the rendering of cutout effects.
UCLASS()
class UITwinClippingRenderer : public UObject
{
	GENERATED_BODY()
public:
	UITwinClippingRenderer();

	void SetEffectManager(UITwinClippingEffectManager* InManager);

	void OnClippingInstanceArrayResized(EITwinClippingPrimitiveType PrimitiveType);

	//! Encode the flipping of the clipping effect in the Material Parameter Collection, so that it can be
	//! accessed in shaders.
	bool EncodeFlippingInMPC(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex);

	bool UpdateBoxPropertiesInMPC(FITwinClippingBoxInfo const& BoxInfo, int32 BoxIndex);

	bool UpdatePlanePropertiesInMPC(FITwinClippingPlaneInfo const& PlaneInfo, int32 PlaneIndex);

	UFUNCTION()
	void OnEffectPropertiesModified(EITwinClippingPrimitiveType EffectType, int32 Index);


	/// Update the given tileset by activating the different clipping effects to it, and deactivating any
	/// effect that should no longer affect it.
	void UpdateTileset(FITwinTilesetAccess const& TilesetAccess,
		std::optional<EITwinClippingPrimitiveType> const& SpecificPrimitiveType = std::nullopt);
	/// Update clipping effects in all tilesets in the scene.
	void UpdateAllTilesets(std::optional<EITwinClippingPrimitiveType> const& SpecificPrimitiveType = std::nullopt);

	//! Registers a new tileset in the renderer, so that it can be updated with the current clipping effects.
	void RegisterTileset(FITwinTilesetAccess const& TilesetAccess);

	//! Returns true if the given world position is cut out by any of the clipping effects affecting the
	//! layer identified by the given model identifier.
	bool ShouldCutOut(FVector const& AbsoluteWorldPosition, ITwin::ModelLink const& ModelIdentifier,
		UCesiumPolygonRasterOverlay const* RasterOverlay) const;

	/// Recompute the model->group mapping from the current influence sets, and push the resulting
	/// per-primitive masks to the MPC. Returns true if the mapping actually changed, in which case
	/// callers must refresh the Custom Primitive Data of ALL tilesets, not just the one they own.
	/// Must be called on influence/registration changes, NOT on geometry-only edits
	/// (see #OnEffectPropertiesModified).
	bool RebuildModelGroups();
	bool UpdateActivationMasksInMPC();

#if WITH_EDITOR
	/// Globally activate/deactivate all effects of given type, at given level. This is for debugging, only
	/// possible in Editor.
	void ActivateEffects(EITwinClippingPrimitiveType Type, EITwinClippingEffectLevel Level, bool bActivate);
#endif // WITH_EDITOR


private:
	/// Retrieve the Material Parameter Collection for clipping.
	UMaterialParameterCollection* GetMPCClipping();
	UMaterialParameterCollectionInstance* GetMPCClippingInstance();

	bool EncodeBoxFlippingInMPC(int32 BoxIndex);

	
	template <EITwinClippingPrimitiveType PrimitiveType>
	bool TUpdatePrimitiveCountInMPC();

	struct FTilesetUpdateInfo;
	void UpdateTileset_Planes(ACesium3DTileset& Tileset, ITwin::ModelLink const& ModelIdentifier,
		FTilesetUpdateInfo& UpdateInfo);
	void UpdateTileset_Boxes(ACesium3DTileset& Tileset, ITwin::ModelLink const& ModelIdentifier,
		FTilesetUpdateInfo& UpdateInfo);
	void UpdateTileset_Polygons(FITwinTilesetAccess const& TilesetAccess,
		FTilesetUpdateInfo& UpdateInfo);


	/// Returns cut-out value (understood as opacity: 0 if cut out, 1 if not) for box primitives at given
	/// point.
	double GetClippingValue_Boxes(FVector const& AbsoluteWorldPosition, ITwin::ModelLink const& ModelIdentifier) const;

	/// Returns cut-out value (understood as opacity: 0 if cut out, 1 if not) for plane primitives at given
	/// point.
	double GetClippingValue_Planes(FVector const& AbsoluteWorldPosition, ITwin::ModelLink const& ModelIdentifier) const;


private:
	UPROPERTY()
	TWeakObjectPtr<UITwinClippingEffectManager> EffectManager;

	UPROPERTY(Category = "iTwin",
		VisibleAnywhere)
	UITwinClippingMPCHolder* ClippingMPCHolder = nullptr;

	class FImpl;
	TPimplPtr<FImpl> Impl;
};
