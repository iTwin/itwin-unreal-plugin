/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinIModelInternals.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Cesium3DTilesSelection/Tile.h>
#include <ITwinSceneMapping.h>
#include <Timeline/TimelineFwd.h>

struct FHitResult;

/// Destructors (of UObject descendents) being called only by the garbage collector, ie. at a time we
/// don't control, and in a random order, we need to do some clean-up in a more controller manner.
/// In particular, ScheduleComponents->Internals.Builder relies on SceneMapping structures, but the
/// owner iModel actor may be deleted before its very own Schedules component, leaving all garbaged
/// raw pointers in place! Also, problems with JsonQueriesCache led me to de-initialize them manually.
/// De-init used to be done in the iModel's EndPlay method, but this is not called for Editor actors.
/// This FIModelUninitializer will be triggered by either 1/ EndPlay, 2/ the iModel's destructor or
/// 3/ the Schedule component's destructor, whichever comes first.
class FIModelUninitializer
{
	std::vector<std::function<void()>> OrderedUninits;

public:
	~FIModelUninitializer() { ensure(OrderedUninits.empty()); }

	void Register(std::function<void()>&& Func)
	{
		OrderedUninits.push_back(std::move(Func));
	}

	void Run()
	{
		for (auto&& Func : OrderedUninits)
			Func();
		OrderedUninits.clear();
	}
};

//! Data & methods associated with an IModel, that are only accessible by the plugin.
//! To access it, call GetInternals(AITwinIModel&).
//! This is an intermediate access level between:
//! - ITwinIModel public members, which can be accessed also from code outside plugin,
//! - and ITwinIModel private members (or its Impl), which can be accessed only from ITwinIModel.cpp.
class FITwinIModelInternals
{
public:
	AITwinIModel& Owner;
	FITwinSceneMapping SceneMapping;
	std::shared_ptr<FIModelUninitializer> Uniniter;
	std::unordered_set<ITwinScene::TileIdx> TilesPendingRenderReadiness;

	FITwinIModelInternals(AITwinIModel& InOwner) : Owner(InOwner)
	{
		Uniniter = std::make_shared<FIModelUninitializer>();
	}

	/// Will have to look up the ElementIDs in all hashed_unique structures: prefer storing random access
	/// indices (aka "ranks": see ITwinTile::ElemIdx and ITwinTile::ExtrIdx) for perf-critical tasks
	template<typename TProcessElementFeaturesInTile, typename TProcessExtractedElementInTile>
	void ProcessElementsInEachTileSLOW(FElementsGroup const& IModelElements,
		TProcessElementFeaturesInTile const& ProcElemFeatures,
		TProcessExtractedElementInTile const& ProcExtractedElem,
		bool const bVisibleOnly)
	{
		SceneMapping.ForEachKnownTile([&](FITwinSceneTile& SceneTile)
		{
			if (bVisibleOnly && !SceneTile.bVisible)
			{
				return;
			}
			for (auto&& Elem : IModelElements)
			{
				{	auto* Found = SceneTile.FindElementFeaturesSLOW(Elem);
					if (Found)
					{
						ProcElemFeatures(SceneTile.TileID, SceneTile, *Found);
					}
				}
				{	auto* Found = SceneTile.FindExtractedElementSLOW(Elem);
					if (Found)
					{
						for (FITwinExtractedEntity& ExtrEnt : Found->Entities)
						{
							ProcExtractedElem(SceneTile, ExtrEnt);
						}
					}
				}
			}
		});
	}

	FBox GetBoundingBox(FElementsGroup const& Elements) const
	{
		FBox GroupBox;
		for (auto&& Elem : Elements)
		{
			GroupBox += SceneMapping.GetBoundingBox(Elem);
		}
		return GroupBox;
	}

	bool HasElementWithID(ITwinElementID const Element) const
	{
		auto const& Elem = SceneMapping.GetElement(Element);
		return (ITwin::NOT_ELEMENT != Elem.ElementID); // <== means it was not found
	}

	void OnNewTileBuilt(Cesium3DTilesSelection::TileID const& TileID);
	void OnElementsTimelineModified(FITwinElementTimeline& ModifiedTimeline,
									std::vector<ITwinElementID> const* OnlyForElements = nullptr);
	void OnVisibilityChanged(const Cesium3DTilesSelection::TileID& TileID, bool visible);

	bool IsElementHiddenInSavedView(ITwinElementID const Element) const;
	bool OnClickedElement(ITwinElementID const Element, FHitResult const& HitResult);
	void DescribeElement(ITwinElementID const Element, TWeakObjectPtr<UPrimitiveComponent> HitComponent = {});
	bool SelectVisibleElement(ITwinElementID const InElementID);
	void HideElements(std::unordered_set<ITwinElementID> const& InElementIDs, bool IsConstruction);
	//! Returns the selected Element's ID, if an Element is selected, or ITwin::NOT_ELEMENT.
	ITwinElementID GetSelectedElement() const;
};
