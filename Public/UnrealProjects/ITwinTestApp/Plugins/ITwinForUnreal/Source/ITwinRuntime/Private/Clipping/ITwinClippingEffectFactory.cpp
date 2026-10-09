/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingEffectFactory.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Clipping/ITwinClippingEffectFactory.h>

#include <Clipping/ITwinClippingEffectManager.h>
#include <Clipping/ITwinClippingEffectManager.inl>
#include <Clipping/ITwinClippingEventHub.h>
#include <Clipping/ITwinClippingInfoBase.inl>
#include <Clipping/ITwinClippingPersistence.h>
#include <Clipping/ITwinClippingToolUtils.inl>
#include <Clipping/ITwinPlaneTileExcluder.h>

#include <Population/ITwinPopulation.h>
#include <Population/ITwinPopulationTool.h>
#include <Spline/ITwinSplineHelper.h>
#include <Spline/ITwinSplineTool.h>


UITwinClippingEffectFactory::UITwinClippingEffectFactory()
{

}

void UITwinClippingEffectFactory::Connect(AITwinClippingEventHub* InEventHub,
	UITwinClippingEffectManager* InEffectManager,
	UITwinClippingPersistence* InPersistence)
{
	SetEventHub(InEventHub);
	EffectManager = InEffectManager;
	Persistence = InPersistence;
}

void UITwinClippingEffectFactory::SetEventHub(AITwinClippingEventHub* InEventHub)
{
	EventHub = InEventHub;
}

inline
bool UITwinClippingEffectFactory::IsValidEffectIndex(EITwinClippingPrimitiveType EffectType, int32 Index) const
{
	return EffectManager.IsValid()
		&& EffectManager->IsValidEffectIndex(EffectType, Index);
}

namespace
{
	template <EITwinClippingPrimitiveType T>
	struct TClippingPrimitiveTrait
	{

	};

	template <>
	struct TClippingPrimitiveTrait<EITwinClippingPrimitiveType::Plane>
	{
		static constexpr int32 MAX_PRIMITIVES = ITwin::MAX_CLIPPING_PLANES;
		static constexpr const TCHAR* PopulationAssetName = TEXT("ClippingPlane");
	};

	template <>
	struct TClippingPrimitiveTrait<EITwinClippingPrimitiveType::Box>
	{
		static constexpr int32 MAX_PRIMITIVES = ITwin::MAX_CLIPPING_BOXES;
		static constexpr const TCHAR* PopulationAssetName = TEXT("ClippingBox");
	};

	template <EITwinClippingPrimitiveType PrimitiveType>
	FString TGetClippingAssetPath()
	{
		using PrimitiveTraits = TClippingPrimitiveTrait<PrimitiveType>;
		return FString::Printf(TEXT("/Game/Clipping/Clipping/%s"), PrimitiveTraits::PopulationAssetName);
	}


	static inline EITwinClippingPrimitiveType ToClippingType(EITwinInstantiatedObjectType ObjectType)
	{
		switch (ObjectType)
		{
		case EITwinInstantiatedObjectType::ClippingBox: return EITwinClippingPrimitiveType::Box;
		case EITwinInstantiatedObjectType::ClippingPlane: return EITwinClippingPrimitiveType::Plane;
		default:
			BE_ISSUE("not a clipping object type", static_cast<int>(ObjectType));
			return EITwinClippingPrimitiveType::Count;
		}
	}
}

/*static*/ EITwinClippingPrimitiveType UITwinClippingEffectFactory::GetEffectType(EITwinInstantiatedObjectType ObjectType)
{
	return ToClippingType(ObjectType);
}

namespace ITwin
{
	FString GetCutoutAssetPath(EITwinClippingPrimitiveType Type)
	{
		switch (Type)
		{
		case EITwinClippingPrimitiveType::Box:
			return TGetClippingAssetPath<EITwinClippingPrimitiveType::Box>();
		case EITwinClippingPrimitiveType::Plane:
			return TGetClippingAssetPath<EITwinClippingPrimitiveType::Plane>();

		BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(
		case EITwinClippingPrimitiveType::Polygon:
		case EITwinClippingPrimitiveType::Count:
			, FString());
		}
	}
}

template <EITwinClippingPrimitiveType PrimitiveType>
bool UITwinClippingEffectFactory::TStartInteractivePrimitiveInstanceCreation()
{
	using PrimitiveTraits = TClippingPrimitiveTrait<PrimitiveType>;
	if (!ensure(EffectManager.IsValid()))
	{
		return false;
	}

	if (EffectManager->NumEffects(PrimitiveType) >= PrimitiveTraits::MAX_PRIMITIVES)
	{
		// Internal limit reached for this primitive.
		return false;
	}

	auto const& PopulationTool = EffectManager->ActivatePopulationTool();
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

		return PopulationTool->StartInteractiveCreation();
	}
	else
	{
		return false;
	}
}

bool UITwinClippingEffectFactory::StartInteractiveEffectCreation(EITwinClippingPrimitiveType Type)
{
	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:
		return TStartInteractivePrimitiveInstanceCreation<EITwinClippingPrimitiveType::Box>();

	case EITwinClippingPrimitiveType::Plane:
		return TStartInteractivePrimitiveInstanceCreation<EITwinClippingPrimitiveType::Plane>();

	case EITwinClippingPrimitiveType::Polygon:
	{
		// Start interactive drawing.
		TWeakObjectPtr<AITwinSplineTool> SplineTool = EffectManager->ActivateSplineTool();
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

	BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(case EITwinClippingPrimitiveType::Count:, false);
	}
}


namespace ITwin::Clipping
{
	static constexpr auto INFL_PREFIX = "infl";
	static constexpr auto PER_LAYER_PREFIX = "perlayer";


	// Encode cutout properties as a string (temporary solution for persistence, as long as we do not save
	// clipping shapes in SceneAPI (nor population instances in another iTwin service...)
	std::string EncodeProperties(const FITwinClippingInfoBase& Prop)
	{
		std::stringstream EncodedInfo;
		EncodedInfo << "clipping (";
		if (!Prop.IsEnabled())
			EncodedInfo << "OFF-";
		if (Prop.GetInvertEffect())
			EncodedInfo << "inv-";
		for (EITwinModelType ModelType : { EITwinModelType::IModel,
			EITwinModelType::RealityData,
			EITwinModelType::GlobalMapLayer })
		{
			FITwinClippingInfluenceInfo const& InfluenceInfo = Prop.GetInfluenceInfo(ModelType);
			if (InfluenceInfo.bInfluenceAll)
			{
				EncodedInfo << INFL_PREFIX << static_cast<int>(ModelType) << "-";
			}
			else if (!InfluenceInfo.SpecificIDs.IsEmpty())
			{
				EncodedInfo << PER_LAYER_PREFIX << static_cast<int>(ModelType) << "[";
				for (FString const& LayerID : InfluenceInfo.SpecificIDs)
				{
					EncodedInfo << TCHAR_TO_ANSI(*LayerID) << ",";
				}
				EncodedInfo << "]-";
			}
		}
		EncodedInfo << ")";
		return EncodedInfo.str();
	}

	bool DecodeProperties(const std::string& EncodedInfo, FITwinClippingInfoBase& Prop)
	{
		if (!EncodedInfo.starts_with("clipping"))
		{
			return false;
		}
		Prop.SetEnabled(EncodedInfo.find("OFF-") == std::string::npos);
		Prop.SetInvertEffect(EncodedInfo.find("inv-") != std::string::npos);

		auto const ParseSpecificLayers = [&](EITwinModelType ModelType) -> int32
		{
			const std::string strModelTypeIndex = std::to_string(static_cast<int>(ModelType));
			const std::string strPerLayerInfl = PER_LAYER_PREFIX + strModelTypeIndex + "[";
			const size_t pos = EncodedInfo.find(strPerLayerInfl);
			if (pos == std::string::npos)
			{
				return 0;
			}
			size_t startPos = pos + strPerLayerInfl.size();
			size_t endPos = EncodedInfo.find("]", startPos);
			if (endPos == std::string::npos)
			{
				BE_ISSUE("failed parsing specific layer list from", EncodedInfo);
				return 0;
			}
			FString const LayerList = ANSI_TO_TCHAR(EncodedInfo.substr(startPos, endPos - startPos).c_str());
			TArray<FString> LayerIDs;
			LayerList.ParseIntoArray(LayerIDs, TEXT(","), true);
			for (FString const& LayerID : LayerIDs)
			{
				Prop.SetInfluenceSpecificModel(std::make_pair(ModelType, LayerID), true);
			}
			return LayerIDs.Num();
		};

		for (EITwinModelType ModelType : { EITwinModelType::IModel,
			EITwinModelType::RealityData,
			EITwinModelType::GlobalMapLayer })
		{
			const std::string strModelTypeIndex = std::to_string(static_cast<uint8_t>(ModelType));
			const std::string strInfl = INFL_PREFIX + strModelTypeIndex + "-";
			const bool bInfluenceAll = EncodedInfo.find(strInfl) != std::string::npos;
			Prop.SetInfluenceFullModelType(ModelType, bInfluenceAll);

			if (!bInfluenceAll)
			{
				// Parse specific layers.
				ParseSpecificLayers(ModelType);
			}
		}
		return true;
	}

	void ConfigureNewInstance(const AdvViz::SDK::IInstancePtr& AVizInstance, AActor& HitActor)
	{
		// Determine the target layer type, if we have hit a tileset owner:
		auto TilesetAccess = GetTilesetAccess(&HitActor);
		if (!TilesetAccess)
			return;
		// We now use per layer influence.
		const ITwin::ModelLink HitLayer = TilesetAccess->GetModelLink();
		FITwinClippingInfoBase ClippingProps;
		ClippingProps.SetInfluenceSpecificModel(HitLayer, true);

		const std::string EncodedCutoutInfo = EncodeProperties(ClippingProps);
		auto inst = AVizInstance->GetAutoLock();
		if (EncodedCutoutInfo != inst->GetName())
		{
			inst->SetName(EncodedCutoutInfo);
			inst->SetShouldSave(true);
		}
	}

	void ConfigureNewInstanceAsDisabled(const AdvViz::SDK::IInstancePtr& AVizInstance)
	{
		FITwinClippingInfoBase ClippingProps;
		ClippingProps.SetEnabled(false);
		const std::string EncodedCutoutInfo = EncodeProperties(ClippingProps);
		auto inst = AVizInstance->GetAutoLock();
		inst->SetName(EncodedCutoutInfo);
	}
}

void UITwinClippingEffectFactory::StorePropertiesInAVizInstance(EITwinClippingPrimitiveType Type, int32 InstanceIndex) const
{
	if (!ensure(IsValidEffectIndex(Type, InstanceIndex)))
	{
		return;
	}
	auto const& Population = EffectManager->GetPopulation(Type);
	if (Population.IsValid())
	{
		auto AVizInstance = Population->GetAVizInstance(InstanceIndex);

		const FITwinClippingInfoBase& Prop = EffectManager->GetEffect(Type, InstanceIndex);

		if (ensure(AVizInstance))
		{
			// Encode our properties in the instance name. This is no longer used for persistence - which now
			// uses the Scene API -, but only for undo/redo system in a session.
			const std::string EncodedInfo = ITwin::Clipping::EncodeProperties(Prop);
			auto inst = AVizInstance->GetAutoLock();
			if (EncodedInfo != inst->GetName())
			{
				inst->SetName(EncodedInfo);
			}
		}
	}
}

void UITwinClippingEffectFactory::UpdatePropertiesFromAVizInstance(EITwinClippingPrimitiveType Type, int32 InstanceIndex)
{
	if (!ensure(IsValidEffectIndex(Type, InstanceIndex)))
	{
		return;
	}
	// Decode properties from instance name - we used to have a method doing the opposite (blame here to find
	// 'UpdateAVizInstanceProperties'), but it was removed when the persistence was moved to SceneAPI.
	// We keep this method for compatibility with old scenes, and also for cutout interactive creation (see
	// #ConfigureNewInstance, #ConfigureNewInstanceAsDisabled), and undo system.
	// Note that 'UpdateAVizInstanceProperties' was reborn as 'StoreClippingPropertiesInAVizInstance', to
	// fix undo case (see ADO#2110376).
	auto const& Population = EffectManager->GetPopulation(Type);
	if (Population.IsValid())
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
				EffectManager->GetMutableEffect(Type, InstanceIndex));
		}
	}
}

template <typename PrimitiveInfo, EITwinClippingPrimitiveType PrimitiveType>
bool UITwinClippingEffectFactory::TAddEffectFromInstance(TArray<PrimitiveInfo>& ClippingInfos, int32 InstanceIndex)
{
	using PrimitiveTraits = TClippingPrimitiveTrait<PrimitiveType>;

	if (!ensure(EffectManager.IsValid()))
	{
		return false;
	}

	bool bHasAddedClippingPrimitive = false;

	if (InstanceIndex < PrimitiveTraits::MAX_PRIMITIVES)
	{
		auto const& PopulationTool = EffectManager->GetPopulationTool();

		bool const bIsInteractiveCreation = PopulationTool.IsValid()
			&& PopulationTool->IsInteractiveCreationMode();
		// We may have already created the effect (as disabled) during interactive creation, for
		// visualization purpose. In such case, do not create a new one but just update the existing one.
		bool const bIsFinalizingInteractiveCreation = (InstanceIndex == ClippingInfos.Num() - 1)
			&& bIsInteractiveCreation;

		ensure(InstanceIndex == ClippingInfos.Num() || bIsFinalizingInteractiveCreation);
		bool const bNeedAddNewPrimitive = (InstanceIndex >= ClippingInfos.Num());
		if (bNeedAddNewPrimitive || bIsFinalizingInteractiveCreation)
		{
			if (bNeedAddNewPrimitive)
			{
				ClippingInfos.SetNum(InstanceIndex + 1);
			}
			bool bIsClippingReady = UpdateClippingPrimitiveFromUEInstance(PrimitiveType, InstanceIndex, EUpdateContext::Add);

			bHasAddedClippingPrimitive = bIsClippingReady;
		}
	}
	return bHasAddedClippingPrimitive;
}

bool UITwinClippingEffectFactory::AddEffectFromInstance(EITwinClippingPrimitiveType PrimitiveType, int32 InstanceIndex)
{
	switch (PrimitiveType)
	{
	case EITwinClippingPrimitiveType::Box:
		return TAddEffectFromInstance<FITwinClippingBoxInfo, EITwinClippingPrimitiveType::Box>(
			EffectManager->ClippingBoxInfos, InstanceIndex);

	case EITwinClippingPrimitiveType::Plane:
		return TAddEffectFromInstance<FITwinClippingPlaneInfo, EITwinClippingPrimitiveType::Plane>(
			EffectManager->ClippingPlaneInfos, InstanceIndex);

		BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(
	case EITwinClippingPrimitiveType::Polygon:
	case EITwinClippingPrimitiveType::Count:, false);
	}
}


EITwinClippingPrimitiveType UITwinClippingEffectFactory::OnClippingInstanceAdded(AITwinPopulation* Population,
	EITwinInstantiatedObjectType ObjectType,
	int32 InstanceIndex)
{
	bool bHasAddedClippingPrimitive = false;
	EITwinClippingPrimitiveType const EffectType = ToClippingType(ObjectType);

	if (ensure(EffectType != EITwinClippingPrimitiveType::Count
		&& EffectManager.IsValid()))
	{
		EffectManager->RegisterCutoutPopulation(EffectType, Population);

		bHasAddedClippingPrimitive = AddEffectFromInstance(EffectType, InstanceIndex);
	}

	return bHasAddedClippingPrimitive ? EffectType : EITwinClippingPrimitiveType::Count;
}


inline double UITwinClippingEffectFactory::GetPrimitiveMasterMeshScale(EITwinClippingPrimitiveType Type) const
{
	const double* MasterMeshScaleCachedValue = MasterMeshScaleCacheByType.Find(Type);
	if (MasterMeshScaleCachedValue)
	{
		return *MasterMeshScaleCachedValue;
	}
	if (!ensure(EffectManager.IsValid()))
		return 1.0;
	auto const& Population = EffectManager->GetPopulation(Type);
	if (!ensure(Population.IsValid()))
		return 1.0;
	double MasterMeshScale = 1.0;
	const FBox MasterMeshBox = Population->GetMasterMeshBoundingBox();

	BE_ASSERT(MasterMeshBox.IsValid || GUsingNullRHI || GIsAutomationTesting,
		"Master mesh bounding box is invalid. This may happen if the master mesh is not loaded yet (e.g. in a headless environment).");
	if (MasterMeshBox.IsValid)
	{
		MasterMeshScale = MasterMeshBox.GetSize().GetAbsMax();
		MasterMeshScaleCacheByType.Add(Type, MasterMeshScale);
	}
	return MasterMeshScale;
}

void UITwinClippingEffectFactory::UpdateEdgesFromInstance(EITwinClippingPrimitiveType Type, int32 InstanceIndex)
{
	if (!ensure(IsValidEffectIndex(Type, InstanceIndex)))
	{
		return;
	}
	auto const& Population = EffectManager->GetPopulation(Type);
	if (!ensure(Population.IsValid()))
		return;

	// Update the spline helpers used to visualize the edges in the viewport.
	auto& CutoutEffect = EffectManager->GetMutableEffect(Type, InstanceIndex);
	if (CutoutEffect.NeedsCreateEdgeSplines())
	{
		// First time we create the box, we also create the edge splines.
		CutoutEffect.CreateEdgeSplines(EffectManager->GetSplineTool());
	}
	FTransform InstanceTransform = Population->GetInstanceTransform(InstanceIndex);
	InstanceTransform.MultiplyScale3D(FVector(GetPrimitiveMasterMeshScale(Type)));
	CutoutEffect.UpdateEdgeSplinesTransform(InstanceTransform);
}

bool UITwinClippingEffectFactory::GetBoxTransformInfoFromUEInstance(glm::dmat3x3& OutMatrix, glm::dvec3& OutTranslation, int32 InInstanceIndex) const
{
	if (!ensure(EffectManager.IsValid()))
		return false;
	auto const& BoxPopulation = EffectManager->GetPopulation(EITwinClippingPrimitiveType::Box);
	if (!ensure(BoxPopulation.IsValid()))
		return false;

	if (InInstanceIndex >= BoxPopulation->GetNumberOfInstances())
		return false;

	const FTransform InstanceTransform = BoxPopulation->GetInstanceTransform(InInstanceIndex);

	FMatrix InstanceMat = InstanceTransform.ToMatrixWithScale();
	// Take the master object's scale into account (depends on the way the box was imported
	// in Unreal...)
	InstanceMat *= GetPrimitiveMasterMeshScale(EITwinClippingPrimitiveType::Box);
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

bool UITwinClippingEffectFactory::UpdateClippingBoxFromUEInstance(int32 InstanceIndex, bool bInvalidateDB)
{
	if (!ensure(IsValidEffectIndex(EITwinClippingPrimitiveType::Box, InstanceIndex)))
	{
		return false;
	}
	const int32 BoxIndex = InstanceIndex;
	FITwinClippingBoxInfo& BoxInfo = EffectManager->ClippingBoxInfos[BoxIndex];

	glm::dmat3x3 BoxMatrix;
	glm::dvec3 BoxTranslation;
	if (!GetBoxTransformInfoFromUEInstance(BoxMatrix, BoxTranslation, InstanceIndex))
		return false;

	// Update the spline helpers used to visualize the box edges in the viewport.
	UpdateEdgesFromInstance(EITwinClippingPrimitiveType::Box, InstanceIndex);


	// Update the box information shared by all tile excluders activating this box.
	BoxInfo.UpdateBoxProperties(BoxMatrix, BoxTranslation);

	// Notify the renderer to update the Material Parameter Collection so that the new box coordinates are
	// seen by all tileset materials.
	EffectPropertiesModifiedEvent.Broadcast(EITwinClippingPrimitiveType::Box, BoxIndex);

	if (bInvalidateDB && Persistence.IsValid())
	{
		Persistence->UpdateBox(InstanceIndex);
	}

	return true;
}


template <typename T>
bool UITwinClippingEffectFactory::GetPlaneEquationFromUEInstance(UE::Math::TVector<T>& OutPlaneOrientation, T& OutPlaneW, int32 InInstanceIndex) const
{
	if (ensure(EffectManager.IsValid()))
	{
		auto const& PlanePopulation = EffectManager->GetPopulation(EITwinClippingPrimitiveType::Plane);

		return ITwinClippingToolUtils::GetPlaneEquationFromUEInstance(OutPlaneOrientation, OutPlaneW,
			PlanePopulation, InInstanceIndex);
	}
	else
	{
		return false;
	}
}

bool UITwinClippingEffectFactory::UpdateClippingPlaneEquationFromUEInstance(int32 InstanceIndex, bool bInvalidateDB)
{
	if (!ensure(IsValidEffectIndex(EITwinClippingPrimitiveType::Plane, InstanceIndex)))
	{
		return false;
	}

	FVector3d PlaneOrientation = FVector3d::ZAxisVector;
	double PlaneW(0.);
	if (!GetPlaneEquationFromUEInstance(PlaneOrientation, PlaneW, InstanceIndex))
		return false;

	const int32 PlaneIndex = InstanceIndex;
	auto& PlaneInfo = EffectManager->ClippingPlaneInfos[PlaneIndex];
	PlaneInfo.SetPlaneEquation(PlaneOrientation, PlaneW, true /* bUpdateTileExcluders*/);

	// Also update the plane equation stored in a Material Parameter Collection so that it can be accessed by
	// all tileset materials.
	EffectPropertiesModifiedEvent.Broadcast(EITwinClippingPrimitiveType::Plane, PlaneIndex);

	if (bInvalidateDB && Persistence.IsValid())
	{
		Persistence->UpdatePlane(InstanceIndex);
	}

	// Update the spline helpers used to visualize the box edges in the viewport.
	UpdateEdgesFromInstance(EITwinClippingPrimitiveType::Plane, InstanceIndex);

	return true;
}

bool UITwinClippingEffectFactory::UpdateClippingPrimitiveFromUEInstance(EITwinClippingPrimitiveType Type, int32 InstanceIndex,
	EUpdateContext Context)
{
	bool bUpdated = false;
	constexpr bool bInvalidateDB = false;
	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:
		bUpdated = UpdateClippingBoxFromUEInstance(InstanceIndex, bInvalidateDB);
		break;
	case EITwinClippingPrimitiveType::Plane:
		bUpdated = UpdateClippingPlaneEquationFromUEInstance(InstanceIndex, bInvalidateDB);
		break;
		BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(
	case EITwinClippingPrimitiveType::Polygon:
	case EITwinClippingPrimitiveType::Count:, false);
	}
	// NB:
	// - when loading cutouts from the Scene API in game, there is no AdvViz Instance created at this point
	// and instead, the cutout properties will be retrieved from the Scene API cutout;
	// - also, when removing an effect, we should not consider the AdvViz properties, as they are not updated
	// during the lifetime of the primitive (read comment in UpdatePropertiesFromAVizInstance).
	const bool bIsLoadingSceneAPICutoutsInGame = Persistence.IsValid()
		&& Persistence->IsLoadingSceneCutoutsInGame();
	if (bUpdated && !bIsLoadingSceneAPICutoutsInGame && Context != EUpdateContext::Remove)
	{
		UpdatePropertiesFromAVizInstance(Type, InstanceIndex);
	}
	return bUpdated;
}

template <typename ClippingPrimitiveInfo, EITwinClippingPrimitiveType PrimitiveType>
void UITwinClippingEffectFactory::TUpdateAllClippingPrimitives(TArray<ClippingPrimitiveInfo>& ClippingInfos, EUpdateContext Context)
{
	if (!ensure(EffectManager.IsValid()))
		return;
	auto const& PopulationPtr = EffectManager->GetPopulation(PrimitiveType);
	if (!ensure(PopulationPtr.IsValid()))
		return;
	const AITwinPopulation& ClippingPopulation = *PopulationPtr;
	const int32 NumPrims = ClippingPopulation.GetNumberOfInstances();

	// Disable cutout effects which have become obsolete.
	for (int32 i(NumPrims); i < ClippingInfos.Num(); ++i)
	{
		ClippingInfos[i].BeforeDestroy();
	}
	ClippingInfos.SetNum(NumPrims);

	// Update all remaining primitives.
	for (int32 InstanceIndex(0); InstanceIndex < NumPrims; ++InstanceIndex)
	{
		UpdateClippingPrimitiveFromUEInstance(PrimitiveType, InstanceIndex, Context);
	}
}

void UITwinClippingEffectFactory::UpdateAllClippingPrimitives(EITwinClippingPrimitiveType PrimitiveType, EUpdateContext Context)
{
	switch (PrimitiveType)
	{
	case EITwinClippingPrimitiveType::Box:
		TUpdateAllClippingPrimitives<FITwinClippingBoxInfo, EITwinClippingPrimitiveType::Box>(
			EffectManager->ClippingBoxInfos, Context);
		break;
	case EITwinClippingPrimitiveType::Plane:
		TUpdateAllClippingPrimitives<FITwinClippingPlaneInfo, EITwinClippingPrimitiveType::Plane>(
			EffectManager->ClippingPlaneInfos, Context);
		break;
	BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(
	case EITwinClippingPrimitiveType::Polygon:
	case EITwinClippingPrimitiveType::Count:);
	}
}

template <typename PrimitiveInfo, EITwinClippingPrimitiveType PrimitiveType>
void UITwinClippingEffectFactory::TOnClippingInstancesRemoved(TArray<PrimitiveInfo>& ClippingInfos,
	const TArray<int32>& IndicesInDescendingOrder, bool bUseRemoveAtSwap)
{
	// Preserve the Scene Link ID and other generic properties for primitives which are kept.
	TArray<PrimitiveInfo> InitialClippingInfos;
	InitialClippingInfos.SetNum(ClippingInfos.Num());
	for (int32 i(0); i < ClippingInfos.Num(); ++i)
	{
		InitialClippingInfos[i].CopyGenericInfoFrom(ClippingInfos[i]);
	}

	// To do so, build correspondance between old array and the remaining ones.
	TArray<int32> IndicesCorresp;
	IndicesCorresp.SetNum(ClippingInfos.Num());
	for (int32 i(0); i < ClippingInfos.Num(); ++i)
	{
		IndicesCorresp[i] = i;
	}
	for (auto const& Index : IndicesInDescendingOrder)
	{
		if (Index < IndicesCorresp.Num())
		{
			if (bUseRemoveAtSwap && IndicesCorresp.Num() > 1)
			{
				IndicesCorresp[Index] = IndicesCorresp.Num() - 1;
				IndicesCorresp.Pop();
			}
			else
			{
				IndicesCorresp.RemoveAt(Index);
			}
		}
	}
	BE_ASSERT(IndicesCorresp.Num() == ClippingInfos.Num() - IndicesInDescendingOrder.Num());

	for (int32 i(0); i < IndicesCorresp.Num(); ++i)
	{
		const int NewIndex = i;
		const int OldIndex = IndicesCorresp[i];
		if (NewIndex != OldIndex)
		{
			ClippingInfos[NewIndex].CopyGenericInfoFrom(InitialClippingInfos[OldIndex]);
		}
	}

	// Update all remaining primitives and actually shrink the array.
	TUpdateAllClippingPrimitives<PrimitiveInfo, PrimitiveType>(ClippingInfos, EUpdateContext::Remove);

	BE_ASSERT(ClippingInfos.Num() == IndicesCorresp.Num());
}

int32 UITwinClippingEffectFactory::RegisterCutoutSpline(AITwinSplineHelper* SplineHelper)
{
	if (!ensure(EffectManager.IsValid()))
	{
		return INDEX_NONE;
	}

	if (SplineHelper->GetUsage() == EITwinSplineUsage::MapCutout
		&& SplineHelper->HasCartographicPolygon())
	{
		const int32 NewEffectIndex = EffectManager->ClippingPolygonInfos.Num();
		FITwinClippingCartographicPolygonInfo& PolygonInfo = EffectManager->ClippingPolygonInfos.AddDefaulted_GetRef();
		PolygonInfo.InitWith(SplineHelper);

		return NewEffectIndex;
	}
	else
	{
		return INDEX_NONE;
	}
}


EITwinClippingPrimitiveType UITwinClippingEffectFactory::OnClippingInstancesLoaded(
	AITwinPopulation* Population, bool bUpdateEffectInfos)
{
	if (!ensure(Population && Population->IsClippingPrimitive()))
		return EITwinClippingPrimitiveType::Count;

	auto const PrimitiveType = ToClippingType(Population->GetObjectType());
	const int32 NumInstances = Population->GetNumberOfInstances();

	if (NumInstances > 0
		&& ensure(PrimitiveType != EITwinClippingPrimitiveType::Count))
	{
		EffectManager->RegisterCutoutPopulation(PrimitiveType, Population);
		if (bUpdateEffectInfos)
		{
			UpdateAllClippingPrimitives(PrimitiveType, EUpdateContext::Load);
		}
	}
	return PrimitiveType;
}


EITwinClippingPrimitiveType UITwinClippingEffectFactory::OnClippingInstanceModified(EITwinInstantiatedObjectType ObjectType,
	int32 InstanceIndex)
{
	switch (ObjectType)
	{
	case EITwinInstantiatedObjectType::ClippingPlane:
		if (UpdateClippingPlaneEquationFromUEInstance(InstanceIndex, true /*bInvalidateDB*/))
		{
			return EITwinClippingPrimitiveType::Plane;
		}
		break;
	case EITwinInstantiatedObjectType::ClippingBox:
		if (UpdateClippingBoxFromUEInstance(InstanceIndex, true /*bInvalidateDB*/))
		{
			return EITwinClippingPrimitiveType::Box;
		}
		break;
	default:
		// Nothing to do for other types.
		break;
	}

	return EITwinClippingPrimitiveType::Count;
}

bool UITwinClippingEffectFactory::DeRegisterCutoutSpline(AITwinSplineHelper* SplineBeingRemoved, bool bTriggeredFromITS)
{
	if (!ensure(EffectManager.IsValid()))
	{
		return false;
	}

	const int32 Index = EffectManager->GetCutoutPolygonIndex(SplineBeingRemoved);
	if (Index != INDEX_NONE)
	{
		if (Persistence.IsValid())
		{
			Persistence->RemoveFromScene(EITwinClippingPrimitiveType::Polygon, Index);
		}

		EffectManager->ClippingPolygonInfos.RemoveAt(Index);

		if (EventHub.IsValid())
		{
			EventHub->EffectRemovedEvent.Broadcast(EITwinClippingPrimitiveType::Polygon, Index, bTriggeredFromITS);
			EventHub->EffectListModifiedEvent.Broadcast();
		}
		return true;
	}
	else
	{
		return false;
	}
}

UITwinClippingEffectFactory::FScopedRemovalContext::FScopedRemovalContext(UITwinClippingEffectFactory& InFactory,
	ERemovalInitiator RemovalInitiator, EITwinClippingPrimitiveType Type)
	: Factory(InFactory)
{
	Factory.RemovalContextOpt.emplace();
	Factory.RemovalContextOpt->Initiator = RemovalInitiator;
	Factory.RemovalContextOpt->PrimitiveType = Type;
}


void UITwinClippingEffectFactory::DeleteSelectedPopulationInstance()
{
	if (!ensure(EffectManager.IsValid()))
	{
		return;
	}
	auto const& PopulationTool = EffectManager->GetPopulationTool();
	if (ensure(PopulationTool.IsValid() && PopulationTool->HasSelection()))
	{
		ensure(PopulationTool->IsUsedOnCutoutPrimitive());
		PopulationTool->DeleteSelectedInstance();
	}
}

void UITwinClippingEffectFactory::BeforeRemoveClippingInstances(EITwinInstantiatedObjectType ObjectType, const TArray<int32>& InstanceIndices)
{
	if (InstanceIndices.IsEmpty())
		return;
	EITwinClippingPrimitiveType RemovedPrimitiveType = ToClippingType(ObjectType);
	if (RemovedPrimitiveType != EITwinClippingPrimitiveType::Count)
	{
		const bool bTriggeredFromITS = RemovalContextOpt
			&& RemovalContextOpt->Initiator == ERemovalInitiator::ITS;
		const bool bHandlePersistence = Persistence.IsValid();
		const bool bBroadcastEvent = EventHub.IsValid();
		for (int32 EffectIndex : InstanceIndices)
		{
			if (bHandlePersistence)
			{
				Persistence->RemoveFromScene(RemovedPrimitiveType, EffectIndex);
			}

			if (bBroadcastEvent)
			{
				EventHub->EffectRemovedEvent.Broadcast(RemovedPrimitiveType, EffectIndex, bTriggeredFromITS);
			}
		}
	}
}


EITwinClippingPrimitiveType UITwinClippingEffectFactory::OnClippingInstancesRemoved(EITwinInstantiatedObjectType ObjectType,
	const TArray<int32>& IndicesInDescendingOrder, bool bUseRemoveAtSwap)
{
	EITwinClippingPrimitiveType const EffectType = ToClippingType(ObjectType);
	if (ensure(EffectType != EITwinClippingPrimitiveType::Count))
	{
		switch (EffectType)
		{
		case EITwinClippingPrimitiveType::Box:
			TOnClippingInstancesRemoved<FITwinClippingBoxInfo, EITwinClippingPrimitiveType::Box>(
				EffectManager->ClippingBoxInfos, IndicesInDescendingOrder, bUseRemoveAtSwap);
			break;
		case EITwinClippingPrimitiveType::Plane:
			TOnClippingInstancesRemoved<FITwinClippingPlaneInfo, EITwinClippingPrimitiveType::Plane>(
				EffectManager->ClippingPlaneInfos, IndicesInDescendingOrder, bUseRemoveAtSwap);
			break;
		BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(
		case EITwinClippingPrimitiveType::Polygon:
		case EITwinClippingPrimitiveType::Count:);
		}
	}
	return EffectType;
}

bool UITwinClippingEffectFactory::RemoveEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bTriggeredFromITS)
{
	if (!ensure(IsValidEffectIndex(Type, PrimitiveIndex)))
	{
		return false;
	}

	FScopedRemovalContext RemovalCtx(*this,
		bTriggeredFromITS ? ERemovalInitiator::ITS : ERemovalInitiator::Unreal,
		Type);

	// Before removing an instance, store the current effect properties in the AdvViz instance so
	// that they are automatically restored if we undo the deletion afterwards.
	if (Type == EITwinClippingPrimitiveType::Box || Type == EITwinClippingPrimitiveType::Plane)
	{
		StorePropertiesInAVizInstance(Type, PrimitiveIndex);
	}

	if (EventHub.IsValid())
	{
		EventHub->RemoveEffectStartedEvent.Broadcast();
	}

	const int32 NumPrimsOld = EffectManager->NumEffects(Type);
	// Remark: for box and plane, the removal of the entry from the info array will be indirect, through a
	// call to #OnClippingInstancesRemoved (see AITwinPopulation::RemoveInstance).
	// Hence, to know if the removal succeeded, we just check the count of primitives at the end.
	switch (Type)
	{
	case EITwinClippingPrimitiveType::Box:
	case EITwinClippingPrimitiveType::Plane:
	{
		DeleteSelectedPopulationInstance();
		break;
	}

	case EITwinClippingPrimitiveType::Polygon:
	{
		auto const& PolygonInfo = EffectManager->ClippingPolygonInfos[PrimitiveIndex];
		auto const& SplineTool = EffectManager->GetSplineTool();
		if (PolygonInfo.GetSpline().IsValid() && ensure(SplineTool.IsValid()))
		{
			SplineTool->DeleteSpline(PolygonInfo.GetSpline().Get(), bTriggeredFromITS);
		}
		break;
	}

	BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(case EITwinClippingPrimitiveType::Count:, false);
	}

	const bool bRemoved = (EffectManager->NumEffects(Type) == NumPrimsOld - 1);
	return bRemoved;
}
