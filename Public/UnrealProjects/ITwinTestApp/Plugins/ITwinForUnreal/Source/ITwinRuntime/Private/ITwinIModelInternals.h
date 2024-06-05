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

#include <vector>
#include <unordered_map>

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
	void ProcessElementInEachTile(ITwinElementID const Elem,
		TProcessElementFeaturesInTile const& ProcElemFeatures,
		TProcessExtractedElementInTile const& ProcExtractedElem)
	{
		for (auto& [TileID, SceneTile] : SceneMapping.KnownTiles)
		{
			{	auto* Found = SceneTile.FindElementFeatures(Elem);
				if (Found)
				{
					ProcElemFeatures(TileID, SceneTile, *Found);
				}
			}
			{	auto* Found = SceneTile.FindExtractedElement(Elem);
				if (Found)
				{
					ProcExtractedElem(SceneTile, *Found);
				}
			}
		}
	}

	FBox GetBoundingBox(ITwinElementID const Element) const
	{
		return SceneMapping.GetBoundingBox(Element);
	}

	/// Currently uses the assumption that BBoxes are computed for all Elements of a tile
	bool HasElementWithID(ITwinElementID const Element) const
	{
		auto const& BBoxes = SceneMapping.GetKnownBBoxes();
		return BBoxes.find(Element) != BBoxes.cend();
	}

	void OnElementTimelineModified(FITwinElementTimeline const& ModifiedTimeline);

	void OnClickedElement(ITwinElementID const Element, FHitResult const& HitResult);

	void SelectElement(ITwinElementID const InElementID);
};
