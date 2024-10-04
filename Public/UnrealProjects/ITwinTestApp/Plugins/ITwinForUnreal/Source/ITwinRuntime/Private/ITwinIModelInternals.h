/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinIModelInternals.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinSceneMapping.h>
#include <Timeline/TimelineFwd.h>

#include <unordered_set>

struct FHitResult;

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

	FITwinIModelInternals(AITwinIModel& InOwner) : Owner(InOwner) {}

	template<typename TProcessElementFeaturesInTile, typename TProcessExtractedElementInTile>
	void ProcessElementsInEachTile(std::set<ITwinElementID> const& IModelElements,
		TProcessElementFeaturesInTile const& ProcElemFeatures,
		TProcessExtractedElementInTile const& ProcExtractedElem,
		bool const bVisibleOnly)
	{
		for (auto& [TileID, SceneTile] : SceneMapping.KnownTiles)
		{
			if (bVisibleOnly && !SceneTile.HasAnyVisibleMesh())
			{
				continue;
			}
			for (auto&& Elem : IModelElements)
			{
				{	auto* Found = SceneTile.FindElementFeatures(Elem);
					if (Found)
					{
						ProcElemFeatures(TileID, SceneTile, *Found);
					}
				}
				{	auto* FoundEntities = SceneTile.FindExtractedElement(Elem);
					if (FoundEntities)
					{
						for (FITwinExtractedEntity& ExtrEnt : *FoundEntities)
						{
							ProcExtractedElem(SceneTile, ExtrEnt);
						}
					}
				}
			}
		}
	}

	FBox GetBoundingBox(std::set<ITwinElementID> const& Elements) const
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
		auto const& AllElems = SceneMapping.GetElementIDs();
		return AllElems.find(Element) != AllElems.cend();
	}

	void OnElementsTimelineModified(FITwinElementTimeline& ModifiedTimeline,
									std::vector<ITwinElementID> const* OnlyForElements = nullptr);

	bool OnClickedElement(ITwinElementID const Element, FHitResult const& HitResult);

	bool SelectElement(ITwinElementID const InElementID);

	//! Returns the selected Element's ID, if an Element is selected, or ITwin::NOT_ELEMENT.
	ITwinElementID GetSelectedElement() const;
};
