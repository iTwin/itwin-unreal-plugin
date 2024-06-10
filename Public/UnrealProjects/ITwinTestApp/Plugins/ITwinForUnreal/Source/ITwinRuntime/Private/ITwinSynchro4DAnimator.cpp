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
#include <Timeline/Timeline.h>
#include <Timeline/TimeInSeconds.h>
#include <Timeline/SchedulesConstants.h>

#include <Materials/MaterialInstanceDynamic.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

class FITwinSynchro4DAnimator::FImpl
{
	friend class FITwinSynchro4DAnimator;
	FITwinSynchro4DAnimator& Owner;
	std::optional<double> LastTickScriptTime;
	bool bIsPlaying = false, bIsPaused = false;

	void ApplyAnimation(bool const bForceUpdateAll);
	void UpdateAllTextures();

public:
	FImpl(FITwinSynchro4DAnimator& InOwner) : Owner(InOwner) {}
};

FITwinSynchro4DAnimator::FITwinSynchro4DAnimator(UITwinSynchro4DSchedules& InOwner)
	: Owner(InOwner)
	, Impl(MakePimpl<FImpl>(*this))
{
}

void FITwinSynchro4DAnimator::TickAnimation(float DeltaTime)
{
	if (Impl->bIsPlaying && Owner.AnimationSpeed != 0.)
	{
		Owner.AnimationTime += FTimespan::FromSeconds(DeltaTime * Owner.AnimationSpeed);
		auto&& ScheduleEnd = GetInternals(Owner).GetTimeline().GetDateRange().GetUpperBound();
		if (ScheduleEnd.IsClosed() && Owner.AnimationTime >= ScheduleEnd.GetValue())
		{
			Pause();
		}
		OnChangedAnimationTime();
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
		Impl->bIsPaused = true;
	}
}

void FITwinSynchro4DAnimator::Stop()
{
	if (Impl->bIsPlaying)
	{
		Pause();
	}
	if (Impl->bIsPaused)
	{
		Impl->LastTickScriptTime.reset();
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
				});
		}
		//Impl->ApplyAnimation(true); No, this would reset the values above for all animated elements
		Impl->UpdateAllTextures();
	}
}

void FITwinSynchro4DAnimator::OnChangedAnimationTime()
{
	// Don't wait next tick, speed can be 0, when setting a new time manually
	if (Impl->bIsPlaying || Impl->bIsPaused)
		Impl->ApplyAnimation(false);
}

void FITwinSynchro4DAnimator::OnChangedAnimationSpeed()
{
	/*no-op*/
}

void FITwinSynchro4DAnimator::OnChangedScheduleRenderSetting()
{
	if (Impl->bIsPlaying || Impl->bIsPaused)
		Impl->ApplyAnimation(true);
}

void FITwinSynchro4DAnimator::OnMaskOutNonAnimatedElements()
{
	OnFadeOutNonAnimatedElements();
}

void FITwinSynchro4DAnimator::OnFadeOutNonAnimatedElements()
{
	FITwinScheduleTimeline const& Timeline = GetInternals(Owner).GetTimeline();
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
	for (auto& it: IModelInternals.SceneMapping.KnownTiles)
	{
		auto & TileID = it.first;
		auto & SceneTile= it.second;
		if (SceneTile.HighlightsAndOpacities)
		{
			SceneTile.ForEachElementFeatures(
				[&Timeline, &FillColor, &SceneTile](FITwinElementFeaturesInTile& ElementFeatures)
				{
					if (-1 == Timeline.GetElementTimelineIndex(ElementFeatures.ElementID))
						SceneTile.HighlightsAndOpacities->SetPixels(ElementFeatures.Features, FillColor);
				});
		}
		// SceneTile.ExtractedElements share the textures: just set opacity (ExtractedElements may soon
		// originate from material mapping, and not just scheduling? Hence not even testing
		// HighlightsAndOpacities here)
		SceneTile.ForEachExtractedElement(
			[&Timeline, &FillColor, &SceneTile](FITwinExtractedEntity& ExtractedElement)
			{
				if (-1 == Timeline.GetElementTimelineIndex(ExtractedElement.ElementID))
					ExtractedElement.SetForcedOpacity(FillColor[3] / 255.f);
			});
	}
	if (Impl->bIsPlaying || Impl->bIsPaused)
		Impl->ApplyAnimation(true);
	else
		Impl->UpdateAllTextures();
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
		// uint8 components too.
		return ITwin::Synchro4D::ReplaceDisabledColorInPlace(ColorBGRA8);
	}

	struct FStateToApply
	{
		FITwinElementTimeline::PropertyOptionals Props;
		bool bFullyHidden = false;
		/// Color and/or Visibility properties as a packed BGRA value for use in the property texture.
		/// Converted once just-in-time from Props.color_ and Props.visibility_.
		std::optional<std::array<uint8, 4>> AsBGRA;
		/// Cutting plane equation property as a packed std::array for use in the property texture.
		/// Converted once just-in-time from Props.clippingPlane_->deferredPlaneEquation_.planeEquation_
		std::optional<std::array<float, 4>> AsPlaneEquation;
		/// Animation transform property as an FMatrix to be applied on an actor or component.
		/// Converted once just-in-time from Props.transform_.
		std::optional<FMatrix> AsMatrix;

		/// OK to call whatever Props.color_ and Props.visibility_
		void EnsureBGRA()
		{
			if (!AsBGRA)
			{
				float const alpha = Props.visibility_.get_value_or(1.f).value_;
				if (Props.color_)
				{
					AsBGRA.emplace(ClampCast01toBGRA8ReplacingDisabled(Props.color_->value_, alpha));
				}
				else
				{
					AsBGRA.emplace(std::array<uint8, 4> S4D_MAT_BGRA_DISABLED(ClampCast01toU8(alpha)));
				}
			}
		}

		/// OK to call whatever Props.clippingPlane_
		void EnsurePlaneEquation()
		{
			if (!AsPlaneEquation)
			{
				if (Props.clippingPlane_)
				{
					auto const& PlaneEq = Props.clippingPlane_->deferredPlaneEquation_.planeEquation_;
					AsPlaneEquation.emplace(std::array<float, 4>
						// see comment about ordering on FITwinSceneTile::CuttingPlanes
						{ (float)PlaneEq.X, (float)PlaneEq.Y, (float)PlaneEq.Z, (float)PlaneEq.W });
				}
				else
				{
					AsPlaneEquation.emplace(std::array<float, 4> S4D_CLIPPING_DISABLED);
				}
			}
		}

		/// MUST have a valid Props.transform_
		void EnsureMatrix()
		{
			if (!AsMatrix)
			{
				AsMatrix.emplace(ITwin::Timeline::TransformToMatrix(*Props.transform_));
			}
		}
	};

	void UpdateExtractedElement(FStateToApply& State,
								FITwinSceneTile& SceneTile,
								FITwinExtractedEntity& ExtractedEntity)
	{
		if (!ExtractedEntity.IsValid())
			return;
		ExtractedEntity.SetHidden(State.bFullyHidden);
		if (State.bFullyHidden)
			return;

		// Note: color and cutting plane need no processing here, as long as the extracted elements
		// use the same material and textures as the batched meshes. Alpha must be set on the material
		// parameter that is used to override the texture look-up for extracted elements, though:
		//State.EnsureBGRA(); NOT needed, the single float value is exactly what we need!
		ExtractedEntity.SetForcedOpacity(State.Props.visibility_.get_value_or(1.f).value_);
		if (State.Props.transform_)
		{
			//State.EnsureMatrix();
			//SetSynchroScheduleTransform(*ExtractedEntity.MeshComponent.Get(), *State.AsMatrix,
			//							ExtractedEntity.OriginalMatrix);
		}
		else
		{
			//SetSynchroScheduleTransform(*ExtractedEntity.MeshComponent.Get(), FMatrix::Identity,
			//							ExtractedEntity.OriginalMatrix);
		}
		FITwinSceneMapping::SetupHighlights(SceneTile, ExtractedEntity);
		FITwinSceneMapping::SetupCuttingPlanes(SceneTile, ExtractedEntity);
	}

	void UpdateBatchedElement(FStateToApply& State, CesiumTileID const&, FITwinSceneTile& SceneTile,
							  FITwinElementFeaturesInTile& ElementFeaturesInTile)
	{
		if (State.bFullyHidden)
		{
			// Masked and Translucent materials should both test this special value
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
					// (alpha is already zeroed in FITwinSceneMapping::OnBatchedElementTimelineModified,
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
		FITwinSceneMapping::SetupHighlights(SceneTile, ElementFeaturesInTile);
		FITwinSceneMapping::SetupCuttingPlanes(SceneTile, ElementFeaturesInTile);
	}

	struct FCheckDeferredPlaneEquationData
	{
		FITwinIModelInternals& IModelInternals;
		ITwinElementID const Element;
	};

	constexpr float HiddenBelowAlpha = 0.02f;

} // ns Detail

namespace ITwin::Timeline::Interpolators
{
	void CheckDeferredPlaneEquationW(Detail::FCheckDeferredPlaneEquationData& UserData,
									 ITwin::Timeline::FDeferredPlaneEquation const& Deferred);
}

void FITwinSynchro4DAnimator::FImpl::ApplyAnimation(bool const bForceUpdateAll)
{
	auto const& Schedules = Owner.Owner;
	if (LastTickScriptTime && Schedules.AnimationTime == *LastTickScriptTime && !bForceUpdateAll)
		return;
	FITwinScheduleTimeline const& Timeline = GetInternals(Schedules).GetTimeline();
	if (Timeline.GetContainer().empty())
		return;
	AITwinIModel* IModel = Cast<AITwinIModel>(Schedules.GetOwner());
	if (!IModel)
		return;
	FITwinIModelInternals& IModelInternals = GetInternals(*IModel);
	double const CurrentTimeInSeconds = ITwin::Time::FromDateTime(Schedules.AnimationTime);
	const auto ScriptTimeRange = (LastTickScriptTime && !bForceUpdateAll) ?
		// Braces are a workaround for compilation error on clang, because without them minmax would return
		// a pair of references, while make_pair returns a pair of values.
		std::minmax({ *LastTickScriptTime, CurrentTimeInSeconds }) :
		std::make_pair(std::numeric_limits<double>::lowest(), // Update everything on first call
					   std::numeric_limits<double>::max());
	bool bHasUpdatedSomething = bForceUpdateAll;
	for (const auto& ElementTimelinePtr : Timeline.GetContainer())
	{
		const auto ElementTimeRange = ElementTimelinePtr->GetTimeRange();
		if (ElementTimeRange.second < ScriptTimeRange.first
			|| ElementTimeRange.first > ScriptTimeRange.second)
		{
			continue;
		}
		bHasUpdatedSomething = true;
		Detail::FCheckDeferredPlaneEquationData UserData{ IModelInternals,
														  ElementTimelinePtr->IModelElementID };
		// 'State' contains boost::optional's of each Timeline property (see ElementStateBase example in
		// Timeline/Definition.h)
		Detail::FStateToApply StateToApply{
			ElementTimelinePtr->GetStateAtTime(CurrentTimeInSeconds,
				ITwin::Timeline::StateAtEntryTimeBehavior::UseLeftInterval, (void*)(&UserData))
		};
		auto& State = StateToApply.Props;
		// Apply (debug) settings_ and property simplifications:
		if (State.color_ && (Schedules.bDisableColoring || !State.color_->hasColor_))
		{
			State.color_.reset();
		}
		if (State.visibility_)
		{
			// No, this would apply to extracted elements as well...or split StateToApply.bFullyHidden
			// into Extracted- and Batched-specific flags.
			/*if (IModelInternals.SceneMapping.IsElementExtracted(ElementTimelinePtr->IModelElementID))
				StateToApply.bFullyHidden = true;
			else*/
			if (Schedules.bDisableVisibilities)
				State.visibility_.reset();
			else if (State.visibility_->value_ <= Detail::HiddenBelowAlpha)
			{
				StateToApply.bFullyHidden = true;
			}
		}
		if (State.clippingPlane_)
		{
			if (Schedules.bDisableCuttingPlanes || State.clippingPlane_->fullyVisible_)
			{
				State.clippingPlane_.reset();
			}
			else if (State.clippingPlane_->fullyHidden_)
			{
				StateToApply.bFullyHidden = true;
			}
			else if (State.clippingPlane_->deferredPlaneEquation_.planeEquation_.W
				== FITwinElementTimeline::DeferredPlaneEquationW)
			{
				// Rare case of a non-interpolated keyframe that was never passed through 
				// WillInterpolateBetween. Do it now: it should rather be in
				// PropertyTimeline<_PropertyValues>::GetStateAtTime, but it would mean going through all
				// the boost fusion mishmash just for this:
				ITwin::Timeline::Interpolators::CheckDeferredPlaneEquationW(
					UserData, State.clippingPlane_->deferredPlaneEquation_);
			}
		}
		if (Schedules.bDisableTransforms) State.transform_.reset();

		IModelInternals.ProcessElementInEachTile(ElementTimelinePtr->IModelElementID,
			[&StateToApply](CesiumTileID const& TileID,
							FITwinSceneTile& SceneTile,
							FITwinElementFeaturesInTile& ElementFeaturesInTile)
				{ Detail::UpdateBatchedElement(StateToApply, TileID, SceneTile,
											   ElementFeaturesInTile); },
			[&StateToApply](FITwinSceneTile& SceneTile,
							FITwinExtractedEntity& ExtractedEntity)
				{ Detail::UpdateExtractedElement(StateToApply, SceneTile,
												 ExtractedEntity); });
	}
	if (bHasUpdatedSomething)
		UpdateAllTextures();
	LastTickScriptTime = CurrentTimeInSeconds;
}

void FITwinSynchro4DAnimator::FImpl::UpdateAllTextures()
{
	AITwinIModel* IModel = Cast<AITwinIModel>(Owner.Owner.GetOwner());
	if (!IModel)
		return;
	FITwinIModelInternals& IModelInternals = GetInternals(*IModel);
	for (auto&& [TileID, SceneTile] : IModelInternals.SceneMapping.KnownTiles)
	{
		if (SceneTile.HighlightsAndOpacities)
			SceneTile.HighlightsAndOpacities->UpdateTexture();
		if (SceneTile.CuttingPlanes)
			SceneTile.CuttingPlanes->UpdateTexture();
		if (SceneTile.SelectionHighlights)
			SceneTile.SelectionHighlights->UpdateTexture();
	}
}

namespace ITwin::Timeline::Interpolators {

using DefrdPlaneEq = ITwin::Timeline::FDeferredPlaneEquation;

template<> DefrdPlaneEq Default::operator ()<DefrdPlaneEq>(const DefrdPlaneEq& x0,
														   const DefrdPlaneEq& x1, float u) const
{
	check(x0.planeEquation_.W != FITwinElementTimeline::DeferredPlaneEquationW
	   && x1.planeEquation_.W != FITwinElementTimeline::DeferredPlaneEquationW);
	return DefrdPlaneEq{ operator()(x0.planeEquation_, x1.planeEquation_, u),
						 ITwin::Timeline::EGrowthBoundary() };
}

void CheckDeferredPlaneEquationW(Detail::FCheckDeferredPlaneEquationData& UserData,
								 DefrdPlaneEq const& Deferred)
{
	if (FITwinElementTimeline::DeferredPlaneEquationW == Deferred.planeEquation_.W)
	{
		// Note: this is actually the BBox of the whole Resource because there is a 1:1 mapping between
		// Resource and AnimatedElement in the current version of Synchro4D tools, according to Bernardas
		auto const& BBox = UserData.IModelInternals.GetBoundingBox(UserData.Element);
		UE_LOG(LogITwin, Display, TEXT("Setting up Cutting Plane for Element 0x%I64x with BBox %s"),
			   UserData.Element.value(), *BBox.ToString());
		FITwinSynchro4DSchedulesInternals::FinalizeCuttingPlaneEquation(Deferred, BBox);
	}
}

template<> void Default::WillInterpolateBetween<DefrdPlaneEq>(
	const DefrdPlaneEq& x0, const DefrdPlaneEq& x1, void* userData) const
{
	auto& UserDataForDeferredW = *(Detail::FCheckDeferredPlaneEquationData*)userData;
	CheckDeferredPlaneEquationW(UserDataForDeferredW, x0);
	CheckDeferredPlaneEquationW(UserDataForDeferredW, x1);
}

} // namespace ITwin::Timeline::Interpolators
