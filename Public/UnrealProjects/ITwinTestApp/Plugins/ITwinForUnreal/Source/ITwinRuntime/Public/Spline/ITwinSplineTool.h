/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSplineTool.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Templates/PimplPtr.h>
#include <ITwinInteractiveTool.h>
#include <Math/MathFwd.h>
#include <Spline/ITwinSplineEnums.h>
#include <memory>
#include "ITwinSplineTool.generated.h"

namespace AdvViz::SDK
{
	class ISplinesManager;
	class ISpline;
}

class AITwinPopulationTool;
class AITwinSplineHelper;
class ACesium3DTileset;
class FITwinTilesetAccess;

UENUM(BlueprintType)
enum class EITwinSplineToolMode : uint8
{
	Undefined,

	/**
	 * A new point will be added to the active spline upon mouse click, by tracing a ray in the scene (if no
	 * impact is found, no point is created).
     */
	InteractiveCreation
};

//! This class is used to select and edit splines.
//! At the moment it works with splines contained in Cesium Cartographic polygons, but it could later be
//! used with other type of splines (for animation and population paths/zones).
UCLASS()
class ITWINRUNTIME_API AITwinSplineTool : public AITwinInteractiveTool
{
	GENERATED_BODY()

public:
	AITwinSplineTool();

	virtual void Tick(float DeltaTime) override;

	//! Returns true if the spline manager used by this tool contains splines.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool HasSplines() const;

	//! Returns the selected spline.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	AITwinSplineHelper* GetSelectedSpline() const;

	//! Sets the selected spline.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetSelectedSpline(AITwinSplineHelper* splineHelper);

	//! Sets the selected point index (in the selected spline).
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetSelectedPointIndex(int32 pointIndex);

	//! Returns true if there is a selected spline and a selected point.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool HasSelectedPoint() const;

	//! Deletes the currently selected spline, and its associated cartographic polygon (if any).
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void DeleteSelectedSpline();

	//! Returns true if the current point can be deleted (for the cutout feature, it prevents
	//! having less than 3 points to keep a non-empty area).
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool CanDeletePoint() const;

	//! Deletes the selected point.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void DeleteSelectedPoint();

	//! Duplicate the selected point.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void DuplicateSelectedPoint();

	//! Enables the automatic duplication of the currently selected point when the user starts moving it.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void EnableDuplicationWhenMovingPoint(bool value);

	//! Returns the reference position and extent of the selected spline, if any. If no spline is currently
	//! selected, and there are splines in the scene, an union of all splines will be considered.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool GetSplineReferencePosition(FVector& RefLocation, FBox& OutBox) const;

	//! Returns the current spline tool mode.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	EITwinSplineToolMode GetMode() const;

	//! Sets the spline tool mode.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetMode(EITwinSplineToolMode NewMode);

	//! Toggle the interactive creation mode.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void ToggleInteractiveCreationMode();

	//! Returns the current spline tool destination usage.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	EITwinSplineUsage GetUsage() const;

	//! Sets the spline tool destination usage.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetUsage(EITwinSplineUsage NewUsage);

	//! Sets the target 3D Tileset for cut-out polygons.
	void SetCutoutTarget(FITwinTilesetAccess* CutoutTargetAccess);

	//! Sets the population tool associated to current context, if applicable.
	void SetPopulationTool(AITwinPopulationTool* InPopulationTool);

	//! Returns the selected spline's tangent mode.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	EITwinTangentMode GetTangentMode() const;

	//! Sets the selected spline's tangent mode.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetTangentMode(EITwinTangentMode TangentMode);

	//! Refresh the scene to apply the latest spline modifications.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void RefreshScene();

	//! Adds a new spline at specified position, for the given usage.
	bool AddSpline(FVector const& Position);

	//! Adds a spline loaded from the decoration service.
	bool LoadSpline(const std::shared_ptr<AdvViz::SDK::ISpline>& spline,
		FITwinTilesetAccess* CutoutTargetAccess = nullptr);

	//! Sets the AdvViz::SDK spline manager (which stores the data for splines and saves it on the decoration service).
	void SetSplinesManager(const std::shared_ptr<AdvViz::SDK::ISplinesManager>& splinesManager);

	// For a generic UI refresh
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSplineEditionEvent);
	UPROPERTY()
	FSplineEditionEvent SplineEditionEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSplineOrPointRemovedEvent);
	UPROPERTY()
	FSplineOrPointRemovedEvent SplinePointRemovedEvent;

	UPROPERTY()
	FSplineOrPointRemovedEvent SplineRemovedEvent;


protected:
	virtual void SetEnabledImpl(bool bValue) override;
	virtual bool IsEnabledImpl() const override;
	virtual bool DoMouseClickActionImpl() override;
	virtual bool HasSelectionImpl() const override;
	virtual FTransform GetSelectionTransformImpl() const override;
	virtual void SetSelectionTransformImpl(const FTransform & Transform) override;
	virtual void DeleteSelectionImpl() override;
	virtual void ResetToDefaultImpl() override;

private:
	class FImpl;
	TPimplPtr<FImpl> Impl;

	/// Target 3D Tileset for cut-out polygons.
	UPROPERTY()
	TWeakObjectPtr<ACesium3DTileset> CutoutTarget = nullptr;

	/// Population tool, if applicable.
	UPROPERTY()
	TWeakObjectPtr<AITwinPopulationTool> PopulationTool = nullptr;
};
