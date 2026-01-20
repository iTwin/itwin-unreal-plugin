/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinHDRIDataAsset.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <CoreMinimal.h>

#include <Engine/DataAsset.h>
#include <Containers/Map.h>


#include "ITwinHDRIDataAsset.generated.h"


/**
 * Stores the ITwinHDRI's parameters in UE format, as a map of strings.
 */
UCLASS()
class ITWINRUNTIME_API UITwinHDRIDataAsset : public UDataAsset
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
		meta = (DisplayName = "HDRI Parameters"))
	TMap<FString, FString> HDRIParameters;
};
