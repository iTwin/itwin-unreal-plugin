/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPopulationHelper.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <Containers/Array.h>
#include <GameFramework/Actor.h>
#include <Misc/EnumRange.h>
#include <Math/Range.h>
#include <Templates/PimplPtr.h>
#include <Spline/ITwinSplineHelper.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#   include <SDK/Core/Visualization/PopulationPersistence.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <memory>
#include <optional>

#include "ITwinPopulationHelper.generated.h"


class AITwinPopulation;
class AITwinSplineHelper;

namespace AdvViz::SDK
{
	class RefID;
}

// Population type, should be in the same order same as in
// ITwinStudioApp\carrot\frontend\components\Toolbar\tools\PopulationTool.tsx
UENUM(BlueprintType)
enum class EITwinPopulationType : uint8
{
	Object,
	Brush,
	Erase,
	Area,
	Path,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EITwinPopulationType, EITwinPopulationType::Count);

UENUM(BlueprintType)
enum class EITwinSplinePopulationMode : uint8
{
	Regular,
	Random,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EITwinSplinePopulationMode, EITwinSplinePopulationMode::Count);

UENUM(BlueprintType)
enum class EITwinPathPopulationRotationMode : uint8
{
	Absolute,
	Relative,

	Count UMETA(Hidden)
};
ENUM_RANGE_BY_COUNT(EITwinPathPopulationRotationMode, EITwinPathPopulationRotationMode::Count);

UCLASS()
class UITwinPopulationHelper : public UObject
{
	GENERATED_BODY()

public:
	virtual ~UITwinPopulationHelper() = default;

	TWeakObjectPtr<AITwinSplineHelper> SplineHelper;
	TSet<TWeakObjectPtr<AITwinPopulation>> Populations;

protected:
	struct FImpl;
	TPimplPtr<FImpl> Impl;

public:
	void Init(AITwinSplineHelper* InSplineHelper, AdvViz::SDK::IPopulationInfoPtr InPopProp);

	AdvViz::SDK::RefID GetPopRefID() const;
	AdvViz::SDK::RefID GetSplineRefID() const;

	const TArray<FString>& Get3DObjects() const;
	void Set3DObjects(const TArray<FString>& AssetPaths);
	void Set3DObjectsFromProps();

	bool IsVisible() const;
	void SetVisible(bool bInIsVisible);

	float GetScale() const;
	void SetScale(float InScale);

	float GetRotation() const;
	void SetRotation(float InRotation);

	float GetDistance() const;
	void SetDistance(float InDistance);

	FFloatRange GetScaleRange() const;
	void SetScaleRange(FFloatRange InScaleRange);

	FFloatRange GetRotationRange() const;
	void SetRotationRange(FFloatRange InRotationRange);

	bool IsScaleRandomized() const;
	void SetIsScaleRandomized(bool InIsScaleRandomized);

	bool IsRotationRandomized() const;
	void SetIsRotationRandomized(bool InIsRotationRandomized);

	virtual float GetGridRotation() const { return 0.f; }
	virtual void SetGridRotation(float /*InGridRotation*/) {}

	virtual float GetDensity() const { return 0.5f; }
	virtual void SetDensity(float /*InDensity*/) {}

	virtual bool IsAvoidOverlapping() const { return false; }
	virtual void SetIsAvoidOverlapping(bool /*InIsAvoidOverlapping*/) {}

	virtual bool IsSpacingRandomized() const { return false; }
	virtual void SetIsSpacingRandomized(bool /*InIsSpacingRandomized*/) {}

	virtual FFloatRange GetDistanceRange() const { return FFloatRange(2.5f, 3.5f); }
	virtual void SetDistanceRange(FFloatRange /*InDistanceRange*/) {}

	virtual EITwinSplinePopulationMode GetMode() const { return EITwinSplinePopulationMode::Random; }
	virtual void SetMode(EITwinSplinePopulationMode /*InMode*/) {}

	virtual EITwinPathPopulationRotationMode GetRotationMode() const { return EITwinPathPopulationRotationMode::Absolute; }
	virtual void SetRotationMode(EITwinPathPopulationRotationMode /*InRotationMode*/) {}
};

UCLASS()
class UITwinAreaPopulationHelper : public UITwinPopulationHelper
{
	GENERATED_BODY()
public:

	float GetDensity() const;
	void SetDensity(float InDensity);

	float GetGridRotation() const;
	void SetGridRotation(float InGridRotation);

	bool IsAvoidOverlapping() const;
	void SetIsAvoidOverlapping(bool InIsAvoidOverlapping);

	EITwinSplinePopulationMode GetMode() const override;
	void SetMode(EITwinSplinePopulationMode InMode) override;
};

UCLASS()
class UITwinPathPopulationHelper : public UITwinPopulationHelper
{
	GENERATED_BODY()
public:

	float GetDistanceMin() const;
	void SetDistanceMin(float InDistanceMin);

	float GetDistanceMax() const;
	void SetDistanceMax(float InDistanceMax);

	FFloatRange GetDistanceRange() const;
	void SetDistanceRange(FFloatRange InDistanceRange);

	bool IsSpacingRandomized() const;
	void SetIsSpacingRandomized(bool InIsSpacingRandomized);

	EITwinPathPopulationRotationMode GetRotationMode() const;
	void SetRotationMode(EITwinPathPopulationRotationMode InRotationMode);
};
