/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTracingHelper.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
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


/// Helper to trace a ray in the scene, and collecting information on the impacted iTwin element.
class FITwinTracingHelper
{
public:
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
