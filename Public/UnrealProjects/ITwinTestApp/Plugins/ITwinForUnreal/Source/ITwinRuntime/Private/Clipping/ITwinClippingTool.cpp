/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingTool.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Clipping/ITwinClippingTool.h>

#include <CesiumGlobeAnchorComponent.h>
#include <CesiumPolygonRasterOverlay.h>
#include <Clipping/ITwinBoxTileExcluder.h>
#include <Clipping/ITwinClipping3DTilesetHelper.h>
#include <Clipping/ITwinClippingMPCHolder.h>
#include <Clipping/ITwinPlaneTileExcluder.h>
#include <Helpers/ITwinConsoleCommandUtils.inl>
#include <Helpers/ITwinMathUtils.h>
#include <Helpers/ITwinTracingHelper.h>
#include <Helpers/WorldSingleton.h>
#include <ITwinGeolocation.h>
#include <ITwinGoogle3DTileset.h>
#include <ITwinIModel.h>
#include <ITwinRealityData.h>
#include <ITwinTilesetAccess.h>
#include <ITwinUtilityLibrary.h>
#include <Math/UEMathConversion.h>
#include <Population/ITwinPopulation.h>
#include <Population/ITwinPopulationTool.h>
#include <Spline/ITwinSplineHelper.h>
#include <Spline/ITwinSplineTool.h>

// UE headers
#include <Blueprint/WidgetLayoutLibrary.h>
#include <DrawDebugHelpers.h>
#include <EngineUtils.h> // for TActorIterator<>
#include <Engine/StaticMeshActor.h>
#include <Materials/MaterialParameterCollection.h>
#include <Materials/MaterialParameterCollectionInstance.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Compil/EnumSwitchCoverage.h>
#	include <Core/Tools/Log.h>
#	include <glm/matrix.hpp>
#	include <glm/gtc/matrix_access.hpp>
#	include <glm/gtx/compatibility.hpp>
#include <Compil/AfterNonUnrealIncludes.h>

// Between iTwin Engage LA and GA, we made an attempt to let the user pick the rotation center for cutout
// planes, but we abandoned the idea (lack of feedback for the user, and possibility to get the same with
// the recenter icon, in a much clearer way...)
#define HAS_ROTATION_CENTER_PICKING_MODE() 0

namespace ITwin
{
	int32 GetLinkedTilesets(AITwinSplineTool::TilesetAccessArray& OutArray,
		AdvViz::SDK::ISplinePtr const& Spline, const UWorld* World);
	bool GetRayToTraceFromScreenCenter(const UObject* WorldContextObject, FVector& OutTraceStart, FVector& OutTraceDir);
}

namespace ITwinClippingDetails
{
	/// Miscellaneous info about the camera view, that we can use to adapt the clipping plane proxy scale, so
	/// that the gizmo is always visible and usable, even when the plane is very far from the camera.
	struct FCameraViewInfo
	{
		FVector CameraLocation = FVector::ZeroVector;
		FVector ViewDirection = FVector::ZeroVector;
		double FovScaling = 1.0;
		double DPIScaling = 1.0;

		FCameraViewInfo() = default;
		FCameraViewInfo(FVector const& InCameraLocation, FVector const& InViewDirection, double InFovScaling, double InDPIScaling)
			: CameraLocation(InCameraLocation)
			, ViewDirection(InViewDirection)
			, FovScaling(InFovScaling)
			, DPIScaling(InDPIScaling)
		{
		}

		bool Equals(FCameraViewInfo const& Other, double Tolerance = 1e-5) const
		{
			return CameraLocation.Equals(Other.CameraLocation, Tolerance)
				&& ViewDirection.Equals(Other.ViewDirection, Tolerance)
				&& FMath::Abs(FovScaling - Other.FovScaling) <= Tolerance
				&& FMath::Abs(DPIScaling - Other.DPIScaling) <= Tolerance;
		}
	};

	std::optional<FCameraViewInfo> GetCameraViewInfo(UWorld const* World)
	{
		APlayerController const* pController = World ? World->GetFirstPlayerController() : nullptr;
		if (!pController || !pController->PlayerCameraManager)
		{
			return {};
		}
		bool const bOrtho = pController->PlayerCameraManager->IsOrthographic();
		if (bOrtho)
		{
			// TODO_JDE - orthographic camera
			BE_ISSUE("Clipping planes automatic scale not implemented in orthographic view.");
			return {};
		}
		return FCameraViewInfo(
			pController->PlayerCameraManager->GetCameraLocation(),
			pController->PlayerCameraManager->GetActorForwardVector(),
			FMath::Tan(FMath::DegreesToRadians(pController->PlayerCameraManager->GetFOVAngle()) * 0.5),
			UWidgetLayoutLibrary::GetViewportScale(World)
		);
	}
}

class AITwinClippingTool::FImpl
{
public:
	AITwinClippingTool& Owner;

	TArray<FITwinClippingPlaneInfo> ClippingPlaneInfos;
	TArray<FITwinClippingBoxInfo> ClippingBoxInfos;
	TArray<FITwinClippingCartographicPolygonInfo> ClippingPolygonInfos;

	TWeakObjectPtr<AITwinPopulationTool> PopulationTool;
	TWeakObjectPtr<AITwinSplineTool> SplineTool;

	// Store whether the removal event was initiated by Unreal (delete key in 3D viewport) or iTwin Studio
	// (trash icon in Cutout Property Page).
	enum class ERemovalInitiator : uint8_t
	{
		Unreal,
		ITS
	};
	std::optional<ERemovalInitiator> RemovalInitiatorOpt;

	/// We store the chosen transformation mode for cutout primitives here, because the user can modify the
	/// transformation mode of the population tool for another purpose (3D Object edition), and we want to
	/// be able to restore the right mode when enabling the cutout tool again.
	std::optional<ETransformationMode> TransformationModeOpt;

#if !HAS_ROTATION_CENTER_PICKING_MODE()
	const
#endif
	/// When entering rotation mode for an (infinite) cutout plane, the user should first pick the exact
	/// rotation center.
	bool bIsRotationCenterPickingMode = false;

	/// Additional information about the camera view and selection when entering interactive transformation.
	using FCameraViewInfo = ITwinClippingDetails::FCameraViewInfo;
	std::optional<FCameraViewInfo> LastViewInfo;
	std::optional<int32> LastSelectedPlaneIndex;


	inline int32 NumEffects(EITwinClippingPrimitiveType Type) const;

	inline
	FITwinClippingInfoBase& GetMutableClippingEffect(EITwinClippingPrimitiveType Type, int32 Index);

	inline
	const FITwinClippingInfoBase& GetClippingEffect(EITwinClippingPrimitiveType Type, int32 Index) const;

	bool RemoveEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bTriggeredFromITS);

	/// For communication with iTwin Studio.
	AdvViz::SDK::RefID GetEffectId(EITwinClippingPrimitiveType EffectType, int32 EffectIndex) const;
	int32 GetEffectIndex(EITwinClippingPrimitiveType EffectType, AdvViz::SDK::RefID const& RefID) const;

	/// Return whether the given effect is enabled.
	bool IsEffectEnabled(EITwinClippingPrimitiveType EffectType, int32 Index) const;
	/// Switches the given effect on or off.
	void EnableEffect(EITwinClippingPrimitiveType EffectType, int32 Index, bool bInEnabled);

	void EnableAllEffectsOfType(EITwinClippingPrimitiveType Type, bool bInEnabled);

	/// Return whether the given effect should influence the given model.
	bool ShouldEffectInfluenceModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		const ITwin::ModelLink& ModelIdentifier) const;
	/// Return whether the given effect should influence the given model type globally.
	bool ShouldEffectInfluenceFullModelType(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		EITwinModelType ModelType) const;
	void SetEffectInfluenceFullModelType(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		EITwinModelType ModelType, bool bAll);
	void SetEffectInfluenceSpecificModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		const ITwin::ModelLink& ModelIdentifier, bool bInfluence);

	/// Retrieve the Material Parameter Collection for clipping.
	UMaterialParameterCollection* GetMPCClipping();
	UMaterialParameterCollectionInstance* GetMPCClippingInstance();

	bool GetInvertEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex) const;
	void SetInvertEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bInvert);

	bool EncodeFlippingInMPC(EITwinClippingPrimitiveType Type);

	template <typename PrimitiveInfo, EITwinClippingPrimitiveType PrimitiveType>
	bool TEncodeFlippingInMPC(TArray<PrimitiveInfo> const& ClippingInfos);


	std::optional<FEffectIdentifier> GetSelectedEffect() const;
	bool SelectEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bEnterIsolationMode = true);

	/// Zoom in on the effect of given type and index.
	void ZoomOnEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex);

	/// Make the Population Tool the active tool, with usage restricted to cutout primitives.
	TWeakObjectPtr<AITwinPopulationTool> ActivatePopulationTool(UWorld* World, bool bUpdateTransformationMode = true);

	AITwinPopulation const* GetSelectedPopulation(int32& OutSelectedInstanceIndex) const;

	/// Select a cutout primitive in Population Tool, or reset selection if Population is null.
	void SelectPopulationInstance(AITwinPopulation* Population,	int32 InstanceIndex, UWorld* World);

	/// Deletes the selected instance, if any.
	void DeleteSelectedPopulationInstance();

	/// Make the Spline Tool the active tool, with usage restricted restricted to cutout polygons.
	TWeakObjectPtr<AITwinSplineTool> ActivateSplineTool(UWorld* World);

	/// Select a spline, or reset selection if InSplineHelper is null.
	void SelectSpline(AITwinSplineHelper* InSplineHelper, UWorld* World);

	void SetTransformationMode(ETransformationMode Mode);
	bool IsRotationMode() const {
		return TransformationModeOpt && TransformationModeOpt.value() == ETransformationMode::Rotate;
	}


	void SetRotationCenterPickingMode(bool b);
	void QuitRotationCenterPickingMode() { SetRotationCenterPickingMode(false); }

	struct [[nodiscard]] FScopedRemovalContext
	{
		FImpl& Impl;

		FScopedRemovalContext(FImpl& InImpl, ERemovalInitiator RemovalInitiator)
			: Impl(InImpl)
		{
			Impl.RemovalInitiatorOpt.emplace(RemovalInitiator);
		}

		~FScopedRemovalContext()
		{
			Impl.RemovalInitiatorOpt.reset();
		}
	};

	inline const TWeakObjectPtr<AITwinPopulation>& GetClippingEffectPopulation(EITwinClippingPrimitiveType Type) const
	{
		ensure(Type != EITwinClippingPrimitiveType::Polygon);
		return (Type == EITwinClippingPrimitiveType::Box) ? Owner.ClippingBoxPopulation : Owner.ClippingPlanePopulation;
	}

	/// Pre-load populations used for cutout effects.
	uint32 PreLoadClippingPrimitives(AITwinPopulationTool& PopulationTool);

	/// Update the given tileset by activating the different clipping effects to it, and deactivating any
	/// effect that should no longer affect it.
	void UpdateTileset(FITwinTilesetAccess const& TilesetAccess,
		std::optional<EITwinClippingPrimitiveType> const& SpecificPrimitiveType = std::nullopt);
	/// Update clipping effects in all tilesets in the scene.
	void UpdateAllTilesets(std::optional<EITwinClippingPrimitiveType> const& SpecificPrimitiveType = std::nullopt);

	struct FTilesetUpdateInfo;
	void UpdateTileset_Planes(ACesium3DTileset& Tileset, ITwin::ModelLink const& ModelIdentifier,
		FTilesetUpdateInfo& UpdateInfo);
	void UpdateTileset_Boxes(ACesium3DTileset& Tileset, ITwin::ModelLink const& ModelIdentifier,
		FTilesetUpdateInfo& UpdateInfo);
	void UpdateTileset_Polygons(FITwinTilesetAccess const& TilesetAccess,
		FTilesetUpdateInfo& UpdateInfo);

	bool UpdateClippingPrimitiveFromUEInstance(EITwinClippingPrimitiveType Type, int32 InstanceIndex);

	template <typename Func>
	void VisitClippingPrimitivesOfType(EITwinClippingPrimitiveType Type, Func const& Fun);

	template <EITwinClippingPrimitiveType PrimitiveType>
	bool TStartInteractivePrimitiveInstanceCreation();

	void OnClippingInstanceAdded(AITwinPopulation* Population, EITwinInstantiatedObjectType ObjectType, int32 InstanceIndex);

	template <typename PrimitiveInfo, EITwinClippingPrimitiveType PrimitiveType>
	bool TAddClippingPrimitive(TArray<PrimitiveInfo>& ClippingInfos, int32 InstanceIndex);

	template <typename PrimitiveInfo, EITwinClippingPrimitiveType PrimitiveType>
	void TUpdateAllClippingPrimitives(TArray<PrimitiveInfo>& ClippingInfos);


	/// Update the plane equation in all tile excluders matching the modified actor, and update it in the
	/// material parameter collection.
	bool UpdateClippingPlaneEquationFromUEInstance(int32 InstanceIndex);

	/// Retrieve the plane equation from the given instance.
	template <typename T>
	bool GetPlaneEquationFromUEInstance(UE::Math::TVector<T>& OutPlaneOrientation, T& OutPlaneW, int32 InInstanceIndex) const;

	void UpdateAllClippingPlanes();

	/// Update the box 3D information in all tile excluders created for the clipping box, as well as in the
	/// material parameter collection.
	bool UpdateClippingBoxFromUEInstance(int32 InstanceIndex);

	/// Retrieve the box 3D information from the given instance.
	bool GetBoxTransformInfoFromUEInstance(glm::dmat3x3& OutMatrix, glm::dvec3& OutTranslation, int32 InInstanceIndex) const;

	void UpdateAllClippingBoxes();


	bool RegisterCutoutSpline(AITwinSplineHelper* SplineHelper);
	bool DeRegisterCutoutSpline(AITwinSplineHelper* SplineBeingRemoved);

	/// Change all effect proxies visibility in the viewport (without deactivating them).
	/// This affects translucent boxes/planes as well as spline meshes displayed for cutout polygons.
	void SetAllEffectProxiesVisibility(bool bVisibleInGame);
	void HideAllEffectProxies() { SetAllEffectProxiesVisibility(false); }

	/// Show/Hide effect proxies for the given cutout type.
	void SetEffectVisibility(EITwinClippingPrimitiveType EffectType, bool bVisibleInGame, bool bIsolationMode = false);
	void ShowOnlyProxiesOfType(EITwinClippingPrimitiveType SelectedType, bool bIsolationMode);

	/// Returns whether the proxies of the given type are visible.
	bool IsEffectProxyVisible(EITwinClippingPrimitiveType Type) const;

	/// Update the instance properties to manage persistence.
	void UpdateAVizInstanceProperties(EITwinClippingPrimitiveType Type, int32 InstanceIndex) const;
	/// Apply properties from the loaded instance.
	void UpdateClippingPropertiesFromAVizInstance(EITwinClippingPrimitiveType Type, int32 InstanceIndex);


	/// Modify the location of the selected cutout polygon point, if any.
	void SetPolygonPointLocation(int32 PolygonIndex, int32 PointIndex, double Latitude, double Longitude) const;

	bool GetEffectTransform(EITwinClippingPrimitiveType EffectType, int32 Index,
		FTransform& OutTransform, double& OutLatitude, double& OutLongitude, double& OutElevation) const;

	template <typename FTransfoBuilderFunc>
	void ModifyEffectTransformation(FTransfoBuilderFunc const& Func,
		EITwinClippingPrimitiveType Type,
		int32 PrimitiveIndex,
		bool bTriggeredFromITS,
		bool bOnlyModifyProxy = false) const;

	bool RecenterPlaneProxyAtRayIntersection(int32 PlaneIndex,
		FVector const& TraceStart,
		FVector const& TraceDir,
		bool bClampToInfluenceBounds);
	bool RecenterPlaneProxyFromPicking(int32 PlaneIndex);

	void InvalidateBoundingBoxOfClippingPlanes(ITwin::ModelLink const& ModelLink);

	void Tick(float DeltaTime);

	/// Returns cut-out value (understood as opacity: 0 if cut out, 1 if not) for box primitives at given
	/// point.
	double GetClippingValue_Boxes(FVector const& AbsoluteWorldPosition, ITwin::ModelLink const& ModelIdentifier) const;

	/// Returns cut-out value (understood as opacity: 0 if cut out, 1 if not) for plane primitives at given
	/// point.
	double GetClippingValue_Planes(FVector const& AbsoluteWorldPosition, ITwin::ModelLink const& ModelIdentifier) const;


#if WITH_EDITOR
	/// Globally activate/deactivate all effects of given type, at given level. This is for debugging, only
	/// possible in Editor.
	void ActivateEffects(EITwinClippingPrimitiveType Type, EITwinClippingEffectLevel Level, bool bActivate);
#endif // WITH_EDITOR
};

TWeakObjectPtr<AITwinPopulationTool> AITwinClippingTool::FImpl::ActivatePopulationTool(UWorld* World,
	bool bUpdateTransformationMode /*= true*/)
{
	if (ensure(PopulationTool.IsValid()))
	{
		if (!PopulationTool->IsEnabled())
		{
			AITwinInteractiveTool::DisableAll(World);
			PopulationTool->SetEnabled(true);
		}
		PopulationTool->SetUsedOnCutout(true);
		PopulationTool->ResetToDefault();
		if (bUpdateTransformationMode && TransformationModeOpt)
		{
			PopulationTool->SetTransformationMode(*TransformationModeOpt);
		}
	}
	return PopulationTool;
}

AITwinPopulation const* AITwinClippingTool::FImpl::GetSelectedPopulation(int32& OutSelectedInstanceIndex) const
{
	AITwinPopulation const* SelectedPopulation = nullptr;
	OutSelectedInstanceIndex = INDEX_NONE;
	if (ensure(PopulationTool.IsValid()))
	{
		SelectedPopulation = PopulationTool->GetSelectedPopulation();
		if (SelectedPopulation)
		{
			OutSelectedInstanceIndex = PopulationTool->GetSelectedInstanceIndex();
		}
	}
	return SelectedPopulation;
}

void AITwinClippingTool::FImpl::SelectPopulationInstance(AITwinPopulation* Population,
	int32 InstanceIndex,
	UWorld* World)
{
	bool const bUpdateTransformationMode = (Population != nullptr);
	auto const PopTool = ActivatePopulationTool(World, bUpdateTransformationMode);
	if (PopTool.IsValid())
	{
		PopTool->SetSelectedPopulation(Population);
		PopTool->SetSelectedInstanceIndex(InstanceIndex);
		PopTool->SelectionChangedEvent.Broadcast();
	}
}

void AITwinClippingTool::FImpl::DeleteSelectedPopulationInstance()
{
	if (ensure(PopulationTool.IsValid()))
	{
		ensure(PopulationTool->IsUsedOnCutoutPrimitive());
		PopulationTool->DeleteSelectedInstance();
	}
}

TWeakObjectPtr<AITwinSplineTool> AITwinClippingTool::FImpl::ActivateSplineTool(UWorld* World)
{
	if (ensure(SplineTool.IsValid()))
	{
		bool bNeedEnableSplineTool = false;
		if (SplineTool->IsEnabled())
		{
			bNeedEnableSplineTool = SplineTool->GetUsage() != EITwinSplineUsage::MapCutout;
		}
		else
		{
			AITwinInteractiveTool::DisableAll(World);
			bNeedEnableSplineTool = true;
		}
		if (bNeedEnableSplineTool)
		{
			ITwin::EnableSplineTool(World, true, EITwinSplineUsage::MapCutout, {}, true/*bAutomaticCutoutTarget*/);
		}
	}
	return SplineTool;
}

void AITwinClippingTool::FImpl::SelectSpline(AITwinSplineHelper* SplineHelper, UWorld* World)
{
	if (!ensure(SplineTool.IsValid()))
		return;
	if (SplineHelper)
	{
		AITwinInteractiveTool::DisableAll(World);

		AITwinSplineTool::TilesetAccessArray CutoutTargets;
		ITwin::GetLinkedTilesets(CutoutTargets, SplineHelper->GetAVizSpline(), World);

		ITwin::EnableSplineTool(World, true, EITwinSplineUsage::MapCutout, std::move(CutoutTargets));
		SplineTool->SetSelectedSpline(SplineHelper);
	}
	else
	{
		// Deselect
		SplineTool->SetSelectedSpline(nullptr);
	}
}

void AITwinClippingTool::FImpl::SetTransformationMode(ETransformationMode Mode)
{
	TransformationModeOpt = Mode;

	if (ensure(PopulationTool.IsValid()))
	{
		PopulationTool->SetTransformationMode(Mode);
	}

	bool bEnterCenterPickingMode = false;
#if HAS_ROTATION_CENTER_PICKING_MODE()
	// For cutout plane, the rotation center needs to be chosen before enabling the rotation gizmo.
	if (Mode == ETransformationMode::Rotate)
	{
		int32 Index(INDEX_NONE);
		auto const SelectedPopulation = GetSelectedPopulation(Index);
		if (SelectedPopulation
			&& SelectedPopulation->GetObjectType() == EITwinInstantiatedObjectType::ClippingPlane)
		{
			bEnterCenterPickingMode = true;
		}
	}
	if (bEnterCenterPickingMode)
	{
		SetRotationCenterPickingMode(true);
	}
	else if (this->bIsRotationCenterPickingMode)
	{
		SetRotationCenterPickingMode(false);
	}
#endif // HAS_ROTATION_CENTER_PICKING_MODE
}

void AITwinClippingTool::FImpl::SetRotationCenterPickingMode(bool b)
{
#if HAS_ROTATION_CENTER_PICKING_MODE()
	bIsRotationCenterPickingMode = b;
#endif

	// Propagate the option to the population tool (for the display of the gizmo)
	if (PopulationTool.IsValid())
	{
		PopulationTool->SetRotationCenterPickingMode(bIsRotationCenterPickingMode);
	}
}


AITwinClippingTool::AITwinClippingTool()
	: Impl(MakePimpl<FImpl>(*this))
{
	this->ClippingMPCHolder = CreateDefaultSubobject<UITwinClippingMPCHolder>(TEXT("MPC_Holder"));

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
}

void AITwinClippingTool::ConnectPopulationTool(AITwinPopulationTool* PopulationTool)
{
	Impl->PopulationTool = PopulationTool;
	if (PopulationTool)
	{
		Impl->PreLoadClippingPrimitives(*PopulationTool);

		PopulationTool->InteractiveCreationAbortedEvent.AddUniqueDynamic(this, &AITwinClippingTool::OnItemCreationAbortedInTool);
	}
}

void AITwinClippingTool::ConnectSplineTool(AITwinSplineTool* SplineTool)
{
	Impl->SplineTool = SplineTool;
	if (SplineTool)
	{
		SplineTool->SplineAddedEvent.AddUniqueDynamic(this, &AITwinClippingTool::OnSplineHelperAdded);
		SplineTool->SplineBeforeRemovedEvent.AddUniqueDynamic(this, &AITwinClippingTool::OnSplineHelperRemoved);
		SplineTool->InteractiveCreationAbortedEvent.AddUniqueDynamic(this, &AITwinClippingTool::OnItemCreationAbortedInTool);
	}
}

UMaterialParameterCollection* AITwinClippingTool::FImpl::GetMPCClipping()
{
	return Owner.ClippingMPCHolder->GetMPCClipping();
}

UMaterialParameterCollectionInstance* AITwinClippingTool::FImpl::GetMPCClippingInstance()
{
	UWorld* World = Owner.GetWorld();
	if (ensure(World))
	{
		return World->GetParameterCollectionInstance(GetMPCClipping());
	}
	return nullptr;
}

void AITwinClippingTool::RegisterTileset(FITwinTilesetAccess const& TilesetAccess)
{
	// When a new tileset is created, automatically apply global clipping effects to it, if any.
	Impl->UpdateTileset(TilesetAccess);

	// If some cutting planes are meant to influence this tileset, invalidate their bounding box (used for
	// automatic recentering).
	Impl->InvalidateBoundingBoxOfClippingPlanes(TilesetAccess.GetModelLink());

	// Also make sure those bounding boxes are invalidated as soon as the tileset is transformed.
	ACesium3DTileset* TilesetPtr = TilesetAccess.GetMutableTileset();
	if (TilesetPtr && TilesetPtr->GetRootComponent())
	{
		TilesetPtr->GetRootComponent()->TransformUpdated.AddLambda(
			[this, ModelLink = TilesetAccess.GetModelLink()](USceneComponent*, EUpdateTransformFlags, ETeleportType)
		{
			// Invalidate bounding box of all clipping plane effects influencing this tileset.
			Impl->InvalidateBoundingBoxOfClippingPlanes(ModelLink);
		});
	}
}

void AITwinClippingTool::OnModelRemoved(const ITwin::ModelLink& ModelIdentifier)
{
	Impl->InvalidateBoundingBoxOfClippingPlanes(ModelIdentifier);
}

namespace
{
	template <class TTilesetOwner>
	inline UITwinClipping3DTilesetHelper* TGetClippingHelper(AActor const* TilesetOwnerActor)
	{
		if (TilesetOwnerActor && ensure(TilesetOwnerActor->IsA(TTilesetOwner::StaticClass())))
		{
			return Cast<TTilesetOwner const>(TilesetOwnerActor)->GetClippingHelper();
		}
		return nullptr;
	}

	template <class TTilesetOwner>
	inline UITwinClipping3DTilesetHelper* TMakeClippingHelper(AActor* TilesetOwnerActor)
	{
		if (TilesetOwnerActor && ensure(TilesetOwnerActor->IsA(TTilesetOwner::StaticClass())))
		{
			TTilesetOwner* TilesetOwner = Cast<TTilesetOwner>(TilesetOwnerActor);
			TilesetOwner->MakeClippingHelper();
			return TilesetOwner->GetClippingHelper();
		}
		return nullptr;
	}


	UITwinClipping3DTilesetHelper* GetClippingHelper(ACesium3DTileset const& Tileset, ITwin::ModelLink const& ModelIdentifier)
	{
		// TODO_JDE: Possible coding improvement: share more code between AITwinIModel, AITwinRealityData and
		// AITwinGoogle3DTileset to avoid this kind of duplication...

		switch (ModelIdentifier.first)
		{
		case EITwinModelType::GlobalMapLayer:
			return TGetClippingHelper<AITwinGoogle3DTileset>(&Tileset);
		case EITwinModelType::IModel:
			return TGetClippingHelper<AITwinIModel>(Tileset.GetOwner());
		case EITwinModelType::RealityData:
			return TGetClippingHelper<AITwinRealityData>(Tileset.GetOwner());

		default:
			break;
		}
		// This tileset is not related to iTwin
		return nullptr;
	}

	UITwinClipping3DTilesetHelper* MakeClippingHelper(ACesium3DTileset& Tileset, ITwin::ModelLink const& ModelIdentifier)
	{
		// TODO_JDE: Possible coding improvement: share more code between AITwinIModel, AITwinRealityData and
		// AITwinGoogle3DTileset to avoid this kind of duplication...

		switch (ModelIdentifier.first)
		{
		case EITwinModelType::GlobalMapLayer:
			return TMakeClippingHelper<AITwinGoogle3DTileset>(&Tileset);
		case EITwinModelType::IModel:
			return TMakeClippingHelper<AITwinIModel>(Tileset.GetOwner());
		case EITwinModelType::RealityData:
			return TMakeClippingHelper<AITwinRealityData>(Tileset.GetOwner());

		default:
			break;
		}
		// This tileset is not related to iTwin
		return nullptr;
	}
}

struct AITwinClippingTool::FImpl::FTilesetUpdateInfo
{
	uint32 AddedExcluders = 0;
	uint32 ActiveEffectsInTileset = 0;
};

void AITwinClippingTool::FImpl::UpdateTileset_Planes(ACesium3DTileset& Tileset, ITwin::ModelLink const& ModelIdentifier,
	FTilesetUpdateInfo& UpdateInfo)
{
	uint32& AddedExcluders(UpdateInfo.AddedExcluders);
	uint32& ActiveEffectsInTileset(UpdateInfo.ActiveEffectsInTileset);

	const auto DeactivateNotMatchedExcluders = [](TArray<UActorComponent*> const& ExistingExcluders,
		std::set<UActorComponent*> const& MatchedExcluders)
	{
		for (UActorComponent* Excluder : ExistingExcluders)
		{
			if (!MatchedExcluders.contains(Excluder))
			{
				UCesiumTileExcluder* TileExcluder = Cast<UCesiumTileExcluder>(Excluder);
				if (TileExcluder)
				{
					TileExcluder->Deactivate();
				}
			}
		}
	};

	// Handle clipping planes.
	TArray<UActorComponent*> ExistingPlaneTileExcluders = Tileset.K2_GetComponentsByClass(UITwinPlaneTileExcluder::StaticClass());
	std::set<UActorComponent*> MatchedPlaneExcluders;

	for (int32 PlaneIndex = 0; PlaneIndex < ClippingPlaneInfos.Num(); PlaneIndex++)
	{
		if (!ShouldEffectInfluenceModel(EITwinClippingPrimitiveType::Plane, PlaneIndex, ModelIdentifier))
			continue;

		FVector3d PlaneOrientation = FVector3d::ZAxisVector;
		double PlaneW(0.);
		if (!GetPlaneEquationFromUEInstance(PlaneOrientation, PlaneW, PlaneIndex))
			break;

		ActiveEffectsInTileset++;

		// Create a tile excluder for this plane if it does not exist.
		UITwinPlaneTileExcluder* TileExcluderForPlane = nullptr;
		for (UActorComponent* CandidateComponent : ExistingPlaneTileExcluders)
		{
			UITwinPlaneTileExcluder* TileExcluder = Cast<UITwinPlaneTileExcluder>(CandidateComponent);
			if (TileExcluder && TileExcluder->PlaneIndex == PlaneIndex)
			{
				TileExcluderForPlane = TileExcluder;
				MatchedPlaneExcluders.insert(CandidateComponent);
				break;
			}
		}

		if (TileExcluderForPlane == nullptr)
		{
			TileExcluderForPlane = Cast<UITwinPlaneTileExcluder>(
				Tileset.AddComponentByClass(UITwinPlaneTileExcluder::StaticClass(), true,
					FTransform::Identity, false));
			if (ensure(TileExcluderForPlane))
			{
				auto& PlaneInfo = ClippingPlaneInfos[PlaneIndex];
				PlaneInfo.TileExcluders.Add(TileExcluderForPlane);

				TileExcluderForPlane->PlaneIndex = PlaneIndex;
				TileExcluderForPlane->PlaneEquation.PlaneOrientation = PlaneOrientation;
				TileExcluderForPlane->PlaneEquation.PlaneW = PlaneW;
				TileExcluderForPlane->SetInvertEffect(PlaneInfo.bInvertEffect);

				TileExcluderForPlane->SetFlags(
					RF_Transient | RF_DuplicateTransient | RF_TextExportTransient);

				Tileset.AddInstanceComponent(TileExcluderForPlane);
				AddedExcluders++;
			}
		}
	}
	// deactivate obsolete tile excluders
	DeactivateNotMatchedExcluders(ExistingPlaneTileExcluders, MatchedPlaneExcluders);
}

void AITwinClippingTool::FImpl::UpdateTileset_Boxes(ACesium3DTileset& Tileset, ITwin::ModelLink const& ModelIdentifier,
	FTilesetUpdateInfo& UpdateInfo)
{
	uint32& AddedExcluders(UpdateInfo.AddedExcluders);
	uint32& ActiveEffectsInTileset(UpdateInfo.ActiveEffectsInTileset);

	// Handle clipping boxes.
	// Since we need to aggregate all boxes for the tile exclusion criteria, we just have one
	// excluder for all boxes.
	TArray<UActorComponent*> ExistingBoxTileExcluders = Tileset.K2_GetComponentsByClass(UITwinBoxTileExcluder::StaticClass());
	UITwinBoxTileExcluder* TileExcluderForBoxes = nullptr;
	for (UActorComponent* CandidateComponent : ExistingBoxTileExcluders)
	{
		UITwinBoxTileExcluder* TileExcluder = Cast<UITwinBoxTileExcluder>(CandidateComponent);
		if (TileExcluder)
		{
			TileExcluderForBoxes = TileExcluder;
			break;
		}
	}
	bool bUseBoxExcluder = false;
	bool bIsNewBoxExcluder = false;


	// Append clipping box information to the excluder, if needed.
	for (int32 BoxIndex = 0; BoxIndex < ClippingBoxInfos.Num(); BoxIndex++)
	{
		if (!ShouldEffectInfluenceModel(EITwinClippingPrimitiveType::Box, BoxIndex, ModelIdentifier))
			continue;
		ActiveEffectsInTileset++;
		bUseBoxExcluder = true;
		// Create one tile excluder for all active boxes if needed:
		if (TileExcluderForBoxes == nullptr)
		{
			TileExcluderForBoxes = Cast<UITwinBoxTileExcluder>(
				Tileset.AddComponentByClass(UITwinBoxTileExcluder::StaticClass(), true,
					FTransform::Identity, false));
			if (ensure(TileExcluderForBoxes))
			{
				bIsNewBoxExcluder = true;
				TileExcluderForBoxes->SetFlags(
					RF_Transient | RF_DuplicateTransient | RF_TextExportTransient);
			}
		}

		auto& BoxInfo = ClippingBoxInfos[BoxIndex];
		if (ensure(TileExcluderForBoxes)
			&& !TileExcluderForBoxes->ContainsBox(BoxInfo.BoxProperties))
		{
			TileExcluderForBoxes->BoxPropertiesArray.push_back(
				BoxInfo.BoxProperties);
			BoxInfo.TileExcluders.Add(TileExcluderForBoxes);
		}
	}
	if (bIsNewBoxExcluder && ensure(TileExcluderForBoxes))
	{
		ensure(bUseBoxExcluder);
		Tileset.AddInstanceComponent(TileExcluderForBoxes);
		AddedExcluders++;
	}
	else if (!bUseBoxExcluder && TileExcluderForBoxes)
	{
		TileExcluderForBoxes->Deactivate();
	}
}

void AITwinClippingTool::FImpl::UpdateTileset_Polygons(FITwinTilesetAccess const& TilesetAccess,
	FTilesetUpdateInfo& UpdateInfo)
{
	uint32& ActiveEffectsInTileset(UpdateInfo.ActiveEffectsInTileset);
	auto const ModelIdentifier = TilesetAccess.GetDecorationKey();
	for (int32 Index = 0; Index < ClippingPolygonInfos.Num(); Index++)
	{
		auto& PolygonInfo = ClippingPolygonInfos[Index];
		if (!PolygonInfo.SplineHelper.IsValid())
			continue;
		const bool bActivate = ShouldEffectInfluenceModel(EITwinClippingPrimitiveType::Polygon, Index, ModelIdentifier);
		PolygonInfo.SplineHelper->ActivateCutoutEffect(TilesetAccess, bActivate);
		if (bActivate)
		{
			PolygonInfo.SplineHelper->InvertCutoutEffect(TilesetAccess, PolygonInfo.GetInvertEffect());
			ActiveEffectsInTileset++;
		}
	}
}

void AITwinClippingTool::FImpl::UpdateTileset(FITwinTilesetAccess const& TilesetAccess,
	std::optional<EITwinClippingPrimitiveType> const& SpecificPrimitiveType /*= std::nullopt*/)
{
	ITwin::ModelLink const ModelIdentifier = TilesetAccess.GetDecorationKey();
	if (ModelIdentifier.first == EITwinModelType::Invalid)
		return; // not something we handle through the iTwin plugin

	ACesium3DTileset* TilesetPtr = TilesetAccess.GetMutableTileset();
	if (!TilesetPtr)
		return;
	ACesium3DTileset& Tileset(*TilesetPtr);

	FTilesetUpdateInfo UpdateInfo;

	// 1. Handle clipping planes if needed.
	if (!SpecificPrimitiveType || *SpecificPrimitiveType == EITwinClippingPrimitiveType::Plane)
	{
		UpdateTileset_Planes(Tileset, ModelIdentifier, UpdateInfo);
	}

	// 2. Handle clipping boxes if needed.
	if (!SpecificPrimitiveType || *SpecificPrimitiveType == EITwinClippingPrimitiveType::Box)
	{
		UpdateTileset_Boxes(Tileset, ModelIdentifier, UpdateInfo);
	}

	// 2. Handle cartographic polygons if needed.
	if (!SpecificPrimitiveType || *SpecificPrimitiveType == EITwinClippingPrimitiveType::Polygon)
	{
		UpdateTileset_Polygons(TilesetAccess, UpdateInfo);
	}

	if (UpdateInfo.AddedExcluders > 0)
	{
		BE_LOGI("ITwinAdvViz", "[Clipping] Added " << UpdateInfo.AddedExcluders << " Tile Excluder(s) for tileset "
			<< TCHAR_TO_UTF8(*Tileset.GetActorNameOrLabel()));
	}

	UITwinClipping3DTilesetHelper* ClippingHelper = GetClippingHelper(Tileset, ModelIdentifier);
	if (!ClippingHelper && UpdateInfo.ActiveEffectsInTileset > 0)
	{
		// Create the helper which will be responsible for updating the Custom Primitive Data in the Unreal
		// meshes, depending on the influences. It will also be used to filter impacts in ray-tracing of the
		// collision meshes.
		ClippingHelper = MakeClippingHelper(Tileset, ModelIdentifier);
	}
	if (ClippingHelper)
	{
		if (ClippingHelper->UpdateCPDFlagsFromClippingSelection(Owner))
		{
			// Update existing meshes, if any.
			ClippingHelper->ApplyCPDFlagsToAllMeshComponentsInTileset(Tileset);
			// Future meshes created when a new tile is loaded will be automatically modified through the
			// Cesium lifecycle mesh creation callback.
		}
	}
}

namespace
{
	template <EITwinClippingPrimitiveType T>
	struct ClippingPrimitiveTrait
	{

	};

	template <>
	struct ClippingPrimitiveTrait<EITwinClippingPrimitiveType::Plane>
	{
		static constexpr int32 MAX_PRIMITIVES = ITwin::MAX_CLIPPING_PLANES;
		static constexpr const TCHAR* PrimitiveName = TEXT("Plane");
		static constexpr const TCHAR* PrimitiveNamePlural = TEXT("Planes");
		static constexpr const TCHAR* PrimitiveCountName = TEXT("PlaneCount");
		static constexpr const TCHAR* PopulationAssetName = TEXT("ClippingPlane");
	};

	template <>
	struct ClippingPrimitiveTrait<EITwinClippingPrimitiveType::Box>
	{
		static constexpr int32 MAX_PRIMITIVES = ITwin::MAX_CLIPPING_BOXES;
		static constexpr const TCHAR* PrimitiveName = TEXT("Box");
		static constexpr const TCHAR* PrimitiveNamePlural = TEXT("Boxes");
		static constexpr const TCHAR* PrimitiveCountName = TEXT("BoxCount");
		static constexpr const TCHAR* PopulationAssetName = TEXT("ClippingBox");
	};

	template <EITwinClippingPrimitiveType PrimitiveType>
	FString TGetClippingAssetPath()
	{
		using PrimitiveTraits = ClippingPrimitiveTrait<PrimitiveType>;
		return FString::Printf(TEXT("/Game/Clipping/Clipping/%s"), PrimitiveTraits::PopulationAssetName);
	}

	template <EITwinClippingPrimitiveType PrimitiveType>
	bool TPreLoadClippingPrimitive(TWeakObjectPtr<AITwinPopulation>& ClippingPopulation,
		AITwinPopulationTool& PopulationTool)
	{
		AITwinPopulation* Population = PopulationTool.PreLoadPopulation(TGetClippingAssetPath<PrimitiveType>());
		if (Population)
		{
			ClippingPopulation = Population;
			return true;
		}
		return false;
	}
}

uint32 AITwinClippingTool::FImpl::PreLoadClippingPrimitives(AITwinPopulationTool& Tool)
{
	uint32 NumPreloaded = 0;
	if (TPreLoadClippingPrimitive<EITwinClippingPrimitiveType::Box>(Owner.ClippingBoxPopulation, Tool))
	{
		NumPreloaded++;
	}
	if (TPreLoadClippingPrimitive<EITwinClippingPrimitiveType::Plane>(Owner.ClippingPlanePopulation, Tool))
	{
		NumPreloaded++;
	}
	return NumPreloaded;
}

template <EITwinClippingPrimitiveType PrimitiveType>
bool AITwinClippingTool::FImpl::TStartInteractivePrimitiveInstanceCreation()
{
	using PrimitiveTraits = ClippingPrimitiveTrait<PrimitiveType>;

	if (NumEffects(PrimitiveType) >= PrimitiveTraits::MAX_PRIMITIVES)
	{
		// Internal limit reached for this primitive.
		return false;
	}

	ActivatePopulationTool(Owner.GetWorld());
	if (PopulationTool.IsValid())
	{
		PopulationTool->SetMode(EPopulationToolMode::Select);
		if (PopulationTool->IsInteractiveCreationMode())
		{
			// Do not accumulate the new effects (can happen if the user clicks several times the Add icon,
			// without validating the position of the new primitive).
			return false;
		}
		PopulationTool->ClearUsedAssets();
		PopulationTool->SetUsedAsset(TGetClippingAssetPath<PrimitiveType>(), true);
		// Ensure the new instance will be visible.
		ShowOnlyProxiesOfType(PrimitiveType, false);
		return PopulationTool->StartInteractiveCreation();
	}
	else
	{
		return false;
	}
}

bool AITwinClippingTool::StartInteractiveEffectCreation(EITwinClippingPrimitiveType Type)
{
	// Abort current effect creation, if any.
	AbortInteractiveCreation(/*bTriggeredFromITS*/true);

	// Make sure we hide all effect proxies (only the new item will be visible).
	Impl->HideAllEffectProxies();

	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:
		return Impl->TStartInteractivePrimitiveInstanceCreation<EITwinClippingPrimitiveType::Box>();

	case EITwinClippingPrimitiveType::Plane:
		return Impl->TStartInteractivePrimitiveInstanceCreation<EITwinClippingPrimitiveType::Plane>();

	case EITwinClippingPrimitiveType::Polygon:
	{
		// Start interactive drawing.
		TWeakObjectPtr<AITwinSplineTool> SplineTool = Impl->ActivateSplineTool(GetWorld());
		if (SplineTool.IsValid())
		{
			// Activate overview camera (Top view).
			SplineTool->OnOverviewCamera();
			// Reset the cutout targets, so that the 1st intersection found upon a click determines the
			// cut-out target layer.
			SplineTool->SetCutoutTargets({});
			SplineTool->StartInteractiveCreation();
			return true;
		}
		return false;
	}

	BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(case EITwinClippingPrimitiveType::Count: , false);
	}
}


template <typename PrimitiveInfo, EITwinClippingPrimitiveType PrimitiveType>
bool AITwinClippingTool::FImpl::TAddClippingPrimitive(TArray<PrimitiveInfo>& ClippingInfos, int32 InstanceIndex)
{
	using PrimitiveTraits = ClippingPrimitiveTrait<PrimitiveType>;

	bool bHasAddedClippingPrimitive = false;

	if (InstanceIndex < PrimitiveTraits::MAX_PRIMITIVES)
	{
		ensure(InstanceIndex == ClippingInfos.Num());
		if (InstanceIndex >= ClippingInfos.Num())
		{
			ClippingInfos.SetNum(InstanceIndex + 1);
			bool bIsClippingReady = UpdateClippingPrimitiveFromUEInstance(PrimitiveType, InstanceIndex);

			UMaterialParameterCollectionInstance* MPCInstance = GetMPCClippingInstance();
			if (ensure(MPCInstance) && bIsClippingReady)
			{
				bool bFound = MPCInstance->SetScalarParameterValue(
					PrimitiveTraits::PrimitiveCountName,
					static_cast<float>(ClippingInfos.Num()));
				ensure(bFound);
				bHasAddedClippingPrimitive = bFound;
				BE_LOGI("ITwinAdvViz", "[Clipping] "
					<< TCHAR_TO_UTF8(PrimitiveTraits::PrimitiveCountName)
					<< ": " << ClippingInfos.Num()
					<< " - set parameter result: " << (bFound ? 1 : 0));
			}
		}
	}
	return bHasAddedClippingPrimitive;
}

void AITwinClippingTool::FImpl::OnClippingInstanceAdded(AITwinPopulation* Population, EITwinInstantiatedObjectType ObjectType, int32 InstanceIndex)
{
	bool bHasAddedClippingPrimitive = false;
	std::optional<EITwinClippingPrimitiveType> PrimitiveToUpdate;
	switch (ObjectType)
	{
	case EITwinInstantiatedObjectType::ClippingPlane:
	{
		Owner.ClippingPlanePopulation = Population;
		bHasAddedClippingPrimitive = TAddClippingPrimitive<FITwinClippingPlaneInfo, EITwinClippingPrimitiveType::Plane>(ClippingPlaneInfos, InstanceIndex);
		PrimitiveToUpdate = EITwinClippingPrimitiveType::Plane;
		break;
	}

	case EITwinInstantiatedObjectType::ClippingBox:
	{
		Owner.ClippingBoxPopulation = Population;
		bHasAddedClippingPrimitive = TAddClippingPrimitive<FITwinClippingBoxInfo, EITwinClippingPrimitiveType::Box>(ClippingBoxInfos, InstanceIndex);
		PrimitiveToUpdate = EITwinClippingPrimitiveType::Box;
		break;
	}

	default:
		// Nothing to do for other types.
		break;
	}

	if (bHasAddedClippingPrimitive)
	{
		// For a new clipping primitive, the invert effect option is false so we would not have to update the
		// Flipping flags in the MPC. But in the context of undo/redo, this can perfectly be true, so we
		// update those flags anyway in all cases.
		EncodeFlippingInMPC(PrimitiveToUpdate.value_or(EITwinClippingPrimitiveType::Count));
		// Create tile excluders in all registered tilesets.
		UpdateAllTilesets(PrimitiveToUpdate);

		Owner.EffectListModifiedEvent.Broadcast();
		if (ensure(PrimitiveToUpdate))
			Owner.EffectAddedEvent.Broadcast(*PrimitiveToUpdate, InstanceIndex);

		if (PrimitiveToUpdate
			&& *PrimitiveToUpdate == EITwinClippingPrimitiveType::Plane
			&& ensure(InstanceIndex < NumEffects(EITwinClippingPrimitiveType::Plane)))
		{
			// Make sure the new plane proxy will not move until the user changes the point of view.
			LastSelectedPlaneIndex = InstanceIndex;
			LastViewInfo = ITwinClippingDetails::GetCameraViewInfo(Owner.GetWorld());
			ClippingPlaneInfos[InstanceIndex].UpdateInfluenceBoundingBox(Owner.GetWorld());
		}
	}
}

void AITwinClippingTool::OnClippingInstanceAdded(AITwinPopulation* Population, EITwinInstantiatedObjectType ObjectType, int32 InstanceIndex)
{
	Impl->OnClippingInstanceAdded(Population, ObjectType, InstanceIndex);
}

void AITwinClippingTool::FImpl::UpdateAllTilesets(std::optional<EITwinClippingPrimitiveType> const& SpecificPrimitiveType /*= std::nullopt*/)
{
	ITwin::IterateAllITwinTilesets([this, SpecificPrimitiveType](FITwinTilesetAccess const& TilesetAccess)
	{
		UpdateTileset(TilesetAccess, SpecificPrimitiveType);
	}, Owner.GetWorld());
}

template <typename T>
bool AITwinClippingTool::FImpl::GetPlaneEquationFromUEInstance(UE::Math::TVector<T>& OutPlaneOrientation, T& OutPlaneW, int32 InInstanceIndex) const
{
	if (!ensure(Owner.ClippingPlanePopulation.IsValid()))
		return false;

	if (InInstanceIndex >= Owner.ClippingPlanePopulation->GetNumberOfInstances())
		return false;

	const FTransform InstanceTransform = Owner.ClippingPlanePopulation->GetInstanceTransform(InInstanceIndex);

	auto const PositionUE = InstanceTransform.GetLocation();
	auto const PlaneOrientationUE = InstanceTransform.GetUnitAxis(EAxis::Z); // GetUpVector
	OutPlaneOrientation = UE::Math::TVector<T>(PlaneOrientationUE);
	OutPlaneW = static_cast<T>(PositionUE.Dot(PlaneOrientationUE));
	return true;
}

bool AITwinClippingTool::FImpl::UpdateClippingPlaneEquationFromUEInstance(int32 InstanceIndex)
{
	if (!ensure(InstanceIndex >= 0 && InstanceIndex < ClippingPlaneInfos.Num()))
		return false;

	FVector3d PlaneOrientation = FVector3d::ZAxisVector;
	double PlaneW(0.);
	if (!GetPlaneEquationFromUEInstance(PlaneOrientation, PlaneW, InstanceIndex))
		return false;

	const int32 PlaneIndex = InstanceIndex;
	auto& PlaneInfo = ClippingPlaneInfos[PlaneIndex];
	PlaneInfo.PlaneEquation.PlaneOrientation = PlaneOrientation;
	PlaneInfo.PlaneEquation.PlaneW = PlaneW;

	// Update the plane equation in all tile excluders created from this plane.
	for (auto const& TileExcluder : PlaneInfo.TileExcluders)
	{
		if (TileExcluder.IsValid())
		{
			UITwinPlaneTileExcluder* PlaneExcluder = Cast<UITwinPlaneTileExcluder>(TileExcluder.Get());
			PlaneExcluder->PlaneEquation.PlaneOrientation = PlaneOrientation;
			PlaneExcluder->PlaneEquation.PlaneW = PlaneW;
		}
	}
	// Also update the plane equation stored in a Material Parameter Collection so that it can be accessed by
	// all tileset materials.
	bool bIsClippingReady = false;
	UMaterialParameterCollectionInstance* MPCInstance = GetMPCClippingInstance();
	if (ensure(MPCInstance))
	{
		const FLinearColor PlaneEquationAsColor =
		{
			static_cast<float>(PlaneOrientation.X),
			static_cast<float>(PlaneOrientation.Y),
			static_cast<float>(PlaneOrientation.Z),
			static_cast<float>(PlaneW)
		};
		bIsClippingReady = MPCInstance->SetVectorParameterValue(
			FName(fmt::format("PlaneEquation_{}", PlaneIndex).c_str()),
			PlaneEquationAsColor);

		ensure(bIsClippingReady);
	}
	return bIsClippingReady;
}

bool AITwinClippingTool::FImpl::UpdateClippingPrimitiveFromUEInstance(EITwinClippingPrimitiveType Type, int32 InstanceIndex)
{
	bool bUpdated = false;
	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:
		bUpdated = UpdateClippingBoxFromUEInstance(InstanceIndex);
		break;
	case EITwinClippingPrimitiveType::Plane:
		bUpdated = UpdateClippingPlaneEquationFromUEInstance(InstanceIndex);
		break;
	BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(
	case EITwinClippingPrimitiveType::Polygon:
	case EITwinClippingPrimitiveType::Count:, false);
	}
	if (bUpdated)
	{
		UpdateClippingPropertiesFromAVizInstance(Type, InstanceIndex);
	}
	return bUpdated;
}

template <typename ClippingPrimitiveInfo, EITwinClippingPrimitiveType PrimitiveType>
void AITwinClippingTool::FImpl::TUpdateAllClippingPrimitives(TArray<ClippingPrimitiveInfo>& ClippingInfos)
{
	auto const& PopulationPtr = GetClippingEffectPopulation(PrimitiveType);
	if (!ensure(PopulationPtr.IsValid()))
		return;
	const AITwinPopulation& ClippingPopulation = *PopulationPtr;
	const int32 NumPrims = ClippingPopulation.GetNumberOfInstances();

	UMaterialParameterCollectionInstance* MPCInstance = GetMPCClippingInstance();
	if (!ensure(MPCInstance))
		return;

	// Disable tile excluders which have become obsolete.
	for (int32 i(NumPrims); i < ClippingInfos.Num(); ++i)
	{
		auto const& PrimInfo = ClippingInfos[i];
		for (auto const& TileExcluder : PrimInfo.TileExcluders)
		{
			if (TileExcluder.IsValid())
			{
				PrimInfo.DeactivatePrimitiveInExcluder(*TileExcluder);
			}
		}
	}
	ClippingInfos.SetNum(NumPrims);

	// Update all remaining primitives.
	for (int32 InstanceIndex(0); InstanceIndex < NumPrims; ++InstanceIndex)
	{
		UpdateClippingPrimitiveFromUEInstance(PrimitiveType, InstanceIndex);
	}

	using PrimitiveTraits = ClippingPrimitiveTrait<PrimitiveType>;

	// Update primitive count for shader.
	MPCInstance->SetScalarParameterValue(
		PrimitiveTraits::PrimitiveCountName,
		static_cast<float>(ClippingInfos.Num()));

	EncodeFlippingInMPC(PrimitiveType);

	UpdateAllTilesets(PrimitiveType);
}

void AITwinClippingTool::FImpl::UpdateAllClippingPlanes()
{
	TUpdateAllClippingPrimitives<FITwinClippingPlaneInfo, EITwinClippingPrimitiveType::Plane>(ClippingPlaneInfos);
}

void AITwinClippingTool::FImpl::UpdateAllClippingBoxes()
{
	TUpdateAllClippingPrimitives<FITwinClippingBoxInfo, EITwinClippingPrimitiveType::Box>(ClippingBoxInfos);
}

bool AITwinClippingTool::FImpl::GetBoxTransformInfoFromUEInstance(glm::dmat3x3& OutMatrix, glm::dvec3& OutTranslation, int32 InInstanceIndex) const
{
	if (!ensure(Owner.ClippingBoxPopulation.IsValid()))
		return false;

	if (InInstanceIndex >= Owner.ClippingBoxPopulation->GetNumberOfInstances())
		return false;

	const FTransform InstanceTransform = Owner.ClippingBoxPopulation->GetInstanceTransform(InInstanceIndex);

	double MasterMeshScale = 1.0;
	// Take the master object's scale into account (depends on the way the box was imported
	// in Unreal...)
	const FBox MasterMeshBox = Owner.ClippingBoxPopulation->GetMasterMeshBoundingBox();
	if (ensure(MasterMeshBox.IsValid))
		MasterMeshScale = MasterMeshBox.GetSize().GetAbsMax();

	FMatrix InstanceMat = InstanceTransform.ToMatrixWithScale();
	InstanceMat *= MasterMeshScale;
	const FVector InstancePos = InstanceTransform.GetTranslation();

	const FVector Col0 = InstanceMat.GetColumn(0);
	const FVector Col1 = InstanceMat.GetColumn(1);
	const FVector Col2 = InstanceMat.GetColumn(2);
	OutMatrix = glm::dmat3x3(
		glm::dvec3(Col0.X, Col0.Y, Col0.Z),
		glm::dvec3(Col1.X, Col1.Y, Col1.Z),
		glm::dvec3(Col2.X, Col2.Y, Col2.Z));
	OutTranslation = glm::dvec3(InstancePos.X, InstancePos.Y, InstancePos.Z);
	return true;
}

bool AITwinClippingTool::FImpl::UpdateClippingBoxFromUEInstance(int32 InstanceIndex)
{
	if (!ensure(InstanceIndex >= 0 && InstanceIndex < ClippingBoxInfos.Num()))
		return false;
	const int32 BoxIndex = InstanceIndex;
	FITwinClippingBoxInfo& BoxInfo = ClippingBoxInfos[BoxIndex];

	glm::dmat3x3 BoxMatrix;
	glm::dvec3 BoxTranslation;
	if (!GetBoxTransformInfoFromUEInstance(BoxMatrix, BoxTranslation, InstanceIndex))
		return false;

	// Update the box information shared by all tile excluders activating this box.
	BoxInfo.UpdateBoxProperties(BoxMatrix, BoxTranslation);

	bool bIsClippingReady = false;
	UMaterialParameterCollectionInstance* MPCInstance = GetMPCClippingInstance();
	if (ensure(MPCInstance))
	{
		// For performance reasons, we store the inverse matrix.
		glm::dmat3x3 const& InverseMatrix = BoxInfo.BoxProperties->BoxInvMatrix;
		glm::dvec3 const col0 = glm::column(InverseMatrix, 0);
		glm::dvec3 const col1 = glm::column(InverseMatrix, 1);
		glm::dvec3 const col2 = glm::column(InverseMatrix, 2);
		bIsClippingReady =
			MPCInstance->SetVectorParameterValue(
				FName(fmt::format("BoxInvMatrix_col0_{}", BoxIndex).c_str()),
				FLinearColor(col0.x, col0.y, col0.z))
			&& MPCInstance->SetVectorParameterValue(
				FName(fmt::format("BoxInvMatrix_col1_{}", BoxIndex).c_str()),
				FLinearColor(col1.x, col1.y, col1.z))
			&& MPCInstance->SetVectorParameterValue(
				FName(fmt::format("BoxInvMatrix_col2_{}", BoxIndex).c_str()),
				FLinearColor(col2.x, col2.y, col2.z))
			&& MPCInstance->SetVectorParameterValue(
				FName(fmt::format("BoxTranslation_{}", BoxIndex).c_str()),
				FLinearColor(BoxTranslation.x, BoxTranslation.y, BoxTranslation.z));
		ensure(bIsClippingReady);
	}
	return bIsClippingReady;
}

void AITwinClippingTool::OnClippingInstanceModified(EITwinInstantiatedObjectType ObjectType, int32 InstanceIndex, bool bTriggeredFromITS)
{
	std::optional<EITwinClippingPrimitiveType> ModifiedType;
	switch (ObjectType)
	{
	case EITwinInstantiatedObjectType::ClippingPlane:
		if (Impl->UpdateClippingPlaneEquationFromUEInstance(InstanceIndex))
		{
			ModifiedType = EITwinClippingPrimitiveType::Plane;
		}
		break;
	case EITwinInstantiatedObjectType::ClippingBox:
		if (Impl->UpdateClippingBoxFromUEInstance(InstanceIndex))
		{
			ModifiedType = EITwinClippingPrimitiveType::Box;
		}
		break;
	default:
		// Nothing to do for other types.
		return;
	}

	if (ModifiedType)
	{
		EffectModifiedEvent.Broadcast(*ModifiedType, InstanceIndex, bTriggeredFromITS);
	}
}

void AITwinClippingTool::BeforeRemoveClippingInstances(EITwinInstantiatedObjectType ObjectType, const TArray<int32>& InstanceIndices)
{
	if (InstanceIndices.IsEmpty())
		return;
	std::optional<EITwinClippingPrimitiveType> RemovedPrimitiveType;
	switch (ObjectType)
	{
	case EITwinInstantiatedObjectType::ClippingPlane:
		RemovedPrimitiveType = EITwinClippingPrimitiveType::Plane;
		break;
	case EITwinInstantiatedObjectType::ClippingBox:
		RemovedPrimitiveType = EITwinClippingPrimitiveType::Box;
		break;
	default:
		// Nothing to do for other types.
		return;
	}
	if (RemovedPrimitiveType)
	{
		const bool bTriggeredFromITS = Impl->RemovalInitiatorOpt
			&& *Impl->RemovalInitiatorOpt == FImpl::ERemovalInitiator::ITS;
		for (int32 EffectIndex : InstanceIndices)
		{
			EffectRemovedEvent.Broadcast(*RemovedPrimitiveType, EffectIndex, bTriggeredFromITS);
		}
	}
}

void AITwinClippingTool::OnClippingInstancesRemoved(EITwinInstantiatedObjectType ObjectType, const TArray<int32>& InstanceIndices)
{
	bool bEffectListModified = false;
	switch (ObjectType)
	{
	case EITwinInstantiatedObjectType::ClippingPlane:
		if (!InstanceIndices.IsEmpty())
		{
			// Recreate all planes from remaining instances.
			Impl->UpdateAllClippingPlanes();
			bEffectListModified = true;
		}
		break;
	case EITwinInstantiatedObjectType::ClippingBox:
		if (!InstanceIndices.IsEmpty())
		{
			// Recreate all boxes from remaining instances.
			Impl->UpdateAllClippingBoxes();
			bEffectListModified = true;
		}
		break;
	default:
		// Nothing to do for other types.
		return;
	}
	if (bEffectListModified)
	{
		// After removing a cutout, we should exit isolation mode.
		// See AzDev#2015685 (it also fixes AzDev#2015686, ie. when a cutout creation is aborted).
		Impl->SetAllEffectProxiesVisibility(true);

		EffectListModifiedEvent.Broadcast();
	}
}

void AITwinClippingTool::OnClippingInstancesLoaded(AITwinPopulation* Population, EITwinInstantiatedObjectType ObjectType)
{
	if (!ensure(Population))
		return;
	const int32 NumInstances = Population->GetNumberOfInstances();
	switch (ObjectType)
	{
	case EITwinInstantiatedObjectType::ClippingPlane:
		if (NumInstances > 0)
		{
			ClippingPlanePopulation = Population;
			Impl->UpdateAllClippingPlanes();
		}
		break;
	case EITwinInstantiatedObjectType::ClippingBox:
		if (NumInstances > 0)
		{
			ClippingBoxPopulation = Population;
			Impl->UpdateAllClippingBoxes();
		}
		break;
	default:
		// Nothing to do for other types.
		return;
	}
}

bool AITwinClippingTool::FImpl::RegisterCutoutSpline(AITwinSplineHelper* SplineHelper)
{
	if (SplineHelper->GetUsage() == EITwinSplineUsage::MapCutout
		&& SplineHelper->HasCartographicPolygon())
	{
		FITwinClippingCartographicPolygonInfo& PolygonInfo = ClippingPolygonInfos.AddDefaulted_GetRef();
		PolygonInfo.SplineHelper = SplineHelper;
		PolygonInfo.SetInvertEffect(SplineHelper->IsInvertedCutoutEffect());
		// Simplified UX for linked models: handle influence per model type only.
		// TODO_JDE modify this when/if we implement per model activation.
		std::set<ITwin::ModelLink> const Links = SplineHelper->GetLinkedModels();
		PolygonInfo.SetInfluenceNone();
		for (ITwin::ModelLink const& Link : Links)
		{
			PolygonInfo.SetInfluenceFullModelType(Link.first, true);

			auto TilesetAccessPtr = ITwin::GetTilesetAccessFromModelLink(Link, Owner.GetWorld());
			if (TilesetAccessPtr)
			{
				UpdateTileset(*TilesetAccessPtr, EITwinClippingPrimitiveType::Polygon);
			}
		}
		Owner.EffectListModifiedEvent.Broadcast();
		Owner.EffectAddedEvent.Broadcast(EITwinClippingPrimitiveType::Polygon, ClippingPolygonInfos.Num() - 1);
		return true;
	}
	else
	{
		return false;
	}
}

void AITwinClippingTool::OnSplineHelperAdded(AITwinSplineHelper* NewSpline)
{
	Impl->RegisterCutoutSpline(NewSpline);
}

bool AITwinClippingTool::FImpl::DeRegisterCutoutSpline(AITwinSplineHelper* SplineBeingRemoved)
{
	auto const SelectedBefore = GetSelectedEffect();

	int32 Index = ClippingPolygonInfos.IndexOfByPredicate(
		[SplineBeingRemoved](FITwinClippingCartographicPolygonInfo const& InItem)
	{
		return (InItem.SplineHelper == SplineBeingRemoved);
	});
	if (Index != INDEX_NONE)
	{
		ClippingPolygonInfos.RemoveAt(Index);

		const bool bTriggeredFromITS = RemovalInitiatorOpt
			&& *RemovalInitiatorOpt == FImpl::ERemovalInitiator::ITS;
		Owner.EffectRemovedEvent.Broadcast(EITwinClippingPrimitiveType::Polygon, Index, bTriggeredFromITS);
		Owner.EffectListModifiedEvent.Broadcast();

		// After removing a cutout, we should exit isolation mode or else we'll be in an inconsistent state
		// (cutout selection mode in iTS, but some or all cutout proxies hidden in Unreal).
		// See AzDev#2015685
		if (SelectedBefore)
		{
			SetAllEffectProxiesVisibility(true);
		}
		return true;
	}
	else
	{
		return false;
	}
}

void AITwinClippingTool::OnSplineHelperRemoved(AITwinSplineHelper* SplineBeingRemoved)
{
	if (SplineBeingRemoved
		&& SplineBeingRemoved->GetUsage() == EITwinSplineUsage::MapCutout)
	{
		Impl->DeRegisterCutoutSpline(SplineBeingRemoved);
	}
}

void AITwinClippingTool::OnItemCreationAbortedInTool(bool bTriggeredFromITS)
{
	InteractiveCreationAbortedEvent.Broadcast(bTriggeredFromITS);
}

inline
int32 AITwinClippingTool::FImpl::NumEffects(EITwinClippingPrimitiveType Type) const
{
	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:		return ClippingBoxInfos.Num();
	case EITwinClippingPrimitiveType::Plane:	return ClippingPlaneInfos.Num();
	case EITwinClippingPrimitiveType::Polygon:	return ClippingPolygonInfos.Num();

	BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(case EITwinClippingPrimitiveType::Count: , 0);
	}
}

int32 AITwinClippingTool::NumEffects(EITwinClippingPrimitiveType Type) const
{
	return Impl->NumEffects(Type);
}

inline
FITwinClippingInfoBase& AITwinClippingTool::FImpl::GetMutableClippingEffect(EITwinClippingPrimitiveType Type, int32 Index)
{
	ensure(Index >= 0 && Index < NumEffects(Type));
	switch (Type)
	{
		BE_UNCOVERED_ENUM_ASSERT_AND_FALLTHROUGH(case EITwinClippingPrimitiveType::Count: )
	case EITwinClippingPrimitiveType::Box:		return ClippingBoxInfos[Index];
	case EITwinClippingPrimitiveType::Plane:	return ClippingPlaneInfos[Index];
	case EITwinClippingPrimitiveType::Polygon:	return ClippingPolygonInfos[Index];
	}
}

inline
const FITwinClippingInfoBase& AITwinClippingTool::FImpl::GetClippingEffect(EITwinClippingPrimitiveType Type, int32 Index) const
{
	ensure(Index >= 0 && Index < NumEffects(Type));
	switch (Type)
	{
		BE_UNCOVERED_ENUM_ASSERT_AND_FALLTHROUGH(case EITwinClippingPrimitiveType::Count: )
	case EITwinClippingPrimitiveType::Box:		return ClippingBoxInfos[Index];
	case EITwinClippingPrimitiveType::Plane:	return ClippingPlaneInfos[Index];
	case EITwinClippingPrimitiveType::Polygon:	return ClippingPolygonInfos[Index];
	}
}

bool AITwinClippingTool::FImpl::RemoveEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bTriggeredFromITS)
{
	if (!ensure(PrimitiveIndex >= 0 && PrimitiveIndex < NumEffects(Type)))
		return false;

	FScopedRemovalContext RemovalCtx(*this,
		bTriggeredFromITS ? ERemovalInitiator::ITS : ERemovalInitiator::Unreal);

	// Select the cutout if needed (for undo/redo) - the effect is already selected if this event is
	// triggered from iTS cutout properties page, but not if the event is triggered from the list of cutouts.
	auto const CurrentSelection = GetSelectedEffect();
	bool bEffectIsSelected = CurrentSelection
		&& CurrentSelection->first == Type
		&& CurrentSelection->second == PrimitiveIndex;
	if (!bEffectIsSelected)
	{
		bEffectIsSelected = SelectEffect(Type, PrimitiveIndex, false);
	}
	Owner.RemoveEffectStartedEvent.Broadcast();

	const int32 NumPrimsOld = NumEffects(Type);
	// Remark: for box and plane, the removal of the entry from the info array will be indirect, through a
	// call to #OnClippingInstancesRemoved (see AITwinPopulation::RemoveInstance).
	// Hence, to know if the removal succeeded, we just check the count of primitives at the end.
	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:
	case EITwinClippingPrimitiveType::Plane:
	{
		if (bEffectIsSelected)
		{
			DeleteSelectedPopulationInstance();
		}
		break;
	}

	case EITwinClippingPrimitiveType::Polygon:
	{
		auto const& PolygonInfo = ClippingPolygonInfos[PrimitiveIndex];
		if (PolygonInfo.SplineHelper.IsValid() && ensure(SplineTool.IsValid()))
		{
			SplineTool->DeleteSpline(PolygonInfo.SplineHelper.Get());
		}
		break;
	}

	BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(case EITwinClippingPrimitiveType::Count: , false);
	}

	const bool bRemoved = (NumEffects(Type) == NumPrimsOld - 1);
	return bRemoved;
}

bool AITwinClippingTool::RemoveEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bTriggeredFromITS)
{
	const bool bRemoved = Impl->RemoveEffect(Type, PrimitiveIndex, bTriggeredFromITS);
	if (bRemoved)
	{
		RemoveEffectCompletedEvent.Broadcast();
	}
	return bRemoved;
}


template <typename PrimitiveInfo, EITwinClippingPrimitiveType PrimitiveType>
bool AITwinClippingTool::FImpl::TEncodeFlippingInMPC(TArray<PrimitiveInfo> const& ClippingInfos)
{
	// We encode the inversion of primitives on float, per groups of 16.
	// inspired by https://theinstructionlimit.com/encoding-boolean-flags-into-a-float-in-hlsl

	int FlipFlags_0_15 = 0;
	for (int32 i = 0; i < std::min(16, ClippingInfos.Num()); i++)
	{
		if (ClippingInfos[i].GetInvertEffect())
			FlipFlags_0_15 |= (1 << i);
	}
	int FlipFlags_16_31 = 0;
	for (int32 i = 0; i < std::min(16, ClippingInfos.Num() - 16); i++)
	{
		if (ClippingInfos[16 + i].GetInvertEffect())
			FlipFlags_16_31 |= (1 << i);
	}

	using PrimitiveTraits = ClippingPrimitiveTrait<PrimitiveType>;

	bool bStoredInMPC = false;
	UMaterialParameterCollectionInstance* MPCInstance = GetMPCClippingInstance();
	if (ensure(MPCInstance))
	{
		bStoredInMPC = MPCInstance->SetScalarParameterValue(
			FName(*FString::Printf(TEXT("Flip%s_0_15"), PrimitiveTraits::PrimitiveNamePlural)),
			static_cast<float>(FlipFlags_0_15));

		bStoredInMPC &= MPCInstance->SetScalarParameterValue(
			FName(*FString::Printf(TEXT("Flip%s_16_31"), PrimitiveTraits::PrimitiveNamePlural)),
			static_cast<float>(FlipFlags_16_31));
		ensure(bStoredInMPC);
	}
	return bStoredInMPC;
}

bool AITwinClippingTool::FImpl::EncodeFlippingInMPC(EITwinClippingPrimitiveType Type)
{
	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:
		return TEncodeFlippingInMPC<FITwinClippingBoxInfo, EITwinClippingPrimitiveType::Box>(ClippingBoxInfos);
	case EITwinClippingPrimitiveType::Plane:
		return TEncodeFlippingInMPC<FITwinClippingPlaneInfo, EITwinClippingPrimitiveType::Plane>(ClippingPlaneInfos);
	BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(
	case EITwinClippingPrimitiveType::Polygon:
	case EITwinClippingPrimitiveType::Count:, false);
	}
}

bool AITwinClippingTool::FImpl::GetInvertEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex) const
{
	if (ensure(PrimitiveIndex >= 0 && PrimitiveIndex < NumEffects(Type)))
	{
		return GetClippingEffect(Type, PrimitiveIndex).GetInvertEffect();
	}
	else
	{
		return false;
	}
}

bool AITwinClippingTool::GetInvertEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex) const
{
	return Impl->GetInvertEffect(Type, PrimitiveIndex);
}

void AITwinClippingTool::FImpl::SetInvertEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bInvert)
{
	if (ensure(PrimitiveIndex >= 0 && PrimitiveIndex < NumEffects(Type)))
	{
		FITwinClippingInfoBase& PrimitiveInfo = GetMutableClippingEffect(Type, PrimitiveIndex);
		if (PrimitiveInfo.GetInvertEffect() != bInvert)
		{
			PrimitiveInfo.SetInvertEffect(bInvert);

			// Refresh tilesets
			if (Type == EITwinClippingPrimitiveType::Polygon)
			{
				// For cartographic polygons we do it through the general update.
				UpdateAllTilesets(Type);
			}
			else
			{
				// For other types we just update flags in Material Parameter Collection.
				EncodeFlippingInMPC(Type);

				// Manage persistence.
				UpdateAVizInstanceProperties(Type, PrimitiveIndex);
			}
		}
	}
}

void AITwinClippingTool::SetInvertEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bInvert)
{
	Impl->SetInvertEffect(Type, PrimitiveIndex, bInvert);
}

void AITwinClippingTool::FlipEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex)
{
	SetInvertEffect(Type, PrimitiveIndex, !GetInvertEffect(Type, PrimitiveIndex));
}

bool AITwinClippingTool::FImpl::SelectEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex,
	bool bEnterIsolationMode /*= true*/)
{
	if (!ensure(PrimitiveIndex >= 0 && PrimitiveIndex < NumEffects(Type)))
		return false;

	bool bHasSetSelection = false;
	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:
	case EITwinClippingPrimitiveType::Plane:
	{
		auto const& Population = GetClippingEffectPopulation(Type);
		if (Population.IsValid())
		{
			if (Type == EITwinClippingPrimitiveType::Plane && IsRotationMode()
				&& bEnterIsolationMode)
			{
				SetRotationCenterPickingMode(true);
			}

			SelectPopulationInstance(Population.Get(), PrimitiveIndex, Owner.GetWorld());

			bHasSetSelection = (Population->GetSelectedInstanceIndex() == PrimitiveIndex);
		}
		break;
	}

	case EITwinClippingPrimitiveType::Polygon:
	{
		auto const& PolygonInfo = ClippingPolygonInfos[PrimitiveIndex];
		if (PolygonInfo.SplineHelper.IsValid())
		{
			SelectSpline(PolygonInfo.SplineHelper.Get(), Owner.GetWorld());

			bHasSetSelection = PolygonInfo.SplineHelper->IsSelected();
		}
		break;
	}

	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinClippingPrimitiveType::Count: );
	}

	if (bEnterIsolationMode && bHasSetSelection)
	{
		// Isolation mode (AzDev#1967146)
		// Hide all other types. Note that the isolation *inside* the selected type is performed at a lower
		// level:
		// - look for #CUSTOM_FLOAT_OPACITY_INDEX in AITwinPopulation for cube/plane
		// - see AITwinSplineTool::FImpl::SetSelectedSpline for cutout polygon
		ShowOnlyProxiesOfType(Type, /*bIsolationMode*/true);
	}
	return bHasSetSelection;
}

bool AITwinClippingTool::SelectEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex,
	bool bEnterIsolationMode /*= true*/)
{
	return Impl->SelectEffect(Type, PrimitiveIndex, bEnterIsolationMode);
}

std::optional<AITwinClippingTool::FEffectIdentifier> AITwinClippingTool::FImpl::GetSelectedEffect() const
{
	// Recover the selected effect from the population tool or spline tool.

	// First test the population tool:
	int32 InstanceIndex(INDEX_NONE);
	AITwinPopulation const* SelectedPopulation = GetSelectedPopulation(InstanceIndex);
	if (SelectedPopulation)
	{
		if (SelectedPopulation == Owner.ClippingBoxPopulation.Get())
			return std::make_pair(EITwinClippingPrimitiveType::Box, InstanceIndex);
		if (SelectedPopulation == Owner.ClippingPlanePopulation.Get())
			return std::make_pair(EITwinClippingPrimitiveType::Plane, InstanceIndex);
	}
	// Then the spline tool:
	AITwinSplineHelper const* SelectedSpline = nullptr;
	if (ensure(SplineTool.IsValid()) && SplineTool->GetUsage() == EITwinSplineUsage::MapCutout)
	{
		SelectedSpline = SplineTool->GetSelectedSpline();
	}
	if (SelectedSpline)
	{
		int32 PolyEffectIndex = ClippingPolygonInfos.IndexOfByPredicate(
			[SelectedSpline](FITwinClippingCartographicPolygonInfo const& InItem)
		{
			return InItem.SplineHelper.Get() == SelectedSpline;
		});
		if (PolyEffectIndex != INDEX_NONE)
		{
			return std::make_pair(EITwinClippingPrimitiveType::Polygon, PolyEffectIndex);
		}
	}
	return std::nullopt;
}

std::optional<AITwinClippingTool::FEffectIdentifier> AITwinClippingTool::GetSelectedEffect() const
{
	return Impl->GetSelectedEffect();
}

void AITwinClippingTool::DeSelectAll(bool bExitIsolationMode /*= true*/)
{
	auto CurrentSelection = GetSelectedEffect();
	if (CurrentSelection)
	{
		BE_ASSERT(Impl->IsEffectProxyVisible(CurrentSelection->first), "selected but invisible?");
		if (CurrentSelection->first == EITwinClippingPrimitiveType::Box
			|| CurrentSelection->first == EITwinClippingPrimitiveType::Plane)
		{
			Impl->SelectPopulationInstance(nullptr, INDEX_NONE, GetWorld());
		}
		else
		{
			Impl->SelectSpline(nullptr, GetWorld());
		}
		if (bExitIsolationMode)
		{
			// Restore visibility of proxies.
			Impl->SetAllEffectProxiesVisibility(true);
		}
	}
	BroadcastSelection();
}

int32 AITwinClippingTool::GetSelectedPolygonPointInfo(double& OutLatitude, double& OutLongitude) const
{
	UWorld* World = GetWorld();
	if (!World)
		return INDEX_NONE;

	auto const CurrentSelection = GetSelectedEffect();
	if (CurrentSelection && CurrentSelection->first == EITwinClippingPrimitiveType::Polygon)
	{
		auto const& SplineTool = Impl->SplineTool;
		if (ensure(SplineTool.IsValid()) && SplineTool->HasSelectedPoint())
		{
			AITwinSplineHelper const* SelectedSpline = SplineTool->GetSelectedSpline();

			// Always prefer using the geo-located geo-reference
			auto&& Geoloc = FITwinGeolocation::Get(*World);

			ACesiumGeoreference const* GeoRef = Geoloc->GeoReference.IsValid()
				? Geoloc->GeoReference.Get() : SelectedSpline->GlobeAnchor->ResolveGeoreference();
			if (ensure(GeoRef))
			{
				const FTransform Transform = SplineTool->GetSelectionTransform();
				const int32 PointIndex = SplineTool->GetSelectedPointIndex();
				const FVector Cartographic =
					GeoRef->TransformUnrealPositionToLongitudeLatitudeHeight(Transform.GetLocation());
				OutLatitude = Cartographic.Y;
				OutLongitude = Cartographic.X;
				return PointIndex;
			}
		}
	}
	return INDEX_NONE;
}

void AITwinClippingTool::FImpl::SetPolygonPointLocation(int32 PolygonIndex, int32 PointIndex, double Latitude, double Longitude) const
{
	UWorld* World = Owner.GetWorld();
	if (!World)
		return;

	if (ensure(PolygonIndex >= 0 && PolygonIndex < ClippingPolygonInfos.Num())
		&& ClippingPolygonInfos[PolygonIndex].SplineHelper.IsValid())
	{
		AITwinSplineHelper* EditedSpline = ClippingPolygonInfos[PolygonIndex].SplineHelper.Get();

		// Always prefer using the geo-located geo-reference
		auto&& Geoloc = FITwinGeolocation::Get(*World);

		ACesiumGeoreference const* GeoRef = Geoloc->GeoReference.IsValid()
			? Geoloc->GeoReference.Get() : EditedSpline->GlobeAnchor->ResolveGeoreference();

		if (ensure(PointIndex >= 0)
			&& PointIndex < EditedSpline->GetNumberOfSplinePoints()
			&& ensure(GeoRef != nullptr))
		{
			FVector CurrentLocation = EditedSpline->GetLocationAtSplinePoint(PointIndex);
			FVector CurrentCartographic =
				GeoRef->TransformUnrealPositionToLongitudeLatitudeHeight(CurrentLocation);
			// Do not change elevation
			FVector NewUEPosition =
				GeoRef->TransformLongitudeLatitudeHeightPositionToUnreal(
					FVector(Longitude, Latitude, CurrentCartographic.Z));
			EditedSpline->SetLocationAtSplinePoint(PointIndex, NewUEPosition);

			// If this is the currently selected point (which, most of the time, will be the case), we need
			// to synchronize the gizmo.
			if (ensure(SplineTool.IsValid())
				&& EditedSpline == SplineTool->GetSelectedSpline()
				&& PointIndex == SplineTool->GetSelectedPointIndex())
			{
				SplineTool->SplinePointMovedEvent.Broadcast(true /*bMovedInITS*/);
			}
		}
	}
}

void AITwinClippingTool::SetPolygonPointLocation(int32 PolygonIndex, int32 PointIndex, double Latitude, double Longitude) const
{
	Impl->SetPolygonPointLocation(PolygonIndex, PointIndex, Latitude, Longitude);
}

bool AITwinClippingTool::FImpl::GetEffectTransform(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex,
	FTransform& OutTransform, double& OutLatitude, double& OutLongitude, double& OutElevation) const
{
	if (!ensure(PrimitiveIndex >= 0 && PrimitiveIndex < NumEffects(Type)))
		return false;

	UWorld* World = Owner.GetWorld();
	if (!World)
		return false;

	auto&& Geoloc = FITwinGeolocation::Get(*World);
	// Always prefer using the geo-located geo-reference
	ACesiumGeoreference const* GeoRef = Geoloc->GeoReference.IsValid()
		? Geoloc->GeoReference.Get() : Geoloc->LocalReference.Get();

	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:
	case EITwinClippingPrimitiveType::Plane:
	{
		auto const& Population = GetClippingEffectPopulation(Type);
		if (Population.IsValid())
		{
			OutTransform = Population->GetInstanceTransform(PrimitiveIndex);
		}
		else
		{
			return false;
		}
		break;
	}

	case EITwinClippingPrimitiveType::Polygon:
	{
		auto const& PolygonInfo = ClippingPolygonInfos[PrimitiveIndex];
		if (PolygonInfo.SplineHelper.IsValid())
		{
			AITwinSplineHelper const* Polygon = PolygonInfo.SplineHelper.Get();
			if (ensure(Polygon->GlobeAnchor))
			{
				GeoRef = Polygon->GlobeAnchor->ResolveGeoreference();
			}
			OutTransform = Polygon->GetTransformForUserInteraction();
		}
		else
		{
			return false;
		}
		break;
	}

	BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(case EITwinClippingPrimitiveType::Count:, false);
	}

	if (ensure(GeoRef))
	{
		const FVector Cartographic =
			GeoRef->TransformUnrealPositionToLongitudeLatitudeHeight(OutTransform.GetLocation());
		OutLatitude = Cartographic.Y;
		OutLongitude = Cartographic.X;
		OutElevation = Cartographic.Z;
	}
	return true;
}

bool AITwinClippingTool::GetEffectTransform(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex,
	FTransform& OutTransform, double& OutLatitude, double& OutLongitude, double& OutElevation) const
{
	return Impl->GetEffectTransform(Type, PrimitiveIndex, OutTransform, OutLatitude, OutLongitude, OutElevation);
}

bool AITwinClippingTool::GetSelectedEffectTransform(FTransform& OutTransform, double& OutLatitude, double& OutLongitude, double& OutElevation) const
{
	auto CurrentSelection = GetSelectedEffect();
	if (CurrentSelection)
	{
		return GetEffectTransform(CurrentSelection->first, CurrentSelection->second,
			OutTransform, OutLatitude, OutLongitude, OutElevation);
	}
	else
	{
		return false;
	}
}


template <typename FTransfoBuilderFunc>
void AITwinClippingTool::FImpl::ModifyEffectTransformation(FTransfoBuilderFunc const& BuilderFunc,
	EITwinClippingPrimitiveType Type,
	int32 PrimitiveIndex,
	bool bTriggeredFromITS,
	bool bOnlyModifyProxy /*= false*/) const
{
	if (!ensure(PrimitiveIndex >= 0 && PrimitiveIndex < NumEffects(Type)))
		return;

	UWorld* World = Owner.GetWorld();
	if (!World)
		return;

	// Always prefer using the geo-located geo-reference
	auto&& Geoloc = FITwinGeolocation::Get(*World);
	ACesiumGeoreference const* GeoRef = Geoloc->GeoReference.IsValid()
		? Geoloc->GeoReference.Get() : Geoloc->LocalReference.Get();

	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:
	case EITwinClippingPrimitiveType::Plane:
	{
		auto const& Population = GetClippingEffectPopulation(Type);
		if (Population.IsValid())
		{
			FTransform NewTransform = Population->GetInstanceTransform(PrimitiveIndex);
			BuilderFunc(NewTransform, GeoRef);
			if (bOnlyModifyProxy)
				Population->SetInstanceTransformUEOnly(PrimitiveIndex, NewTransform);
			else
				Population->SetInstanceTransform(PrimitiveIndex, NewTransform, bTriggeredFromITS);

			if (ensure(PopulationTool.IsValid())
				&& Population.Get() == PopulationTool->GetSelectedPopulation()
				&& PrimitiveIndex == PopulationTool->GetSelectedInstanceIndex())
			{
				PopulationTool->SelectionChangedEvent.Broadcast();
			}
		}
		break;
	}

	case EITwinClippingPrimitiveType::Polygon:
	{
		auto const& PolygonInfo = ClippingPolygonInfos[PrimitiveIndex];
		if (PolygonInfo.SplineHelper.IsValid())
		{
			AITwinSplineHelper* EditedSpline = PolygonInfo.SplineHelper.Get();
			if (ensure(EditedSpline->GlobeAnchor))
			{
				GeoRef = EditedSpline->GlobeAnchor->ResolveGeoreference();
			}
			FTransform NewTransform = EditedSpline->GetTransformForUserInteraction();
			BuilderFunc(NewTransform, GeoRef);
			EditedSpline->SetTransformFromUserInteraction(NewTransform);

			// If this is the currently selected point (which, most of the time, will be the case), we need
			// to synchronize the gizmo.
			if (ensure(SplineTool.IsValid())
				&& EditedSpline == SplineTool->GetSelectedSpline())
			{
				SplineTool->SplineSelectionEvent.Broadcast();
			}
		}
		break;
	}

	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinClippingPrimitiveType::Count:);
	}
}

void AITwinClippingTool::SetEffectLocation(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex,
	double InLatitude, double InLongitude, double InElevation,
	bool bTriggeredFromITS) const
{
	Impl->ModifyEffectTransformation(
		[=](FTransform& NewTransform, ACesiumGeoreference const* GeoRef)
	{
		if (ensure(GeoRef))
		{
			FVector NewUEPosition =
				GeoRef->TransformLongitudeLatitudeHeightPositionToUnreal(
					FVector(InLongitude, InLatitude, InElevation));
			NewTransform.SetLocation(NewUEPosition);
		}
	}, Type, PrimitiveIndex, bTriggeredFromITS);
}

void AITwinClippingTool::SetEffectRotation(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex,
	double InRotX, double InRotY, double InRotZ,
	bool bTriggeredFromITS) const
{
	Impl->ModifyEffectTransformation(
		[=](FTransform& NewTransform, ACesiumGeoreference const* GeoRef)
	{
		FVector Rot(InRotX, InRotY, InRotZ);
		NewTransform.SetRotation(FQuat::MakeFromEuler(Rot));
	}, Type, PrimitiveIndex, bTriggeredFromITS);
}


namespace ITwinClippingDetails
{
	double GetClippingPlaneScale(FTransform const& PlaneTransform, FCameraViewInfo const& ViewInfo,
		FBox const& InfluenceBoundingBox)
	{
		auto const PlaneLocation = PlaneTransform.GetLocation();
		auto const ToCamera = ViewInfo.CameraLocation - PlaneLocation;
		static constexpr double ScalingFactor = 0.006;
		double const PlaneWorldScale = FMath::Abs(FVector::DotProduct(ToCamera, ViewInfo.ViewDirection))
			* ViewInfo.FovScaling
			* ViewInfo.DPIScaling
			* ScalingFactor;
		if (InfluenceBoundingBox.IsValid)
		{
			// Limit plane size to the half of the max dimension of the influence bounding box, to avoid too
			// big planes when the camera is far from the plane.
			constexpr double PLANE_TO_UNREAL = 0.01; // because the imported geometry is exactly 1 meter.
			return FMath::Min(PlaneWorldScale, InfluenceBoundingBox.GetSize().GetMax() * PLANE_TO_UNREAL * 0.75);
		}
		else
		{
			return PlaneWorldScale;
		}
	}


	inline double SnapToGrid1D(double Value, double SnapGridSize)
	{
		if (ensure(SnapGridSize > 0))
			return FMath::RoundToFloat(Value / SnapGridSize) * SnapGridSize;
		else
			return SnapGridSize;
	}

	inline FVector SnapVectorToGrid(FVector const& Vec, double SnapGridSize)
	{
		if (ensure(SnapGridSize > 0))
		{
			return FVector(
				FMath::RoundToFloat(Vec.X / SnapGridSize) * SnapGridSize,
				FMath::RoundToFloat(Vec.Y / SnapGridSize) * SnapGridSize,
				FMath::RoundToFloat(Vec.Z / SnapGridSize) * SnapGridSize);
		}
		else
		{
			return Vec;
		}
	}

	static FVector SnapLocationToGrid(FVector const& WorldPosition,
		FBox const& RefBoundingBox,
		double SnapGridSize)
	{
		if (ensure(SnapGridSize > 0))
		{
			if (RefBoundingBox.IsValid)
			{
				FVector const RelativePos = WorldPosition - RefBoundingBox.Min;
				return RefBoundingBox.Min + FVector(
					FMath::RoundToFloat(RelativePos.X / SnapGridSize) * SnapGridSize,
					FMath::RoundToFloat(RelativePos.Y / SnapGridSize) * SnapGridSize,
					FMath::RoundToFloat(RelativePos.Z / SnapGridSize) * SnapGridSize);
			}
			else
			{
				return FVector(
					FMath::RoundToFloat(WorldPosition.X / SnapGridSize) * SnapGridSize,
					FMath::RoundToFloat(WorldPosition.Y / SnapGridSize) * SnapGridSize,
					FMath::RoundToFloat(WorldPosition.Z / SnapGridSize) * SnapGridSize);
			}
		}
		else
		{
			return WorldPosition;
		}
	}

	void UpdateClippingPlaneScale(AITwinPopulation& Population, int32 InstanceIndex, FCameraViewInfo const& ViewInfo,
		FBox const& InfluenceBoundingBox)
	{
		BE_ASSERT(Population.GetObjectType() == EITwinInstantiatedObjectType::ClippingPlane);
		auto PlaneTsf = Population.GetInstanceTransform(InstanceIndex);
		double const PlaneWorldScale = GetClippingPlaneScale(PlaneTsf, ViewInfo, InfluenceBoundingBox);
		if (PlaneWorldScale > 0)
		{
			double const ScaleFactor = PlaneWorldScale / PlaneTsf.GetScale3D().X;
			if (FMath::Abs(ScaleFactor - 1.0) > 0.02)
			{
				PlaneTsf.MultiplyScale3D(FVector(ScaleFactor));
				Population.SetInstanceTransformUEOnly(InstanceIndex, PlaneTsf);
			}
		}
	}

	inline double GetSnapGridSize(FBox const& InfluenceBoundingBox)
	{
		if (InfluenceBoundingBox.IsValid)
		{
			return FMath::Max(InfluenceBoundingBox.GetSize().GetMax() * 0.001,
				1000.0);
		}
		else
		{
			// Infinite influence (Google tileset)
			// Let's use a fixed size of 20 meters.
			return 20 * 100.0;
		}
	}

}

bool AITwinClippingTool::FImpl::RecenterPlaneProxyAtRayIntersection(int32 PlaneIndex,
	FVector const& TraceStart, FVector const& TraceDir,
	bool bClampToInfluenceBounds)
{
	using namespace ITwinClippingDetails;
	if (!ensure(PlaneIndex >= 0 && PlaneIndex < NumEffects(EITwinClippingPrimitiveType::Plane)))
	{
		return false;
	}
	FVector PlaneOrientation = FVector::ZAxisVector;
	FVector::FReal PlaneW(0.);
	if (!GetPlaneEquationFromUEInstance(PlaneOrientation, PlaneW, PlaneIndex))
	{
		return false;
	}
	FPlane const Plane(PlaneOrientation, PlaneW);

	// Try to intersect the infinite plane defined by the cutout with the ray.
	FVector HitPoint;
	double Distance(0.);
	if (ITwin::RayPlaneIntersection(TraceStart, TraceDir, Plane, HitPoint, Distance))
	{
		std::optional<FVector> ClampedLocationOpt;

		if (bClampToInfluenceBounds)
		{
			FITwinClippingPlaneInfo& PlaneInfo = ClippingPlaneInfos[PlaneIndex];
			// Retrieve the influence bounds of the plane, to limit the re-centering within these bounds.
			if (PlaneInfo.bNeedsUpdateBoundingBox)
			{
				PlaneInfo.UpdateInfluenceBoundingBox(Owner.GetWorld());
			}
			FBox InfluenceBoundingBox = PlaneInfo.GetInfluenceBoundingBox();
			if (InfluenceBoundingBox.IsValid)
			{
				// Try to shrink the influence box by the current plane box, to avoid snapping on the edge
				// of the plane box instead of the influence bounds when the plane box is almost as big as
				// the influence bounds.
				FVector const InfluenceBoxExtent = InfluenceBoundingBox.GetExtent();

				// We already tested this population validity in #GetPlaneEquationFromUEInstance, so we can
				// safely use it here.
				auto const& Population = GetClippingEffectPopulation(EITwinClippingPrimitiveType::Plane);
				FBox const CurrentPlaneBox = Population->GetInstanceBoundingBox(PlaneIndex);
				FVector PlaneBoxExtent = SnapVectorToGrid(CurrentPlaneBox.GetExtent(), 500.);
				PlaneBoxExtent = FVector::Min(PlaneBoxExtent, 0.8 * InfluenceBoxExtent);
				InfluenceBoundingBox = InfluenceBoundingBox.ExpandBy(-PlaneBoxExtent);
			}

			FVector ClosestPointOnOrInsideBox;
			if (!InfluenceBoundingBox.IsValid || InfluenceBoundingBox.IsInsideOrOn(HitPoint))
			{
				// The hit point is already inside the influence bounds.
				ClosestPointOnOrInsideBox = HitPoint;
			}
			else
			{
				ClosestPointOnOrInsideBox = InfluenceBoundingBox.GetClosestPointTo(HitPoint);
			}

			// Snap position to a fixed grid depending on the influence box.
			const double SnapGridSize = GetSnapGridSize(InfluenceBoundingBox);
			FVector const ClosestPoint = SnapLocationToGrid(ClosestPointOnOrInsideBox, InfluenceBoundingBox, SnapGridSize);

			FVector Dir_Closest = ClosestPoint - TraceStart;
			FVector HitPoint_Closest;
			double Distance_Closest(0.);
			if (Dir_Closest.Normalize()
				&& ITwin::RayPlaneIntersection(TraceStart, Dir_Closest, Plane, HitPoint_Closest, Distance_Closest))
			{
				ClampedLocationOpt = HitPoint_Closest;
			}
		}

		const FVector FinalPlaneLocation = ClampedLocationOpt.value_or(HitPoint);
		// Move the center of the plane to the intersected point. This does not change the plane equation, so
		// we should not invalidate DB nor recompute the plane effect: only modify the proxy transform.
		ModifyEffectTransformation(
			[&FinalPlaneLocation](FTransform& NewTransform, ACesiumGeoreference const* /*GeoRef*/)
		{
			NewTransform.SetLocation(FinalPlaneLocation);
		},
			EITwinClippingPrimitiveType::Plane,
			PlaneIndex,
			false /*bTriggeredFromITS*/,
			true /*bOnlyModifyProxy*/);

		return true;
	}
	else
	{
		return false;
	}
}

bool AITwinClippingTool::RecenterPlaneProxy(int32 PlaneIndex)
{
	// Build a ray starting from the center of the screen, and try to intersect the infinite plane defined by
	// the cutout.
	FVector TraceStart, TraceDir;
	if (!ITwin::GetRayToTraceFromScreenCenter(this, TraceStart, TraceDir))
	{
		return false;
	}
	return Impl->RecenterPlaneProxyAtRayIntersection(PlaneIndex, TraceStart, TraceDir, true);
}

bool AITwinClippingTool::FImpl::RecenterPlaneProxyFromPicking(int32 PlaneIndex)
{
	FVector TraceStart, TraceDir;
	FVector2D MousePosition;
	if (!FITwinTracingHelper::GetRayFromMousePosition(Owner.GetWorld(),
		MousePosition, TraceStart, TraceDir))
	{
		return false;
	}
	return RecenterPlaneProxyAtRayIntersection(PlaneIndex, TraceStart, TraceDir, false);
}

void AITwinClippingTool::FImpl::InvalidateBoundingBoxOfClippingPlanes(ITwin::ModelLink const& ModelLink)
{
	const int32 NumPlanes = NumEffects(EITwinClippingPrimitiveType::Plane);
	for (int32 PlaneIndex = 0; PlaneIndex < NumPlanes; ++PlaneIndex)
	{
		FITwinClippingPlaneInfo& PlaneInfo = ClippingPlaneInfos[PlaneIndex];
		if (PlaneInfo.ShouldInfluenceModel(ModelLink))
		{
			PlaneInfo.InvalidateInfluenceBoundingBox();
		}
	}
}

void AITwinClippingTool::FImpl::Tick(float DeltaTime)
{
	const int32 NumPlanes = NumEffects(EITwinClippingPrimitiveType::Plane);
	if (NumPlanes <= 0)
	{
		return;
	}
	auto const& PlanePopulation = GetClippingEffectPopulation(EITwinClippingPrimitiveType::Plane);
	if (!PlanePopulation.IsValid())
	{
		return;
	}

	// Build a ray starting from the center of the screen, and try to intersect the infinite plane defined by
	// each cutout.
	FVector TraceStart, TraceDir;
	if (!ITwin::GetRayToTraceFromScreenCenter(&Owner, TraceStart, TraceDir))
	{
		return;
	}
	using namespace ITwinClippingDetails;
	auto const ViewInfoOpt = GetCameraViewInfo(Owner.GetWorld());

	if (PlanePopulation->IsBeingInteractivelyTransformed())
	{
		// Don't update the plane proxy position/scale while the user is transforming it, to avoid conflicts
		// between the automatic update and the user transformation.
		// Also, when the transformation is done, we should not recenter the proxy at once, or this will
		// cause a jump of the gizmo, which can be really disturbing.
		if (!LastSelectedPlaneIndex.has_value())
		{
			LastSelectedPlaneIndex = PlanePopulation->GetSelectedInstanceIndex();
			LastViewInfo = ViewInfoOpt;
		}
		return;
	}
	else if (LastSelectedPlaneIndex.has_value())
	{
		// If nothing has changed since last update, avoid recomputing the gizmo position: it could cycle
		// between 2 positions when the user is looking in a specific direction (parallel to the plane),
		// which can be really disturbing.
		// This is due to the different snapping behavior we use to find the new position of the proxies.
		if (LastSelectedPlaneIndex.value() == PlanePopulation->GetSelectedInstanceIndex()
			&& ViewInfoOpt.has_value() == LastViewInfo.has_value()
			&& (!ViewInfoOpt.has_value() || ViewInfoOpt->Equals(*LastViewInfo))
			&& !ClippingPlaneInfos.ContainsByPredicate([](FITwinClippingPlaneInfo const& Info)
			{
				return Info.bNeedsUpdateBoundingBox;
			}))
		{
			return;
		}
	}

	for (int32 PlaneIndex = 0; PlaneIndex < NumPlanes; ++PlaneIndex)
	{
		bool const bRecentered = RecenterPlaneProxyAtRayIntersection(PlaneIndex, TraceStart, TraceDir, true);

		if (bRecentered && ViewInfoOpt)
		{
			FITwinClippingPlaneInfo const& PlaneInfo = ClippingPlaneInfos[PlaneIndex];
			ensure(!PlaneInfo.bNeedsUpdateBoundingBox);
			UpdateClippingPlaneScale(*PlanePopulation, PlaneIndex, *ViewInfoOpt,
				PlaneInfo.GetInfluenceBoundingBox());
		}
	}

	LastSelectedPlaneIndex = PlanePopulation->GetSelectedInstanceIndex();
	LastViewInfo = ViewInfoOpt;
}

void AITwinClippingTool::FImpl::ZoomOnEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex)
{
	if (!ensure(PrimitiveIndex >= 0 && PrimitiveIndex < NumEffects(Type)))
		return;

	FBox FocusBBox;
	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:
	//case EITwinClippingPrimitiveType::Plane:
	{
		auto const& Population = GetClippingEffectPopulation(Type);
		if (Population.IsValid())
		{
			FocusBBox = Population->GetInstanceBoundingBox(PrimitiveIndex);
		}
		break;
	}

	case EITwinClippingPrimitiveType::Plane:
	{
		// The position of the (infinite) planes is not relevant (and changes with the camera view).
		// So instead of working on the instance, we will use the influenced bounding box.
		FITwinClippingPlaneInfo& PlaneInfo = ClippingPlaneInfos[PrimitiveIndex];
		if (PlaneInfo.bNeedsUpdateBoundingBox)
		{
			PlaneInfo.UpdateInfluenceBoundingBox(Owner.GetWorld());
		}
		FocusBBox = PlaneInfo.GetInfluenceBoundingBox();
		if (!FocusBBox.IsValid)
		{
			// If influence has no bounds (case of Google tilesets), we will just zoom on the plane proxy
			// itself, even if it is not really relevant.
			auto const& Population = GetClippingEffectPopulation(Type);
			if (Population.IsValid())
			{
				FocusBBox = Population->GetInstanceBoundingBox(PrimitiveIndex);
			}
		}
		break;
	}

	case EITwinClippingPrimitiveType::Polygon:
	{
		auto const& PolygonInfo = ClippingPolygonInfos[PrimitiveIndex];
		if (PolygonInfo.SplineHelper.IsValid())
		{
			// AzDev#1967143 => for a polygon, use overview camera for zoom
			//PolygonInfo.SplineHelper->IncludeInWorldBox(FocusBBox);
			Owner.OnOverviewCamera(PolygonInfo.SplineHelper.Get());
		}
		break;
	}

	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinClippingPrimitiveType::Count:);
	}
	if (FocusBBox.IsValid)
	{
		UITwinUtilityLibrary::ZoomOn(FocusBBox, Owner.GetWorld() /*, MinDistanceToCenter*/);
	}
}

void AITwinClippingTool::ZoomOnEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex)
{
	Impl->ZoomOnEffect(Type, PrimitiveIndex);
}


void AITwinClippingTool::FImpl::SetEffectVisibility(EITwinClippingPrimitiveType EffectType, bool bVisibleInGame,
	bool bIsolationMode /*= false*/)
{
	switch (EffectType)
	{
	case EITwinClippingPrimitiveType::Box:
	case EITwinClippingPrimitiveType::Plane:
	{
		auto const& Population = GetClippingEffectPopulation(EffectType);
		if (Population.IsValid())
			Population->SetHiddenInGame(!bVisibleInGame);
		break;
	}

	case EITwinClippingPrimitiveType::Polygon:
	{
		for (TActorIterator<AITwinSplineHelper> SplineIter(Owner.GetWorld()); SplineIter; ++SplineIter)
		{
			if ((*SplineIter)->GetUsage() == EITwinSplineUsage::MapCutout)
			{
				// For isolation mode, we also test the selection status.
				const bool bShowSpline = bVisibleInGame
					&& (!bIsolationMode || (*SplineIter)->IsSelected());
				(*SplineIter)->SetActorHiddenInGame(!bShowSpline);
			}
		}
		break;
	}

	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinClippingPrimitiveType::Count: );
	}

	if (EffectType == EITwinClippingPrimitiveType::Plane)
	{
		// Enable Tick for plane proxies, as we need to update their scale and position each frame to keep
		// them visible for the user.
		Owner.SetActorTickEnabled(bVisibleInGame);
	}
}

bool AITwinClippingTool::FImpl::IsEffectProxyVisible(EITwinClippingPrimitiveType EffectType) const
{
	switch (EffectType)
	{
	case EITwinClippingPrimitiveType::Box:
	case EITwinClippingPrimitiveType::Plane:
	{
		auto const& Population = GetClippingEffectPopulation(EffectType);
		return Population.IsValid() && !Population->IsHiddenInGame();
	}

	case EITwinClippingPrimitiveType::Polygon:
	{
		for (TActorIterator<AITwinSplineHelper> SplineIter(Owner.GetWorld()); SplineIter; ++SplineIter)
		{
			if ((*SplineIter)->GetUsage() == EITwinSplineUsage::MapCutout
				&& !(*SplineIter)->IsHidden())
			{
				return true;
			}
		}
		break;
	}

	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinClippingPrimitiveType::Count:);
	}

	return false;
}

void AITwinClippingTool::FImpl::SetAllEffectProxiesVisibility(bool bVisibleInGame)
{
	for (EITwinClippingPrimitiveType Type : TEnumRange<EITwinClippingPrimitiveType>())
	{
		SetEffectVisibility(Type, bVisibleInGame);
	}
}

void AITwinClippingTool::FImpl::ShowOnlyProxiesOfType(EITwinClippingPrimitiveType SelectedType, bool bIsolationMode)
{
	for (EITwinClippingPrimitiveType Type : TEnumRange<EITwinClippingPrimitiveType>())
	{
		SetEffectVisibility(Type, Type == SelectedType, bIsolationMode);
	}
}

void AITwinClippingTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	Impl->Tick(DeltaTime);
}

void AITwinClippingTool::OnActivatePicking(bool bActivate)
{
	if (bActivate)
	{
		// Beware the tool can be activated *after* the user selects a cutout from the list in the UI: in
		// such case, we should not make all proxies visible, but instead preserve the current isolation
		// mode.
		auto const CurrentSelection = GetSelectedEffect();
		if (CurrentSelection)
			Impl->ShowOnlyProxiesOfType(CurrentSelection->first, /*bIsolationMode*/true);
		else
			Impl->SetAllEffectProxiesVisibility(true);
	}
	else
	{
		Impl->HideAllEffectProxies();
	}
}

bool AITwinClippingTool::DoMouseClickPicking(bool& bOutSelectionGizmoNeeded)
{
	bool bRelevantAction = false;
	bOutSelectionGizmoNeeded = false;
	UWorld* World = GetWorld();
	if (!World)
		return false;

	auto const OldSelection = GetSelectedEffect();

	// Handle rotation center picking (for cutout planes).
	if (Impl->bIsRotationCenterPickingMode)
	{
		if (OldSelection && OldSelection->first == EITwinClippingPrimitiveType::Plane)
		{
			if (Impl->RecenterPlaneProxyFromPicking(OldSelection->second))
			{
				// The plane rotation center is now selected => exit center picking mode in order to activate
				// the rotation gizmo.
				Impl->QuitRotationCenterPickingMode();
				bOutSelectionGizmoNeeded = bRelevantAction = true;
				return true;
			}
		}
		else
		{
			// No plane selected => quit plane center picking mode.
			Impl->QuitRotationCenterPickingMode();
		}
	}

	// Test population then cut-out splines.

	// Note that we can only have one active tool at a time, but we don't want the cutout splines to be
	// hidden just because we temporarily disable the spline tool...
	AITwinSplineTool::FAutomaticVisibilityDisabler AutoVisDisabler;

	if (Impl->ClippingBoxInfos.Num() + Impl->ClippingPlaneInfos.Num() > 0)
	{
		auto const PopulationTool = Impl->ActivatePopulationTool(World);
		if (PopulationTool.IsValid())
		{
			AITwinPopulationTool::FPickingContext RestrictOnClipping(*PopulationTool, true);
			bRelevantAction = PopulationTool->DoMouseClickAction();
			if (bRelevantAction)
				bOutSelectionGizmoNeeded = PopulationTool->HasSelectedPopulation();
		}
	}
	if (!bRelevantAction && Impl->ClippingPolygonInfos.Num() > 0)
	{
		auto const SplineTool = Impl->ActivateSplineTool(World);
		if (SplineTool.IsValid())
		{
			// Quick fix for point selection/insertion: we need to restore the initial selection, if a
			// polygon was selected, as point operations are only allowed on the selected spline (and
			// the selection is lost when the spline tool is disabled through ActivatePopulationTool...)
			if (OldSelection
				&& OldSelection->first == EITwinClippingPrimitiveType::Polygon
				&& OldSelection->second >= 0
				&& OldSelection->second < Impl->ClippingPolygonInfos.Num())
			{
				SplineTool->SetSelectedSpline(Impl->ClippingPolygonInfos[OldSelection->second].SplineHelper.Get());
			}
			bRelevantAction = SplineTool->DoMouseClickAction();
			if (bRelevantAction)
				bOutSelectionGizmoNeeded = SplineTool->HasSelection();
		}
	}

	auto const NewSelection = GetSelectedEffect();
	if (NewSelection)
	{
		// Isolation of the selected item, if any.
		if (!OldSelection || OldSelection->first != NewSelection->first)
		{
			Impl->ShowOnlyProxiesOfType(NewSelection->first, true);
		}
		// If the selected tool is Rotate, and we select a different plane, let's enter rotation center
		// picking mode again:
		if (NewSelection != OldSelection
			&& NewSelection->first == EITwinClippingPrimitiveType::Plane
			&& Impl->IsRotationMode())
		{
			Impl->SetRotationCenterPickingMode(true);
			bOutSelectionGizmoNeeded = true;
		}
	}
	else if (OldSelection)
	{
		// End of isolation mode.
		Impl->SetAllEffectProxiesVisibility(true);
	}

	// Notify new selection. If nothing is selected, notify it as well (using -1 as index).
	BroadcastSelection();

	return bRelevantAction;
}

void AITwinClippingTool::BroadcastSelection()
{
	// Notify new selection. If nothing is selected, notify it as well (using -1 as index).
	auto const NewSelection = GetSelectedEffect();
	if (NewSelection)
	{
		EffectSelectedEvent.Broadcast(NewSelection->first, NewSelection->second);
	}
	else
	{
		EffectSelectedEvent.Broadcast(
			static_cast<EITwinClippingPrimitiveType>(0), -1);
	}
}

void AITwinClippingTool::OnOverviewCamera(AITwinSplineHelper const* SpecificSpline /*= nullptr*/)
{
	UWorld* World = GetWorld();
	if (!World)
		return;
	TWeakObjectPtr<AITwinSplineTool> SplineTool = Impl->ActivateSplineTool(World);
	if (SplineTool.IsValid())
	{
		SplineTool->OnOverviewCamera(SpecificSpline);
	}
}

void AITwinClippingTool::SetTransformationMode(ETransformationMode Mode)
{
	AITwinInteractiveTool* ActiveTool = AITwinInteractiveTool::GetActiveTool(GetWorld());
	if (ActiveTool && ActiveTool->IsUsedOnCutoutPrimitive())
	{
		if (ActiveTool->IsPopulationTool())
		{
			Impl->SetTransformationMode(Mode);

			// Trigger event to refresh the selection gizmo, typically.
			ActivationEvent.Broadcast(true);
		}
	}
}

template <typename Func>
void AITwinClippingTool::FImpl::VisitClippingPrimitivesOfType(EITwinClippingPrimitiveType Type, Func const& Fun)
{
	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:
		for (auto& BoxInfo : ClippingBoxInfos)
		{
			Fun(BoxInfo);
		}
		break;

	case EITwinClippingPrimitiveType::Plane:
		for (auto& PlaneInfo : ClippingPlaneInfos)
		{
			Fun(PlaneInfo);
		}
		break;

	case EITwinClippingPrimitiveType::Polygon:
		for (auto& PolygonInfo : ClippingPolygonInfos)
		{
			Fun(PolygonInfo);
		}
		break;

	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinClippingPrimitiveType::Count: );
	}
}

#if WITH_EDITOR

void AITwinClippingTool::FImpl::ActivateEffects(EITwinClippingPrimitiveType Type, EITwinClippingEffectLevel Level, bool bActivate)
{
	if (Level == EITwinClippingEffectLevel::Tileset)
	{
		VisitClippingPrimitivesOfType(Type,	[bActivate](FITwinClippingInfoBase& PrimitiveInfo)
		{
			PrimitiveInfo.ActivateEffectAtTilesetLevel(bActivate);
		});
	}

	if (Level == EITwinClippingEffectLevel::Shader
		&& Type != EITwinClippingPrimitiveType::Polygon)
	{
		UMaterialParameterCollectionInstance* MPCInstance = GetMPCClippingInstance();
		if (ensure(MPCInstance))
		{
			switch (Type)
			{
			case EITwinClippingPrimitiveType::Box:
				MPCInstance->SetScalarParameterValue(
					ClippingPrimitiveTrait<EITwinClippingPrimitiveType::Box>::PrimitiveCountName,
					static_cast<float>(bActivate ? ClippingBoxInfos.Num() : 0));
				break;

			case EITwinClippingPrimitiveType::Plane:
				MPCInstance->SetScalarParameterValue(
					ClippingPrimitiveTrait<EITwinClippingPrimitiveType::Plane>::PrimitiveCountName,
					static_cast<float>(bActivate ? ClippingPlaneInfos.Num() : 0));
				break;

			BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(
			case EITwinClippingPrimitiveType::Polygon:
			case EITwinClippingPrimitiveType::Count: );
			}
		}
	}
}

void AITwinClippingTool::ActivateEffects(EITwinClippingPrimitiveType Type, EITwinClippingEffectLevel Level, bool bActivate)
{
	Impl->ActivateEffects(Type, Level, bActivate);
}
void AITwinClippingTool::ActivateEffectsAllLevels(EITwinClippingPrimitiveType Type, bool bActivate)
{
	ActivateEffects(Type, EITwinClippingEffectLevel::Tileset,	bActivate);
	ActivateEffects(Type, EITwinClippingEffectLevel::Shader,	bActivate);
}

#endif // WITH_EDITOR


bool AITwinClippingTool::FImpl::IsEffectEnabled(EITwinClippingPrimitiveType EffectType, int32 Index) const
{
	if (ensure(Index < NumEffects(EffectType)))
	{
		return GetClippingEffect(EffectType, Index).IsEnabled();
	}
	return false;
}

bool AITwinClippingTool::IsEffectEnabled(EITwinClippingPrimitiveType EffectType, int32 Index) const
{
	return Impl->IsEffectEnabled(EffectType, Index);
}

void AITwinClippingTool::FImpl::EnableEffect(EITwinClippingPrimitiveType EffectType, int32 Index, bool bInEnabled)
{
	if (ensure(Index < NumEffects(EffectType)))
	{
		GetMutableClippingEffect(EffectType, Index).SetEnabled(bInEnabled);
		UpdateAllTilesets(EffectType);

		if (EffectType != EITwinClippingPrimitiveType::Polygon)
		{
			UpdateAVizInstanceProperties(EffectType, Index);
		}
	}
}

void AITwinClippingTool::EnableEffect(EITwinClippingPrimitiveType EffectType, int32 Index, bool bInEnabled)
{
	Impl->EnableEffect(EffectType, Index, bInEnabled);
}

void AITwinClippingTool::FImpl::EnableAllEffectsOfType(EITwinClippingPrimitiveType Type, bool bInEnabled)
{
	const int32 NumEff = NumEffects(Type);
	for (int32 i(0); i < NumEff; ++i)
	{
		GetMutableClippingEffect(Type, i).SetEnabled(bInEnabled);
		if (Type != EITwinClippingPrimitiveType::Polygon)
		{
			UpdateAVizInstanceProperties(Type, i);
		}
	}
	UpdateAllTilesets(Type);
}

void AITwinClippingTool::EnableAllEffects(bool bInEnabled)
{
	for (EITwinClippingPrimitiveType Type : TEnumRange<EITwinClippingPrimitiveType>())
	{
		Impl->EnableAllEffectsOfType(Type, bInEnabled);
	}
}

bool AITwinClippingTool::FImpl::ShouldEffectInfluenceModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
	const ITwin::ModelLink& ModelIdentifier) const
{
	if (ensure(EffectIndex < NumEffects(EffectType)))
	{
		return GetClippingEffect(EffectType, EffectIndex).ShouldInfluenceModel(ModelIdentifier);
	}
	return false;
}

bool AITwinClippingTool::ShouldEffectInfluenceModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
	const ITwin::ModelLink& ModelIdentifier) const
{
	return Impl->ShouldEffectInfluenceModel(EffectType, EffectIndex, ModelIdentifier);
}

bool AITwinClippingTool::FImpl::ShouldEffectInfluenceFullModelType(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
	EITwinModelType ModelType) const
{
	if (ensure(EffectIndex < NumEffects(EffectType)))
	{
		return GetClippingEffect(EffectType, EffectIndex).ShouldInfluenceFullModelType(ModelType);
	}
	return false;
}

bool AITwinClippingTool::ShouldEffectInfluenceFullModelType(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
	EITwinModelType ModelType) const
{
	return Impl->ShouldEffectInfluenceFullModelType(EffectType, EffectIndex, ModelType);
}

void AITwinClippingTool::FImpl::SetEffectInfluenceFullModelType(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
	EITwinModelType ModelType, bool bAll)
{
	if (ensure(EffectIndex < NumEffects(EffectType)))
	{
		GetMutableClippingEffect(EffectType, EffectIndex).SetInfluenceFullModelType(ModelType, bAll);
		UpdateAllTilesets(EffectType);

		if (EffectType != EITwinClippingPrimitiveType::Polygon)
		{
			UpdateAVizInstanceProperties(EffectType, EffectIndex);
		}
	}
}

void AITwinClippingTool::SetEffectInfluenceFullModelType(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
	EITwinModelType ModelType, bool bAll)
{
	Impl->SetEffectInfluenceFullModelType(EffectType, EffectIndex, ModelType, bAll);
}

void AITwinClippingTool::FImpl::SetEffectInfluenceSpecificModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
	const ITwin::ModelLink& ModelIdentifier, bool bInfluence)
{
	if (ensure(EffectIndex < NumEffects(EffectType)))
	{
		GetMutableClippingEffect(EffectType, EffectIndex).SetInfluenceSpecificModel(ModelIdentifier, bInfluence);
		UpdateAllTilesets(EffectType);

		if (EffectType != EITwinClippingPrimitiveType::Polygon)
		{
			UpdateAVizInstanceProperties(EffectType, EffectIndex);
		}
	}
}

void AITwinClippingTool::SetEffectInfluenceSpecificModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
	const ITwin::ModelLink& ModelIdentifier, bool bInfluence)
{
	Impl->SetEffectInfluenceSpecificModel(EffectType, EffectIndex, ModelIdentifier, bInfluence);
}

AdvViz::SDK::RefID AITwinClippingTool::FImpl::GetEffectId(EITwinClippingPrimitiveType EffectType, int32 EffectIndex) const
{
	switch (EffectType)
	{
	case EITwinClippingPrimitiveType::Box:
	case EITwinClippingPrimitiveType::Plane:
	{
		auto const& Population = GetClippingEffectPopulation(EffectType);
		if (Population.IsValid())
		{
			return Population->GetInstanceRefId(EffectIndex);
		}
		break;
	}

	case EITwinClippingPrimitiveType::Polygon:
		if (EffectIndex >= 0 && EffectIndex < ClippingPolygonInfos.Num())
		{
			auto const& PolygonInfo = ClippingPolygonInfos[EffectIndex];
			if (PolygonInfo.SplineHelper.IsValid())
			{
				return PolygonInfo.SplineHelper->GetAVizSplineId();
			}
		}
		break;

	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinClippingPrimitiveType::Count: );
	}
	return AdvViz::SDK::RefID::Invalid();
}

AdvViz::SDK::RefID AITwinClippingTool::GetEffectId(EITwinClippingPrimitiveType EffectType, int32 EffectIndex) const
{
	return Impl->GetEffectId(EffectType, EffectIndex);
}

int32 AITwinClippingTool::FImpl::GetEffectIndex(EITwinClippingPrimitiveType EffectType, AdvViz::SDK::RefID const& RefID) const
{
	switch (EffectType)
	{
	case EITwinClippingPrimitiveType::Box:
	case EITwinClippingPrimitiveType::Plane:
	{
		auto const& Population = GetClippingEffectPopulation(EffectType);
		if (Population.IsValid())
		{
			return Population->GetInstanceIndexFromRefId(RefID);
		}
		break;
	}
	case EITwinClippingPrimitiveType::Polygon:
		return ClippingPolygonInfos.IndexOfByPredicate(
			[&RefID](FITwinClippingCartographicPolygonInfo const& InItem)
		{
			return InItem.SplineHelper.IsValid()
				&& InItem.SplineHelper->GetAVizSplineId() == RefID;
		});

	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinClippingPrimitiveType::Count: );
	}
	return INDEX_NONE;
}

int32 AITwinClippingTool::GetEffectIndex(EITwinClippingPrimitiveType EffectType, AdvViz::SDK::RefID const& RefID) const
{
	return Impl->GetEffectIndex(EffectType, RefID);
}

namespace ITwin::Clipping
{
	// Encode cutout properties as a string (temporary solution for persistence, as long as we do not save
	// clipping shapes in SceneAPI (nor population instances in another iTwin service...)
	std::string EncodeProperties(const FITwinClippingInfoBase& Prop)
	{
		std::string EncodedInfo("clipping (");
		if (!Prop.IsEnabled())
			EncodedInfo += "OFF-";
		if (Prop.GetInvertEffect())
			EncodedInfo += "inv-";
		for (EITwinModelType ModelType : { EITwinModelType::IModel,
			EITwinModelType::RealityData,
			EITwinModelType::GlobalMapLayer })
		{
			if (Prop.ShouldInfluenceFullModelType(ModelType))
				EncodedInfo += std::string("infl") + std::to_string(static_cast<uint8_t>(ModelType)) + "-";
		}
		EncodedInfo += ")";
		return EncodedInfo;
	}

	bool DecodeProperties(const std::string& EncodedInfo, FITwinClippingInfoBase& Prop)
	{
		if (!EncodedInfo.starts_with("clipping"))
		{
			return false;
		}
		Prop.SetEnabled(EncodedInfo.find("OFF-") == std::string::npos);
		Prop.SetInvertEffect(EncodedInfo.find("inv-") != std::string::npos);
		static const std::string StrInflPrefix("infl");
		for (EITwinModelType ModelType : { EITwinModelType::IModel,
			EITwinModelType::RealityData,
			EITwinModelType::GlobalMapLayer })
		{
			const std::string strInfl = StrInflPrefix + std::to_string(static_cast<uint8_t>(ModelType)) + "-";
			Prop.SetInfluenceFullModelType(ModelType,
				EncodedInfo.find(strInfl) != std::string::npos);
		}
		return true;
	}

	void ConfigureNewInstance(const AdvViz::SDK::IInstancePtr& AVizInstance, AActor& HitActor)
	{
		// Determine the target layer type, if we have hit a tileset owner:
		auto TilesetAccess = GetTilesetAccess(&HitActor);
		if (!TilesetAccess)
			return;
		const EITwinModelType HitModelType = TilesetAccess->GetModelType();

		FITwinClippingInfoBase ClippingProps;
		for (EITwinModelType ModelType : { EITwinModelType::IModel,
			EITwinModelType::RealityData,
			EITwinModelType::GlobalMapLayer })
		{
			ClippingProps.SetInfluenceFullModelType(ModelType, HitModelType == ModelType);
		}
		const std::string EncodedCutoutInfo = EncodeProperties(ClippingProps);
		auto inst = AVizInstance->GetAutoLock();
		if (EncodedCutoutInfo != inst->GetName())
		{
			inst->SetName(EncodedCutoutInfo);
			inst->SetShouldSave(true);
		}
	}
}

void AITwinClippingTool::FImpl::UpdateAVizInstanceProperties(EITwinClippingPrimitiveType Type, int32 InstanceIndex) const
{
	auto const& Population = GetClippingEffectPopulation(Type);
	if (Population.IsValid()
		&& ensure(InstanceIndex >= 0 && InstanceIndex < NumEffects(Type)))
	{
		auto AVizInstance = Population->GetAVizInstance(InstanceIndex);

		const FITwinClippingInfoBase& Prop = GetClippingEffect(Type, InstanceIndex);

		if (ensure(AVizInstance))
		{
			// For now we encode our properties in the instance name (Decoration Service)
			// In the future, we'll probably integrate cutout in SceneAPI instead...
			const std::string EncodedInfo = ITwin::Clipping::EncodeProperties(Prop);
			auto inst = AVizInstance->GetAutoLock();
			if (EncodedInfo != inst->GetName())
			{
				inst->SetName(EncodedInfo);
				inst->SetShouldSave(true);
			}
		}
	}
}

void AITwinClippingTool::FImpl::UpdateClippingPropertiesFromAVizInstance(EITwinClippingPrimitiveType Type, int32 InstanceIndex)
{
	// Decode properties from instance name - see #UpdateAVizInstanceProperties
	auto const& Population = GetClippingEffectPopulation(Type);
	if (Population.IsValid()
		&& ensure(InstanceIndex >= 0 && InstanceIndex < NumEffects(Type)))
	{
		auto AVizInstance = Population->GetAVizInstance(InstanceIndex);
		// Note that if the instance was just created, its name may not encode any information yet (see
		// condition in #ConfigureNewInstance), and in this case we will keep the default ones (ie. apply the
		// effect to all layers).
		if (ensure(AVizInstance))
		{				
			auto inst = AVizInstance->GetAutoLock();
			ITwin::Clipping::DecodeProperties(
				inst->GetName(),
				GetMutableClippingEffect(Type, InstanceIndex));
		}
	}
}

void AITwinClippingTool::AbortInteractiveCreation(bool bTriggeredFromITS)
{
	// Abort current effect creation, if any.
	AITwinInteractiveTool* ActiveTool = AITwinInteractiveTool::GetActiveTool(GetWorld());
	if (ActiveTool && ActiveTool->IsUsedOnCutoutPrimitive())
	{
		if (ActiveTool->IsInteractiveCreationMode())
			ActiveTool->AbortInteractiveCreation(bTriggeredFromITS);
		ActiveTool->SetEnabled(false);
		ActiveTool->SetUsedOnCutoutPrimitive(false);
	}
}

void AITwinClippingTool::Deactivate()
{
	// Abort current effect creation, if any.
	AbortInteractiveCreation(/*bTriggeredFromITS*/true);

	// Deselect all, without changing the visibility (since we will hide all below...)
	DeSelectAll(/*bExitIsolationMode*/false);

	// Trigger event to refresh the selection gizmo, typically.
	ActivationEvent.Broadcast(false);

	Impl->HideAllEffectProxies();
}

double AITwinClippingTool::FImpl::GetClippingValue_Boxes(FVector const& AbsoluteWorldPosition, ITwin::ModelLink const& ModelIdentifier) const
{
	// Equivalent of shader GetBoxClipping.ush
	double clippingValue = 1.0;

	uint32 NumActiveBoxes = 0;
	uint32 NumActiveAdditiveBoxes = 0;
	double AdditiveBoxValue = 0.0;
	double SubtractiveBoxValue = 0.0;

	// test inclusion in a box without branching (from https://stackoverflow.com/questions/12751080/glsl-point-inside-box-test)
	static const glm::double3 bottomLeft = glm::double3(-0.5, -0.5, -0.5);
	static const glm::double3 topRight = glm::double3(0.5, 0.5, 0.5);

	const glm::double3 WorldPosition(
		AbsoluteWorldPosition.X,
		AbsoluteWorldPosition.Y,
		AbsoluteWorldPosition.Z);
	for (FITwinClippingBoxInfo const& BoxInfo : ClippingBoxInfos)
	{
		if (BoxInfo.ShouldInfluenceModel(ModelIdentifier))
		{
			glm::double3 pos_BoxCoords = BoxInfo.BoxProperties->BoxInvMatrix * (WorldPosition - BoxInfo.BoxProperties->BoxTranslation);
			glm::double3 s = glm::step(bottomLeft, pos_BoxCoords) - glm::step(topRight, pos_BoxCoords);
			double isInsideValueForBox = s.x * s.y * s.z;
			if (BoxInfo.BoxProperties->bInvertEffect)
			{
				SubtractiveBoxValue += isInsideValueForBox;
			}
			else
			{
				NumActiveAdditiveBoxes++;
				AdditiveBoxValue += isInsideValueForBox;
			}
			NumActiveBoxes++;
		}
	}

	if (NumActiveBoxes > 0)
	{
		if (SubtractiveBoxValue > 0.0)
			clippingValue = 0.0;
		else if (NumActiveAdditiveBoxes > 0)
			clippingValue = AdditiveBoxValue;
	}

	return glm::step(1.0, clippingValue);
}

double AITwinClippingTool::FImpl::GetClippingValue_Planes(FVector const& WorldPosition, ITwin::ModelLink const& ModelIdentifier) const
{
	// Equivalent of shader GetPlanesClipping.ush
	for (FITwinClippingPlaneInfo const& PlaneInfo : ClippingPlaneInfos)
	{
		if (PlaneInfo.ShouldInfluenceModel(ModelIdentifier))
		{
			auto const& PlaneEquation(PlaneInfo.PlaneEquation);
			double Value = glm::step(PlaneEquation.PlaneOrientation.Dot(WorldPosition) - PlaneEquation.PlaneW, 0.);
			if (PlaneInfo.bInvertEffect)
				Value = 1.0 - Value;
			if (Value < 0.5)
				return 0.;
		}
	}
	return 1.;
}

bool AITwinClippingTool::ShouldCutOut(FVector const& AbsoluteWorldPosition, ITwin::ModelLink const& ModelIdentifier,
	UCesiumPolygonRasterOverlay const* RasterOverlay) const
{
	double ClippingValue = 1.0;
	ClippingValue = Impl->GetClippingValue_Boxes(AbsoluteWorldPosition, ModelIdentifier);
	if (ClippingValue < 0.5)
		return true;
	ClippingValue = Impl->GetClippingValue_Planes(AbsoluteWorldPosition, ModelIdentifier);
	if (ClippingValue < 0.5)
		return true;
	// Cutout polygons are tested through the Cesium raster overlay.
	if (RasterOverlay && RasterOverlay->ShouldExcludePoint(AbsoluteWorldPosition))
		return true;
	return false;
}

#if ENABLE_DRAW_DEBUG

// Console command to flip all clipping effects
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinFlipClippingEffects(
	TEXT("cmd.ITwinFlipClippingEffects"),
	TEXT("Flip all clipping effects."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	std::optional<EITwinClippingPrimitiveType> SingleType = std::nullopt;
	if (Args.Num() >= 1)
	{
		SingleType = ITwin::GetEnumFromCmdArg<EITwinClippingPrimitiveType>(Args, 0);
	}
	auto ClippingActor = TWorldSingleton<AITwinClippingTool>().Get(World);
	if (ensure(ClippingActor))
	{
		auto const FlipEffectsOfType = [&ClippingActor](EITwinClippingPrimitiveType Type)
		{
			const int32 NumEffects = ClippingActor->NumEffects(Type);
			for (int32 i(0); i < NumEffects; ++i)
			{
				ClippingActor->FlipEffect(Type, i);
			}
		};
		if (SingleType)
		{
			FlipEffectsOfType(*SingleType);
		}
		else
		{
			for (EITwinClippingPrimitiveType Type : TEnumRange<EITwinClippingPrimitiveType>())
			{
				FlipEffectsOfType(Type);
			}
		}
	}
}));


// Console command to activate/deactivate all clipping effects
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinActivateClippingEffects(
	TEXT("cmd.ITwinActivateClippingEffects"),
	TEXT("Activate/deactivate all clipping effects."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
#if WITH_EDITOR
	if (Args.Num() != 3)
	{
		UE_LOG(LogITwin, Error, TEXT("Need exactly 3 args: <box|plane> <shader|tileset> <0|1>"));
		return;
	}
	auto EffectType = ITwin::GetEnumFromCmdArg<EITwinClippingPrimitiveType>(Args, 0);
	auto EffectLevel = ITwin::GetEnumFromCmdArg<EITwinClippingEffectLevel>(Args, 1);
	auto const ActivateOpt = ITwin::ToggleFromCmdArg(Args, 2);

	if (EffectLevel && ActivateOpt)
	{
		auto ClippingActor = TWorldSingleton<AITwinClippingTool>().Get(World);
		if (ensure(ClippingActor))
		{
			ClippingActor->ActivateEffects(*EffectType, *EffectLevel, *ActivateOpt);
		}
	}
#else
	UE_LOG(LogITwin, Error, TEXT("ActivateEffects is not available in game"));
#endif
}));


// Console command to activate/deactivate clipping effects to a category of models.
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinActivatePerModelClippingEffects(
	TEXT("cmd.ITwinActivatePerModelClippingEffects"),
	TEXT("Activate/deactivate all clipping effects to a given model or model category."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	if (Args.Num() < 3)
	{
		UE_LOG(LogITwin, Error, TEXT("Expects 3 to 5 args: <box|plane> <IModel|RealityData|GlobalMapLayer> <0|1> [<PrimitiveId> <SingleModelId>"));
		return;
	}
	auto EffectType = ITwin::GetEnumFromCmdArg<EITwinClippingPrimitiveType>(Args, 0);
	auto ModelType = ITwin::GetEnumFromCmdArg<EITwinModelType>(Args, 1);
	auto const ActivateOpt = ITwin::ToggleFromCmdArg(Args, 2);
	int32 EffectIndex = -1;
	FString SingleModelId;
	if (Args.Num() > 3)
	{
		EffectIndex = FCString::Strtoi(*Args[3], nullptr, /*base*/10);
	}
	if (Args.Num() > 4)
	{
		SingleModelId = Args[4];
		SingleModelId.TrimStartAndEndInline();
	}
	if (EffectType && ModelType && ActivateOpt)
	{
		auto ClippingActor = TWorldSingleton<AITwinClippingTool>().Get(World);
		if (ensure(ClippingActor))
		{
			auto ChangeClippingInfluenceForEffect = [&](EITwinClippingPrimitiveType Type, int32 Index,
														EITwinModelType InModelType)
			{
				if (SingleModelId.IsEmpty())
				{
					ClippingActor->SetEffectInfluenceFullModelType(Type, Index, InModelType, *ActivateOpt);
				}
				else
				{
					ClippingActor->SetEffectInfluenceFullModelType(Type, Index, InModelType, false);
					ClippingActor->SetEffectInfluenceSpecificModel(Type, Index, std::make_pair(InModelType, SingleModelId), *ActivateOpt);
				}
			};

			if (EffectIndex == -1)
			{
				// Apply to all clipping effects
				const int32 NumEffects = ClippingActor->NumEffects(*EffectType);
				for (int32 i(0); i < NumEffects; ++i)
				{
					ChangeClippingInfluenceForEffect(*EffectType, i, *ModelType);
				}
			}
			else
			{
				ChangeClippingInfluenceForEffect(*EffectType, EffectIndex, *ModelType);
			}
		}
	}
}));

#endif // ENABLE_DRAW_DEBUG
