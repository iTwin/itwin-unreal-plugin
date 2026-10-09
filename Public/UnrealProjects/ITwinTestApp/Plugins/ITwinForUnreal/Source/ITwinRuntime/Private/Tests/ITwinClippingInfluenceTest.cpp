/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingInfluenceTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS

#include <Clipping/ITwinClippingInfoBase.h>
#include <Clipping/ITwinClippingInfoBase.inl>
#include <ITwinModelType.h>

#include <Tests/ITwinAutomationTestBaseNoLogs.h>

#include <Misc/AutomationTest.h>
#include <Misc/LowLevelTestAdapter.h>

namespace
{
	/// Minimal concrete effect: FITwinClippingInfoBase is abstract only in intent (all its virtuals
	/// have a default implementation), so we just need a type that can report an "invert" state in
	/// order to exercise CopyGenericInfoFrom.
	struct FTestClippingInfo : public FITwinClippingInfoBase
	{
		bool bInvert = false;

		bool GetInvertEffect() const override { return bInvert; }

	protected:
		void DoSetInvertEffect(bool bInInvert) override { bInvert = bInInvert; }
	};

	inline ITwin::ModelLink MakeModel(EITwinModelType Type, TCHAR const* Id)
	{
		return ITwin::ModelLink(Type, FString(Id));
	}
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinClippingInfluenceTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.ClippingInfluence", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinClippingInfluenceTest::RunTest(FString const& /*Parameters*/)
{
	ITwin::ModelLink const IModelA = MakeModel(EITwinModelType::IModel, TEXT("IModelA"));
	ITwin::ModelLink const IModelB = MakeModel(EITwinModelType::IModel, TEXT("IModelB"));
	ITwin::ModelLink const RealityA = MakeModel(EITwinModelType::RealityData, TEXT("RealityA"));
	ITwin::ModelLink const GoogleTiles = MakeModel(EITwinModelType::GlobalMapLayer, TEXT("Google"));

	// --- A brand new effect influences nothing ---
	{
		FTestClippingInfo Info;
		TestTrue(TEXT("A new effect is enabled"), Info.IsEnabled());
		TestFalse(TEXT("New effect does not influence any iModel"), Info.DoesInfluenceModel(IModelA));
		TestFalse(TEXT("New effect does not influence reality data"), Info.DoesInfluenceModel(RealityA));
		TestFalse(TEXT("New effect does not influence global map layers"), Info.DoesInfluenceModel(GoogleTiles));
		TestFalse(TEXT("New effect is not using per-layer-type influence"), Info.IsUsingPerLayerTypeInfluence());
	}

	// --- Influence is routed per layer type and the three sets are independent ---
	{
		FTestClippingInfo Info;
		Info.SetInfluenceFullModelType(EITwinModelType::IModel, true);

		TestTrue(TEXT("iModels are fully influenced"),
			Info.ShouldInfluenceFullModelType(EITwinModelType::IModel));
		TestFalse(TEXT("Reality data is untouched by the iModel setting"),
			Info.ShouldInfluenceFullModelType(EITwinModelType::RealityData));
		TestFalse(TEXT("Global map layers are untouched by the iModel setting"),
			Info.ShouldInfluenceFullModelType(EITwinModelType::GlobalMapLayer));

		// bInfluenceAll covers every id of that type, including ids never seen before.
		TestTrue(TEXT("bInfluenceAll covers any iModel id"), Info.DoesInfluenceModel(IModelA));
		TestTrue(TEXT("bInfluenceAll covers unknown iModel ids"), Info.DoesInfluenceModel(IModelB));
		TestFalse(TEXT("bInfluenceAll on iModels does not leak to reality data"),
			Info.DoesInfluenceModel(RealityA));
		TestFalse(TEXT("bInfluenceAll on iModels does not leak to global map layers"),
			Info.DoesInfluenceModel(GoogleTiles));
	}

	// --- Specific ids only influence the exact (type, id) pair ---
	{
		FTestClippingInfo Info;
		Info.SetInfluenceSpecificModel(IModelA, true);

		TestTrue(TEXT("The selected iModel is influenced"), Info.DoesInfluenceModel(IModelA));
		TestFalse(TEXT("Another iModel is not influenced"), Info.DoesInfluenceModel(IModelB));

		// Same id string, different layer type => must NOT match.
		TestFalse(TEXT("An identical id under another layer type is not influenced"),
			Info.DoesInfluenceModel(MakeModel(EITwinModelType::RealityData, TEXT("IModelA"))));

		TestFalse(TEXT("Specific ids alone do not mean per-layer-type influence"),
			Info.IsUsingPerLayerTypeInfluence());

		Info.SetInfluenceSpecificModel(IModelA, false);
		TestFalse(TEXT("Deselecting removes the influence"), Info.DoesInfluenceModel(IModelA));

		// Setting the same state twice must be a no-op, not a toggle.
		Info.SetInfluenceSpecificModel(IModelB, true);
		Info.SetInfluenceSpecificModel(IModelB, true);
		TestTrue(TEXT("Selecting twice keeps the model influenced"), Info.DoesInfluenceModel(IModelB));
	}

	// --- ShouldInfluenceModel additionally honors the enabled state, DoesInfluenceModel does not ---
	{
		FTestClippingInfo Info;
		Info.SetInfluenceSpecificModel(IModelA, true);
		TestTrue(TEXT("Enabled effect influences the model"), Info.ShouldInfluenceModel(IModelA));

		Info.SetEnabled(false);
		TestFalse(TEXT("Disabled effect does not influence the model"), Info.ShouldInfluenceModel(IModelA));
		TestTrue(TEXT("Disabling preserves the influence settings"), Info.DoesInfluenceModel(IModelA));

		Info.SetEnabled(true);
		TestTrue(TEXT("Re-enabling restores the influence"), Info.ShouldInfluenceModel(IModelA));
	}

	// --- IsUsingPerLayerTypeInfluence reports true as soon as ONE layer type is "all" ---
	{
		FTestClippingInfo Info;
		TestFalse(TEXT("No layer type is 'all' initially"), Info.IsUsingPerLayerTypeInfluence());

		Info.SetInfluenceFullModelType(EITwinModelType::GlobalMapLayer, true);
		TestTrue(TEXT("A single 'all' layer type is enough"), Info.IsUsingPerLayerTypeInfluence());

		Info.SetInfluenceFullModelType(EITwinModelType::GlobalMapLayer, false);
		TestFalse(TEXT("Clearing the last 'all' layer type switches back to per-layer"),
			Info.IsUsingPerLayerTypeInfluence());
	}

	// --- ConvertToPerLayerInfluence must not lose the current influence ---
	{
		FTestClippingInfo Info;
		Info.SetInfluenceFullModelType(EITwinModelType::IModel, true);
		Info.SetInfluenceSpecificModel(RealityA, true);

		TMap<EITwinModelType, TSet<FString>> CurrentLayers;
		CurrentLayers.Add(EITwinModelType::IModel, TSet<FString>{ TEXT("IModelA"), TEXT("IModelB") });
		CurrentLayers.Add(EITwinModelType::RealityData, TSet<FString>{ TEXT("RealityA") });

		Info.ConvertToPerLayerInfluence(CurrentLayers);

		TestFalse(TEXT("Conversion clears the per-layer-type flag"), Info.IsUsingPerLayerTypeInfluence());
		TestFalse(TEXT("iModels are no longer influenced as a whole type"),
			Info.ShouldInfluenceFullModelType(EITwinModelType::IModel));

		// The models that were implicitly influenced must now be explicitly influenced.
		TestTrue(TEXT("Loaded iModel A is still influenced after conversion"),
			Info.DoesInfluenceModel(IModelA));
		TestTrue(TEXT("Loaded iModel B is still influenced after conversion"),
			Info.DoesInfluenceModel(IModelB));

		// A layer that was not loaded at conversion time cannot be captured, and must NOT be
		// influenced afterwards (this is the behavioral difference with 'influence all').
		TestFalse(TEXT("An iModel loaded after the conversion is not influenced"),
			Info.DoesInfluenceModel(MakeModel(EITwinModelType::IModel, TEXT("IModelC"))));

		// Layer types that were already per-layer must be left strictly untouched.
		TestTrue(TEXT("Pre-existing specific influence is preserved"), Info.DoesInfluenceModel(RealityA));
	}

	// --- Conversion of an "all" layer type absent from the loaded layers map influences nothing ---
	{
		FTestClippingInfo Info;
		Info.SetInfluenceFullModelType(EITwinModelType::RealityData, true);

		TMap<EITwinModelType, TSet<FString>> const NoLayers;
		Info.ConvertToPerLayerInfluence(NoLayers);

		TestFalse(TEXT("No loaded layer of that type => nothing influenced"),
			Info.DoesInfluenceModel(RealityA));
		TestFalse(TEXT("The 'all' flag is cleared anyway"),
			Info.ShouldInfluenceFullModelType(EITwinModelType::RealityData));
	}

	// --- SetInfluenceNone resets every layer type at once ---
	{
		FTestClippingInfo Info;
		Info.SetInfluenceFullModelType(EITwinModelType::IModel, true);
		Info.SetInfluenceFullModelType(EITwinModelType::GlobalMapLayer, true);
		Info.SetInfluenceSpecificModel(RealityA, true);

		Info.SetInfluenceNone();

		TestFalse(TEXT("iModels are no longer influenced"), Info.DoesInfluenceModel(IModelA));
		TestFalse(TEXT("Reality data is no longer influenced"), Info.DoesInfluenceModel(RealityA));
		TestFalse(TEXT("Global map layers are no longer influenced"), Info.DoesInfluenceModel(GoogleTiles));
		TestFalse(TEXT("No layer type is 'all' anymore"), Info.IsUsingPerLayerTypeInfluence());
		TestTrue(TEXT("SetInfluenceNone invalidates the influence bounding box"),
			Info.NeedsUpdateInfluenceBoundingBox());
	}

	// --- CopyGenericInfoFrom copies enabled state, invert state and the three influence sets ---
	{
		FTestClippingInfo Source;
		Source.SetEnabled(false);
		Source.SetInvertEffect(true);
		Source.SetInfluenceFullModelType(EITwinModelType::GlobalMapLayer, true);
		Source.SetInfluenceSpecificModel(IModelA, true);
		Source.SetInfluenceSpecificModel(RealityA, true);

		FTestClippingInfo Target;
		Target.CopyGenericInfoFrom(Source);

		TestFalse(TEXT("Enabled state is copied"), Target.IsEnabled());
		TestTrue(TEXT("Invert state is copied"), Target.GetInvertEffect());
		TestTrue(TEXT("GlobalMapLayer 'all' flag is copied"),
			Target.ShouldInfluenceFullModelType(EITwinModelType::GlobalMapLayer));
		TestTrue(TEXT("iModel specific influence is copied"), Target.DoesInfluenceModel(IModelA));
		TestTrue(TEXT("RealityData specific influence is copied"), Target.DoesInfluenceModel(RealityA));
		TestFalse(TEXT("Non-influenced models stay non-influenced"), Target.DoesInfluenceModel(IModelB));

		// The copy must be deep: editing the target must not affect the source.
		Target.SetInfluenceSpecificModel(IModelB, true);
		TestFalse(TEXT("Editing the copy does not affect the source"), Source.DoesInfluenceModel(IModelB));
	}

	// --- Influence changes invalidate the cached influence bounding box ---
	{
		FTestClippingInfo Info;
		Info.SetInfluenceSpecificModel(IModelA, true);
		TestTrue(TEXT("Adding a specific model invalidates the bounding box"),
			Info.NeedsUpdateInfluenceBoundingBox());

		FTestClippingInfo Info2;
		Info2.SetInfluenceFullModelType(EITwinModelType::IModel, true);
		TestTrue(TEXT("Switching a layer type to 'all' invalidates the bounding box"),
			Info2.NeedsUpdateInfluenceBoundingBox());
	}

	return true;
}

#endif // WITH_TESTS
