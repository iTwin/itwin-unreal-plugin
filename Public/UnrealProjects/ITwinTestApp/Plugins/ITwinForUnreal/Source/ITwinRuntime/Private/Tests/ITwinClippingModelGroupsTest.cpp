/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingModelGroupsTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS

#include <Clipping/ITwinClippingModelGroups.h>
#include <Clipping/ITwinClippingEnums.h>
#include <Clipping/ITwinClippingConstants.h>
#include <ITwinModelType.h>

#include <Tests/ITwinAutomationTestBaseNoLogs.h>

#include <Misc/AutomationTest.h>
#include <Misc/LowLevelTestAdapter.h>

#include <map>
#include <set>
#include <vector>

namespace
{
	/// In-memory influence source: one entry per primitive, holding its stable id and the set of
	/// models it influences. Mirrors what the real manager exposes, without any Unreal dependency.
	class FFakeInfluenceSource : public IITwinClippingInfluenceSource
	{
	public:
		struct FPrimitive
		{
			uint64 Id = 0;
			std::set<ITwin::ModelLink> InfluencedModels;
		};

		std::vector<FPrimitive> Boxes, Planes;

		int32 NumEffects(EITwinClippingPrimitiveType Type) const override
		{
			return static_cast<int32>(Of(Type).size());
		}
		uint64 GetEffectId(EITwinClippingPrimitiveType Type, int32 Index) const override
		{
			return Of(Type)[Index].Id;
		}
		bool ShouldEffectInfluenceModel(EITwinClippingPrimitiveType Type, int32 Index,
										ITwin::ModelLink const& Model) const override
		{
			auto const& Influenced = Of(Type)[Index].InfluencedModels;
			return Influenced.find(Model) != Influenced.end();
		}

	private:
		std::vector<FPrimitive> const& Of(EITwinClippingPrimitiveType Type) const
		{
			return (Type == EITwinClippingPrimitiveType::Box) ? Boxes : Planes;
		}
	};

	inline ITwin::ModelLink MakeModel(int32 Index)
	{
		return ITwin::ModelLink(EITwinModelType::IModel, FString::Printf(TEXT("Model_%d"), Index));
	}
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinClippingModelGroupsTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.ClippingModelGroups", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinClippingModelGroupsTest::RunTest(FString const& /*Parameters*/)
{
	// --- Models influenced by nothing land in the reserved group 0 ---
	{
		FFakeInfluenceSource Source;
		FITwinClippingModelGroups Groups;
		std::vector<ITwin::ModelLink> const Models{ MakeModel(0), MakeModel(1) };

		Groups.Rebuild(Source, Models);
		TestEqual(TEXT("Unclipped model -> NoClippingGroup"),
			Groups.GetGroupId(MakeModel(0)), FITwinClippingModelGroups::NoClippingGroup);
		TestFalse(TEXT("No overflow"), Groups.HasOverflowed());
		TestEqual(TEXT("Unknown model -> NoClippingGroup"),
			Groups.GetGroupId(MakeModel(42)), FITwinClippingModelGroups::NoClippingGroup);
	}

	// --- Models sharing the same influence set share a group; masks point back at them ---
	{
		FFakeInfluenceSource Source;
		Source.Boxes.push_back({ 100, { MakeModel(0), MakeModel(1) } });
		Source.Planes.push_back({ 200, { MakeModel(2) } });

		FITwinClippingModelGroups Groups;
		std::vector<ITwin::ModelLink> const Models{
			MakeModel(0), MakeModel(1), MakeModel(2), MakeModel(3) };
		TestTrue(TEXT("First rebuild reports a change"), Groups.Rebuild(Source, Models));

		const uint32 G0 = Groups.GetGroupId(MakeModel(0));
		const uint32 G2 = Groups.GetGroupId(MakeModel(2));
		TestEqual(TEXT("Same influence set -> same group"), Groups.GetGroupId(MakeModel(1)), G0);
		TestNotEqual(TEXT("Different influence set -> different group"), G2, G0);
		TestEqual(TEXT("Model 3 is unclipped"),
			Groups.GetGroupId(MakeModel(3)), FITwinClippingModelGroups::NoClippingGroup);
		TestNotEqual(TEXT("Clipped models never get group 0"),
			G0, FITwinClippingModelGroups::NoClippingGroup);

		const uint64 BoxMask = Groups.GetPrimitiveMask(EITwinClippingPrimitiveType::Box, 0);
		TestTrue(TEXT("Box mask has the bit of the group it influences"),
			(BoxMask & (uint64(1) << G0)) != 0);
		TestTrue(TEXT("Box mask has no bit for the group it does not influence"),
			(BoxMask & (uint64(1) << G2)) == 0);
		TestEqual(TEXT("Out-of-range primitive mask is 0"),
			Groups.GetPrimitiveMask(EITwinClippingPrimitiveType::Box, 7), uint64(0));
	}

	// --- An identical rebuild is a no-op and must report "unchanged" ---
	{
		FFakeInfluenceSource Source;
		Source.Boxes.push_back({ 100, { MakeModel(0) } });

		FITwinClippingModelGroups Groups;
		std::vector<ITwin::ModelLink> const Models{ MakeModel(0), MakeModel(1) };
		Groups.Rebuild(Source, Models);
		TestFalse(TEXT("Rebuilding an unchanged configuration reports no change"),
			Groups.Rebuild(Source, Models));
	}

	// --- R4: adding a primitive must not disturb the groups of unaffected models ---
	{
		FFakeInfluenceSource Source;
		Source.Boxes.push_back({ 100, { MakeModel(0) } });
		Source.Boxes.push_back({ 101, { MakeModel(1) } });

		FITwinClippingModelGroups Groups;
		std::vector<ITwin::ModelLink> const Models{ MakeModel(0), MakeModel(1), MakeModel(2) };
		Groups.Rebuild(Source, Models);
		const uint32 G0Before = Groups.GetGroupId(MakeModel(0));
		const uint32 G1Before = Groups.GetGroupId(MakeModel(1));

		// Insert a new box *in front*: with index-based signatures this shifted every bit and
		// reallocated all ids. With RefID-based signatures, only model 2 is affected.
		Source.Boxes.insert(Source.Boxes.begin(), { 102, { MakeModel(2) } });
		TestTrue(TEXT("Adding an influencing box reports a change"), Groups.Rebuild(Source, Models));

		TestEqual(TEXT("Model 0 keeps its group id"), Groups.GetGroupId(MakeModel(0)), G0Before);
		TestEqual(TEXT("Model 1 keeps its group id"), Groups.GetGroupId(MakeModel(1)), G1Before);
		TestNotEqual(TEXT("Model 2 is now clipped"),
			Groups.GetGroupId(MakeModel(2)), FITwinClippingModelGroups::NoClippingGroup);

		// Masks must follow the new indices, not the old ones.
		const uint32 G2 = Groups.GetGroupId(MakeModel(2));
		TestTrue(TEXT("Mask of the inserted box (index 0) targets model 2's group"),
			(Groups.GetPrimitiveMask(EITwinClippingPrimitiveType::Box, 0) & (uint64(1) << G2)) != 0);
	}

	// --- Removing a primitive frees its id for reuse ---
	{
		FFakeInfluenceSource Source;
		Source.Boxes.push_back({ 100, { MakeModel(0) } });

		FITwinClippingModelGroups Groups;
		std::vector<ITwin::ModelLink> const Models{ MakeModel(0), MakeModel(1) };
		Groups.Rebuild(Source, Models);

		Source.Boxes.clear();
		TestTrue(TEXT("Removing the last box reports a change"), Groups.Rebuild(Source, Models));
		TestEqual(TEXT("Model falls back to NoClippingGroup"),
			Groups.GetGroupId(MakeModel(0)), FITwinClippingModelGroups::NoClippingGroup);

		// The freed id must be available again.
		Source.Boxes.push_back({ 300, { MakeModel(1) } });
		Groups.Rebuild(Source, Models);
		TestNotEqual(TEXT("A reclaimed id is handed out to the new configuration"),
			Groups.GetGroupId(MakeModel(1)), FITwinClippingModelGroups::NoClippingGroup);
	}

	// --- Overflow fails "open": excess models stay unclipped, and it is reported ---
	{
		FFakeInfluenceSource Source;
		FITwinClippingModelGroups Groups;
		std::vector<ITwin::ModelLink> Models;

		// One distinct signature per model => one id each. Ids 1..MaxGroups-1 are usable.
		const int32 NumModels = static_cast<int32>(FITwinClippingModelGroups::MaxGroups) + 5;
		for (int32 i = 0; i < NumModels; ++i)
		{
			Models.push_back(MakeModel(i));
			Source.Boxes.push_back({ uint64(1000 + i), { MakeModel(i) } });
		}
		Groups.Rebuild(Source, Models);

		TestTrue(TEXT("Overflow is reported"), Groups.HasOverflowed());

		int32 NumAssigned = 0;
		std::set<uint32> DistinctIds;
		for (int32 i = 0; i < NumModels; ++i)
		{
			const uint32 Id = Groups.GetGroupId(MakeModel(i));
			if (Id != FITwinClippingModelGroups::NoClippingGroup)
			{
				++NumAssigned;
				DistinctIds.insert(Id);
			}
		}
		TestEqual(TEXT("Ids are never handed out twice"),
			static_cast<int32>(DistinctIds.size()), NumAssigned);
		TestEqual(TEXT("Exactly MaxGroups-1 models get an id, the rest fail open"),
			NumAssigned, static_cast<int32>(FITwinClippingModelGroups::MaxGroups) - 1);
	}

	// --- Every assigned id must fit in the mask width the shader can read ---
	{
		FFakeInfluenceSource Source;
		Source.Boxes.push_back({ 100, { MakeModel(0) } });
		FITwinClippingModelGroups Groups;
		std::vector<ITwin::ModelLink> const Models{ MakeModel(0) };
		Groups.Rebuild(Source, Models);
		TestTrue(TEXT("Group id fits in CLIPPING_MASK_WORDS * CLIPPING_BITS_PER_MASK_WORD bits"),
			Groups.GetGroupId(MakeModel(0))
				< uint32(ITwin::CLIPPING_MASK_WORDS * ITwin::CLIPPING_BITS_PER_MASK_WORD));
	}

	return true;
}

#endif // WITH_TESTS
