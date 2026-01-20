/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingCustomPrimitiveDataHelper.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Cesium3DTilesetLifecycleEventReceiver.h>

#include <ITwinModelType.h>

#include "ITwinClippingCustomPrimitiveDataHelper.generated.h"

class AITwinClippingTool;
class ACesium3DTileset;
class UPrimitiveComponent;

/// Used in Cesium lifecycle mechanism in order to customize mesh component with the appropriate flags to
/// activate the clipping primitives associated to the current tileset.
UCLASS()
class UITwinClippingCustomPrimitiveDataHelper : public UObject, public ICesium3DTilesetLifecycleEventReceiver
{
	GENERATED_BODY()
public:
	void SetModelIdentifier(const ITwin::ModelLink& InModelIdentifier);

	void OnTileMeshPrimitiveLoaded(ICesiumLoadedTilePrimitive& TilePrim) override;

	/// Actually applies the Custom Primitive Data parameters to the given mesh component.
	void ApplyCPDFlagsToMeshComponent(UPrimitiveComponent& Component) const;

	/// Applies the CPD parameters to all meshes belonging to the given tileset.
	void ApplyCPDFlagsToAllMeshComponentsInTileset(const ACesium3DTileset& Tileset);

	/// Update the Custom Primitive Data values depending on current activation of the clipping planes and
	/// boxes, and return true if at least one value was modified.
	bool UpdateCPDFlagsFromClippingSelection(const AITwinClippingTool& ClippingTool);

private:
	ITwin::ModelLink ModelIdentifier; // Identifies the iModel/RealityData/GlobalMapLayer the tileset belongs to.

	// For internal reasons (see ITwinClippingTool.cpp for details), there are currently up to 32 planes and
	// 32 cubes, and we encode them by groups of 16.
	// See Shaders/ITwin/GetPlanesClipping.ush and Shaders/ITwin/GetBoxClipping.ush as well as the material
	// graph in ITwin/Materials/MF_GlobalClipping.uasset
	float ScalarActivePlanes_0_15 = 0.f;
	float ScalarActivePlanes_16_31 = 0.f;

	float ScalarActiveBoxes_0_15 = 0.f;
	float ScalarActiveBoxes_16_31 = 0.f;
};
