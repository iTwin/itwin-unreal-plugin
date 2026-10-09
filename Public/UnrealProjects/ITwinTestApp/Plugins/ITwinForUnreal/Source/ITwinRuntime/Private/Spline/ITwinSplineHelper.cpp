/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSplineHelper.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Spline/ITwinSplineHelper.h>
#include <Spline/ITwinSplineHelper.inl>

#include <Spline/ITwinSplineHelper2DWidgetImpl.h>
#include <Spline/ITwinSplineGeometry.h>
#include <PathAnimation/ITwinAnimPathShaderParameters.h>

#include <Math/UEMathConversion.h>
#include <CesiumGlobeAnchorComponent.h>
#include <CesiumCartographicPolygon.h>
#include <Components/SceneComponent.h>
#include <Components/SplineComponent.h>
#include <Components/SplineMeshComponent.h>
#include <Engine/Polys.h>
#include <Engine/World.h>
#include <GameFramework/PlayerController.h>
#include <IncludeCesium3DTileset.h>
#include <ITwinTilesetAccess.h>

#include <DrawDebugHelpers.h>
#include <EngineUtils.h> // for TActorIterator<>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Compil/EnumSwitchCoverage.h>
#	include <SDK/Core/Tools/Assert.h>
#	include <SDK/Core/Visualization/Spline.h>
#include <Compil/AfterNonUnrealIncludes.h>

#define SPL_LOCAL ESplineCoordinateSpace::Local
#define SPL_WORLD ESplineCoordinateSpace::World
#define SMOOTH_FACTOR 0.5

#define CHECK_NUMBER_OF_POINTS() \
	BE_ASSERT(CheckNumberOfPoints(), "Wrong number of spline points")

#define CHECK_NUMBER_OF_SPLINE_MESH_COMPONENTS() \
	BE_ASSERT(CheckSplineMeshComponents(), "Wrong number of spline mesh components")

namespace ITwinSpline
{
	AdvViz::SDK::ESplineTangentMode UEToAViz(const EITwinTangentMode sdkMode)
	{
		switch (sdkMode)
		{
			BE_NO_UNCOVERED_ENUM_ASSERT_AND_FALLTHROUGH
		case EITwinTangentMode::Linear:
			return AdvViz::SDK::ESplineTangentMode::Linear;
		case EITwinTangentMode::Smooth:
			return AdvViz::SDK::ESplineTangentMode::Smooth;
		case EITwinTangentMode::Custom:
			return AdvViz::SDK::ESplineTangentMode::Custom;
		}
	}

	EITwinTangentMode AVizToUE(const AdvViz::SDK::ESplineTangentMode ueMode)
	{
		switch (ueMode)
		{
			BE_NO_UNCOVERED_ENUM_ASSERT_AND_FALLTHROUGH
		case AdvViz::SDK::ESplineTangentMode::Linear:
			return EITwinTangentMode::Linear;
		case AdvViz::SDK::ESplineTangentMode::Smooth:
			return EITwinTangentMode::Smooth;
		case AdvViz::SDK::ESplineTangentMode::Custom:
			return EITwinTangentMode::Custom;
		}
	}
}

struct AITwinSplineHelper::FImpl
{
	AITwinSplineHelper& Owner;
	EITwinTangentMode TangentMode = EITwinTangentMode::Custom;
	EITwinSplineUsage Usage = EITwinSplineUsage::Undefined;
	AdvViz::SDK::ISplinePtr Spline;
	double ScaleFactor = 2.0;
	std::optional<double> FixedSplineScale;
	bool bHasComputedScale = false;

	struct FTracingData
	{
		/// Polygon built from the spline points (used for line tracing).
		FPoly SplinePolygon;

		/// Barycenter used for selection gizmo when the spline is globally selected.
		FVector SplineBarycenter = FVector::ZeroVector;

		bool bNeedUpdateTracingData = true;
	};
	mutable FTracingData TracingData;

	bool bSelected = false;
	int32 SelectedPointIndex = -1;
	bool bInteractiveCreationInProgress = false;

	bool bNeedsUpdate2DElements = false;

#if WITH_EDITOR
	FString CustomActorLabel;
#endif

	//! Store the shader scalar parameters for the anim path usage. Useful in case of insertion
	//! of a new spline mesh.
	std::array<float, static_cast<size_t>(EITwinAnimPathShaderScalarParam::Count)> AnimPathShaderScalarParams;

	static constexpr double RIBBON_SCALE = 0.60;
	static std::optional<EITwinSplineUsage> UsageForSpawnedActor;


	FImpl(AITwinSplineHelper& InOwner);
	void Initialize(USplineComponent* splineComp, AdvViz::SDK::ISplinePtr spline);
	void UpdatePointFromUEtoAViz(int32 pointIndex);
	void UpdatePointFromAVizToUE(int32 pointIndex);
	void UpdateSplineFromUEtoAViz();
	void UpdateSplineFromAVizToUE();

	inline bool NeedsDraw3DElements() const
	{
		return Owner.bDraw3DRibbon || Owner.bDraw3DPoints;
	}
	void InitMeshComponent(UStaticMeshComponent* meshComp, UStaticMesh* mesh);
	void AddAllMeshComponents();
	void RecreateAllMeshComponents();
	void AddSplineMeshComponentsForPoint(int32 pointIndex);
	void RemoveSplineMeshComponentForPoint(int32 pointIndex);
	bool CheckSplineMeshComponents() const;
	void AddMeshComponentsForPoint(int32 pointIndex);
	void UpdateAllMeshComponents();
	void UpdateMeshComponentsForPoint(int32 pointIndex);

	void SetTangentMode(const EITwinTangentMode mode);
	void SetTransform(const FTransform& NewTransform, bool markSplineForSaving);
	FVector GetLocationAtSplinePoint(int32 pointIndex) const;
	void SetLocationAtSplinePoint(int32 pointIndex, const FVector& location);

	void UpdateTangentAuto(int32 pointIndex, const FVector& pos, const FVector& prevPos, const FVector& nextPos);

	void UpdateTangentDir(int32 pointIndex, const FVector& newTangentDir);
	void UpdateTangent(int32 pointIndex, const FVector& newTangentDir, float tangentLength);

	bool IncludeInWorldBox(FBox& Box) const;

	//! Returns true if the point was successfully deleted.
	bool DeletePoint(int32 pointIndex);

	bool DuplicatePoint(int32 pointIndex);
	bool DuplicatePoint(int32& pointIndex, FVector& newWorldPosition);
	int32 InsertPointAt(const int32 PointIndex, FVector const& NewWorldPosition);

	void ScaleMeshComponentsForCurrentPOV();
	void ComputeSplineScaleFromWidth(float Width);
	inline bool AutoScalePoints() const
	{
		return true;
	}
	inline bool AutoScaleSplineRibbon() const
	{
		return Owner.GetUsage() != EITwinSplineUsage::AnimPathTraffic
			&& Owner.GetUsage() != EITwinSplineUsage::AnimPathCrowd;
	}

	//! Whether we use the path animation shader for this spline (with advanced featurs such as lane count,
	//! lane width, separator width, etc.).
	inline bool UsePathAnimationShader() const
	{
		return Owner.GetUsage() == EITwinSplineUsage::AnimPathTraffic
			|| Owner.GetUsage() == EITwinSplineUsage::AnimPathCrowd;
	}

	//! Returns true if the spline is used as an helper for edge display (introduced for cutout cubes).
	inline bool IsEdgeDisplayHelper() const { return (Owner.GetUsage() == EITwinSplineUsage::EdgeDisplayHelper); }
	inline double GetRibbonScale() const;
	FVector2D GetRibbonScale2D() const;

	void SetClosedLoop(bool bInClosedLoop, bool bUpdateSpline);
	bool LoopIndices() const;

	//! Propagate a modification to all secondary cartographic polygons. PrevPointIndex and NextPointIndex
	//! can be supplied to update the surrounding points as well.
	void CopyPointToSecondaryCartographicPolygons(int32 PointIndex,
		int32 PrevPointIndex = -1, int32 NextPointIndex = -1);

	void InsertPointInSecondaryCartographicPolygons(int32 PointIndex);

	template <typename TFunc>
	void ForEachUESplineComponent(TFunc const& Func) const;


	inline int32 GetCommonNumberOfPointsInAllPolygons() const
	{
		int32 CommonNum = -1;
		for (auto const& [_, PolygonPtr] : Owner.PerGeorefPolygonMap)
		{
			ACesiumCartographicPolygon const* Polygon = PolygonPtr.Get();
			if (Polygon && ensure(Polygon->Polygon))
			{
				const int32 NbPts = Polygon->Polygon->GetNumberOfSplinePoints();
				if (CommonNum == -1)
					CommonNum = NbPts;
				else if (NbPts != CommonNum)
					return -1;
			}
		}
		return CommonNum;
	}

	inline bool CheckNumberOfPoints() const
	{
		if (!Spline)
		{
			BE_ISSUE("CheckNumberOfPoints: no Spline object.");
			return false;
		}
		{
			auto spline = Spline->GetRAutoLock();
			if (!ensureMsgf(
				Owner.SplineComponent->GetNumberOfSplinePoints() == static_cast<int32>(spline->GetNumberOfPoints()),
				TEXT("The UE and AdvViz::SDK splines should have the same number of points.")))
			{
				return false;
			}
		}

		if (!ensureMsgf(Owner.PerGeorefPolygonMap.IsEmpty() ||
			Owner.SplineComponent->GetNumberOfSplinePoints() == GetCommonNumberOfPointsInAllPolygons(),
			TEXT("All cartographic polygons associated to this spline should have the same number of points.")))
		{
			return false;
		}
		return true;
	}

	void UpdateTracingData() const;
	void InvalidateTracingData() { TracingData.bNeedUpdateTracingData = true; }
	bool DoesLineIntersectSplinePolygon(const FVector& Start, const FVector& End) const;

	FVector const& GetBarycenter() const {
		if (TracingData.bNeedUpdateTracingData)
			UpdateTracingData();
		return TracingData.SplineBarycenter;
	}

	void SetSelected(bool bInSelected);
	void SetSelectedPointIndex(int32 PointIndex);

	void SetInteractiveCreationInProgress(bool bInProgress);

	bool NeedsUpdate2DElements() const { return bNeedsUpdate2DElements; }
	void SetNeedsUpdate2DElements(bool bNeedsUpdate) { bNeedsUpdate2DElements  = bNeedsUpdate; }
	void Invalidate2DElements() { SetNeedsUpdate2DElements(true); }

	void OnSplineModified()
	{
		InvalidateTracingData();
		Invalidate2DElements();
	}

	void SetTightness(int32 PointIndex, float InTightness);
	float GetTightness(int32 PointIndex) const;

	//! Set a scalar parameter value for the path animation shader.
	void SetPathAnimShaderScalarParameterValue(EITwinAnimPathShaderScalarParam Param, float Value, bool bApplyToMeshComponents);
	//! Transfer all path animation shader parameters to the spline mesh components (to be called after a
	//! batch of changes).
	void TransferPathAnimShaderParametersToMeshes();
	//! Returns the array view of the path animation shader scalar parameters, and the index of the first.
	inline TArrayView<const float> GetPathAnimScalarParams(int32& OutFirstParamIndex) const;

	//! Adapt the U-scaling of the given spline mesh component to the current length of the corresponding
	//! segment of the spline (for path animation usage).
	void AdaptArrowsDensityForSplineMeshComponent(int32 PointIndex);
};

/*static*/
std::optional<EITwinSplineUsage> AITwinSplineHelper::FImpl::UsageForSpawnedActor = std::nullopt;

AITwinSplineHelper::FImpl::FImpl(AITwinSplineHelper& InOwner) : Owner(InOwner)
{
	// Initialize AnimPathShaderScalarParams with default values (will be filled with actual values when the
	// spline is initialized from the animation path.
	AnimPathShaderScalarParams.fill(0.f);
}

template <typename TFunc>
void AITwinSplineHelper::FImpl::ForEachUESplineComponent(TFunc const& Func) const
{
	if (Owner.SplineComponent)
	{
		Func(*Owner.SplineComponent);
	}
	for (auto& [_, PolygonPtr] : Owner.PerGeorefPolygonMap)
	{
		ACesiumCartographicPolygon* Polygon = PolygonPtr.Get();
		if (Polygon && Polygon->Polygon != Owner.SplineComponent
			&& ensure(Polygon->Polygon))
		{
			Func(*Polygon->Polygon);
		}
	}
}

void AITwinSplineHelper::FImpl::Initialize(
	USplineComponent* splineComp, AdvViz::SDK::ISplinePtr splinePtr2)
{
	if (!splineComp || !splinePtr2)
	{
		BE_ISSUE("Invalid parameters to initialize the spline helper.");
		return;
	}

	Owner.SplineComponent = splineComp;
	Spline = splinePtr2;

	size_t numberOfPoints = 0;
	int32 numberOfSplinePoints = 0;
	{
		auto spline = Spline->GetRAutoLock();
		ensureMsgf(Spline && static_cast<EITwinSplineUsage>(spline->GetUsage()) == this->Usage,
			TEXT("spline usage mismatch vs AdvViz::SDK"));
		numberOfPoints = spline->GetNumberOfPoints();
		numberOfSplinePoints = splineComp->GetNumberOfSplinePoints();
	}

	// Detect the direction of the update
	if (numberOfPoints == 0 && numberOfSplinePoints > 0)
	{
		UpdateSplineFromUEtoAViz();
	}
	else if (numberOfPoints > 0 && numberOfSplinePoints == 0)
	{
		UpdateSplineFromAVizToUE();
	}

	AddAllMeshComponents();
	Invalidate2DElements();
}

void AITwinSplineHelper::FImpl::UpdatePointFromUEtoAViz(int32 pointIndex)
{
	using namespace AdvViz::SDK;
	typedef FITwinMathConversion MathConv;

	if (!Spline || !Owner.SplineComponent)
		return;

	size_t index = static_cast<size_t>(pointIndex);
	USplineComponent const& SplineComp(*Owner.SplineComponent);

	ISplinePointPtr pointPtr;
	{
		auto spline = Spline->GetRAutoLock();
		pointPtr = spline->GetPoint(index);
	}
	auto point = pointPtr->GetAutoLock();
	point->SetPosition(MathConv::UEtoSDK(SplineComp.GetLocationAtSplinePoint(index, SPL_LOCAL)));
	point->SetUpVector(MathConv::UEtoSDK(SplineComp.GetUpVectorAtSplinePoint(index, SPL_LOCAL)));
	point->SetInTangent(MathConv::UEtoSDK(SplineComp.GetArriveTangentAtSplinePoint(index, SPL_LOCAL)));
	point->SetOutTangent(MathConv::UEtoSDK(SplineComp.GetLeaveTangentAtSplinePoint(index, SPL_LOCAL)));
	point->SetInTangentMode(ITwinSpline::UEToAViz(TangentMode));
	point->SetOutTangentMode(ITwinSpline::UEToAViz(TangentMode));
	point->SetShouldSave(true);
}

void AITwinSplineHelper::FImpl::UpdatePointFromAVizToUE(int32 pointIndex)
{
	using namespace AdvViz::SDK;
	typedef FITwinMathConversion MathConv;

	if (!Spline || !Owner.SplineComponent)
		return;

	auto spline = Spline->GetRAutoLock();

	ISplinePointPtr pointPtr = spline->GetPoint(static_cast<size_t>(pointIndex));
	auto point = pointPtr->GetAutoLock();
	USplineComponent& SplineComp(*Owner.SplineComponent);

	SplineComp.SetLocationAtSplinePoint(
		pointIndex, MathConv::SDKtoUE(point->GetPosition()), SPL_LOCAL, false);

	SplineComp.SetTangentsAtSplinePoint(
		pointIndex,
		MathConv::SDKtoUE(point->GetInTangent()),
		MathConv::SDKtoUE(point->GetOutTangent()),
		SPL_LOCAL, false);

	SplineComp.SetUpVectorAtSplinePoint(
		pointIndex, MathConv::SDKtoUE(point->GetUpVector()), SPL_LOCAL, false);

	CopyPointToSecondaryCartographicPolygons(pointIndex);
}

void AITwinSplineHelper::FImpl::UpdateSplineFromUEtoAViz()
{
	if (!Spline || !Owner.SplineComponent)
		return;
	USplineComponent const& SplineComp(*Owner.SplineComponent);

	int32 NbPoints = 0;
	{
		auto spline = Spline->GetAutoLock();
		spline->SetTransform(FITwinMathConversion::UEtoSDK(Owner.GetActorTransform()));

		// Adjust the number of points in the AdvViz::SDK spline
		NbPoints = SplineComp.GetNumberOfSplinePoints();
		if (NbPoints != static_cast<int32>(spline->GetNumberOfPoints()))
		{
			spline->SetNumberOfPoints(static_cast<size_t>(NbPoints));
		}
	}

	for (int32 i = 0; i < NbPoints; ++i)
	{
		UpdatePointFromUEtoAViz(i);
	}

	CHECK_NUMBER_OF_POINTS();

	{
		auto spline = Spline->GetAutoLock();
		spline->SetShouldSave(true);
	}
}

void AITwinSplineHelper::FImpl::UpdateSplineFromAVizToUE()
{
	if (!Spline || !Owner.SplineComponent)
		return;

	auto spline = Spline->GetAutoLock();
	Owner.SetTransform(FITwinMathConversion::SDKtoUE(spline->GetTransform()), false);

	// Adjust the number of points in all USplineComponent(s)
	const int32 NbPoints = static_cast<int32>(spline->GetNumberOfPoints());

	ForEachUESplineComponent([NbPoints](USplineComponent& SplineComp)
	{
		int32 CurNbPoints = SplineComp.GetNumberOfSplinePoints();
		if (CurNbPoints < NbPoints)
		{
			// Add missing point(s) to UE component
			for (; CurNbPoints < NbPoints; ++CurNbPoints)
			{
				SplineComp.AddSplinePoint(FVector(0), SPL_LOCAL, false);
			}
		}
		else if (CurNbPoints > NbPoints)
		{
			// Remove supernumerary point(s) from UE component
			for (; CurNbPoints > NbPoints; CurNbPoints--)
			{
				SplineComp.RemoveSplinePoint(CurNbPoints - 1, false);
			}
		}
	});
	CHECK_NUMBER_OF_POINTS();

	ForEachUESplineComponent([bIsClosedLoop = spline->IsClosedLoop()](USplineComponent& SplineComponent)
	{
		SplineComponent.SetClosedLoop(bIsClosedLoop, false /*bUpdateSpline*/);
	});

	// Update points
	for (int32 i = 0; i < NbPoints; ++i)
	{
		UpdatePointFromAVizToUE(i);
	}

	// Update tangent mode
	bool isSameModeForAllPoints = true;
	bool bHasInitializedMode = false;
	AdvViz::SDK::ESplineTangentMode tgtMode = AdvViz::SDK::ESplineTangentMode::Linear;
	for (auto const& spPointPtr : spline->GetPoints())
	{
		auto spPoint = spPointPtr->GetRAutoLock();
		if (!bHasInitializedMode)
		{
			tgtMode = spPoint->GetInTangentMode();
			bHasInitializedMode = true;
		}
		if ((spPoint->GetInTangentMode() != tgtMode) || (spPoint->GetOutTangentMode() != tgtMode))
		{
			isSameModeForAllPoints = false;
			break;
		}
	}
	TangentMode = isSameModeForAllPoints ? ITwinSpline::AVizToUE(tgtMode) : EITwinTangentMode::Custom;

	ForEachUESplineComponent([NbPoints](USplineComponent& SplineComponent)
	{
		SplineComponent.UpdateSpline();
	});

	OnSplineModified();
}

void AITwinSplineHelper::FImpl::InitMeshComponent(UStaticMeshComponent* meshComp, UStaticMesh* mesh)
{
	BE_ASSERT(NeedsDraw3DElements());
	USceneComponent* rootComp = Owner.GetRootComponent();

	rootComp->SetMobility(EComponentMobility::Static); // avoids a warning
	meshComp->AttachToComponent(rootComp, FAttachmentTransformRules::KeepWorldTransform);
	rootComp->SetMobility(EComponentMobility::Movable);

	meshComp->SetMobility(EComponentMobility::Movable);
	meshComp->SetStaticMesh(mesh);
	meshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	meshComp->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Block);
}

void AITwinSplineHelper::FImpl::AddAllMeshComponents()
{
	if (!NeedsDraw3DElements())
	{
		// No 3D element should be drawn, so no need to create mesh components.
		return;
	}
	if (Owner.bDraw3DRibbon && !Owner.SplineMesh)
	{
		// For edge display mode, use a cylinder with rounded caps ("EdgeMesh") instead of a ribbon
		// ("SplineMesh") to make edges independent from the view point.
		// For path animation with one or several lanes, use a dedicated mesh (with an arrow indicating the
		// direction).
		FString const SplineMeshAssetName = IsEdgeDisplayHelper()
			? TEXT("EdgeMesh")
			: (UsePathAnimationShader()
				? TEXT("PathAnimationMesh")
				: TEXT("SplineMeshTransp"));
		Owner.SplineMesh = LoadObject<UStaticMesh>(
			nullptr,
			*FString::Printf(TEXT("/ITwinForUnreal/ITwin/Meshes/%s.%s"), *SplineMeshAssetName, *SplineMeshAssetName),
			nullptr,
			LOAD_None,
			nullptr);
	}

	if (Owner.bDraw3DPoints && !Owner.PointMesh)
	{
		Owner.PointMesh = LoadObject<UStaticMesh>(
			nullptr, TEXT("/ITwinForUnreal/ITwin/Meshes/PointMesh.PointMesh"), nullptr, LOAD_None, nullptr);
	}

	const int32 NbSplinePoints = Owner.GetNumberOfSplinePoints();
	for (int32 i = 0; i < NbSplinePoints; ++i)
	{
		AddMeshComponentsForPoint(i);
	}
	CHECK_NUMBER_OF_SPLINE_MESH_COMPONENTS();

	if (IsEdgeDisplayHelper())
	{
		// Edge only mode.
		Owner.Set3DPointsHiddenInGame(true);
	}
}

void AITwinSplineHelper::FImpl::RecreateAllMeshComponents()
{
	for (auto const& SplineMeshComp : Owner.SplineMeshComponents)
	{
		SplineMeshComp->UnregisterComponent();
		SplineMeshComp->DestroyComponent();
	}
	Owner.SplineMeshComponents.Reset();

	for (auto const& PointMeshComp : Owner.PointMeshComponents)
	{
		PointMeshComp->UnregisterComponent();
		PointMeshComp->DestroyComponent();
	}
	Owner.PointMeshComponents.Reset();

	AddAllMeshComponents();
}

bool AITwinSplineHelper::FImpl::CheckSplineMeshComponents() const
{
	if (!Owner.bDraw3DRibbon)
	{
		return Owner.SplineMeshComponents.Num() == 0;
	}
	const bool bIsClosedLoop = Owner.IsClosedLoop();
	const int32 NbSplinePoints = Owner.GetNumberOfSplinePoints();
	if (bIsClosedLoop)
	{
		return Owner.SplineMeshComponents.Num() == NbSplinePoints;
	}
	else
	{
		return Owner.SplineMeshComponents.Num() == NbSplinePoints - 1;
	}
}

inline double AITwinSplineHelper::FImpl::GetRibbonScale() const
{
	if (FixedSplineScale.has_value())
	{
		return *FixedSplineScale;
	}
	else
	{
		return this->ScaleFactor * RIBBON_SCALE;
	}
}

FVector2D AITwinSplineHelper::FImpl::GetRibbonScale2D() const
{
	const double RibbonScale = GetRibbonScale();
	// For edge display mode, the geometry is a cylinder (with rounded caps), so the scale should be applied
	// on both axis. For other modes, the geometry is a ribbon, so the scale should only be applied on the
	// width axis.
	return FVector2D(
		RibbonScale,
		IsEdgeDisplayHelper() ? RibbonScale : 1.0
	);
}

inline TArrayView<const float> AITwinSplineHelper::FImpl::GetPathAnimScalarParams(int32& OutFirstParamIndex) const
{
	// Offset of 1 because the first parameter is the selection parameter, which is not managed here.
	constexpr int32 FirstParamIndex = 1;
	OutFirstParamIndex = FirstParamIndex;
	return TArrayView<const float>(
		AnimPathShaderScalarParams.data() + FirstParamIndex,
		(int32)AnimPathShaderScalarParams.size() - FirstParamIndex);
}

void AITwinSplineHelper::FImpl::AdaptArrowsDensityForSplineMeshComponent(int32 PointIndex)
{
	if (!ensure(UsePathAnimationShader()
		&& PointIndex >= 0
		&& PointIndex < Owner.SplineMeshComponents.Num()))
	{
		return;
	}
	if (!Owner.SplineComponent || !Owner.SplineMeshComponents[PointIndex])
	{
		return;
	}
	// The constant 6 below is defined in M_PathAnimation.uasset (UScaling). I did not put this parameter
	// in the enum EITwinAnimPathShaderScalarParam because it is not to be defined per path but per segment,
	// as it is used to have a regular distribution of arrows over the whole path.
	static constexpr int32 UV_SCALING_PARAM_INDEX = 6;
	static_assert(UV_SCALING_PARAM_INDEX == static_cast<int32>(EITwinAnimPathShaderScalarParam::Count), "UV scaling parameter index mismatch");

	const float StartDistance = Owner.SplineComponent->GetDistanceAlongSplineAtSplinePoint(PointIndex);
	const float EndDistance = Owner.SplineComponent->GetDistanceAlongSplineAtSplinePoint(PointIndex + 1);
	const float DistanceInMeters = FMath::Abs(EndDistance - StartDistance) * 0.01f;
	static constexpr float ARROW_PER_METER = 1.0f / 6.0f; // 1 arrow every 6 meters

	Owner.SplineMeshComponents[PointIndex]->SetCustomPrimitiveDataFloat(UV_SCALING_PARAM_INDEX, DistanceInMeters * ARROW_PER_METER);
}

void AITwinSplineHelper::FImpl::AddSplineMeshComponentsForPoint(int32 pointIndex)
{
	BE_ASSERT(Owner.bDraw3DRibbon);

	if (!ensure(pointIndex >= 0 && pointIndex <= Owner.SplineMeshComponents.Num()))
		return;

	USplineMeshComponent* splineMeshComp = Cast<USplineMeshComponent>(
		Owner.AddComponentByClass(USplineMeshComponent::StaticClass(), true, Owner.GetTransform(), false));

	Owner.SplineMeshComponents.Insert(splineMeshComp, pointIndex);

	InitMeshComponent(splineMeshComp, Owner.SplineMesh.Get());

	splineMeshComp->SetForwardAxis(ESplineMeshAxis::X, false);
	const FVector2D SplineScale(GetRibbonScale2D());
	splineMeshComp->SetStartScale(SplineScale, false);
	splineMeshComp->SetEndScale(SplineScale, false);

	splineMeshComp->SetCustomPrimitiveDataFloat(0, bSelected ? 1.0f : 0.0f);

	if (UsePathAnimationShader())
	{
		// Apply the current shader scalar parameters for path animation to the new spline mesh component.
		int32 FirstParamIndex = 0;
		const TArrayView<const float> ScalarParams = GetPathAnimScalarParams(FirstParamIndex);
		splineMeshComp->SetCustomPrimitiveDataFloatArray(FirstParamIndex, ScalarParams);

		AdaptArrowsDensityForSplineMeshComponent(pointIndex);
	}
}

void AITwinSplineHelper::FImpl::AddMeshComponentsForPoint(int32 PointIndex)
{
	BE_ASSERT(NeedsDraw3DElements());

	const int32 NbSplinePoints = Owner.GetNumberOfSplinePoints();
	if (!Owner.SplineComponent ||
		PointIndex < 0 || PointIndex >= NbSplinePoints)
		return;

	const bool bIsClosedLoop = Owner.SplineComponent->IsClosedLoop();

	// Add a spline mesh if needed
	if (Owner.bDraw3DRibbon)
	{
		if (bIsClosedLoop || PointIndex < NbSplinePoints - 1)
		{
			AddSplineMeshComponentsForPoint(PointIndex);
		}
	}

	// Add a point mesh if needed.
	if (Owner.bDraw3DPoints)
	{
		UStaticMeshComponent* PointMeshComp = Cast<UStaticMeshComponent>(
			Owner.AddComponentByClass(UStaticMeshComponent::StaticClass(), true, Owner.GetTransform(), false));

		Owner.PointMeshComponents.Insert(PointMeshComp, PointIndex);

		InitMeshComponent(PointMeshComp, Owner.PointMesh.Get());

		PointMeshComp->SetRelativeLocation(Owner.SplineComponent->GetLocationAtSplinePoint(PointIndex, SPL_LOCAL));
		PointMeshComp->SetRelativeScale3D(FVector(ScaleFactor));

		PointMeshComp->SetCustomPrimitiveDataFloat(0, bSelected ? 1.0f : 0.0f);
	}

	// Update meshes
	UpdateMeshComponentsForPoint(PointIndex);
}

void AITwinSplineHelper::FImpl::UpdateAllMeshComponents()
{
	if (!NeedsDraw3DElements())
		return;
	const int32 NbSplinePoints = Owner.GetNumberOfSplinePoints();
	for (int32 i = 0; i < NbSplinePoints; ++i)
	{
		UpdateMeshComponentsForPoint(i);
	}
}

void AITwinSplineHelper::FImpl::UpdateMeshComponentsForPoint(int32 PointIndex)
{
	BE_ASSERT(NeedsDraw3DElements());

	if (!Owner.SplineComponent ||
		PointIndex < 0 || PointIndex >= Owner.SplineComponent->GetNumberOfSplinePoints())
		return;

	USplineComponent const& SplineComp(*Owner.SplineComponent);

	if (Owner.bDraw3DRibbon && PointIndex < Owner.SplineMeshComponents.Num())
	{
		bool bLoop = LoopIndices();
		int32 StartIndex = PointIndex;
		int32 EndIndex = ITwinSpline::GetNextIndex(StartIndex, SplineComp.GetNumberOfSplinePoints(), bLoop);

		USplineMeshComponent* splineMeshComp = Owner.SplineMeshComponents[PointIndex];
		splineMeshComp->SetStartAndEnd(
			SplineComp.GetLocationAtSplinePoint(StartIndex, SPL_LOCAL),
			SplineComp.GetLeaveTangentAtSplinePoint(StartIndex, SPL_LOCAL),
			SplineComp.GetLocationAtSplinePoint(EndIndex, SPL_LOCAL),
			SplineComp.GetArriveTangentAtSplinePoint(EndIndex, SPL_LOCAL));

		if (UsePathAnimationShader())
		{
			AdaptArrowsDensityForSplineMeshComponent(PointIndex);
		}
	}
	if (Owner.bDraw3DPoints && ensure(PointIndex < Owner.PointMeshComponents.Num()))
	{
		UStaticMeshComponent* PointMeshComp = Owner.PointMeshComponents[PointIndex];
		PointMeshComp->SetRelativeLocation(
			SplineComp.GetLocationAtSplinePoint(PointIndex, SPL_LOCAL));
	}
}

void AITwinSplineHelper::FImpl::SetTangentMode(const EITwinTangentMode mode)
{
	TangentMode = mode;

	if (!Owner.SplineComponent ||
		TangentMode == EITwinTangentMode::Custom)
	{
		return;
	}

	USplineComponent& SplineComp(*Owner.SplineComponent);
	const int32 NbSplinePoints = SplineComp.GetNumberOfSplinePoints();
	for (int32 i = 0; i < NbSplinePoints; ++i)
	{
		bool isLoop = LoopIndices();
		int32 prevIndex = ITwinSpline::GetPrevIndex(i, NbSplinePoints, isLoop);
		int32 currIndex = i;
		int32 nextIndex = ITwinSpline::GetNextIndex(i, NbSplinePoints, isLoop);

		FVector prevPoint = SplineComp.GetLocationAtSplinePoint(prevIndex, SPL_LOCAL);
		FVector nextPoint = SplineComp.GetLocationAtSplinePoint(nextIndex, SPL_LOCAL);

		if (TangentMode == EITwinTangentMode::Linear)
		{
			FVector currPoint = SplineComp.GetLocationAtSplinePoint(currIndex, SPL_LOCAL);
			SplineComp.SetTangentsAtSplinePoint(
				i, currPoint - prevPoint, nextPoint - currPoint, SPL_LOCAL, false);
		}
		else if (TangentMode == EITwinTangentMode::Smooth)
		{
			SplineComp.SetTangentAtSplinePoint(
				i, (nextPoint - prevPoint) * SMOOTH_FACTOR, SPL_LOCAL, false);
		}
	}

	SplineComp.UpdateSpline();

	UpdateSplineFromUEtoAViz();

	UpdateAllMeshComponents();

	Invalidate2DElements();
}

void AITwinSplineHelper::FImpl::SetTransform(const FTransform& NewTransform, bool markSplineForSaving)
{
	Owner.IterateAllCartographicPolygons([&](ACesiumCartographicPolygon& Polygon)
	{
		Polygon.SetActorTransform(NewTransform);
	});
	OnSplineModified();

	if (Spline)
	{
		auto spline = Spline->GetAutoLock();
		spline->SetTransform(FITwinMathConversion::UEtoSDK(NewTransform));
		if (markSplineForSaving)
		{
			spline->SetShouldSave(true);
		}
	}
}

FVector AITwinSplineHelper::FImpl::GetLocationAtSplinePoint(int32 pointIndex) const
{
	if (Owner.SplineComponent)
	{
		return Owner.SplineComponent->GetLocationAtSplinePoint(pointIndex, SPL_WORLD);
	}
	return FVector(0);
}

void AITwinSplineHelper::FImpl::SetLocationAtSplinePoint(int32 pointIndex, const FVector& location)
{
	if (!Owner.SplineComponent)
	{
		return;
	}

	USplineComponent& SplineComp(*Owner.SplineComponent);
	SplineComp.SetLocationAtSplinePoint(pointIndex, location, SPL_WORLD);

	// Use the local position for the next calculations
	FVector pos = SplineComp.GetLocationAtSplinePoint(pointIndex, SPL_LOCAL);

	bool isLoop = LoopIndices();
	int32 numPoints = SplineComp.GetNumberOfSplinePoints();
	int32 prevPointIndex = ITwinSpline::GetPrevIndex(pointIndex, numPoints, isLoop);
	int32 nextPointIndex = ITwinSpline::GetNextIndex(pointIndex, numPoints, isLoop);

	FVector prevPos = SplineComp.GetLocationAtSplinePoint(prevPointIndex, SPL_LOCAL);
	FVector nextPos = SplineComp.GetLocationAtSplinePoint(nextPointIndex, SPL_LOCAL);

	// Update tangents
	if (TangentMode == EITwinTangentMode::Linear)
	{
		FVector arriveTangent, leaveTangent;
		arriveTangent = SplineComp.GetArriveTangentAtSplinePoint(prevPointIndex, SPL_LOCAL);
		leaveTangent = (pos - prevPos);
		SplineComp.SetTangentsAtSplinePoint(
			prevPointIndex, arriveTangent, leaveTangent, SPL_LOCAL, false);

		arriveTangent = leaveTangent;
		leaveTangent = (nextPos - pos);
		SplineComp.SetTangentsAtSplinePoint(
			pointIndex, arriveTangent, leaveTangent, SPL_LOCAL, false);

		arriveTangent = leaveTangent;
		leaveTangent = SplineComp.GetLeaveTangentAtSplinePoint(nextPointIndex, SPL_LOCAL);
		SplineComp.SetTangentsAtSplinePoint(
			nextPointIndex, arriveTangent, leaveTangent, SPL_LOCAL, false);
	}
	else if (TangentMode == EITwinTangentMode::Smooth)
	{
		FVector prevPrevPos = SplineComp.GetLocationAtSplinePoint(
			ITwinSpline::GetPrevIndex(prevPointIndex, numPoints, isLoop), SPL_LOCAL);
		FVector nextNextPos = SplineComp.GetLocationAtSplinePoint(
			ITwinSpline::GetNextIndex(nextPointIndex, numPoints, isLoop), SPL_LOCAL);

		if (prevPointIndex != pointIndex)// && SplineComp.GetSplinePointType(prevPointIndex) != ESplinePointType::CurveCustomTangent)
			UpdateTangentAuto(prevPointIndex, prevPos, prevPrevPos, pos);
		UpdateTangentAuto(pointIndex, pos, prevPos, nextPos);
		if (nextPointIndex != pointIndex)// && SplineComp.GetSplinePointType(nextPointIndex) != ESplinePointType::CurveCustomTangent)
			UpdateTangentAuto(nextPointIndex, nextPos, pos, nextNextPos);
	}

	SplineComp.UpdateSpline();

	// Update the AdvViz::SDK spline (for the saving of points)
	if (Spline)
	{
		UpdatePointFromUEtoAViz(prevPointIndex);
		UpdatePointFromUEtoAViz(pointIndex);
		UpdatePointFromUEtoAViz(nextPointIndex);
	}

	// Update meshes
	if (NeedsDraw3DElements())
	{
		UpdateMeshComponentsForPoint(pointIndex);
		UpdateMeshComponentsForPoint(prevPointIndex);

		if (TangentMode == EITwinTangentMode::Smooth)
		{
			UpdateMeshComponentsForPoint(ITwinSpline::GetNextIndex(pointIndex, numPoints, isLoop));
			UpdateMeshComponentsForPoint(ITwinSpline::GetPrevIndex(prevPointIndex, numPoints, isLoop));
		}
	}

	CopyPointToSecondaryCartographicPolygons(pointIndex, prevPointIndex, nextPointIndex);

	OnSplineModified();
}

void AITwinSplineHelper::FImpl::UpdateTangentAuto(int32 pointIndex, const FVector& pos, const FVector& prevPos, const FVector& nextPos)
{
	UpdateTangent(pointIndex, (nextPos - prevPos), FMath::Min((pos - prevPos).Length(), (nextPos - pos).Length()));
}

void AITwinSplineHelper::FImpl::UpdateTangentDir(int32 pointIndex, const FVector& newTangentDir)
{
	USplineComponent& SplineComp(*Owner.SplineComponent);

	FVector tangent = SplineComp.GetTangentAtSplinePoint(pointIndex, SPL_LOCAL);
	float TargetTangentLength = FMath::Max(tangent.Length(), SMALL_NUMBER);
	UpdateTangent(pointIndex, newTangentDir, TargetTangentLength);
}

void AITwinSplineHelper::FImpl::UpdateTangent(int32 pointIndex, const FVector & newTangentDir, float TargetTangentLength)
{
	USplineComponent& SplineComp(*Owner.SplineComponent);

	FVector tangent = newTangentDir;
	float TangentLength = FMath::Max(tangent.Length(), SMALL_NUMBER);
	tangent *= TargetTangentLength / TangentLength;
	SplineComp.SetTangentAtSplinePoint(
		pointIndex, tangent, SPL_LOCAL, false);
}

bool AITwinSplineHelper::FImpl::IncludeInWorldBox(FBox& Box) const
{
	if (!Owner.SplineComponent)
	{
		return false;
	}
	USplineComponent const& SplineComp(*Owner.SplineComponent);
	int32 const NbPoints = SplineComp.GetNumberOfSplinePoints();
	for (int32 i = 0; i < NbPoints; ++i)
	{
		Box += SplineComp.GetLocationAtSplinePoint(i, SPL_WORLD);
	}
	return NbPoints > 0;
}

void AITwinSplineHelper::FImpl::RemoveSplineMeshComponentForPoint(int32 PointIndex)
{
	BE_ASSERT(Owner.bDraw3DRibbon);

	// Remove the spline mesh attached to the point.
	if (PointIndex < Owner.SplineMeshComponents.Num()
		&& Owner.SplineMeshComponents[PointIndex])
	{
		Owner.SplineMeshComponents[PointIndex]->UnregisterComponent();
		Owner.SplineMeshComponents[PointIndex]->DestroyComponent();
		Owner.SplineMeshComponents.RemoveAt(PointIndex);
	}
}

bool AITwinSplineHelper::FImpl::DeletePoint(int32 pointIndex)
{
	if (!Owner.SplineComponent)
	{
		return false;
	}
	CHECK_NUMBER_OF_SPLINE_MESH_COMPONENTS();

	USplineComponent& SplineComp(*Owner.SplineComponent);

	bool bDeleted = false;

	// Set the new tangents before deleting the point
	bool isLoop = LoopIndices();
	int32 numPoints = SplineComp.GetNumberOfSplinePoints();
	const bool bIsLastPoint = (pointIndex == numPoints - 1);
	int32 prevPointIndex = ITwinSpline::GetPrevIndex(pointIndex, numPoints, isLoop);
	int32 nextPointIndex = ITwinSpline::GetNextIndex(pointIndex, numPoints, isLoop);
	FVector prevPos = SplineComp.GetLocationAtSplinePoint(prevPointIndex, SPL_LOCAL);
	FVector nextPos = SplineComp.GetLocationAtSplinePoint(nextPointIndex, SPL_LOCAL);

	if (TangentMode == EITwinTangentMode::Linear)
	{
		SplineComp.SetTangentsAtSplinePoint(prevPointIndex,
			SplineComp.GetArriveTangentAtSplinePoint(prevPointIndex, SPL_LOCAL),
			nextPos - prevPos,
			SPL_LOCAL, false);

		SplineComp.SetTangentsAtSplinePoint(nextPointIndex,
			nextPos - prevPos,
			SplineComp.GetLeaveTangentAtSplinePoint(nextPointIndex, SPL_LOCAL),
			SPL_LOCAL, false);
	}
	else if (TangentMode == EITwinTangentMode::Smooth)
	{
		FVector prevPrevPos = SplineComp.GetLocationAtSplinePoint(
			ITwinSpline::GetPrevIndex(prevPointIndex, numPoints, isLoop), SPL_LOCAL);
		FVector nextNextPos = SplineComp.GetLocationAtSplinePoint(
			ITwinSpline::GetNextIndex(nextPointIndex, numPoints, isLoop), SPL_LOCAL);

		if (nextPointIndex == pointIndex) // deleting last point of a non-closed spline -> do not take into account the position of the point being deleted
			SplineComp.SetTangentAtSplinePoint(
				prevPointIndex, (prevPos - prevPrevPos) * SMOOTH_FACTOR, SPL_LOCAL, false);
		else 
			SplineComp.SetTangentAtSplinePoint(
				prevPointIndex, (nextPos - prevPrevPos) * SMOOTH_FACTOR, SPL_LOCAL, false);

		if (prevPointIndex == pointIndex) // deleting first point of a non-closed spline -> do not take into account the position of the point being deleted
			SplineComp.SetTangentAtSplinePoint(
				nextPointIndex, (nextNextPos - nextPos) * SMOOTH_FACTOR, SPL_LOCAL, false);
		else
			SplineComp.SetTangentAtSplinePoint(
				nextPointIndex, (nextNextPos - prevPos) * SMOOTH_FACTOR, SPL_LOCAL, false);
	}
	// Update tangents in the secondary polygons
	CopyPointToSecondaryCartographicPolygons(pointIndex, prevPointIndex, nextPointIndex);

	if (Spline)
	{
		auto spline = Spline->GetAutoLock();
		if (pointIndex < spline->GetNumberOfPoints())
		{
			UpdatePointFromUEtoAViz(prevPointIndex);
			UpdatePointFromUEtoAViz(nextPointIndex);
		}
	}

	// Remove the spline point
	if (pointIndex < SplineComp.GetNumberOfSplinePoints())
	{
		ForEachUESplineComponent([pointIndex](USplineComponent& SplineComponent)
		{
			SplineComponent.RemoveSplinePoint(pointIndex);
		});
		if (Spline)
		{
			auto spline = Spline->GetAutoLock();
			if (pointIndex < spline->GetNumberOfPoints())
			{
				spline->RemovePoint(static_cast<size_t>(pointIndex));
			}
		}
		if (pointIndex == SelectedPointIndex)
		{
			SelectedPointIndex = -1;
		}
		CHECK_NUMBER_OF_POINTS();

		bDeleted = true;
	}

	if (bDeleted && NeedsDraw3DElements())
	{
		// Remove the meshes representing the point

		if (Owner.bDraw3DRibbon)
		{
			int32 SplineMeshIndex = pointIndex;
			if (bIsLastPoint && !Owner.IsClosedLoop())
			{
				SplineMeshIndex = pointIndex - 1;
			}
			RemoveSplineMeshComponentForPoint(SplineMeshIndex);
		}
		CHECK_NUMBER_OF_SPLINE_MESH_COMPONENTS();

		if (Owner.bDraw3DPoints
			&& pointIndex < Owner.PointMeshComponents.Num()
			&& Owner.PointMeshComponents[pointIndex])
		{
			Owner.PointMeshComponents[pointIndex]->UnregisterComponent();
			Owner.PointMeshComponents[pointIndex]->DestroyComponent();
			Owner.PointMeshComponents.RemoveAt(pointIndex);
		}

		// Update the meshes of the previous point to fill the gap
		numPoints = SplineComp.GetNumberOfSplinePoints();
		prevPointIndex = ITwinSpline::GetPrevIndex(pointIndex, numPoints, isLoop);
		UpdateMeshComponentsForPoint(prevPointIndex);

		if (TangentMode == EITwinTangentMode::Smooth)
		{
			UpdateMeshComponentsForPoint(ITwinSpline::GetNextIndex(prevPointIndex, numPoints, isLoop));
			UpdateMeshComponentsForPoint(ITwinSpline::GetPrevIndex(prevPointIndex, numPoints, isLoop));
		}
	}

	OnSplineModified();

	return bDeleted;
}

bool AITwinSplineHelper::FImpl::DuplicatePoint(int32 pointIndex)
{
	using namespace AdvViz::SDK;

	if (!Owner.SplineComponent || pointIndex < 0)
	{
		return false;
	}

	USplineComponent& SplineComp(*Owner.SplineComponent);

	FVector pointPos = SplineComp.GetLocationAtSplinePoint(pointIndex, SPL_LOCAL);
	FVector arriveTangent = SplineComp.GetArriveTangentAtSplinePoint(pointIndex, SPL_LOCAL);
	FVector leaveTangent = SplineComp.GetLeaveTangentAtSplinePoint(pointIndex, SPL_LOCAL);
	SplineComp.AddSplinePointAtIndex(pointPos, pointIndex, SPL_LOCAL, false);
	SplineComp.SetTangentsAtSplinePoint(pointIndex, arriveTangent, FVector(0), SPL_LOCAL, false);
	SplineComp.SetTangentsAtSplinePoint(pointIndex + 1, FVector(0), leaveTangent, SPL_LOCAL, false);
	SplineComp.UpdateSpline();

	if (NeedsDraw3DElements())
	{
		AddMeshComponentsForPoint(pointIndex);
	}

	CHECK_NUMBER_OF_SPLINE_MESH_COMPONENTS();

	InsertPointInSecondaryCartographicPolygons(pointIndex);
	CopyPointToSecondaryCartographicPolygons(pointIndex, -1, pointIndex + 1);

	// Add the same point in the SDK Core spline.
	if (Spline)
	{
		{
			auto spline = Spline->GetAutoLock();
			spline->InsertPoint(static_cast<size_t>(pointIndex));
		}
		UpdatePointFromUEtoAViz(pointIndex);
		UpdatePointFromUEtoAViz(pointIndex + 1);
	}
	CHECK_NUMBER_OF_POINTS();

	Invalidate2DElements();
	return true;
}

bool AITwinSplineHelper::FImpl::DuplicatePoint(int32& pointIndex, FVector& newWorldPosition)
{
	if (!Owner.SplineComponent)
	{
		return false;
	}

	bool isLoop = LoopIndices();
	USplineComponent& SplineComp(*Owner.SplineComponent);
	int32 numPoints = SplineComp.GetNumberOfSplinePoints();
	int32 prevPointIndex = ITwinSpline::GetPrevIndex(pointIndex, numPoints, isLoop);
	int32 nextPointIndex = ITwinSpline::GetNextIndex(pointIndex, numPoints, isLoop);
	FVector prevPos = SplineComp.GetLocationAtSplinePoint(prevPointIndex, SPL_WORLD);
	FVector currPos = SplineComp.GetLocationAtSplinePoint(pointIndex, SPL_WORLD);
	FVector nextPos = SplineComp.GetLocationAtSplinePoint(nextPointIndex, SPL_WORLD);

	if (!DuplicatePoint(pointIndex))
	{
		return false;
	}

	if (ITwinSpline::ShouldAdvanceIndexAfterDuplication(prevPos, currPos, nextPos, newWorldPosition))
	{
		pointIndex++;
	}
	return true;
}

int32 AITwinSplineHelper::FImpl::InsertPointAt(const int32 PointIndex, FVector const& NewWorldPosition)
{
	if (!Owner.SplineComponent)
	{
		return INDEX_NONE;
	}
	USplineComponent& SplineComp(*Owner.SplineComponent);
	int32 NumPoints = SplineComp.GetNumberOfSplinePoints();

	if (!ensure(ITwinSpline::IsValidInsertionIndex(PointIndex, NumPoints)))
	{
		return INDEX_NONE;
	}
	// To avoid code duplication, let's duplicate a point and move it at once.
	DuplicatePoint(ITwinSpline::ResolveDuplicationSourceIndex(PointIndex, NumPoints));

	if (ensure(PointIndex < SplineComp.GetNumberOfSplinePoints()))
	{
		SetLocationAtSplinePoint(PointIndex, NewWorldPosition);
		return PointIndex;
	}
	else
	{
		return INDEX_NONE;
	}
}

void AITwinSplineHelper::FImpl::ScaleMeshComponentsForCurrentPOV()
{
	// Scale meshes depending on distance to the camera
	APlayerController const* pController = Owner.GetWorld()->GetFirstPlayerController();
	if (!pController || !pController->PlayerCameraManager)
	{
		return;
	}
	bool const bOrtho = pController->PlayerCameraManager->IsOrthographic();
	if (bOrtho)
	{
		// TODO_JDE - orthographic camera
		BE_ISSUE("Spline mesh components automatic scaling not implemented in orthographic view.");
		return;
	}
	FVector const CameraPos = pController->PlayerCameraManager->GetCameraLocation();
	double const FOVRad = FMath::DegreesToRadians(pController->PlayerCameraManager->GetFOVAngle());
	double const SinFOV = FMath::Sin(FOVRad);
	double MinScreenPercentage = 1.0;
	for (auto const& PointMeshComp : Owner.PointMeshComponents)
	{
		if (PointMeshComp)
		{
			auto const Dist = (PointMeshComp->Bounds.Origin - CameraPos).Length();
			double const EvalScreenPercentage = PointMeshComp->Bounds.SphereRadius / (Dist * SinFOV);
			MinScreenPercentage = std::min(MinScreenPercentage, EvalScreenPercentage);
		}
	}
	double const DesiredPercentage = (IsEdgeDisplayHelper() ? 0.25 : 1.0) * 0.01;
	double const dMult = DesiredPercentage / MinScreenPercentage;
	if (std::fabs(1.0 - dMult) < 0.05)
		return;

	ScaleFactor *= dMult;

	const FVector NewScale3D = FVector(ScaleFactor);
	const FVector2D NewSplineScale = GetRibbonScale2D();

	// Scale all components to reach the desired size (approximatively).
	if (AutoScalePoints())
	{
		for (auto const& PointMeshComp : Owner.PointMeshComponents)
		{
			if (PointMeshComp)
			{
				PointMeshComp->SetRelativeScale3D(NewScale3D);
			}
		}
	}
	if (AutoScaleSplineRibbon())
	{
		for (auto const& SplineMeshComp : Owner.SplineMeshComponents)
		{
			if (SplineMeshComp)
			{
				SplineMeshComp->SetStartScale(NewSplineScale, true);
				SplineMeshComp->SetEndScale(NewSplineScale, true);
			}
		}
	}
}

void AITwinSplineHelper::FImpl::ComputeSplineScaleFromWidth(float Width)
{
	if (Owner.SplineMeshComponents.Num() == 0)
		return;

	FVector Origin;
	FVector BoxExtent;
	Owner.SplineMeshComponents[0]->GetLocalBounds(Origin, BoxExtent);
	auto MeshWidth = BoxExtent.Y * 2.0f; // Get the width of the mesh in Unreal units
	FixedSplineScale = Width / MeshWidth;

	const FVector2D NewSplineScale = FVector2D(GetRibbonScale(), 1.);
	for (auto const& SplineMeshComp : Owner.SplineMeshComponents)
	{
		if (SplineMeshComp)
		{
			SplineMeshComp->SetStartScale(NewSplineScale, true);
			SplineMeshComp->SetEndScale(NewSplineScale, true);
		}
	}
	bHasComputedScale = true;
}

void AITwinSplineHelper::FImpl::SetClosedLoop(bool bInClosedLoop, bool bUpdateSpline)
{
	if (!Owner.SplineComponent)
		return;

	CHECK_NUMBER_OF_SPLINE_MESH_COMPONENTS();

	const bool bPropertyChanged = (bInClosedLoop != Owner.SplineComponent->IsClosedLoop());
	Owner.SplineComponent->SetClosedLoop(bInClosedLoop, bUpdateSpline);

	if (bPropertyChanged)
	{
		// Add or remove the last segment, depending on the new closed loop state
		if (Owner.bDraw3DRibbon)
		{
			const int32 NbSplinePoints = Owner.GetNumberOfSplinePoints();
			if (bInClosedLoop && ensure(Owner.SplineMeshComponents.Num() == NbSplinePoints - 1))
			{
				AddSplineMeshComponentsForPoint(NbSplinePoints - 1);
				UpdateMeshComponentsForPoint(NbSplinePoints - 1);
			}
			else if (!bInClosedLoop && ensure(Owner.SplineMeshComponents.Num() == NbSplinePoints))
			{
				RemoveSplineMeshComponentForPoint(NbSplinePoints - 1);
			}
		}
		if (Spline)
		{
			auto spline = Spline->GetAutoLock();
			spline->SetClosedLoop(bInClosedLoop);
		}
		Invalidate2DElements();
	}
	CHECK_NUMBER_OF_SPLINE_MESH_COMPONENTS();
}

bool AITwinSplineHelper::FImpl::LoopIndices() const
{
	return Owner.SplineComponent->IsClosedLoop();
}


void AITwinSplineHelper::FImpl::CopyPointToSecondaryCartographicPolygons(int32 PointIndex, int32 PrevPointIndex /*= -1*/, int32 NextPointIndex /*= -1*/)
{
	if (!Owner.SplineComponent)
		return;
	USplineComponent const& SrcSplineComponent(*Owner.SplineComponent);
	if (!ensure(PointIndex < SrcSplineComponent.GetNumberOfSplinePoints()))
		return;
	if (PrevPointIndex >= 0 && !ensure(PrevPointIndex < SrcSplineComponent.GetNumberOfSplinePoints()))
		PrevPointIndex = -1;
	if (NextPointIndex >= 0 && !ensure(NextPointIndex < SrcSplineComponent.GetNumberOfSplinePoints()))
		NextPointIndex = -1;
	for (auto& [_, PolygonPtr] : Owner.PerGeorefPolygonMap)
	{
		ACesiumCartographicPolygon* Polygon = PolygonPtr.Get();
		if (Polygon && Polygon->Polygon != Owner.SplineComponent
			&& ensure(Polygon->Polygon))
		{
			USplineComponent& DstSplineComponent(*Polygon->Polygon);
			if (ensure(PointIndex < DstSplineComponent.GetNumberOfSplinePoints()))
			{
				DstSplineComponent.SetLocationAtSplinePoint(PointIndex,
					SrcSplineComponent.GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World),
					ESplineCoordinateSpace::World);
				DstSplineComponent.SetTangentsAtSplinePoint(PointIndex,
					SrcSplineComponent.GetArriveTangentAtSplinePoint(PointIndex, ESplineCoordinateSpace::World),
					SrcSplineComponent.GetLeaveTangentAtSplinePoint(PointIndex, ESplineCoordinateSpace::World),
					ESplineCoordinateSpace::World);
			}
			if (PrevPointIndex >= 0 &&
				ensure(PrevPointIndex < DstSplineComponent.GetNumberOfSplinePoints()))
			{
				DstSplineComponent.SetLocationAtSplinePoint(PrevPointIndex,
					SrcSplineComponent.GetLocationAtSplinePoint(PrevPointIndex, ESplineCoordinateSpace::World),
					ESplineCoordinateSpace::World);
				DstSplineComponent.SetTangentsAtSplinePoint(PrevPointIndex,
					SrcSplineComponent.GetArriveTangentAtSplinePoint(PrevPointIndex, ESplineCoordinateSpace::World),
					SrcSplineComponent.GetLeaveTangentAtSplinePoint(PrevPointIndex, ESplineCoordinateSpace::World),
					ESplineCoordinateSpace::World);
			}
			if (NextPointIndex >= 0 &&
				ensure(NextPointIndex < DstSplineComponent.GetNumberOfSplinePoints()))
			{
				DstSplineComponent.SetLocationAtSplinePoint(NextPointIndex,
					SrcSplineComponent.GetLocationAtSplinePoint(NextPointIndex, ESplineCoordinateSpace::World),
					ESplineCoordinateSpace::World);
				DstSplineComponent.SetTangentsAtSplinePoint(NextPointIndex,
					SrcSplineComponent.GetArriveTangentAtSplinePoint(NextPointIndex, ESplineCoordinateSpace::World),
					SrcSplineComponent.GetLeaveTangentAtSplinePoint(NextPointIndex, ESplineCoordinateSpace::World),
					ESplineCoordinateSpace::World);
			}
		}
	}
}

void AITwinSplineHelper::FImpl::InsertPointInSecondaryCartographicPolygons(int32 PointIndex)
{
	for (auto& [_, PolygonPtr] : Owner.PerGeorefPolygonMap)
	{
		ACesiumCartographicPolygon* Polygon = PolygonPtr.Get();
		if (Polygon && Polygon->Polygon != Owner.SplineComponent
			&& ensure(Polygon->Polygon))
		{
			USplineComponent& DstSplineComponent(*Polygon->Polygon);
			if (ensure(PointIndex < DstSplineComponent.GetNumberOfSplinePoints()))
			{
				DstSplineComponent.AddSplinePointAtIndex(FVector::ZeroVector, PointIndex,
					SPL_LOCAL, false);
			}
		}
	}
}

void AITwinSplineHelper::FImpl::UpdateTracingData() const
{
	TArray<FVector> SplinePts;
	const int32 NumPoints = Owner.GetNumberOfSplinePoints();
	SplinePts.SetNum(NumPoints);
	for (int32 i(0); i < NumPoints; ++i)
	{
		SplinePts[i] = GetLocationAtSplinePoint(i);
	}
	ITwinSpline::BuildSplinePolygon(SplinePts, TracingData.SplinePolygon, TracingData.SplineBarycenter);
	TracingData.bNeedUpdateTracingData = false;
}

bool AITwinSplineHelper::FImpl::DoesLineIntersectSplinePolygon(const FVector& Start, const FVector& End) const
{
	if (TracingData.bNeedUpdateTracingData)
	{
		UpdateTracingData();
	}
	return ITwinSpline::DoesLineIntersectPolygon(TracingData.SplinePolygon, Start, End);
}

void AITwinSplineHelper::FImpl::SetSelected(bool bInSelected)
{
	if (bSelected == bInSelected)
		return;

	// Don't keep a point selected if the spline is globally deselected.
	if (!bInSelected)
	{
		SetSelectedPointIndex(-1);
	}

	bSelected = bInSelected;
	const float fSelectionValue = bSelected ? 1.0f : 0.0f;
	for (auto const& SplineMeshComp : Owner.SplineMeshComponents)
	{
		if (SplineMeshComp)
		{
			SplineMeshComp->SetCustomPrimitiveDataFloat(0, fSelectionValue);
		}
	}
	for (auto const& PointMeshComp : Owner.PointMeshComponents)
	{
		if (PointMeshComp)
		{
			PointMeshComp->SetCustomPrimitiveDataFloat(0, fSelectionValue);
		}
	}
	// Change color of 2D widget
	if (Owner.OnScreen2DWidget)
	{
		Owner.OnScreen2DWidget->SetTint(bSelected ? FLinearColor(0.374, 1., 0.701) : FLinearColor::White);
	}
}

void AITwinSplineHelper::FImpl::SetSelectedPointIndex(int32 PointIndex)
{
	if (this->SelectedPointIndex == PointIndex)
		return;
	if (this->SelectedPointIndex >= 0
		&& Owner.bDraw3DPoints
		&& ensure(this->SelectedPointIndex < Owner.PointMeshComponents.Num()))
	{
		Owner.PointMeshComponents[this->SelectedPointIndex]->SetCustomPrimitiveDataFloat(1, 0.0f);
	}
	this->SelectedPointIndex = PointIndex;
	if (this->SelectedPointIndex >= 0
		&& Owner.bDraw3DPoints
		&& ensure(this->SelectedPointIndex < Owner.PointMeshComponents.Num()))
	{
		Owner.PointMeshComponents[this->SelectedPointIndex]->SetCustomPrimitiveDataFloat(1, 1.0f);
	}
}

void AITwinSplineHelper::FImpl::SetInteractiveCreationInProgress(bool bInProgress)
{
	if (this->bInteractiveCreationInProgress != bInProgress)
	{
		this->bInteractiveCreationInProgress = bInProgress;
		// Invalidate 2D elements, as some buttons are disabled during interactive creation.
		Invalidate2DElements();
	}
}

void AITwinSplineHelper::FImpl::SetPathAnimShaderScalarParameterValue(EITwinAnimPathShaderScalarParam Param, float Value,
	bool bApplyToMeshComponents)
{
	BE_ASSERT(ITwinSpline::IsPathAnim(Usage));
	if (!UsePathAnimationShader())
	{
		// Skip setting the parameter if the shader is not used, to avoid unnecessary updates.
		return;
	}

	BE_ASSERT(Param != EITwinAnimPathShaderScalarParam::Selection, "selection is not managed here");

	const int32 ParamIndex = static_cast<int32>(Param);

	if (ParamIndex >= (int32)AnimPathShaderScalarParams.size())
	{
		BE_ISSUE("out of range", ParamIndex, EITwinAnimPathShaderScalarParam::Count);
		return;
	}

	const bool bHasChanged = AnimPathShaderScalarParams[ParamIndex] != Value;
	AnimPathShaderScalarParams[ParamIndex] = Value;

	// Apply it to existing spline mesh components.
	if (bHasChanged && bApplyToMeshComponents)
	{
		for (auto const& SplineMeshComp : Owner.SplineMeshComponents)
		{
			if (SplineMeshComp)
			{
				SplineMeshComp->SetCustomPrimitiveDataFloat(ParamIndex, Value);
			}
		}
	}
}

void AITwinSplineHelper::FImpl::TransferPathAnimShaderParametersToMeshes()
{
	if (!UsePathAnimationShader())
	{
		// Skip setting the parameter if the shader is not used, to avoid unnecessary updates.
		return;
	}
	int32 FirstParamIndex = 0;
	const TArrayView<const float> ScalarParams = GetPathAnimScalarParams(FirstParamIndex);
	for (auto const& SplineMeshComp : Owner.SplineMeshComponents)
	{
		if (SplineMeshComp)
		{
			SplineMeshComp->SetCustomPrimitiveDataFloatArray(FirstParamIndex, ScalarParams);
		}
	}
}

/*static*/ bool AITwinSplineHelper::Is2DDrawingEnabled()
{
	return true;
}

/*static*/ AITwinSplineHelper* AITwinSplineHelper::FindClosestSplineToScreenPosition(const FVector2D& ScreenPosition,
	FVector::FReal& OutClosestDistance,
	const TFunction<bool(const AITwinSplineHelper&)>& IgnoreSpline)
{
	BE_ASSERT(Is2DDrawingEnabled());
	return UITwinSplineHelper2DWidgetImpl::FindClosestSplineToScreenPosition(
		ScreenPosition, OutClosestDistance, IgnoreSpline);
}

AITwinSplineHelper::FSpawnContext::FSpawnContext(EITwinSplineUsage SplineUsage)
{
	ensureMsgf(!FImpl::UsageForSpawnedActor, TEXT("do not nest AITwinSplineHelper construction"));
	FImpl::UsageForSpawnedActor = SplineUsage;
}

AITwinSplineHelper::FSpawnContext::~FSpawnContext()
{
	FImpl::UsageForSpawnedActor.reset();
}


AITwinSplineHelper::AITwinSplineHelper()
	: AActor(), Impl(MakePimpl<FImpl>(*this))
{
	// For cutout polygon, we'll use the spline component of the cartographic polygon ; in all other cases, we
	// use a standalone spline component:
	EITwinSplineUsage SplineUsage = FImpl::UsageForSpawnedActor.value_or(EITwinSplineUsage::Undefined);
	Impl->Usage = SplineUsage;
	if (SplineUsage != EITwinSplineUsage::MapCutout)
	{
		this->SplineComponent = CreateDefaultSubobject<USplineComponent>(
			FName(*UEnum::GetDisplayValueAsText(SplineUsage).ToString()));
		this->SplineComponent->SetClosedLoop(SplineUsage == EITwinSplineUsage::PopulationZone
			|| SplineUsage == EITwinSplineUsage::EdgeDisplayHelper);
		this->SplineComponent->SetMobility(EComponentMobility::Movable);
		// Create just one point located at reference position
		this->SplineComponent->SetSplinePoints(
			TArray<FVector>{
			FVector(0.0f, 0.0f, 0.0f)},
			ESplineCoordinateSpace::Local);
		SetRootComponent(this->SplineComponent);
	}
	else
	{
		SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("root")));
	}

	// Configure drawing settings according to the usage of the spline helper.
	SetDraw3DPoints(false); // legacy mode, not relevant anymore.
	SetDraw2DElements(true);
	BE_ASSERT(Is2DDrawingEnabled());
	// Display 3D ribbon if it makes sense.
	SetDraw3DRibbon(SplineUsage == EITwinSplineUsage::AnimPathTraffic
		|| SplineUsage == EITwinSplineUsage::AnimPathCrowd
		|| SplineUsage == EITwinSplineUsage::PopulationPath);

	GetRootComponent()->SetMobility(EComponentMobility::Movable); // needed for the anchor

	GlobeAnchor = CreateDefaultSubobject<UCesiumGlobeAnchorComponent>(TEXT("GlobeAnchor"));

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		// Create widget for screen-space rendering.
		const FString WidgetPath =
			TEXT("/Script/UMGEditor.WidgetBlueprint'/ITwinForUnreal/ITwin/Splines/ITwinSplineHelper2DWidget.ITwinSplineHelper2DWidget_C'");

		if (!UITwinSplineHelper2DWidgetImpl::GetMasterInstance())
		{
			// Create a master instance of the 2D widget to be used for all spline helpers.
			UITwinSplineHelper2DWidgetImpl* MasterScreen2DWidget =
				CreateWidget<UITwinSplineHelper2DWidgetImpl>(GetWorld(), LoadClass<UITwinSplineHelper2DWidgetImpl>(
					nullptr,
					*WidgetPath));
			if (ensure(MasterScreen2DWidget))
			{
				MasterScreen2DWidget->AddToViewport(10);
				MasterScreen2DWidget->SetVisibility(ESlateVisibility::Hidden);
				UITwinSplineHelper2DWidgetImpl::SetMasterInstance(MasterScreen2DWidget);
			}
		}

		OnScreen2DWidget = CreateWidget<UITwinSplineHelper2DWidgetImpl>(GetWorld(), LoadClass<UITwinSplineHelper2DWidgetImpl>(
			nullptr,
			*WidgetPath));
		if (ensure(OnScreen2DWidget))
		{
			OnScreen2DWidget->SetSplineHelper(this);
			OnScreen2DWidget->SetVisibility(ESlateVisibility::Hidden);
		}
	}

	PrimaryActorTick.bCanEverTick = true;
}

void AITwinSplineHelper::BeginPlay()
{
	Super::BeginPlay();

	SetTickGroup(ETickingGroup::TG_PostUpdateWork);

	// Show the 2D widget after everything is initialized
	if (OnScreen2DWidget && bDraw2DElements)
	{
		if (Impl->IsEdgeDisplayHelper())
		{
			// Edge only mode.
			OnScreen2DWidget->SetShowPins(false);
		}

		// Only one instance, the master, is actually added to the viewport. The slave instances are used to
		// set the correct position of the spline helper in screen space.
		BE_ASSERT(UITwinSplineHelper2DWidgetImpl::GetMasterInstance() != nullptr);
		UITwinSplineHelper2DWidgetImpl::RegisterSlaveWidget(OnScreen2DWidget);

		// Make sure the chunk widgets are initially hidden.
		OnScreen2DWidget->OnVisibilityUpdated();
	}

#if WITH_EDITOR
	if (!Impl->CustomActorLabel.IsEmpty())
	{
		SetActorLabel(Impl->CustomActorLabel);
	}
#endif
}

void AITwinSplineHelper::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (OnScreen2DWidget)
	{
		UITwinSplineHelper2DWidgetImpl::UnregisterSlaveWidget(OnScreen2DWidget);

		OnScreen2DWidget->RemoveFromParent();
	}
	Super::EndPlay(EndPlayReason);
}


AdvViz::SDK::ISplinePtr AITwinSplineHelper::GetAVizSpline() const
{
	return Impl->Spline;
}

void AITwinSplineHelper::SetAVizSpline(AdvViz::SDK::ISplinePtr const& Spline)
{
	if (Impl->Spline != Spline)
	{
		Impl->Spline = Spline;
		Impl->UpdateSplineFromAVizToUE();
		Impl->RecreateAllMeshComponents();
	}
}

AdvViz::SDK::RefID AITwinSplineHelper::GetAVizSplineId() const
{
	if (Impl->Spline)
	{
		auto spline = Impl->Spline->GetRAutoLock();
		return spline->GetId();
	}
	else
		return AdvViz::SDK::RefID::Invalid();
}

namespace ITwin {
	std::set<ModelLink> GetSplineModelLinks(AdvViz::SDK::ISplinePtr const& Spline);
}

std::set<ITwin::ModelLink> AITwinSplineHelper::GetLinkedModels() const
{
	if (Impl->Spline)
	{
		auto spline = Impl->Spline->GetRAutoLock();
		if (!spline->GetLinkedModels().empty())
		{
			return ITwin::GetSplineModelLinks(Impl->Spline);
		}
	}
	return {};
}

int32 AITwinSplineHelper::GetNumberOfSplinePoints() const
{
	return SplineComponent ? SplineComponent->GetNumberOfSplinePoints() : 0;
}

bool AITwinSplineHelper::IsClosedLoop() const
{
	return SplineComponent && SplineComponent->IsClosedLoop();
}

void AITwinSplineHelper::SetClosedLoop(bool bInClosedLoop, bool bUpdateSpline /*= true*/)
{
	Impl->SetClosedLoop(bInClosedLoop, bUpdateSpline);
}

void AITwinSplineHelper::Initialize(USplineComponent* splineComp, AdvViz::SDK::ISplinePtr spline)
{
	Impl->Initialize(splineComp, spline);
}

void AITwinSplineHelper::SetCustomActorLabel(const FString& InCustomActorLabel)
{
#if WITH_EDITOR
	if (HasActorBegunPlay())
	{
		SetActorLabel(InCustomActorLabel);
	}
	else
	{
		// Avoid setting the name too soon, which does break the root component of the actor.
		// (at least in unit test context).
		Impl->CustomActorLabel = InCustomActorLabel;
	}
#endif // WITH_EDITOR
}

bool AITwinSplineHelper::IsInteractiveCreationInProgress() const
{
	return Impl->bInteractiveCreationInProgress;
}

void AITwinSplineHelper::SetInteractiveCreationInProgress(bool bInProgress)
{
	Impl->SetInteractiveCreationInProgress(bInProgress);
}

EITwinSplineUsage AITwinSplineHelper::GetUsage() const
{
#ifndef RELEASE_CONFIG
	if (Impl->Spline)
	{
		auto spline = Impl->Spline->GetRAutoLock();
		BE_ASSERT(static_cast<EITwinSplineUsage>(spline->GetUsage()) == Impl->Usage,
			"unsynchronized spline usage Unreal vs AdvViz::SDK");
	}
#endif
	return Impl->Usage;
}

bool AITwinSplineHelper::IsPointEditionAllowed() const
{
	const bool bIsEditionAllowed =
		/* AzDev#1967146: point insertion is now only possible for the selected polygon. */
		IsSelected()
		/* Edge display helpers (introduced for cutout cubes), are just used for display and thus are not
		 * editable. */
		&& GetUsage() != EITwinSplineUsage::EdgeDisplayHelper;

	return bIsEditionAllowed;
}

EITwinTangentMode AITwinSplineHelper::GetTangentMode() const
{
	return Impl->TangentMode;
}

void AITwinSplineHelper::SetTangentMode(const EITwinTangentMode mode)
{
	Impl->SetTangentMode(mode);
}

void AITwinSplineHelper::FImpl::SetTightness(int32 PointIndex, float InTightness)
{
	float TargetTangentLength = ITwinSpline::TangentLengthFromTightness(InTightness);

	USplineComponent& SplineComp(*Owner.SplineComponent);

	const int32 NumPoints = SplineComp.GetNumberOfSplinePoints();
	const bool bIsLoop = SplineComp.IsClosedLoop();
	
	// Get neighboring point indices
	const int32 PrevIndex = ITwinSpline::GetPrevIndex(PointIndex, NumPoints, bIsLoop);
	const int32 NextIndex = ITwinSpline::GetNextIndex(PointIndex, NumPoints, bIsLoop);

	// Get positions of the current, previous, and next points
	const FVector PrevPos = SplineComp.GetLocationAtSplinePoint(PrevIndex, SPL_LOCAL);
	//const FVector CurrPos = SplineComp.GetLocationAtSplinePoint(PointIndex, SPL_LOCAL);
	const FVector NextPos = SplineComp.GetLocationAtSplinePoint(NextIndex, SPL_LOCAL);

	UpdateTangent(PointIndex, NextPos - PrevPos, TargetTangentLength);

	SplineComp.UpdateSpline();

	// Update the internal AdvViz spline data (for the saving of points)
	if (Spline)
		UpdatePointFromUEtoAViz(PointIndex);

	// Update meshes
	if (NeedsDraw3DElements())
	{
		UpdateMeshComponentsForPoint(ITwinSpline::GetPrevIndex(PointIndex, NumPoints, bIsLoop));
		UpdateMeshComponentsForPoint(PointIndex);
	}

	OnSplineModified();
}

float AITwinSplineHelper::FImpl::GetTightness(int32 PointIndex) const
{
	USplineComponent const& SplineComp(*Owner.SplineComponent);

	const FVector ArriveTangent = SplineComp.GetArriveTangentAtSplinePoint(PointIndex, SPL_LOCAL);
	return ITwinSpline::TightnessFromTangentLength(ArriveTangent.Length());
}

void AITwinSplineHelper::SetTightness(int32 PointIndex, float InTightness/* = 0.5f*/)
{
	if (!SplineComponent || PointIndex < 0 || PointIndex >= SplineComponent->GetNumberOfSplinePoints())
		return;

	Impl->SetTightness(PointIndex, InTightness);
}

float AITwinSplineHelper::GetTightness(int32 PointIndex) const
{
	if (!SplineComponent || PointIndex < 0 || PointIndex >= SplineComponent->GetNumberOfSplinePoints())
		return 0.0f;

	return Impl->GetTightness(PointIndex);
}

int32 AITwinSplineHelper::FindPointIndexFromMeshComponent(UStaticMeshComponent* MeshComp) const
{
	return PointMeshComponents.Find(MeshComp);
}

UStaticMeshComponent* AITwinSplineHelper::GetLastPointMeshComponent() const
{
	if (PointMeshComponents.Num() > 0)
	{
		return PointMeshComponents.Last();
	}
	else
	{
		return nullptr;
	}
}

int32 AITwinSplineHelper::FindSegmentIndexFromSplineComponent(USplineMeshComponent* SplineMeshComp) const
{
	return SplineMeshComponents.Find(SplineMeshComp);
}


ACesiumCartographicPolygon* AITwinSplineHelper::GetCartographicPolygonForTileset(FITwinTilesetAccess const& TilesetAccess) const
{
	const ACesium3DTileset* Tileset = TilesetAccess.GetTileset();
	if (Tileset)
		return GetCartographicPolygonForGeoref(Tileset->GetGeoreference());
	else
		return nullptr;
}

ACesiumCartographicPolygon* AITwinSplineHelper::GetCartographicPolygonForGeoref(TSoftObjectPtr<ACesiumGeoreference> const& Georef) const
{
	auto const* Polygon = PerGeorefPolygonMap.Find(Georef);
	if (Polygon)
	{
		return (*Polygon).Get();
	}
	else
	{
		return nullptr;
	}
}

bool AITwinSplineHelper::HasCartographicPolygon() const
{
	for (auto const& [_, PolygonPtr] : PerGeorefPolygonMap)
	{
		if (PolygonPtr.Get())
			return true;
	}
	return false;
}

void AITwinSplineHelper::SetCartographicPolygonForTileset(ACesiumCartographicPolygon* Polygon, FITwinTilesetAccess const& TilesetAccess)
{
	const ACesium3DTileset* Tileset = TilesetAccess.GetTileset();
	if (Tileset)
		SetCartographicPolygonForGeoref(Polygon, Tileset->GetGeoreference());
}

void AITwinSplineHelper::SetCartographicPolygonForGeoref(ACesiumCartographicPolygon* Polygon, TSoftObjectPtr<ACesiumGeoreference> const& Georef)
{
	PerGeorefPolygonMap.FindOrAdd(Georef) = Polygon;
}

ACesiumCartographicPolygon* AITwinSplineHelper::ClonePolygonForTileset(FITwinTilesetAccess const& TilesetAccess)
{
	const ACesium3DTileset* Tileset = TilesetAccess.GetTileset();
	if (Tileset)
		return ClonePolygonForGeoref(Tileset->GetGeoreference());
	else
		return nullptr;
}

namespace ITwin
{
	ACesiumCartographicPolygon* DuplicatePolygonForGeoref(ACesiumCartographicPolygon const& SrcPolygon,
		const TSoftObjectPtr<ACesiumGeoreference>& Georef,
		UWorld& World)
	{
		if (!Georef)
			return nullptr;
		if (!SrcPolygon.Polygon || SrcPolygon.Polygon->GetNumberOfSplinePoints() == 0)
			return nullptr;
		// Create a Cesium cartographic polygon
		ACesiumCartographicPolygon* DstPolygon = World.SpawnActor<ACesiumCartographicPolygon>();
		DstPolygon->GlobeAnchor->SetGeoreference(Georef);
		DstPolygon->SetActorLocation(SrcPolygon.GetActorLocation());

		USplineComponent* DstSplineComponent = DstPolygon->Polygon;
		if (!ensure(DstSplineComponent != nullptr))
		{
			return nullptr;
		}

		DstPolygon->GlobeAnchor->SetAdjustOrientationForGlobeWhenMoving(false);
		// Replace the default spline points by those defined by the AdvViz spline.
		DstSplineComponent->ClearSplinePoints();

		USplineComponent const& SrcSplineComponent(*SrcPolygon.Polygon);
		const int32 NumPoints = SrcSplineComponent.GetNumberOfSplinePoints();
		TArray<FVector> DstPoints;
		DstPoints.SetNum(NumPoints);
		for (int32 i(0); i < NumPoints; ++i)
		{
			DstPoints[i] = SrcSplineComponent.GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
		}
		DstSplineComponent->SetSplinePoints(DstPoints, ESplineCoordinateSpace::World);

		DstPolygon->GlobeAnchor->SetAdjustOrientationForGlobeWhenMoving(true);
		return DstPolygon;
	}
}

ACesiumCartographicPolygon* AITwinSplineHelper::ClonePolygonForGeoref(TSoftObjectPtr<ACesiumGeoreference> const& Georef)
{
	ensureMsgf(GetCartographicPolygonForGeoref(Georef) == nullptr, TEXT("Polygon already exists for this geo-ref"));

	// Try to find a valid polygon in map
	ACesiumCartographicPolygon const* MasterPolygon = nullptr;
	for (auto const& [_, PolygonPtr] : PerGeorefPolygonMap)
	{
		MasterPolygon = PolygonPtr.Get();
		if (MasterPolygon)
			break;
	}
	if (!ensure(MasterPolygon != nullptr))
		return nullptr;
	UWorld* World = GetWorld();
	if (!ensure(World))
		return nullptr;
	ACesiumCartographicPolygon* NewPolygon = ITwin::DuplicatePolygonForGeoref(*MasterPolygon, Georef, *World);
	if (NewPolygon)
	{
		SetCartographicPolygonForGeoref(NewPolygon, Georef);
	}
	return NewPolygon;
}

void AITwinSplineHelper::DeleteCartographicPolygons(TFunction<void(ACesiumCartographicPolygon*)> const& BeforeDeleteCallback)
{
	for (auto& [_, PolygonPtr] : PerGeorefPolygonMap)
	{
		ACesiumCartographicPolygon* Polygon = PolygonPtr.Get();
		if (Polygon)
		{
			if (BeforeDeleteCallback)
			{
				BeforeDeleteCallback(Polygon);
			}
			Polygon->Destroy();
			PolygonPtr = {};
		}
	}
	PerGeorefPolygonMap.Reset();
}

void AITwinSplineHelper::SetTransform(const FTransform& NewTransform, bool bMarkSplineForSaving)
{
	SetActorTransform(NewTransform);

	Impl->SetTransform(NewTransform, bMarkSplineForSaving);
}

FTransform AITwinSplineHelper::GetTransformForUserInteraction() const
{
	FTransform Transform = GetActorTransform();
	Transform.SetTranslation(Impl->GetBarycenter());
	return Transform;
}

void AITwinSplineHelper::SetTransformFromUserInteraction(const FTransform& NewTransform)
{
	// Take the offset with the barycenter into account.
	FTransform FinalTransform = NewTransform;
	FVector FinalPos = NewTransform.GetTranslation();
	FinalPos += GetActorLocation() - Impl->GetBarycenter();
	FinalTransform.SetTranslation(FinalPos);
	SetTransform(FinalTransform, true);
}

FVector AITwinSplineHelper::GetLocationAtSplinePoint(int32 pointIndex) const
{
	return Impl->GetLocationAtSplinePoint(pointIndex);
}

void AITwinSplineHelper::SetLocationAtSplinePoint(int32 pointIndex, const FVector& location)
{
	Impl->SetLocationAtSplinePoint(pointIndex, location);
}

bool AITwinSplineHelper::IncludeInWorldBox(FBox& Box) const
{
	return Impl->IncludeInWorldBox(Box);
}

bool AITwinSplineHelper::DoesLineIntersectSplinePolygon(const FVector& Start, const FVector& End) const
{
	return Impl->DoesLineIntersectSplinePolygon(Start, End);
}

int32 AITwinSplineHelper::MinNumberOfPointsForValidSpline() const
{
	return ITwinSpline::MinNumberOfPointsForValidSpline(IsClosedLoop());
}

bool AITwinSplineHelper::CanDeletePoint() const
{
	return ITwinSpline::CanDeletePoint(GetNumberOfSplinePoints(), IsClosedLoop());
}

bool AITwinSplineHelper::DeletePoint(int32 pointIndex)
{
	return Impl->DeletePoint(pointIndex);
}

bool AITwinSplineHelper::DuplicatePoint(int32 pointIndex)
{
	return Impl->DuplicatePoint(pointIndex);
}

// This function can be used to duplicate a point when moving it. The passed index should
// be the currently selected point. It is modified if necessary depending on the movement.
bool AITwinSplineHelper::DuplicatePoint(int32& pointIndex, FVector& newWorldPosition)
{
	return Impl->DuplicatePoint(pointIndex, newWorldPosition);
}

int32 AITwinSplineHelper::InsertPointAt(const int32 PointIndex, FVector const& NewWorldPosition)
{
	return Impl->InsertPointAt(PointIndex, NewWorldPosition);
}

void AITwinSplineHelper::SetPointsHiddenInGame(bool bNewHidden) const
{
	Set3DPointsHiddenInGame(bNewHidden);
	if (OnScreen2DWidget)
	{
		OnScreen2DWidget->SetShowPins(!bNewHidden);
	}
}

void AITwinSplineHelper::Set3DPointsHiddenInGame(bool bNewHidden) const
{
	for (auto const& PointMeshComp : PointMeshComponents)
	{
		if (PointMeshComp)
		{
			PointMeshComp->SetHiddenInGame(bNewHidden);
		}
	}
}

void AITwinSplineHelper::Set3DSplinesHiddenInGame(bool bNewHidden) const
{
	for (auto const& SplineMeshComp : SplineMeshComponents)
	{
		if (SplineMeshComp)
		{
			SplineMeshComp->SetHiddenInGame(bNewHidden);
		}
	}
}

void AITwinSplineHelper::SetDraw3DRibbon(bool bInDraw3DRibbon)
{
	if (bDraw3DRibbon != bInDraw3DRibbon)
	{
		bDraw3DRibbon = bInDraw3DRibbon;
		if (GetNumberOfSplinePoints() > 0)
		{
			Impl->RecreateAllMeshComponents();
		}
	}
}

void AITwinSplineHelper::SetDraw3DPoints(bool bInDraw3DPoints)
{
	if (bDraw3DPoints != bInDraw3DPoints)
	{
		bDraw3DPoints = bInDraw3DPoints;
		if (GetNumberOfSplinePoints() > 0)
		{
			Impl->RecreateAllMeshComponents();
		}
	}
}

void AITwinSplineHelper::SetDraw2DElements(bool bInDraw2DElements)
{
	bDraw2DElements = bInDraw2DElements;
	Update2DWidgetVisibility();
}

void AITwinSplineHelper::Set2DThickness(float InThickness)
{
	if (OnScreen2DWidget)
	{
		OnScreen2DWidget->SetThickness(InThickness);
	}
}

bool AITwinSplineHelper::NeedsUpdate2DElements() const
{
	return Impl->NeedsUpdate2DElements();
}

void AITwinSplineHelper::SetNeedsUpdate2DElements(bool bNeedsUpdate)
{
	Impl->SetNeedsUpdate2DElements(bNeedsUpdate);
}


void AITwinSplineHelper::Update2DWidgetVisibility()
{
	if (OnScreen2DWidget)
	{
		OnScreen2DWidget->SetVisibility(
			(bDraw2DElements && !IsHidden()) ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		OnScreen2DWidget->OnVisibilityUpdated();
	}
}

void AITwinSplineHelper::SetActorHiddenInGame(bool bNewHidden)
{
	if (!bNewHidden && !Impl->bHasComputedScale)
	{
		// Avoid abruptly rescaling the spline meshes on next tick when the spline becomes visible, by
		// computing the appropriate scale now.
		Impl->ScaleMeshComponentsForCurrentPOV();
	}
	Super::SetActorHiddenInGame(bNewHidden);
	Update2DWidgetVisibility();
}

void AITwinSplineHelper::Tick(float /*DeltaTime*/)
{
	if (IsHidden() && Impl->bHasComputedScale)
	{
		return;
	}
	Impl->ScaleMeshComponentsForCurrentPOV();
}

void AITwinSplineHelper::SetFixedSplineWidth(float Width)
{
	Impl->ComputeSplineScaleFromWidth(Width);
}

namespace ITwin
{
	bool ActivateCutoutPolygon(FITwinTilesetAccess const& TilesetAccess, ACesiumCartographicPolygon* Polygon, bool bActivate);
	void InvertCutoutPolygonEffect(FITwinTilesetAccess const& TilesetAccess, ACesiumCartographicPolygon* Polygon, bool bInvertEffect);
}

void AITwinSplineHelper::ActivateCutoutEffect(FITwinTilesetAccess const& TilesetAccess, bool bActivate,
	bool bIsCreatingSpline /*= false*/)
{
	if (!ensure(GetUsage() == EITwinSplineUsage::MapCutout))
		return;
	const ITwin::ModelLink Link = TilesetAccess.GetDecorationKey();
	const bool bActivated_Cur = GetLinkedModels().contains(Link);
	if (bActivated_Cur == bActivate && !bIsCreatingSpline)
		return; // Nothing to do

	ACesiumCartographicPolygon* Polygon = GetCartographicPolygonForTileset(TilesetAccess);
	if (bActivate && !Polygon)
	{
		// Here me may need to instantiate a new polygon, if none exists yet for this tileset
		// geo-reference:
		Polygon = ClonePolygonForTileset(TilesetAccess);
	}
	if (Polygon)
	{
		ITwin::ActivateCutoutPolygon(TilesetAccess, Polygon, bActivate);
	}

	// Handle persistence:
	if (Impl->Spline)
	{
		// Rebuild the list of linked models in SDKCore
		auto spline = Impl->Spline->GetAutoLock();
		std::vector<AdvViz::SDK::SplineLinkedModel> LinkedModels = spline->GetLinkedModels();
		const AdvViz::SDK::SplineLinkedModel EditedModel = {
			.modelType = ITwin::ModelTypeToString(Link.first),
			.modelId = TCHAR_TO_UTF8(*Link.second)
		};
		if (bActivate)
		{
			if (std::find(LinkedModels.begin(), LinkedModels.end(), EditedModel) == LinkedModels.end())
			{
				LinkedModels.push_back(EditedModel);
			}
		}
		else
		{
			std::erase_if(LinkedModels, [&EditedModel](const auto& LinkedModel)
			{
				return LinkedModel.modelType == EditedModel.modelType
					&& LinkedModel.modelId == EditedModel.modelId;
			});
		}
		spline->SetLinkedModels(LinkedModels);
		ensure(GetLinkedModels().contains(Link) == bActivate);
	}
}

bool AITwinSplineHelper::IsEnabledEffect() const
{
	if (!Impl->Spline)
		return false;
	auto spline = Impl->Spline->GetRAutoLock();
	return spline->IsEnabledEffect();
}

void AITwinSplineHelper::EnableEffect(bool bEnable)
{
	// Handle persistence
	if (Impl->Spline)
	{
		auto spline = Impl->Spline->GetAutoLock();
		spline->EnableEffect(bEnable);
	}
}

bool AITwinSplineHelper::IsInvertedCutoutEffect() const
{
	if (!Impl->Spline)
		return false;
	auto spline = Impl->Spline->GetRAutoLock();
	return spline->GetInvertEffect();
}

void AITwinSplineHelper::InvertCutoutEffect(FITwinTilesetAccess const& TilesetAccess, bool bInvert)
{
	if (!ensure(GetUsage() == EITwinSplineUsage::MapCutout))
		return;

	ACesiumCartographicPolygon* Polygon = GetCartographicPolygonForTileset(TilesetAccess);
	if (!Polygon)
		return;

	// NB: the inversion of a cutout polygon is managed in UCesiumPolygonRasterOverlay level, and not in
	// ACesiumCartographicPolygon => we cannot invert the effect of an individual polygon, it will apply
	// to all polygons of the tileset.
	ITwin::InvertCutoutPolygonEffect(TilesetAccess, Polygon, bInvert);

	// Handle persistence
	if (Impl->Spline)
	{
		auto spline = Impl->Spline->GetAutoLock();
		spline->SetInvertEffect(bInvert);
	}
}

void AITwinSplineHelper::SetSelected(bool bSelected)
{
	Impl->SetSelected(bSelected);
}

bool AITwinSplineHelper::IsSelected() const
{
	return Impl->bSelected;
}

void AITwinSplineHelper::SetSelectedPointIndex(int32 PointIndex)
{
	Impl->SetSelectedPointIndex(PointIndex);
}

int32 AITwinSplineHelper::GetSelectedPointIndex() const
{
	return IsSelected() ? Impl->SelectedPointIndex : -1;
}

bool AITwinSplineHelper::IsUsedForPathAnim() const
{
	return ITwinSpline::IsPathAnim(GetUsage());
}

void AITwinSplineHelper::SetPathAnimShaderScalarParameterValue(EITwinAnimPathShaderScalarParam Param, float Value,
	bool bApplyToMeshComponents /*= true*/)
{
	Impl->SetPathAnimShaderScalarParameterValue(Param, Value, bApplyToMeshComponents);
}

void AITwinSplineHelper::TransferPathAnimShaderParametersToMeshes()
{
	Impl->TransferPathAnimShaderParametersToMeshes();
}

bool AITwinSplineHelper::IsUsedForPopulation() const
{
	return ITwinSpline::IsPopulation(GetUsage());
}

#if ENABLE_DRAW_DEBUG

// Console command to set spline thickness.
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinSetSplineThickness(
	TEXT("cmd.ITwinSetSplineThickness"),
	TEXT("Set the thickness of the 2D representation of all splines."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	if (Args.Num() != 1)
	{
		BE_ISSUE("Need exactly 1 args: <thickness>");
		return;
	}
	const float NewThickness = FCString::Atof(*Args[0]);
	if (NewThickness > 0.f)
	{
		for (TActorIterator<AITwinSplineHelper> SplineIter(World); SplineIter; ++SplineIter)
		{
			(*SplineIter)->Set2DThickness(NewThickness);
		}
	}
}));


// Console command to change spline draw mode.
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinSetSplineDrawMode(
	TEXT("cmd.ITwinSetSplineDrawMode"),
	TEXT("Set the draw mode of all splines (mode should contain 2D|3D_Point|3D_Ribbon)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	if (Args.Num() != 1)
	{
		BE_ISSUE("Need exactly 1 args: <draw_mode>");
		return;
	}
	const FString NewDrawMode = Args[0];
	if (!NewDrawMode.IsEmpty())
	{
		const bool bDraw2DElements = NewDrawMode.Contains(TEXT("2D"));
		const bool bDraw3DPoints = NewDrawMode.Contains(TEXT("3D_Point"));
		const bool bDraw3DRibbon = NewDrawMode.Contains(TEXT("3D_Ribbon"));
		for (TActorIterator<AITwinSplineHelper> SplineIter(World); SplineIter; ++SplineIter)
		{
			(*SplineIter)->SetDraw2DElements(bDraw2DElements);
			(*SplineIter)->SetDraw3DPoints(bDraw3DPoints);
			(*SplineIter)->SetDraw3DRibbon(bDraw3DRibbon);
		}
	}
}));


#endif // ENABLE_DRAW_DEBUG
