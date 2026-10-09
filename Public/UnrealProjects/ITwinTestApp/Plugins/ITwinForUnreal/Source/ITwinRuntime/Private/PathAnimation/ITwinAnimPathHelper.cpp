/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAnimPathHelper.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <PathAnimation/ITwinAnimPathHelper.h>
#include <Spline/ITwinSplineHelper.h>
#include <PathAnimation/BakedAnimKeyFrames.h>
#include <PathAnimation/ITwinAnimPathShaderParameters.h>

#include <Components/SplineComponent.h>
#include <Engine/World.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Compil/EnumSwitchCoverage.h>
#	include <Core/Tools/Log.h>
#	include <SDK/Core/Visualization/RefID.h>
//#   include <SDK/Core/Visualization/PathAnimation.h>
#include <Compil/AfterNonUnrealIncludes.h>


struct UITwinAnimPathHelper::FImpl
{
	UITwinAnimPathHelper& Owner;

	//FString PathName; // do we need to rename paths?
	AdvViz::SDK::IAnimationPathInfoPtr PathProp;
	TArray<TStrongObjectPtr<UBakedAnimKeyFrames> > BakedFramesPerLane;
	TArray<FString> Objects; // same as 'objects' of PathProp but in FString format
	// Spline length at the moment of last baking (used to detect if spline length
	// has changed and significantly after edition and therefore path repopulation is required)
	float BakedSplineLength = 0.f;
	bool IsPaused = false;

	FImpl(UITwinAnimPathHelper& InOwner)
		: Owner(InOwner)
	{}

	bool HasBakedAnimation() const;
	void InvalidateBakedAnimation();
	void BakeAnimationIfNeeded(AdvViz::SDK::ISplinePtr Spline);
	UBakedAnimKeyFrames* GetBakedFrames(int laneIdx);
	void UpdateBakedAnimationSpeed();

private:
	void UpdateLaneCountIfNeeded();

	float lastBakingRequestTime = -1.f;
};

bool UITwinAnimPathHelper::FImpl::HasBakedAnimation() const
{
	if (lastBakingRequestTime > 0)
		return false; // baking is requested and not yet processed
	if (BakedFramesPerLane.Num() == 0 || BakedFramesPerLane.Num() != Owner.GetFullLaneCount())
		return false;
	for (int i = 0; i < BakedFramesPerLane.Num(); ++i)
		if (!BakedFramesPerLane[i] || !BakedFramesPerLane[i]->IsReady())
			return false;
	return true;
}

bool UITwinAnimPathHelper::HasBakedAnimation() const
{
	return Impl->HasBakedAnimation();
}

void UITwinAnimPathHelper::FImpl::UpdateLaneCountIfNeeded()
{
	if (BakedFramesPerLane.Num() == Owner.GetFullLaneCount())
		return;

	BakedFramesPerLane.SetNum(Owner.GetFullLaneCount());
	for (int i = 0; i < BakedFramesPerLane.Num(); ++i)
	{
		if (!BakedFramesPerLane[i].IsValid())
			BakedFramesPerLane[i] = TStrongObjectPtr<UBakedAnimKeyFrames>(NewObject<UBakedAnimKeyFrames>(&Owner));
	}
}

void UITwinAnimPathHelper::FImpl::UpdateBakedAnimationSpeed()
{
	for (int i = 0; i < BakedFramesPerLane.Num(); ++i)
		if (BakedFramesPerLane[i].IsValid())
			BakedFramesPerLane[i]->SetSpeed(Owner.GetLaneSpeed(i));
}

void UITwinAnimPathHelper::FImpl::InvalidateBakedAnimation()
{
	lastBakingRequestTime = Owner.GetWorld()->GetRealTimeSeconds();

	UpdateLaneCountIfNeeded();

	for (int i = 0; i < BakedFramesPerLane.Num(); ++i)
		BakedFramesPerLane[i]->MarkForUpdate();
}

void UITwinAnimPathHelper::InvalidateBakedAnimation()
{
	Impl->InvalidateBakedAnimation();
}

void UITwinAnimPathHelper::FImpl::BakeAnimationIfNeeded(AdvViz::SDK::ISplinePtr Spline)
{
	auto splineInst = Spline->GetRAutoLock();
	bool bFirstBaking(BakedFramesPerLane.Num() == 0);

	UpdateLaneCountIfNeeded();

	// Avoid baking too often (e.g. when moving spline points)
	if (!bFirstBaking && (lastBakingRequestTime < 0 || Owner.GetWorld()->GetRealTimeSeconds() - lastBakingRequestTime < 5.f))
		return;

	for (int32 i = 0; i < BakedFramesPerLane.Num(); ++i)
		if (BakedFramesPerLane[i].IsValid())
			BakedFramesPerLane[i]->BakeSpline(Owner.GetWorld(), splineInst->GetId(), Owner.GetLaneSpeed(i), i, Owner.GetLaneOffset(i, false));

	BakedSplineLength = Owner.SplineHelper->GetSplineComponent()->GetSplineLength();
	lastBakingRequestTime = -1.f;
}

void UITwinAnimPathHelper::BakeAnimationIfNeeded()
{
	if (!ensure(SplineHelper.IsValid()) || SplineHelper->GetNumberOfSplinePoints() < 2)
		return;
	Impl->BakeAnimationIfNeeded(SplineHelper->GetAVizSpline());
}

UBakedAnimKeyFrames* UITwinAnimPathHelper::FImpl::GetBakedFrames(int laneIdx)
{
	if (BakedFramesPerLane.IsValidIndex(laneIdx))
		return BakedFramesPerLane[laneIdx].Get();
	return nullptr;
}

UBakedAnimKeyFrames* UITwinAnimPathHelper::GetBakedFrames(int laneIdx/* = 0*/)
{
	return Impl->GetBakedFrames(laneIdx);
}

FTransform UITwinAnimPathHelper::GetStartTransform(int laneIdx, bool bNeedAlignmentFix) const
{
	if (auto Frames = Impl->GetBakedFrames(laneIdx))
		if (Frames->IsReady())
			return Frames->GetTransform(0.f, bNeedAlignmentFix, IsInvDirLane(laneIdx));
	return FTransform(SplineHelper->GetSplineComponent()->GetLocationAtDistanceAlongSpline(0.0, ESplineCoordinateSpace::World));
}

float UITwinAnimPathHelper::GetLaneLength(int laneIdx) const
{
	if (auto Frames = Impl->GetBakedFrames(laneIdx))
		if (Frames->IsReady())
			return Frames->GetTotalLength();
	return GetSplineLength();
}

float UITwinAnimPathHelper::GetSplineLength() const
{
	return SplineHelper.IsValid() ? SplineHelper->GetSplineComponent()->GetSplineLength() : 0.f;
}

float UITwinAnimPathHelper::GetBakedSplineLength() const
{
	return Impl->BakedSplineLength;
}

void UITwinAnimPathHelper::Init(AITwinSplineHelper* InSplineHelper, AdvViz::SDK::IAnimationPathInfoPtr InPathProp)
{
	SplineHelper = InSplineHelper;

	Impl = MakePimpl<FImpl>(*this);
	Impl->PathProp = InPathProp;
	
	// TODO: apply the following right after spline tool activation
	UpdateSpline();

	if (SplineHelper.IsValid())
	{
		// Fill shader parameters for path animation (e.g. lane count, lane width, separator width, etc.)
		auto PathProp = Impl->PathProp->GetRAutoLock();

		// we don't want to apply those values to the mesh components yet: wewill send all values at once
		// afterwards, to avoid multiple updates of the mesh components.
		constexpr bool bApplyToMeshComponents = false;
		SplineHelper->SetPathAnimShaderScalarParameterValue(
			EITwinAnimPathShaderScalarParam::NumLanes,
			static_cast<float>(PathProp->GetLaneCount()),
			bApplyToMeshComponents);
		SplineHelper->SetPathAnimShaderScalarParameterValue(
			EITwinAnimPathShaderScalarParam::LaneWidth,
			PathProp->GetLaneWidth(),
			bApplyToMeshComponents);
		SplineHelper->SetPathAnimShaderScalarParameterValue(
			EITwinAnimPathShaderScalarParam::CentralSpacing,
			PathProp->GetSepWidth(),
			bApplyToMeshComponents);
		SplineHelper->SetPathAnimShaderScalarParameterValue(
			EITwinAnimPathShaderScalarParam::TwoWay,
			PathProp->IsOneWay() ? 0.f : 1.f,
			bApplyToMeshComponents);
		SplineHelper->SetPathAnimShaderScalarParameterValue(
			EITwinAnimPathShaderScalarParam::LeftHandDrive,
			PathProp->HasInvDir() ? 1.f : 0.f,
			bApplyToMeshComponents);
		// Then update meshes with all shader parameters at once.
		SplineHelper->TransferPathAnimShaderParametersToMeshes();
	}
}

AdvViz::SDK::RefID UITwinAnimPathHelper::GetPathRefID() const
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	return pathProp->GetId();
}

AdvViz::SDK::RefID UITwinAnimPathHelper::GetSplineRefID() const
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	return pathProp->GetSplineId();
}

//AdvViz::SDK::RefID UITwinAnimPathHelper::GetInstanceGroupRefID() const
//{
//	auto pathProp = Impl->PathProp->GetAutoLock();
//	return pathProp->GetInstGroupId();
//}

void UITwinAnimPathHelper::UpdateSpline()
{
	if (!SplineHelper.IsValid())
		return;
	SplineHelper->SetClosedLoop(IsLoop());
}

void UITwinCrowdAnimPathHelper::UpdateSpline()
{
	if (!SplineHelper.IsValid())
		return;

	SplineHelper->SetClosedLoop(IsLoop());
	SplineHelper->SetFixedSplineWidth(GetRoadWidth());
}

const TArray<FString>& UITwinAnimPathHelper::Get3DObjectPaths() const
{
	return Impl->Objects;
}

void UITwinAnimPathHelper::Get3DObjects(TArray<FString>& Assets) const
{
	Assets.Empty();
	std::vector<std::string> paths;
	auto pathProp = Impl->PathProp->GetRAutoLock();
	pathProp->GetObjects(paths);
	for (auto path : paths)
	{
		Assets.Add(UTF8_TO_TCHAR(path.c_str()));
	}
}

void UITwinAnimPathHelper::Set3DObjects(const TArray<FString>& Assets)
{
	Impl->Objects.Empty();
	std::vector<std::string> paths;
	for (auto Asset : Assets)
	{
		paths.push_back(TCHAR_TO_UTF8(*Asset));

		//TArray<FString> Parts;
		//Asset.ParseIntoArray(Parts, TEXT("###"), /*InCullEmpty=*/false);
		//if (Parts.Num() > 2)
		//	Impl->Objects.Add(Parts[2]);
		Impl->Objects.Add(Asset);
	}
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetObjects(paths);
}

void UITwinAnimPathHelper::Set3DObjectsFromProps()
{
	std::vector<std::string> paths;
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->GetObjects(paths);
	Impl->Objects.Empty();
	for (auto path : paths)
	{
		FString Asset(UTF8_TO_TCHAR(path.c_str()));

		//TArray<FString> Parts;
		//Asset.ParseIntoArray(Parts, TEXT("###"), /*InCullEmpty=*/false);
		//if (Parts.Num() > 2)
		//	Impl->Objects.Add(Parts[2]);
		Impl->Objects.Add(Asset);
	}
}

FString UITwinAnimPathHelper::GetRandomObjectPath() const
{
	static FRandomStream RandomStream(reinterpret_cast<uintptr_t>(SplineHelper.Get()));
	if (Impl->Objects.Num() == 0)
		return FString();
	if (!CanHaveMultipleObjects())
	{
		ensure(Impl->Objects.Num() == 1);
		return Impl->Objects[0];
	}
	return Impl->Objects[RandomStream.RandRange(0, Impl->Objects.Num() - 1)];
}

bool UITwinAnimPathHelper::IsPaused() const
{
	return Impl->IsPaused;
}

void UITwinAnimPathHelper::SetPaused(bool bInIsPaused)
{
	Impl->IsPaused = bInIsPaused;
}

bool UITwinAnimPathHelper::IsVisible() const
{
	auto pathProp = Impl->PathProp->GetRAutoLock();
	return pathProp->IsEnabled();
}

void UITwinAnimPathHelper::SetVisible(bool bInIsVisible)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetIsEnabled(bInIsVisible);
}

bool UITwinAnimPathHelper::HasInvDirection() const
{
	auto pathProp = Impl->PathProp->GetRAutoLock();
	return pathProp->HasInvDir();
}

void UITwinAnimPathHelper::SetInvDirection(bool bInInvDirection)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetInvDir(bInInvDirection);

	if (SplineHelper.IsValid())
	{
		SplineHelper->SetPathAnimShaderScalarParameterValue(
			EITwinAnimPathShaderScalarParam::LeftHandDrive, bInInvDirection ? 1.f : 0.f);
	}
}

bool UITwinAnimPathHelper::IsLoop() const
{
	auto pathProp = Impl->PathProp->GetRAutoLock();
	return pathProp->IsLooping();
}

void UITwinAnimPathHelper::SetIsLoop(bool bInIsLoop)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetIsLooping(bInIsLoop);
}

////////////////////////////////////////////////
// UITwinObjectAnimPathHelper

float UITwinObjectAnimPathHelper::GetSpeed() const
{
	auto pathProp = Impl->PathProp->GetRAutoLock();
	return pathProp->GetSpeed();
}

void UITwinObjectAnimPathHelper::SetSpeed(float InSpeed)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetSpeed(InSpeed);
	Impl->UpdateBakedAnimationSpeed();
}

float UITwinObjectAnimPathHelper::GetDelay() const
{
	auto pathProp = Impl->PathProp->GetRAutoLock();
	return pathProp->GetStartTime();
}

void UITwinObjectAnimPathHelper::SetDelay(float InDelay)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetStartTime(InDelay);
}

EITwinAnimPathRepeatMode UITwinObjectAnimPathHelper::GetRepeatMode() const
{
	auto pathProp = Impl->PathProp->GetRAutoLock();
	return static_cast<EITwinAnimPathRepeatMode>(pathProp->GetRepeatMode());
}

void UITwinObjectAnimPathHelper::SetRepeatMode(EITwinAnimPathRepeatMode InRepeatMode)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetRepeatMode(static_cast<int>(InRepeatMode));
}

////////////////////////////////////////////////
// UITwinCrowdAnimPathHelper

float UITwinCrowdAnimPathHelper::GetSpeed() const
{
	// TODO: manage speed per lane, like in traffic?
	return 300.f;
}

bool UITwinCrowdAnimPathHelper::IsOneWay() const
{
	auto pathProp = Impl->PathProp->GetRAutoLock();
	return pathProp->IsOneWay();
}

void UITwinCrowdAnimPathHelper::SetOneWay(bool bInOneWay)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetOneWay(bInOneWay);

	if (SplineHelper.IsValid())
	{
		SplineHelper->SetPathAnimShaderScalarParameterValue(
			EITwinAnimPathShaderScalarParam::TwoWay, bInOneWay ? 0.f : 1.f);
	}
}

int UITwinCrowdAnimPathHelper::GetLaneCount() const
{
	auto pathProp = Impl->PathProp->GetRAutoLock();
	return pathProp->GetLaneCount();
}

void UITwinCrowdAnimPathHelper::SetLaneCount(int InLaneCount)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetLaneCount(InLaneCount);

	if (SplineHelper.IsValid())
	{
		SplineHelper->SetPathAnimShaderScalarParameterValue(
			EITwinAnimPathShaderScalarParam::NumLanes, static_cast<float>(InLaneCount));
	}
}

float UITwinCrowdAnimPathHelper::GetLaneWidth() const
{
	auto pathProp = Impl->PathProp->GetRAutoLock();
	return pathProp->GetLaneWidth();
}

void UITwinCrowdAnimPathHelper::SetLaneWidth(float InLaneWidth)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetLaneWidth(InLaneWidth);

	if (SplineHelper.IsValid())
	{
		SplineHelper->SetPathAnimShaderScalarParameterValue(
			EITwinAnimPathShaderScalarParam::LaneWidth, InLaneWidth);
	}
}

float UITwinCrowdAnimPathHelper::GetDensity() const
{
	auto pathProp = Impl->PathProp->GetRAutoLock();
	return pathProp->GetDensity();
}

void UITwinCrowdAnimPathHelper::SetDensity(float InDensity)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetDensity(InDensity);

	// TODO: update population when density changes
}

////////////////////////////////////////////////
// UITwinTrafficAnimPathHelper

float UITwinTrafficAnimPathHelper::GetSeparatorWidth() const
{
	auto pathProp = Impl->PathProp->GetRAutoLock();
	return pathProp->GetSepWidth();
}

void UITwinTrafficAnimPathHelper::SetSeparatorWidth(float InSeparatorWidth)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetSepWidth(InSeparatorWidth);

	if (SplineHelper.IsValid())
	{
		SplineHelper->SetPathAnimShaderScalarParameterValue(
			EITwinAnimPathShaderScalarParam::CentralSpacing, InSeparatorWidth);
	}
}

float UITwinTrafficAnimPathHelper::GetSpeed() const
{
	return GetMaxSpeed();
}

void UITwinTrafficAnimPathHelper::SetSpeed(float InSpeed)
{
	SetMinSpeed(InSpeed);
	SetMaxSpeed(InSpeed);
}

float UITwinTrafficAnimPathHelper::GetMinSpeed() const
{
	auto pathProp = Impl->PathProp->GetRAutoLock();
	return pathProp->GetMinSpeed();
}

void UITwinTrafficAnimPathHelper::SetMinSpeed(float InMinSpeed)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetMinSpeed(InMinSpeed);
	Impl->UpdateBakedAnimationSpeed();
}

float UITwinTrafficAnimPathHelper::GetMaxSpeed() const
{
	auto pathProp = Impl->PathProp->GetRAutoLock();
	return pathProp->GetMaxSpeed();
}

void UITwinTrafficAnimPathHelper::SetMaxSpeed(float InMaxSpeed)
{
	auto pathProp = Impl->PathProp->GetAutoLock();
	pathProp->SetMaxSpeed(InMaxSpeed);
	Impl->UpdateBakedAnimationSpeed();
}

bool UITwinCrowdAnimPathHelper::IsSlowLane(int laneIdx) const
{
	int nbSlowLanes = std::max(IsOneWay() ? GetFullLaneCount() / 2 : GetFullLaneCount() / 4, 1);
	if (IsOneWay())
		return HasInvDirection() ? (laneIdx < nbSlowLanes) : (laneIdx >= GetFullLaneCount() - nbSlowLanes);
	else
		return laneIdx < nbSlowLanes || laneIdx >= GetFullLaneCount() - nbSlowLanes;
}

bool UITwinCrowdAnimPathHelper::IsInvDirLane(int laneIdx) const
{
	//if (hasAlternatingLanes && !IsOneWay())
	//	return laneIdx % 2 == 1; // TODO: for pedestrian traffic
	//else
		return !IsOneWay() && ((HasInvDirection() && (laneIdx >= GetFirstRightLaneIndex())) || (!HasInvDirection() && (laneIdx < GetFirstRightLaneIndex())));
}

float UITwinTrafficAnimPathHelper::GetLaneSpeed(int laneIdx) const
{
	// distribute the speed range among faster and slower lanes
	float speed(GetMinSpeed());
	float speedDelta(GetMaxSpeed() - GetMinSpeed());
	if (speedDelta > 0.f)
	{
		if (IsOneWay())
		{
			if (GetLaneCount() > 1)
			{
				int laneID = HasInvDirection() ? laneIdx : GetLaneCount() - 1 - laneIdx; // laneID=0 - slowest lane, laneID=(nbLanes-l) - fastest lane
				speed += speedDelta * laneID / (GetLaneCount() - 1);
			}
		}
		else
		{
			if (laneIdx < GetFirstRightLaneIndex() && GetFirstRightLaneIndex() > 1)
				speed += speedDelta * laneIdx / (GetFirstRightLaneIndex() - 1);
			else if (laneIdx >= GetFirstRightLaneIndex() && GetFirstRightLaneIndex() < GetFullLaneCount() - 1)
				speed += speedDelta * (GetFullLaneCount() - 1 - laneIdx) / (GetFullLaneCount() - GetFirstRightLaneIndex() - 1);
		}
	}

	return speed;
}

float UITwinTrafficAnimPathHelper::GetLaneDensity(int laneIdx) const
{
	float laneDensity(GetDensity());

	// Slightly increase density on slower lanes and decrease on faster ones if traffic is not intense.
	// Lanes are numbered from left to write (laneIdx=0 corresponds to the leftmost lane).
	if (laneDensity < 0.8f)
	{
		float coef(0.5f);
		if (IsOneWay())
		{
			if (GetLaneCount() > 1)
			{
				coef = HasInvDirection() ? 1.f - laneIdx / (float)(GetLaneCount() - 1)
					: laneIdx / (float)(GetLaneCount() - 1);
			}
		}
		else
		{
			if (laneIdx < GetFirstRightLaneIndex() && GetFirstRightLaneIndex() > 1)
				coef = 1.f - laneIdx / (float)(GetFirstRightLaneIndex() - 1);
			else if (laneIdx >= GetFirstRightLaneIndex() && GetFirstRightLaneIndex() < GetFullLaneCount() - 1)
				coef = 1.f - (GetFullLaneCount() - 1 - laneIdx) / (float)(GetFullLaneCount() - 1 - GetFirstRightLaneIndex());
		}
		laneDensity += 0.2f * coef - 0.1f;
		if (GetDensity() <= 0.1f)
			laneDensity = std::max(laneDensity, GetDensity());
	}

	return laneDensity;
}

float UITwinCrowdAnimPathHelper::GetLaneOffset(int laneIdx, bool bAddRandomVariation) const
{
	float posOffset = IsOneWay() ? (laneIdx + 0.5f - 0.5f * GetLaneCount()) * GetLaneWidth()
		: (laneIdx + 0.5f - GetFirstRightLaneIndex()) * GetLaneWidth();
	// Take into account separator width (if any)
	if (!IsOneWay() && GetSeparatorWidth() > 0.f)
		posOffset += (laneIdx < GetFirstRightLaneIndex()) ? -0.5f * GetSeparatorWidth() : 0.5f * GetSeparatorWidth();

	if (bAddRandomVariation)
	{
		// Slightly shift each vehicle from the main axis to avoid the vehicles being strictly aligned
		// (but leave at least 1.5m distance between the cars)
		float vehicleWidth = 200; // TODO: taking 2m here but can also use real object width
		float widthDiff(GetLaneWidth() - vehicleWidth - 1.5f);
		if (widthDiff > KINDA_SMALL_NUMBER)
			posOffset += (FMath::FRand() - 0.5f) * widthDiff;
	}

	return posOffset;
}

float UITwinTrafficAnimPathHelper::GetMinInterObjectDistance(int laneIdx, bool bDrive/* = false*/) const
{
	// Minimum allowed distance between objects depends on object speed;
	// that means that 100% lane density at low speed will result in more instances
	// that at high speed.
	float speed = std::clamp(0.036f * GetLaneSpeed(laneIdx), 1.f, 130.f); // convert speed from cm/s to km/h and clamp to [1,130]
	float lowSpeedDistance(50.f); // at least 0.5m when speed is 1km/h or less
	float highSpeedDistance(bDrive ? 300.f : 500.f); // at least 5m when speed is 130km/h or more (3m if driving a vehicle)
	return (highSpeedDistance * (speed - 1.f) + lowSpeedDistance * (130.f - speed)) / 129.f;
}

