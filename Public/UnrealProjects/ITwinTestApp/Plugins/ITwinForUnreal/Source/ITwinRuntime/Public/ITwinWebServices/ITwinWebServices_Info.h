/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinWebServices_Info.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
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

	UPROPERTY(BlueprintReadWrite, Category = "SavedViewInfo")
		FString CreationTime;

	UPROPERTY(BlueprintReadWrite, Category = "SavedViewInfo")
		TArray<FString> Extensions;
};

USTRUCT(BlueprintType)
struct FSavedViewInfos
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SavedView")
		TArray<FSavedViewInfo> SavedViews;

	UPROPERTY(BlueprintReadOnly, Category = "SavedView")
		FString ITwinId;

	UPROPERTY(BlueprintReadOnly, Category = "SavedView")
		FString IModelId;

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
struct FPerModelCategoryVisibilityProps
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "SavedView")
		FString ModelId;

	UPROPERTY(BlueprintReadWrite, Category = "SavedView")
		FString CategoryId;

	bool operator==(const FPerModelCategoryVisibilityProps& Other) const
	{
		return ModelId == Other.ModelId && CategoryId == Other.CategoryId;
	}
};

FORCEINLINE uint32 GetTypeHash(const FPerModelCategoryVisibilityProps& Props)
{
	return HashCombine(GetTypeHash(Props.ModelId), GetTypeHash(Props.CategoryId));
}

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

	UPROPERTY(BlueprintReadOnly, Category = "SavedView")
		TSet<FString> HiddenCategories;

	UPROPERTY(BlueprintReadOnly, Category = "SavedView")
		TSet<FString> HiddenModels;

	UPROPERTY(BlueprintReadOnly, Category = "SavedView")
		TSet<FString> HiddenElements;

	UPROPERTY(BlueprintReadOnly, Category = "SavedView")
		TSet<FPerModelCategoryVisibilityProps> HiddenCategoriesPerModel;

	UPROPERTY(BlueprintReadOnly, Category = "SavedView")
		TSet<FPerModelCategoryVisibilityProps> AlwaysDrawnCategoriesPerModel;

	UPROPERTY(BlueprintReadOnly, Category = "SavedView")
		TSet<FString> AlwaysDrawnElements;

	UPROPERTY(BlueprintReadWrite, Category = "SavedView")
		FDisplayStyle DisplayStyle;

	UPROPERTY(BlueprintReadWrite, Category = "SavedView")
		FVector FrustumOrigin = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadWrite, Category = "SavedView")
		double FocusDist = 0.0;
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

	UPROPERTY(BlueprintReadOnly, Category = "SavedViewGroup")
		FString IModelId;
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
struct FExtendedData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		FString icon;
	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		bool isSubject = false;
	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		bool isCategory = false;
	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		FString modelId;
	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		FString categoryId;
};
USTRUCT(BlueprintType)
struct FInstanceKey
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		FString className;
	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		FString id;
};
USTRUCT(BlueprintType)
struct FBinding
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		FString type;
	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		FString value;
};
USTRUCT(BlueprintType)
struct FInstanceKeySelectQuery
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		TArray<FBinding> bindings;
	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		FString query;
};
USTRUCT(BlueprintType)
struct FNodeKey
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		TArray<FInstanceKey> instanceKeys;
	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		FInstanceKeySelectQuery instanceKeysSelectQuery;
	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		TArray<FString> pathFromRoot;
	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		FString type;
	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		int version = -1;
};
USTRUCT(BlueprintType)
struct FLabelDefinition
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		FString displayValue;
	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		FString rawValue;
	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		FString typeName;
};
USTRUCT(BlueprintType)
struct FIModelNodeItem
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		FExtendedData extendedData;
	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		bool hasChildren = false;
	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		FString description;
	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		FNodeKey key;
	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		FLabelDefinition labelDefinition;
	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		bool supportsFiltering = false;
};
USTRUCT(BlueprintType)
struct FResult
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		TArray<FIModelNodeItem> items;

	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		int total = 0;
};
USTRUCT(BlueprintType)
struct FIModelPagedNodesRes
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		FResult result;
};

USTRUCT(BlueprintType)
struct FFilteredResItem
{
	GENERATED_USTRUCT_BODY()

	//Can't use uproperty here because it doesn't support Struct recursion via arrays
		TArray<FFilteredResItem> children;

	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		FIModelNodeItem node;
};

USTRUCT(BlueprintType)
struct FFilteredNodesRes
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IModelTree")
		TArray<FFilteredResItem> result;
};

USTRUCT(BlueprintType)
struct FProjectExtents
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "IModelProps")
		FVector High = FVector(0, 0, 0);

	UPROPERTY(BlueprintReadOnly, Category = "IModelProps")
		FVector Low = FVector(0, 0, 0);

	//! Global offset of the iModel with respect to its spatial coordinates (do not confuse with
	//! FEcefLocation::Origin). Placed here because FEcefLocation is optional and its presence means
	//! the iModel is geolocated.
	UPROPERTY(BlueprintReadOnly, Category = "IModelProps")
		FVector GlobalOrigin = FVector(0, 0, 0);
};

/// Note that, in contradition with iTwin.js common policy enforcing SI Base Units and SI Derived Units
/// (https://www.itwinjs.org/bis/guide/other-topics/units/#angle-units - Note the exception for
/// YawPitchRoll! ;^_^), longitude and latitude are actually stored in degrees in this structure.
/// Despite corporate policy, my arguments for storing degrees are: 1/ Reality data still use degrees for
/// their extents, same of course for Cesium in their API, 2/ I've never heard of geographical coordinates
/// being expressed in radians and 3/ if we are to expose values to external users, it seems better to
/// comply to such a reasonable common expectation.
/// Height is expressed in meters above the WGS84 ellipsoid (ie. _not_ Mean Sea Level), as specified in
/// https://www.itwinjs.org/reference/core-common/geometry/cartographicprops/.
USTRUCT(BlueprintType)
struct FCartographicProps
{
	GENERATED_USTRUCT_BODY()

	/// Height in meters above the WGS84 ellipsoid (ie. _not_ Mean Sea Level)
	UPROPERTY(BlueprintReadOnly, Category = "IModelProps")
		double Height = 0.0;
	/// Signed northward latitude, in degrees
	UPROPERTY(BlueprintReadOnly, Category = "IModelProps")
		double Latitude = 0.0;
	/// Signed eastward longitude, in degrees
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
	//! ECEF Origin of the iModel spatial coordinates (do not confuse with FProjectExtents::GlobalOrigin)
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
	UPROPERTY(BlueprintReadOnly, Category = "IModelProps")
		bool bHasGeographicCoordinateSystem = false;
	UPROPERTY(BlueprintReadOnly, Category = "IModelProps")
		int GeographicCoordinateSystemEPSG = -1;
	UPROPERTY(BlueprintReadOnly, Category = "IModelProps")
		bool bHasProjectExtentsCenterGeoCoords = false;
	UPROPERTY(BlueprintReadOnly, Category = "IModelProps")
		FCartographicProps ProjectExtentsCenterGeoCoords;
};


