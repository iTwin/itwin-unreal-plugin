/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSplineHelper.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"

#include <Spline/ITwinSplineEnums.h>

#include <Containers/Map.h>
#include <GameFramework/Actor.h>
#include <ITwinModelType.h>
#include <UObject/ObjectMacros.h>
#include <Templates/Function.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Visualization/Spline.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <memory>
#include <optional>
#include <set>
#include "ITwinSplineHelper.generated.h"


class USceneComponent;
class USplineComponent;
class USplineMeshComponent;
class UStaticMeshComponent;
class UCesiumGlobeAnchorComponent;
class ACesiumCartographicPolygon;
class ACesiumGeoreference;
class FITwinTilesetAccess;
class UITwinSplineHelper2DWidgetImpl;
enum class EITwinAnimPathShaderScalarParam : uint8;

//! This class is used to edit a spline.
//! It handles the synchronization of points between a USplineComponent (to which instances
//! of UStaticMeshComponent and USplineMeshComponent are attached for the display) and an
//! AdvViz::SDK::ISpline (used to save the data on a server).
UCLASS()
class ITWINRUNTIME_API AITwinSplineHelper : public AActor
{
	GENERATED_BODY()

public:
	static bool Is2DDrawingEnabled();

	static AITwinSplineHelper* FindClosestSplineToScreenPosition(const FVector2D& ScreenPosition,
		FVector::FReal& OutClosestDistance,
		const TFunction<bool(const AITwinSplineHelper&)>& IgnoreSpline = {});

	// Quick workaround to pass SplineUsage parameter to the constructor
	struct [[nodiscard]] FSpawnContext
	{
		FSpawnContext(EITwinSplineUsage SplineUsage);
		~FSpawnContext();
	};

	AITwinSplineHelper();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetActorHiddenInGame(bool bNewHidden) override;
	virtual void Tick(float DeltaTime) override;

	//! Sets spline width to visualize route width for example. It does not impact the actual data of the spline.
	void SetFixedSplineWidth(float Width);

	//! Returns the USplineMeshComponent of this spline helper.
	USplineComponent* GetSplineComponent() const { return SplineComponent.Get(); }

	//! Returns the AdvViz::SDK::ISpline of this spline helper.
	AdvViz::SDK::ISplinePtr GetAVizSpline() const;

	//! Sets the AdvViz::SDK::ISpline of this spline helper.
	void SetAVizSpline(AdvViz::SDK::ISplinePtr const& Spline);

	//! Returns the identifier of the underlying AdvViz::SDK::ISpline, if any.
	AdvViz::SDK::RefID GetAVizSplineId() const;

	//! Returns the number of points in the spline.
	int32 GetNumberOfSplinePoints() const;

	//! Returns whether the spline is a closed loop or not.
	bool IsClosedLoop() const;

	//! Specify whether the spline is a closed loop or not.
	void SetClosedLoop(bool bInClosedLoop, bool bUpdateSpline = true);

	//! Initializes the current spline helper, and does an automatic transfer/update of the data from the
	//! USplineMeshComponent to the AdvViz::SDK::ISpline, or vice-versa depending on which one contains
	//! points.
	void Initialize(USplineComponent* splineComp, AdvViz::SDK::ISplinePtr spline);

	//! Sets a custom label for this spline helper, which will be used in the editor to identify it.
	void SetCustomActorLabel(const FString& InCustomActorLabel);

	//! Returns true if the spline is currently being created in interactive mode (i.e. the user is adding
	//! points one by one, and the spline is not finished yet).
	UFUNCTION(Category = "iTwin Spline",
		BlueprintCallable)
	bool IsInteractiveCreationInProgress() const;

	//! Set whether the spline is currently being created in interactive mode.
	UFUNCTION(Category = "iTwin Spline",
		BlueprintCallable)
	void SetInteractiveCreationInProgress(bool bInProgress);

	//! Returns the spline's usage.
	EITwinSplineUsage GetUsage() const;

	//! Returns true if the spline is editable, i.e. if the user can modify its points and insert new points.
	UFUNCTION(Category = "iTwin Spline",
		BlueprintCallable)
	bool IsPointEditionAllowed() const;

	//! Returns the model(s) linked to this spline, if any.
	std::set<ITwin::ModelLink> GetLinkedModels() const;

	//! Returns the spline's tangent mode.
	EITwinTangentMode GetTangentMode() const;

	//! Sets the tangent mode for all points (Linear or Smooth) and recomputes the tangents automatically.
	//! It does nothing for the Custom mode, which should be set for points individually.
	void SetTangentMode(const EITwinTangentMode mode);

	//! Sets tangents at a spline point with automatic computation based on a tightness parameter.
	//! Should only be used in Smooth tangent mode. 
	//! @param Tightness Controls the curve sharpness: 
	//!		0.0 = completely flat/smooth (no tangent influence)
	//!		1.0 = sharp turn (full tangent magnitude)
	//!		Default smooth mode uses 0.5
	void SetTightness(int32 PointIndex, float InTightness = 0.5f);

	//! Gets the tangent tightness value at a spline point.
	//! @return Tightness value (0.0 to 1.0), or 0.0 if the calculation is invalid
	float GetTightness(int32 PointIndex) const;

	//! Return the last point mesh component, if any.
	UStaticMeshComponent* GetLastPointMeshComponent() const;

	//! Given a mesh component (obtained by a line tracing operation after a user click for example), return
	//! the associated point index in this spline, if any (else return INDEX_NONE).
	int32 FindPointIndexFromMeshComponent(UStaticMeshComponent* MeshComp) const;

	//! Set the visibility of all points (3D point mesh components and 2D pins).
	void SetPointsHiddenInGame(bool bNewHidden) const;


	//! Set the visibility of all 3D point mesh components.
	void Set3DPointsHiddenInGame(bool bNewHidden) const;

	//! Set the visibility of all 3D spline mesh components.
	void Set3DSplinesHiddenInGame(bool bNewHidden) const;

	//! Turn on/off the visibility of the (3D) spline mesh components (the ribbon).
	UFUNCTION(Category = "iTwin Spline",
		BlueprintCallable)
	void SetDraw3DRibbon(bool bInDraw3DRibbon);

	//! Turn on/off the visibility of the 3D point mesh components (legacy mode).
	UFUNCTION(Category = "iTwin Spline",
		BlueprintCallable)
	void SetDraw3DPoints(bool bInDraw3DPoints);


	//! Turn on/off the visibility of the widget representing the spline in 2D.
	UFUNCTION(Category = "iTwin Spline",
		BlueprintCallable)
	void SetDraw2DElements(bool bInDraw2DElements);

	//! Set thickness of 2D representation of the spline.
	UFUNCTION(Category = "iTwin Spline",
		BlueprintCallable)
	void Set2DThickness(float InThickness);

	//! Returns whether the 2D elements should be updated.
	bool NeedsUpdate2DElements() const;

	//! Sets whether the 2D elements should be updated.
	void SetNeedsUpdate2DElements(bool bNeedsUpdate);

	//! Given a spline mesh component (obtained by a line tracing operation after a user click for example),
	//! return the associated segment index in this spline, if any (else return INDEX_NONE).
	int32 FindSegmentIndexFromSplineComponent(USplineMeshComponent* SplineMeshComp) const;

	//! Returns the associated Cesium cartographic polygon (if any).
	ACesiumCartographicPolygon* GetCartographicPolygonForTileset(FITwinTilesetAccess const& TilesetAccess) const;
	ACesiumCartographicPolygon* GetCartographicPolygonForGeoref(TSoftObjectPtr<ACesiumGeoreference> const& Georef) const;
	bool HasCartographicPolygon() const;

	//! Sets the associated Cesium cartographic polygon.
	void SetCartographicPolygonForTileset(ACesiumCartographicPolygon* polygon, FITwinTilesetAccess const& TilesetAccess);
	void SetCartographicPolygonForGeoref(ACesiumCartographicPolygon* polygon, TSoftObjectPtr<ACesiumGeoreference> const& Georef);

	//! Clones the cartographic polygon associated to this spline (if any) for the given tileset geo-reference.
	ACesiumCartographicPolygon* ClonePolygonForTileset(FITwinTilesetAccess const& TilesetAccess);
	ACesiumCartographicPolygon* ClonePolygonForGeoref(TSoftObjectPtr<ACesiumGeoreference> const& Georef);

	//! Deletes all cartographic polygons owned by this spline.
	void DeleteCartographicPolygons(TFunction<void(ACesiumCartographicPolygon*)> const& BeforeDeleteCallback);

	template <typename TFunc>
	void IterateAllCartographicPolygons(TFunc const& Func) const;

	//! Sets the current transformation of the spline. markSplineForSaving should be true to ensure
	//! that the change will be saved on the server, but false if it's called in a loading operation.
	void SetTransform(const FTransform& NewTransform, bool bMarkSplineForSaving);

	//! Returns the current transformation for the selection gizmo (we should return the position of the
	//! barycenter rather than the actor location, which is confounded with the first spline point when
	//! interactive creation mode is used).
	FTransform GetTransformForUserInteraction() const;

	//! Sets the transformation from the selection gizmo (user interaction).
	void SetTransformFromUserInteraction(const FTransform& NewTransform);

	//! Gets the location of the spline point at the given index.
	FVector GetLocationAtSplinePoint(int32 pointIndex) const;

	//! Sets the location of the spline point at the given index.
	void SetLocationAtSplinePoint(int32 pointIndex, const FVector& location);

	//! Includes the current spline in the given box (using points in world space).
	bool IncludeInWorldBox(FBox& Box) const;

	//! Test line intersection with the polygon defined by the spline's points.
	bool DoesLineIntersectSplinePolygon(const FVector& Start, const FVector& End) const;

	//! Returns the minimum number of points to build a valid spline. The returned value depends on whether
	//! the spline is closed or not.
	int32 MinNumberOfPointsForValidSpline() const;

	//! Returns whether a point can be removed without inducing a degenerated spline.
	bool CanDeletePoint() const;

	//! Deletes the point at the given index.
	//! @note The caller is responsible for re-establishing the point selection afterwards:
	//! the selection is reset when the deleted point was the selected one, but indices after
	//! pointIndex shift down and are NOT adjusted. See AITwinSplineTool::DeleteSelectedPoint.
	//! Returns true if the point was successfully deleted.
	bool DeletePoint(int32 pointIndex);

	//! Duplicates the point at the given index.
	bool DuplicatePoint(int32 pointIndex);

	//! Duplicates the point at the given index, using the given new position to detect which of the 2 points
	//! should be moved (but the method doesn't actually move it).
	//! If it's the new point, pointIndex stays the same.
	//! If it's the existing point, pointIndex is incremented by 1.
	bool DuplicatePoint(int32& pointIndex, FVector& newWorldPosition);

	//! Inserts a new point at the given index. Returns the new point index (which will be PointIndex if it
	//! succeeded, or else INDEX_NONE).
	int32 InsertPointAt(const int32 PointIndex, FVector const& NewWorldPosition);

	//! Activates or deactivates this cut-out polygon in the given tileset.
	void ActivateCutoutEffect(FITwinTilesetAccess const& TilesetAccess, bool bActivate,
		bool bIsCreatingSpline = false);

	//! Returns whether the effect induced by this spline is enabled.
	bool IsEnabledEffect() const;

	//! Set whether the effect induced by this spline is enabled or not.
	void EnableEffect(bool bEnable);

	//! Returns whether the cut-out effect is inverted.
	bool IsInvertedCutoutEffect() const;

	//! Set whether we invert this cut-out polygon effect in the given tileset.
	void InvertCutoutEffect(FITwinTilesetAccess const& TilesetAccess, bool bInvert);

	//! Select/deselect this spline.
	void SetSelected(bool bSelected);
	//! Returns whether this spline is selected.
	bool IsSelected() const;

	//! Set the selected point (use -1 to reset selection to none).
	void SetSelectedPointIndex(int32 PointIndex);
	//! Returns the index of the selected control point, if any, or -1 if none is selected.
	int32 GetSelectedPointIndex() const;

	bool IsUsedForPathAnim() const;

	//! Set a scalar parameter value for the path animation shader. (In practice, those are stored as
	//! per-primitive data in the spline mesh components, and used by the shader to adapt the path look
	//! (lanes, two-way, left hand drive etc.).
	void SetPathAnimShaderScalarParameterValue(EITwinAnimPathShaderScalarParam Param, float Value,
		bool bApplyToMeshComponents = true);
	//! Transfer all path animation shader parameters to the spline mesh components (to be called after a
	//! batch of changes).
	void TransferPathAnimShaderParametersToMeshes();

	bool IsUsedForPopulation() const;

	//! The globe anchor is a constraint ensuring that the spline helper is correctly
	//! placed on the earth surface.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "iTwin Spline")
	UCesiumGlobeAnchorComponent* GlobeAnchor;

private:
	void Update2DWidgetVisibility();

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

	// Per geo-reference cartographic polygons (only relevant for cut-out usage).
	UPROPERTY()
	TMap< TSoftObjectPtr<ACesiumGeoreference>, TObjectPtr<ACesiumCartographicPolygon> > PerGeorefPolygonMap;

	//! Whether we draw the 3D ribbon (spline mesh components). Useful when the ribbon width has a physical
	//! meaning (e.g. route width), but can be turned off when only the spline shape matters.
	UPROPERTY(Category = "iTwin Spline",
		VisibleAnywhere,
		BlueprintSetter = SetDraw3DRibbon)
	bool bDraw3DRibbon = false;

	//! Whether we draw the spline's point as 3D elements (legacy mode).
	UPROPERTY(Category = "iTwin Spline",
		VisibleAnywhere,
		BlueprintSetter = SetDraw3DPoints)
	bool bDraw3DPoints = false;

	//! Whether the spline is drawn with 2D elements (widgets displayed in screen-space).
	UPROPERTY(Category = "iTwin Spline",
		VisibleAnywhere,
		BlueprintSetter = SetDraw2DElements)
	bool bDraw2DElements = true;

	UPROPERTY()
	TObjectPtr<UITwinSplineHelper2DWidgetImpl> OnScreen2DWidget;
};
