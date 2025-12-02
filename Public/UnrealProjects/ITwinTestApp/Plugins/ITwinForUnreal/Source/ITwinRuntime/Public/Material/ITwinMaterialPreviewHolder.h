/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMaterialPreviewHolder.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include <ITwinMaterialPreviewHolder.generated.h>

class UMaterialInterface;

/// Component holding resources (= base materials) for the material previews.
UCLASS()
class ITWINRUNTIME_API UITwinMaterialPreviewHolder : public UActorComponent
{
	GENERATED_BODY()

public:
	UITwinMaterialPreviewHolder();

	UPROPERTY(EditAnywhere, Category = "iTwin")
	UMaterialInterface* BaseMaterialMasked = nullptr;

	UPROPERTY(EditAnywhere, Category = "iTwin")
	UMaterialInterface* BaseMaterialTranslucent = nullptr;

	UPROPERTY(EditAnywhere, Category = "iTwin")
	UMaterialInterface* BaseMaterialGlass = nullptr;
};
