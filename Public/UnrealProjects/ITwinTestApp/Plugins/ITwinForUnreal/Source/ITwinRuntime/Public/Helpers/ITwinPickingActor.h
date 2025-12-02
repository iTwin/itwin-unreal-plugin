/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPickingActor.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include <GameFramework/Actor.h>
#include <Helpers/ITwinPickingOptions.h>
#include <Helpers/ITwinPickingResult.h>

#include "ITwinPickingActor.generated.h"

class AITwinIModel;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMaterialPicked, uint64, MaterialId, FString, IModelId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnElemPicked, FString, ElementId, FString, IModelId);


UCLASS()
class ITWINRUNTIME_API AITwinPickingActor : public AActor
{
	GENERATED_BODY()
public:
	/// Determine the position some properties of the first visible object below the mouse cursor in the
	/// viewport. Does _not_ select the Element (in case of iModel geometry).
	/// \param ElementId In case of iModel geometry, return the ElementId of the face hit.
	/// \param MousePosition Output for the mouse cursor position.
	/// \param ThisIModelOnly Only consider intersections with this iModel's geometry. Pass nullptr to accept
	///		all intersections from any kind of geometry.
	/// \param VisibleHit Output for the full hit structure of the first visible impact.
	UFUNCTION(BlueprintCallable, Category = "iTwin")
	void PickObjectAtMousePosition(FString& ElementId, FVector2D& MousePosition, AITwinIModel* ThisIModelOnly,
								   FHitResult& VisibleHit);

	/// Determine the position some properties of the first visible object below the mouse cursor in the
	/// viewport. Does _not_ necessarily select the Element (in case of iModel geometry): see Options parameter.
	/// \param ElementId In case of iModel geometry, return the ElementId of the face hit.
	/// \param MousePosition Output for the mouse cursor position.
	/// \param ThisIModelOnly Only consider intersections with this iModel's geometry. Pass nullptr to accept
	///		all intersections from any kind of geometry.
	/// \param VisibleHit Output for the full hit structure of the first visible impact.
	/// \param Options Contains flags instructing to select the Element and/or the material found at the impact
	///		point.
	UFUNCTION(BlueprintCallable, Category = "iTwin")
	void PickUnderCursorWithOptions(FString& ElementId, FVector2D& MousePosition, AITwinIModel* ThisIModelOnly,
									FHitResult& VisibleHit, FITwinPickingOptions const& Options);

	using FPickingResult = FITwinPickingResult;

	/// Variant of PickUnderCursorWithOptions only available in C++ (uint64 is not supported by BP).
	/// Useful to retrieve the picked material ID without having to use the OnMaterialPicked delegate.
	void PickUnderCursorWithOptions(FPickingResult& OutPickingResult, AITwinIModel* ThisIModelOnly,
		FITwinPickingOptions const& Options);

	UFUNCTION(BlueprintCallable, Category = "iTwin")
	void DeSelect(AITwinIModel* iModel);

	DECLARE_EVENT_OneParam(AITwinPickingActor, FElementPicked, FString);
	FElementPicked& OnElementPicked() { return ElementPickedEvent; }

	UPROPERTY(BlueprintAssignable, Category = "iTwin")
	FOnMaterialPicked OnMaterialPicked;

	UPROPERTY(BlueprintAssignable, Category = "iTwin")
	FOnElemPicked OnElemPicked;
private:
	FElementPicked ElementPickedEvent;
};
