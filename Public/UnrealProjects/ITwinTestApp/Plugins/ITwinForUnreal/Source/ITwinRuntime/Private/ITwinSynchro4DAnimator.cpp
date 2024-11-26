/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSynchro4DAnimator.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinSynchro4DAnimator.h>
#include <ITwinSynchro4DSchedules.h>
#include <ITwinSynchro4DSchedulesInternals.h>
#include <ITwinIModel.h>
#include <ITwinIModelInternals.h>
#include <ITwinSceneMapping.h>
#include <ITwinExtractedMeshComponent.h>
#include <Timeline/Timeline.h>
#include <Timeline/TimeInSeconds.h>
#include <Timeline/SchedulesConstants.h>

#include <GenericPlatform/GenericPlatformTime.h>
#include <Materials/MaterialInstanceDynamic.h>
#include <Math/Plane.h>
#include <Math/Quat.h>
#include <Math/Transform.h>
#include <Math/Vector.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

namespace ITwin::Synchro4D
{
	bool s_bMaskTilesUntilFullyAnimated = false;
}

class FITwinSynchro4DAnimator::FImpl
{
	friend class FITwinSynchro4DAnimator;
	FITwinSynchro4DAnimator& Owner;
	/// Last AnimationTime for which the animation was applied for all timelines
	std::optional<double> LastAnimationTime;
	bool bIsPlaying = false, bIsPaused = true;

// Variables used by ApplyAnimation to handle distribution of the work load over several ticks:
	/// Schedule time used to apply the animation: it should probably be consistent for the whole scene,
	/// even though the animation is applied over several ticks (which means AnimationTime <= ScheduleTime)
	double AnimationTime = 0;
	/// Next timeline to process in ApplyAnimation so, when zero, it means the state of all timelines is
	/// consistent with AnimationTime.
	size_t NextTimelineToUpdate = 0;
	/// Whether applying all timelines has touched at least one property texture
	bool bHasUpdatedSomething = false;
	/// We need to store the flag passed to ApplyAnimation so that the information persists over the several
	/// ticks it can take to apply all timelines. Also, if ApplyAnimation is called with bForceUpdateAll=true
	/// while in the middle of an update loop, we need to store it to launch a new full-update loop later on.
	bool bNeedUpdateAll = false, bNeedUpdateAllAgain = false;
	/// Time it last took to apply the whole animation (informative)
	double TimeToApplyAllTimelines = 0;

	std::unordered_set<FITwinSceneTile*> HasNewMeshesToAnimate;
	std::optional<ITwinElementID> DebugElem;

	void ApplyAnimation(bool const bForceUpdateAll);

public:
	FImpl(FITwinSynchro4DAnimator& InOwner) : Owner(InOwner) {}
};

FITwinSynchro4DAnimator::FITwinSynchro4DAnimator(UITwinSynchro4DSchedules& InOwner)
	: Owner(InOwner)
	, Impl(MakePimpl<FImpl>(*this))
{
}

void FITwinSynchro4DAnimator::TickAnimation(float DeltaTime, bool const bNewTilesReceived)
{
	auto&& SchedInternals = GetInternals(Owner);
	if (SchedInternals.PrefetchAllElementAnimationBindings()
		&& !SchedInternals.IsPrefetchedAvailableAndApplied())
	{
		ensure(false); return;
	}
	if (!Impl->bIsPlaying && !Impl->bIsPaused)
	{
		if (ITwin::Synchro4D::s_bMaskTilesUntilFullyAnimated)
		{
			auto* IModel = Cast<AITwinIModel>(Owner.GetOwner());
			if (!IModel)
				return;
			FITwinIModelInternals& IModelInternals = GetInternals(*IModel);
			// Set visibility here since animation will not be applied! May still flick off and on but the whole
			// system is a temporary solution anyway...
			for (auto&& [_, SceneTile] : IModelInternals.SceneMapping.KnownTiles)
			{
				if (SceneTile.bNewMeshesToAnimate)
				{
					SceneTile.bNewMeshesToAnimate = false;
					for (auto&& Mat : SceneTile.GetMaterials())
						FITwinSceneMapping::SetForcedOpacity(Mat, -1.f);
				}
			}
		}
		return;
	}
	auto&& ScheduleRange = Owner.GetDateRange();
	// Avoid incrementing time when clicking Play repeatedly at the end of the schedule (positive speeds,
	// also handle reverse playback)
	if (ScheduleRange != FDateRange()
		&&  ((Owner.GetReplaySpeed() > 0. && Owner.ScheduleTime >= ScheduleRange.GetUpperBoundValue())
		  || (Owner.GetReplaySpeed() < 0. && Owner.ScheduleTime <= ScheduleRange.GetLowerBoundValue())))
	{
		Pause();
	}
	if (Impl->bIsPlaying)
	{
		Owner.ScheduleTime += DeltaTime * Owner.GetReplaySpeed();
	}
	if (Impl->bIsPlaying || Impl->bIsPaused)
	{
		Impl->ApplyAnimation(bNewTilesReceived);
	}
}

void FITwinSynchro4DAnimator::Play()
{
	if (!Impl->bIsPlaying)
	{
		Impl->bIsPlaying = true;
		Impl->bIsPaused = false;
	}
}

void FITwinSynchro4DAnimator::Pause()
{
	if (Impl->bIsPlaying)
	{
		Impl->bIsPlaying = false;
	}
	// If already Stop'd, using Pause can be still useful to diplay the animation without changing the time
	Impl->bIsPaused = true;
}

void FITwinSynchro4DAnimator::Stop()
{
	auto&& SchedInternals = GetInternals(Owner);
	if (SchedInternals.PrefetchAllElementAnimationBindings()
		&& !SchedInternals.IsPrefetchedAvailableAndApplied())
	{
		return;
	}
	if (Impl->bIsPlaying)
	{
		Pause();
	}
	if (Impl->bIsPaused)
	{
		Impl->LastAnimationTime.reset();
		Impl->bIsPaused = false;
		auto* IModel = Cast<AITwinIModel>(Owner.GetOwner());
		if (!IModel)
			return;
		FITwinIModelInternals& IModelInternals = GetInternals(*IModel);
		for (auto&& [TileID, SceneTile] : IModelInternals.SceneMapping.KnownTiles)
		{
			if (SceneTile.HighlightsAndOpacities)
				SceneTile.HighlightsAndOpacities->FillWith(S4D_MAT_BGRA_DISABLED(255));
			if (SceneTile.CuttingPlanes)
				SceneTile.CuttingPlanes->FillWith(S4D_CLIPPING_DISABLED);
			SceneTile.ForEachExtractedElement([](FITwinExtractedEntity& Extracted)
				{
					Extracted.SetForcedOpacity(1.f);
					if (Extracted.MeshComponent.IsValid())
						Extracted.MeshComponent->SetWorldTransform(Extracted.OriginalTransform, false,
																   nullptr, ETeleportType::TeleportPhysics);
				});
		}
		//Impl->ApplyAnimation(true); No, this would reset the values above for all animated elements
		IModelInternals.SceneMapping.UpdateAllTextures();
	}
}

void FITwinSynchro4DAnimator::OnChangedScheduleTime(bool const bForceUpdateAll)
{
	auto&& SchedInternals = GetInternals(Owner);
	if (SchedInternals.PrefetchAllElementAnimationBindings()
		&& !SchedInternals.IsPrefetchedAvailableAndApplied())
	{
		return;
	}
	TickAnimation(0.f, bForceUpdateAll);
}

void FITwinSynchro4DAnimator::OnChangedAnimationSpeed()
{
	/*no-op*/
}

void FITwinSynchro4DAnimator::OnChangedScheduleRenderSetting()
{
	auto&& SchedInternals = GetInternals(Owner);
	if (SchedInternals.PrefetchAllElementAnimationBindings()
		&& !SchedInternals.IsPrefetchedAvailableAndApplied())
	{
		return;
	}
	if (Impl->bIsPlaying || Impl->bIsPaused)
		Impl->ApplyAnimation(true);
}

void FITwinSynchro4DAnimator::OnMaskOutNonAnimatedElements()
{
	OnFadeOutNonAnimatedElements();
}

void FITwinSynchro4DAnimator::OnFadeOutNonAnimatedElements()
{
	auto&& SchedInternals = GetInternals(Owner);
	if (SchedInternals.PrefetchAllElementAnimationBindings()
		&& !SchedInternals.IsPrefetchedAvailableAndApplied())
	{
		return;
	}
	FITwinScheduleTimeline const& Timeline = SchedInternals.GetTimeline();
	if (Timeline.GetContainer().empty())
		return;
	auto* IModel = Cast<AITwinIModel>(Owner.GetOwner());
	if (!IModel)
		return;
	bool const bSomethingIsOn = (Owner.bFadeOutNonAnimatedElements || Owner.bMaskOutNonAnimatedElements);
	auto const FillColor = bSomethingIsOn
		? std::array<uint8, 4>({ 32, 32, 32,
								 Owner.bMaskOutNonAnimatedElements ? (uint8)0 : (uint8)255 })
		: std::array<uint8, 4>(S4D_MAT_BGRA_DISABLED(255));
	FITwinIModelInternals& IModelInternals = GetInternals(*IModel);
	std::unordered_set<ITwinElementID> AllAnimatedElements;
	for (auto const& ElementTimelinePtr : Timeline.GetContainer())
	{
		for (auto&& Elem : ElementTimelinePtr->GetIModelElements())
			AllAnimatedElements.insert(Elem);
	}
	for (auto& it: IModelInternals.SceneMapping.KnownTiles)
	{
		auto & TileID = it.first;
		auto & SceneTile= it.second;
		bool bJustCreatedOpaTex = false;
		SceneTile.ForEachElementFeatures(
			[&AllAnimatedElements, &FillColor, &SceneTile, &bJustCreatedOpaTex]
			(FITwinElementFeaturesInTile& ElementFeatures)
			{
				if (AllAnimatedElements.end() == AllAnimatedElements.find(ElementFeatures.ElementID))
				{
					if (!SceneTile.HighlightsAndOpacities)
					{
						bJustCreatedOpaTex = true;
						SceneTile.HighlightsAndOpacities = std::make_unique<FITwinDynamicShadingBGRA8Property>(
							SceneTile.MaxFeatureID, std::array<uint8, 4>(S4D_MAT_BGRA_DISABLED(255)));
					}
					if (bJustCreatedOpaTex)
					{
						FITwinSceneMapping::SetupHighlightsOpacities(SceneTile, ElementFeatures);
					}
					SceneTile.HighlightsAndOpacities->SetPixels(ElementFeatures.Features, FillColor);
				}
			});
		// SceneTile.ExtractedElements share the textures: just set opacity (ExtractedElements may soon
		// originate from material mapping, and not just scheduling? Hence not even testing
		// HighlightsAndOpacities here)
		SceneTile.ForEachExtractedElement(
			[&AllAnimatedElements, &FillColor, &SceneTile](FITwinExtractedEntity& ExtractedElement)
			{
				if (AllAnimatedElements.end() == AllAnimatedElements.find(ExtractedElement.ElementID))
					ExtractedElement.SetForcedOpacity(FillColor[3] / 255.f);
			});
	}
	if (Impl->bIsPlaying || Impl->bIsPaused)
		Impl->ApplyAnimation(true);
	else
		IModelInternals.SceneMapping.UpdateAllTextures();
}

namespace Detail
{
	/// Note: the mapping from [0;1] to [0;255] is not homogenous: only the special value 1.f maps to 255,
	/// and the rest maps linearly to [0;254]
	uint8 ClampCast01toU8(float const v)
	{
		return static_cast<uint8>(255. * std::clamp(v, 0.f, 1.f));
	}

	std::array<uint8, 4> ClampCast01toBGRA8ReplacingDisabled(FVector const& RGBColor, float const Alpha)
	{
		std::array<uint8, 4> ColorBGRA8 =
			{ ClampCast01toU8(RGBColor.Z), ClampCast01toU8(RGBColor.Y), ClampCast01toU8(RGBColor.X),
			  ClampCast01toU8(Alpha) };
		// Note: this is indeed late to do the replacement, that's because the timeline stores the color as
		// a float vector - TODO_GCO keep uint8 all along since 4D animations are actually described with
		// uint8 components too (and transparencies as a percentage!).
		return ITwin::Synchro4D::ReplaceDisabledColorInPlace(ColorBGRA8);
	}

	struct FStateToApply
	{
		FITwinElementTimeline::PropertyOptionals Props;
		FITwinElementTimeline& ElementsTimeline;
		std::function<FBox(std::set<ITwinElementID> const&)> const& GroupBBoxGetter;
		std::function<FBox const&(ITwinElementID const&)> const& BBoxGetter;
		bool bFullyHidden = false;
		/// Color and/or Visibility properties as a packed BGRA value for use in the property texture.
		/// Converted once just-in-time from Props.Color and Props.Visibility.
		std::optional<std::array<uint8, 4>> AsBGRA;
		/// Cutting plane equation property as a packed std::array for use in the property texture.
		/// Converted once just-in-time from Props.ClippingPlane->DefrdPlaneEq members
		std::optional<std::array<float, 4>> AsPlaneEquation;
		std::optional<FTransform> AsTransform;

		/// OK to call whatever Props.Color and Props.Visibility
		void EnsureBGRA()
		{
			if (!AsBGRA)
			{
				float const alpha = Props.Visibility.value_or(1.f).Value;
				if (Props.Color)
				{
					AsBGRA.emplace(ClampCast01toBGRA8ReplacingDisabled(Props.Color->Value, alpha));
				}
				else
				{
					AsBGRA.emplace(std::array<uint8, 4> S4D_MAT_BGRA_DISABLED(ClampCast01toU8(alpha)));
				}
			}
		}

		/// OK to call whatever Props.ClippingPlane
		void EnsurePlaneEquation()
		{
			if (!AsPlaneEquation)
			{
				if (Props.ClippingPlane)
				{
					// In SynchroPro 6.5.4, static task transforms do NOT alter cut plane direction, I'll keep
					// the code in case we need it in the future. Also, 3D paths entirely disable Growth, so we
					// don't even reach this in that case.
					//if (Props.Transform)
					//{
					//	EnsureTransform();
					//	// UE stores a plane as Xx+Yy+Zz=W like us! ;^^
					//	FPlane4f const TransformdPlane =
					//		FPlane4f(Props.ClippingPlane->DefrdPlaneEq.PlaneOrientation,
					//				 (double)Props.ClippingPlane->DefrdPlaneEq.PlaneW)
					//		.TransformBy(FMatrix44f(AsTransform->ToMatrixWithScale()));
					//	AsPlaneEquation.emplace(std::array<float, 4>
					//		{ TransformdPlane.X, TransformdPlane.Y, TransformdPlane.Z, TransformdPlane.W });
					//}
					//else
					{
						auto const& PlaneDir(Props.ClippingPlane->DefrdPlaneEq.PlaneOrientation);
						AsPlaneEquation.emplace(std::array<float, 4>
							// see comment about ordering on FITwinSceneTile::CuttingPlanes
							{ PlaneDir.X, PlaneDir.Y, PlaneDir.Z, Props.ClippingPlane->DefrdPlaneEq.PlaneW });
					}
				}
				else
					AsPlaneEquation.emplace(std::array<float, 4> S4D_CLIPPING_DISABLED);
			}
		}

		void EnsureTransform()
		{
			if (!AsTransform && Props.Transform)
			{
				// For 'Original [Position]' anchoring, Keyframes simply store relative translations.
				bool const bPositionIsRelative =
					ITwin::Timeline::EAnchorPoint::Original == Props.Transform->DefrdAnchor.AnchorPoint
					|| ITwin::Timeline::EAnchorPoint::Static == Props.Transform->DefrdAnchor.AnchorPoint;
				// Location of the Element's reference point (origin of its local CRS) is unknown, since
				// the local CRS is lost when Elements are merged into the Gltf meshes by the Mesh export!
				// The only case where it is needed is when rotating a single Element using the 'Original'
				// anchor, in which case the Element's origin should be used instead of the group's BBox
				// center (I guess? TODO_GCO) - and there is no offset in that case.
				FVector const ElemGroupAnchor =
					ElementsTimeline.GetIModelElementsBBox(GroupBBoxGetter).GetCenter()
						- Props.Transform->DefrdAnchor.Offset;
				AsTransform.emplace(FTransform(-ElemGroupAnchor)
					* FTransform(Props.Transform->Rotation)
					* (bPositionIsRelative
						? FTransform(ElemGroupAnchor + Props.Transform->Position)
						: FTransform(Props.Transform->Position)));
			}
		}
	};

	void UpdateExtractedElement(FStateToApply& State, FITwinSceneTile& SceneTile,
								FITwinExtractedEntity& ExtractedEntity)
	{
		if (!ExtractedEntity.IsValid()) // checks both Material and MeshComponent
			return;
		ExtractedEntity.SetHidden(State.bFullyHidden);
		if (State.bFullyHidden)
			return;

		// Note: color and cutting plane need no processing here, as long as the extracted elements
		// use the same material and textures as the batched meshes. Alpha must be set on the material
		// parameter that is used to override the texture look-up for extracted elements, though:
		//State.EnsureBGRA(); NOT needed, the single float value is exactly what we need!
		ExtractedEntity.SetForcedOpacity(State.Props.Visibility.value_or(1.f).Value);
	#if SYNCHRO4D_ENABLE_TRANSFORMATIONS()
		if (State.Props.Transform)
		{
			State.EnsureTransform();
			ExtractedEntity.MeshComponent->SetWorldTransform(
				ExtractedEntity.OriginalTransform * (*State.AsTransform),
				false, nullptr, ETeleportType::TeleportPhysics);
			// TODO_GCO: could optimize the static-transform case, with a static transform ID (not a hash...)
			ExtractedEntity.bIsCurrentlyTransformed = true;
		}
		else if (ExtractedEntity.bIsCurrentlyTransformed)
		{
			ExtractedEntity.bIsCurrentlyTransformed = false;
			ExtractedEntity.MeshComponent->SetWorldTransform(ExtractedEntity.OriginalTransform,
															 false, nullptr, ETeleportType::TeleportPhysics);
		}
	#endif // SYNCHRO4D_ENABLE_TRANSFORMATIONS()
		FITwinSceneMapping::SetupHighlightsOpacities(SceneTile, ExtractedEntity);
		FITwinSceneMapping::SetupCuttingPlanes(SceneTile, ExtractedEntity);
	}

	void UpdateBatchedElement(FStateToApply& State, FITwinSceneTile& SceneTile,
							  FITwinElementFeaturesInTile& ElementFeaturesInTile)
	{
		if (State.bFullyHidden)
		{
			if (SceneTile.HighlightsAndOpacities)
				SceneTile.HighlightsAndOpacities->SetPixelsAlpha(ElementFeaturesInTile.Features, 0);
		}
		else
		{
			if (SceneTile.HighlightsAndOpacities)
			{
				State.EnsureBGRA();
				std::array<uint8, 4> PixelValue = *State.AsBGRA;
				if (ElementFeaturesInTile.bIsElementExtracted)
				{
					// Ensure the parts that were extracted are made invisible in the original mesh
					// (alpha is already zeroed in FITwinSceneMapping::OnElementsTimelineModified,
					// but here we still need to set the BGR part for the extracted mesh coloring)
					PixelValue[3] = 0;
				}
				SceneTile.HighlightsAndOpacities->SetPixels(ElementFeaturesInTile.Features, PixelValue);
			}
			if (SceneTile.CuttingPlanes)
			{
				State.EnsurePlaneEquation();
				SceneTile.CuttingPlanes->SetPixels(ElementFeaturesInTile.Features, *State.AsPlaneEquation);
			}
		}
		FITwinSceneMapping::SetupHighlightsOpacities(SceneTile, ElementFeaturesInTile);
		FITwinSceneMapping::SetupCuttingPlanes(SceneTile, ElementFeaturesInTile);
	}

	void ForceHideBatchedElement(FITwinSceneTile& SceneTile,
								 FITwinElementFeaturesInTile& ElementFeaturesInTile)
	{
		if (SceneTile.HighlightsAndOpacities)
			SceneTile.HighlightsAndOpacities->SetPixelsAlpha(ElementFeaturesInTile.Features, 0);
		FITwinSceneMapping::SetupHighlightsOpacities(SceneTile, ElementFeaturesInTile);
	}

	struct FFinalizeDeferredPropData
	{
		FITwinIModelInternals& IModelInternals;
		FITwinElementTimeline& ElementsTimeline;
	};

	constexpr float HiddenBelowAlpha = 0.04f;
	constexpr float OpaqueAboveAlpha = 0.96f;

} // ns Detail

namespace ITwin::Timeline::Interpolators
{
	void AnchorPosFinalizer(::Detail::FFinalizeDeferredPropData& UserData,
							ITwin::Timeline::FDeferredAnchor const& Deferred);
	void PlaneEquationFinalizer(::Detail::FFinalizeDeferredPropData& UserData,
								ITwin::Timeline::FDeferredPlaneEquation const& Deferred);
}

void FITwinSynchro4DAnimator::FImpl::ApplyAnimation(bool const bForceUpdateAll)
{
	auto const& Schedules = Owner.Owner;
	auto&& SchedInternals = GetInternals(Schedules);
	if (SchedInternals.PrefetchAllElementAnimationBindings()
		&& !SchedInternals.IsPrefetchedAvailableAndApplied())
	{
		ensure(false); return;
	}
	//bool const bNeedCheckForElementExtraction = !SchedInternals.PrefetchAllElementAnimationBindings();
	FITwinScheduleTimeline const& Timeline = SchedInternals.GetTimeline();
	AITwinIModel* IModel = Cast<AITwinIModel>(Schedules.GetOwner());
	if (!IModel || Timeline.GetContainer().empty())
		return;

	FITwinIModelInternals& IModelInternals = GetInternals(*IModel);
	bool bHasUpdatedTextures = false;
	if (IModelInternals.SceneMapping.NewTilesReceivedHaveTextures(bHasUpdatedTextures))
	{
		// restart from scratch
		LastAnimationTime.reset();
		NextTimelineToUpdate = 0;
		TimeToApplyAllTimelines = 0;
		if (ITwin::Synchro4D::s_bMaskTilesUntilFullyAnimated)
		{
			for (auto& SceneTile : HasNewMeshesToAnimate)
			{
				SceneTile->bNewMeshesToAnimate = true;
			}
			HasNewMeshesToAnimate.clear();
		}
		if (bHasUpdatedTextures)
		{
			// Don't do UpdateInMaterials in same tick :/ but nothing guarantees that the render thread will
			// have processed our UpdateTexture messages before next tick, right?
			// TODO_GCO: sync with UpdateTextureRegions's DataCleanup functor?
			//	if so, please also update FITwinSceneTile::UpdateSelectionTextureInMaterials, which currently
			//	makes the same assumptions...
			// Because of the early exit, we need the next call to TickAnimation to enter ApplyAnimation even
			// when paused *and* NextTimelineToUpdate == 0, to finish the rest of the job!
			// Since we have reset LastAnimationTime anyway, let's set this flag:
			bNeedUpdateAll = true;
			return;
		}
	}
	IModelInternals.SceneMapping.HandleNewTileTexturesNeedUpateInMaterials();

	if (NextTimelineToUpdate == 0)
	{
		AnimationTime = ITwin::Time::FromDateTime(Schedules.ScheduleTime);
		// As a precaution, don't start applying animation as long as some textures are dirty.
		// "Useful"(is it?) only the first time, except when new timelines are added
		if (IModelInternals.SceneMapping.UpdateAllTextures() != 0)
		{
			// Because of the early exit, we need the next call to TickAnimation to enter ApplyAnimation even
			// when paused *and* NextTimelineToUpdate == 0, to finish the rest of the job!
			bNeedUpdateAll = true;
			UE_LOG(LogITwin, Verbose, TEXT("Skipping ApplyAnimation (dirty textures)"));
			return;
		}
	}

	if (LastAnimationTime
		&& std::abs(ITwin::Time::FromDateTime(Schedules.ScheduleTime) - (*LastAnimationTime)) < 0.01//seconds
		&& !bForceUpdateAll && !bNeedUpdateAll && NextTimelineToUpdate == 0)
	{
		return;
	}
	auto const& GroupBBoxGetter = std::bind(&FITwinIModelInternals::GetBoundingBox, &IModelInternals,
											std::placeholders::_1);
	auto const& BBoxGetter = std::bind(&FITwinSceneMapping::GetBoundingBox, &IModelInternals.SceneMapping,
										std::placeholders::_1);
	std::optional<std::pair<double const&, double const&>> TimeIncrement;
	if (bForceUpdateAll)
	{
		if (bNeedUpdateAll)
			bNeedUpdateAllAgain = true;
		else
			bNeedUpdateAll = true;
	}
	if (!LastAnimationTime)
		bNeedUpdateAll = true;
	double const TimelineUpdateEnd = // store time before handling masking of duplicates in condition below
		FPlatformTime::Seconds() + (Schedules.MaxTimelineUpdateMilliseconds / 1000.);
	if (bNeedUpdateAll)
	{
		if (NextTimelineToUpdate == 0)
		{
			IModelInternals.ProcessElementsInEachTile(Timeline.GetNonAnimatedDuplicates(),
				std::bind(&::Detail::ForceHideBatchedElement, std::placeholders::_2, std::placeholders::_3),
				[](FITwinSceneTile&, FITwinExtractedEntity&) {}, false);
		}
	}
	else
	{
		TimeIncrement.emplace(std::minmax(*LastAnimationTime, AnimationTime));
	}
	if (NextTimelineToUpdate == 0 && ITwin::Synchro4D::s_bMaskTilesUntilFullyAnimated)
	{
		for (auto&& [_, SceneTile] : IModelInternals.SceneMapping.KnownTiles)
		{
			if (SceneTile.bNewMeshesToAnimate)
			{
				SceneTile.bNewMeshesToAnimate = false;
				HasNewMeshesToAnimate.insert(&SceneTile);
			}
		}
	}
	//if (!DebugElem) DebugElem.emplace(94557999988851ULL);
	auto&& AllTimelines = Timeline.GetContainer();
	size_t const FirstTimelineUpdated = NextTimelineToUpdate;
	size_t const NumberOfTimelines = AllTimelines.size();
	for ( ; NextTimelineToUpdate < NumberOfTimelines; ++NextTimelineToUpdate)
	{
		const auto& ElementTimelinePtr = AllTimelines[NextTimelineToUpdate];
		const auto TimelineRange = ElementTimelinePtr->GetTimeRange();
		// After a timeline has been applied once(*), this is a good optim as most timelines correspond to
		// tasks which duration is rather small with respect to the whole animation. Note that a hack like
		// FixColor (see Timeline.cpp) would rather spoil this!
		// (*) and not "modified" since, like adding Elements to existing (grouped Elements) timelines.
		//     "Modified" used to also include discovering new tiles using known Elements, but for that we
		//     now restart from scratch anyway (see bNewTilesReceivedHaveTextures above)
		// If we want to skip hidden tiles later on, we'll have to handle newly visible tiles here too.
		if (!ElementTimelinePtr->TestModifiedAndResetFlag() && TimeIncrement
			&& (TimelineRange.second < TimeIncrement->first || TimelineRange.first > TimeIncrement->second))
		{
			continue;
		}
		bHasUpdatedSomething = true;
		using namespace ITwin::Timeline;
		::Detail::FFinalizeDeferredPropData UserData{ IModelInternals, *ElementTimelinePtr };
		// 'State' contains std::optional's of each Timeline property (see ElementStateBase example in
		// Timeline/Definition.h)
		::Detail::FStateToApply StateToApply{
			ElementTimelinePtr->GetStateAtTime(AnimationTime,
											   StateAtEntryTimeBehavior::UseLeftInterval, (void*)(&UserData))
			, *ElementTimelinePtr, GroupBBoxGetter, BBoxGetter
		};
		auto& State = StateToApply.Props;
		auto const& TimelineElems = ElementTimelinePtr->GetIModelElements();
		// Apply (debug) settings_ and property simplifications:
		if (State.Color && (Schedules.bDisableColoring || !State.Color->bHasColor))
		{
			State.Color.reset();
		}
		bool bNeedTranslucentMat = false, bNeedTransformable = false;
		if (State.Visibility)
		{
			// No, this would apply to extracted elements as well...or split StateToApply.bFullyHidden
			// into Extracted- and Batched-specific flags.
			/*if (IModelInternals.SceneMapping.IsElementExtracted(ElementTimelinePtr->IModelElementID))
				StateToApply.bFullyHidden = true;
			else*/
			if (Schedules.bDisableVisibilities)
				State.Visibility.reset();
			else
			{
				if (State.Visibility->Value <= ::Detail::HiddenBelowAlpha)
					StateToApply.bFullyHidden = true;
				else if (State.Visibility->Value < ::Detail::OpaqueAboveAlpha)
				{
					bNeedTranslucentMat = true;
				}
			}
		}
		if (State.Transform)
		{
			if (!State.Transform->bIsTransformed || Schedules.bDisableTransforms)
			{
				State.Transform.reset();
			}
			else
			{
				bNeedTransformable = true;
				// Case of a non-interpolated keyframe, need to call "finalizers" now (see same comment below)
				if (State.Transform->DefrdAnchor.IsDeferred())
					Interpolators::AnchorPosFinalizer(UserData, State.Transform->DefrdAnchor);
			}
		}
		// Actually we always need to check for Element extraction, unless we call OnElementsTimelineModified
		// for new tiles in HandleReceivedElements like I had done at first but later removed
		if (/*bNeedCheckForElementExtraction &&*/ (bNeedTranslucentMat || bNeedTransformable))
		{
			IModelInternals.SceneMapping.CheckAndExtractElements(TimelineElems,
																 bNeedTranslucentMat, bNeedTransformable);
		}
		if (State.ClippingPlane)
		{
			// Case of a non-interpolated keyframe, need to call "finalizers" now: it should rather be
			// in PropertyTimeline<_PropertyValues>::GetStateAtTime, but it would mean going through all
			// the boost fusion mishmash just for this:
			if (State.ClippingPlane->DefrdPlaneEq.IsDeferred())
			{
				Interpolators::PlaneEquationFinalizer(UserData, State.ClippingPlane->DefrdPlaneEq);
			}
			// At this point, only non-Deferred states remain possible
			if (EGrowthStatus::FullyGrown == State.ClippingPlane->DefrdPlaneEq.GrowthStatus
				|| Schedules.bDisableCuttingPlanes
				// In SynchroPro 6.5.4, static task transforms do NOT alter cut plane direction, and 3D paths
				// entirely disable Growth, let's do it here:
				|| (bNeedTransformable && EAnchorPoint::Static != State.Transform->DefrdAnchor.AnchorPoint))
			{
				State.ClippingPlane.reset();
			}
			else if (EGrowthStatus::FullyRemoved == State.ClippingPlane->DefrdPlaneEq.GrowthStatus)
			{
				StateToApply.bFullyHidden = true;
			}
		}
		//if (DebugElem && TimelineElems.find(*DebugElem) != TimelineElems.end())
		//{
		//	UE_LOG(LogITwin, Display, TEXT("ANIM %s CLR %d VIZ %.2f CUT %d TRSF %d"),
		//		*ITwin::ToString(*DebugElem), State.Color ? 1 : 0,
		//		State.Visibility ? State.Visibility->Value : 1.f,
		//		State.ClippingPlane ? 1 : 0, State.Transform ? 1 : 0);
		//}
		IModelInternals.ProcessElementsInEachTile(TimelineElems,
			std::bind(&::Detail::UpdateBatchedElement, std::ref(StateToApply),
					  std::placeholders::_2, std::placeholders::_3),
			std::bind(&::Detail::UpdateExtractedElement, std::ref(StateToApply),
					  std::placeholders::_1, std::placeholders::_2),
			// bVisibleOnly: cannot use this optim for the moment because LOD changes will not trigger a
			// call of ApplyAnimation
			false);
		if (FPlatformTime::Seconds() >= TimelineUpdateEnd)
		{
			++NextTimelineToUpdate;
			break;
		}
	}
	double const LoopTime = FPlatformTime::Seconds()
		- (TimelineUpdateEnd - Schedules.MaxTimelineUpdateMilliseconds / 1000.);
	UE_LOG(LogITwin, VeryVerbose, TEXT("Spent %dms applying animation for %llu timelines"),
		(int)std::round(1000 * LoopTime), NextTimelineToUpdate - FirstTimelineUpdated);
	TimeToApplyAllTimelines += LoopTime;
	if (NextTimelineToUpdate >= NumberOfTimelines)
	{
		UE_LOG(LogITwin, Verbose, TEXT("Spent a total of %dms applying all %llu timelines"),
			(int)std::round(1000 * TimeToApplyAllTimelines), NumberOfTimelines);
		if (bHasUpdatedSomething)
		{
			IModelInternals.SceneMapping.UpdateAllTextures();
			if (ITwin::Synchro4D::s_bMaskTilesUntilFullyAnimated)
			{
				for (auto& SceneTile : HasNewMeshesToAnimate)
				{
					if (SceneTile->bNewMeshesToAnimate) // was reset in the meantime => do not show tile!
						continue;
					for (auto&& Mat : SceneTile->GetMaterials())
						FITwinSceneMapping::SetForcedOpacity(Mat, -1.f);
				}
				HasNewMeshesToAnimate.clear();
			}
		}
		bHasUpdatedSomething = false;
		if (bNeedUpdateAllAgain)
		{
			bNeedUpdateAllAgain = false;
			bNeedUpdateAll = true;
		}
		else
			bNeedUpdateAll = false;
		NextTimelineToUpdate = 0;
		TimeToApplyAllTimelines = 0;
		// see comment at start of method - use the most conservative value "common" to all timelines, even
		// if some timelines were applied at a more recent time
		LastAnimationTime = AnimationTime;
	}
}

namespace ITwin::Timeline::Interpolators {

//---------------------------------------------------------------------------------------
// Slightly less-than-basic interpolators
//---------------------------------------------------------------------------------------

template<> inline FContinue Default::operator ()<FTransform>(
	FTransform& Out, FTransform const& x0, FTransform const& x1, float u, void* /*userData*/) const
{
	Out.Blend(x0, x1, u);
	return Continue;
}

template<> inline FContinue Default::operator ()<ITwin::Flag::FPresence>(
	ITwin::Flag::FPresence& Out, ITwin::Flag::FPresence const& x0, ITwin::Flag::FPresence const& x1,
	float u, void* /*userData*/) const
{
	Out = (u == 0) ? x0 : ((u == 1) ? x1 : (x0 | x1));
	return (Out == ITwin::Flag::Present) ? Continue : Stop; // skip other properties when Absent
}

//---------------------------------------------------------------------------------------
// A couple utility functions
//---------------------------------------------------------------------------------------

/// This is exactly UE::Math::TVector<T>::SlerpNormals but with proper const ref for params!!
template<typename T>
UE::Math::TVector<T> ConstQualSlerpNormals(UE::Math::TVector<T> const& NormalA,
										   UE::Math::TVector<T> const& NormalB, T const Alpha)
{
	using TVector = UE::Math::TVector<T>;
	using TQuat = UE::Math::TQuat<T>;

	// Find rotation from A to B
	const TQuat RotationQuat = TQuat::FindBetweenNormals(NormalA, NormalB);
	const TVector Axis = RotationQuat.GetRotationAxis();
	const T AngleRads = RotationQuat.GetAngle();

	// Rotate from A toward B using portion of the angle specified by Alpha.
	const TQuat DeltaQuat(Axis, AngleRads * Alpha);
	TVector Result = DeltaQuat.RotateVector(NormalA);
	return Result;
}

template<typename FDefrdProp>
void FinalizeDeferredProperty(::Detail::FFinalizeDeferredPropData& UserData, FDefrdProp const& Deferred,
	std::function<void(FDefrdProp const&, FBox const&)> const& Finalizer, FString const& PropName)
{
	if (Deferred.IsDeferred())
	{
		auto&& IModelElements = UserData.ElementsTimeline.GetIModelElements();
		auto const& IModelElementsBBox = UserData.ElementsTimeline.GetIModelElementsBBox(
			std::bind(&FITwinIModelInternals::GetBoundingBox, &UserData.IModelInternals,
					  std::placeholders::_1));
		UE_LOG(LogITwin, Verbose, TEXT("Setting up %s for %s with BBox %s"), *PropName,
			IModelElements.size() == 1
				? (*FString::Printf(TEXT("Element 0x%I64x"), IModelElements.begin()->value()))
				: (*FString::Printf(TEXT("%llu Elements"), IModelElements.size())),
			*IModelElementsBBox.ToString());
		Finalizer(Deferred, IModelElementsBBox);
	}
}

//---------------------------------------------------------------------------------------
// FDeferredAnchor interpolation and "finalizer"
//---------------------------------------------------------------------------------------

using FDefrdAnchorRot = ITwin::Timeline::FDeferredAnchor;

void AnchorPosFinalizer(::Detail::FFinalizeDeferredPropData& UserData, FDefrdAnchorRot const& Deferred)
{
	FinalizeDeferredProperty<FDefrdAnchorRot>(UserData, Deferred,
		&FITwinSynchro4DSchedulesInternals::FinalizeAnchorPos, TEXT("AnchorPos"));
}

template<> FContinue Default::operator ()<FDefrdAnchorRot>(
	FDefrdAnchorRot& Out, const FDefrdAnchorRot& x0, const FDefrdAnchorRot& x1, float u, void* userData) const
{
	auto& UserDataForDeferredCalc = *(::Detail::FFinalizeDeferredPropData*)userData;
	AnchorPosFinalizer(UserDataForDeferredCalc, x0);
	//AnchorPosFinalizer(UserDataForDeferredCalc, x1); 'Finalizer' only affects Offset, which could be shared,
	// but is not because each keyframe has its rotation and we need both to interpolate the offset, so:
	if (x1.IsDeferred())
	{
		x1.Offset = x0.Offset;
		x1.bDeferred = false;
	}
	Out = x0;
	return Continue;
}

//---------------------------------------------------------------------------------------
// FDeferredPlaneEquation interpolation and "finalizer"
//---------------------------------------------------------------------------------------

/*  We could optimize the case when cut planes and transforms have keyframes at the same times (ie. in
	the case of a static transform), by interpolating directly the transformed plane equations. But we
	would need to store the Transforms and their Times in userData, but also handle the case of non-inter-
	polated keyframes, which is currently handled after all interpolations are done! It's probably more
	trouble than it's worth, so for the moment we'll recompute the cut plane at each tick whenever there
	is a transform, be it static or following a 3D path.
*/

using FDefrdPlaneEq = ITwin::Timeline::FDeferredPlaneEquation;

void PlaneEquationFinalizer(::Detail::FFinalizeDeferredPropData& UserData,
							FDefrdPlaneEq const& Deferred)
{
	FinalizeDeferredProperty<FDefrdPlaneEq>(UserData, Deferred,
		&FITwinSynchro4DSchedulesInternals::FinalizeCuttingPlaneEquation, TEXT("Cutting Plane"));
}

template<> FContinue Default::operator ()<FDefrdPlaneEq>(
	FDefrdPlaneEq& Out, const FDefrdPlaneEq& x0, const FDefrdPlaneEq& x1, float u, void* userData) const
{
	auto& UserDataForDeferredCalc = *(::Detail::FFinalizeDeferredPropData*)userData;
	PlaneEquationFinalizer(UserDataForDeferredCalc, x0);
	PlaneEquationFinalizer(UserDataForDeferredCalc, x1);
	ensure(!x0.IsDeferred() && !x1.IsDeferred()
		&& x0.PlaneOrientation.IsUnit() && x1.PlaneOrientation.IsUnit());
	if (x0.GrowthStatus == x1.GrowthStatus && (x0.GrowthStatus == EGrowthStatus::FullyGrown
											|| x0.GrowthStatus == EGrowthStatus::FullyRemoved))
	{
		// avoid useless interpolation below - this is called from GetStateAtTime, unrelated to the other
		// optims of the ElementTimelinePtr loop in ApplyAnimation!
		return Stop;
	}
	if ((x0.PlaneOrientation - x1.PlaneOrientation).IsNearlyZero()) // ie nearly equal
	{
		Out = FDefrdPlaneEq{ x0.PlaneOrientation, Lerp(x0.PlaneW, x1.PlaneW, u),
							 ITwin::Timeline::EGrowthStatus::Partial };
	}
	else if ((x0.PlaneOrientation + x1.PlaneOrientation).IsNearlyZero()) // ie nearly opposite
	{
		Out = FDefrdPlaneEq{ x0.PlaneOrientation, Lerp(x0.PlaneW, -x1.PlaneW, u),
							 ITwin::Timeline::EGrowthStatus::Partial };
	}
	else
	{
		Out = FDefrdPlaneEq{ ConstQualSlerpNormals(x0.PlaneOrientation, x1.PlaneOrientation, u),
							 Lerp(x0.PlaneW, x1.PlaneW, u),
							 ITwin::Timeline::EGrowthStatus::Partial };
	}
	return Continue;
}

} // namespace ITwin::Timeline::Interpolators
