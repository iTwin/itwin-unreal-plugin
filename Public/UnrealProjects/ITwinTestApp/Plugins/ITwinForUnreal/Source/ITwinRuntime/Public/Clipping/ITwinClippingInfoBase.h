/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingInfoBase.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CoreMinimal.h>
#include <Containers/Array.h>
#include <ITwinModelType.h>

#include <Containers/Set.h>

#include <ITwinClippingInfoBase.generated.h>

class UITwinTileExcluderBase;
class UWorld;


USTRUCT()
struct FITwinClippingInfluenceInfo
{
	GENERATED_USTRUCT_BODY()

	void SetInfluenceNone();

	/// Whether the effect applies to all items of the given type.
	UPROPERTY()
	bool bInfluenceAll = true;

	/// Set of influenced items. Only relevant when bInfluenceAll is false.
	UPROPERTY()
	TSet<FString> SpecificIDs;
};

USTRUCT()
struct FITwinClippingInfoBase
{
	GENERATED_USTRUCT_BODY()

	virtual ~FITwinClippingInfoBase();

	bool IsEnabled() const { return bIsEnabled; }
	void SetEnabled(bool bInEnabled);

	void ActivateEffectAtTilesetLevel(bool bActivate) const;

	void SetInvertEffect(bool bInvert);
	virtual bool GetInvertEffect() const { return false; }

	virtual void DeactivatePrimitiveInExcluder(UITwinTileExcluderBase& Excluder) const;

	/// Returns whether the given model should be influenced by this clipping effect.
	/// Note that if the effect is disabled, this will always return false.
	/// (Google 3D tilesets use EITwinModelType::GlobalMapLayer as model type).
	bool ShouldInfluenceModel(const ITwin::ModelLink& ModelIdentifier) const;

	bool ShouldInfluenceFullModelType(EITwinModelType ModelType) const;
	void SetInfluenceFullModelType(EITwinModelType ModelType, bool bAll);

	void SetInfluenceSpecificModel(const ITwin::ModelLink& ModelIdentifier, bool bInfluence);

	//! Make the effect apply to none.
	void SetInfluenceNone();

	//! Get the bounds of the area influenced by this clipping primitive, in world coordinates.
	//! An invalid box will be returned if the cutout influences the Google tileset (which is infinite), or
	//! if it influences nothing.
	FBox const& GetInfluenceBoundingBox() const;

	void InvalidateInfluenceBoundingBox();
	void UpdateInfluenceBoundingBox(UWorld const* World);

	FBox const& GetUpToDateInfluenceBoundingBox(UWorld const* World);

protected:
	virtual void DoSetInvertEffect(bool bInvert);
	virtual void DoSetEnabled(bool bInEnabled);


private:
	inline FITwinClippingInfluenceInfo& MutableInfluenceInfo(EITwinModelType ModelType);
	inline FITwinClippingInfluenceInfo const& GetInfluenceInfo(EITwinModelType ModelType) const;

	/// Returns whether the given model should be influenced by this clipping effect, independently of the
	/// enabled state of the effect.
	inline bool DoesInfluenceModel(const ITwin::ModelLink& ModelIdentifier) const;

protected:

	/// Cesium tile exclusion helpers created for this primitive.
	UPROPERTY()
	TArray<TWeakObjectPtr<UITwinTileExcluderBase>> TileExcluders;


private:
	UPROPERTY()
	bool bIsEnabled = true;

	UPROPERTY()
	FITwinClippingInfluenceInfo IModelInfluenceInfo;

	UPROPERTY()
	FITwinClippingInfluenceInfo RealityDataInfluenceInfo;

	// Global Map Layers is the generic term for tilesets such as the Google tileset.
	UPROPERTY()
	FITwinClippingInfluenceInfo GlobalMapLayersInfluenceInfo;

	/// Bounding box of the zone influenced by this clipping primitive, in world coordinates.
	/// Beware it will need to be updated if the corresponding layer is moved/transformed.
	FBox InfluenceBoundingBox;
	bool bNeedsUpdateBoundingBox = true;

	friend class AITwinClippingTool;
};
