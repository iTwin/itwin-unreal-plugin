/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingModelGroups.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinClippingModelGroups.h"
#include "ITwinClippingEffectManager.h"
#include "ITwinClippingEffectManager.inl"

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/Tools/Log.h>
#include <Compil/AfterNonUnrealIncludes.h>


int32 FITwinClippingEffectManagerInfluenceSource::NumEffects(EITwinClippingPrimitiveType Type) const
{
	return Manager.NumEffects(Type);
}

uint64 FITwinClippingEffectManagerInfluenceSource::GetEffectId(EITwinClippingPrimitiveType Type,
															   int32 Index) const
{
	auto const RefId = Manager.GetEffectId(Type, Index);
	// An invalid id is uint64(-1) and would silently merge distinct effects into one signature.
	BE_ASSERT(RefId.IsValid());
	return RefId.ID();
}

bool FITwinClippingEffectManagerInfluenceSource::ShouldEffectInfluenceModel(
	EITwinClippingPrimitiveType Type, int32 Index, ITwin::ModelLink const& Model) const
{
	return Manager.ShouldEffectInfluenceModel(Type, Index, Model);
}


bool FITwinClippingModelGroups::Rebuild(IITwinClippingInfluenceSource const& InfluenceSource,
										std::vector<ITwin::ModelLink> const& Models)
{
	const int32 NumBoxes  = InfluenceSource.NumEffects(EITwinClippingPrimitiveType::Box);
	const int32 NumPlanes = InfluenceSource.NumEffects(EITwinClippingPrimitiveType::Plane);

	// ---- 1. Signature of each model: RefIDs of the primitives influencing it, sorted ----
	// Cache the ids once: GetEffectId() is called NumPrims times per model otherwise.
	std::vector<uint64> BoxIds(NumBoxes), PlaneIds(NumPlanes);
	for (int32 i = 0; i < NumBoxes; ++i)
	{
		BoxIds[i] = InfluenceSource.GetEffectId(EITwinClippingPrimitiveType::Box, i);
	}
	for (int32 i = 0; i < NumPlanes; ++i)
	{
		PlaneIds[i] = InfluenceSource.GetEffectId(EITwinClippingPrimitiveType::Plane, i);
	}

	std::map<ITwin::ModelLink, FSignature> ModelSignatures;
	for (ITwin::ModelLink const& Model : Models)
	{
		FSignature Sig;
		for (int32 i = 0; i < NumBoxes; ++i)
			if (InfluenceSource.ShouldEffectInfluenceModel(EITwinClippingPrimitiveType::Box, i, Model))
				Sig.push_back(BoxIds[i]);
		for (int32 i = 0; i < NumPlanes; ++i)
			if (InfluenceSource.ShouldEffectInfluenceModel(EITwinClippingPrimitiveType::Plane, i, Model))
				Sig.push_back(PlaneIds[i]);

		if (!Sig.empty())
		{
			// Sorted so that the signature does not depend on primitive ordering.
			std::sort(Sig.begin(), Sig.end());
			ModelSignatures.emplace(Model, std::move(Sig));
		}
		// else: no signature -> group 0 (reserved, costs no id)
	}

	// ---- 2. Assign a group id per distinct signature ----
	// Ids of signatures that already existed are preserved, to avoid needless CPD churn on
	// every mesh component; ids of signatures that disappeared are reclaimed.
	std::map<FSignature, uint32> NewSignatureToGroup;
	std::vector<bool> UsedIds(MaxGroups, false);
	// Id 0 is reserved for "influenced by nothing", so the usable range is 1..MaxGroups-1.
	UsedIds[NoClippingGroup] = true;

	for (auto const& [Model, Sig] : ModelSignatures)
	{
		if (NewSignatureToGroup.count(Sig))
			continue;
		auto const OldIt = SignatureToGroup.find(Sig);
		if (OldIt != SignatureToGroup.end() && !UsedIds[OldIt->second])
		{
			NewSignatureToGroup.emplace(Sig, OldIt->second);
			UsedIds[OldIt->second] = true;
		}
	}

	bool bNewOverflowed = false;
	for (auto const& [Model, Sig] : ModelSignatures)
	{
		if (NewSignatureToGroup.count(Sig))
			continue;
		uint32 FreeId = NoClippingGroup;
		for (uint32 Id = 1; Id < MaxGroups; ++Id)
			if (!UsedIds[Id]) { FreeId = Id; break; }

		if (FreeId == NoClippingGroup)
		{
			// Fail visibly, and fail "open": affected models stay unclipped rather than having
			// geometry vanish for a reason the user cannot diagnose.
			if (!bNewOverflowed)
			{
				BE_LOGE("Cutout", "Too many distinct clipping configurations (max "
					<< MaxGroups - 1 << ") - some models will be left unclipped");
			}
			bNewOverflowed = true;
			continue; // signature stays unmapped -> GetGroupId() returns NoClippingGroup
		}
		NewSignatureToGroup.emplace(Sig, FreeId);
		UsedIds[FreeId] = true;
	}

	// ---- 3. Build the model->group map and the per-primitive group masks ----
	std::map<ITwin::ModelLink, uint32> NewModelToGroup;
	std::vector<uint64> NewBoxMasks(NumBoxes, 0), NewPlaneMasks(NumPlanes, 0);

	for (ITwin::ModelLink const& Model : Models)
	{
		auto const SigIt = ModelSignatures.find(Model);
		if (SigIt == ModelSignatures.end())
		{
			NewModelToGroup.emplace(Model, NoClippingGroup);
			continue;
		}
		auto const GroupIt = NewSignatureToGroup.find(SigIt->second);
		const uint32 GroupId = (GroupIt != NewSignatureToGroup.end())
			? GroupIt->second : NoClippingGroup; // overflowed
		NewModelToGroup.emplace(Model, GroupId);
	}

	// Transpose the per-model signatures into per-primitive masks: bit g of BoxMasks[i] means
	// "box i influences group g". This is what the shader tests against the CPD group id.
	std::map<uint64, int32> BoxIdToIndex, PlaneIdToIndex;
	for (int32 i = 0; i < NumBoxes; ++i)   BoxIdToIndex.emplace(BoxIds[i], i);
	for (int32 i = 0; i < NumPlanes; ++i)  PlaneIdToIndex.emplace(PlaneIds[i], i);

	for (auto const& [Sig, GroupId] : NewSignatureToGroup)
	{
		const uint64 GroupBit = uint64(1) << GroupId;
		for (uint64 Id : Sig)
		{
			if (auto const It = BoxIdToIndex.find(Id); It != BoxIdToIndex.end())
				NewBoxMasks[It->second] |= GroupBit;
			else if (auto const It2 = PlaneIdToIndex.find(Id); It2 != PlaneIdToIndex.end())
				NewPlaneMasks[It2->second] |= GroupBit;
		}
	}

	// ---- 4. Change detection ----
	const bool bChanged =
		   bNewOverflowed   != bOverflowed
		|| NewModelToGroup  != ModelToGroup
		|| NewBoxMasks      != BoxMasks
		|| NewPlaneMasks    != PlaneMasks;

	SignatureToGroup = std::move(NewSignatureToGroup);
	ModelToGroup     = std::move(NewModelToGroup);
	BoxMasks         = std::move(NewBoxMasks);
	PlaneMasks       = std::move(NewPlaneMasks);
	bOverflowed      = bNewOverflowed;

	return bChanged;
}

uint32 FITwinClippingModelGroups::GetGroupId(ITwin::ModelLink const& Model) const
{
	auto const It = ModelToGroup.find(Model);
	return It != ModelToGroup.end() ? It->second : NoClippingGroup;
}

uint64 FITwinClippingModelGroups::GetPrimitiveMask(EITwinClippingPrimitiveType Type, int32 Index) const
{
	// Polygons are not handled through the MPC (they use Cesium raster overlays).
	BE_ASSERT(Type == EITwinClippingPrimitiveType::Box || Type == EITwinClippingPrimitiveType::Plane);
	auto const& Masks = (Type == EITwinClippingPrimitiveType::Box) ? BoxMasks : PlaneMasks;
	return (Index >= 0 && Index < (int32)Masks.size()) ? Masks[Index] : 0;
}
