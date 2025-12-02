/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingMPCHolder.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include <ITwinClippingMPCHolder.generated.h>

class UMaterialParameterCollection;

UCLASS()
class ITWINRUNTIME_API UITwinClippingMPCHolder : public UActorComponent
{
	GENERATED_BODY()

public:
	UITwinClippingMPCHolder();
	UMaterialParameterCollection* GetMPCClipping() const { return MPC_Clipping; }

private:
	UPROPERTY()
	UMaterialParameterCollection* MPC_Clipping = nullptr;
};
