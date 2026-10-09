/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinFeatureChange.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <UObject/ObjectMacros.h>
#include <Containers/Array.h>
#include <Containers/UnrealString.h>
#include <HAL/Platform.h>

#include "ITwinFeatureChange.generated.h"

UENUM(BlueprintType)
enum class EChangeType : uint8 {
	Added = 0,
	Deleted = 1,
	Modified = 2
};

UENUM(BlueprintType)
enum class EFeatureType : uint8 {
	None = 255,
	Clipping = 0,
	PopulationObject = 1,
	PopulationSpline = 2,
	PathAnimation = 3,
};

USTRUCT(BlueprintType)
struct ITWINRUNTIME_API FFeatureEventProperty
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "ITwin")
	FString Key;

	UPROPERTY(BlueprintReadWrite, Category = "ITwin")
	FString Value;

	FFeatureEventProperty() = default;

	FFeatureEventProperty(const FString& InKey, const FString& InValue)
		: Key(InKey), Value(InValue)
	{}
};

USTRUCT(BlueprintType)
struct ITWINRUNTIME_API FFeatureEventProperties{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "ITwin")
	EChangeType ChangeType = EChangeType::Added;
	UPROPERTY(BlueprintReadWrite, Category = "ITwin")
	EFeatureType FeatureType = EFeatureType::None;
	UPROPERTY(BlueprintReadWrite, Category = "ITwin")
	TArray<FFeatureEventProperty> Properties;

	void AddProperty(const FString& key, const FString& value);
};
