/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClipping3DTilesetHelper.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Cesium3DTilesetLifecycleEventReceiver.h>

#include <ITwinModelType.h>

#include "ITwinClipping3DTilesetHelper.generated.h"

class AITwinClippingTool;
class UITwinClippingRenderer;
class UITwinClippingEffectManager;
class ACesium3DTileset;
class UPrimitiveComponent;
class FITwinTilesetAccess;
class FITwinClippingModelGroups;
class UCesiumPolygonRasterOverlay;

/// Used in Cesium lifecycle mechanism in order to:
///   - customize mesh components with the appropriate flags to activate the clipping primitives associated
///     to the current tileset.
///   - filter out points which should be excluded in collision meshes (see ITwinClippingTool::ShouldCutOut).
UCLASS()
class UITwinClipping3DTilesetHelper : public UObject, public ICesium3DTilesetLifecycleEventReceiver
{
	GENERATED_BODY()
public:
	UITwinClipping3DTilesetHelper();
	void InitWith(FITwinTilesetAccess const& TilesetAccess);
	void SetModelIdentifier(const ITwin::ModelLink& InModelIdentifier);

	void OnTileMeshPrimitiveLoaded(ICesiumLoadedTilePrimitive& TilePrim) override;

	/// Actually applies the Custom Primitive Data parameters to the given mesh component.
	void ApplyCPDFlagsToMeshComponent(UPrimitiveComponent& Component) const;

	/// Applies the CPD parameters to all meshes belonging to the given tileset.
	void ApplyCPDFlagsToAllMeshComponentsInTileset(const ACesium3DTileset& Tileset);

	/// Update the Custom Primitive Data values depending on current activation of the clipping planes and
	/// boxes, and return true if at least one value was modified.
	bool UpdateCPDFlagsFromClippingSelection(FITwinClippingModelGroups const& ModelGroups);

	void SetClippingTool(const AITwinClippingTool* InClippingTool);
	void SetCutoutOverlay(const UCesiumPolygonRasterOverlay* InPolygonRasterOverlay);

	/// Return whether the given world position should be cut out (i.e. excluded from collision meshes).
	bool ShouldCutOut(FVector const& AbsoluteWorldPosition) const;

private:
	ITwin::ModelLink ModelIdentifier; // Identifies the iModel/RealityData/GlobalMapLayer the tileset belongs to.
	TWeakObjectPtr<const UITwinClippingRenderer> ClippingRenderer;
	TWeakObjectPtr<const UCesiumPolygonRasterOverlay> CutoutOverlayPtr;
	// Identifies this model's clipping configuration; the per-primitive masks live in
	// MPC_Clipping. See FITwinClippingModelGroups and ClippingCommon.ush.
	float ScalarClippingModelGroupId = 0.f;
};
