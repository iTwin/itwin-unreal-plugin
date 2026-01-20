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
	/// (Google 3D tilesets use EITwinModelType::GlobalMapLayer as model type).
	bool ShouldInfluenceModel(const ITwin::ModelLink& ModelIdentifier) const;

	bool ShouldInfluenceFullModelType(EITwinModelType ModelType) const;
	void SetInfluenceFullModelType(EITwinModelType ModelType, bool bAll);

	void SetInfluenceSpecificModel(const ITwin::ModelLink& ModelIdentifier, bool bInfluence);

	//! Make the effect apply to none.
	void SetInfluenceNone();

protected:
	virtual void DoSetInvertEffect(bool bInvert);
	virtual void DoSetEnabled(bool bInEnabled);


private:
	inline FITwinClippingInfluenceInfo& MutableInfluenceInfo(EITwinModelType ModelType);
	inline FITwinClippingInfluenceInfo const& GetInfluenceInfo(EITwinModelType ModelType) const;


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

	friend class AITwinClippingTool;
};
