/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingModelGroups.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CoreMinimal.h>
#include <Clipping/ITwinClippingEnums.h>
#include <Clipping/ITwinClippingConstants.h>
#include <ITwinModelType.h>

#include <map>
#include <vector>

class UITwinClippingEffectManager;

/// Minimal view of the clipping configuration needed to allocate model groups. Introduced so that
/// FITwinClippingModelGroups can be exercised without a World, populations or effect actors:
/// UITwinClippingEffectManager exposes non-virtual methods over protected arrays, and GetEffectId()
/// needs a live AITwinPopulation, none of which can be faked in a unit test.
class IITwinClippingInfluenceSource
{
public:
	virtual ~IITwinClippingInfluenceSource() = default;
	virtual int32 NumEffects(EITwinClippingPrimitiveType Type) const = 0;
	/// Stable identifier of the effect (AdvViz::SDK::RefID::ID()), used as signature key.
	virtual uint64 GetEffectId(EITwinClippingPrimitiveType Type, int32 Index) const = 0;
	virtual bool ShouldEffectInfluenceModel(EITwinClippingPrimitiveType Type, int32 Index,
											ITwin::ModelLink const& Model) const = 0;
};

/// Adapter binding the allocator to the real effect manager.
class FITwinClippingEffectManagerInfluenceSource final : public IITwinClippingInfluenceSource
{
public:
	explicit FITwinClippingEffectManagerInfluenceSource(UITwinClippingEffectManager const& InManager)
		: Manager(InManager) {}

	int32 NumEffects(EITwinClippingPrimitiveType Type) const override;
	uint64 GetEffectId(EITwinClippingPrimitiveType Type, int32 Index) const override;
	bool ShouldEffectInfluenceModel(EITwinClippingPrimitiveType Type, int32 Index,
									ITwin::ModelLink const& Model) const override;
private:
	UITwinClippingEffectManager const& Manager;
};

/// Maps each model to a "clipping model group": models influenced by exactly the same set of
/// clipping primitives share an id. Group 0 is reserved for models influenced by nothing.
/// The per-primitive masks (one bit per group) are uploaded to MPC_Clipping, while the group
/// id itself travels through Custom Primitive Data.
class FITwinClippingModelGroups
{
public:
	static constexpr uint32 NoClippingGroup = 0;
	/// Ids 0 .. MaxGroups-1. Bit g of a primitive mask means "influences group g".
	static constexpr uint32 MaxGroups =
		ITwin::CLIPPING_MASK_WORDS * ITwin::CLIPPING_BITS_PER_MASK_WORD;

	bool Rebuild(IITwinClippingInfluenceSource const& InfluenceSource,
				 std::vector<ITwin::ModelLink> const& Models);

	uint32 GetGroupId(ITwin::ModelLink const& Model) const;
	/// Bitmask of groups influenced by the given primitive.
	uint64 GetPrimitiveMask(EITwinClippingPrimitiveType Type, int32 Index) const;

	bool HasOverflowed() const { return bOverflowed; }

private:
	/// Sorted list of the RefIDs of the primitives influencing a model. Deliberately keyed on the
	/// *stable* effect ids rather than on primitive indices: this way, adding or removing a primitive
	/// only changes the signature of the models it actually influences, so all other models keep their
	/// group id (and their Custom Primitive Data) untouched across a rebuild.
	using FSignature = std::vector<uint64>;

	std::map<ITwin::ModelLink, uint32> ModelToGroup;

	/// Kept across rebuilds so that an unchanged configuration keeps its id (avoids CPD churn on every
	/// mesh component). Keyed on stable RefIDs, so adding/removing a primitive leaves the entries of
	/// unaffected models intact.
	std::map<FSignature, uint32> SignatureToGroup;
	std::vector<uint64> BoxMasks, PlaneMasks;
	bool bOverflowed = false;
};
