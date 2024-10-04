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

	UPROPERTY(BlueprintReadOnly, Category = "iTwin")
		double Latitude = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "iTwin")
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

	UPROPERTY(BlueprintReadWrite, Category = "SavedViewInfo")
		FString Id;

	UPROPERTY(BlueprintReadWrite, Category = "SavedViewInfo")
		FString DisplayName;

	UPROPERTY(BlueprintReadWrite, Category = "SavedViewInfo")
		bool bShared = false;
};

USTRUCT(BlueprintType)
struct FSavedViewInfos
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SavedView")
		TArray<FSavedViewInfo> SavedViews;

	UPROPERTY(BlueprintReadOnly, Category = "SavedView")
		FString GroupId;
};

USTRUCT(BlueprintType)
struct FDisplayStyle
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "SavedView")
		FString RenderTimeline;

	UPROPERTY(BlueprintReadWrite, Category = "SavedView")
		double TimePoint = 0.0;
};

USTRUCT(BlueprintType)
struct FSavedView
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "SavedView")
		FVector Origin = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "SavedView")
		FVector Extents = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "SavedView")
		FRotator Angles = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadWrite, Category = "SavedView")
		FDisplayStyle DisplayStyle;
};

USTRUCT(BlueprintType)
struct FSavedViewGroupInfo
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "SavedViewGroup")
		FString Id;

	UPROPERTY(BlueprintReadWrite, Category = "SavedViewGroup")
		FString DisplayName;

	UPROPERTY(BlueprintReadWrite, Category = "SavedViewGroup")
		bool bShared = false;

	UPROPERTY(BlueprintReadWrite, Category = "SavedViewGroupInfo")
		bool bReadOnly = false;
};

USTRUCT(BlueprintType)
struct FSavedViewGroupInfos
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SavedViewGroup")
		TArray<FSavedViewGroupInfo> SavedViewGroups;
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

USTRUCT(BlueprintType)
struct FElementAttribute
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "BIMInfo")
		FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "BIMInfo")
		FString Value;
};

USTRUCT(BlueprintType)
struct FElementProperty
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "BIMInfo")
		FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "BIMInfo")
		TArray<FElementAttribute> Attributes;
};

USTRUCT(BlueprintType)
struct FElementProperties
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "BIMInfo")
		TArray<FElementProperty> Properties;
};

USTRUCT(BlueprintType)
struct FProjectExtents
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IModelProps")
		FVector High = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadOnly, Category = "IModelProps")
		FVector Low = FVector(0, 0, 0);
};

USTRUCT(BlueprintType)
struct FCartographicProps
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IModelProps")
		double Height = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "IModelProps")
		double Latitude = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "IModelProps")
		double Longitude = 0.0;
};

USTRUCT(BlueprintType)
struct FEcefLocation
{
	GENERATED_USTRUCT_BODY()
	
	//! Indicates whether CartographicOrigin member contains valid data.
	UPROPERTY(BlueprintReadOnly, Category = "IModelProps")
		bool bHasCartographicOrigin = false;
	UPROPERTY(BlueprintReadOnly, Category = "IModelProps")
		FCartographicProps CartographicOrigin;
	UPROPERTY(BlueprintReadOnly, Category = "IModelProps")
		FRotator Orientation = FRotator::ZeroRotator;
	UPROPERTY(BlueprintReadOnly, Category = "IModelProps")
		FVector Origin = FVector(0, 0, 0);
	//! Indicates whether Transform member contains valid data.
	UPROPERTY(BlueprintReadOnly, Category = "IModelProps")
		bool bHasTransform = false;
	UPROPERTY(BlueprintReadOnly, Category = "IModelProps")
		FMatrix Transform = FMatrix::Identity;
	//! Indicates whether xVector & yVector members contain valid data.
	UPROPERTY(BlueprintReadOnly, Category = "IModelProps")
		bool bHasVectors = false;
	UPROPERTY(BlueprintReadOnly, Category = "IModelProps")
		FVector xVector = FVector(0, 0, 0);
	UPROPERTY(BlueprintReadOnly, Category = "IModelProps")
		FVector yVector = FVector(0, 0, 0);
};


