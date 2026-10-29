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
#include <HAL/PlatformTime.h>
#include <ITwinIModel.h>
#include <ITwinRealityData.h>
#include <ProfilingDebugging/CpuProfilerTrace.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Compil/EnumSwitchCoverage.h>
#	include <Core/Tools/Log.h>
#	include <SDK/Core/Visualization/RefID.h>
#include <Compil/AfterNonUnrealIncludes.h>


float UBakedAnimKeyFrames::BakedFramesStep = 50.f; // 50 cm between the baked frames

namespace PathAnimHelpers
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

bool HasCurvedIntervals(const USplineComponent* Spline)
{
	const float DirectionTolerance = 0.9999f;
	// Iterate over all spline points
	for (int32 i = 0; i < Spline->GetNumberOfSplinePoints() - 1; ++i)
	{
		const FVector P0 = Spline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local);
		const FVector P1 = Spline->GetLocationAtSplinePoint(i + 1, ESplineCoordinateSpace::Local);

		const FVector Chord = P1 - P0;
		const float ChordLengthSq = Chord.SizeSquared();

		if (ChordLengthSq < SMALL_NUMBER)
			continue;

		const FVector Direction = Chord.GetSafeNormal();

		const FVector T0 = Spline->GetTangentAtSplinePoint(i, ESplineCoordinateSpace::Local).GetSafeNormal();
		const FVector T1 = Spline->GetTangentAtSplinePoint(i + 1, ESplineCoordinateSpace::Local).GetSafeNormal();

		// Check if tangents point in the same direction as the segment
		if (FVector::DotProduct(T0, Direction) < DirectionTolerance ||
			FVector::DotProduct(T1, Direction) < DirectionTolerance)
		{
			return true;
		}
	}
	return false;
}

bool IsValidSplineLength(float SplineLength)
{
	constexpr float MaxReasonableSplineLength = 2'000'000.0f; // 20 km
	return FMath::IsFinite(SplineLength) && SplineLength >= 1.f && SplineLength <= MaxReasonableSplineLength;
}

bool IsValidSpline(const FSplineCurves& SplineCurves)
{
	return IsValidSplineLength(SplineCurves.GetSplineLength());
}

void SampleSpline(const FSplineCurves& SplineCurves, FTransform ComponentToWorld, float SamplingStep, TArray<FVector> &OutPositions, TArray<FVector>& OutDirections)
{
	float SplineLength = SplineCurves.GetSplineLength();
	OutPositions.SetNumUninitialized(FMath::CeilToInt(SplineLength / SamplingStep) + 1);
	OutDirections.SetNumUninitialized(FMath::CeilToInt(SplineLength / SamplingStep) + 1);
	float Distance = 0.0f;
	int Index = 0;
	while (true)
	{
		const float InputKey = SplineCurves.ReparamTable.Eval(Distance);
		OutPositions[Index] = ComponentToWorld.TransformPosition(SplineCurves.Position.Eval(InputKey));
		OutDirections[Index] = ComponentToWorld.TransformVectorNoScale(SplineCurves.Position.EvalDerivative(InputKey)).GetSafeNormal();

		//if (OutDirections[Index].IsNearlyZero())
		//{
		//	// The tangents of the first and last points can be 0. In this case we
		//	// compute forward direction from nearby input keys.
		//	const float DeltaKey = 0.001f;
		//	const float Key0 = FMath::Max(0.0f, InputKey - DeltaKey);
		//	const float Key1 = FMath::Min(static_cast<float>(SplineCurves.Position.Points.Num() - 1), InputKey + DeltaKey);
		//	OutDirections[Index] = (SplineCurves.Position.Eval(Key1) - SplineCurves.Position.Eval(Key0)).GetSafeNormal();
		//}

		Index++;

		if (Distance >= SplineLength)
			break;
		Distance += SamplingStep;
	}
	OutPositions.SetNum(Index);
	OutDirections.SetNum(Index);
	// Hack to avoid zero tangents at the start and end of the spline when 
	// the tangents of the first and last points are 0.
	if (OutDirections[0].IsNearlyZero())
		OutDirections[0] = OutDirections[1];
	if (OutDirections.Last().IsNearlyZero())
		OutDirections.Last() = OutDirections[OutDirections.Num() - 2];
}

void PrecomputeKeyFrames(UWorld* World, const TArray<FVector>& InPositions, const TArray<FVector>& InDirections, float InSamplingStep, float InSpeed, float InOffset, TArray<FTransform>& OutTransforms, float OutTotalLength)
{
	OutTotalLength = (InPositions.Num() - 1) * InSamplingStep;

	bool isLaneWithOffset = (FMath::Abs(InOffset) > KINDA_SMALL_NUMBER);
	int32 EstimatedKeyframes = FMath::CeilToInt(OutTotalLength / UBakedAnimKeyFrames::GetBakedFramesStep()) + 1;
	if (isLaneWithOffset)
		EstimatedKeyframes *= 2;
	OutTransforms.SetNumUninitialized(EstimatedKeyframes);

	// Apply alignment fix (Y+ to X+ correction, currently required for all vehicle
	// content items except articulated ones)
	FQuat AlignmentFix = FQuat(FVector::UpVector, 3 * PI / 2);

	FVector PrevLocation = InPositions[0];
	float CurrentDistance = 0.0f;
	float AccumulatedDistance = 0.0f;
	float NextFrameDistance = 0.0f;
	int32 Index = 0;
	for (int32 i = 0; i < InPositions.Num(); ++i)
	{
		FVector SplineLocation = InPositions[i];
		FVector SplineTangent = InDirections[i];
		FVector SplineRight = FVector::CrossProduct(SplineTangent, FVector::UpVector).GetSafeNormal();
		if (isLaneWithOffset)
		{
			SplineLocation = SplineLocation + SplineRight * InOffset;
			if (i > 0)
				AccumulatedDistance += FVector::Dist(PrevLocation, SplineLocation);
			PrevLocation = SplineLocation;
			if (AccumulatedDistance < NextFrameDistance)
			{
				// Skip this frame, we haven't reached the next step yet
				continue;
			}
		}

		// Ground height and normal
		float GroundZ = 0.0f;
		FVector GroundNormal = FVector::UpVector;
		//FVector TracePosition = SplineLocation + FVector(0, 0, 500); // trace from above
		if (FindHeight(World, SplineLocation/*TracePosition*/, GroundZ, GroundNormal, 200)) // limit to 2m to avoid snapping to bridges
		{
			SplineLocation.Z = GroundZ;
		}

		// Build orientation
		FVector Forward = SplineTangent;
		FVector Up = GroundNormal;
		FVector Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();
		FVector AlignedForward = FVector::CrossProduct(Right, Up).GetSafeNormal();

		// World orientation based on spline and surface
		FMatrix Basis(AlignedForward, Right, Up, FVector::ZeroVector);
		FQuat WorldRotation = FQuat(Basis);
		FQuat FinalRotation = WorldRotation * AlignmentFix;

		// Add new transform keyframe
		FTransform Keyframe(FinalRotation, SplineLocation);
		OutTransforms[Index++] = Keyframe;
		NextFrameDistance += UBakedAnimKeyFrames::GetBakedFramesStep();
	}

	OutTransforms.SetNum(Index);

	// If offset is used, total path length might be different from the spline length
	if (isLaneWithOffset)
		OutTotalLength = AccumulatedDistance;
}

} // namespace PathAnimHelpers

void UBakedAnimKeyFrames::CopyFromData(int32 InLaneIdx, float InSpeed, float InTotalLength, const TArray<FTransform>& InTransforms)
{
	LaneIdx = InLaneIdx;
	TotalLength = InTotalLength;
	TotalTime = InTotalLength / InSpeed;
	Transforms = InTransforms;
	Status = Transforms.Num() > 0 ? EBakedKeyFramesStatus::Ready : EBakedKeyFramesStatus::Invalid;
}

void UBakedAnimKeyFrames::MarkForUpdate()
{
	Status = EBakedKeyFramesStatus::NeedsUpdate;
}

bool UBakedAnimKeyFrames::NeedsUpdate() const
{
	return Status == EBakedKeyFramesStatus::NeedsUpdate || Status == EBakedKeyFramesStatus::Invalid;
}

bool UBakedAnimKeyFrames::IsReady() const
{
	return Status == EBakedKeyFramesStatus::Ready && Transforms.Num() > 0;
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

void UBakedAnimKeyFrames::BakeSpline(UWorld* World, const AdvViz::SDK::RefID& SplineId, float InSpeed, int32 InLaneIdx, float InOffset)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(BakedAnimKeyFrames_BakeSpline);
	const double BakeStartTime = FPlatformTime::Seconds();
	auto LogBakeDuration = [BakeStartTime](const char* Outcome)
		{
			const double ElapsedMs = (FPlatformTime::Seconds() - BakeStartTime) * 1000.0;
			BE_LOGI("App", "BakeSpline benchmark (" << Outcome << "): " << ElapsedMs << " ms");
		};

	Status = EBakedKeyFramesStatus::InProgress;
	Transforms.Empty();

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
		LogBakeDuration("invalid input");
		return;
	}

	auto UESpline = AnimSpline->GetSplineComponent();
	if (!UESpline)
	{
		Status = EBakedKeyFramesStatus::Invalid;
		LogBakeDuration("missing spline component");
		return;
	}

	LaneIdx = InLaneIdx;

	TotalLength = UESpline->GetSplineLength(); // in cm
	BE_LOGI("App", "Processing animation spline of length " << TotalLength);
	if (!PathAnimHelpers::IsValidSplineLength(TotalLength))
	{
		BE_LOGI("App", "Invalid spline, animation baking cancelled");
		Status = EBakedKeyFramesStatus::Invalid;
		LogBakeDuration("invalid spline length");
		return;
	}

	bool isLaneWithOffset = (FMath::Abs(InOffset) > KINDA_SMALL_NUMBER);
	int32 EstimatedKeyframes = FMath::CeilToInt(TotalLength / BakedFramesStep) + 1;
	if (isLaneWithOffset)
		EstimatedKeyframes *= 2;
	Transforms.SetNumUninitialized(EstimatedKeyframes);

	// Apply alignment fix (Y+ to X+ correction, currently required for all vehicle
	// content items except articulated ones)
	FQuat AlignmentFix = FQuat(FVector::UpVector, 3 * PI / 2);

	FVector PrevLocation = UESpline->GetLocationAtDistanceAlongSpline(0.0f, ESplineCoordinateSpace::World);
	float CurrentDistance = 0.0f;
	float AccumulatedDistance = 0.0f;
	float NextFrameDistance = 0.0f;
	// For lanes with offset, we use a smaller step to ensure we trace a smooth curve
	// along the central spline (otherwise it might lead to visual inconsistencies 
	// with multi-lane paths at sharp turns)
	float DistanceStep = UBakedAnimKeyFrames::GetSamplingStep(PathAnimHelpers::HasCurvedIntervals(UESpline), isLaneWithOffset);
	int32 Index = 0;
	while (CurrentDistance <= TotalLength)
	{
		// Position along spline at given distance
		FVector SplineLocation = UESpline->GetLocationAtDistanceAlongSpline(CurrentDistance, ESplineCoordinateSpace::World);
		FVector SplineTangent = UESpline->GetTangentAtDistanceAlongSpline(CurrentDistance, ESplineCoordinateSpace::World).GetSafeNormal();
		FVector SplineRight = UESpline->GetRightVectorAtDistanceAlongSpline(CurrentDistance, ESplineCoordinateSpace::World);

		if (isLaneWithOffset)
		{
			SplineLocation = SplineLocation + SplineRight * InOffset;

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

		// Ground height and normal
		float GroundZ = 0.0f;
		FVector GroundNormal = FVector::UpVector;
		//FVector TracePosition = SplineLocation + FVector(0, 0, 500); // trace from above
		if (PathAnimHelpers::FindHeight(World, SplineLocation/*TracePosition*/, GroundZ, GroundNormal, 200)) // limit to 2m to avoid snapping to bridges
		{
			SplineLocation.Z = GroundZ;
		}
		
		// Build orientation
		FVector Forward = SplineTangent;
		FVector Up = GroundNormal;
		FVector Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();
		FVector AlignedForward = FVector::CrossProduct(Right, Up).GetSafeNormal();

		// World orientation based on spline and surface
		FMatrix Basis(AlignedForward, Right, Up, FVector::ZeroVector);
		FQuat WorldRotation = FQuat(Basis);
		FQuat FinalRotation = WorldRotation * AlignmentFix;

		// Add new transform keyframe
		FTransform Keyframe(FinalRotation, SplineLocation);
		Transforms[Index++] = Keyframe;

		NextFrameDistance += BakedFramesStep;
		CurrentDistance += DistanceStep;
	}

	Transforms.SetNum(Index);

	// If offset is used, total path length might be different from the spline length
	if (isLaneWithOffset)
		TotalLength = AccumulatedDistance;
	BE_LOGI("App", "Successfully baked " << Index << " keyframes for lane " << LaneIdx << ", final lane length is " << 0.01*TotalLength << "m");
	TotalTime = TotalLength / InSpeed;

	Status = Transforms.Num() > 0 ? EBakedKeyFramesStatus::Ready : EBakedKeyFramesStatus::Invalid;
	LogBakeDuration(Status == EBakedKeyFramesStatus::Ready ? "ready" : "invalid (no keyframes)");
}

int32 UBakedAnimKeyFrames::GetKeyframeIndex(float Time)
{
	if (!IsReady())
		return -1;

	int32 Index = FMath::FloorToInt((Transforms.Num() - 1) * Time / TotalTime);
	return FMath::Clamp(Index, 0, Transforms.Num() - 2); // -2 to allow interpolation with next frame
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
		IndexFloat = (Transforms.Num() - 1) * (TotalTime-Time) / TotalTime;
	}
	else
	{
		IndexFloat = (Transforms.Num() - 1) * Time / TotalTime;
	}
	int32 Index = FMath::FloorToInt(IndexFloat);
	int32 IndexNext = FMath::Clamp(Index + 1, 0, Transforms.Num() - 1);
	float Alpha = IndexFloat - (float)Index;

	const FTransform& A = Transforms[Index];
	const FTransform& B = Transforms[IndexNext];

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
