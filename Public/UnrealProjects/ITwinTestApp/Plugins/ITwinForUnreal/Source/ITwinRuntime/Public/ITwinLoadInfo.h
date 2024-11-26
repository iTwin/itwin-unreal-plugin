/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinLoadInfo.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"

#include "ITwinLoadInfo.generated.h"

/// Gathers all identifiers and information identifying a unique loading.
USTRUCT(BlueprintType)
struct FITwinLoadInfo
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "iTwin|Loading")
		FString ITwinId;

	UPROPERTY(BlueprintReadOnly, Category = "iTwin|Loading")
		FString IModelId;

	UPROPERTY(BlueprintReadOnly, Category = "iTwin|Loading")
		FString ChangesetId;

	UPROPERTY(BlueprintReadOnly, Category = "iTwin|Loading")
		FString ExportId;

	UPROPERTY(BlueprintReadOnly, Category = "iTwin|Loading")
		FString IModelDisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "iTwin|Loading")
		FString ITwinDisplayName;
};
