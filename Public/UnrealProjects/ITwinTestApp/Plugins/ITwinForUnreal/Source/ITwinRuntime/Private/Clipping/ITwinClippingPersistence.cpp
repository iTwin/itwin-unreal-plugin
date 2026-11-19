/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingPersistence.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Clipping/ITwinClippingPersistence.h>

#include <Clipping/ITwinClippingEffectManager.h>
#include <Clipping/ITwinClippingEffectManager.inl>
#include <Clipping/ITwinClippingInfoBase.inl>
#include <Clipping/ITwinClippingToolUtils.inl>
#include <Decoration/ITwinDecorationHelper.h>
#include <Math/UEMathConversion.h>
#include <Population/ITwinPopulation.h>
#include <Population/ITwinPopulationTool.h>
#include <Spline/ITwinSplineHelper.h>


#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Compil/EnumSwitchCoverage.h>
#	include <BeHeaders/Util/CleanUpGuard.h>
#	include <SDK/Core/Visualization/ScenePersistence.h>
#	include <SDK/Core/Visualization/Spline.h>
#	include <Core/Tools/Log.h>
#include <Compil/AfterNonUnrealIncludes.h>



namespace AdvViz::SDK
{
	std::string FindModelTypeFromLayerId(IScenePersistence const& Scene, std::string const& LayerId);
	bool ConvertSplineToCutout(ISplinePtr const& Spline, Cutout& cutout);
}


UITwinClippingPersistence::UITwinClippingPersistence()
{

}

void UITwinClippingPersistence::SetEffectManager(UITwinClippingEffectManager* InEffectManager)
{
	EffectManager = InEffectManager;
}

void UITwinClippingPersistence::Connect(AITwinDecorationHelper* InPersistenceMgr)
{
	PersistenceMgr = InPersistenceMgr;
	if (PersistenceMgr.IsValid())
	{
		PersistenceMgr->OnSceneLoaded.AddUniqueDynamic(this, &UITwinClippingPersistence::OnSceneLoaded);
		PersistenceMgr->OnPopulationsLoaded.AddUniqueDynamic(this, &UITwinClippingPersistence::OnPopulationsLoaded);
	}
}

bool UITwinClippingPersistence::CheckPersistenceMgr() const
{
	if (!PersistenceMgr.IsValid())
	{
		const_cast<UITwinClippingPersistence*>(this)
			->Connect(AITwinDecorationHelper::GetInstance(GetWorld()));
	}
	return PersistenceMgr.IsValid();
}

inline
bool UITwinClippingPersistence::IsValidEffectIndex(EITwinClippingPrimitiveType EffectType, int32 Index) const
{
	return EffectManager.IsValid()
		&& EffectManager->IsValidEffectIndex(EffectType, Index);
}

namespace ITwin
{
	FString GetCutoutAssetPath(EITwinClippingPrimitiveType Type);
}

namespace
{
	static inline AdvViz::SDK::ECutoutType ToAVizCutoutType(EITwinClippingPrimitiveType Type)
	{
		switch (Type)
		{
		case EITwinClippingPrimitiveType::Box:		return AdvViz::SDK::ECutoutType::Box;
		case EITwinClippingPrimitiveType::Plane:	return AdvViz::SDK::ECutoutType::Plane;
		case EITwinClippingPrimitiveType::Polygon:	return AdvViz::SDK::ECutoutType::Polygons;

		BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(
		case EITwinClippingPrimitiveType::Count:, AdvViz::SDK::ECutoutType::ENUM_END);
		}
	}

	static inline EITwinClippingPrimitiveType ToPrimitiveType(AdvViz::SDK::ECutoutType Type)
	{
		switch (Type)
		{
		case AdvViz::SDK::ECutoutType::Box:			return EITwinClippingPrimitiveType::Box;
		case AdvViz::SDK::ECutoutType::Plane:		return EITwinClippingPrimitiveType::Plane;
		case AdvViz::SDK::ECutoutType::Polygons:	return EITwinClippingPrimitiveType::Polygon;

		BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(
		case AdvViz::SDK::ECutoutType::ENUM_END:, EITwinClippingPrimitiveType::Count);
		}
	}

	// Convenient function to get the AdvViz instance associated to a given clipping primitive, and assert
	// on the validity of the population and index.
	inline AdvViz::SDK::IInstancePtr GetAVizInstance(TWeakObjectPtr<AITwinPopulation> const& Population,
		int32 Index)
	{
		if (ensure(Population.IsValid() && Index < Population->GetNumberOfInstances()))
		{
			return Population->GetAVizInstance(Index);
		}
		else
		{
			return {};
		}
	}

	void FillCutoutBaseFromEffect(FITwinClippingInfoBase const& Effect,
		EITwinClippingPrimitiveType EffectType,
		AdvViz::SDK::CutoutBase& OutCutoutBase)
	{
		OutCutoutBase.cutoutType = ToAVizCutoutType(EffectType);
		OutCutoutBase.enabled = Effect.IsEnabled();
		OutCutoutBase.inverse = Effect.GetInvertEffect();
		for (EITwinModelType ModelType : { EITwinModelType::IModel,
			EITwinModelType::RealityData,
			EITwinModelType::GlobalMapLayer })
		{
			FITwinClippingInfluenceInfo const& InfluenceInfo = Effect.GetInfluenceInfo(ModelType);
			BE_ASSERT(!InfluenceInfo.bInfluenceAll, "influence-all not supported with SceneAPI");
			for (auto const& LayerId : InfluenceInfo.SpecificIDs)
			{
				OutCutoutBase.appliesTo.push_back(TCHAR_TO_UTF8(*LayerId));
			}
		}
	}

	FORCEINLINE void FillMatrix4x4FromMatrix3x4(
		std::array<double, 16>& OutTransform4x4,
		AdvViz::SDK::dmat3x4 const& Transform3x4)
	{
		std::memcpy(OutTransform4x4.data(), Transform3x4.data(), 12 * sizeof(double));
		OutTransform4x4[12] = 0.0;
		OutTransform4x4[13] = 0.0;
		OutTransform4x4[14] = 0.0;
		OutTransform4x4[15] = 1.0;
	}

	bool FillCutoutBoxFromAVizInstance(
		AdvViz::SDK::IInstancePtr const& AVizInstPtr,
		AdvViz::SDK::CutoutBox& OutCutoutBox)
	{
		// Work with the AdvViz instance to get the transformation in GCS, not the one from UE.
		if (ensure(AVizInstPtr))
		{
			auto AVizInst = AVizInstPtr->GetRAutoLock();

			const AdvViz::SDK::dmat3x4& Transform3x4 = AVizInst->GetTransform();
			auto& Transform4x4 = OutCutoutBox.transformFromClip.emplace();
			FillMatrix4x4FromMatrix3x4(Transform4x4, Transform3x4);

			// Half extents, translation and rotation are all included in the transformation, so we just
			// need to set the half extents to (0.5, 0.5, 0.5).
			OutCutoutBox.halfExtents = { 0.5, 0.5, 0.5 };

			return true;
		}
		else
		{
			return false;
		}
	}

	void FillCutoutPlaneFromEquation(FVector const& PlaneOrientation, double PlaneW,
		AdvViz::SDK::CutoutPlane& OutCutoutPlane)
	{
		// As 'normal' is meant to point toward the region that should be kept, we need to invert it as
		// we use the other convention since the beginning...
		BE_ASSERT(PlaneOrientation.IsNormalized());
		OutCutoutPlane.normal = { -PlaneOrientation.X, -PlaneOrientation.Y, -PlaneOrientation.Z };
		OutCutoutPlane.distance = -PlaneW;
	}

	void FillCutoutPlaneFromUETransform(
		FTransform const& PlaneTransform_UE,
		AdvViz::SDK::CutoutPlane& OutCutoutPlane)
	{
		FVector PlaneNormal_UE = FVector::ZeroVector;
		double Distance_UE(0.);
		ITwinClippingToolUtils::GetPlaneEquationFromTransform(PlaneNormal_UE, Distance_UE, PlaneTransform_UE);

		// Transform to SDK.
		FVector OriginSDK = toUnreal(FITwinMathConversion::UEtoSDK(FVector::ZeroVector));
		FVector NormalSDK = toUnreal(FITwinMathConversion::UEtoSDK(PlaneNormal_UE));
		NormalSDK -= OriginSDK;
		NormalSDK.Normalize();

		// Convert a point lying on the plane so that the resulting transform satisfies:
		// Position.Dot(NormalUE) == ConvertedDistance.
		const FVector PointOnPlaneUE = {
			PlaneNormal_UE.X * Distance_UE,
			PlaneNormal_UE.Y * Distance_UE,
			PlaneNormal_UE.Z * Distance_UE
		};
		FVector const PointOnPlane_SDK = toUnreal(FITwinMathConversion::UEtoSDK(PointOnPlaneUE));
		double const Distance_SDK = PointOnPlane_SDK.Dot(NormalSDK);
		FillCutoutPlaneFromEquation(NormalSDK, Distance_SDK, OutCutoutPlane);
	}

	FTransform BuildTransformFromAVizCutoutPlane(AdvViz::SDK::CutoutPlane const& CutoutPlane)
	{
		// First invert the plane equation to get the normal pointing toward the clipped region.

		const AdvViz::SDK::double3 NormalSDK = {
			-CutoutPlane.normal[0],
			-CutoutPlane.normal[1],
			-CutoutPlane.normal[2]
		};
		const AdvViz::SDK::double3 ZeroSDK = { 0., 0., 0. };

		const double DistanceSDK = -CutoutPlane.distance;

		const AdvViz::SDK::double3 PointOnPlaneSDK = {
			NormalSDK[0] * DistanceSDK,
			NormalSDK[1] * DistanceSDK,
			NormalSDK[2] * DistanceSDK
		};

		// Transform to Unreal.
		FVector NormalUE = FITwinMathConversion::SDKtoUE(NormalSDK);
		FVector OriginUE = FITwinMathConversion::SDKtoUE(ZeroSDK);
		NormalUE -= OriginUE;
		NormalUE.Normalize();

		// Convert a point lying on the plane so that the resulting transform satisfies:
		// Position.Dot(NormalUE) == ConvertedDistance.
		FVector const PointOnPlaneUE = FITwinMathConversion::SDKtoUE(PointOnPlaneSDK);

		double const ConvertedDistance = PointOnPlaneUE.Dot(NormalUE);

		// Build an orthonormal basis using the plane normal as Up/Z axis.
		FVector Tangent = FVector::CrossProduct(
			(FMath::Abs(NormalUE.Z) < 0.999) ? FVector::UpVector : FVector::ForwardVector,
			NormalUE);
		Tangent.Normalize();

		FVector Bitangent = FVector::CrossProduct(NormalUE, Tangent);
		Bitangent.Normalize();

		FMatrix PlaneMatrix = FMatrix::Identity;
		FVector const Origin = NormalUE * ConvertedDistance;
		PlaneMatrix.SetAxes(&Tangent, &Bitangent, &NormalUE, &Origin);

		return FTransform(PlaneMatrix);
	}

}

bool UITwinClippingPersistence::PreLoadCutoutPrimitive(EITwinClippingPrimitiveType PrimitiveType)
{
	auto const& PopulationTool = EffectManager->GetPopulationTool();
	if (PopulationTool.IsValid())
	{
		AITwinPopulation* Population = PopulationTool->PreLoadPopulation(ITwin::GetCutoutAssetPath(PrimitiveType));
		if (Population)
		{
			EffectManager->RegisterCutoutPopulation(PrimitiveType, Population);
			return true;
		}
	}
	return false;
}

uint32 UITwinClippingPersistence::PreLoadClippingPrimitives()
{
	if (!EffectManager.IsValid())
	{
		return 0;
	}
	/// By pre-loading the cutout populations, we avoid some latency when the user creates a first
	/// cutout cube or plane.
	uint32 NumPreloaded = 0;
	if (PreLoadCutoutPrimitive(EITwinClippingPrimitiveType::Box))
	{
		NumPreloaded++;
	}
	if (PreLoadCutoutPrimitive(EITwinClippingPrimitiveType::Plane))
	{
		NumPreloaded++;
	}
	return NumPreloaded;
}

void UITwinClippingPersistence::LoadSceneAPICutoutInstancesInGame()
{
	// New persistence system for cutouts: now loaded within the scene.
	// Let's create the equivalent cube/plane instances.
	CheckPersistenceMgr();
	auto Scene = GetScene();
	if (!ensure(Scene && EffectManager.IsValid()))
	{
		return;
	}

	Be::CleanUpGuard RestoreGuard([this]
	{
		bIsLoadingSceneAPICutoutsInGame = false;
	});
	bIsLoadingSceneAPICutoutsInGame = true;

	for (AdvViz::SDK::ECutoutType CutoutType : {
			AdvViz::SDK::ECutoutType::Box,
			AdvViz::SDK::ECutoutType::Plane
	})
	{
		EITwinClippingPrimitiveType PrimitiveType = ToPrimitiveType(CutoutType);
		TWeakObjectPtr<AITwinPopulation> Population = EffectManager->GetPopulation(PrimitiveType);
		if (!ensure(Population.IsValid()))
		{
			continue;
		}
		// Initially, hide the cutout population (will be made visible when the user activates the cutout
		// selection tool (or selects a cutout from the list in iTS).
		Population->SetHiddenInGame(true);

		auto const AVizCutouts = Scene->GetCutouts({ CutoutType });

		int32 CreatedInstances = 0;
		for (auto const& [RefId, Cutout] : AVizCutouts)
		{
			BE_ASSERT(Cutout.cutoutType == CutoutType);

			// Create a new instance for each cutout.
			FTransform UETransform = FTransform::Identity;

			if (CutoutType == AdvViz::SDK::ECutoutType::Box)
			{
				// Build transformation from the cube transformation.
				const auto* CutoutBox = std::get_if<AdvViz::SDK::CutoutBox>(&Cutout.cutoutData);
				if (ensure(CutoutBox) && CutoutBox->transformFromClip)
				{
					AdvViz::SDK::dmat3x4 transform3x4;
					auto const& transform4x4 = CutoutBox->transformFromClip.value();
					std::memcpy(transform3x4.data(), transform4x4.data(), 12 * sizeof(double));
					UETransform = FITwinMathConversion::SDKtoUE(transform3x4);
				}
			}
			else
			{
				// Build a transformation from the plane equation.
				const auto* CutoutPlane = std::get_if<AdvViz::SDK::CutoutPlane>(&Cutout.cutoutData);
				if (ensure(CutoutPlane))
				{
					UETransform = BuildTransformFromAVizCutoutPlane(*CutoutPlane);
				}
			}

			const int32 InstanceIndex = Population->AddInstance(UETransform,
				AITwinPopulation::EAddInstanceContext::LoadScene);

			if (InstanceIndex != INDEX_NONE
				&& ensure(InstanceIndex < EffectManager->NumEffects(PrimitiveType)))
			{
				FITwinClippingInfoBase& PrimitiveInfo = EffectManager->GetMutableEffect(PrimitiveType, InstanceIndex);
				PrimitiveInfo.SetSceneLinkId(RefId);
				PrimitiveInfo.SetInvertEffect(Cutout.inverse);
				PrimitiveInfo.SetEnabled(Cutout.enabled);
				for (auto const& LayerId : Cutout.appliesTo)
				{
					auto LayerType = AdvViz::SDK::FindModelTypeFromLayerId(*Scene, LayerId);
					if (LayerType.empty())
					{
						BE_ISSUE("Cutout applies to unknown layer", LayerId);
						continue;
					}
					else
					{
						PrimitiveInfo.SetInfluenceSpecificModel(
							std::make_pair(
								ITwin::StrToModelType(LayerType),
								FString(LayerId.c_str())),
							true);
					}
				}
				CreatedInstances++;
			}
		}
		if (CreatedInstances > 0)
		{
			// No need to fill effect infos again: we have just done it.
			// Just update rendering data.
			EffectManager->OnClippingInstancesLoaded(Population.Get(), false /*bUpdateEffectInfos*/);
		}
	}
}

void UITwinClippingPersistence::OnCutoutAdded(EITwinClippingPrimitiveType EffectType, int32 Index,
	std::optional<bool> const& IsLoadingSpline /*= std::nullopt*/)
{
	BE_ASSERT((EffectType == EITwinClippingPrimitiveType::Polygon && IsLoadingSpline.has_value())
		|| (EffectType != EITwinClippingPrimitiveType::Polygon && !IsLoadingSpline.has_value()));

	if (CheckPersistenceMgr() && !bIsLoadingSceneAPICutoutsInGame)
	{
		// Create new cutout in scene if needed.
		bool bAddToSceneAPI = true;

		// If we are currently loading a spline from the Scene API, we don't want to create a new cutout in
		// the scene as well. If the spline comes from the Decoration Service instead, we do create a new
		// cutout in the scene, to achieve the conversion of old scenes.
		if (EffectType == EITwinClippingPrimitiveType::Polygon
			&& IsLoadingSpline.value_or(false))
		{
			const AITwinSplineHelper* SplineHelper = EffectManager->GetCutoutSpline(Index);
			AdvViz::SDK::RefID const SplineId = SplineHelper ? SplineHelper->GetAVizSplineId()
				: AdvViz::SDK::RefID::Invalid();
			auto const Scene = GetScene();

			bAddToSceneAPI = Scene
				&& Scene->FindCutoutType(SplineId) != AdvViz::SDK::ECutoutType::Polygons;

			if (!bAddToSceneAPI && ensure(IsValidEffectIndex(EffectType, Index)))
			{
				// The spline already has a corresponding cutout in the scene. Make sure it is correctly
				// linked to it, to avoid re-sending its data at the end of the loading.
				// (see AITwinClippingTool::FImpl::#RegisterLoadedEffectsInScene)
				FITwinClippingInfoBase& Effect = EffectManager->GetMutableEffect(EffectType, Index);
				BE_ASSERT(!Effect.HasSceneLink() || Effect.GetSceneLinkId() == SplineId,
					"Effect and spline IDs should match");
				Effect.SetSceneLinkId(SplineId);
			}
		}

		if (bAddToSceneAPI)
		{
			// Create new cutout in scene.
			SendToScene(EffectType, Index);
		}
	}
}

inline
bool UITwinClippingPersistence::CanSendToScene(EITwinClippingPrimitiveType EffectType, int32 Index) const
{
	CheckPersistenceMgr();
	if (!GetScene() || !IsValidEffectIndex(EffectType, Index))
	{
		return false;
	}
	return true;
}

inline
bool UITwinClippingPersistence::IsValidPopulationIndex(EITwinClippingPrimitiveType EffectType, int32 Index) const
{
	return EffectManager.IsValid()
		&& EffectManager->IsValidPopulationIndex(EffectType, Index);
}

bool UITwinClippingPersistence::SendToScene(EITwinClippingPrimitiveType EffectType, int32 Index)
{
	if (!CanSendToScene(EffectType, Index))
	{
		return false;
	}

	FITwinClippingInfoBase& Effect = EffectManager->GetMutableEffect(EffectType, Index);

	AdvViz::SDK::Cutout AVizCutout;
	FillCutoutBaseFromEffect(Effect, EffectType, AVizCutout);
	switch (EffectType)
	{
	case EITwinClippingPrimitiveType::Box:
	{
		auto const BoxPopulation = EffectManager->GetPopulation(EffectType);
		AdvViz::SDK::CutoutBox& CutoutBox = AVizCutout.cutoutData.emplace<AdvViz::SDK::CutoutBox>();
		AdvViz::SDK::IInstancePtr AVizInstPtr = GetAVizInstance(BoxPopulation, Index);
		if (!FillCutoutBoxFromAVizInstance(AVizInstPtr, CutoutBox))
		{
			BE_ISSUE("Failed to get box transform from AViz instance");
			return false;
		}
		break;
	}
	case EITwinClippingPrimitiveType::Plane:
	{
		if (!IsValidPopulationIndex(EffectType, Index))
		{
			BE_ISSUE("Failed to get plane transform");
			return false;
		}
		AdvViz::SDK::CutoutPlane& CutoutPlane = AVizCutout.cutoutData.emplace<AdvViz::SDK::CutoutPlane>();
		auto const PlanePopulation = EffectManager->GetPopulation(EffectType);
		const FTransform PlaneTransform = PlanePopulation->GetInstanceTransform(Index);
		FillCutoutPlaneFromUETransform(PlaneTransform, CutoutPlane);
		break;
	}
	case EITwinClippingPrimitiveType::Polygon:
	{
		const AITwinSplineHelper* SplineHelper = EffectManager->GetCutoutSpline(Index);
		if (!SplineHelper)
		{
			BE_ISSUE("Failed to get spline");
			return false;
		}
		auto Spline = SplineHelper->GetAVizSpline();
		if (!AdvViz::SDK::ConvertSplineToCutout(Spline, AVizCutout))
		{
			BE_ISSUE("Failed to convert spline to cutout");
			return false;
		}
		break;
	}
	BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(case EITwinClippingPrimitiveType::Count:, false);
	}

	// If the effect has no ID yet, let's assign one.
	if (!Effect.HasSceneLink())
	{
		auto SceneLinkId = EffectManager->GetEffectId(EffectType, Index);
		// Ignore the DB identifier, if any (it would come from the Decoration Service, hence not relevant
		// in the Scene API!).
		SceneLinkId.SetDBIdentifier("");

		Effect.SetSceneLinkId(SceneLinkId);
	}

	GetScene()->SetCutout(Effect.GetSceneLinkId(), AVizCutout);
	return true;
}

void UITwinClippingPersistence::UpdateBaseInfo(EITwinClippingPrimitiveType EffectType, int32 Index)
{
	if (!CanSendToScene(EffectType, Index))
	{
		return;
	}
	const FITwinClippingInfoBase& Effect = EffectManager->GetEffect(EffectType, Index);
	// Update the box properties in the persistence manager, and invalidate the scene link.
	if (Effect.HasSceneLink())
	{
		AdvViz::SDK::CutoutBase CutoutBase;
		FillCutoutBaseFromEffect(Effect, EffectType, CutoutBase);
		GetScene()->UpdateCutoutBaseInfo(Effect.GetSceneLinkId(), CutoutBase);
	}
}

void UITwinClippingPersistence::UpdateInversionInfo(EITwinClippingPrimitiveType EffectType, int32 Index)
{
	if (EffectType == EITwinClippingPrimitiveType::Plane)
	{
		// Special case for the plane: the schema does not have a specific property for inversion, so we have
		// to invert the plane equation instead...
		UpdatePlane(Index);
	}
	else
	{
		UpdateBaseInfo(EffectType, Index);
	}
}

void UITwinClippingPersistence::UpdateBox(int32 Index)
{
	constexpr EITwinClippingPrimitiveType EffectType = EITwinClippingPrimitiveType::Box;
	if (!CanSendToScene(EffectType, Index))
	{
		return;
	}
	const FITwinClippingInfoBase& BoxInfo = EffectManager->GetEffect(EffectType, Index);
	if (BoxInfo.HasSceneLink())
	{
		// Update the box transformation in the persistence manager, and invalidate the scene link.
		// Work with the AdvViz instance to get the transformation in GCS, not the one from UE.
		AdvViz::SDK::CutoutBox CutoutBox;
		auto const BoxPopulation = EffectManager->GetPopulation(EffectType);
		AdvViz::SDK::IInstancePtr AVizInstPtr = GetAVizInstance(BoxPopulation, Index);
		if (FillCutoutBoxFromAVizInstance(AVizInstPtr, CutoutBox))
		{
			GetScene()->UpdateCutoutBox(BoxInfo.GetSceneLinkId(), CutoutBox);
		}
	}
}

void UITwinClippingPersistence::UpdatePlane(int32 Index)
{
	constexpr EITwinClippingPrimitiveType EffectType = EITwinClippingPrimitiveType::Plane;
	if (!CanSendToScene(EffectType, Index))
	{
		return;
	}
	const FITwinClippingInfoBase& PlaneInfo = EffectManager->GetEffect(EffectType, Index);
	if (PlaneInfo.HasSceneLink())
	{
		if (!IsValidPopulationIndex(EITwinClippingPrimitiveType::Plane, Index))
		{
			BE_ISSUE("Cannot get plane transform to update scene link");
			return;
		}
		// Update the plane equation in the persistence manager, and invalidate the scene link.
		AdvViz::SDK::CutoutPlane AVizPlaneInfo;
		auto const PlanePopulation = EffectManager->GetPopulation(EITwinClippingPrimitiveType::Plane);
		const FTransform PlaneTransform = PlanePopulation->GetInstanceTransform(Index);
		FillCutoutPlaneFromUETransform(PlaneTransform, AVizPlaneInfo);

		GetScene()->UpdateCutoutPlane(PlaneInfo.GetSceneLinkId(), AVizPlaneInfo, PlaneInfo.GetInvertEffect());
	}
}

void UITwinClippingPersistence::UpdatePolygonTransform(int32 Index, const AdvViz::SDK::dmat3x4& Transform)
{
	if (!CanSendToScene(EITwinClippingPrimitiveType::Polygon, Index))
	{
		return;
	}
	const FITwinClippingInfoBase& PolygonInfo = EffectManager->GetEffect(EITwinClippingPrimitiveType::Polygon, Index);
	if (PolygonInfo.HasSceneLink())
	{
		std::array<double, 16> Transform4x4;
		FillMatrix4x4FromMatrix3x4(Transform4x4, Transform);

		GetScene()->UpdateCutoutPolygonTransform(PolygonInfo.GetSceneLinkId(),
			Transform4x4);
	}
}

void UITwinClippingPersistence::UpdatePolygonPoint(int32 Index, size_t PointIndex,
	const AdvViz::SDK::double3& Position)
{
	if (!CanSendToScene(EITwinClippingPrimitiveType::Polygon, Index))
	{
		return;
	}
	const FITwinClippingInfoBase& PolygonInfo = EffectManager->GetEffect(EITwinClippingPrimitiveType::Polygon, Index);
	if (PolygonInfo.HasSceneLink())
	{
		GetScene()->UpdateCutoutPolygonPoint(PolygonInfo.GetSceneLinkId(),
			0 /*polygonIndexInSet*/,
			PointIndex,
			Position);
	}
}

void UITwinClippingPersistence::RemoveFromScene(EITwinClippingPrimitiveType EffectType, int32 Index)
{
	if (!CanSendToScene(EffectType, Index))
	{
		return;
	}
	const FITwinClippingInfoBase& Effect = EffectManager->GetEffect(EffectType, Index);
	if (!Effect.HasSceneLink())
	{
		return;
	}
	GetScene()->RemoveCutout(Effect.GetSceneLinkId());
}

void UITwinClippingPersistence::OnSceneLoaded(bool bSuccess)
{
	// Store whether the loaded scene contains cutouts, so that we can later check if we need to convert
	// legacy ones coming from the Decoration Service.
	bool bSceneWithNewCutouts = false;
	if (bSuccess)
	{
		CheckPersistenceMgr();
		auto const Scene = GetScene();
		bSceneWithNewCutouts = Scene && Scene->HasCutouts();
	}
	bDoesLoadedSceneContainCutouts = bSceneWithNewCutouts;
}

bool UITwinClippingPersistence::DoesLoadedSceneContainCutouts() const
{
	BE_ASSERT(bDoesLoadedSceneContainCutouts.has_value(),
		"scene *must* be loaded before instances/splines!");
	return bDoesLoadedSceneContainCutouts.value_or(false);
}

void UITwinClippingPersistence::OnPopulationsLoaded(bool bSuccess)
{
	if (!EffectManager.IsValid())
	{
		return;
	}
	auto const& PopulationTool = EffectManager->GetPopulationTool();
	if (PopulationTool.IsValid())
	{
		// Pre-load cube and plane primitives if needed (ie. if they were not loaded from the scene).
		PreLoadClippingPrimitives();

		// New persistence system for cutouts: now loaded within the scene.
		// Let's create the equivalent cube/plane instances.
		LoadSceneAPICutoutInstancesInGame();
	}
}

std::shared_ptr<AdvViz::SDK::IScenePersistence> UITwinClippingPersistence::GetScene() const
{
	if (PersistenceMgr.IsValid())
	{
		return PersistenceMgr->GetScenePersistence();
	}
	else
	{
		return {};
	}
}
