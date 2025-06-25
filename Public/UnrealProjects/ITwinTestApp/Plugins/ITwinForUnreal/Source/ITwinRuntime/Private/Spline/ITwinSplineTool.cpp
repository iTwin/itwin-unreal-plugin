/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSplineTool.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Spline/ITwinSplineTool.h>
#include <Spline/ITwinSplineHelper.h>

#include <Math/UEMathConversion.h>
#include <ITwinGoogle3DTileset.h>
#include <CesiumCartographicPolygon.h>
#include <CesiumPolygonRasterOverlay.h>
#include <ITwinGeolocation.h>
#include <ITwinTilesetAccess.h>

#include <Components/SplineComponent.h>
#include <Components/StaticMeshComponent.h>
#include <EngineUtils.h>
#include <Engine/World.h>
#include <GameFramework/PlayerController.h>
#include <Kismet/KismetSystemLibrary.h>
#include <Kismet/GameplayStatics.h>
#include <Population/ITwinPopulationTool.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Compil/CleanUpGuard.h>
#	include <SDK/Core/Visualization/SplinesManager.h>
#include <Compil/AfterNonUnrealIncludes.h>

class AITwinSplineTool::FImpl
{
public:
	AITwinSplineTool& owner;

	bool bIsEnabled = false; // boolean used to switch on or off the spline tool

	AITwinSplineHelper* selectedSplineHelper = nullptr;
	int32 selectedPointIndex = -1;
	bool duplicateWhenMovingPoint = false;
	EITwinSplineToolMode ToolMode = EITwinSplineToolMode::Undefined;
	EITwinSplineUsage ToolUsage = EITwinSplineUsage::Undefined;
	std::shared_ptr<AdvViz::SDK::ISplinesManager> splinesManager;
	std::optional<ITwin::ModelDecorationIdentifier> CutoutTargetIdentifier;
	bool bIsLoadingSpline = false;

	FImpl(AITwinSplineTool& inOwner);

	// Implementation of AITwinSplineTool functions
	AITwinSplineHelper* GetSelectedSpline() const;
	void SetSelectedSpline(AITwinSplineHelper* splineHelper);
	void SetSelectedPointIndex(int32 pointIndex);
	bool HasSelection() const;
	void DeleteSelection();
	bool HasSelectedPoint() const;
	void DeleteSelectedSpline();
	bool CanDeletePoint() const;
	void DeleteSelectedPoint();
	void DuplicateSelectedPoint();
	void EnableDuplicationWhenMovingPoint(bool value);
	FTransform GetSelectionTransform() const;
	void SetSelectionTransform(const FTransform& transform);
	void SetEnabled(bool value);
	bool IsEnabled() const;
	void ResetToDefault();
	bool DoMouseClickAction();

	bool ActionOnTick(float DeltaTime);

	bool AddSpline(FVector const& Position);

	bool LoadSpline(AdvViz::SDK::SharedSpline const& Spline);


	bool GetSplineReferencePosition(FVector& RefLocation, FBox& OutBox) const;

	EITwinSplineToolMode GetMode() const { return ToolMode; }
	void SetMode(EITwinSplineToolMode NewMode) { ToolMode = NewMode; }
	void ToggleInteractiveCreationMode();

	EITwinSplineUsage GetUsage() const { return ToolUsage; }
	void SetUsage(EITwinSplineUsage NewUsage) { ToolUsage = NewUsage; }

	EITwinTangentMode GetTangentMode() const;
	void SetTangentMode(EITwinTangentMode TangentMode);

	void RefreshScene(AITwinSplineHelper const* TargetSpline = nullptr);

private:
	ACesium3DTileset* GetCutoutTarget3DTileset() const;

	AITwinSplineHelper* CreateSpline(EITwinSplineUsage SplineUsage,
		std::optional<FVector> const& PositionOpt, AdvViz::SDK::SharedSpline const& LoadedSpline);

	void RemoveCartographicPolygon(ACesiumCartographicPolygon* polygon);
};

AITwinSplineTool::FImpl::FImpl(AITwinSplineTool& inOwner)
	: owner(inOwner)
{
}

AITwinSplineHelper* AITwinSplineTool::FImpl::GetSelectedSpline() const
{
	return selectedSplineHelper;
}

void AITwinSplineTool::FImpl::SetSelectedSpline(AITwinSplineHelper* splineHelper)
{
	selectedSplineHelper = splineHelper;
	selectedPointIndex = -1;

	owner.SplineEditionEvent.Broadcast();
}

void AITwinSplineTool::FImpl::SetSelectedPointIndex(int32 pointIndex)
{
	selectedPointIndex = pointIndex;

	owner.SplineEditionEvent.Broadcast();
}

bool AITwinSplineTool::FImpl::HasSelection() const
{
	return IsValid(selectedSplineHelper);
}

void AITwinSplineTool::FImpl::DeleteSelection()
{
	if (HasSelection())
	{
		if (CanDeletePoint())
		{
			DeleteSelectedPoint();
		}
		else
		{
			DeleteSelectedSpline();
		}
		owner.SplineEditionEvent.Broadcast();
	}
}

void AITwinSplineTool::FImpl::DeleteSelectedSpline()
{
	if (HasSelection())
	{
		ACesiumCartographicPolygon* polygon = selectedSplineHelper->GetCartographicPolygon();
		if (polygon)
		{
			RemoveCartographicPolygon(polygon);
			selectedSplineHelper->SetCartographicPolygon(nullptr);
			polygon->Destroy();
		}
		if (splinesManager)
		{
			splinesManager->RemoveSpline(selectedSplineHelper->GetAVizSpline());
		}
		selectedSplineHelper->Destroy();
		selectedSplineHelper = nullptr;
		selectedPointIndex = -1;

		owner.SplineRemovedEvent.Broadcast();
		owner.SplineEditionEvent.Broadcast();
	}
}

bool AITwinSplineTool::FImpl::HasSelectedPoint() const
{
	return HasSelection() && selectedPointIndex >= 0;
}

bool AITwinSplineTool::FImpl::CanDeletePoint() const
{
	// The minimum number of spline points is 3, because we won't cut anything interesting  with a segment...
	return HasSelection()
		&& selectedPointIndex >= 0
		&& selectedSplineHelper->CanDeletePoint();
}

void AITwinSplineTool::FImpl::DeleteSelectedPoint()
{
	if (CanDeletePoint())
	{
		selectedSplineHelper->DeletePoint(selectedPointIndex);

#define SELECT_ONE_OF_REMAINING_POINTS() 0
		// TODO_JDE: for some reason, when implementing this, the gizmo remained "orphan", ie. it is moved
		// to the position of the newly selected point *but* when one try to move the gizmo, the spline point
		// no longer follows it.
		// So, for the EAP, I'm just discard the selection (btw this was not a request from the UX designer,
		// but just an initiative from me...)

#if SELECT_ONE_OF_REMAINING_POINTS()
		// Select the next point in the spline (most of the time, we can just keep current index unchanged,
		// since the point was just removed. The only exception is when we deleted the last one => then we
		// loop...)
		if (selectedPointIndex >= selectedSplineHelper->GetNumberOfSplinePoints())
		{
			selectedPointIndex = 0;
		}
		if (ensure(selectedPointIndex < selectedSplineHelper->GetNumberOfSplinePoints()))
		{
			SetSelectedPointIndex(selectedPointIndex);

			owner.SplinePointRemovedEvent.Broadcast();
		}
		else
#endif // SELECT_ONE_OF_REMAINING_POINTS
		{
			// Should not happen as we enforce keeping at least 3 points...
			selectedSplineHelper = nullptr;
			selectedPointIndex = -1;

			owner.SplinePointRemovedEvent.Broadcast();
			owner.SplineEditionEvent.Broadcast();
		}
	}
}

void AITwinSplineTool::FImpl::DuplicateSelectedPoint()
{
	if (HasSelectedPoint())
	{
		selectedSplineHelper->DuplicatePoint(selectedPointIndex);
	}
}

void AITwinSplineTool::FImpl::EnableDuplicationWhenMovingPoint(bool value)
{
	duplicateWhenMovingPoint = value;
}

FTransform AITwinSplineTool::FImpl::GetSelectionTransform() const
{
	if (IsValid(selectedSplineHelper))
	{
		if (selectedPointIndex >= 0 &&
			selectedPointIndex < selectedSplineHelper->GetNumberOfSplinePoints())
		{
			FVector pos = selectedSplineHelper->GetLocationAtSplinePoint(selectedPointIndex);
			FTransform tm = selectedSplineHelper->GetActorTransform();
			tm.SetTranslation(pos);
			return tm;
		}
		else
		{
			return selectedSplineHelper->GetActorTransform();
		}
	}

	return FTransform();
}

void AITwinSplineTool::FImpl::SetSelectionTransform(const FTransform& transform)
{
	if (IsValid(selectedSplineHelper))
	{
		if (selectedPointIndex >= 0)
		{
			FVector position = transform.GetLocation();

			if (selectedSplineHelper->GetLocationAtSplinePoint(selectedPointIndex) !=
				position)
			{
				if (duplicateWhenMovingPoint)
				{
					selectedSplineHelper->DuplicatePoint(selectedPointIndex, position);
					duplicateWhenMovingPoint = false;
				}

				selectedSplineHelper->SetLocationAtSplinePoint(selectedPointIndex, position);
			}
		}
		else
		{
			selectedSplineHelper->SetTransform(transform, true);
		}
	}
}

void AITwinSplineTool::FImpl::SetEnabled(bool value)
{
	if (value != this->bIsEnabled)
	{
		this->bIsEnabled = value;

		// Show/hide splines helpers matching the current usage
		TArray<AActor*> splineHelpers;
		UGameplayStatics::GetAllActorsOfClass(
			owner.GetWorld(), AITwinSplineHelper::StaticClass(), splineHelpers);
		for (auto splineHelper : splineHelpers)
		{
			bool bHideSpline = !this->bIsEnabled;
			if (this->bIsEnabled)
			{
				// Only display splines of the selected usage, and linked to the current model, if any.
				auto const SplHelper = Cast<AITwinSplineHelper>(splineHelper);
				bHideSpline = SplHelper->GetUsage() != this->GetUsage()
					|| SplHelper->GetLinkedModel() != this->CutoutTargetIdentifier;
			}
			splineHelper->SetActorHiddenInGame(bHideSpline);
		}

		if (!this->bIsEnabled)
		{
			SetSelectedSpline(nullptr);
		}
	}
}

bool AITwinSplineTool::FImpl::IsEnabled() const
{
	return bIsEnabled;
}

void AITwinSplineTool::FImpl::ResetToDefault()
{
}

bool AITwinSplineTool::FImpl::DoMouseClickAction()
{
	FHitResult hitResult = owner.DoPickingAtMousePosition();
	AActor* hitActor = hitResult.GetActor();

	if (GetMode() == EITwinSplineToolMode::InteractiveCreation)
	{
		if (hitActor)
		{
			if (!HasSelection())
			{
				// Start a new spline
				AITwinSplineHelper* NewSplineHelper = CreateSpline(ToolUsage, hitResult.ImpactPoint, {});
				SetSelectedSpline(NewSplineHelper);

				// Select the last point of the created spline
				if (NewSplineHelper)
				{
					SetSelectedPointIndex(NewSplineHelper->GetNumberOfSplinePoints() - 1);
				}
			}

			if (HasSelectedPoint())
			{
				// Immediately duplicate the current point, so that the user can start moving the next one
				// interactively.
				DuplicateSelectedPoint();

				SetSelectedPointIndex(selectedSplineHelper->GetNumberOfSplinePoints() - 1);
			}
		}
	}
	else
	{
		SetSelectedSpline(nullptr);
		if (hitActor)
		{
			if (hitActor->IsA(AITwinSplineHelper::StaticClass()))
			{
				AITwinSplineHelper* splineHelper = Cast<AITwinSplineHelper>(hitActor);
				SetSelectedSpline(splineHelper);

				if (hitResult.GetComponent() &&
					hitResult.GetComponent()->IsA(UStaticMeshComponent::StaticClass()))
				{
					int32 pointIndex = splineHelper->FindPointIndexFromMeshComponent(
						Cast<UStaticMeshComponent>(hitResult.GetComponent()));

					if (pointIndex != INDEX_NONE)
					{
						SetSelectedPointIndex(pointIndex);
					}
				}
				return true;
			}
		}
	}
	return false;
}

bool AITwinSplineTool::FImpl::ActionOnTick(float DeltaTime)
{
	if (GetMode() != EITwinSplineToolMode::InteractiveCreation)
		return false;
	if (!HasSelectedPoint())
		return false;

	// Try to project the selected point on the object below it, under the mouse
	FHitResult HitResult = owner.DoPickingAtMousePosition();
	AActor* HitActor = HitResult.GetActor();
	if (HitActor && !HitActor->IsA(AITwinSplineHelper::StaticClass()))
	{
		FTransform TM = selectedSplineHelper->GetActorTransform();
		TM.SetTranslation(HitResult.ImpactPoint);
		SetSelectionTransform(TM);
		return true;
	}
	return false;
}

namespace
{
	/// Helper class to instantiate AITwinSplineHelper, either from a spline loaded from the decoration
	/// service, or from scratch, with a new reference position.
	/// This class is abstract, as the implementation will depend on the specific spline usage.
	class ISplineHelperMaker
	{
	public:
		virtual ~ISplineHelperMaker();

		using ModelIdentifier = ITwin::ModelDecorationIdentifier;

		AITwinSplineHelper* MakeSplineHelper(UWorld& World, bool bCreateAsHidden,
			AdvViz::SDK::ISplinesManager& SplineManager,
			std::optional<ModelIdentifier> const& LinkedModel);

		bool IsLoadingFromAdvViz() const { return (bool)LoadedSpline; }

	protected:
		ISplineHelperMaker(
			TSoftObjectPtr<ACesiumGeoreference> const InGeoreference,
			EITwinSplineToolMode InToolMode,
			std::optional<FVector> const& InPositionOpt,
			AdvViz::SDK::SharedSpline const& InLoadedSpline,
			EITwinSplineUsage const InUsage);

		virtual USplineComponent* GetSplineComponent(UWorld& World,
			ACesiumCartographicPolygon*& OutCartographicPolygon) = 0;

		virtual void OnSplineCreated(AITwinSplineHelper& SplineHelper) {}


		TSoftObjectPtr<ACesiumGeoreference> const Georeference;
		EITwinSplineToolMode const ToolMode;
		std::optional<FVector> const PositionOpt;
		AdvViz::SDK::SharedSpline const LoadedSpline;
		EITwinSplineUsage const SplineUsage;

		EITwinTangentMode TangentMode = EITwinTangentMode::Custom;
	};

	ISplineHelperMaker::ISplineHelperMaker(
		TSoftObjectPtr<ACesiumGeoreference> const InGeoreference,
		EITwinSplineToolMode InToolMode,
		std::optional<FVector> const& InPositionOpt,
		AdvViz::SDK::SharedSpline const& InLoadedSpline,
		EITwinSplineUsage const InUsage)
		: Georeference(InGeoreference)
		, ToolMode(InToolMode)
		, PositionOpt(InPositionOpt)
		, LoadedSpline(InLoadedSpline)
		, SplineUsage(InUsage)
	{}

	ISplineHelperMaker::~ISplineHelperMaker()
	{

	}

	AITwinSplineHelper* ISplineHelperMaker::MakeSplineHelper(UWorld& World, bool bCreateAsHidden,
		AdvViz::SDK::ISplinesManager& SplineManager,
		std::optional<ModelIdentifier> const& LinkedModel)
	{
		ACesiumCartographicPolygon* CartographicPolygon = nullptr;
		USplineComponent* SplineComp = GetSplineComponent(World, CartographicPolygon);

		const bool bUseCartographicSplineComponent = (SplineUsage == EITwinSplineUsage::MapCutout);
		if (bUseCartographicSplineComponent && !ensure(SplineComp))
		{
			return nullptr;
		}

		AITwinSplineHelper* SplineHelper = nullptr;
		{
			const AITwinSplineHelper::FSpawnContext UsageContext(SplineUsage);

			SplineHelper = World.SpawnActor<AITwinSplineHelper>();
		}

		// For a generic spline, the spline component should have been created during the spawning of the
		// helper (thanks to the FSpawnContext above).
		if (!bUseCartographicSplineComponent)
		{
			SplineComp = SplineHelper->GetSplineComponent();
		}
		if (!ensure(SplineComp))
		{
			return nullptr;
		}
		const bool bCreateDefaultSpline = !LoadedSpline;
		if (bCreateDefaultSpline)
		{
			// Reduce the area of the default polygon
			for (int32 i = 0; i < SplineComp->GetNumberOfSplinePoints(); ++i)
			{
				FVector pos = SplineComp->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local);
				SplineComp->SetLocationAtSplinePoint(i, pos * 0.25, ESplineCoordinateSpace::Local);
			}
		}

		SplineHelper->GlobeAnchor->SetGeoreference(Georeference);
		if (PositionOpt)
		{
			SplineHelper->SetActorLocation(*PositionOpt);
		}
		else
		{
			// Disable automatic orientation, as we want to impose the transformation from the points just
			// loaded from the decoration service.
			SplineHelper->GlobeAnchor->SetAdjustOrientationForGlobeWhenMoving(false);
		}
		SplineHelper->SetCartographicPolygon(CartographicPolygon);

		AdvViz::SDK::SharedSpline Spline = LoadedSpline;
		if (!Spline)
		{
			// Instantiate the AdvViz spline for this new polygon.
			Spline = SplineManager.AddSpline();
			Spline->SetUsage(static_cast<AdvViz::SDK::ESplineUsage>(SplineUsage));
			if (LinkedModel)
			{
				Spline->SetLinkedModelType(ITwin::ModelTypeToString(LinkedModel->first));
				Spline->SetLinkedModelId(TCHAR_TO_UTF8(*LinkedModel->second));
			}
		}
		SplineHelper->Initialize(SplineComp, Spline);

		if (bCreateDefaultSpline)
		{
			SplineHelper->SetTangentMode(this->TangentMode);
		}
		else
		{
			SplineHelper->SetActorHiddenInGame(bCreateAsHidden);

			// After the initialization, since the transformation of the AdvViz::SDK spline has been
			// applied to UE actors, the automatic orientation can be re-enabled.
			if (CartographicPolygon)
			{
				CartographicPolygon->GlobeAnchor->SetAdjustOrientationForGlobeWhenMoving(true);
			}
			SplineHelper->GlobeAnchor->SetAdjustOrientationForGlobeWhenMoving(true);
		}

		OnSplineCreated(*SplineHelper);

		return SplineHelper;
	}

	/// Special implementation for Cutout polygon
	class FCutoutPolygonMaker : public ISplineHelperMaker
	{
	public:
		FCutoutPolygonMaker(
			ACesium3DTileset& InTileset,
			UCesiumPolygonRasterOverlay* InRasterOverlay,
			EITwinSplineToolMode InToolMode,
			std::optional<FVector> const& InPositionOpt,
			AdvViz::SDK::SharedSpline const& InSpline);

	protected:
		virtual USplineComponent* GetSplineComponent(UWorld& World,
			ACesiumCartographicPolygon*& OutCartographicPolygon) override;

		virtual void OnSplineCreated(AITwinSplineHelper& SplineHelper) override;

	private:
		ACesium3DTileset& Tileset;
		UCesiumPolygonRasterOverlay* RasterOverlay = nullptr;
		ACesiumCartographicPolygon* CartographicPolygon = nullptr;
	};


	FCutoutPolygonMaker::FCutoutPolygonMaker(ACesium3DTileset& InTileset,
		UCesiumPolygonRasterOverlay* InRasterOverlay,
		EITwinSplineToolMode InToolMode,
		std::optional<FVector> const& InPositionOpt,
		AdvViz::SDK::SharedSpline const& InSpline)
		: ISplineHelperMaker(InTileset.GetGeoreference(), InToolMode, InPositionOpt, InSpline,
			EITwinSplineUsage::MapCutout)
		, Tileset(InTileset)
		, RasterOverlay(InRasterOverlay)
	{
		// Cesium cutout polygons only support linear tangent mode.
		TangentMode = EITwinTangentMode::Linear;
	}

	USplineComponent* FCutoutPolygonMaker::GetSplineComponent(UWorld& World,
		ACesiumCartographicPolygon*& OutCartographicPolygon)
	{
		if (!ensure(RasterOverlay))
		{
			return nullptr;
		}

		// Create a Cesium cartographic polygon
		CartographicPolygon = World.SpawnActor<ACesiumCartographicPolygon>();
		CartographicPolygon->GlobeAnchor->SetGeoreference(Georeference);
		if (PositionOpt)
		{
			CartographicPolygon->SetActorLocation(*PositionOpt);
		}
		if (!PositionOpt || ToolMode == EITwinSplineToolMode::InteractiveCreation)
		{
			CartographicPolygon->GlobeAnchor->SetAdjustOrientationForGlobeWhenMoving(false);
			// We will replace the default spline points by those defined by the AdvViz spline.
			CartographicPolygon->Polygon->ClearSplinePoints();
		}
		if (ToolMode == EITwinSplineToolMode::InteractiveCreation)
		{
			// Interactive creation mode starts with just one point.
			CartographicPolygon->Polygon->SetSplinePoints(
				TArray<FVector>{
					FVector(0.0f, 0.0f, 0.0f)},
				ESplineCoordinateSpace::Local);
		}
		RasterOverlay->Polygons.Push(CartographicPolygon);

		OutCartographicPolygon = CartographicPolygon;
		return CartographicPolygon->Polygon;
	}

	void FCutoutPolygonMaker::OnSplineCreated(AITwinSplineHelper& /*SplineHelper*/)
	{
		// In interactive creation mode, the polygon is created with only one point => no need to refresh the
		// tileset now, as t^his will have no effect but showing the tileset off then on...
		if (ToolMode != EITwinSplineToolMode::InteractiveCreation)
		{
			Tileset.RefreshTileset();
		}
	}


	/// Generic implementation for all other usages
	class FGenericSplineHelperMaker : public ISplineHelperMaker
	{
	public:
		FGenericSplineHelperMaker(
			TSoftObjectPtr<ACesiumGeoreference> const InGeoreference,
			EITwinSplineToolMode InToolMode,
			std::optional<FVector> const& InPositionOpt,
			AdvViz::SDK::SharedSpline const& InLoadedSpline,
			EITwinSplineUsage const InUsage);

	protected:
		virtual USplineComponent* GetSplineComponent(UWorld& World,
			ACesiumCartographicPolygon*& OutCartographicPolygon) override {
			OutCartographicPolygon = nullptr;
			return nullptr;
		}
	};

	FGenericSplineHelperMaker::FGenericSplineHelperMaker(
		TSoftObjectPtr<ACesiumGeoreference> const InGeoreference,
		EITwinSplineToolMode InToolMode,
		std::optional<FVector> const& InPositionOpt,
		AdvViz::SDK::SharedSpline const& InLoadedSpline,
		EITwinSplineUsage const InUsage)
		: ISplineHelperMaker(InGeoreference, InToolMode, InPositionOpt, InLoadedSpline, InUsage)
	{
		ensureMsgf(SplineUsage != EITwinSplineUsage::MapCutout,
			TEXT("use FCutoutPolygonMaker for cutout polygon"));
	}
}

ACesium3DTileset* AITwinSplineTool::FImpl::GetCutoutTarget3DTileset() const
{
	if (owner.CutoutTarget.IsValid())
	{
		return owner.CutoutTarget.Get();
	}
	else
	{
		// If the target 3D tileset for cut-out is unspecified, we will work on the (singleton) Google 3D
		// tileset by default:
		for (TActorIterator<AITwinGoogle3DTileset> Google3DIter(owner.GetWorld()); Google3DIter; ++Google3DIter)
		{
			return (*Google3DIter);
		}
	}
	ensureMsgf(false, TEXT("no cut-out target tileset"));
	return nullptr;
}

AITwinSplineHelper* AITwinSplineTool::FImpl::CreateSpline(EITwinSplineUsage SplineUsage,
	std::optional<FVector> const& PositionOpt, AdvViz::SDK::SharedSpline const& LoadedSpline)
{
	if ((!PositionOpt) == (!LoadedSpline))
	{
		ensureMsgf(false, TEXT("coding mistake: provide either a position or an AdvViz spline"));
		return nullptr;
	}
	if (!splinesManager)
	{
		ensureMsgf(false, TEXT("no spline manager"));
		return nullptr;
	}
	UWorld* World = owner.GetWorld();
	if (!World)
	{
		ensureMsgf(false, TEXT("no world to instantiate spline"));
		return nullptr;
	}
	ensure(SplineUsage != EITwinSplineUsage::Undefined);

	std::unique_ptr<ISplineHelperMaker> SplineMaker;

	if (SplineUsage == EITwinSplineUsage::MapCutout)
	{
		// If the target 3D tileset for cut-out is unspecified, we will work on the (singleton) Google 3D
		// tileset by default:
		ACesium3DTileset* CutoutTarget3DTileset = GetCutoutTarget3DTileset();
		if (CutoutTarget3DTileset)
		{
			ITwin::InitCutoutOverlay(*CutoutTarget3DTileset);
			auto* RasterOverlay = ITwin::GetCutoutOverlay(*CutoutTarget3DTileset);
			if (ensure(RasterOverlay))
			{
				SplineMaker = std::make_unique<FCutoutPolygonMaker>(
					*CutoutTarget3DTileset, RasterOverlay, owner.GetMode(), PositionOpt, LoadedSpline);
			}
		}
		ensureMsgf(SplineMaker.get() != nullptr, TEXT("no tileset ready for cut-out polygon creation"));
	}
	else
	{
		// Generic spline creation.
		auto&& Geoloc = FITwinGeolocation::Get(*World);
		SplineMaker = std::make_unique<FGenericSplineHelperMaker>(
			Geoloc->GeoReference.Get(), owner.GetMode(), PositionOpt, LoadedSpline, SplineUsage);
	}

	AITwinSplineHelper* const CreatedSpline = SplineMaker
		 ? SplineMaker->MakeSplineHelper(*World, !bIsEnabled, *splinesManager, CutoutTargetIdentifier)
		 : nullptr;

	if (CreatedSpline)
	{
		owner.SplineEditionEvent.Broadcast();
	}
	else
	{
		ensureMsgf(false, TEXT("no spline created for usage %d"), static_cast<int>(SplineUsage));
	}
	return CreatedSpline;
}

bool AITwinSplineTool::FImpl::AddSpline(FVector const& Position)
{
	return CreateSpline(ToolUsage, Position, {}) != nullptr;
}

bool AITwinSplineTool::FImpl::LoadSpline(const AdvViz::SDK::SharedSpline& Spline)
{
	if (!Spline)
	{
		ensureMsgf(false, TEXT("no spline in input!"));
		return false;
	}
	return CreateSpline(static_cast<EITwinSplineUsage>(Spline->GetUsage()), {}, Spline) != nullptr;
}

void AITwinSplineTool::FImpl::RemoveCartographicPolygon(ACesiumCartographicPolygon* polygon)
{
	ACesium3DTileset* CutoutTargetTileset = GetCutoutTarget3DTileset();
	if (CutoutTargetTileset)
	{
		UCesiumPolygonRasterOverlay* RasterOverlay = ITwin::GetCutoutOverlay(*CutoutTargetTileset);
		if (RasterOverlay)
		{
			const int32 index = RasterOverlay->Polygons.Find(polygon);
			if (index != INDEX_NONE)
			{
				 RasterOverlay->Polygons.RemoveAt(index);
				 CutoutTargetTileset->RefreshTileset();
			}
		}
	}
}

bool AITwinSplineTool::FImpl::GetSplineReferencePosition(FVector& RefLocation, FBox& OutBox) const
{
	if (HasSelection())
	{
		RefLocation = selectedSplineHelper->GetActorLocation();
		return selectedSplineHelper->IncludeInWorldBox(OutBox);
	}
	else
	{
		int NumValidSplines = 0;
		for (TActorIterator<AITwinSplineHelper> SplineIter(owner.GetWorld()); SplineIter; ++SplineIter)
		{
			if (SplineIter->IncludeInWorldBox(OutBox))
				NumValidSplines++;
		}
		if (NumValidSplines > 0)
		{
			RefLocation = OutBox.GetCenter();
		}
		return (NumValidSplines > 0);
	}
}

void AITwinSplineTool::FImpl::ToggleInteractiveCreationMode()
{
	const EITwinSplineToolMode PreviousMode = ToolMode;

	ToolMode = (PreviousMode == EITwinSplineToolMode::InteractiveCreation)
		? EITwinSplineToolMode::Undefined
		: EITwinSplineToolMode::InteractiveCreation;

	bool bHasNewSpline = false;
	TWeakObjectPtr<AITwinSplineHelper> NewSpline = nullptr;
	if (PreviousMode == EITwinSplineToolMode::InteractiveCreation
		&& HasSelectedPoint())
	{
		// Discard the last duplicated point.
		// If the newly create spline has not enough points, remove it at once.
		if (CanDeletePoint())
		{
			NewSpline = selectedSplineHelper; // Store value before it is reset in DeleteSelectedPoint
			DeleteSelectedPoint();
			bHasNewSpline = true;
		}
		else
		{
			DeleteSelectedSpline();
		}
	}

	if (bHasNewSpline)
	{
		// End of the creation of a spline in interactive mode => refresh scene.
		RefreshScene(NewSpline.Get());
	}

	if (ToolMode == EITwinSplineToolMode::InteractiveCreation)
	{
		// Avoid conflict with slightly similar feature...
		EnableDuplicationWhenMovingPoint(false);
	}
}

void AITwinSplineTool::FImpl::RefreshScene(AITwinSplineHelper const* TargetSpline /*= nullptr*/)
{
	if (GetUsage() == EITwinSplineUsage::MapCutout)
	{
		// Refresh the target tileset.
		ACesium3DTileset* CutoutTargetTileset = GetCutoutTarget3DTileset();
		if (CutoutTargetTileset)
		{
			CutoutTargetTileset->RefreshTileset();
		}
	}
	else if ((GetUsage() == EITwinSplineUsage::PopulationPath
		   || GetUsage() == EITwinSplineUsage::PopulationZone)
		&& owner.PopulationTool.IsValid())
	{
		// (Re-)populate the active spline.
		AITwinSplineHelper const* SplineToPopulate = TargetSpline
			? TargetSpline
			: (HasSelection() ? selectedSplineHelper : nullptr);
		if (SplineToPopulate)
		{
			owner.PopulationTool->PopulateSpline(*SplineToPopulate);
		}
	}
}

EITwinTangentMode AITwinSplineTool::FImpl::GetTangentMode() const
{
	if (HasSelection())
	{
		return selectedSplineHelper->GetTangentMode();
	}
	else
	{
		return EITwinTangentMode::Custom;
	}
}

void AITwinSplineTool::FImpl::SetTangentMode(EITwinTangentMode TangentMode)
{
	if (HasSelection())
	{
		selectedSplineHelper->SetTangentMode(TangentMode);
	}
}

// -----------------------------------------------------------------------------
//                              AITwinSplineTool

AITwinSplineTool::AITwinSplineTool()
	: AITwinInteractiveTool(), Impl(MakePimpl<FImpl>(*this))
{
	// Set this actor to call Tick() every frame.
	PrimaryActorTick.bCanEverTick = true;
}

void AITwinSplineTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	Impl->ActionOnTick(DeltaTime);
}

AITwinSplineHelper* AITwinSplineTool::GetSelectedSpline() const
{
	return Impl->GetSelectedSpline();
}

void AITwinSplineTool::SetSelectedSpline(AITwinSplineHelper* splineHelper)
{
	Impl->SetSelectedSpline(splineHelper);
}

void AITwinSplineTool::SetSelectedPointIndex(int32 pointIndex)
{
	Impl->SetSelectedPointIndex(pointIndex);
}

bool AITwinSplineTool::HasSelectionImpl() const
{
	return Impl->HasSelection();
}

bool AITwinSplineTool::HasSelectedPoint() const
{
	return Impl->HasSelectedPoint();
}

bool AITwinSplineTool::HasSplines() const
{
	return Impl->splinesManager && Impl->splinesManager->HasSplines();
}

void AITwinSplineTool::DeleteSelectionImpl()
{
	Impl->DeleteSelection();
}

void AITwinSplineTool::DeleteSelectedSpline()
{
	Impl->DeleteSelectedSpline();
}

bool AITwinSplineTool::CanDeletePoint() const
{
	return Impl->CanDeletePoint();
}

void AITwinSplineTool::DeleteSelectedPoint()
{
	Impl->DeleteSelectedPoint();
}

void AITwinSplineTool::DuplicateSelectedPoint()
{
	Impl->DuplicateSelectedPoint();
}

void AITwinSplineTool::EnableDuplicationWhenMovingPoint(bool value)
{
	Impl->EnableDuplicationWhenMovingPoint(value);
}

FTransform AITwinSplineTool::GetSelectionTransformImpl() const
{
	return Impl->GetSelectionTransform();
}

void AITwinSplineTool::SetSelectionTransformImpl(const FTransform& transform)
{
	Impl->SetSelectionTransform(transform);
}

void AITwinSplineTool::SetEnabledImpl(bool bValue)
{
	Impl->SetEnabled(bValue);
}

bool AITwinSplineTool::IsEnabledImpl() const
{
	return Impl->IsEnabled();
}

void AITwinSplineTool::ResetToDefaultImpl()
{
	Impl->ResetToDefault();
}

bool AITwinSplineTool::DoMouseClickActionImpl()
{
	return Impl->DoMouseClickAction();
}

bool AITwinSplineTool::AddSpline(FVector const& Position)
{
	return Impl->AddSpline(Position);
}

bool AITwinSplineTool::LoadSpline(const std::shared_ptr<AdvViz::SDK::ISpline>& spline,
	FITwinTilesetAccess* CutoutTargetAccess /*= nullptr*/)
{
	Be::CleanUpGuard restoreGuard([this, bIsLoadingSplineOld = Impl->bIsLoadingSpline]
	{
		Impl->bIsLoadingSpline = bIsLoadingSplineOld;
	});
	Impl->bIsLoadingSpline = true;
	SetCutoutTarget(CutoutTargetAccess);
	return Impl->LoadSpline(spline);
}

void AITwinSplineTool::SetSplinesManager(const std::shared_ptr<AdvViz::SDK::ISplinesManager>& splinesManager)
{
	Impl->splinesManager = splinesManager;
}

bool AITwinSplineTool::GetSplineReferencePosition(FVector& RefLocation, FBox& OutBox) const
{
	return Impl->GetSplineReferencePosition(RefLocation, OutBox);
}

EITwinSplineToolMode AITwinSplineTool::GetMode() const
{
	return Impl->GetMode();
}

void AITwinSplineTool::SetMode(EITwinSplineToolMode NewMode)
{
	Impl->SetMode(NewMode);
}

void AITwinSplineTool::ToggleInteractiveCreationMode()
{
#if WITH_EDITOR
	Impl->ToggleInteractiveCreationMode();
#endif
}

EITwinSplineUsage AITwinSplineTool::GetUsage() const
{
	return Impl->GetUsage();
}

void AITwinSplineTool::SetUsage(EITwinSplineUsage NewUsage)
{
	Impl->SetUsage(NewUsage);
}

void AITwinSplineTool::SetCutoutTarget(FITwinTilesetAccess* CutoutTargetAccess)
{
	ensure(CutoutTargetAccess == nullptr
		|| GetUsage() == EITwinSplineUsage::MapCutout
		|| (GetUsage() == EITwinSplineUsage::Undefined && Impl->bIsLoadingSpline));

	CutoutTarget = nullptr;
	Impl->CutoutTargetIdentifier.reset();

	if (CutoutTargetAccess)
	{
		CutoutTarget = CutoutTargetAccess->GetMutableTileset();
		if (CutoutTarget.IsValid())
		{
			// Create the cut-out overlay now, to avoid letting the iModel or reality data disappear when
			// we start drawing the first cut-out polygon.
			ITwin::InitCutoutOverlay(*CutoutTarget);
		}
		// Store identifier used for persistence in the decoration service.
		Impl->CutoutTargetIdentifier = CutoutTargetAccess->GetDecorationKey();
	}
}

EITwinTangentMode AITwinSplineTool::GetTangentMode() const
{
	return Impl->GetTangentMode();
}

void AITwinSplineTool::SetTangentMode(EITwinTangentMode TangentMode)
{
	Impl->SetTangentMode(TangentMode);
}

void AITwinSplineTool::RefreshScene()
{
	Impl->RefreshScene();
}

void AITwinSplineTool::SetPopulationTool(AITwinPopulationTool* InPopulationTool)
{
	PopulationTool = InPopulationTool;
}
