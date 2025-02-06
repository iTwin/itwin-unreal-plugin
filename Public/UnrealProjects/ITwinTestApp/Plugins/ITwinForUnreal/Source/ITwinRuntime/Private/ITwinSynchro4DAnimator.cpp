/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSynchro4DAnimator.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
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

namespace {

class FIModelInvariants
{
public:
	FITwinIModelInternals& Internals;
	std::function<FBox(FElementsGroup const&)> const GroupBBoxGetter;
	std::function<FBox const& (ITwinElementID const&)> const BBoxGetter;

	FIModelInvariants(AITwinIModel& IModel)
		: Internals(GetInternals(IModel))
		, GroupBBoxGetter(std::bind(&FITwinIModelInternals::GetBoundingBox, &Internals,
																			std::placeholders::_1))
		, BBoxGetter(std::bind(&FITwinSceneMapping::GetBoundingBox, &Internals.SceneMapping,
																	std::placeholders::_1))
	{
	}
};

} // anon. ns

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
	double TotalExtractionTime = 0.;

	std::optional<ITwinElementID> DebugElem;
	std::optional<FIModelInvariants> IModelInvariants;

	void ApplyAnimation(bool const bForceUpdateAll);
	void ApplyTimeline(FITwinElementTimeline& Timeline,
					   std::optional<std::pair<double const&, double const&>> const& TimeIncrement,
					   std::optional<ITwinScene::TileIdx> OnlySceneTile, bool const bOnlyVisibleTiles);
	/// \param OnlyThisTile If nullptr, stop animation in all tiles
	void StopAnimationInTiles(FITwinSceneTile* OnlyThisTile = nullptr);

public:
	FImpl(FITwinSynchro4DAnimator& InOwner) : Owner(InOwner) {}
};

FITwinSynchro4DAnimator::FITwinSynchro4DAnimator(UITwinSynchro4DSchedules& InOwner)
	: Owner(InOwner)
	, Impl(MakePimpl<FImpl>(*this))
{
}

void FITwinSynchro4DAnimator::TickAnimation(float DeltaTime, bool const bForceUpdateAll)
{
	auto&& SchedInternals = GetInternals(Owner);
	if (SchedInternals.PrefetchAllElementAnimationBindings()
		&& !SchedInternals.IsPrefetchedAvailableAndApplied())
	{
		ensure(false); return;
	}
	if (!Impl->bIsPlaying && !Impl->bIsPaused)
	{
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
		Impl->ApplyAnimation(bForceUpdateAll);
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
	// If already Stop'd, using Pause can be still useful to redisplay the animation without changing the
	// current ScheduleTime
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
		Impl->StopAnimationInTiles();
	}
}

void FITwinSynchro4DAnimator::FImpl::StopAnimationInTiles(FITwinSceneTile* OnlyThisTile/*= nullptr*/)
{
	auto* IModel = Cast<AITwinIModel>(Owner.Owner.GetOwner());
	if (!IModel)
		return;
	auto&& SchedInternals = GetInternals(Owner.Owner);
	auto const& NonAnimatedDuplicates = SchedInternals.GetTimeline().GetNonAnimatedDuplicates();
	auto const& StopAnimForTile = [&SchedInternals, &NonAnimatedDuplicates](FITwinSceneTile& SceneTile)
		{
			if (SceneTile.HighlightsAndOpacities)
				SceneTile.HighlightsAndOpacities->FillWith(S4D_MAT_BGRA_DISABLED(255));
			if (SceneTile.CuttingPlanes)
				SceneTile.CuttingPlanes->FillWith(S4D_CLIPPING_DISABLED);
			SceneTile.ForEachExtractedEntity([](FITwinExtractedEntity& Extracted)
				{
					Extracted.SetForcedOpacity(1.f);
					if (Extracted.MeshComponent.IsValid())
						Extracted.MeshComponent->SetWorldTransform(Extracted.OriginalTransform, false,
							nullptr, ETeleportType::TeleportPhysics);
				});
			SchedInternals.HideNonAnimatedDuplicates(SceneTile, NonAnimatedDuplicates);
		};
	FITwinIModelInternals& IModelInternals = GetInternals(*IModel);
	if (OnlyThisTile)
		StopAnimForTile(*OnlyThisTile);
	else
		IModelInternals.SceneMapping.ForEachKnownTile(StopAnimForTile);
	IModelInternals.SceneMapping.Update4DAnimTextures();
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
	auto const& NonAnimatedDuplicates = Timeline.GetNonAnimatedDuplicates();
	IModelInternals.SceneMapping.ForEachKnownTile(
		[&IModelInternals, &FillColor, &SchedInternals, &NonAnimatedDuplicates,
			bNeedHideNonAnimDupl = (!Owner.bMaskOutNonAnimatedElements)]
		(FITwinSceneTile& SceneTile)
	{
		bool bJustCreatedOpaTex = false;
		SceneTile.ForEachElementFeatures(
			[&IModelInternals, &FillColor, &SceneTile, &bJustCreatedOpaTex]
			(FITwinElementFeaturesInTile& ElementFeatures)
			{
				if (IModelInternals.SceneMapping.ElementFor(ElementFeatures.SceneRank).AnimationKeys.empty())
				{
					if (!SceneTile.HighlightsAndOpacities)
					{
						bJustCreatedOpaTex = true;
						IModelInternals.SceneMapping.CreateHighlightsAndOpacitiesTexture(SceneTile);
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
		SceneTile.ForEachExtractedEntity(
			[&IModelInternals, &FillColor, &SceneTile](FITwinExtractedEntity& ExtractedElement)
			{
				if (IModelInternals.SceneMapping.ElementForSLOW(ExtractedElement.ElementID)
					.AnimationKeys.empty())
				{
					ExtractedElement.SetForcedOpacity(FillColor[3] / 255.f);
				}
			});
		if (bNeedHideNonAnimDupl)
			SchedInternals.HideNonAnimatedDuplicates(SceneTile, NonAnimatedDuplicates);
	});
	if (Impl->bIsPlaying || Impl->bIsPaused)
		Impl->ApplyAnimation(true);
	else
	{
		bool bWaitingForTextures = false;
		// we don't need the value set into bWaitingForTextures, as long as the return value is true, it means
		// Update4DAnimTextures has been called already
		if (!IModelInternals.SceneMapping.TilesHaveNew4DAnimTextures(bWaitingForTextures))
			IModelInternals.SceneMapping.Update4DAnimTextures();
	}
}

namespace Detail
{
	/// Note: the mapping from [0;1] to [0;255] is not homogenous: only the special value 1.f maps to 255,
	/// and the rest maps linearly to [0;254]
	static uint8 ClampCast01toU8(float const v)
	{
		return static_cast<uint8>(255. * std::clamp(v, 0.f, 1.f));
	}

	static std::array<uint8, 4> ClampCast01toBGRA8ReplacingDisabled(FVector const& RGBColor, float const Alpha)
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
		FIModelInvariants const& IModelInvariants;
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
				AsTransform.emplace(FITwinSynchro4DSchedulesInternals::ComputeTransformFromFinalizedKeyframe(
					IModelInvariants.Internals.SceneMapping.GetIModel2UnrealCoordConv(), *Props.Transform,
					ElementsTimeline.GetIModelElementsBBox(IModelInvariants.GroupBBoxGetter).GetCenter(),
					/*bWantsResultAsIfIModelUntransformed*/false));
			}
		}
	};

	static void UpdateExtractedElement(FStateToApply& State, FITwinSceneTile& SceneTile,
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

	static void UpdateBatchedElement(FStateToApply& State, FITwinSceneTile& SceneTile,
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
	static void AnchorPosFinalizer(::Detail::FFinalizeDeferredPropData& UserData,
								   ITwin::Timeline::FDeferredAnchor const& Deferred);
	static void PlaneEquationFinalizer(::Detail::FFinalizeDeferredPropData& UserData,
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
	FITwinScheduleTimeline const& Timeline = SchedInternals.GetTimeline();
	AITwinIModel* IModel = Cast<AITwinIModel>(Schedules.GetOwner());
	if (!IModel || Timeline.GetContainer().empty())
		return;

	if (!IModelInvariants)
	{
		IModelInvariants.emplace(*IModel);
	}
	bool bWaitingForTextures = false;
	if (IModelInvariants->Internals.SceneMapping.TilesHaveNew4DAnimTextures(bWaitingForTextures))
	{
		// restart from scratch
		LastAnimationTime.reset();
		NextTimelineToUpdate = 0;
		TimeToApplyAllTimelines = 0;
		if (bWaitingForTextures)
		{
			// Don't do SetupInMaterials in same tick (in fact we will wait until the render thread has
			// finished processing our UpdateTexture messages, using a sync with UpdateTextureRegions's
			// DataCleanup functor - see details in ITwinDynamicShadingProperty.cpp)
			// Because of the early exit, we need the next call to TickAnimation to enter ApplyAnimation even
			// when paused *and* NextTimelineToUpdate == 0, to finish the rest of the job!
			// Since we have reset LastAnimationTime anyway, let's set this flag:
			bNeedUpdateAll = true;
			return;
		}
	}
	IModelInvariants->Internals.SceneMapping.HandleNew4DAnimTexturesNeedingSetupInMaterials();

	double const StartAnim = FPlatformTime::Seconds();
	double LastStepTime = StartAnim;
	double const TimelineUpdateEnd = StartAnim + (Schedules.MaxTimelineUpdateMilliseconds / 1000.);
	if (NextTimelineToUpdate == 0)
	{
		AnimationTime = ITwin::Time::FromDateTime(Schedules.ScheduleTime);
	}

	if (LastAnimationTime
		&& std::abs(ITwin::Time::FromDateTime(Schedules.ScheduleTime) - (*LastAnimationTime)) < 0.01//seconds
		&& !bForceUpdateAll && !bNeedUpdateAll && NextTimelineToUpdate == 0)
	{
		return;
	}
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
	if (!bNeedUpdateAll)
		TimeIncrement.emplace(std::minmax(*LastAnimationTime, AnimationTime));
	//if (!DebugElem) DebugElem.emplace(94557999988851ULL);
	//if (!DebugElem) DebugElem.emplace();
	//*DebugElem = IModelInvariants->Internals.SceneMapping.GetSelectedElement();
	auto&& AllTimelines = Timeline.GetContainer();
	size_t const FirstTimelineUpdated = NextTimelineToUpdate;
	size_t const NumberOfTimelines = AllTimelines.size();
	for ( ; NextTimelineToUpdate < NumberOfTimelines; ++NextTimelineToUpdate)
	{
		ApplyTimeline(*AllTimelines[NextTimelineToUpdate], TimeIncrement, {}, /*bOnlyVisibleTiles*/true);
		if (FPlatformTime::Seconds() >= TimelineUpdateEnd)
		{
			++NextTimelineToUpdate;
			break;
		}
	}
	double const LoopTime = FPlatformTime::Seconds()
		- (TimelineUpdateEnd - Schedules.MaxTimelineUpdateMilliseconds / 1000.);
	if (LoopTime > 0.1)
	{
		UE_LOG(LogITwin, Verbose, TEXT("Spent %dms applying animation for %llu timelines"),
			(int)std::round(1000 * LoopTime), NextTimelineToUpdate - FirstTimelineUpdated);
	}
	TimeToApplyAllTimelines += LoopTime;
	if (NextTimelineToUpdate >= NumberOfTimelines)
	{
		//size_t NumSeenTiles = 0, NumLoadedTiles = 0, NumVisibleTiles = 0;
		//IModelInvariants->Internals.SceneMapping.ForEachKnownTile([&](FITwinSceneTile const& SceneTile)
		//	{
		//		++NumSeenTiles;
		//		if (SceneTile.IsLoaded())
		//		{
		//			++NumLoadedTiles;
		//			if (SceneTile.bVisible)
		//				++NumVisibleTiles;
		//		}
		//	});
		UE_LOG(LogITwin, Verbose, TEXT("Total %dms to apply %llu timelines, incl. %dms extraction time"),
			(int)std::round(1000 * TimeToApplyAllTimelines), NumberOfTimelines,
			(int)std::round(1000 * TotalExtractionTime));
		//UE_LOG(LogITwin, Verbose, TEXT("Total visible, loaded, encountered tiles: %llu, %llu, %llu"),
		//	NumVisibleTiles, NumLoadedTiles, NumSeenTiles);
		if (bHasUpdatedSomething)
		{
			LastStepTime = FPlatformTime::Seconds();
			IModelInvariants->Internals.SceneMapping.Update4DAnimTextures();
			double const CurTime = FPlatformTime::Seconds();
			if ((CurTime - LastStepTime) > 0.1)
			{
				UE_LOG(LogITwin, Display, TEXT("Update4DAnimTextures #2 took %.1fs"),
										  (CurTime - LastStepTime));
				LastStepTime = CurTime;
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

void FITwinSynchro4DAnimator::FImpl::ApplyTimeline(FITwinElementTimeline& Timeline,
	std::optional<std::pair<double const&, double const&>> const& TimeIncrement,
	std::optional<ITwinScene::TileIdx> OnlySceneTile, bool const bOnlyVisibleTiles)
{
	if (!Timeline.ExtraData)
		return; // no mesh yet loaded is animated by this timeline, we can skip it
	const auto TimelineRange = Timeline.GetTimeRange();
	// After a timeline has been applied once(*), this is a good optim as most timelines correspond to
	// tasks which duration is rather small with respect to the whole animation. Note that a hack like
	// FixColor (see Timeline.cpp) would rather spoil this!
	// (*) and not "modified" since, like adding Elements to existing (grouped Elements) timelines.
	//     "Modified" used to also include discovering new tiles using known Elements, but for that we
	//     now restart from scratch anyway (see TilesHaveNew4DAnimTextures(..) call above)
	// If we want to skip hidden tiles later on, we'll have to handle newly visible tiles here too.
	if (!Timeline.TestModifiedAndResetFlag() && TimeIncrement
		&& (TimelineRange.second < TimeIncrement->first || TimelineRange.first > TimeIncrement->second))
	{
		return;
	}
	if (!OnlySceneTile)
		bHasUpdatedSomething = true;
	using namespace ITwin::Timeline;
	::Detail::FFinalizeDeferredPropData UserData{ IModelInvariants->Internals, Timeline };
	// 'State' contains std::optional's of each Timeline property (see ElementStateBase example in
	// Timeline/Definition.h)
	::Detail::FStateToApply StateToApply{
		Timeline.GetStateAtTime(AnimationTime, StateAtEntryTimeBehavior::UseLeftInterval, (void*)(&UserData))
		, Timeline, *IModelInvariants
	};
	auto const& Schedules = Owner.Owner;
	auto& State = StateToApply.Props;
	// Apply (debug) settings_ and property simplifications:
	if (State.Color && (Schedules.bDisableColoring || !State.Color->bHasColor))
	{
		State.Color.reset();
	}
	bool bNeedTranslucentMat = false, bNeedTransformable = false;
	if (State.Visibility)
	{
		if (Schedules.bDisableVisibilities) [[unlikely]]
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
	FTimelineToScene* TimelineOptim = static_cast<FTimelineToScene*>(Timeline.ExtraData);
	bool const bStateToApplyNeedsExtraction = (bNeedTranslucentMat || bNeedTransformable);
	if (bStateToApplyNeedsExtraction)
	{
		IModelInvariants->Internals.SceneMapping.CheckAndExtractElements(*TimelineOptim, bOnlyVisibleTiles,
																		 OnlySceneTile);
	}
	if (State.ClippingPlane)
	{
		// Case of a non-interpolated keyframe, need to call "finalizers" now: it should rather be
		// in PropertyTimeline<_PropertyValues>::GetStateAtTime, but it would mean going through all
		// the boost fusion mishmash just for this:
		if (State.ClippingPlane->DefrdPlaneEq.IsDeferred()) [[unlikely]]
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
	//auto const& TimelineElems = Timeline.GetIModelElements();
	//if (DebugElem && TimelineElems.find(*DebugElem) != TimelineElems.end())
	//{
	//	UE_LOG(LogITwin, Display, TEXT("ANIM %s CLR %d VIZ %.2f CUT %d TRSF %d"),
	//		*ITwin::ToString(*DebugElem), State.Color ? 1 : 0,
	//		State.Visibility ? State.Visibility->Value : 1.f,
	//		State.ClippingPlane ? 1 : 0, State.Transform ? 1 : 0);
	//}
	for (auto&& TileOptim : TimelineOptim->Tiles)
	{
		if (OnlySceneTile && (*OnlySceneTile) != TileOptim.Rank)
			continue;
		auto& SceneTile = IModelInvariants->Internals.SceneMapping.KnownTile(TileOptim.Rank);
		if (!SceneTile.IsLoaded() || (bOnlyVisibleTiles && !SceneTile.bVisible))
			continue;
		// NO! Let's fill the texture data appropriately even if the textures are not yet plugged into the
		// materials: otherwise we'd have to ApplyAnimation all over again once all SetupInMaterial(s)
		// have returned true:
		//if (SceneTile.Need4DAnimTexturesSetupInMaterials()) continue;
		auto const Start = TimelineOptim->TileElems.begin() + TileOptim.FirstElement;
		auto const End = Start + TileOptim.NbOfElements;
		bool const bHasExtractions =
			/*bStateToApplyNeedsExtraction <== no, update already extracted entities!!
			&&*/ (NO_EXTRACTION != TileOptim.FirstExtract);
		auto const ExtrStart = bHasExtractions
			? (TimelineOptim->Extracts.begin() + TileOptim.FirstExtract)
			: TimelineOptim->Extracts.end();
		auto const ExtrEnd = bHasExtractions ? (ExtrStart + TileOptim.NbOfElements)
												: TimelineOptim->Extracts.end();
		auto ExtrIt = ExtrStart;
		for (auto It = Start; It != End; ++It)
		{
			FITwinElementFeaturesInTile& ElementInTile = SceneTile.ElementFeatures(*It);
			::Detail::UpdateBatchedElement(StateToApply, SceneTile, ElementInTile);
			if (bHasExtractions)
			{
				FITwinExtractedElement& ExtractedElem = SceneTile.ExtractedElement(*ExtrIt);
				for (auto&& ExtractedEntity : ExtractedElem.Entities)
					::Detail::UpdateExtractedElement(StateToApply, SceneTile, ExtractedEntity);
				++ExtrIt;
			}
		}
	}
}

/// Will apply *all* timelines at once: this is particularly necessary for newly loaded tiles, to avoid
/// 4D effects "popping" into existence after the tile has been shown with 4D anim incompletely applied
void FITwinSynchro4DAnimator::ApplyAnimationOnTile(FITwinSceneTile& SceneTile)
{
	if (SceneTile.TimelinesIndices.empty())
		return;
	AITwinIModel* IModel = Cast<AITwinIModel>(Owner.GetOwner());
	if (!IModel)
		return;
	auto&& SchedInternals = GetInternals(Owner);
	if (!Impl->bIsPlaying && !Impl->bIsPaused) // ie Stopped
	{
		Impl->StopAnimationInTiles(&SceneTile);
		return;
	}
	if (SchedInternals.PrefetchAllElementAnimationBindings()
		&& !SchedInternals.IsPrefetchedAvailableAndApplied())
	{
		return;
	}
	auto&& AllTimelines = SchedInternals.GetTimeline().GetContainer();
	auto& IModelInternals = GetInternals(*IModel);
	auto const TileRank = IModelInternals.SceneMapping.KnownTileRank(SceneTile);
	for (auto&& Index : SceneTile.TimelinesIndices)
	{
		Impl->ApplyTimeline(*AllTimelines[Index], {}/*TODO_GCO: store last applied time*/, TileRank,
							/*bOnlyVisibleTiles*/false/*because flag not toggled yet!*/);
	}
	size_t dummy1, dummy2;
	IModelInternals.SceneMapping.Update4DAnimTileTextures(SceneTile, dummy1, dummy2);
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
	std::function<void(FITwinCoordConversions const&, FDefrdProp const&, FBox const&)> const& Finalizer,
	FString const& PropName)
{
	if (Deferred.IsDeferred()) [[unlikely]]
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
		Finalizer(UserData.IModelInternals.SceneMapping.GetIModel2UnrealCoordConv(), Deferred,
				  IModelElementsBBox);
	}
}

//---------------------------------------------------------------------------------------
// FDeferredAnchor interpolation and "finalizer"
//---------------------------------------------------------------------------------------

using FDefrdAnchorRot = ITwin::Timeline::FDeferredAnchor;

static void AnchorPosFinalizer(::Detail::FFinalizeDeferredPropData& UserData, FDefrdAnchorRot const& Deferred)
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
	if (x1.IsDeferred()) [[unlikely]]
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

using FDefrdPlaneEq = ITwin::Timeline::FDeferredPlaneEquation;

static void PlaneEquationFinalizer(::Detail::FFinalizeDeferredPropData& UserData,
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
	// Too slow, we indeed pass here very often
	//ensure(!x0.IsDeferred() && !x1.IsDeferred()
	//	&& (x0.PlaneOrientation.IsUnit() || FVector3f::ZeroVector == x0.PlaneOrientation)
	//	&& (x1.PlaneOrientation.IsUnit() || FVector3f::ZeroVector == x1.PlaneOrientation));
	if (x0.GrowthStatus == x1.GrowthStatus && (x0.GrowthStatus == EGrowthStatus::FullyGrown
											|| x0.GrowthStatus == EGrowthStatus::FullyRemoved))
		[[unlikely]]
	{
		// avoid useless interpolation below - this is called from GetStateAtTime, unrelated to the other
		// optims of the AllTimelines loop in ApplyAnimation!
		return Stop;
	}
	// Zero direction allows to identify keyframes added with "{}, EGrowthStatus::FullyGrown" parameters
	// for the case of successive tasks (see early on in AddCuttingPlaneToTimeline - Note that
	// SetCuttingPlaneAt converts {} to ZeroVector), which may fall in the middle of other tasks in the
	// (non-supported since non-specified!) case of overlapping tasks! (witnessed on Element 0x2000000CA33,
	// SourceElementID=cce938af-547b-4348-9b02-e1dffb1a2ae4, in HS2).
	if (FVector3f::ZeroVector == x0.PlaneOrientation)
		Out = x1;
	// Note: we don't care to test if both are zero: in that case boundary mode must be FullyGrown otherwise
	// we would have had an assert earlier, and returning that is perfectly fine
	else if (FVector3f::ZeroVector == x1.PlaneOrientation)
		Out = x0;
	else if ((x0.PlaneOrientation - x1.PlaneOrientation).IsNearlyZero()) // ie nearly equal
		[[likely]]
	{
		Out = FDefrdPlaneEq{ x0.PlaneOrientation, nullptr, Lerp(x0.PlaneW, x1.PlaneW, u),
							 ITwin::Timeline::EGrowthStatus::Partial };
	}
	else if ((x0.PlaneOrientation + x1.PlaneOrientation).IsNearlyZero()) // ie nearly opposite
	{
		Out = FDefrdPlaneEq{ x0.PlaneOrientation,  nullptr,Lerp(x0.PlaneW, -x1.PlaneW, u),
							 ITwin::Timeline::EGrowthStatus::Partial };
	}
	else
	{
		Out = FDefrdPlaneEq{ ConstQualSlerpNormals(x0.PlaneOrientation, x1.PlaneOrientation, u), nullptr,
							 Lerp(x0.PlaneW, x1.PlaneW, u),
							 ITwin::Timeline::EGrowthStatus::Partial };
	}
	return Continue;
}

} // namespace ITwin::Timeline::Interpolators
