/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingTool.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Clipping/ITwinClippingTool.h>

#include <CesiumGlobeAnchorComponent.h>
#include <Clipping/ITwinBoxTileExcluder.h>
#include <Clipping/ITwinClippingCustomPrimitiveDataHelper.h>
#include <Clipping/ITwinClippingMPCHolder.h>
#include <Clipping/ITwinPlaneTileExcluder.h>
#include <Helpers/ITwinConsoleCommandUtils.inl>
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
#include <DrawDebugHelpers.h>
#include <EngineUtils.h> // for TActorIterator<>
#include <Engine/StaticMeshActor.h>
#include <Materials/MaterialParameterCollection.h>
#include <Materials/MaterialParameterCollectionInstance.h>

#include <glm/matrix.hpp>
#include <glm/gtc/matrix_access.hpp>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Compil/EnumSwitchCoverage.h>
#	include <Core/Tools/Log.h>
#include <Compil/AfterNonUnrealIncludes.h>


class AITwinClippingTool::FImpl
{
public:
	AITwinClippingTool& Owner;

	// Store whether the removal event was initiated by Unreal (delete key in 3D viewport) or iTwin Studio
	// (trash icon in Cutout Property Page).
	enum class ERemovalInitiator : uint8_t
	{
		Unreal,
		ITS
	};
	std::optional<ERemovalInitiator> RemovalInitiatorOpt;

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
};


AITwinClippingTool::AITwinClippingTool()
	: Impl(MakePimpl<FImpl>(*this))
{
	this->ClippingMPCHolder = CreateDefaultSubobject<UITwinClippingMPCHolder>(TEXT("MPC_Holder"));
}

UMaterialParameterCollection* AITwinClippingTool::GetMPCClipping()
{
	return this->ClippingMPCHolder->GetMPCClipping();
}

UMaterialParameterCollectionInstance* AITwinClippingTool::GetMPCCLippingInstance()
{
	UWorld* World = GetWorld();
	if (ensure(World))
	{
		return World->GetParameterCollectionInstance(GetMPCClipping());
	}
	return nullptr;
}

void AITwinClippingTool::RegisterTileset(FITwinTilesetAccess const& TilesetAccess)
{
	// When a new tileset is created, automatically apply global clipping effects to it, if any.
	UpdateTileset(TilesetAccess);
}

namespace
{
	UITwinClippingCustomPrimitiveDataHelper* GetClippingCPDHelper(ACesium3DTileset const& Tileset, ITwin::ModelLink const& ModelIdentifier)
	{
		// TODO_JDE: Possible coding improvement: share more code between AITwinIModel, AITwinRealityData and
		// AITwinGoogle3DTileset to avoid this kind of duplication...

		switch (ModelIdentifier.first)
		{
		case EITwinModelType::GlobalMapLayer:
		{
			if (ensure(Tileset.IsA(AITwinGoogle3DTileset::StaticClass())))
			{
				return Cast<AITwinGoogle3DTileset const>(&Tileset)->GetClippingHelper();
			}
			break;
		}

		case EITwinModelType::IModel:
		{
			AActor* Owner = Tileset.GetOwner();
			if (Owner && ensure(Owner->IsA(AITwinIModel::StaticClass())))
			{
				return Cast<AITwinIModel const>(Owner)->GetClippingHelper();
			}
			break;
		}

		case EITwinModelType::RealityData:
		{
			AActor* Owner = Tileset.GetOwner();
			if (Owner && ensure(Owner->IsA(AITwinRealityData::StaticClass())))
			{
				return Cast<AITwinRealityData const>(Owner)->GetClippingHelper();
			}
			break;
		}

		default:
			break;
		}
		// This tileset is not related to iTwin
		return nullptr;
	}

	UITwinClippingCustomPrimitiveDataHelper* MakeClippingCPDHelper(ACesium3DTileset& Tileset, ITwin::ModelLink const& ModelIdentifier)
	{
		// TODO_JDE: Possible coding improvement: share more code between AITwinIModel, AITwinRealityData and
		// AITwinGoogle3DTileset to avoid this kind of duplication...

		bool bCreated = false;
		switch (ModelIdentifier.first)
		{
		case EITwinModelType::GlobalMapLayer:
		{
			if (ensure(Tileset.IsA(AITwinGoogle3DTileset::StaticClass())))
			{
				bCreated = Cast<AITwinGoogle3DTileset>(&Tileset)->MakeClippingHelper();
			}
			break;
		}

		case EITwinModelType::IModel:
		{
			AActor* Owner = Tileset.GetOwner();
			if (Owner && ensure(Owner->IsA(AITwinIModel::StaticClass())))
			{
				bCreated = Cast<AITwinIModel>(Owner)->MakeClippingHelper();
			}
			break;
		}

		case EITwinModelType::RealityData:
		{
			AActor* Owner = Tileset.GetOwner();
			if (Owner && ensure(Owner->IsA(AITwinRealityData::StaticClass())))
			{
				bCreated = Cast<AITwinRealityData>(Owner)->MakeClippingHelper();
			}
			break;
		}

		default:
			break;
		}
		return bCreated ? GetClippingCPDHelper(Tileset, ModelIdentifier) : nullptr;
	}
}

struct AITwinClippingTool::FTilesetUpdateInfo
{
	uint32 AddedExcluders = 0;
	uint32 ActiveEffectsInTileset = 0;
};

void AITwinClippingTool::UpdateTileset_Planes(ACesium3DTileset& Tileset, ITwin::ModelLink const& ModelIdentifier,
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

		FVector3f PlaneOrientation = FVector3f::ZAxisVector;
		float PlaneW(0.f);
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

void AITwinClippingTool::UpdateTileset_Boxes(ACesium3DTileset& Tileset, ITwin::ModelLink const& ModelIdentifier,
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

void AITwinClippingTool::UpdateTileset_Polygons(FITwinTilesetAccess const& TilesetAccess, FTilesetUpdateInfo& UpdateInfo)
{
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
		}
	}
}

void AITwinClippingTool::UpdateTileset(FITwinTilesetAccess const& TilesetAccess,
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

	UITwinClippingCustomPrimitiveDataHelper* CPDHelper = GetClippingCPDHelper(Tileset, ModelIdentifier);
	if (!CPDHelper && UpdateInfo.ActiveEffectsInTileset > 0)
	{
		// Create the helper which will be responsible for updating the Custom Primitive Data in the Unreal
		// meshes, depending on the influences.
		CPDHelper = MakeClippingCPDHelper(Tileset, ModelIdentifier);
	}
	if (CPDHelper)
	{
		if (CPDHelper->UpdateCPDFlagsFromClippingSelection(*this))
		{
			// Update existing meshes, if any.
			CPDHelper->ApplyCPDFlagsToAllMeshComponentsInTileset(Tileset);
			// Future meshes created when a new tile is loaded will be automatically modified through the
			// Cesium lifecycle mesh creation callback.
		}
	}
}

inline
const TWeakObjectPtr<AITwinPopulation>& AITwinClippingTool::GetClippingEffectPopulation(EITwinClippingPrimitiveType Type) const
{
	ensure(Type != EITwinClippingPrimitiveType::Polygon);
	return (Type == EITwinClippingPrimitiveType::Box) ? ClippingBoxPopulation : ClippingPlanePopulation;
}

namespace ITwin
{
	TWeakObjectPtr<AITwinPopulationTool> ActivatePopulationTool(UWorld* World)
	{
		TWeakObjectPtr<AITwinPopulationTool> PopulationTool;
		for (TActorIterator<AITwinPopulationTool> It(World); It; ++It)
		{
			PopulationTool = *It;
			if (PopulationTool.IsValid())
				break;
		}
		if (PopulationTool.IsValid())
		{
			if (!PopulationTool->IsEnabled())
			{
				AITwinInteractiveTool::DisableAll(World);
				PopulationTool->SetEnabled(true);
			}
			PopulationTool->ResetToDefault();
		}
		return PopulationTool;
	}

	inline TWeakObjectPtr<AITwinSplineTool> GetSplineTool(UWorld* World)
	{
		TWeakObjectPtr<AITwinSplineTool> SplineTool;
		for (TActorIterator<AITwinSplineTool> It(World); It; ++It)
		{
			SplineTool = *It;
			if (SplineTool.IsValid())
				break;
		}
		return SplineTool;
	}

	TWeakObjectPtr<AITwinSplineTool> ActivateSplineToolForCutout(UWorld* World)
	{
		TWeakObjectPtr<AITwinSplineTool> SplineTool = GetSplineTool(World);
		if (SplineTool.IsValid())
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
				EnableSplineTool(World, true, EITwinSplineUsage::MapCutout, {}, true/*bAutomaticCutoutTarget*/);
			}
		}
		return SplineTool;
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
}

template <EITwinClippingPrimitiveType PrimitiveType>
bool AITwinClippingTool::TPreLoadClippingPrimitive(TWeakObjectPtr<AITwinPopulation>& ClippingPopulation,
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

uint32 AITwinClippingTool::PreLoadClippingPrimitives(AITwinPopulationTool& PopulationTool)
{
	uint32 NumPreloaded = 0;
	if (TPreLoadClippingPrimitive<EITwinClippingPrimitiveType::Box>(ClippingBoxPopulation, PopulationTool))
	{
		NumPreloaded++;
	}
	if (TPreLoadClippingPrimitive<EITwinClippingPrimitiveType::Plane>(ClippingPlanePopulation, PopulationTool))
	{
		NumPreloaded++;
	}
	return NumPreloaded;
}

template <EITwinClippingPrimitiveType PrimitiveType>
bool AITwinClippingTool::TStartInteractivePrimitiveInstanceCreation()
{
	using PrimitiveTraits = ClippingPrimitiveTrait<PrimitiveType>;

	if (NumEffects(PrimitiveType) >= PrimitiveTraits::MAX_PRIMITIVES)
	{
		// Internal limit reached for this primitive.
		return false;
	}

	UWorld* World = GetWorld();
	if (!ensure(World))
		return false;

	TWeakObjectPtr<AITwinPopulationTool> PopulationTool = ITwin::ActivatePopulationTool(World);
	if (PopulationTool.IsValid())
	{
		if (PopulationTool->IsInteractiveCreationMode())
		{
			// Do not accumulate the new effects (can happen if the user clicks several times the Add icon,
			// without validating the position of the new primitive).
			return false;
		}
		PopulationTool->ClearUsedAssets();
		PopulationTool->SetUsedAsset(TGetClippingAssetPath<PrimitiveType>(), true);
		// Ensure the new instance will be visible.
		SetEffectVisibility(PrimitiveType, true);
		return PopulationTool->StartInteractiveCreation();
	}
	else
	{
		return false;
	}
}

bool AITwinClippingTool::StartInteractiveEffectCreation(EITwinClippingPrimitiveType Type)
{
	// Make sure we hide all effect helpers (only the new item will be visible).
	HideAllEffectHelpers();

	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:
		return TStartInteractivePrimitiveInstanceCreation<EITwinClippingPrimitiveType::Box>();

	case EITwinClippingPrimitiveType::Plane:
		return TStartInteractivePrimitiveInstanceCreation<EITwinClippingPrimitiveType::Plane>();

	case EITwinClippingPrimitiveType::Polygon:
	{
		// Start interactive drawing.
		TWeakObjectPtr<AITwinSplineTool> SplineTool = ITwin::ActivateSplineToolForCutout(GetWorld());
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
bool AITwinClippingTool::TAddClippingPrimitive(TArray<PrimitiveInfo>& ClippingInfos, int32 InstanceIndex)
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

			UMaterialParameterCollectionInstance* MPCInstance = GetMPCCLippingInstance();
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

void AITwinClippingTool::OnClippingInstanceAdded(AITwinPopulation* Population, EITwinInstantiatedObjectType ObjectType, int32 InstanceIndex)
{
	bool bHasAddedClippingPrimitive = false;
	std::optional<EITwinClippingPrimitiveType> PrimitiveToUpdate;
	switch (ObjectType)
	{
	case EITwinInstantiatedObjectType::ClippingPlane:
	{
		ClippingPlanePopulation = Population;
		bHasAddedClippingPrimitive = TAddClippingPrimitive<FITwinClippingPlaneInfo, EITwinClippingPrimitiveType::Plane>(ClippingPlaneInfos, InstanceIndex);
		PrimitiveToUpdate = EITwinClippingPrimitiveType::Plane;
		break;
	}

	case EITwinInstantiatedObjectType::ClippingBox:
	{
		ClippingBoxPopulation = Population;
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
		// Create tile excluders in all registered tilesets.
		UpdateAllTilesets(PrimitiveToUpdate);

		EffectListModifiedEvent.Broadcast();
		if (ensure(PrimitiveToUpdate))
			EffectAddedEvent.Broadcast(*PrimitiveToUpdate, InstanceIndex);
	}
}

void AITwinClippingTool::UpdateAllTilesets(std::optional<EITwinClippingPrimitiveType> const& SpecificPrimitiveType /*= std::nullopt*/)
{
	ITwin::IterateAllITwinTilesets([this, SpecificPrimitiveType](FITwinTilesetAccess const& TilesetAccess)
	{
		UpdateTileset(TilesetAccess, SpecificPrimitiveType);
	}, GetWorld());
}

bool AITwinClippingTool::GetPlaneEquationFromUEInstance(FVector3f& OutPlaneOrientation, float& OutPlaneW, int32 InInstanceIndex) const
{
	if (!ensure(ClippingPlanePopulation.IsValid()))
		return false;

	if (InInstanceIndex >= ClippingPlanePopulation->GetNumberOfInstances())
		return false;

	const FTransform InstanceTransform = ClippingPlanePopulation->GetInstanceTransform(InInstanceIndex);

	auto const PositionUE = InstanceTransform.GetLocation();
	auto const PlaneOrientationUE = InstanceTransform.GetUnitAxis(EAxis::Z); // GetUpVector
	OutPlaneOrientation = FVector3f(PlaneOrientationUE);
	OutPlaneW = static_cast<float>(PositionUE.Dot(PlaneOrientationUE));
	return true;
}

bool AITwinClippingTool::UpdateClippingPlaneEquationFromUEInstance(int32 InstanceIndex)
{
	if (!ensure(ClippingPlanePopulation.IsValid()))
		return false;
	if (!ensure(InstanceIndex < ClippingPlaneInfos.Num()))
		return false;

	FVector3f PlaneOrientation = FVector3f::ZAxisVector;
	float PlaneW(0.f);
	if (!GetPlaneEquationFromUEInstance(PlaneOrientation, PlaneW, InstanceIndex))
		return false;

	const int32 PlaneIndex = InstanceIndex;
	auto const& PlaneInfo = ClippingPlaneInfos[PlaneIndex];

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
	UMaterialParameterCollectionInstance* MPCInstance = GetMPCCLippingInstance();
	if (ensure(MPCInstance))
	{
		const FLinearColor PlaneEquationAsColor =
		{
			PlaneOrientation.X,
			PlaneOrientation.Y,
			PlaneOrientation.Z,
			PlaneW
		};
		bIsClippingReady = MPCInstance->SetVectorParameterValue(
			FName(fmt::format("PlaneEquation_{}", PlaneIndex).c_str()),
			PlaneEquationAsColor);

		ensure(bIsClippingReady);
	}
	return bIsClippingReady;
}

bool AITwinClippingTool::UpdateClippingPrimitiveFromUEInstance(EITwinClippingPrimitiveType Type, int32 InstanceIndex)
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
void AITwinClippingTool::TUpdateAllClippingPrimitives(TArray<ClippingPrimitiveInfo>& ClippingInfos)
{
	auto const& PopulationPtr = GetClippingEffectPopulation(PrimitiveType);
	if (!ensure(PopulationPtr.IsValid()))
		return;
	const AITwinPopulation& ClippingPopulation = *PopulationPtr;
	const int32 NumPrims = ClippingPopulation.GetNumberOfInstances();

	UMaterialParameterCollectionInstance* MPCInstance = GetMPCCLippingInstance();
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

void AITwinClippingTool::UpdateAllClippingPlanes()
{
	TUpdateAllClippingPrimitives<FITwinClippingPlaneInfo, EITwinClippingPrimitiveType::Plane>(ClippingPlaneInfos);
}

void AITwinClippingTool::UpdateAllClippingBoxes()
{
	TUpdateAllClippingPrimitives<FITwinClippingBoxInfo, EITwinClippingPrimitiveType::Box>(ClippingBoxInfos);
}

bool AITwinClippingTool::GetBoxTransformInfoFromUEInstance(glm::dmat3x3& OutMatrix, glm::dvec3& OutTranslation, int32 InInstanceIndex) const
{
	if (!ensure(ClippingBoxPopulation.IsValid()))
		return false;

	if (InInstanceIndex >= ClippingBoxPopulation->GetNumberOfInstances())
		return false;

	const FTransform InstanceTransform = ClippingBoxPopulation->GetInstanceTransform(InInstanceIndex);

	double MasterMeshScale = 1.0;
	// Take the master object's scale into account (depends on the way the box was imported
	// in Unreal...)
	const FBox MasterMeshBox = ClippingBoxPopulation->GetMasterMeshBoundingBox();
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

bool AITwinClippingTool::UpdateClippingBoxFromUEInstance(int32 InstanceIndex)
{
	if (!ensure(InstanceIndex < ClippingBoxInfos.Num()))
		return false;
	const int32 BoxIndex = InstanceIndex;
	FITwinClippingBoxInfo& BoxInfo = ClippingBoxInfos[BoxIndex];

	glm::dmat3x3 BoxMatrix;
	glm::dvec3 BoxTranslation;
	if (!GetBoxTransformInfoFromUEInstance(BoxMatrix, BoxTranslation, InstanceIndex))
		return false;

	// Update the box information shared by all tile excluders activating this box.
	BoxInfo.CalcBoxBounds(BoxMatrix, BoxTranslation);

	bool bIsClippingReady = false;
	UMaterialParameterCollectionInstance* MPCInstance = GetMPCCLippingInstance();
	if (ensure(MPCInstance))
	{
		// For performance reasons, we store the inverse matrix.
		glm::dmat3x3 const InverseMatrix = glm::inverse(BoxMatrix);
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

void AITwinClippingTool::OnClippingInstanceModified(EITwinInstantiatedObjectType ObjectType, int32 InstanceIndex)
{
	switch (ObjectType)
	{
	case EITwinInstantiatedObjectType::ClippingPlane:
		if (InstanceIndex < ClippingPlaneInfos.Num())
		{
			UpdateClippingPlaneEquationFromUEInstance(InstanceIndex);
		}
		break;
	case EITwinInstantiatedObjectType::ClippingBox:
		if (InstanceIndex < ClippingBoxInfos.Num())
		{
			UpdateClippingBoxFromUEInstance(InstanceIndex);
		}
		break;
	default:
		// Nothing to do for other types.
		return;
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
			UpdateAllClippingPlanes();
			bEffectListModified = true;
		}
		break;
	case EITwinInstantiatedObjectType::ClippingBox:
		if (!InstanceIndices.IsEmpty())
		{
			// Recreate all boxes from remaining instances.
			UpdateAllClippingBoxes();
			bEffectListModified = true;
		}
		break;
	default:
		// Nothing to do for other types.
		return;
	}
	if (bEffectListModified)
	{
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
			UpdateAllClippingPlanes();
		}
		break;
	case EITwinInstantiatedObjectType::ClippingBox:
		if (NumInstances > 0)
		{
			ClippingBoxPopulation = Population;
			UpdateAllClippingBoxes();
		}
		break;
	default:
		// Nothing to do for other types.
		return;
	}
}

bool AITwinClippingTool::RegisterCutoutSpline(AITwinSplineHelper* SplineHelper)
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
		}
		EffectListModifiedEvent.Broadcast();
		EffectAddedEvent.Broadcast(EITwinClippingPrimitiveType::Polygon, ClippingPolygonInfos.Num() - 1);
		return true;
	}
	else
	{
		return false;
	}
}

void AITwinClippingTool::UpdatePolygonInfosFromScene()
{
	// Rebuild the array of cutout polygon infos from current world.
	ClippingPolygonInfos.Reset();
	for (TActorIterator<AITwinSplineHelper> SplineIter(GetWorld()); SplineIter; ++SplineIter)
	{
		RegisterCutoutSpline(*SplineIter);
	}
}

void AITwinClippingTool::OnSplineHelperAdded(AITwinSplineHelper* NewSpline)
{
	RegisterCutoutSpline(NewSpline);
}

void AITwinClippingTool::OnSplineHelperRemoved(AITwinSplineHelper* SplineBeingRemoved)
{
	if (SplineBeingRemoved
		&& SplineBeingRemoved->GetUsage() == EITwinSplineUsage::MapCutout)
	{
		int32 Index = ClippingPolygonInfos.IndexOfByPredicate(
			[SplineBeingRemoved](FITwinClippingCartographicPolygonInfo const& InItem)
		{
			return (InItem.SplineHelper == SplineBeingRemoved);
		});
		if (Index != INDEX_NONE)
		{
			ClippingPolygonInfos.RemoveAt(Index);

			const bool bTriggeredFromITS = Impl->RemovalInitiatorOpt
				&& *Impl->RemovalInitiatorOpt == FImpl::ERemovalInitiator::ITS;
			EffectRemovedEvent.Broadcast(EITwinClippingPrimitiveType::Polygon, Index, bTriggeredFromITS);
			EffectListModifiedEvent.Broadcast();
		}
	}
}

int32 AITwinClippingTool::NumEffects(EITwinClippingPrimitiveType Type) const
{
	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:		return ClippingBoxInfos.Num();
	case EITwinClippingPrimitiveType::Plane:	return ClippingPlaneInfos.Num();
	case EITwinClippingPrimitiveType::Polygon:	return ClippingPolygonInfos.Num();

	BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(case EITwinClippingPrimitiveType::Count: , 0);
	}
}

FITwinClippingInfoBase& AITwinClippingTool::GetMutableClippingEffect(EITwinClippingPrimitiveType Type, int32 Index)
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

const FITwinClippingInfoBase& AITwinClippingTool::GetClippingEffect(EITwinClippingPrimitiveType Type, int32 Index) const
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

namespace ITwin
{
	extern int32 GetLinkedTilesets(
		AITwinSplineTool::TilesetAccessArray& OutArray,
		std::shared_ptr<AdvViz::SDK::ISpline> const& Spline,
		const UWorld* World);


	void RemoveSpline(AITwinSplineHelper* SplineHelper, UWorld* World)
	{
		TWeakObjectPtr<AITwinSplineTool> SplineTool = GetSplineTool(World);
		if (SplineTool.IsValid())
			SplineTool->DeleteSpline(SplineHelper);
	}

	void SelectSpline(AITwinSplineHelper* SplineHelper, UWorld* World)
	{
		TWeakObjectPtr<AITwinSplineTool> SplineTool = GetSplineTool(World);
		if (!SplineTool.IsValid())
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

	void SelectPopulationInstance(AITwinPopulation* Population, int32 InstanceIndex, UWorld* World)
	{
		TWeakObjectPtr<AITwinPopulationTool> PopulationTool = ITwin::ActivatePopulationTool(World);
		if (PopulationTool.IsValid())
		{
			PopulationTool->SetSelectedPopulation(Population);
			PopulationTool->SetSelectedInstanceIndex(InstanceIndex);
			PopulationTool->SelectionChangedEvent.Broadcast();
		}
	}

	AITwinPopulation const* GetSelectedPopulation(int32& OutSelectedInstanceIndex, UWorld* World)
	{
		AITwinPopulation const* SelectedPopulation = nullptr;
		OutSelectedInstanceIndex = INDEX_NONE;

		TWeakObjectPtr<AITwinPopulationTool> PopulationTool;
		for (TActorIterator<AITwinPopulationTool> It(World); It; ++It)
		{
			PopulationTool = *It;
			if (PopulationTool.IsValid())
				break;
		}
		if (PopulationTool.IsValid())
		{
			SelectedPopulation = PopulationTool->GetSelectedPopulation();
			if (SelectedPopulation)
			{
				OutSelectedInstanceIndex = PopulationTool->GetSelectedInstanceIndex();
			}
		}
		return SelectedPopulation;
	}
}

bool AITwinClippingTool::RemoveEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bTriggeredFromITS)
{
	if (!ensure(PrimitiveIndex >= 0 && PrimitiveIndex < NumEffects(Type)))
		return false;

	FImpl::FScopedRemovalContext RemovalCtx(*Impl,
		bTriggeredFromITS ? FImpl::ERemovalInitiator::ITS : FImpl::ERemovalInitiator::Unreal);

	// Select the cutout if needed (for undo/redo) - in general, the effect is already selected since this
	// event is triggered from iTS cutout properties page.
	auto CurrentSelection = GetSelectedEffect();
	if (!CurrentSelection
		|| CurrentSelection->first != Type
		|| CurrentSelection->second != PrimitiveIndex)
	{
		SelectEffect(Type, PrimitiveIndex);
	}
	RemoveEffectStartedEvent.Broadcast();

	const int32 NumPrimsOld = NumEffects(Type);
	// Remark: for box and plane, the removal of the entry from the info array will be indirect, through a
	// call to #OnClippingInstancesRemoved (see AITwinPopulation::RemoveInstance).
	// Hence, to know if the removal succeeded, we just check the count of primitives at the end.
	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:
	case EITwinClippingPrimitiveType::Plane:
	{
		auto const& Population = GetClippingEffectPopulation(Type);
		if (Population.IsValid())
		{
			Population->RemoveInstance(PrimitiveIndex);
		}
		break;
	}

	case EITwinClippingPrimitiveType::Polygon:
	{
		auto const& PolygonInfo = ClippingPolygonInfos[PrimitiveIndex];
		if (PolygonInfo.SplineHelper.IsValid())
		{
			ITwin::RemoveSpline(PolygonInfo.SplineHelper.Get(), GetWorld());
		}
		break;
	}

	BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(case EITwinClippingPrimitiveType::Count: , false);
	}

	const bool bRemoved = (NumEffects(Type) == NumPrimsOld - 1);
	if (bRemoved)
	{
		RemoveEffectCompletedEvent.Broadcast();
	}

	return bRemoved;
}


template <typename PrimitiveInfo, EITwinClippingPrimitiveType PrimitiveType>
bool AITwinClippingTool::TEncodeFlippingInMPC(TArray<PrimitiveInfo> const& ClippingInfos)
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
	UMaterialParameterCollectionInstance* MPCInstance = GetMPCCLippingInstance();
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

bool AITwinClippingTool::EncodeFlippingInMPC(EITwinClippingPrimitiveType Type)
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

bool AITwinClippingTool::GetInvertEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex) const
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

void AITwinClippingTool::SetInvertEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bInvert)
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

void AITwinClippingTool::FlipEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex)
{
	SetInvertEffect(Type, PrimitiveIndex, !GetInvertEffect(Type, PrimitiveIndex));
}

void AITwinClippingTool::SelectEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex)
{
	if (!ensure(PrimitiveIndex >= 0 && PrimitiveIndex < NumEffects(Type)))
		return;

	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:
	case EITwinClippingPrimitiveType::Plane:
	{
		auto const& Population = GetClippingEffectPopulation(Type);
		if (Population.IsValid())
		{
			ITwin::SelectPopulationInstance(Population.Get(), PrimitiveIndex, GetWorld());
		}
		break;
	}

	case EITwinClippingPrimitiveType::Polygon:
	{
		auto const& PolygonInfo = ClippingPolygonInfos[PrimitiveIndex];
		if (PolygonInfo.SplineHelper.IsValid())
		{
			ITwin::SelectSpline(PolygonInfo.SplineHelper.Get(), GetWorld());
		}
		break;
	}

	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinClippingPrimitiveType::Count: );
	}
}

std::optional<AITwinClippingTool::FEffectIdentifier> AITwinClippingTool::GetSelectedEffect() const
{
	// Recover the selected effect from the population tool or spline tool.

	// First test the population tool:
	int32 InstanceIndex(INDEX_NONE);
	AITwinPopulation const* SelectedPopulation = ITwin::GetSelectedPopulation(InstanceIndex, GetWorld());
	if (SelectedPopulation)
	{
		if (SelectedPopulation == ClippingBoxPopulation.Get())
			return std::make_pair(EITwinClippingPrimitiveType::Box, InstanceIndex);
		if (SelectedPopulation == ClippingPlanePopulation.Get())
			return std::make_pair(EITwinClippingPrimitiveType::Plane, InstanceIndex);
	}
	// Then the spline tool:
	AITwinSplineHelper const* SelectedSpline = nullptr;
	TWeakObjectPtr<AITwinSplineTool> SplineTool = ITwin::GetSplineTool(GetWorld());
	if (SplineTool.IsValid() && SplineTool->GetUsage() == EITwinSplineUsage::MapCutout)
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

void AITwinClippingTool::DeSelectAll()
{
	auto CurrentSelection = GetSelectedEffect();
	if (CurrentSelection)
	{
		if (CurrentSelection->first == EITwinClippingPrimitiveType::Box
			|| CurrentSelection->first == EITwinClippingPrimitiveType::Plane)
		{
			ITwin::SelectPopulationInstance(nullptr, INDEX_NONE, GetWorld());
		}
		else
		{
			ITwin::SelectSpline(nullptr, GetWorld());
		}
	}
}

int32 AITwinClippingTool::GetSelectedPolygonPointInfo(double& OutLatitude, double& OutLongitude) const
{
	UWorld* World = GetWorld();
	if (!World)
		return INDEX_NONE;

	auto CurrentSelection = GetSelectedEffect();
	if (CurrentSelection && CurrentSelection->first == EITwinClippingPrimitiveType::Polygon)
	{
		TWeakObjectPtr<AITwinSplineTool> SplineTool = ITwin::GetSplineTool(World);
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

void AITwinClippingTool::SetPolygonPointLocation(int32 PolygonIndex, int32 PointIndex, double Latitude, double Longitude) const
{
	UWorld* World = GetWorld();
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
			TWeakObjectPtr<AITwinSplineTool> SplineTool = ITwin::GetSplineTool(World);
			if (SplineTool.IsValid()
				&& EditedSpline == SplineTool->GetSelectedSpline()
				&& PointIndex == SplineTool->GetSelectedPointIndex())
			{
				SplineTool->SplinePointMovedEvent.Broadcast(true /*bMovedInITS*/);
			}
		}
	}
}


void AITwinClippingTool::ZoomOnEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex)
{
	if (!ensure(PrimitiveIndex >= 0 && PrimitiveIndex < NumEffects(Type)))
		return;

	FBox FocusBBox;
	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:
	case EITwinClippingPrimitiveType::Plane:
	{
		auto const& Population = GetClippingEffectPopulation(Type);
		if (Population.IsValid())
		{
			FocusBBox = Population->GetInstanceBoundingBox(PrimitiveIndex);
		}
		break;
	}

	case EITwinClippingPrimitiveType::Polygon:
	{
		auto const& PolygonInfo = ClippingPolygonInfos[PrimitiveIndex];
		if (PolygonInfo.SplineHelper.IsValid())
		{
			PolygonInfo.SplineHelper->IncludeInWorldBox(FocusBBox);
		}
		break;
	}

	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinClippingPrimitiveType::Count:);
	}
	if (FocusBBox.IsValid)
	{
		UITwinUtilityLibrary::ZoomOn(FocusBBox, GetWorld() /*, MinDistanceToCenter*/);
	}
}


void AITwinClippingTool::SetEffectVisibility(EITwinClippingPrimitiveType EffectType, bool bVisibleInGame)
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
		for (TActorIterator<AITwinSplineHelper> SplineIter(GetWorld()); SplineIter; ++SplineIter)
		{
			if ((*SplineIter)->GetUsage() == EITwinSplineUsage::MapCutout)
				(*SplineIter)->SetActorHiddenInGame(!bVisibleInGame);
		}
		break;
	}

	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(case EITwinClippingPrimitiveType::Count: );
	}
}

void AITwinClippingTool::SetAllEffectHelpersVisibility(bool bVisibleInGame)
{
	SetEffectVisibility(EITwinClippingPrimitiveType::Box, bVisibleInGame);
	SetEffectVisibility(EITwinClippingPrimitiveType::Plane, bVisibleInGame);
	SetEffectVisibility(EITwinClippingPrimitiveType::Polygon, bVisibleInGame);
}

void AITwinClippingTool::OnActivatePicking(bool bActivate)
{
	SetAllEffectHelpersVisibility(bActivate);
}

bool AITwinClippingTool::DoMouseClickPicking(bool& bOutSelectionGizmoNeeded)
{
	bool bRelevantAction = false;
	bOutSelectionGizmoNeeded = false;
	UWorld* World = GetWorld();
	if (!World)
		return false;
	// Test population then cut-out splines.

	// Note that we can only have one active tool at a time, but we don't want the cutout splines to be
	// hidden just because we temporarily disable the spline tool...
	AITwinSplineTool::FAutomaticVisibilityDisabler AutoVisDisabler;

	if (ClippingBoxInfos.Num() + ClippingPlaneInfos.Num() > 0)
	{
		TWeakObjectPtr<AITwinPopulationTool> PopulationTool = ITwin::ActivatePopulationTool(World);
		if (PopulationTool.IsValid())
		{
			AITwinPopulationTool::FPickingContext RestrictOnClipping(*PopulationTool, true);
			bRelevantAction = PopulationTool->DoMouseClickAction();
			if (bRelevantAction)
				bOutSelectionGizmoNeeded = PopulationTool->HasSelectedPopulation();
		}
	}
	if (!bRelevantAction && ClippingPolygonInfos.Num() > 0)
	{
		TWeakObjectPtr<AITwinSplineTool> SplineTool = ITwin::ActivateSplineToolForCutout(World);
		if (SplineTool.IsValid())
		{
			bRelevantAction = SplineTool->DoMouseClickAction();
			if (bRelevantAction)
				bOutSelectionGizmoNeeded = SplineTool->HasSelection();
		}
	}
	if (bRelevantAction)
	{
		// Notify new selection.
		auto const NewSelection = GetSelectedEffect();
		if (NewSelection)
		{
			EffectSelectedEvent.Broadcast(NewSelection->first, NewSelection->second);
		}
	}
	return bRelevantAction;
}

void AITwinClippingTool::OnOverviewCamera()
{
	UWorld* World = GetWorld();
	if (!World)
		return;
	TWeakObjectPtr<AITwinSplineTool> SplineTool = ITwin::ActivateSplineToolForCutout(World);
	if (SplineTool.IsValid())
	{
		SplineTool->OnOverviewCamera();
	}
}

template <typename Func>
void AITwinClippingTool::VisitClippingPrimitivesOfType(EITwinClippingPrimitiveType Type, Func const& Fun)
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

void AITwinClippingTool::ActivateEffects(EITwinClippingPrimitiveType Type, EITwinClippingEffectLevel Level, bool bActivate)
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
		UMaterialParameterCollectionInstance* MPCInstance = GetMPCCLippingInstance();
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

void AITwinClippingTool::ActivateEffectsAllLevels(EITwinClippingPrimitiveType Type, bool bActivate)
{
	ActivateEffects(Type, EITwinClippingEffectLevel::Tileset,	bActivate);
	ActivateEffects(Type, EITwinClippingEffectLevel::Shader,	bActivate);
}

#endif // WITH_EDITOR


bool AITwinClippingTool::IsEffectEnabled(EITwinClippingPrimitiveType EffectType, int32 Index) const
{
	if (ensure(Index < NumEffects(EffectType)))
	{
		return GetClippingEffect(EffectType, Index).IsEnabled();
	}
	return false;
}

void AITwinClippingTool::EnableEffect(EITwinClippingPrimitiveType EffectType, int32 Index, bool bInEnabled)
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

bool AITwinClippingTool::ShouldEffectInfluenceModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
	const ITwin::ModelLink& ModelIdentifier) const
{
	if (ensure(EffectIndex < NumEffects(EffectType)))
	{
		return GetClippingEffect(EffectType, EffectIndex).ShouldInfluenceModel(ModelIdentifier);
	}
	return false;
}

bool AITwinClippingTool::ShouldEffectInfluenceFullModelType(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
	EITwinModelType ModelType) const
{
	if (ensure(EffectIndex < NumEffects(EffectType)))
	{
		return GetClippingEffect(EffectType, EffectIndex).ShouldInfluenceFullModelType(ModelType);
	}
	return false;
}

void AITwinClippingTool::SetEffectInfluenceFullModelType(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
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

void AITwinClippingTool::SetEffectInfluenceSpecificModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
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

AdvViz::SDK::RefID AITwinClippingTool::GetEffectId(EITwinClippingPrimitiveType EffectType, int32 EffectIndex) const
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

int32 AITwinClippingTool::GetEffectIndex(EITwinClippingPrimitiveType EffectType, AdvViz::SDK::RefID const& RefID) const
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

	void ConfigureNewInstance(AdvViz::SDK::IInstance& AVizInstance, AActor& HitActor)
	{
		// Determine the target layer type, if we have hit a tileset owner:
		auto TilesetAccess = GetTilesetAccess(&HitActor);
		if (!TilesetAccess)
			return;
		const EITwinModelType HitModelType = TilesetAccess->GetDecorationKey().first;

		FITwinClippingInfoBase ClippingProps;
		for (EITwinModelType ModelType : { EITwinModelType::IModel,
			EITwinModelType::RealityData,
			EITwinModelType::GlobalMapLayer })
		{
			ClippingProps.SetInfluenceFullModelType(ModelType, HitModelType == ModelType);
		}
		const std::string EncodedCutoutInfo = EncodeProperties(ClippingProps);
		if (EncodedCutoutInfo != AVizInstance.GetName())
		{
			AVizInstance.SetName(EncodedCutoutInfo);
			AVizInstance.SetShouldSave(true);
		}
	}
}

void AITwinClippingTool::UpdateAVizInstanceProperties(EITwinClippingPrimitiveType Type, int32 InstanceIndex) const
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
			if (EncodedInfo != AVizInstance->GetName())
			{
				AVizInstance->SetName(EncodedInfo);
				AVizInstance->SetShouldSave(true);
			}
		}
	}
}

void AITwinClippingTool::UpdateClippingPropertiesFromAVizInstance(EITwinClippingPrimitiveType Type, int32 InstanceIndex)
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
			ITwin::Clipping::DecodeProperties(
				AVizInstance->GetName(),
				GetMutableClippingEffect(Type, InstanceIndex));
		}
	}
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
			FlipEffectsOfType(EITwinClippingPrimitiveType::Box);
			FlipEffectsOfType(EITwinClippingPrimitiveType::Plane);
			FlipEffectsOfType(EITwinClippingPrimitiveType::Polygon);
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
