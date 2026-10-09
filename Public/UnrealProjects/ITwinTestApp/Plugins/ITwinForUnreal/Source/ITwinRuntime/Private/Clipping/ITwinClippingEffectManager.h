/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingEffectManager.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Clipping/ITwinClippingConstants.h>
#include <Clipping/ITwinClippingEnums.h>
#include <Clipping/ITwinClippingBoxInfo.h>
#include <Clipping/ITwinClippingCartographicPolygonInfo.h>
#include <Clipping/ITwinClippingPlaneInfo.h>

#include "ITwinClippingEffectManager.generated.h"


class AITwinSplineTool;
class AITwinPopulation;
class AITwinPopulationTool;
class AITwinSplineHelper;

/// Holds the different kinds of cutout effects, and the link with the Spline Tool and Population tool, which
/// are used to edit respectively the cutout polygons and the cutout cubes & planes.
UCLASS()
class UITwinClippingEffectManager : public UObject
{
	GENERATED_BODY()
public:
	UITwinClippingEffectManager();

	/// Returns the number of effects of the given type.
	inline int32 NumEffects(EITwinClippingPrimitiveType Type) const;

	/// Returns a mutable reference to the effect of the given type and index.
	inline FITwinClippingInfoBase& GetMutableEffect(EITwinClippingPrimitiveType Type, int32 Index);

	/// Returns a const reference to the effect of the given type and index.
	inline const FITwinClippingInfoBase& GetEffect(EITwinClippingPrimitiveType Type, int32 Index) const;

	inline const FITwinClippingBoxInfo& GetBoxEffect(int32 Index) const;
	inline const FITwinClippingPlaneInfo& GetPlaneEffect(int32 Index) const;
	inline const FITwinClippingCartographicPolygonInfo& GetPolygonEffect(int32 Index) const;

	/// Returns a unique identifier for the given effect.
	AdvViz::SDK::RefID GetEffectId(EITwinClippingPrimitiveType EffectType, int32 EffectIndex) const;

	/// Returns the index of the effect with the given identifier, or INDEX_NONE if not found.
	int32 GetEffectIndex(EITwinClippingPrimitiveType EffectType, AdvViz::SDK::RefID const& RefID) const;

	/// Returns the index of the cutout polygon corresponding to the given spline, or INDEX_NONE if not found.
	int32 GetCutoutPolygonIndex(AITwinSplineHelper const* Spline) const;

	void SetPopulationTool(AITwinPopulationTool* InPopulationTool);
	TWeakObjectPtr<AITwinPopulationTool> const& GetPopulationTool() const { return PopulationTool; }

	/// Make the Population Tool the active tool, with populations restricted to cutout cubes and planes.
	virtual TWeakObjectPtr<AITwinPopulationTool> ActivatePopulationTool(bool bUpdateTransformationMode = true);


	void SetSplineTool(AITwinSplineTool* SplineTool);
	TWeakObjectPtr<AITwinSplineTool> const& GetSplineTool() const { return SplineTool; }

	/// Make the Spline Tool the active tool, with usage restricted to cutout polygons.
	virtual TWeakObjectPtr<AITwinSplineTool> ActivateSplineTool();


	void RegisterCutoutPopulation(EITwinClippingPrimitiveType EffectType, AITwinPopulation* Population);

	/// Update the clipping information and/or rendering data upon the loading of clipping primitives.
	virtual void OnClippingInstancesLoaded(AITwinPopulation* Population, bool bUpdateEffectInfos);

	/// Return the spline corresponding to the given polygon index, or nullptr if the index is invalid.
	AITwinSplineHelper* GetCutoutSpline(int32 PolygonIndex) const;

	inline bool IsValidEffectIndex(EITwinClippingPrimitiveType EffectType, int32 Index) const;

	inline TWeakObjectPtr<AITwinPopulation> GetPopulation(EITwinClippingPrimitiveType Type) const;
	inline bool IsValidPopulationIndex(EITwinClippingPrimitiveType EffectType, int32 Index) const;

	/// Return whether the given effect should influence the given model.
	bool ShouldEffectInfluenceModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		const ITwin::ModelLink& ModelIdentifier) const;

	template <typename Func>
	void VisitClippingPrimitivesOfType(EITwinClippingPrimitiveType Type, Func const& Fun);

protected:
	/// Cutout box effects.
	UPROPERTY()
	TArray<FITwinClippingBoxInfo> ClippingBoxInfos;

	/// Cutout plane effects.
	UPROPERTY()
	TArray<FITwinClippingPlaneInfo> ClippingPlaneInfos;

	/// Cutout polygon effects.
	UPROPERTY()
	TArray<FITwinClippingCartographicPolygonInfo> ClippingPolygonInfos;


	UPROPERTY()
	TWeakObjectPtr<AITwinSplineTool> SplineTool;

	UPROPERTY()
	TWeakObjectPtr<AITwinPopulationTool> PopulationTool;

	UPROPERTY()
	TWeakObjectPtr<AITwinPopulation> PlanePopulation;

	UPROPERTY()
	TWeakObjectPtr<AITwinPopulation> BoxPopulation;


	friend class UITwinClippingEffectFactory;
};
