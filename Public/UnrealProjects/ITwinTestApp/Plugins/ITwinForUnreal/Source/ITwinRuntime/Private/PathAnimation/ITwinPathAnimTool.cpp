/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPathAnimTool.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <PathAnimation/ITwinPathAnimTool.h>
#include <PathAnimation/BakedAnimKeyFrames.h>
#include <PathAnimation/ITwinAVConnector.h>
#include <PathAnimation/PathAnimPlaybackHelper.h>
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
#include <ITwinFeatureChange.h>
#include <Decoration/ITwinDecorationHelper.h>
#include <Math/UEMathConversion.h>
#include "Math/BoxSphereBounds.h"
#include "Math/Box.h"
#include <Population/ITwinPopulation.h>
#include <Population/ITwinPopulationTool.h>
#include <Spline/ITwinSplineGeometry.h>
#include <Spline/ITwinSplineHelper.h>
#include <Spline/ITwinSplineTool.h>
#include <Spline/ITwinSplineEnums.h>
#include <Components/SplineComponent.h>

// UE headers
#include <Blueprint/WidgetLayoutLibrary.h>
#include <DrawDebugHelpers.h>
#include <EngineUtils.h> // for TActorIterator<>
#include <Engine/StaticMeshActor.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Compil/EnumSwitchCoverage.h>
#	include <BeUtils/SplineSampling/SplineSampling.h>
#	include <SDK/Core/Visualization/Instance.h>
#	include <SDK/Core/Visualization/InstancesGroup.h>
#	include <SDK/Core/Visualization/InstancesManager.h>
#   include <SDK/Core/Visualization/KeyframeAnimator.h>
#   include <SDK/Core/Visualization/PathAnimation.h>
#	include <SDK/Core/Visualization/Spline.h>
#	include <SDK/Core/Tools/Log.h>
#	include <SDK/Core/Tools/TypeId.h>
#	include <SDK/Core/Tools/Extension.h>
#	include "SDK/Core/Tools/DelayedCall.h"
#include <Compil/AfterNonUnrealIncludes.h>


namespace
{
	bool IsSplineUsedForPathAnim(AITwinSplineHelper const* SplineHelper)
	{
		return SplineHelper && ITwinSpline::IsPathAnim(SplineHelper->GetUsage());
	}

	EITwinSplineUsage GetSplineUsageFromAnimPathType(EITwinAnimPathType PathType)
	{
		switch (PathType)
		{
		case EITwinAnimPathType::Object:
			return EITwinSplineUsage::AnimPathObject;
		case EITwinAnimPathType::Traffic:
			return EITwinSplineUsage::AnimPathTraffic;
		case EITwinAnimPathType::Crowd:
			return EITwinSplineUsage::AnimPathCrowd;
		default:
			ensureMsgf(false, TEXT("Unknown anim path type"));
			return EITwinSplineUsage::Undefined;
		}
	}

	EITwinAnimPathType GetAnimPathTypeFromSplineUsage(EITwinSplineUsage SplineUsage)
	{
		switch (SplineUsage)
		{
		case EITwinSplineUsage::AnimPathObject:
			return EITwinAnimPathType::Object;
		case EITwinSplineUsage::AnimPathTraffic:
			return EITwinAnimPathType::Traffic;
		case EITwinSplineUsage::AnimPathCrowd:
			return EITwinAnimPathType::Crowd;
		default:
			ensureMsgf(false, TEXT("Unknown spline usage"));
			return EITwinAnimPathType::Count;
		}
	}

	class AssetSelector
	{
	public:
		AssetSelector(uintptr_t Seed, const AdvViz::SDK::RefID &InGroupID)
			: GroupID(InGroupID)
		{
			RandomStream = FRandomStream(Seed);
		}

		void AddAsset(FString InAsset, FBox InBBox)
		{
			if (InBBox.GetSize().Y <= 0.f || InBBox.GetSize().Y > 5000.f)
			{
				BE_LOGI("PathAnim", "Asset " << TCHAR_TO_UTF8(*InAsset) << " has invalid bounding box size. Please check that the asset's bounding box is correctly set up.");
				return;
			}

			if (AssetToSizeMap.Contains(InAsset))
				return;

			BE_LOGI("PathAnim", "Adding asset " << TCHAR_TO_UTF8(*InAsset) << ": x=" << InBBox.GetSize().X << ", y=" << InBBox.GetSize().Y << ", z=" << InBBox.GetSize().Z);

			AssetToSizeMap.Add(InAsset, InBBox);
			if (InBBox.GetSize().Y < 800.f)
				StandardAssetsOnly.Add(InAsset);
			else
				LongAssetsOnly.Add(InAsset);
		}

		void AddAVAsset(FString InAsset)
		{
			if (AssetToSizeMap.Contains(InAsset))
				return;

			BE_LOGI("PathAnim", "Adding articulated vehicle asset " << TCHAR_TO_UTF8(*InAsset));

			AssetToSizeMap.Add(InAsset, FBox());
			LongAssetsOnly.Add(InAsset);
		}

		bool HasBBox(FString InAsset) const
		{
			return AssetToSizeMap.Contains(InAsset) && AssetToSizeMap[InAsset].IsValid;
		}

		void SetBBox(FString InAsset, FBox InBBox)
		{
			if (!AssetToSizeMap.Contains(InAsset))
				return;

			BE_LOGI("PathAnim", "Adding bbox of asset " << TCHAR_TO_UTF8(*InAsset) << ": x=" << InBBox.GetSize().X << ", y=" << InBBox.GetSize().Y << ", z=" << InBBox.GetSize().Z);

			AssetToSizeMap[InAsset] = InBBox;
		}

		FString GetRandomAsset(bool bIncludeLongAssets) const
		{
			if (AssetToSizeMap.Num() == 0)
				return FString();
			if (bIncludeLongAssets && LongAssetsOnly.Num() > 0 && FMath::FRand() <= LongAssetFactor || StandardAssetsOnly.Num() == 0)
				return LongAssetsOnly.Array()[RandomStream.RandRange(0, LongAssetsOnly.Num() - 1)];
			else
				return StandardAssetsOnly.Array()[RandomStream.RandRange(0, StandardAssetsOnly.Num() - 1)];
		}

		float GetAssetLength(FString InAsset) const
		{
			if (AssetToSizeMap.Contains(InAsset))
			{
				return AssetToSizeMap[InAsset].GetSize().Y;
			}
			return 0.f;
		}

		FVector2D GetAssetRange(FString InAsset) const
		{
			if (AssetToSizeMap.Contains(InAsset))
			{
				return FVector2D(AssetToSizeMap[InAsset].Min.Y, AssetToSizeMap[InAsset].Max.Y);
			}
			return FVector2D::ZeroVector;
		}

		float GetAssetDistNoGap(FString InAssetBack, FString InAssetFront) const
		{
			if (AssetToSizeMap.Contains(InAssetBack) && AssetToSizeMap.Contains(InAssetFront))
			{
				return FMath::Abs(AssetToSizeMap[InAssetBack].Max.Y) + FMath::Abs(AssetToSizeMap[InAssetFront].Min.Y);
			}
			return 0.f;
		}

		AdvViz::SDK::RefID GetGroupID() const { return GroupID; }

	private:
		AdvViz::SDK::RefID GroupID;
		FRandomStream RandomStream;
		// Map from asset name to its bounding box (in cm). Note that assets are considered
		// to be aligned along the Y axis (forward direction), as it is currently the case
		// with all supplied vehicule components except the articulated vehicles. Therefore,
		// the Y dimension of the bounding box is used to determine the length of the asset.
		TMap<FString, FBox> AssetToSizeMap;
		TSet<FString> StandardAssetsOnly;
		TSet<FString> LongAssetsOnly;
		// Default percentage of long assets (like trucks, buses) to be used on
		// the lanes where such assets are allowed.
		float LongAssetFactor = 0.3f; // TODO: add this parameter to UI?
	};
}


typedef AdvViz::SDK::IInstancePtr SharedInstance;

class InstanceWithAnimPathExt : public PathAnimPlaybackHelper, public AdvViz::SDK::Tools::Extension, public AdvViz::SDK::Tools::TypeId<InstanceWithAnimPathExt>, public std::enable_shared_from_this<InstanceWithAnimPathExt>
{
public:
	InstanceWithAnimPathExt() {};

	using AdvViz::SDK::Tools::TypeId<InstanceWithAnimPathExt>::GetTypeId;
};



class AITwinPathAnimTool::FImpl
{
public:
	AITwinPathAnimTool& Owner;

	TArray<TStrongObjectPtr<UITwinObjectAnimPathHelper> > ObjectAnimPaths;
	TArray<TStrongObjectPtr<UITwinTrafficAnimPathHelper> > TrafficAnimPaths;
	TArray<TStrongObjectPtr<UITwinCrowdAnimPathHelper> > CrowdAnimPaths;

	std::shared_ptr<AdvViz::SDK::IPathAnimManager> PathAnimManager;

	TWeakObjectPtr<AITwinPopulationTool> PopulationTool;
	TWeakObjectPtr<AITwinSplineTool> SplineTool;

	TWeakObjectPtr<AITwinDecorationHelper> DecorationHelper;

	TWeakObjectPtr<AITwinAVConnector> AVConnector;

	// global setting for left-hand drive (traffic only).
	// Once set it will be applied to all newly created paths.
	std::optional<bool> bLeftHandDrive;

	FImpl(AITwinPathAnimTool& InOwner) : Owner(InOwner)
	{}

	inline int32 NumPaths(EITwinAnimPathType PathType) const;

	inline UITwinAnimPathHelper* GetMutableAnimPathHelper(FAnimPathIdentifier PathHandle);
	inline const UITwinAnimPathHelper* GetAnimPathHelper(FAnimPathIdentifier PathHandle) const;

	void LoadAnimationPaths();

	bool RegisterAnimPathSpline(AITwinSplineHelper* SplineHelper);
	bool UnregisterAnimPathSpline(AITwinSplineHelper* SplineBeingRemoved, bool bTriggeredFromITS);

	// For communication with iTwin Studio
	AdvViz::SDK::RefID GetPathRefId(FAnimPathIdentifier PathHandle) const;
	FAnimPathIdentifier GetPathIdentifier(AdvViz::SDK::RefID const& RefID) const;

	std::optional<FAnimPathIdentifier> GetSelectedPath() const;
	bool SelectPath(FAnimPathIdentifier PathHandle, bool bEnterIsolationMode = true);

	bool RemovePath(FAnimPathIdentifier PathHandle, bool bTriggeredFromITS);
	void ZoomOnPath(FAnimPathIdentifier PathHandle);

	void ResetAnimation(FAnimPathIdentifier PathHandle);
	void PlayAnimation(FAnimPathIdentifier PathHandle, bool bPlay);

	void PopulatePathObjects(UITwinAnimPathHelper* PathHelper, bool bOnSceneLoad, bool bClearPrevious = false);

	void RemovePathObjects(UITwinAnimPathHelper* PathHelper);
	void RemovePathObjects(FAnimPathIdentifier PathHandle);

	void HidePathAndObjects(UITwinAnimPathHelper* PathHelper, bool bHide);

	// Make the Spline Tool the active tool, with usage restricted to the creation of the given type of animation path
	TWeakObjectPtr<AITwinSplineTool> ActivateSplineTool(UWorld* World, EITwinAnimPathType PathType);

	// Select a spline, or reset selection if InSplineHelper is null
	void SelectSpline(AITwinSplineHelper* InSplineHelper, UWorld* World);

	void OnActivatePicking(bool bActivate);
	bool DoMouseClickPicking(bool& bOutSelectionGizmoNeeded);

	bool NeedRepopulateSplineAfterEdition(UITwinAnimPathHelper* PathHelper);
	void OnSplineEditedInTool();

	// Track a modification event for a path animation spline, when there is no specific
	// UITwinAnimPathHelper instance to report (eg. spline point added/removed).
	void PathAnimChanged(EITwinAnimPathType PathType, EChangeType change,
		const FString& eventSource = FString(), const FString& modificationKind = FString());

	void OnSplineChanged(const FString& eventSource, const FString& modificationKind);

	// Change all path splines visibility in the viewport (without deactivating them)
	void SetAllAnimPathProxiesVisibility(bool bVisibleInGame);
	void HideAllAnimPathProxies() { SetAllAnimPathProxiesVisibility(false); }

	// Show/Hide path splines for the given path type
	void SetAnimPathProxyVisibility(EITwinAnimPathType PathType, bool bVisibleInGame, bool bIsolationMode = false);
	void ShowOnlyAnimPathProxiesOfType(EITwinAnimPathType SelectedType, bool bIsolationMode);

	// Returns whether the splines of the given path type are visible
	bool IsAnimPathProxyVisible(EITwinAnimPathType PathType) const;

	void BakeAnimation(FAnimPathIdentifier PathHandle);
	void BakeAnimation();

	void UpdateAnimatedObjects(FAnimPathIdentifier PathHandle, float DeltaTime, bool bTimelineMode);
	void UpdateAllAnimatedObjects(float DeltaTime);

	// Verifies whether the populations for the path assets are fully loaded.
	// If called when loading a scene, it also checks whether the path asset instances have finished loading.
	bool ArePopulationsFullyLoaded(UITwinAnimPathHelper* PathHelper, bool bOnSceneLoad);

	void ToggleSplineToolForSelectedPath();

	bool TryLoadArticulatedVehicle(UITwinAnimPathHelper* PathHelper, const FString& Asset, FBox& BBox, int32 Lane);

	bool HasArticulatedVehicleSupport() const;

private:
	FAnimPathIdentifier GetPathIdentifierFromSpline(AdvViz::SDK::RefID const& RefID) const;
	UITwinAnimPathHelper* CreatePath(EITwinAnimPathType PathType);
	void DoPopulatePathObjects(UITwinAnimPathHelper* PathHelper);
	bool CreateSingleAnimatedObject(UITwinAnimPathHelper* PathHelper, const FString& Asset);
	bool CreateAnimatedObjectGroup(UITwinAnimPathHelper* PathHelper, const TArray<FString>& Assets);
	bool CreateAnimatedObjectGroup(UITwinAnimPathHelper* PathHelper, int32 Lane, AssetSelector& Selector, TMap<TWeakObjectPtr<AITwinPopulation>, int32>& PopulationInstancesMap);
};

/*
TWeakObjectPtr<AITwinPopulationTool> AITwinPathAnimTool::FImpl::ActivatePopulationTool(UWorld* World,
	bool bUpdateTransformationMode)
{
	if (ensure(PopulationTool.IsValid()))
	{
		if (!PopulationTool->IsEnabled())
		{
			AITwinInteractiveTool::DisableAll(World);
			PopulationTool->SetEnabled(true);
		}
		PopulationTool->ResetToDefault();
		if (bUpdateTransformationMode && TransformationModeOpt)
		{
			PopulationTool->SetTransformationMode(*TransformationModeOpt);
		}
	}
	return PopulationTool;
}

AITwinPopulation const* AITwinPathAnimTool::FImpl::GetSelectedPopulation(int32& OutSelectedInstanceIndex) const
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

void AITwinPathAnimTool::FImpl::SelectPopulationInstance(AITwinPopulation* Population,
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

void AITwinPathAnimTool::FImpl::DeleteSelectedPopulationInstance()
{
	if (ensure(PopulationTool.IsValid()))
	{
		PopulationTool->DeleteSelectedInstance();
	}
}
*/

TWeakObjectPtr<AITwinSplineTool> AITwinPathAnimTool::FImpl::ActivateSplineTool(UWorld* World, EITwinAnimPathType PathType)
{
	ensure(SplineTool.IsValid());
	return ITwin::ActivateSplineTool(World, GetSplineUsageFromAnimPathType(PathType), SplineTool);
}

void AITwinPathAnimTool::FImpl::SelectSpline(AITwinSplineHelper* SplineHelper, UWorld* World)
{
	ensure(SplineTool.IsValid());
	ITwin::SelectSpline(SplineHelper, INDEX_NONE, World, SplineTool);
}


AITwinPathAnimTool::AITwinPathAnimTool()
	: Impl(MakePimpl<FImpl>(*this))
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;//false; TODO
	PrimaryActorTick.bTickEvenWhenPaused = true;
	//PrimaryActorTick.TickGroup = TG_PostUpdateWork;
}

void AITwinPathAnimTool::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// Release all TStrongObjectPtr references before shutdown, otherwise 
	// these strong references may keep World-owned UObjects alive and prevent
	// World from being garbage collected (we do not use TObjectPtr for AnimPathHelpers 
	// because AITwinPathAnimTool::Impl is not an UObject, so Unreal CG will not work for them).
	Impl->ObjectAnimPaths.Empty();
	Impl->TrafficAnimPaths.Empty();
	Impl->CrowdAnimPaths.Empty();
}

void AITwinPathAnimTool::ConnectPopulationTool(AITwinPopulationTool* PopulationTool)
{
	Impl->PopulationTool = PopulationTool;
}

void AITwinPathAnimTool::ConnectSplineTool(AITwinSplineTool* SplineTool)
{
	Impl->SplineTool = SplineTool;
	if (SplineTool)
	{
		SplineTool->SplineAddedEvent.AddUniqueDynamic(this, &AITwinPathAnimTool::OnSplineHelperAdded);
		SplineTool->SplineBeforeRemovedEvent.AddUniqueDynamic(this, &AITwinPathAnimTool::OnSplineHelperRemoved);
		SplineTool->InteractiveCreationAbortedEvent.AddUniqueDynamic(this, &AITwinPathAnimTool::OnItemCreationAbortedInTool);
		SplineTool->SplineEditionEvent.AddUniqueDynamic(this, &AITwinPathAnimTool::OnSplineEditedInTool);
		SplineTool->SplinePointMovedEvent.AddUniqueDynamic(this, &AITwinPathAnimTool::OnSplinePointMovedInTool);
		SplineTool->SplinePointAddedEvent.AddUniqueDynamic(this, &AITwinPathAnimTool::OnSplinePointAddedInTool);
		SplineTool->SplinePointRemovedEvent.AddUniqueDynamic(this, &AITwinPathAnimTool::OnSplinePointRemovedInTool);
		SplineTool->SplineSelectedEvent.AddUniqueDynamic(this, &AITwinPathAnimTool::OnPathAnimPolygonSelected);
		//SplineTool->SplinePointSelectedEvent.AddUniqueDynamic(this, &AITwinPathAnimTool::OnSplinePointSelected);
	}
}

void AITwinPathAnimTool::SetArticulatedVehiclesConnector(AITwinAVConnector* AVConnector)
{
	Impl->AVConnector = AVConnector;
}

void AITwinPathAnimTool::SetDecorationHelper(AITwinDecorationHelper* InDecoHelper)
{
	Impl->DecorationHelper = InDecoHelper;
}

void AITwinPathAnimTool::SetPathAnimManager(const std::shared_ptr<AdvViz::SDK::IPathAnimManager>& InPathAnimManager)
{
	Impl->PathAnimManager = InPathAnimManager;
}

void AITwinPathAnimTool::FImpl::LoadAnimationPaths()
{
	if (!ensure(PathAnimManager))
		return;

	std::set<AdvViz::SDK::RefID> AnimPathIds;
	PathAnimManager->GetAnimationPathIds(AnimPathIds);
	std::unordered_map<AdvViz::SDK::RefID, AITwinSplineHelper*> SplineRefIdToSplineMap;
	for (TActorIterator<AITwinSplineHelper> SplineIter(Owner.GetWorld()); SplineIter; ++SplineIter)
	{
		if (IsSplineUsedForPathAnim(*SplineIter))
			SplineRefIdToSplineMap[SplineIter->GetAVizSplineId()] = *SplineIter;
	}
	bool allLeftHand = AnimPathIds.size() > 0;
	for (auto id : AnimPathIds)
	{
		if (auto PathPropPtr = PathAnimManager->GetAnimationPathInfo(id))
		{
			auto PathProp = PathPropPtr->GetRAutoLock();
			auto SplineRefID = PathProp->GetSplineId();
			if (!ensure(SplineRefID.IsValid() && SplineRefIdToSplineMap.contains(SplineRefID)))
			{
				BE_LOGI("PathAnim", "Couldn't load path anim with id=" << id.ID() <<" - corresponding spline not found!");
				continue;
			}
			auto SplineHelper = SplineRefIdToSplineMap[SplineRefID];
			SplineRefIdToSplineMap.erase(SplineRefID);
			EITwinAnimPathType PathType = GetAnimPathTypeFromSplineUsage(SplineHelper->GetUsage());
			if (auto PathHelper = CreatePath(PathType))
			{
				PathHelper->Init(SplineHelper, PathPropPtr);
				std::vector<std::string> assets;
				PathProp->GetObjects(assets);
				if (assets.size() > 0)
				{
					PathHelper->Set3DObjectsFromProps();
					PopulatePathObjects(PathHelper, true/*bOnSceneLoad*/);
				}
				if (PathType == EITwinAnimPathType::Traffic)
					allLeftHand = allLeftHand && PathHelper->HasInvDirection();
			}
		}
	}
	// if all loaded traffic paths are left-hand drive, set the global setting to left-hand drive
	// for all newly created traffic paths
	if (allLeftHand)
		bLeftHandDrive = true;

	if (!SplineRefIdToSplineMap.empty())
	{
		BE_LOGI("PathAnim", "Some splines were found without corresponding path anims. They will be ignored.");
		for (auto [_, SplinePtr] : SplineRefIdToSplineMap)
		{
			BE_LOGI("PathAnim", "Removing dangling spline " << SplinePtr->GetAVizSplineId().ID());
			SplineTool->DeleteSplineAtLoad(SplinePtr);
		}
	}
}

void AITwinPathAnimTool::LoadAnimationPaths()
{
	Impl->LoadAnimationPaths();
}

bool AITwinPathAnimTool::IsLoadingAnimationPaths() const
{
	if (Impl->SplineTool.IsValid() && Impl->SplineTool->IsLoadingSpline())
		return true;
	return false;
}

void AITwinPathAnimTool::FImpl::BakeAnimation(FAnimPathIdentifier PathHandle)
{
	if (auto PathHelper = GetMutableAnimPathHelper(PathHandle))
		PathHelper->BakeAnimationIfNeeded();
}

void AITwinPathAnimTool::FImpl::BakeAnimation()
{
	for (EITwinAnimPathType PathType : {
		EITwinAnimPathType::Object,
		EITwinAnimPathType::Traffic,
		EITwinAnimPathType::Crowd })
	{
		for (int32 Index(0); Index < NumPaths(PathType); ++Index)
		{
			BakeAnimation(FAnimPathIdentifier(PathType, Index));
		}
	}
}

void AITwinPathAnimTool::FImpl::RemovePathObjects(UITwinAnimPathHelper* PathHelper)
{
	if (!PathHelper)
		return;

	// Clear articulated vehicles for the path if any
	if (HasArticulatedVehicleSupport())
	{
		AVConnector->DeleteArticulatedVehicles(PathHelper);
	}

	// Clear populations for the path if any
	for (auto Population : PathHelper->Populations)
	{
		if (Population.IsValid())
			Population->RemoveAllInstances();
	}
	PathHelper->Populations.Empty();
}

void AITwinPathAnimTool::FImpl::RemovePathObjects(FAnimPathIdentifier PathHandle)
{
	RemovePathObjects(GetMutableAnimPathHelper(PathHandle));
}

void AITwinPathAnimTool::FImpl::HidePathAndObjects(UITwinAnimPathHelper* PathHelper, bool bHide)
{
	if (!PathHelper)
		return;
	for (auto Population : PathHelper->Populations)
	{
		if (!Population.IsValid())
			continue;
		Population->SetHiddenInGame(bHide);
	}
	if (PathHelper->SplineHelper.IsValid())
		PathHelper->SplineHelper->SetActorHiddenInGame(bHide);
}

bool AITwinPathAnimTool::FImpl::TryLoadArticulatedVehicle(UITwinAnimPathHelper* PathHelper, const FString& Asset, FBox& BBox, int32 Lane)
{
	auto TestVehiclePath(TEXT("/Game/CarrotLibrary/UserContent/artic-test-001/artic-test-001_Hierarchy.artic-test-001_Hierarchy"));
	auto VehiclePath = TestVehiclePath;//Asset.IsEmpty() ? TestVehiclePath : Asset;

	if (HasArticulatedVehicleSupport() && AVConnector->LoadArticulatedVehicleForPath(PathHelper, VehiclePath, Lane) > 0.f)
	{
		BBox = AVConnector->GetBBoxOfLoadedVehicle(VehiclePath);
		return true;
	}
	return false;
}

bool AITwinPathAnimTool::FImpl::ArePopulationsFullyLoaded(UITwinAnimPathHelper* PathHelper, bool bOnSceneLoad)
{
	if (!PathHelper || !PathHelper->SplineHelper.IsValid())
		return false;
	auto InstGroupId = DecorationHelper->GetInstancesGroupIdForSpline(*(PathHelper->SplineHelper));
	auto Assets = PathHelper->Get3DObjectPaths();
	for (auto asset : Assets)
	{
		AITwinPopulation* Population = DecorationHelper->GetPopulation(asset, InstGroupId);
		if (!Population || Population->GetNumberOfInstances() == 0)
			return false;
	}
	return true;
}

namespace {
	bool PopulationCanBeAnimated(AITwinPopulation* Population)
	{
		// TODO: move filter to UI and support other types of objects if needed
		return (Population->GetObjectType() == EITwinInstantiatedObjectType::Character
			|| Population->GetObjectType() == EITwinInstantiatedObjectType::Vehicle
			|| Population->GetObjectType() == EITwinInstantiatedObjectType::Crane);
	}

	bool IsArticulatedVehicle(const FString& Asset)
	{
		return Asset.Contains(TEXT("GTransport002")); // TODO: replace with proper way to detect articulated vehicles
	}
}

bool AITwinPathAnimTool::FImpl::HasArticulatedVehicleSupport() const
{
	return AVConnector.IsValid() && AVConnector->IsReady();
}

bool AITwinPathAnimTool::FImpl::CreateSingleAnimatedObject(UITwinAnimPathHelper* PathHelper, const FString& Asset)
{
	auto InstGroupId = DecorationHelper->GetInstancesGroupIdForSpline(*(PathHelper->SplineHelper));

	// Articulated vehicles are managed separately by the AVConnector, so we don't create a population for them.
	if (IsArticulatedVehicle(Asset))
	{
		RemovePathObjects(PathHelper);
		FBox BBox;
		return TryLoadArticulatedVehicle(PathHelper, Asset, BBox, 0);
	}

	// Create a population for the asset if it doesn't exist yet, and add an instance of it to the path.
	AITwinPopulation* Population = DecorationHelper->GetPopulation(Asset, InstGroupId);
	if (!ensure(Population))
	{
		BE_LOGI("PathAnim", "Error creating animation: the population for asset " << TCHAR_TO_UTF8(*Asset) << " failed to load.");
		return false; // all required populations should have been created by now
	}
	if (!PopulationCanBeAnimated(Population))
	{
		BE_LOGI("PathAnim", "Error creating animation: selected asset " << TCHAR_TO_UTF8(*Asset) << " is not an animatable object type.");
		return false;
	}

	// If path has already been populated with a different asset, remove corresponding instance
	if (PathHelper->Populations.Num() > 0 && PathHelper->Populations.Array()[0] != Population)
		RemovePathObjects(PathHelper); // note that it won't delete existing populations when loading the scene as PathHelper->Populations hasn't been initialized yet
	
	Population->SetPartOfPathAnimation(true);
	PathHelper->Populations.Add(Population);

	auto StartTransform = PathHelper->GetStartTransform(0, true);
	int32 instIdx(0);
	if (Population->GetNumberOfInstances() == 0)
		instIdx = Population->AddInstance(StartTransform);
	if (!ensure(instIdx != INDEX_NONE))
	{
		BE_LOGI("PathAnim", "Error creating animation: failed to add an instance of asset " << TCHAR_TO_UTF8(*Asset) << " to the population.");
		return false;
	}

	if (auto InstancePtr = Population->GetAVizInstance(instIdx))
	{
		std::shared_ptr<InstanceWithAnimPathExt> animPathExt = std::make_shared<InstanceWithAnimPathExt>();
		animPathExt->SetKeyFrames(PathHelper->GetBakedFrames());
		animPathExt->SetStartTransform(StartTransform);
		auto Instance = InstancePtr->GetAutoLock();
		Instance->SetAnimPathId(PathHelper->GetPathRefID());
		Instance->AddExtension(animPathExt);
		return true;
	}

	return false;
}

bool AITwinPathAnimTool::FImpl::CreateAnimatedObjectGroup(UITwinAnimPathHelper* PathHelper, const TArray<FString>& Assets)
{
	// Let's keep track of the existing population instances created for this path and try to reuse them when possible.
	TMap<TWeakObjectPtr<AITwinPopulation>, int32> ReusedPopulationInstancesMap; // map population to number of instances that are reused in current path configuration
	for (auto Population : PathHelper->Populations)
	{
		if (Population.IsValid())
			ReusedPopulationInstancesMap.Add(Population, 0);
	}
	// Clear the populations array (but do not remove the actual populations).
	PathHelper->Populations.Empty();

	// For articulated vehicles, we don't try to reuse existing instances for now.
	if (HasArticulatedVehicleSupport())
	{
		AVConnector->DeleteArticulatedVehicles(PathHelper);
	}

	auto InstGroupId = DecorationHelper->GetInstancesGroupIdForSpline(*(PathHelper->SplineHelper));
	
	// Initialize the asset selector.
	AssetSelector Selector(reinterpret_cast<uintptr_t>(PathHelper->SplineHelper.Get()), InstGroupId);
	for (auto Asset : Assets)
	{
		if (IsArticulatedVehicle(Asset))
		{
			Selector.AddAVAsset(Asset);
			continue;
		}

		AITwinPopulation* Population = DecorationHelper->GetPopulation(Asset, InstGroupId);
		if (!ensure(Population))
		{
			BE_LOGI("PathAnim", "Population for asset " << TCHAR_TO_UTF8(*Asset) << " failed to load. It won't be used for animation.");
			continue;
		}
		if (!PopulationCanBeAnimated(Population))
		{
			BE_LOGI("PathAnim", "Selected asset " << TCHAR_TO_UTF8(*Asset) << " is not an animatable object type. It won't be used for animation.");
			continue;
		}
		Population->SetPartOfPathAnimation(true);
		PathHelper->Populations.Add(Population);
		// Population instances related to the given assets can already exist in the scene even if PathHelper->Populations
		// was empty (in particular, when loading a scene), so we add them to the reused instances map as well.
		ReusedPopulationInstancesMap.Add(Population, 0);
		Selector.AddAsset(Asset, Population->GetMasterMeshBoundingBox());
	}

	// Populate the lanes.
	for (int32 Lane(0); Lane < PathHelper->GetFullLaneCount(); ++Lane)
	{
		CreateAnimatedObjectGroup(PathHelper, Lane, Selector, ReusedPopulationInstancesMap);
	}

	// Remove the excess instances that were potentially remaining from previous traffic and were not reused.
	for (auto& [Population, NextIdx] : ReusedPopulationInstancesMap)
	{
		while (Population->GetNumberOfInstances() > NextIdx)
		{
			Population->RemoveInstance(Population->GetNumberOfInstances() - 1);
		}
	}

	return true;
}

bool AITwinPathAnimTool::FImpl::CreateAnimatedObjectGroup(UITwinAnimPathHelper* PathHelper, int32 Lane, AssetSelector& Selector, TMap<TWeakObjectPtr<AITwinPopulation>, int32> &PopulationInstancesMap)
{
	float laneOffset = PathHelper->GetLaneOffset(Lane, false);
	float laneSpeed = PathHelper->GetLaneSpeed(Lane);
	float laneLength = PathHelper->GetLaneLength(Lane);
	float laneDensity = PathHelper->GetLaneDensity(Lane);
	auto startTransform = PathHelper->GetStartTransform(Lane, true);
	float minInterObjectDist = PathHelper->GetMinInterObjectDistance(Lane);

	// Index of the first object on the lane whose speed will be variating (to avoid all objects on the lane having the same speed).
	// For simplicity we do not variate speed of the first and the last object on the lane.
	int nextSpeedVarObjectIdx(1 + Lane % 2);

	BE_LOGI("PathAnim", "Populating lane " << Lane << ": length = " << laneLength << ", offset = " << laneOffset << ", speed = " << laneSpeed << ", density = " << laneDensity << ", min allowed distance between objects = " << minInterObjectDist << ", is slow = " << PathHelper->IsSlowLane(Lane));

	std::vector<std::variant<AdvViz::SDK::IInstancePtr, FString>> laneInstances;
	float targetObjectLength = laneLength * laneDensity;
	float totalObjectLength = 0.f;

	// Create instances to populate the lane by randomly picking up assets
	// and taking into account their dimensions to approximately respect the lane density
	int32 nbInstances(0);
	int32 nbAttempts(0);
	while (totalObjectLength < targetObjectLength)
	{
		if (laneInstances.size() > laneLength * 0.03f) // normally more than 3 instances per 1m indicate some issue
		{
			// Triggered once due to 'nan' Y dimension of an asset when rebuilding traffic, needs to be investigated
			BE_LOGI("PathAnim", "Number of instances (" << laneInstances.size() << ") is too high for lane " << Lane << " of path " << TCHAR_TO_UTF8(*PathHelper->GetPathName()) << ". Stopping population for this lane. Please check that the assets bounding boxes are correct.");
			break;
		}
		if (nbInstances == laneInstances.size()) // avoid infinite loop in case of repeated failure to pick up a suitable asset
		{
			if (++nbAttempts > 100)
			{
				BE_LOGI("PathAnim", "Too many attempts to populate lane " << Lane << " of path " << TCHAR_TO_UTF8(*PathHelper->GetPathName()) << ". Stopping population for this lane. Please check that the assets bounding boxes are correct.");
				break;
			}
		}
		else
		{
			nbAttempts = 0;
			++nbInstances;
		}

		FString asset = Selector.GetRandomAsset(PathHelper->IsSlowLane(Lane) || targetObjectLength - totalObjectLength < 2500.f);
		if (asset.IsEmpty())
			continue;

		float objectLength = minInterObjectDist;
		if (IsArticulatedVehicle(asset))
		{
			FBox BBox;
			if (TryLoadArticulatedVehicle(PathHelper, asset, BBox, Lane))
			{
				if (!Selector.HasBBox(asset))
				{
					FBox BBoxWithAlignmentFix(
						FVector(BBox.Min.Y, BBox.Min.X, BBox.Min.Z),
						FVector(BBox.Max.Y, BBox.Max.X, BBox.Max.Z)
					);
					Selector.SetBBox(asset, BBoxWithAlignmentFix);
				}
				laneInstances.push_back(asset);
				objectLength += BBox.GetSize().X;
				totalObjectLength += objectLength;
			}
			else
			{
				BE_LOGI("PathAnim", "Failed to load articulated vehicle asset " << TCHAR_TO_UTF8(*asset) << ". It won't be used for animation.");
			}
			continue;
		}
		else
		{
			objectLength += Selector.GetAssetLength(asset);
			// Check whether the asset we are trying to can fit in the lane taking into account
			// user-specified density setting (however, make to sure to add at least one object per lane).
			if (!laneInstances.empty() && totalObjectLength + objectLength > targetObjectLength)
				break;
		}

		AITwinPopulation* Population = DecorationHelper->GetPopulation(asset, Selector.GetGroupID());
		int32 instIdx = (PopulationInstancesMap.Contains(Population) && Population->GetNumberOfInstances() > PopulationInstancesMap[Population]) ?
				PopulationInstancesMap[Population] : Population->AddInstance(startTransform);
		PopulationInstancesMap[Population]++;

		if (!ensure(instIdx != INDEX_NONE))
			break;

		if (auto InstancePtr = Population->GetAVizInstance(instIdx))
		{
			std::shared_ptr<InstanceWithAnimPathExt> animPathExt = std::make_shared<InstanceWithAnimPathExt>();
			animPathExt->SetKeyFrames(PathHelper->GetBakedFrames(Lane));
			animPathExt->SetStartTransform(startTransform);
			auto Instance = InstancePtr->GetAutoLock();
			Instance->SetAnimPathId(PathHelper->GetPathRefID());
			if (Instance->HasExtension<InstanceWithAnimPathExt>())
				Instance->RemoveExtension<InstanceWithAnimPathExt>();
			Instance->AddExtension(animPathExt);
			laneInstances.push_back(InstancePtr);
		}
		totalObjectLength += objectLength;
	}

	BE_LOGI("PathAnim", "Populating lane " << Lane << ": total number of instances = " << laneInstances.size());
	if (laneInstances.size() == 0)
		return false;

	// Once instances are created, compute the actual inter-object distance to distribute them evenly on the lane.
	float interObjectDist = (laneLength - totalObjectLength) / laneInstances.size();
	// Since totalObjectLength incorporates min inter-object distance for each instance, we need to add it as well.
	interObjectDist += minInterObjectDist;

	BE_LOGI("PathAnim", "Populating lane " << Lane << ": distance between instances = " << interObjectDist);

	// Actually distribute objects on the lane by applying a time offset to each instance animation extension,
	// taking into account the object dimensions to avoid overlaps
	float initPosOffsetPrev = 0.f;
	FString assetPrev;
	int instanceIdx = 0;
	int AVehicleIdx = 0;
	for (auto Entry : laneInstances)
	{
		float initPosOffset = 0.f;
		std::shared_ptr<InstanceWithAnimPathExt> animPathExt;
		FString asset;
		if (auto AssetPath = std::get_if<FString>(&Entry))
		{
			asset = *AssetPath;
		}
		else if (auto InstancePtr = std::get_if<AdvViz::SDK::IInstancePtr>(&Entry))
		{
			auto Instance = (*InstancePtr)->GetAutoLock();
			asset = UTF8_TO_TCHAR(Instance->GetObjectRef().c_str());
			animPathExt = Instance->GetExtension<InstanceWithAnimPathExt>();
		}
		
		if (instanceIdx == 0)
		{
			// Slightly move the first object back (not more than half than inter-object distance on this lane)
			// to avoid first row of objects on all the lanes being aligned.
			// (The remaining objects will be distributed unevenly due to slight fluctuations of density between lanes.)
			initPosOffset = std::min(0.5f * interObjectDist, 30.f + FMath::FRand()*200.f);
			// We ignore the eventual shift of the first object for the following objects so
			// initPosOffsetPrev is kept at 0.
		}
		else
		{
			// Object position is set relative to the one in front, taking into account
			// inter-object distance for this and this and front object dimensions
			initPosOffset = initPosOffsetPrev + interObjectDist + Selector.GetAssetDistNoGap(asset, assetPrev);
			ensure(initPosOffset < laneLength); // usually indicates a problem with a bounding box
			//maxFrontSpeedVarDistances.push_back(initPosOffset - initPosOffsetPrev - Selector.GetAssetDistNoGap(asset, assetPrev) - minInterObjectDist);
			initPosOffsetPrev = initPosOffset;
		}
		//if (GetItemType() == BRW_LRTCharacters)
		//{
		//	//compute random variation
		//	float randomness = (float)std::fmod(randomGen.RandFloat() * 10.f, 1.f);
		//	randomOffset = (lane.interVehicleDist + vehicleInfoPrev.distToNextNoGap) * (0.4f * (float)glm::sin(randomness * 2 * PI) + 0.1f);
		//}

		BE_LOGI("PathAnim", "Populating lane " << Lane << ": initial position offset for object " << instanceIdx << " is " << initPosOffset);
		
		// Compute final time shift that will be used by the animation system to actually distribute objects on the lane
		float initTimeOffset = (initPosOffset/* + randomOffset*/) / laneSpeed;
		if (animPathExt)
		{
			animPathExt->RemoveSpeedVariation();
			PathAnimPlaybackSettings Settings(PathHelper, Lane, true);
			auto startTransformWithOffset = animPathExt->GetTransform(initTimeOffset, true/*bTimelineMode*/, Settings);
			animPathExt->ResetAnimation(PathHelper->GetDelay(), initTimeOffset, startTransformWithOffset);

			// Init speed variation intervals for this object if needed

			// To simplify management of the inter-object distance we always keep one object with average lane
			// speed (without speed variation) between two objects with speed variation.
			// Note that the first instance can be slightly shifted so the max speed variation distance for
			// the second object has to be smaller.
			if (instanceIdx == nextSpeedVarObjectIdx && instanceIdx < (int)laneInstances.size() - 1)
			{
				float maxDeltaDist = (instanceIdx != 1) ? (interObjectDist - minInterObjectDist) : std::max(0.f, 0.5f * interObjectDist - minInterObjectDist);
				if (maxDeltaDist > 50.f)
				{
					animPathExt->InitSpeedVariation(laneSpeed, maxDeltaDist);
					nextSpeedVarObjectIdx += 2;	
				}
				else
					nextSpeedVarObjectIdx++; // skip this object and try to variate speed of the next one
			}
		}
		else
		{
			// Reset animation of the articulated vehicle to the initial position on the lane
			AVConnector->SetTimeOffset(PathHelper, AVehicleIdx, initTimeOffset);
			AVehicleIdx++;
		}
		assetPrev = asset;
		instanceIdx++;
	}

	return true;
}

void AITwinPathAnimTool::FImpl::DoPopulatePathObjects(UITwinAnimPathHelper* PathHelper)
{
	if (!PopulationTool.IsValid() || !PathHelper || !PathHelper->SplineHelper.IsValid())
		return;

	auto Assets = PathHelper->Get3DObjectPaths();
	if (Assets.Num() == 0)
	{
		RemovePathObjects(PathHelper);
		return;
	}

	if (!PathHelper->CanHaveMultipleObjects())
	{
		// Case of animation paths having only one object
		ensure(Assets.Num() == 1 && PathHelper->Populations.Num() <= 1);
		CreateSingleAnimatedObject(PathHelper, Assets[0]);
	}
	else
	{
		// Case of animation paths that can have multiple objects (crowds, traffic)
		CreateAnimatedObjectGroup(PathHelper, Assets);
	}
}

void AITwinPathAnimTool::FImpl::PopulatePathObjects(UITwinAnimPathHelper* PathHelper, bool bOnSceneLoad, bool bClearPrevious/* = false*/)
{
	if (!PathHelper || !PathHelper->SplineHelper.IsValid())
		return;
	if (!ensure(PopulationTool.IsValid() && DecorationHelper.IsValid()))
		return;

	auto Assets = PathHelper->Get3DObjectPaths();
	if ((bClearPrevious || Assets.IsEmpty()) && PathHelper->Populations.Num() > 0)
	{
		RemovePathObjects(PathHelper);
	}

	auto InstGroupId = DecorationHelper->GetInstancesGroupIdForSpline(*(PathHelper->SplineHelper));
	if (Assets.Num() > 0)
	{
		PathHelper->BakeAnimationIfNeeded();

		bool bAllAssetsLoaded = true;
		for (auto asset : Assets)
		{
			// We do not want to trigger new population creation when loading an existing scene, just wait for all populations to load and then create the link
			AITwinPopulation* Population = bOnSceneLoad ? DecorationHelper->GetPopulation(asset, InstGroupId) : DecorationHelper->GetOrCreatePopulation(asset, InstGroupId);
			if (!Population)
				bAllAssetsLoaded = false;
		}

		if (bAllAssetsLoaded)
			return DoPopulatePathObjects(PathHelper);

		// Delay adding objects to path until all items have been completely loaded from the component center.
		TWeakObjectPtr<AITwinPathAnimTool> weakOwner(&Owner);
		std::string const delayedCallId = "RetryPopulatePath_" + std::to_string(reinterpret_cast<uintptr_t>(PathHelper));
		AdvViz::SDK::UniqueDelayedCall(delayedCallId,
			[weakOwner, PathHelper, bOnSceneLoad]() -> AdvViz::SDK::DelayedCall::EReturnedValue
			{
				if (!weakOwner.IsValid())
					return AdvViz::SDK::DelayedCall::EReturnedValue::Done;

				if (weakOwner->Impl->ArePopulationsFullyLoaded(PathHelper, bOnSceneLoad))
				{
					weakOwner->Impl->DoPopulatePathObjects(PathHelper);
					return AdvViz::SDK::DelayedCall::EReturnedValue::Done;
				}

				return AdvViz::SDK::DelayedCall::EReturnedValue::Repeat;
			},
			0.25f);
	}
}

//FString AITwinPathAnimTool::GetName(FAnimPathIdentifier PathHandle) const
//{
//	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
//		return PathHelper->GetName();
//	return FString();
//}
//
//void AITwinPathAnimTool::SetName(FAnimPathIdentifier PathHandle, const FString& Name)
//{
//	Impl->GetMutableAnimPathHelper(PathHandle)->SetName(Name);
//}

void AITwinPathAnimTool::Get3DObjects(FAnimPathIdentifier PathHandle, TArray<FString>& Assets) const
{
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		PathHelper->Get3DObjects(Assets);
	else
		Assets.Empty();
}

void AITwinPathAnimTool::Set3DObjects(FAnimPathIdentifier PathHandle, const TArray<FString>& Assets)
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object || Assets.Num() == 1);
	auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle);
	if (!PathHelper)
		return;
	PathHelper->Set3DObjects(Assets);
	Impl->PopulatePathObjects(PathHelper, false/*bOnSceneLoad*/);
}

bool AITwinPathAnimTool::IsVisible(FAnimPathIdentifier PathHandle) const
{
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->IsVisible();
	return false;
}

void AITwinPathAnimTool::SetVisible(FAnimPathIdentifier PathHandle, bool isVisible)
{
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
	{
		PathHelper->SetVisible(isVisible);
		Impl->HidePathAndObjects(PathHelper, !isVisible);
	}
}

void AITwinPathAnimTool::SetAllVisible(bool isVisible)
{
	for (EITwinAnimPathType PathType : {
		EITwinAnimPathType::Object,
		EITwinAnimPathType::Traffic,
		EITwinAnimPathType::Crowd })
	{
		for (int32 Index(0); Index < NumPaths(PathType); ++Index)
		{
			SetVisible(FAnimPathIdentifier(PathType, Index), isVisible);
		}
	}
}

void AITwinPathAnimTool::TriggerRebake(UITwinAnimPathHelper* PathHelper, bool bMultiObjectOnly)
{
	if (!bMultiObjectOnly || PathHelper->CanHaveMultipleObjects())
		PathHelper->InvalidateBakedAnimation();
}

void AITwinPathAnimTool::TriggerRepopulate(UITwinAnimPathHelper* PathHelper, bool bMultiObjectOnly)
{
	if (!bMultiObjectOnly || PathHelper->CanHaveMultipleObjects())
		Impl->PopulatePathObjects(PathHelper, false/*bOnSceneLoad*/);
}

bool AITwinPathAnimTool::HasInvDirection(FAnimPathIdentifier PathHandle) const
{
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->HasInvDirection();
	return false;
}

void AITwinPathAnimTool::SetInvDirectionAll(EITwinAnimPathType PathType, bool bInvDirection)
{
	for (int32 Index(0); Index < NumPaths(PathType); ++Index)
	{
		SetInvDirection(FAnimPathIdentifier(PathType, Index), bInvDirection);
	}
	Impl->bLeftHandDrive = bInvDirection;
}

void AITwinPathAnimTool::SetInvDirection(FAnimPathIdentifier PathHandle, bool bInvDirection)
{
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
	{
#ifdef DEBUG_AV_ANIMATION
		// Temporal hook to test articulated vehicles
		if (Impl->HasArticulatedVehicleSupport())
		{
			Impl->AVConnector->DeleteArticulatedVehicles(PathHelper);
			return;
		}
#endif

		PathHelper->SetInvDirection(bInvDirection);
		TriggerRebake(PathHelper, true);
		TriggerRepopulate(PathHelper, true);
	}
}

bool AITwinPathAnimTool::IsLoop(FAnimPathIdentifier PathHandle) const
{
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->IsLoop();
	return false;
}

void AITwinPathAnimTool::SetIsLoop(FAnimPathIdentifier PathHandle, bool isLoop)
{
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
	{
	#ifdef DEBUG_AV_ANIMATION
		// Temporal hook to test articulated vehicles on the spline
		if (Impl->HasArticulatedVehicleSupport())
		{
			PathHelper->BakeAnimationIfNeeded();
			// Try to create articulated vehicle
			auto TestVehiclePath(TEXT("/Game/CarrotLibrary/UserContent/artic-test-001/artic-test-001_Hierarchy.artic-test-001_Hierarchy"));
			if (Impl->AVConnector->LoadArticulatedVehicleForPath(PathHelper, TestVehiclePath, -1) > 0.f)
				return;
		}
	#endif

		PathHelper->SetIsLoop(isLoop);
		PathHelper->UpdateSpline();
		TriggerRebake(PathHelper, false);
		TriggerRepopulate(PathHelper, true);
	}
}

float AITwinPathAnimTool::GetSpeed(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->GetSpeed();
	return 0.f;
}

void AITwinPathAnimTool::SetSpeed(FAnimPathIdentifier PathHandle, float Speed)
{
	//ensure(PathHandle.PathType == EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
	{
		PathHelper->SetSpeed(Speed);
		TriggerRepopulate(PathHelper, true);
	}
}

float AITwinPathAnimTool::GetDelay(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->GetDelay();
	return 0.f;
}

void AITwinPathAnimTool::SetDelay(FAnimPathIdentifier PathHandle, float Delay)
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
	{
		PathHelper->SetDelay(Delay);
		Impl->ResetAnimation(PathHandle);
	}
}

EITwinAnimPathRepeatMode AITwinPathAnimTool::GetRepeatMode(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->GetRepeatMode();
	return EITwinAnimPathRepeatMode::Count;
}

void AITwinPathAnimTool::SetRepeatMode(FAnimPathIdentifier PathHandle, EITwinAnimPathRepeatMode RepeatMode)
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
	{
		PathHelper->SetRepeatMode(RepeatMode);
		Impl->ResetAnimation(PathHandle);
	}
}

bool AITwinPathAnimTool::IsOneWay(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->IsOneWay();
	return true;
}

void AITwinPathAnimTool::SetOneWay(FAnimPathIdentifier PathHandle, bool bOneWay)
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
	{
		PathHelper->SetOneWay(bOneWay);
		PathHelper->UpdateSpline();
		TriggerRebake(PathHelper, true);
		TriggerRepopulate(PathHelper, true);
	}
}

int AITwinPathAnimTool::GetLaneCount(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->GetLaneCount();
	return 0;
}

void AITwinPathAnimTool::SetLaneCount(FAnimPathIdentifier PathHandle, int LaneCount)
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
	{
		PathHelper->SetLaneCount(LaneCount);
		PathHelper->UpdateSpline();
		TriggerRebake(PathHelper, true);
		TriggerRepopulate(PathHelper, true);
	}
}

float AITwinPathAnimTool::GetLaneWidth(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->GetLaneWidth();
	return 0;
}

void AITwinPathAnimTool::SetLaneWidth(FAnimPathIdentifier PathHandle, float LaneWidth)
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
	{
		PathHelper->SetLaneWidth(LaneWidth);
		PathHelper->UpdateSpline();
		TriggerRebake(PathHelper, true);
	}
}

float AITwinPathAnimTool::GetDensity(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->GetDensity();
	return 0.f;
}

void AITwinPathAnimTool::SetDensity(FAnimPathIdentifier PathHandle, float Density)
{
	ensure(PathHandle.PathType != EITwinAnimPathType::Object);
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
	{
		PathHelper->SetDensity(Density);
		TriggerRepopulate(PathHelper, true);
	}
}

float AITwinPathAnimTool::GetSeparatorWidth(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Traffic);
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->GetSeparatorWidth();
	return 0.f;
}

void AITwinPathAnimTool::SetSeparatorWidth(FAnimPathIdentifier PathHandle, float SeparatorWidth)
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Traffic);
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
	{
		PathHelper->SetSeparatorWidth(SeparatorWidth);
		PathHelper->UpdateSpline();
		TriggerRebake(PathHelper, true);
	}
}

float AITwinPathAnimTool::GetMinSpeed(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Traffic);
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->GetMinSpeed();
	return 0.f;
}

void AITwinPathAnimTool::SetMinSpeed(FAnimPathIdentifier PathHandle, float MinSpeed)
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Traffic);
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
	{
		PathHelper->SetMinSpeed(MinSpeed);
		TriggerRepopulate(PathHelper, true);
	}
}

float AITwinPathAnimTool::GetMaxSpeed(FAnimPathIdentifier PathHandle) const
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Traffic);
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
		return PathHelper->GetMaxSpeed();
	return 0.f;
}

void AITwinPathAnimTool::SetMaxSpeed(FAnimPathIdentifier PathHandle, float MaxSpeed)
{
	ensure(PathHandle.PathType == EITwinAnimPathType::Traffic);
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
	{
		float prevMaxSpeed = PathHelper->GetMaxSpeed();
		PathHelper->SetMaxSpeed(MaxSpeed);
		// Avoid unnecessary repopulation (TODO: add min/max check directly in the UI)
		if (prevMaxSpeed != MaxSpeed && (MaxSpeed > PathHelper->GetMinSpeed() || prevMaxSpeed > PathHelper->GetMinSpeed()))
			TriggerRepopulate(PathHelper, true);
	}
}

float AITwinPathAnimTool::GetTightness(FAnimPathIdentifier PathHandle, int32 PointIndex) const
{
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
	{
		if (ensure(PathHelper->SplineHelper.IsValid()))
		{
			return PathHelper->SplineHelper->GetTightness(PointIndex);
		}
	}
	return 0.f;
}

void AITwinPathAnimTool::SetTightness(FAnimPathIdentifier PathHandle, int32 PointIndex, float Tightness)
{
	if (auto PathHelper = Impl->GetMutableAnimPathHelper(PathHandle))
	{
		if (ensure(PathHelper->SplineHelper.IsValid()))
		{
			PathHelper->SplineHelper->SetTightness(PointIndex, Tightness);
			TriggerRebake(PathHelper, true);
		}
	}
}

int32 AITwinPathAnimTool::GetSelectedSplinePoint(FAnimPathIdentifier PathHandle, bool& CanEditTangents) const
{
	CanEditTangents = true;
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
	{
		if (Impl->SplineTool.IsValid()
			&& Impl->SplineTool->GetSelectedSpline() == PathHelper->SplineHelper.Get()
			&& Impl->SplineTool->HasSelectedPoint())
		{
			auto PointIndex = Impl->SplineTool->GetSelectedPointIndex();
			if (!PathHelper->SplineHelper->IsClosedLoop() &&
				(PointIndex == 0 || PointIndex == PathHelper->SplineHelper->GetNumberOfSplinePoints() - 1))
				CanEditTangents = false;
			return PointIndex;
		}
	}
	return INDEX_NONE;
}

float AITwinPathAnimTool::GetPathLength(FAnimPathIdentifier PathHandle) const
{
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
	{
		if (ensure(PathHelper->SplineHelper.IsValid()))
		{
			return PathHelper->SplineHelper->GetSplineComponent()->GetSplineLength();
		}
	}
	return 0.f;
}

int32 AITwinPathAnimTool::GetInstancesNum(FAnimPathIdentifier PathHandle) const
{
	int32 nbInstances(0);
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
	{
		for (auto population : PathHelper->Populations)
			nbInstances += population.IsValid() ? population->GetNumberOfInstances() : 0;
		if (Impl->HasArticulatedVehicleSupport())
			nbInstances += Impl->AVConnector->GetArticulatedVehicleCount(PathHelper);
	}

	return nbInstances;
}

bool AITwinPathAnimTool::IsSplineToolActive(FAnimPathIdentifier PathHandle) const
{
	if (auto PathHelper = Impl->GetAnimPathHelper(PathHandle))
	{
		if (Impl->SplineTool.IsValid() && Impl->SplineTool->GetSelectedSpline() == PathHelper->SplineHelper.Get())
			return Impl->SplineTool->IsInteractiveCreationMode();
	}
	return false;
}

bool AITwinPathAnimTool::StartInteractiveCreation(EITwinAnimPathType PathType)
{
	// Abort current anim path creation, if any
	AbortInteractiveCreation(true);

	// Make sure we hide all anim path proxies (only the new item will be visible).
	Impl->HideAllAnimPathProxies();

	// Start interactive drawing
	TWeakObjectPtr<AITwinSplineTool> SplineTool = Impl->ActivateSplineTool(GetWorld(), PathType);
	if (SplineTool.IsValid())
	{
		// Activate overview camera (top view)
		//SplineTool->OnOverviewCamera(); // no need for path animation for now
		SplineTool->StartInteractiveCreation();
		return true;
	}
	return false;
}

UITwinAnimPathHelper* AITwinPathAnimTool::FImpl::CreatePath(EITwinAnimPathType PathType)
{
	switch (PathType)
	{
	case EITwinAnimPathType::Object:
	{
		TStrongObjectPtr<UITwinObjectAnimPathHelper> ObjectPathHelper(NewObject<UITwinObjectAnimPathHelper>(&Owner));
		ObjectAnimPaths.Add(ObjectPathHelper);
		return ObjectPathHelper.Get();
	}
	case EITwinAnimPathType::Traffic:
	{
		TStrongObjectPtr<UITwinTrafficAnimPathHelper> TrafficPathHelper(NewObject<UITwinTrafficAnimPathHelper>(&Owner));
		TrafficAnimPaths.Add(TrafficPathHelper);
		return TrafficPathHelper.Get();
	}
	case EITwinAnimPathType::Crowd:
	{
		TStrongObjectPtr<UITwinCrowdAnimPathHelper> CrowdPathHelper(NewObject<UITwinCrowdAnimPathHelper>(&Owner));
		CrowdAnimPaths.Add(CrowdPathHelper);
		return CrowdPathHelper.Get();
	}
	BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(case EITwinAnimPathType::Count:, nullptr);
	}
}

bool AITwinPathAnimTool::FImpl::RegisterAnimPathSpline(AITwinSplineHelper* SplineHelper)
{
	// This function is called when a new spline is created with the Spline Tool, and also when an existing spline
	// is loaded from the server. If we are loading an existing path animation spline from server, we should wait until
	// all the animation paths are loaded before registering them here, so we ignore this for now
	if (SplineHelper->GetAVizSplineId().HasDBIdentifier())
		return false;

	// Check whether it's actually a new spline and not interactive edition of an existing one
	if (GetPathIdentifierFromSpline(SplineHelper->GetAVizSplineId()).IsValid())
		return false;

	FAnimPathIdentifier PathHandle;
	PathHandle.PathType = GetAnimPathTypeFromSplineUsage(SplineHelper->GetUsage());
	PathHandle.PathIndex = NumPaths(PathHandle.PathType);

	UITwinAnimPathHelper* PathHelper = CreatePath(PathHandle.PathType);
	if (!PathHelper)
		return false;

	// Creating new spline
	auto PathPropPtr = PathAnimManager->AddAnimationPathInfo();
	auto PathProp = PathPropPtr->GetAutoLock();
	PathProp->SetSplineId(SplineHelper->GetAVizSplineId());
	if (bLeftHandDrive)
		PathProp->SetInvDir(*bLeftHandDrive);
	// Make sure the created path gets saved on the server, even if no other changes have been made.
	// (Default save status is NeverSaved, which is ignored by the save system.)
	PathProp->SetShouldSave(true);
	PathHelper->Init(SplineHelper, PathPropPtr);

	PathAnimChanged(PathHandle.PathType, EChangeType::Added);

	Owner.AnimPathListModifiedEvent.Broadcast();
	Owner.AnimPathAddedEvent.Broadcast(PathHandle);
	return true;
}

void AITwinPathAnimTool::OnSplineHelperAdded(AITwinSplineHelper* NewSpline)
{
	if (IsSplineUsedForPathAnim(NewSpline))
	{
		Impl->RegisterAnimPathSpline(NewSpline);
	}
}

bool AITwinPathAnimTool::FImpl::UnregisterAnimPathSpline(AITwinSplineHelper* SplineBeingRemoved, bool bTriggeredFromITS)
{
	auto const SelectedBefore = GetSelectedPath();

	auto PathHandle = GetPathIdentifierFromSpline(SplineBeingRemoved->GetAVizSplineId());
	if (PathHandle.IsValid(NumPaths(PathHandle.PathType))) // path handle can be invalid if the spline creation was cancelled (and therefore the path isn't registered yet)
	{
		// Remove associated objects from the population tool, if any
		RemovePathObjects(PathHandle);
		// Mark path as removed on the server side
		PathAnimManager->RemoveAnimationPathInfo(GetAnimPathHelper(PathHandle)->GetPathRefID());
		// Remove path helper
		switch (PathHandle.PathType)
		{
		case EITwinAnimPathType::Object:
			ObjectAnimPaths.RemoveAt(PathHandle.PathIndex);
			break;
		case EITwinAnimPathType::Traffic:
			TrafficAnimPaths.RemoveAt(PathHandle.PathIndex);
			break;
		case EITwinAnimPathType::Crowd:
			CrowdAnimPaths.RemoveAt(PathHandle.PathIndex);
			break;
		default:
			break;
		}

		if (!bTriggeredFromITS && !SplineBeingRemoved->IsInteractiveCreationInProgress())
		{
			PathAnimChanged(PathHandle.PathType, EChangeType::Deleted, TEXT("key_down"));
		}

		Owner.AnimPathRemovedEvent.Broadcast(PathHandle, bTriggeredFromITS);
		Owner.AnimPathListModifiedEvent.Broadcast();

		// After removing a path, we should exit isolation mode or else we'll be in an inconsistent state
		if (SelectedBefore)
		{
			SetAllAnimPathProxiesVisibility(true);
		}
		return true;
	}

	return false;
}

void AITwinPathAnimTool::OnSplineHelperRemoved(AITwinSplineHelper* SplineBeingRemoved, bool bTriggeredFromITS)
{
	if (IsSplineUsedForPathAnim(SplineBeingRemoved))
	{
		Impl->UnregisterAnimPathSpline(SplineBeingRemoved, bTriggeredFromITS);
	}
}

void AITwinPathAnimTool::OnItemCreationAbortedInTool(const AITwinInteractiveTool* Tool, bool bTriggeredFromITS)
{
	if (Tool && Tool->IsUsedForPathAnim())
	{
		InteractiveCreationAbortedEvent.Broadcast(bTriggeredFromITS);
	}
}

bool AITwinPathAnimTool::FImpl::NeedRepopulateSplineAfterEdition(UITwinAnimPathHelper* PathHelper)
{
	if (!PathHelper)
		return false;
	// We do not need to repopulate the path if the spline length is still close enough to the baked animation length
	if (FMath::Abs(PathHelper->GetSplineLength() - PathHelper->GetBakedSplineLength()) < 300.f)
		return false;
	return true;
}

void AITwinPathAnimTool::FImpl::OnSplineEditedInTool()
{
	if (!SplineTool.IsValid() || !SplineTool->IsUsedForPathAnim() || !SplineTool->GetSelectedSpline())
		return;

	if (SplineTool->IsInteractiveCreationMode())
	{
		// Path not yet created/finalized, no need to invalidate anything.
		return;
	}

	auto PathHandle = GetPathIdentifierFromSpline(SplineTool->GetSelectedSpline()->GetAVizSplineId());
	if (auto PathHelper = GetMutableAnimPathHelper(PathHandle))
	{
		bool bNeedRepopulate = NeedRepopulateSplineAfterEdition(PathHelper);
		PathHelper->InvalidateBakedAnimation();
		if (bNeedRepopulate)
			Owner.TriggerRepopulate(PathHelper, true);
	}

	Owner.SelectedAnimPathModifiedEvent.Broadcast(); // update UI
}

void AITwinPathAnimTool::OnSplineEditedInTool()
{
	Impl->OnSplineEditedInTool();
}

void AITwinPathAnimTool::OnSplinePointMovedInTool(bool bTriggeredFromITS)
{
	Impl->OnSplineEditedInTool();
}

void AITwinPathAnimTool::FImpl::PathAnimChanged(EITwinAnimPathType PathType, EChangeType change,
	const FString& eventSource /*= FString()*/, const FString& modificationKind /*= FString()*/)
{
	FFeatureEventProperties properties;
	properties.ChangeType = change;
	properties.FeatureType = EFeatureType::PathAnimation;

	FString path_animation_type;
	switch (PathType)
	{
	case EITwinAnimPathType::Object:	path_animation_type = TEXT("single"); break;
	case EITwinAnimPathType::Traffic:	path_animation_type = TEXT("traffic"); break;
	case EITwinAnimPathType::Crowd:		path_animation_type = TEXT("crowd"); break;
	default: break;
	}
	properties.AddProperty(TEXT("path_animation_type"), path_animation_type);
	if (!eventSource.IsEmpty())
	{
		properties.AddProperty(TEXT("event_source"), eventSource);
	}
	if (!modificationKind.IsEmpty())
	{
		properties.AddProperty(TEXT("path_animation_setting"), modificationKind);
	}

	Owner.PathAnimChangedEvent.Broadcast(properties);
}

void AITwinPathAnimTool::OnSplineMoveStart()
{
	Impl->OnSplineChanged(TEXT("gizmo"), TEXT("position"));
}

void AITwinPathAnimTool::OnSplinePointMoveStart()
{
	Impl->OnSplineChanged(TEXT("gizmo"), TEXT("point_position"));
}

void AITwinPathAnimTool::FImpl::OnSplineChanged(const FString& eventSource, const FString& modificationKind)
{
	if (SplineTool.IsValid() && SplineTool->IsUsedForPathAnim()
		&& SplineTool->GetSelectedSpline()
		&& !SplineTool->IsInteractiveCreationMode()
		&& !Owner.IsLoadingAnimationPaths())
	{
		auto PathHandle = GetPathIdentifierFromSpline(SplineTool->GetSelectedSpline()->GetAVizSplineId());
		if (PathHandle.IsValid(NumPaths(PathHandle.PathType)))
		{
			PathAnimChanged(PathHandle.PathType, EChangeType::Modified, eventSource, modificationKind);
		}
	}
}

void AITwinPathAnimTool::OnSplinePointAddedInTool()
{
	Impl->OnSplineChanged(TEXT("mouse_click"), TEXT("point_added"));
}

void AITwinPathAnimTool::OnSplinePointRemovedInTool()
{
	Impl->OnSplineChanged(TEXT("key_down"), TEXT("point_removed"));
}

inline int32 AITwinPathAnimTool::FImpl::NumPaths(EITwinAnimPathType PathType) const
{
	switch (PathType)
	{
	case EITwinAnimPathType::Object:	return ObjectAnimPaths.Num();
	case EITwinAnimPathType::Traffic:	return TrafficAnimPaths.Num();
	case EITwinAnimPathType::Crowd:		return CrowdAnimPaths.Num();
	BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(case EITwinAnimPathType::Count: , 0);
	}
}

int32 AITwinPathAnimTool::NumPaths(EITwinAnimPathType PathType) const
{
	return Impl->NumPaths(PathType);
}

int32 AITwinPathAnimTool::NumPaths() const
{
	int32 totalPaths(0);
	for (EITwinAnimPathType PathType : {
		EITwinAnimPathType::Object,
		EITwinAnimPathType::Traffic,
		EITwinAnimPathType::Crowd })
	{
		totalPaths += NumPaths(PathType);
	}
	return totalPaths;
}

inline UITwinAnimPathHelper* AITwinPathAnimTool::FImpl::GetMutableAnimPathHelper(FAnimPathIdentifier PathHandle)
{
	if (!PathHandle.IsValid(NumPaths(PathHandle.PathType)))
		return nullptr;
	switch (PathHandle.PathType)
	{
	BE_UNCOVERED_ENUM_ASSERT_AND_FALLTHROUGH(case EITwinAnimPathType::Count: )
	case EITwinAnimPathType::Object:	return ObjectAnimPaths[PathHandle.PathIndex].Get();
	case EITwinAnimPathType::Traffic:	return TrafficAnimPaths[PathHandle.PathIndex].Get();
	case EITwinAnimPathType::Crowd:		return CrowdAnimPaths[PathHandle.PathIndex].Get();
	}
}

inline const UITwinAnimPathHelper* AITwinPathAnimTool::FImpl::GetAnimPathHelper(FAnimPathIdentifier PathHandle) const
{
	if (!PathHandle.IsValid(NumPaths(PathHandle.PathType)))
		return nullptr;
	switch (PathHandle.PathType)
	{
	BE_UNCOVERED_ENUM_ASSERT_AND_FALLTHROUGH(case EITwinAnimPathType::Count:)
	case EITwinAnimPathType::Object:	return ObjectAnimPaths[PathHandle.PathIndex].Get();
	case EITwinAnimPathType::Traffic:	return TrafficAnimPaths[PathHandle.PathIndex].Get();
	case EITwinAnimPathType::Crowd:		return CrowdAnimPaths[PathHandle.PathIndex].Get();
	}
}

bool AITwinPathAnimTool::FImpl::RemovePath(FAnimPathIdentifier PathHandle, bool bTriggeredFromITS)
{
	if (!ensure(PathHandle.IsValid(NumPaths(PathHandle.PathType))))
		return false;

	// Select the animation path if needed (for undo/redo) - the path is already selected if this event is
	// triggered from iTS animation path properties page, but not if the event is triggered from the list of paths.
	auto const CurrentSelection = GetSelectedPath();
	bool bPathIsSelected = CurrentSelection
		&& CurrentSelection->PathType == PathHandle.PathType
		&& CurrentSelection->PathIndex == PathHandle.PathIndex;
	if (!bPathIsSelected)
	{
		bPathIsSelected = SelectPath(PathHandle, false);
	}
	//Owner.RemoveAnimPathStartedEvent.Broadcast(); TODO

	const int32 NumPathsOld = NumPaths(PathHandle.PathType);
	PlayAnimation(PathHandle, false);
	if (auto PathHelper = GetAnimPathHelper(PathHandle))
	{
		// Remove spline and path animation info
		if (PathHelper->SplineHelper.IsValid() && ensure(SplineTool.IsValid()))
			SplineTool->DeleteSpline(PathHelper->SplineHelper.Get(), bTriggeredFromITS);
	}

	return true;
}

bool AITwinPathAnimTool::RemovePath(FAnimPathIdentifier PathHandle, bool bTriggeredFromITS)
{
	const bool bRemoved = Impl->RemovePath(PathHandle, bTriggeredFromITS);
	if (bRemoved)
	{
		//RemoveAnimPathCompletedEvent.Broadcast(); TODO
	}
	return bRemoved;
}

bool AITwinPathAnimTool::FImpl::SelectPath(FAnimPathIdentifier PathHandle, bool bEnterIsolationMode)
{
	if (!ensure(PathHandle.IsValid(NumPaths(PathHandle.PathType))))
		return false;

	bool bHasSetSelection = false;
	auto PathHelper = GetAnimPathHelper(PathHandle);
	if (PathHelper && PathHelper->SplineHelper.IsValid())
	{
		SelectSpline(PathHelper->SplineHelper.Get(), Owner.GetWorld());

		bHasSetSelection = PathHelper->SplineHelper->IsSelected();
	}
	if (bEnterIsolationMode && bHasSetSelection)
	{
		ShowOnlyAnimPathProxiesOfType(PathHandle.PathType, true);
	}
	return bHasSetSelection;
}

bool AITwinPathAnimTool::SelectPath(FAnimPathIdentifier PathHandle, bool bEnterIsolationMode)
{
	return Impl->SelectPath(PathHandle, bEnterIsolationMode);
}

std::optional<FAnimPathIdentifier> AITwinPathAnimTool::FImpl::GetSelectedPath() const
{
	if (ensure(SplineTool.IsValid()) && SplineTool->IsUsedForPathAnim())
	{
		if (auto SelectedSpline = SplineTool->GetSelectedSpline())
			return GetPathIdentifierFromSpline(SelectedSpline->GetAVizSplineId());
	}
	return std::nullopt;
}

std::optional<FAnimPathIdentifier> AITwinPathAnimTool::GetSelectedPath() const
{
	return Impl->GetSelectedPath();
}

void AITwinPathAnimTool::DeSelectAll(bool bExitIsolationMode)
{
	auto CurrentSelection = GetSelectedPath();
	if (CurrentSelection)
	{
		Impl->SelectSpline(nullptr, GetWorld());
		
		if (bExitIsolationMode)
		{
			// Restore visibility of proxies.
			Impl->SetAllAnimPathProxiesVisibility(true);
		}
	}
	BroadcastSelection();
}

void AITwinPathAnimTool::FImpl::UpdateAnimatedObjects(FAnimPathIdentifier PathHandle, float DeltaTime, bool bTimelineMode)
{
	if (auto PathHelper = GetMutableAnimPathHelper(PathHandle))
	{
		if (!PathHelper->IsVisible() || PathHelper->IsPaused())
			return;
		if (!PathHelper->HasBakedAnimation())
		{
			PathHelper->BakeAnimationIfNeeded();
			return;
		}

		for (auto Population : PathHelper->Populations)
		{
			if (!Population.IsValid())
				continue;
			PathAnimPlaybackSettings Settings(PathHelper, 0, true);
			for (int32 instIdx(0); instIdx < Population->GetNumberOfInstances(); instIdx++)
			{
				if (auto InstancePtr = Population->GetAVizInstance(instIdx))
				{
					auto inst = InstancePtr->GetRAutoLock();
					if (auto animPathExt = inst->GetExtension<InstanceWithAnimPathExt>())
					{
						Settings.bReverse = PathHelper->IsInvDirLane(animPathExt->GetLaneIndex());
						auto transform = animPathExt->GetTransform(DeltaTime, bTimelineMode, Settings);
						Population->SetInstanceTransformUEOnly(instIdx, transform);
					}
				}
			}
		}
	}
}

void AITwinPathAnimTool::FImpl::UpdateAllAnimatedObjects(float DeltaTime)
{
	bool bTimelineMode(false);
	if (Owner.GetTimelineFixedTime.IsBound())
	{
		auto TimelineFixedTime = Owner.GetTimelineFixedTime.Execute();
		if (TimelineFixedTime.IsSet())
		{
			bTimelineMode = true;
			DeltaTime = TimelineFixedTime.GetValue();
		}
	}

	for (EITwinAnimPathType PathType : {
		EITwinAnimPathType::Object,
		EITwinAnimPathType::Traffic,
		EITwinAnimPathType::Crowd })
	{
		for (int32 Index(0); Index < NumPaths(PathType); ++Index)
		{
			UpdateAnimatedObjects(FAnimPathIdentifier(PathType, Index), DeltaTime, bTimelineMode);
		}
	}

	// Update articulated vehicles
	//if (HasArticulatedVehicleSupport())
	//{
	//	AVConnector->UpdateArticulatedVehicles(DeltaTime, bTimelineMode);
	//}
}

void AITwinPathAnimTool::FImpl::ZoomOnPath(FAnimPathIdentifier PathHandle)
{
	auto PathHelper = GetAnimPathHelper(PathHandle);
	if (PathHelper && PathHelper->SplineHelper.IsValid())
	{
		// use overview camera for zoom
		Owner.OnOverviewCamera(PathHelper->SplineHelper.Get());
	}
}

void AITwinPathAnimTool::ZoomOnPath(FAnimPathIdentifier PathHandle)
{
	Impl->ZoomOnPath(PathHandle);
}

void AITwinPathAnimTool::FImpl::ResetAnimation(FAnimPathIdentifier PathHandle)
{
	// Reset animation resets the animation delay and is only supported for single object animation paths
	if (!ensure(PathHandle.PathType == EITwinAnimPathType::Object))
		return;
	if (!ensure(PathHandle.IsValid(NumPaths(PathHandle.PathType))))
		return;

	auto PathHelper = GetAnimPathHelper(PathHandle);
	if (!PathHelper)
		return;
	for (auto Population : PathHelper->Populations)
	{
		if (!Population.IsValid())
			continue;

		for (int32 instIdx(0); instIdx < Population->GetNumberOfInstances(); instIdx++)
		{
			if (auto InstancePtr = Population->GetAVizInstance(instIdx))
			{
				auto inst = InstancePtr->GetRAutoLock();
				if (auto animPathExt = inst->GetExtension<InstanceWithAnimPathExt>())
				{
					// If animation was baked after path population, the start transform might not be
					// initialized correctly (not oriented along the spline), so we reset it here.
					animPathExt->ResetAnimation(PathHelper->GetDelay(), 
						std::nullopt, 
						PathHelper->GetStartTransform(animPathExt->GetLaneIndex(), true));
				}
			}
		}
	}
}

void AITwinPathAnimTool::ResetAnimation(FAnimPathIdentifier PathHandle)
{
	Impl->ResetAnimation(PathHandle);
}

void AITwinPathAnimTool::FImpl::PlayAnimation(FAnimPathIdentifier PathHandle, bool bPlay)
{
	if (auto PathHelper = GetMutableAnimPathHelper(PathHandle))
		PathHelper->SetPaused(!bPlay);
}

void AITwinPathAnimTool::PlayAnimation(FAnimPathIdentifier PathHandle, bool bPlay)
{
	Impl->PlayAnimation(PathHandle, bPlay);
}

void AITwinPathAnimTool::FImpl::SetAnimPathProxyVisibility(EITwinAnimPathType PathType, bool bVisibleInGame, bool bIsolationMode/* = false*/)
{
	for (int32 i(0); i < NumPaths(PathType); i++)
	{
		auto PathHelper = GetAnimPathHelper(FAnimPathIdentifier(PathType, i));
		if (PathHelper && PathHelper->SplineHelper.IsValid())
		{
			const bool bShowSpline = bVisibleInGame
				&& (!bIsolationMode || PathHelper->SplineHelper->IsSelected())
				&& PathHelper->IsVisible();
			PathHelper->SplineHelper->SetActorHiddenInGame(!bShowSpline);
		}
	}
}

bool AITwinPathAnimTool::FImpl::IsAnimPathProxyVisible(EITwinAnimPathType PathType) const
{
	for (int32 i(0); i < NumPaths(PathType); i++)
	{
		auto PathHelper = GetAnimPathHelper(FAnimPathIdentifier(PathType, i));
		if (PathHelper && PathHelper->SplineHelper.IsValid())
		{
			return !PathHelper->SplineHelper->IsHidden();
		}
	}

	return false;
}

void AITwinPathAnimTool::FImpl::SetAllAnimPathProxiesVisibility(bool bVisibleInGame)
{
	for (EITwinAnimPathType PathType : TEnumRange<EITwinAnimPathType>())
	{
		SetAnimPathProxyVisibility(PathType, bVisibleInGame);
	}
}

void AITwinPathAnimTool::FImpl::ShowOnlyAnimPathProxiesOfType(EITwinAnimPathType SelectedType, bool bIsolationMode)
{
	for (EITwinAnimPathType PathType : TEnumRange<EITwinAnimPathType>())
	{
		SetAnimPathProxyVisibility(PathType, PathType == SelectedType, bIsolationMode);
	}
}

void AITwinPathAnimTool::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	Impl->UpdateAllAnimatedObjects(DeltaTime);
}

void AITwinPathAnimTool::FImpl::OnActivatePicking(bool bActivate)
{
	if (bActivate)
	{
		// Beware the tool can be activated *after* the user selects animation path from the list in the UI: in
		// such case, we should not make all proxies visible, but instead preserve the current isolation
		// mode.
		auto const CurrentSelection = GetSelectedPath();
		if (CurrentSelection)
			ShowOnlyAnimPathProxiesOfType(CurrentSelection->PathType, true);
		else
		{
			if (!SplineTool->IsEnabled() || !SplineTool->IsUsedForPathAnim())
				ITwin::ActivateSplineTool(Owner.GetWorld(), EITwinSplineUsage::AnimPath, SplineTool);
			SetAllAnimPathProxiesVisibility(true);
		}
	}
	else
	{
		HideAllAnimPathProxies();
	}
}

void AITwinPathAnimTool::OnActivatePicking(bool bActivate)
{
	Impl->OnActivatePicking(bActivate);
}

bool AITwinPathAnimTool::FImpl::DoMouseClickPicking(bool& bOutSelectionGizmoNeeded)
{
	bOutSelectionGizmoNeeded = false;

	if (ensure(SplineTool.IsValid()))
	{
		// Spline tool should already be activated when path selection mode is set (see AITwinPathAnimTool::FImpl::OnActivatePicking)
		ensure(SplineTool->IsEnabled() && SplineTool->IsUsedForPathAnim());

		if (SplineTool->DoMouseClickAction())
		{
			bOutSelectionGizmoNeeded = SplineTool->HasSelection();
			return true;
		}
	}

	return false;
}

bool AITwinPathAnimTool::DoMouseClickPicking(bool& bOutSelectionGizmoNeeded)
{
	bool bRelevantAction = false;
	if (NumPaths() > 0)
		bRelevantAction = Impl->DoMouseClickPicking(bOutSelectionGizmoNeeded);

	auto const NewSelection = GetSelectedPath();
	if (NewSelection)
	{
		// Isolation of the selected item, if any.
		Impl->ShowOnlyAnimPathProxiesOfType(NewSelection->PathType, true);
	}
	else
	{
		// End of isolation mode.
		Impl->SetAllAnimPathProxiesVisibility(true);
	}

	// Notify new selection. If nothing is selected, notify it as well
	BroadcastSelection();

	return bRelevantAction;
}

void AITwinPathAnimTool::BroadcastSelection()
{
	// Notify new selection (using -1 as index if nothing i selected)
	auto const NewSelection = GetSelectedPath();
	if (NewSelection)
	{
		AnimPathSelectedEvent.Broadcast(NewSelection.value());
	}
	else
	{
		AnimPathSelectedEvent.Broadcast(FAnimPathIdentifier());
	}
}

void AITwinPathAnimTool::OnPathAnimPolygonSelected()
{
	BroadcastSelection();
}

void AITwinPathAnimTool::OnOverviewCamera(AITwinSplineHelper const* SpecificSpline)
{
	UWorld* World = GetWorld();
	if (!World)
		return;
	TWeakObjectPtr<AITwinSplineTool> SplineTool = Impl->ActivateSplineTool(World, GetAnimPathTypeFromSplineUsage(SpecificSpline->GetUsage()));
	if (SplineTool.IsValid())
	{
		SplineTool->OnOverviewCamera(SpecificSpline);
	}
}

//template <typename Func>
//void AITwinPathAnimTool::FImpl::VisitAnimPathOfType(EITwinAnimPathType PathType, Func const& Fn)
//{
//	switch (PathType)
//	{
//	case EITwinAnimPathType::Object:
//		for (auto PathHelper : ObjectAnimPaths)
//		{
//			Fn(PathHelper);
//		}
//		break;
//
//	case EITwinAnimPathType::Crowd:
//		for (auto PathHelper : CrowdAnimPaths)
//		{
//			Fn(PathHelper);
//		}
//		break;
//
//	case EITwinAnimPathType::Traffic:
//		for (auto PathHelper : TrafficAnimPaths)
//		{
//			Fn(PathHelper);
//		}
//		break;
//
//	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinAnimPathType::Count: );
//	}
//}

FAnimPathIdentifier AITwinPathAnimTool::FImpl::GetPathIdentifierFromSpline(AdvViz::SDK::RefID const& RefID) const
{
	int32 Index = ObjectAnimPaths.IndexOfByPredicate(
		[&RefID](TStrongObjectPtr<UITwinObjectAnimPathHelper> const InItem)
		{
			return InItem->SplineHelper.IsValid() && InItem->SplineHelper->GetAVizSplineId() == RefID;
		});
	if (Index != INDEX_NONE)
		return FAnimPathIdentifier(EITwinAnimPathType::Object, Index);

	Index = TrafficAnimPaths.IndexOfByPredicate(
		[&RefID](TStrongObjectPtr<UITwinTrafficAnimPathHelper> const InItem)
		{
			return InItem->SplineHelper.IsValid() && InItem->SplineHelper->GetAVizSplineId() == RefID;
		});
	if (Index != INDEX_NONE)
		return FAnimPathIdentifier(EITwinAnimPathType::Traffic, Index);

	Index = CrowdAnimPaths.IndexOfByPredicate(
		[&RefID](TStrongObjectPtr<UITwinCrowdAnimPathHelper> const InItem)
		{
			return InItem->SplineHelper.IsValid() && InItem->SplineHelper->GetAVizSplineId() == RefID;
		});
	if (Index != INDEX_NONE)
		return FAnimPathIdentifier(EITwinAnimPathType::Crowd, Index);

	return FAnimPathIdentifier(EITwinAnimPathType::Count, INDEX_NONE);
}

AdvViz::SDK::RefID AITwinPathAnimTool::FImpl::GetPathRefId(FAnimPathIdentifier PathHandle) const
{
	auto PathHelper = GetAnimPathHelper(PathHandle);
	return PathHelper ? PathHelper->GetPathRefID() : AdvViz::SDK::RefID::Invalid();
}

AdvViz::SDK::RefID AITwinPathAnimTool::GetPathRefId(FAnimPathIdentifier PathHandle) const
{
	return Impl->GetPathRefId(PathHandle);
}

FAnimPathIdentifier AITwinPathAnimTool::FImpl::GetPathIdentifier(AdvViz::SDK::RefID const& RefID) const
{
	int32 Index = ObjectAnimPaths.IndexOfByPredicate(
		[&RefID](TStrongObjectPtr<UITwinObjectAnimPathHelper> const InItem)
		{
			return InItem->GetPathRefID() == RefID;
		});
	if (Index != INDEX_NONE)
		return FAnimPathIdentifier(EITwinAnimPathType::Object, Index);

	Index = TrafficAnimPaths.IndexOfByPredicate(
		[&RefID](TStrongObjectPtr<UITwinTrafficAnimPathHelper> const InItem)
		{
			return InItem->GetPathRefID() == RefID;
		});
	if (Index != INDEX_NONE)
		return FAnimPathIdentifier(EITwinAnimPathType::Traffic, Index);

	Index = CrowdAnimPaths.IndexOfByPredicate(
		[&RefID](TStrongObjectPtr<UITwinCrowdAnimPathHelper> const InItem)
		{
			return InItem->GetPathRefID() == RefID;
		});
	if (Index != INDEX_NONE)
		return FAnimPathIdentifier(EITwinAnimPathType::Crowd, Index);

	return FAnimPathIdentifier(EITwinAnimPathType::Count, INDEX_NONE);
}

FAnimPathIdentifier AITwinPathAnimTool::GetPathIdentifier(AdvViz::SDK::RefID const& RefID) const
{
	return Impl->GetPathIdentifier(RefID);
}

void AITwinPathAnimTool::AbortInteractiveCreation(bool bTriggeredFromITS)
{
	// Abort current animation path creation, if any
	AITwinInteractiveTool* ActiveTool = AITwinInteractiveTool::GetActiveTool(GetWorld());
	if (ActiveTool && ActiveTool->IsUsedForPathAnim())
	{
		if (ActiveTool->IsInteractiveCreationMode())
			ActiveTool->AbortInteractiveCreation(bTriggeredFromITS);
		ActiveTool->SetEnabled(false);
		ActiveTool->SetUsedForPathAnim(false);
	}
}

void AITwinPathAnimTool::Deactivate()
{
	// Abort current path creation, if any.
	AbortInteractiveCreation(true);

	// Deselect all, without changing the visibility (since we will hide all below...)
	DeSelectAll(false);

	// Trigger event to refresh the selection gizmo
	ActivationEvent.Broadcast(false);

	Impl->HideAllAnimPathProxies();
}

void AITwinPathAnimTool::FImpl::ToggleSplineToolForSelectedPath()
{
	if (SplineTool.IsValid())
	{
		auto SelectedSpline = SplineTool->GetSelectedSpline();
		if (SelectedSpline && SelectedSpline->IsUsedForPathAnim())
		{
			SplineTool->ToggleInteractiveEditionMode();
		}
	}
}

void AITwinPathAnimTool::ToggleSplineToolForSelectedPath()
{
	Impl->ToggleSplineToolForSelectedPath();
}
