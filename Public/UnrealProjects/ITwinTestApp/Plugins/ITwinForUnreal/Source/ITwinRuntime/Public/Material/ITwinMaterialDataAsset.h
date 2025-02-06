/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMaterialDataAsset.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <CoreMinimal.h>

#include <Engine/DataAsset.h>
#include <Containers/Map.h>


#include "ITwinMaterialDataAsset.generated.h"


/**
 * Stores the ITwinMaterial's parameters in UE format, as a map of strings.
 */
UCLASS()
class ITWINRUNTIME_API UITwinMaterialDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:

	/**
	* All relevant material parameters.
	*/
	UPROPERTY(
		EditAnywhere,
		AssetRegistrySearchable,
		Category = "ITwin",
		meta = (DisplayName = "Material Parameters"))
	TMap<FString, FString> MaterialParameters;
};
