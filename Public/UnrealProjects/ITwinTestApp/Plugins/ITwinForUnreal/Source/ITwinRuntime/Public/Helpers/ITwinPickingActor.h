/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPickingActor.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ITwinPickingActor.generated.h"

class AITwinIModel;


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

	DECLARE_EVENT_OneParam(AITwinPickingActor, FElementPicked, FString);
	FElementPicked& OnElementPicked() { return ElementPickedEvent; }

private:
	FElementPicked ElementPickedEvent;
};
