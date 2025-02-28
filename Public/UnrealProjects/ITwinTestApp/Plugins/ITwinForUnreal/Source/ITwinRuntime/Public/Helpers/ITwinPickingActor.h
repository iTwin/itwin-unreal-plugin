/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPickingActor.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include <Helpers/ITwinPickingOptions.h>
#include "ITwinPickingActor.generated.h"

class AITwinIModel;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMaterialPicked, uint64, MaterialId, FString, IModelId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnElemPicked, FString, ElementId);

UCLASS()
class ITWINRUNTIME_API AITwinPickingActor : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AITwinPickingActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "iTwin")
	void PickObjectAtMousePosition(FString& Id, FVector2D& MousePosition, AITwinIModel* iModel);

	UFUNCTION(BlueprintCallable, Category = "iTwin")
	void PickUnderCursorWithOptions(FString& Id, FVector2D& MousePosition, AITwinIModel* iModel,
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
