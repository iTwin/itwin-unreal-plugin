/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSplineHelper.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include <GameFramework/Actor.h>
#include <ITwinModelType.h>
#include <UObject/ObjectMacros.h>
#include <Spline/ITwinSplineEnums.h>
#include <memory>
#include <optional>
#include "ITwinSplineHelper.generated.h"

namespace AdvViz::SDK
{
	class ISpline;
}

class USceneComponent;
class USplineComponent;
class USplineMeshComponent;
class UStaticMeshComponent;
class UCesiumGlobeAnchorComponent;
class ACesiumCartographicPolygon;


//! This class is used to edit a spline.
//! It handles the synchronization of points between a USplineComponent (to which instances
//! of UStaticMeshComponent and USplineMeshComponent are attached for the display) and an
//! AdvViz::SDK::ISpline (used to save the data on a server).
UCLASS()
class ITWINRUNTIME_API AITwinSplineHelper : public AActor
{
	GENERATED_BODY()

public:
	// Quick workaround to pass SplineUsage parameter to the constructor
	struct [[nodiscard]] FSpawnContext
	{
		FSpawnContext(EITwinSplineUsage SplineUsage);
		~FSpawnContext();
	};

	AITwinSplineHelper();

	virtual void Tick(float DeltaTime) override;

	//! Returns the USplineMeshComponent of this spline helper.
	USplineComponent* GetSplineComponent() const { return SplineComponent.Get(); }

	//! Returns the AdvViz::SDK::ISpline of this spline helper.
	std::shared_ptr<AdvViz::SDK::ISpline> GetAVizSpline() const;

	//! Returns the number of points in the spline.
	int32 GetNumberOfSplinePoints() const;

	//! Returns whether the spline is a closed loop or not.
	bool IsClosedLoop() const;

	//! Specify whether the spline is a closed loop or not.
	void SetClosedLoop(bool bInClosedLoop, bool bUpdateSpline = true);

	//! Initializes the current spline helper, and does an automatic transfer/update of the data from the
	//! USplineMeshComponent to the AdvViz::SDK::ISpline, or vice-versa depending on which one contains
	//! points.
	void Initialize(USplineComponent* splineComp, std::shared_ptr<AdvViz::SDK::ISpline> spline);

	//! Returns the spline's usage.
	EITwinSplineUsage GetUsage() const;

	//! Returns the model linked to this spline, if any.
	std::optional<ITwin::ModelLink> GetLinkedModel() const;

	//! Returns the spline's tangent mode.
	EITwinTangentMode GetTangentMode() const;

	//! Sets the tangent mode for all points (Linear or Smooth) and recomputes the tangents automatically.
	//! It does nothing for the Custom mode, which should be set for points individually.
	void SetTangentMode(const EITwinTangentMode mode);

	//! Given a mesh component (obtained by a line tracing operation after a user click for example)
	//! it returns the associated point index in this spline.
	int32 FindPointIndexFromMeshComponent(UStaticMeshComponent* meshComp) const;

	//! Returns the associated Cesium cartographic polygon (if any).
	ACesiumCartographicPolygon* GetCartographicPolygon() const;

	//! Sets the associated Cesium cartographic polygon.
	void SetCartographicPolygon(ACesiumCartographicPolygon* polygon);

	//! Sets the current transformation of the spline. markSplineForSaving should be true to ensure
	//! that the change will be saved on the server, but false if it's called in a loading operation.
	void SetTransform(const FTransform& NewTransform, bool markSplineForSaving);

	//! Gets the location of the spline point at the given index.
	FVector GetLocationAtSplinePoint(int32 pointIndex) const;

	//! Sets the location of the spline point at the given index.
	void SetLocationAtSplinePoint(int32 pointIndex, const FVector& location);

	//! Includes the current spline in the given box (using points in world space).
	bool IncludeInWorldBox(FBox& Box) const;

	//! Returns the minimum number of points to build a valid spline. The returned value depends on whether
	//! the spline is closed or not.
	int32 MinNumberOfPointsForValidSpline() const;

	//! Returns whether a point can be removed without inducing a degenerated spline.
	bool CanDeletePoint() const;

	//! Deletes the point at the given index.
	void DeletePoint(int32 pointIndex);

	//! Duplicates the point at the given index.
	void DuplicatePoint(int32 pointIndex);

	//! Duplicates the point at the given index, using the given new position to detect which of the 2 points
	//! should be moved (but the method doesn't actually move it).
	//! If it's the new point, pointIndex stays the same.
	//! If it's the existing point, pointIndex is incremented by 1.
	void DuplicatePoint(int32& pointIndex, FVector& newWorldPosition);

	//! The globe anchor is a constraint ensuring that the spline helper is correctly
	//! placed on the earth surface.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cesium")
	UCesiumGlobeAnchorComponent* GlobeAnchor;

private:
	struct FImpl;
	TPimplPtr<FImpl> Impl;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SplineMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> PointMesh;

	UPROPERTY()
	TObjectPtr<USplineComponent> SplineComponent;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> PointMeshComponents;

	UPROPERTY()
	TArray<TObjectPtr<USplineMeshComponent>> SplineMeshComponents;

	// Optional pointer to a cartographic polygon (can be left null)
	UPROPERTY()
	TObjectPtr<ACesiumCartographicPolygon> CartographicPolygon;
};
