/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTilesetAccess.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#pragma once

#include <ITwinModelType.h>
#include <Templates/Function.h>
#include <UObject/WeakObjectPtrTemplates.h>

class AActor;
class ACesium3DTileset;
class AITwinDecorationHelper;
struct ITwinSceneInfo;
class UCesiumPolygonRasterOverlay;
class FITwinTilesetAccess;

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

	using VisitTilesetFunction = TFunction<void(FITwinTilesetAccess const&)>;
	ITWINRUNTIME_API void IterateAllITwinTilesets(const VisitTilesetFunction& VisitFunc, class UWorld* World);

	//! This is the screenspace error it is best limiting oneself to with Google 3D Tiles,
	//! to avoid huge memory usage and download times
	ITWINRUNTIME_API extern const double GOOGLE3D_BEST_SCREENSPACE_ERROR;

	ITWINRUNTIME_API TUniquePtr<FITwinTilesetAccess> GetTilesetAccess(AActor* Actor);
}


/// Helper to modify a 3D tileset actor and notify the persistence manager if needed.
/// Introduced to avoid code duplication between AITwinIModel and AITwinRealityData.
class ITWINRUNTIME_API FITwinTilesetAccess
{
public:
	FITwinTilesetAccess(AActor* TilesetOwnerActor);
	virtual ~FITwinTilesetAccess();

	virtual TUniquePtr<FITwinTilesetAccess> Clone() const = 0;

	bool IsValid() const { return TilesetOwner.IsValid(); }

	// Persistence management
	virtual ITwin::ModelDecorationIdentifier GetDecorationKey() const = 0;
	virtual AITwinDecorationHelper* GetDecorationHelper() const = 0;

	void ApplyLoadedInfo(ITwinSceneInfo const& LoadedSceneInfo, bool bIsModelFullyLoaded) const;

	virtual const ACesium3DTileset* GetTileset() const;
	virtual ACesium3DTileset* GetMutableTileset() const;
	bool HasTileset() const;

	// Visibility
	void HideTileset(bool bHide) const;
	bool IsTilesetHidden() const;

	/// Returns the tileset quality as a percentage (value in range [0;1])
	float GetTilesetQuality() const;

	/// Adjust the tileset quality, given a percentage (value in range [0;1])
	void SetTilesetQuality(float Value) const;

	/// Returns the model manual "offset" (= scene placement customization).
	void GetModelOffset(FVector& Pos, FVector& Rot) const;

	/// Sets the model manual "offset" (= scene placement customization), and persist it to Cloud.
	void SetModelOffset(const FVector& Pos, const FVector& Rot) const;

	virtual void RefreshTileset() const;

protected:
	virtual void OnModelOffsetLoaded() const;

private:
	TWeakObjectPtr<AActor> TilesetOwner;
};
