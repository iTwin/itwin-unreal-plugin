/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinWebServices_Info.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ITwinWebServices_Info.generated.h"


USTRUCT(BlueprintType)
struct FITwinGeolocationInfo
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly)
		double Latitude = 0.0;

	UPROPERTY(BlueprintReadOnly)
		double Longitude = 0.0;
};

USTRUCT(BlueprintType)
struct FITwinExportInfo
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "ExportInfo")
		FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "ExportInfo")
		FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "ExportInfo")
		FString Status;

	UPROPERTY(BlueprintReadOnly, Category = "ExportInfo")
		FString iModelId;

	UPROPERTY(BlueprintReadOnly, Category = "ExportInfo")
		FString iTwinId;

	UPROPERTY(BlueprintReadOnly, Category = "ExportInfo")
		FString ChangesetId;

	UPROPERTY(BlueprintReadOnly, Category = "ExportInfo")
		FString MeshUrl;
};

USTRUCT(BlueprintType)
struct FITwinExportInfos
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "ExportInfo")
		TArray<FITwinExportInfo> ExportInfos;
};

USTRUCT(BlueprintType)
struct FITwinInfo
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "iTwinInfo")
		FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "iTwinInfo")
		FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "iTwinInfo")
		FString Status;

	UPROPERTY(BlueprintReadOnly, Category = "iTwinInfo")
		FString Number;
};

USTRUCT(BlueprintType)
struct FITwinInfos
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "iTwinInfo")
		TArray<FITwinInfo> iTwins;
};

USTRUCT(BlueprintType)
struct FIModelInfo
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "iModelInfo")
		FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "iModelInfo")
		FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "iModelInfo")
		FString Status;

	UPROPERTY(BlueprintReadOnly, Category = "iModelInfo")
		FString Number;
};

USTRUCT(BlueprintType)
struct FIModelInfos
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "iModelInfo")
		TArray<FIModelInfo> iModels;
};

USTRUCT(BlueprintType)
struct FChangesetInfo
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "ChangesetInfo")
		FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "ChangesetInfo")
		FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "ChangesetInfo")
		FString Description;

	UPROPERTY(BlueprintReadOnly, Category = "ChangesetInfo")
		int Index = 0;
};

USTRUCT(BlueprintType)
struct FChangesetInfos
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "ChangesetInfo")
		TArray<FChangesetInfo> Changesets;
};

USTRUCT(BlueprintType)
struct FSavedViewInfo
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SavedViewInfo")
		FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "SavedViewInfo")
		FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "SavedViewInfo")
		bool bShared = false;
};

USTRUCT(BlueprintType)
struct FSavedViewInfos
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SavedView")
		TArray<FSavedViewInfo> SavedViews;
};

USTRUCT(BlueprintType)
struct FSavedView
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SavedView")
		FVector Origin = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadOnly, Category = "SavedView")
		FVector Extents = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadOnly, Category = "SavedView")
		FRotator Angles = FRotator::ZeroRotator;
};

USTRUCT(BlueprintType)
struct FITwinRealityDataInfo
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RealityData")
		FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "RealityData")
		FString DisplayName;
};


USTRUCT(BlueprintType)
struct FITwinRealityDataInfos
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RealityData")
		TArray<FITwinRealityDataInfo> Infos;
};


USTRUCT(BlueprintType)
struct FITwinRealityData3DInfo : public FITwinRealityDataInfo
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RealityData")
		bool bGeolocated = false;

	UPROPERTY(BlueprintReadOnly, Category = "RealityData")
		FITwinGeolocationInfo ExtentSouthWest = {};

	UPROPERTY(BlueprintReadOnly, Category = "RealityData")
		FITwinGeolocationInfo ExtentNorthEast = {};

	UPROPERTY(BlueprintReadOnly, Category = "RealityData")
		FString MeshUrl;
};
