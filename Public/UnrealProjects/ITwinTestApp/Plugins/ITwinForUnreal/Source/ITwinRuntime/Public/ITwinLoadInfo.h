/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinLoadInfo.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"

#include "ITwinLoadInfo.generated.h"

//! A "model" designates any dataset that can be loaded by the plugin.
UENUM(BlueprintType)
enum class EITwinModelType : uint8
{
	IModel,
	RealityData,
};

/// Gathers all identifiers and information identifying a unique loading.
USTRUCT(BlueprintType)
struct FITwinLoadInfo
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "iTwin|Loading")
		EITwinModelType ModelType = EITwinModelType::IModel;

	UPROPERTY(BlueprintReadOnly, Category = "iTwin|Loading")
		FString ITwinId;
	//! Valid only for iModel type.
	UPROPERTY(BlueprintReadOnly, Category = "iTwin|Loading")
		FString IModelId;
	//! Valid only for iModel type.
	UPROPERTY(BlueprintReadOnly, Category = "iTwin|Loading")
		FString ChangesetId;
	//! Valid only for iModel type.
	UPROPERTY(BlueprintReadOnly, Category = "iTwin|Loading")
		FString ExportId;
	//! Valid only for iModel type.
	UPROPERTY(BlueprintReadOnly, Category = "iTwin|Loading")
		FString IModelDisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "iTwin|Loading")
		FString ITwinDisplayName;
	//! Valid only for Reality Data type.
	UPROPERTY(BlueprintReadOnly, Category = "iTwin|Loading")
		FString RealityDataId;
};
