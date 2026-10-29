/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTileExcluderTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Clipping/ITwinBoxTileExcluder.h>
#include <Clipping/ITwinPlaneTileExcluder.h>
#include <Clipping/ITwinClippingPlaneInfo.h>

#include <Misc/AutomationTest.h>

#if WITH_DEV_AUTOMATION_TESTS

#include <CesiumTile.h>

#include <UObject/GCObjectScopeGuard.h>
#include <UObject/Package.h>

namespace ITwinTileExcluderTest
{
	//! Builds a transient tile whose bounds are the axis-aligned box [Min, Max].
	UCesiumTile* MakeTile(FVector3d const& Min, FVector3d const& Max)
	{
		UCesiumTile* Tile = NewObject<UCesiumTile>(GetTransientPackage());
		Tile->Bounds = FBoxSphereBounds(FBox(Min, Max));
		return Tile;
	}
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FITwinBoxTileExcluderTest,
	"Bentley.ITwin.Clipping.BoxTileExcluder",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FITwinBoxTileExcluderTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ITwinTileExcluderTest;

	UITwinBoxTileExcluder* Excluder = NewObject<UITwinBoxTileExcluder>(GetTransientPackage());
	UCesiumTile* const Tile =
		MakeTile(FVector3d(0.), FVector3d(100.));

	// no box registered -> never excluded
	TestFalse(TEXT("Empty excluder keeps the tile"), Excluder->ShouldExclude_Implementation(Tile));

	auto Props = std::make_shared<FITwinClippingBoxInfo::FBoxProperties>();
	Props->bIsSubtractive = false;
	Props->BoxBounds = FBoxSphereBounds(FBox(FVector(500.), FVector(600.))); // disjoint
	Excluder->AddBox(Props);
	TestTrue(TEXT("Non-overlapping additive box excludes the tile"),
		Excluder->ShouldExclude_Implementation(Tile));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FITwinPlaneTileExcluderTest,
	"Bentley.ITwin.Clipping.PlaneTileExcluder",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FITwinPlaneTileExcluderTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ITwinTileExcluderTest;

	UITwinPlaneTileExcluder* Excluder = NewObject<UITwinPlaneTileExcluder>(GetTransientPackage());
	FGCObjectScopeGuard ExcluderGuard(Excluder);

	// Horizontal plane at Z = 100, normal pointing up.
	Excluder->SetPlaneEquation(FVector3d::ZAxisVector, 100.);

	UCesiumTile* const TileAbove =
		MakeTile(FVector3d(-50., -50., 200.), FVector3d(50., 50., 300.));
	UCesiumTile* const TileBelow =
		MakeTile(FVector3d(-50., -50., -300.), FVector3d(50., 50., -200.));
	UCesiumTile* const TileStraddling =
		MakeTile(FVector3d(-50., -50., 0.), FVector3d(50., 50., 300.));

	// --- Default orientation: the half-space in front of the plane is discarded. ---
	TestTrue(TEXT("Tile fully in front of the plane is excluded"),
		Excluder->ShouldExclude_Implementation(TileAbove));
	TestFalse(TEXT("Tile fully behind the plane is kept"),
		Excluder->ShouldExclude_Implementation(TileBelow));
	TestFalse(TEXT("Tile straddling the plane is kept"),
		Excluder->ShouldExclude_Implementation(TileStraddling));

	// --- Inverted effect: the opposite half-space is discarded. ---
	Excluder->SetInvertEffect(true);
	TestTrue(TEXT("Invert: ShouldInvertEffect reflects the setter"),
		Excluder->ShouldInvertEffect());
	TestFalse(TEXT("Invert: tile in front of the plane is kept"),
		Excluder->ShouldExclude_Implementation(TileAbove));
	TestTrue(TEXT("Invert: tile behind the plane is excluded"),
		Excluder->ShouldExclude_Implementation(TileBelow));
	TestFalse(TEXT("Invert: tile straddling the plane is kept"),
		Excluder->ShouldExclude_Implementation(TileStraddling));

	Excluder->SetInvertEffect(false);

	// --- A tile exactly on the plane counts as "behind" (step() is inclusive at 0). ---
	UCesiumTile* const TileOnPlane =
		MakeTile(FVector3d(-50., -50., 100.), FVector3d(50., 50., 100.));
	TestFalse(TEXT("Tile lying exactly on the plane is kept"),
		Excluder->ShouldExclude_Implementation(TileOnPlane));

	// --- Non-axis-aligned plane, to validate the dot product path. ---
	FVector3d const Diagonal = FVector3d(1., 1., 1.).GetSafeNormal();
	Excluder->SetPlaneEquation(Diagonal, 0.);
	TestTrue(TEXT("Diagonal plane: tile in the positive octant is excluded"),
		Excluder->ShouldExclude_Implementation(
			MakeTile(FVector3d(10., 10., 10.), FVector3d(20., 20., 20.))));
	TestFalse(TEXT("Diagonal plane: tile in the negative octant is kept"),
		Excluder->ShouldExclude_Implementation(
			MakeTile(FVector3d(-20., -20., -20.), FVector3d(-10., -10., -10.))));

	// --- Default-constructed excluder: plane Z = 0, nothing inverted. ---
	UITwinPlaneTileExcluder* const Fresh = NewObject<UITwinPlaneTileExcluder>(GetTransientPackage());
	FGCObjectScopeGuard FreshGuard(Fresh);
	TestFalse(TEXT("Fresh excluder does not invert by default"), Fresh->ShouldInvertEffect());
	TestEqual(TEXT("Fresh excluder has no plane index"), Fresh->GetPlaneIndex(), -1);
	TestTrue(TEXT("Fresh excluder discards what is above Z = 0"),
		Fresh->ShouldExclude_Implementation(TileAbove));

	return true;
}


//---------------------------------------------------------------------------------------
// FITwinClippingPlaneInfo
//---------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FITwinClippingPlaneInfoTest,
	"Bentley.ITwin.Clipping.ClippingPlaneInfo",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FITwinClippingPlaneInfoTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ITwinTileExcluderTest;

	FITwinClippingPlaneInfo PlaneInfo;

	// --- Defaults. ---
	TestFalse(TEXT("Plane info does not invert by default"), PlaneInfo.GetInvertEffect());
	TestEqual(TEXT("Default plane orientation is +Z"),
		PlaneInfo.GetPlaneEquation().PlaneOrientation, FVector::ZAxisVector);
	TestEqual(TEXT("Default plane W is 0"), PlaneInfo.GetPlaneEquation().PlaneW, 0.);

	// --- SetPlaneEquation stores the equation. ---
	const FVector Orientation = FVector(0., 1., 0.);
	PlaneInfo.SetPlaneEquation(Orientation, 42.);
	TestEqual(TEXT("SetPlaneEquation stores the orientation"),
		PlaneInfo.GetPlaneEquation().PlaneOrientation, Orientation);
	TestEqual(TEXT("SetPlaneEquation stores W"), PlaneInfo.GetPlaneEquation().PlaneW, 42.);

	// --- GetEffectivePlaneEquation folds the inversion in. ---
	TestEqual(TEXT("Effective equation matches the raw one when not inverted"),
		PlaneInfo.GetEffectivePlaneEquation().PlaneOrientation, Orientation);
	PlaneInfo.SetInvertEffect(true);
	TestTrue(TEXT("SetInvertEffect updates the info"), PlaneInfo.GetInvertEffect());
	TestEqual(TEXT("Effective orientation is negated when inverted"),
		PlaneInfo.GetEffectivePlaneEquation().PlaneOrientation, -Orientation);
	TestEqual(TEXT("Effective W is negated when inverted"),
		PlaneInfo.GetEffectivePlaneEquation().PlaneW, -42.);
	PlaneInfo.SetInvertEffect(false);

	// --- Propagation to an attached excluder. ---
	UITwinPlaneTileExcluder* Excluder = NewObject<UITwinPlaneTileExcluder>(GetTransientPackage());
	// TileExcluders are held as TWeakObjectPtr: keep the excluder alive for the whole test.
	FGCObjectScopeGuard ExcluderGuard(Excluder);
	PlaneInfo.RecordTileExcluder(Excluder);

	// Plane at Z = 100 pointing up: what is above gets discarded.
	PlaneInfo.SetPlaneEquation(FVector::ZAxisVector, 100.);

	UCesiumTile* const TileAbove =
		MakeTile(FVector3d(-50., -50., 200.), FVector3d(50., 50., 300.));
	UCesiumTile* const TileBelow =
		MakeTile(FVector3d(-50., -50., -300.), FVector3d(50., 50., -200.));

	TestTrue(TEXT("Propagated equation: tile above the plane is excluded"),
		Excluder->ShouldExclude_Implementation(TileAbove));
	TestFalse(TEXT("Propagated equation: tile below the plane is kept"),
		Excluder->ShouldExclude_Implementation(TileBelow));

	// Moving the plane up past the tile must be reflected in the excluder.
	PlaneInfo.SetPlaneEquation(FVector::ZAxisVector, 1000.);
	TestFalse(TEXT("Raising the plane above the tile keeps it"),
		Excluder->ShouldExclude_Implementation(TileAbove));

	// --- bPropagateToTileExcluders = false leaves the excluder untouched. ---
	PlaneInfo.SetPlaneEquation(FVector::ZAxisVector, 100., /*bPropagateToTileExcluders*/false);
	TestEqual(TEXT("Non-propagating setter still updates the info"),
		PlaneInfo.GetPlaneEquation().PlaneW, 100.);
	TestFalse(TEXT("Non-propagating setter does not touch the excluder"),
		Excluder->ShouldExclude_Implementation(TileAbove));

	// --- SetInvertEffect propagates to the excluder. ---
	PlaneInfo.SetPlaneEquation(FVector::ZAxisVector, 100.);
	TestFalse(TEXT("Excluder is not inverted initially"), Excluder->ShouldInvertEffect());

	PlaneInfo.SetInvertEffect(true);
	TestTrue(TEXT("SetInvertEffect propagates to the excluder"), Excluder->ShouldInvertEffect());
	TestFalse(TEXT("Inverted: tile above the plane is now kept"),
		Excluder->ShouldExclude_Implementation(TileAbove));
	TestTrue(TEXT("Inverted: tile below the plane is now excluded"),
		Excluder->ShouldExclude_Implementation(TileBelow));

	PlaneInfo.SetInvertEffect(false);
	TestFalse(TEXT("Un-inverting propagates back to the excluder"), Excluder->ShouldInvertEffect());
	TestTrue(TEXT("Un-inverted: tile above the plane is excluded again"),
		Excluder->ShouldExclude_Implementation(TileAbove));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
