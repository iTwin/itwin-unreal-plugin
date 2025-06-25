/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTilesetAccess.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#pragma once

#include <ITwinModelType.h>

class ACesium3DTileset;
class AITwinDecorationHelper;
struct ITwinSceneInfo;
class UCesiumPolygonRasterOverlay;

namespace ITwin
{
	//! Adjust the tileset quality, given a percentage (value in range [0;1])
	ITWINRUNTIME_API void SetTilesetQuality(ACesium3DTileset& Tileset, float Value);

	//! Returns the tileset quality as a percentage (value in range [0;1])
	ITWINRUNTIME_API float GetTilesetQuality(ACesium3DTileset const& Tileset);

	//! Adds support for cut-out polygons to the given tileset.
	ITWINRUNTIME_API void InitCutoutOverlay(ACesium3DTileset& Tileset);

	//! Returns the cut-out polygons overlay for the given tileset.
	ITWINRUNTIME_API UCesiumPolygonRasterOverlay* GetCutoutOverlay(ACesium3DTileset const& Tileset);
}


/// Helper to modify a 3D tileset actor and notify the persistence manager if needed.
/// Introduced to avoid code duplication between AITwinIModel and AITwinRealityData.
class ITWINRUNTIME_API FITwinTilesetAccess
{
public:
	FITwinTilesetAccess(AActor& TilesetOwnerActor);
	virtual ~FITwinTilesetAccess();

	// Persistence management
	virtual ITwin::ModelDecorationIdentifier GetDecorationKey() const = 0;
	virtual AITwinDecorationHelper* GetDecorationHelper() const = 0;

	void ApplyLoadedInfo(ITwinSceneInfo const& LoadedSceneInfo, bool bIsModelFullyLoaded);

	const ACesium3DTileset* GetTileset() const;
	ACesium3DTileset* GetMutableTileset();
	bool HasTileset() const;

	// Visibility
	void HideTileset(bool bHide);
	bool IsTilesetHidden() const;

	/// Returns the tileset quality as a percentage (value in range [0;1])
	float GetTilesetQuality() const;

	/// Adjust the tileset quality, given a percentage (value in range [0;1])
	void SetTilesetQuality(float Value);

	/// Returns the model manual "offset" (= scene placement customization).
	void GetModelOffset(FVector& Pos, FVector& Rot) const;

	/// Sets the model manual "offset" (= scene placement customization), and persist it to Cloud.
	void SetModelOffset(const FVector& Pos, const FVector& Rot);

protected:
	virtual void OnModelOffsetLoaded();

private:
	AActor& TilesetOwner;
};
