/*--------------------------------------------------------------------------------------+
|
|     $Source: BakedAnimKeyFrames.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <PathAnimation/BakedAnimKeyFrames.h>
#include <Spline/ITwinSplineHelper.h>
#include <Components/SplineComponent.h>
#include <Math/UEMathConversion.h>
#include <EngineUtils.h> // for TActorIterator<>
#include <ITwinIModel.h>
#include <ITwinRealityData.h>
#include <ProfilingDebugging/CpuProfilerTrace.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Compil/EnumSwitchCoverage.h>
#	include <Core/Tools/Log.h>
#	include <SDK/Core/Visualization/RefID.h>
#include <Compil/AfterNonUnrealIncludes.h>



namespace
{
bool FindHeight(UWorld* World, const FVector& InPos, float& OutHeight, FVector& OutNormal, float maxHeight = 1000)
{
	// Raycast from above to below the point
	FVector Start = InPos + FVector(0, 0, maxHeight); // start high above
	FVector End = InPos - FVector(0, 0, 10000); // cast far below

	FHitResult HitResult;
	FCollisionQueryParams Params(NAME_None, false, nullptr);
	Params.bReturnPhysicalMaterial = false;

	bool bHit = World->LineTraceSingleByChannel(
		HitResult,
		Start,
		End,
		ECC_Visibility, // or create a custom channel if needed
		Params
	);

#if 0//WITH_EDITOR
	// Optional: visualize the trace in editor
	DrawDebugLine(World, Start, End, FColor::Green, false, 2.0f, 0, 1.0f);
	if (bHit)
	{
		DrawDebugPoint(World, HitResult.ImpactPoint, 12.0f, FColor::Red, false, 2.0f);
	}
#endif

	if (bHit)
	{
		// Using GetOwner() because the hit actor is actually the cesium tileset
		AActor* HitTilesetOwner = nullptr;
		if (HitResult.HasValidHitObjectHandle())
			if (AActor* HitTileset = HitResult.GetActor())
				HitTilesetOwner = HitTileset->GetOwner(); // may be null or sth else than an iModel of course
		if (Cast<AITwinIModel>(HitTilesetOwner) || Cast<AITwinRealityData>(HitTilesetOwner))
		{
			OutHeight = HitResult.ImpactPoint.Z;
			OutNormal = HitResult.ImpactNormal.GetSafeNormal();
			return true;
		}
	}

	// Default fallback if nothing was hit
	OutHeight = InPos.Z;
	OutNormal = FVector::UpVector;
	return false;
}
}

void UBakedAnimKeyFrames::MarkForUpdate()
{
	Status = EBakedKeyFramesStatus::NeedsUpdate;
}

bool UBakedAnimKeyFrames::NeedsUpdate() const
{
	return Status == EBakedKeyFramesStatus::NeedsUpdate;
}

bool UBakedAnimKeyFrames::IsReady() const
{
	return Status == EBakedKeyFramesStatus::Ready && transforms.Num() > 0;
}

float UBakedAnimKeyFrames::GetTotalTime() const
{
	return TotalTime;
}

float UBakedAnimKeyFrames::GetTotalLength() const
{
	return TotalLength;
}

int32 UBakedAnimKeyFrames::GetLaneIndex() const
{
	return LaneIdx;
}

void UBakedAnimKeyFrames::SetSpeed(float InSpeed)
{
	if (InSpeed > 0.f)
		TotalTime = TotalLength / InSpeed;
}

void UBakedAnimKeyFrames::BakeSpline(UWorld* World, const AdvViz::SDK::RefID& SplineId, float InSpeed, int32 InLaneIdx, std::optional<float> InOffset)
{
	Status = EBakedKeyFramesStatus::InProgress;
	transforms.Empty();

	auto AnimSpline = [World, SplineId]() -> AITwinSplineHelper*
		{
			for (TActorIterator<AITwinSplineHelper> It(World); It; ++It)
			{
				auto splineInst = It->GetAVizSpline()->GetRAutoLock();
				if (splineInst->GetId() == SplineId) return *It;
			}
			return nullptr;
		}();

	if (!AnimSpline || InSpeed <= 0.0f)
	{
		Status = EBakedKeyFramesStatus::Invalid;
		return;
	}

	auto UESpline = AnimSpline->GetSplineComponent();
	if (!UESpline)
	{
		Status = EBakedKeyFramesStatus::Invalid;
		return;
	}

	LaneIdx = InLaneIdx;

	constexpr float MaxReasonableSplineLength = 2'000'000.0f; // 20 km

	TotalLength = UESpline->GetSplineLength(); // in cm
	BE_LOGI("App", "Processing animation spline of length " << TotalLength);
	// Check whether the spline is not too short or corrupted (we set a limit of 20 km here)
	if (!FMath::IsFinite(TotalLength) || TotalLength < 0.01f || TotalLength > MaxReasonableSplineLength)
	{
		BE_LOGI("App", "Invalid spline, animation baking cancelled");
		Status = EBakedKeyFramesStatus::Invalid;
		return;
	}

	int32 EstimatedKeyframes = FMath::CeilToInt(TotalLength / BakedFramesStep) + 1;
	if (InOffset.has_value())
		EstimatedKeyframes *= 2;
	transforms.SetNumUninitialized(EstimatedKeyframes);

	FVector PrevLocation = UESpline->GetLocationAtDistanceAlongSpline(0.0f, ESplineCoordinateSpace::World);
	float CurrentDistance = 0.0f;
	float AccumulatedDistance = 0.0f;
	float NextFrameDistance = 0.0f;
	float DistanceStep = InOffset.has_value() ? 0.1f : BakedFramesStep;
	int32 Index = 0;
	while (CurrentDistance <= TotalLength)
	{
		// Position along spline at given distance
		FVector SplineLocation = UESpline->GetLocationAtDistanceAlongSpline(CurrentDistance, ESplineCoordinateSpace::World);
		FVector SplineTangent = UESpline->GetTangentAtDistanceAlongSpline(CurrentDistance, ESplineCoordinateSpace::World).GetSafeNormal();
		FVector SplineRight = UESpline->GetRightVectorAtDistanceAlongSpline(CurrentDistance, ESplineCoordinateSpace::World);

		if (InOffset.has_value())
			SplineLocation = SplineLocation + SplineRight * InOffset.value();

		// Ground height and normal
		float GroundZ = 0.0f;
		FVector GroundNormal = FVector::UpVector;

		//FVector TracePosition = SplineLocation + FVector(0, 0, 500); // trace from above
		if (FindHeight(World, SplineLocation/*TracePosition*/, GroundZ, GroundNormal, 200)) // limit to 2m to avoid snapping to bridges
		{
			SplineLocation.Z = GroundZ;
		}

		if (InOffset.has_value())
		{
			if (CurrentDistance > 0.0f)
				AccumulatedDistance += FVector::Dist(PrevLocation, SplineLocation);
			PrevLocation = SplineLocation;
			if (AccumulatedDistance < NextFrameDistance)
			{
				// Skip this frame, we haven't reached the next step yet
				CurrentDistance += DistanceStep;
				continue;
			}
		}
		
		// Build orientation
		FVector Forward = SplineTangent;
		FVector Up = GroundNormal;
		FVector Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();
		FVector AlignedForward = FVector::CrossProduct(Right, Up).GetSafeNormal();

		// World orientation based on spline and surface
		FMatrix Basis(AlignedForward, Right, Up, FVector::ZeroVector);
		FQuat WorldRotation = FQuat(Basis);

		// Apply alignment fix (Y+ to X+ correction, currently required for all vehicle content items)
		FQuat AlignmentFix = FQuat(FVector::UpVector, 3 * PI / 2);
		FQuat FinalRotation = WorldRotation * AlignmentFix;

		// Add new transform keyframe
		FTransform Keyframe(FinalRotation, SplineLocation);
		transforms[Index++] = Keyframe;

		NextFrameDistance += BakedFramesStep;
		CurrentDistance += DistanceStep;
	}

	transforms.SetNum(Index);

	// If offset is used, total path length might be different from the spline length
	if (InOffset.has_value())
		TotalLength = AccumulatedDistance;
	BE_LOGI("App", "Successfully baked " << Index << " keyframes for lane " << LaneIdx << ", final lane length is " << TotalLength);
	TotalTime = TotalLength / InSpeed;

	Status = transforms.Num() > 0 ? EBakedKeyFramesStatus::Ready : EBakedKeyFramesStatus::Invalid;
}

int32 UBakedAnimKeyFrames::GetKeyframeIndex(float Time)
{
	if (!IsReady())
		return -1;

	int32 Index = FMath::FloorToInt((transforms.Num() - 1) * Time / TotalTime);
	return FMath::Clamp(Index, 0, transforms.Num() - 2); // -2 to allow interpolation with next frame
}

FTransform UBakedAnimKeyFrames::GetTransform(float Time, bool bNeedAlignmentFix, bool bReverse)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(BakedAnimKeyFrames_GetTransform);
	BE_ASSERT(Time <= TotalTime);

	if (!IsReady())
		return FTransform();

	Time = FMath::Clamp(Time, 0.f, TotalTime);

	float IndexFloat(0.f);
	if (bReverse)
	{
		IndexFloat = (transforms.Num() - 1) * (TotalTime-Time) / TotalTime;
	}
	else
	{
		IndexFloat = (transforms.Num() - 1) * Time / TotalTime;
	}
	int32 Index = FMath::FloorToInt(IndexFloat);
	int32 IndexNext = FMath::Clamp(Index + 1, 0, transforms.Num() - 1);
	float Alpha = IndexFloat - (float)Index;

	const FTransform& A = transforms[Index];
	const FTransform& B = transforms[IndexNext];

	FVector Location = FMath::Lerp(
		A.GetLocation(),
		B.GetLocation(),
		Alpha);

	FQuat Rotation = FQuat::Slerp(
		A.GetRotation(),
		B.GetRotation(),
		Alpha).GetNormalized();

	if (!bNeedAlignmentFix)
	{
		// Currently most assets used in animation are standard content vehicles which require alignment fix.
		// So we apply it by default when baking animation and undo it here if needed - for instance, 
		// for characters or articulated vehicles.
		FQuat UndoAlignmentFix = FQuat(FVector::UpVector, PI / 2);
		FQuat NewRot = Rotation * UndoAlignmentFix;
		NewRot.Normalize();
		Rotation = NewRot;
	}

	if (bReverse)
	{
		// rotate the vehicle 180 degrees to face the opposite direction
		const FVector Up = Rotation.GetUpVector();
		const FQuat FlipQuat(Up, PI); // 180 degrees rotation around local up
		FQuat NewRot = Rotation * FlipQuat;
		NewRot.Normalize();
		Rotation = NewRot;
	}

	return FTransform(Rotation, Location);
}
