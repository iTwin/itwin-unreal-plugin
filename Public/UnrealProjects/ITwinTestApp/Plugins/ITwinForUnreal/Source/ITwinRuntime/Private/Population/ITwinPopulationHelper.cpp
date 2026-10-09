/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPopulationHelper.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Population/ITwinPopulationHelper.h>
#include <Spline/ITwinSplineHelper.h>


struct UITwinPopulationHelper::FImpl
{
	UITwinPopulationHelper& Owner;

	AdvViz::SDK::IPopulationInfoPtr PopProp;
	TArray<FString> Objects; // same as 'objects' of PopProp but in FString format

	FImpl(UITwinPopulationHelper& InOwner)
		: Owner(InOwner)
	{
	}
};

void UITwinPopulationHelper::Init(AITwinSplineHelper* InSplineHelper, AdvViz::SDK::IPopulationInfoPtr InPopProp)
{
	SplineHelper = InSplineHelper;
	Impl = MakePimpl<FImpl>(*this);
	Impl->PopProp = InPopProp;
}

AdvViz::SDK::RefID UITwinPopulationHelper::GetPopRefID() const
{
	auto popProp = Impl->PopProp->GetAutoLock();
	return popProp->GetId();
}

AdvViz::SDK::RefID UITwinPopulationHelper::GetSplineRefID() const
{
	auto popProp = Impl->PopProp->GetAutoLock();
	return popProp->GetSplineId();
}

const TArray<FString>& UITwinPopulationHelper::Get3DObjects() const
{
	return Impl->Objects;
}

void UITwinPopulationHelper::Set3DObjects(const TArray<FString>& AssetPaths)
{
	Impl->Objects = AssetPaths;
	std::vector<std::string> paths;
	for (auto Asset : AssetPaths)
		paths.push_back(TCHAR_TO_UTF8(*Asset));
	auto popProp = Impl->PopProp->GetAutoLock();
	popProp->SetObjects(paths);
}

void UITwinPopulationHelper::Set3DObjectsFromProps()
{
	std::vector<std::string> paths;
	auto popProp = Impl->PopProp->GetAutoLock();
	popProp->GetObjects(paths);
	Impl->Objects.Empty();
	for (auto path : paths)
	{
		FString Asset(UTF8_TO_TCHAR(path.c_str()));
		Impl->Objects.Add(Asset);
	}
}

bool UITwinPopulationHelper::IsVisible() const
{
	auto popProp = Impl->PopProp->GetAutoLock();
	return popProp->IsVisible();
}

void UITwinPopulationHelper::SetVisible(bool bInIsVisible)
{
	auto popProp = Impl->PopProp->GetAutoLock();
	popProp->SetIsVisible(bInIsVisible);
}

float UITwinPopulationHelper::GetScale() const
{
	auto popProp = Impl->PopProp->GetAutoLock();
	return popProp->GetScale();
}

void UITwinPopulationHelper::SetScale(float InScale)
{
	auto popProp = Impl->PopProp->GetAutoLock();
	popProp->SetScale(InScale);
}

float UITwinPopulationHelper::GetRotation() const
{
	auto popProp = Impl->PopProp->GetAutoLock();
	return popProp->GetRotation();
}

void UITwinPopulationHelper::SetRotation(float InRotation)
{
	auto popProp = Impl->PopProp->GetAutoLock();
	popProp->SetRotation(InRotation);
}

float UITwinPopulationHelper::GetDistance() const
{
	auto popProp = Impl->PopProp->GetAutoLock();
	return popProp->GetDistance();
}

void UITwinPopulationHelper::SetDistance(float InDistance)
{
	auto popProp = Impl->PopProp->GetAutoLock();
	popProp->SetDistance(InDistance);
}

FFloatRange UITwinPopulationHelper::GetScaleRange() const
{
	auto popProp = Impl->PopProp->GetAutoLock();
	return FFloatRange(popProp->GetScaleRangeMin(),popProp->GetScaleRangeMax());
}

void UITwinPopulationHelper::SetScaleRange(FFloatRange InScaleRange)
{
	auto popProp = Impl->PopProp->GetAutoLock();
	popProp->SetScaleRange(InScaleRange.GetLowerBoundValue(),InScaleRange.GetUpperBoundValue());
}

FFloatRange UITwinPopulationHelper::GetRotationRange() const
{
	auto popProp = Impl->PopProp->GetAutoLock();
	return FFloatRange(popProp->GetRotationRangeMin(), popProp->GetRotationRangeMax());
}

void UITwinPopulationHelper::SetRotationRange(FFloatRange InRotationRange)
{
	auto popProp = Impl->PopProp->GetAutoLock();
	popProp->SetRotationRange(InRotationRange.GetLowerBoundValue(), InRotationRange.GetUpperBoundValue());
}

bool UITwinPopulationHelper::IsScaleRandomized() const
{
	auto popProp = Impl->PopProp->GetAutoLock();
	return popProp->IsScaleRandomized();
}

void UITwinPopulationHelper::SetIsScaleRandomized(bool InIsScaleRandomized)
{
	auto popProp = Impl->PopProp->GetAutoLock();
	popProp->SetIsScaleRandomized(InIsScaleRandomized);
}

bool UITwinPopulationHelper::IsRotationRandomized() const
{
	auto popProp = Impl->PopProp->GetAutoLock();
	return popProp->IsRotationRandomized();
}

void UITwinPopulationHelper::SetIsRotationRandomized(bool InIsRotationRandomized)
{
	auto popProp = Impl->PopProp->GetAutoLock();
	popProp->SetIsRotationRandomized(InIsRotationRandomized);
}

float UITwinAreaPopulationHelper::GetDensity() const
{
	auto popProp = Impl->PopProp->GetAutoLock();
	return popProp->GetDensity();
}

void UITwinAreaPopulationHelper::SetDensity(float InDensity)
{
	auto popProp = Impl->PopProp->GetAutoLock();
	popProp->SetDensity(InDensity);
}

float UITwinAreaPopulationHelper::GetGridRotation() const
{
	auto popProp = Impl->PopProp->GetAutoLock();
	return popProp->GetGridRotation();
}

void UITwinAreaPopulationHelper::SetGridRotation(float InGridRotation)
{
	auto popProp = Impl->PopProp->GetAutoLock();
	popProp->SetGridRotation(InGridRotation);
}

bool UITwinAreaPopulationHelper::IsAvoidOverlapping() const
{
	auto popProp = Impl->PopProp->GetAutoLock();
	return popProp->IsAvoidOverlapping();
}

void UITwinAreaPopulationHelper::SetIsAvoidOverlapping(bool InIsAvoidOverlapping)
{
	auto popProp = Impl->PopProp->GetAutoLock();
	popProp->SetAvoidOverlapping(InIsAvoidOverlapping);
}

EITwinSplinePopulationMode UITwinAreaPopulationHelper::GetMode() const
{
	auto popProp = Impl->PopProp->GetAutoLock();
	return static_cast<EITwinSplinePopulationMode>(popProp->GetMode());
}

void UITwinAreaPopulationHelper::SetMode(EITwinSplinePopulationMode InMode)
{
	auto popProp = Impl->PopProp->GetAutoLock();
	popProp->SetMode(static_cast<int>(InMode));
}

float UITwinPathPopulationHelper::GetDistanceMin() const
{
	auto popProp = Impl->PopProp->GetAutoLock();
	return popProp->GetDistanceRangeMin();
}

void UITwinPathPopulationHelper::SetDistanceMin(float InDistanceMin)
{
	auto popProp = Impl->PopProp->GetAutoLock();
	popProp->SetDistanceRange(InDistanceMin, popProp->GetDistanceRangeMax());
}

float UITwinPathPopulationHelper::GetDistanceMax() const
{
	auto popProp = Impl->PopProp->GetAutoLock();
	return popProp->GetDistanceRangeMax();
}

void UITwinPathPopulationHelper::SetDistanceMax(float InDistanceMax)
{
	auto popProp = Impl->PopProp->GetAutoLock();
	popProp->SetDistanceRange(popProp->GetDistanceRangeMin(), InDistanceMax);
}

FFloatRange UITwinPathPopulationHelper::GetDistanceRange() const
{
	auto popProp = Impl->PopProp->GetAutoLock();
	return FFloatRange(popProp->GetDistanceRangeMin(), popProp->GetDistanceRangeMax());
}

void UITwinPathPopulationHelper::SetDistanceRange(FFloatRange InDistanceRange)
{
	auto popProp = Impl->PopProp->GetAutoLock();
	popProp->SetDistanceRange(InDistanceRange.GetLowerBoundValue(), InDistanceRange.GetUpperBoundValue());
}

bool UITwinPathPopulationHelper::IsSpacingRandomized() const
{
	auto popProp = Impl->PopProp->GetAutoLock();
	return popProp->IsSpacingRandomized();
}

void UITwinPathPopulationHelper::SetIsSpacingRandomized(bool InIsSpacingRandomized)
{
	auto popProp = Impl->PopProp->GetAutoLock();
	popProp->SetIsSpacingRandomized(InIsSpacingRandomized);
}

EITwinPathPopulationRotationMode UITwinPathPopulationHelper::GetRotationMode() const
{
	auto popProp = Impl->PopProp->GetAutoLock();
	return static_cast<EITwinPathPopulationRotationMode>(popProp->GetRotationMode());
}

void UITwinPathPopulationHelper::SetRotationMode(EITwinPathPopulationRotationMode InRotationMode)
{
	auto popProp = Impl->PopProp->GetAutoLock();
	popProp->SetRotationMode(static_cast<int>(InRotationMode));
}