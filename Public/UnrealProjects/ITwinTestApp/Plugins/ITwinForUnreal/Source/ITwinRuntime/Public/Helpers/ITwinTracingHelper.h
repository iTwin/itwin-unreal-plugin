/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTracingHelper.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"
#include <Engine/HitResult.h>

#include <ITwinElementID.h>

#include <functional>
#include <optional>
#include <unordered_set>

class AITwinIModel;
class UPrimitiveComponent;



struct FITwinRayTraceInput
{
	FVector TraceStart = FVector::ZeroVector;
	FVector TraceDirection = FVector::XAxisVector;
	//std::optional<float> TraceExtentInMeters;
};


/// Helper to trace a ray in the scene, and collecting information on the impacted iTwin element.
class FITwinTracingHelper
{
public:
	/// Computes tracing start and direction from current mouse position (or a custom one, if
	/// CustomMousePosition is provided).
	static bool GetRayFromMousePosition(UWorld const* World,
		FVector2D& OutMousePosition,
		FITwinRayTraceInput& OutTraceInput,
		std::optional<FVector2D> const& CustomMousePosition = std::nullopt);

	/// Computes the start and direction of a ray trace for a given set of ratios on screen.
	/// For each ratio, the ray is computed from the corresponding position on screen. The function returns
	/// the number of successfully computed rays.
	/// A ratio of (0.5, 0.5) corresponds to the center of the screen, (0, 0) to the top left corner, and
	// (1, 1) to the bottom right corner.
	static int32 GetRayTraceInputsFromScreenRatios(const UObject* WorldContextObject,
		TArray<FVector2d> const& InScreenRatios,
		TArray<FITwinRayTraceInput>& OutTraceInputs);

	/// Computes the start and direction of a ray trace from the center of the screen.
	static bool GetRayToTraceFromScreenCenter(const UObject* WorldContextObject,
		FITwinRayTraceInput& OutTraceInput);


	FITwinTracingHelper();

	/// Add actors to ignore in intersection tests.
	void AddIgnoredActors(const TArray<AActor*>& ActorsToIgnore);
	void AddIgnoredActors(const TArray<const AActor*>& ActorsToIgnore);

	/// Add components to ignore in intersection tests.
	void AddIgnoredComponents(const TArray<UPrimitiveComponent*>& ComponentsToIgnore);

	ITwinElementID VisitElementsUnderCursor(UWorld const* World,
		FVector2D& OutMousePosition, FVector& OutTraceStart, FVector& OutTraceEnd,
		std::function<void(FHitResult const&, std::unordered_set<ITwinElementID>&)>&& HitResultHandler,
		std::optional<uint32> const& MaxUniqueElementsHit = std::nullopt,
		std::optional<float> const& CustomTraceExtentInMeters = std::nullopt,
		std::optional<FVector2D> const& CustomMousePosition = std::nullopt);

	/// Find the nearest impact located on the segment defined by a start position, a direction and a length,
	/// filtering out objects and/or iTwin elements currently invisible.
	bool FindNearestImpact(FHitResult& OutHitResult, UWorld const* World,
		FVector const& TraceStart, FVector const& TraceEnd);

	/// Checks whether the impact corresponds to an Element which can be picked, ie which is currently
	/// visible.
	bool PickVisibleElement(FHitResult const& HitResult, AITwinIModel& IModel, ITwinElementID& OutEltID,
		bool bSelectElement);

private:
	struct FImpl;
	TPimplPtr<FImpl> Impl;
};
