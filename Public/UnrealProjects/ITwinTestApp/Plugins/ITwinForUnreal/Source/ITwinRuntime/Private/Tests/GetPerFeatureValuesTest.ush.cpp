/*--------------------------------------------------------------------------------------+
|
|     $Source: GetPerFeatureValuesTest.ush.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <CoreMinimal.h>
#include <Misc/AutomationTest.h>
#include <Misc/LowLevelTestAdapter.h>

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <glm/glm.hpp>
#include <Compil/AfterNonUnrealIncludes.h>

#include <algorithm>
#include <limits>
#include <optional>

#ifdef WITH_TESTS

namespace Detail {

using int3 = glm::ivec3;
using uint = uint32;
using rgb = glm::dvec3;

struct float4
{
	rgb xyz;
	double w;

	float4(double x, double y, double z, double in_w)
	{
		xyz.x = x;
		xyz.y = y;
		xyz.z = z;
		w = in_w;
	}
	float4(double v = 0.) : float4(v, v, v, v) {}

	double& x() { return xyz.x; }
	double& y() { return xyz.y; }
	double& z() { return xyz.z; }
};
bool operator==(float4 const& a, float4 const& b)
{
	return a.xyz == b.xyz && a.w == b.w;
}

class TextureLoader
{
	FAutomationTestBase& Test;
	uint Dimension = 0;
	std::vector<float4> Data;
	friend struct InputsAggr;

public:
	TextureLoader(FAutomationTestBase& InTest, uint InDimension, float4 const& Default)
		: Test(InTest), Dimension(InDimension)
	{
		if (Dimension)
			Data.resize(Dimension * Dimension, Default);
	}
	void GetDimensions(uint& Out_x, uint& Out_y) const
	{
		Out_x = Out_y = Dimension;
	}
	float4 Load(int3 const& At) const
	{
		if (At.z == 0)
		{
			uint const Offset = At.y * Dimension + At.x;
			if (Offset < Data.size())
			{
				return Data[Offset];
			}
		}
		Test.AddError(TEXT("Invalid texture coordinates passed"));
		return float4(std::numeric_limits<double>::signaling_NaN());
	}
};

#pragma warning (push)
#pragma warning (disable:4701) // potentially uninitialized local variable
double GetPerFeatureValueWrapped(TextureLoader const& Synchro4D_RGBA_DATA,
	TextureLoader const& Synchro4D_CutPlanes_DATA, TextureLoader const& Selection_RGBA_DATA,
	uint const FeatureID, double BlinkingFactor, float4& Combined_RGBA, float4& Synchro4D_CutPlane)
{
	using std::min;
	#include "GeneratedShaderTest/GetPerFeatureValues.ush.inl"
}
#pragma warning (pop)

struct InputsAggr
{
	FAutomationTestBase& Test;
	TextureLoader tex4D, texSelHide, texPlanes;
	mutable float4 Combined_RGBA, Synchro4D_CutPlane;
	InputsAggr(FAutomationTestBase& InTest, uint dim4D) : InputsAggr(InTest, dim4D, 1, 1) {}
	InputsAggr(FAutomationTestBase& InTest, uint dim4D, uint dimSelHide) : InputsAggr(InTest, dim4D, dimSelHide, 1) {}
	InputsAggr(FAutomationTestBase& InTest, uint dim4D, uint dimSelHide, uint dimPlanes)
		: Test(InTest)
		// Default from FITwinSceneMapping::CreateHighlightsAndOpacitiesTexture: S4D_MAT_BGRA_DISABLED(255)
		, tex4D(InTest, dim4D, float4(0., 0., 0., 1.))
		// Default from ITwinSceneTile.cpp: COLOR_UNSELECT_ELEMENT_BGRA
		, texSelHide(InTest, dimSelHide, float4(0., 0., 0., 1.))
		// Default from FITwinSceneMapping::CreateCuttingPlanesTexture: S4D_CLIPPING_DISABLED
		, texPlanes(InTest, dimPlanes, float4(0.))
	{}
	void ResetOutputs() const
	{
		Combined_RGBA = float4();
		Synchro4D_CutPlane = float4();
	}
	void SetTex4D(uint const FeatureID, rgb const& RGB, double A = 1.)
	{
		if (tex4D.Data.size() > FeatureID)
			tex4D.Data[FeatureID] = float4(RGB.x, RGB.y, RGB.z, A);
		else
			Test.AddError(TEXT("Invalid FeatureID passed to SetTex4D"));
	}
	void FillTex4D(rgb const& RGB, double A = 1.)
	{
		for (auto& p : tex4D.Data)
			p = float4(RGB.x, RGB.y, RGB.z, A);
	}
	void SetTexSelHide(uint const FeatureID, rgb const& RGB, double A = 1.)
	{
		if (texSelHide.Data.size() > FeatureID)
			texSelHide.Data[FeatureID] = float4(RGB.x, RGB.y, RGB.z, A);
		else
			Test.AddError(TEXT("Invalid FeatureID passed to SetTexSelHide"));
	}
	void FillTexSelHide(rgb const& RGB, double A = 1.)
	{
		for (auto& p : texSelHide.Data)
			p = float4(RGB.x, RGB.y, RGB.z, A);
	}
	void SetTexPlanes(uint const FeatureID, double X, double Y, double Z)
	{
		if (texPlanes.Data.size() > FeatureID)
			texPlanes.Data[FeatureID] = float4(X, Y, Z, 1.);
		else
			Test.AddError(TEXT("Invalid FeatureID passed to SetTexPlanes"));
	}
	void FillTexPlanes(double X, double Y, double Z)
	{
		for (auto& p : texPlanes.Data)
			p = float4(X, Y, Z, 1.);
	}
	double Get(uint const FeatureID, double BlinkingFactor) const
	{
		ResetOutputs();
		return GetPerFeatureValueWrapped(tex4D, texPlanes, texSelHide, FeatureID, BlinkingFactor,
										 Combined_RGBA, Synchro4D_CutPlane);
	}
};

constexpr uint NoTexture = 1;
constexpr double FullBaseColor = 0.; // full base color used
constexpr double ColorOverride = 1.; // fully overridden base color output ratio
constexpr rgb No4DColor(0.);
constexpr rgb NoSelColor(0.);
constexpr rgb Selected(.4, .8, .1); // anything with a non-zero sum would do
constexpr rgb Any4DColor(.6, .2, .8); // anything with a non-zero sum would do
constexpr double AnyBlink = 0.25; // phase of the blinking cycle: anything not zero or one
constexpr rgb SelAnd4DBlinkedMix(0.55, 0.35, 0.625); // mix of Any4DColor and Selected by AnyBlink
constexpr double HiddenButSelectedBlinkedAlpha = 0.375; // current 1,5 x AnyBlink
constexpr double Opaque = 1.;
constexpr double Hidden = 0.;
constexpr double AnyAlpha = 0.8;
constexpr double SelectedAnyAlpha = 0.9; // when selected, translucency is halved

constexpr double tolerance = 1e-8;

bool ApproxEqual(double const a, double const b)
{
	return std::abs(a - b) < tolerance;
}

bool ApproxEqual(rgb const& a, rgb const& b)
{
	return ApproxEqual(a.x, b.x) && ApproxEqual(a.y, b.y) && ApproxEqual(a.z, b.z);
}

bool CheckOutRGBA(InputsAggr const& v, rgb const& col, double const alpha)
{
	return ApproxEqual(v.Combined_RGBA.xyz, col) && ApproxEqual(v.Combined_RGBA.w, alpha);
}

} // namespace Detail

BEGIN_DEFINE_SPEC(ShaderMixSpec, "Bentley.ITwinForUnreal.ITwinRuntime.GetPerFeatureValues",
				  EAutomationTestFlags::EngineFilter | EAutomationTestFlags_ApplicationContextMask)
END_DEFINE_SPEC(ShaderMixSpec)
void ShaderMixSpec::Define()
{
	using namespace Detail;
	Describe("A spec for the GetPerFeatureValues shader", [this]()
		{
			It("checks with empty textures we get full base color", [this]() {
				Detail::InputsAggr In(*this, NoTexture);
				CHECK(In.Get(0, Detail::AnyBlink) == Detail::FullBaseColor);
				CHECK(In.Synchro4D_CutPlane == float4(0.));
				In.SetTexPlanes(0, .2, .4, .7);
				CHECK(In.Get(0, Detail::AnyBlink) == Detail::FullBaseColor);
				CHECK(In.Synchro4D_CutPlane == float4(0.));// still zero: 1x1 is considered "empty" texture
				In.SetTexSelHide(0, Detail::Selected);
				CHECK(In.Get(0, Detail::AnyBlink) == Detail::FullBaseColor);
				CHECK(In.Synchro4D_CutPlane == float4(0.));
				In.SetTex4D(0, rgb(1.), 1.);
				CHECK(In.Get(0, Detail::AnyBlink) == Detail::FullBaseColor);
				CHECK(In.Synchro4D_CutPlane == float4(0.));
				});
			It("checks with too large FeatureID we get full base color", [this]() {
				Detail::InputsAggr In(*this, 2, 2, 2); // all 2x2 textures
				CHECK(In.Get(42, Detail::AnyBlink) == Detail::FullBaseColor);
				CHECK(In.Synchro4D_CutPlane == float4(0.));
				In.FillTexPlanes(.1, .2, .3);
				CHECK(In.Get(42, Detail::AnyBlink) == Detail::FullBaseColor);
				CHECK(In.Synchro4D_CutPlane == float4(0.));
				In.FillTexSelHide(Detail::Selected);
				CHECK(In.Get(42, Detail::AnyBlink) == Detail::FullBaseColor);
				CHECK(In.Combined_RGBA.xyz != Detail::Selected);
				CHECK(In.Synchro4D_CutPlane == float4(0.));
				In.FillTex4D(Detail::Any4DColor, 1.);
				CHECK(In.Get(42, Detail::AnyBlink) == Detail::FullBaseColor);
				CHECK(In.Combined_RGBA.xyz != Detail::Selected && In.Combined_RGBA.xyz != Detail::Any4DColor);
				CHECK(In.Synchro4D_CutPlane == float4(0.));
				});
			It("checks we get what's in the 4D and Planes texture (visible, blink=1)", [this]() {
				Detail::InputsAggr In(*this, 2, 2, 2); // all 2x2 textures
				In.FillTexPlanes(.1, .2, .3);
				In.FillTex4D(Detail::Any4DColor, Detail::AnyAlpha);
				In.SetTexSelHide(2, Detail::Selected, 1./*Element visibility untouched*/);
				// Non-selected cases
				CHECK(In.Get(0, Detail::AnyBlink) == Detail::ColorOverride);
				CHECK(In.Combined_RGBA.xyz == Detail::Any4DColor && In.Combined_RGBA.w == Detail::AnyAlpha);
				CHECK(In.Synchro4D_CutPlane == float4(.1, .2, .3, 1.));
				CHECK(In.Get(1, Detail::AnyBlink) == Detail::ColorOverride);
				CHECK(In.Combined_RGBA.xyz == Detail::Any4DColor && In.Combined_RGBA.w == Detail::AnyAlpha);
				CHECK(In.Synchro4D_CutPlane == float4(.1, .2, .3, 1.));
				// Simple selected case with blink=1 = full selected hilite
				CHECK(In.Get(2, /*blink:*/1.) == Detail::ColorOverride);
				// When selected but visibility is unaffected, 4D alpha is untouched
				CHECK(Detail::CheckOutRGBA(In, Detail::Selected, Detail::SelectedAnyAlpha));
				CHECK(In.Synchro4D_CutPlane == float4(.1, .2, .3, 1.));
				// Last non-selected case
				CHECK(In.Get(3, Detail::AnyBlink) == Detail::ColorOverride);
				CHECK(In.Combined_RGBA.xyz == Detail::Any4DColor && In.Combined_RGBA.w == Detail::AnyAlpha);
				CHECK(In.Synchro4D_CutPlane == float4(.1, .2, .3, 1.));
				});
			It("checks we get 4D color if non-zero, base otherwise (4D tex only)", [this]() {
				Detail::InputsAggr In(*this, 2); // 2x2 texture
				In.FillTex4D(Detail::Any4DColor, Detail::AnyAlpha);
				In.SetTex4D(2, Detail::No4DColor, Detail::AnyAlpha); // reset just this one to "no 4D color"
				CHECK(In.Get(0, Detail::AnyBlink) == Detail::ColorOverride);
				CHECK(In.Combined_RGBA.xyz == Detail::Any4DColor && In.Combined_RGBA.w == Detail::AnyAlpha);
				CHECK(In.Get(1, Detail::AnyBlink) == Detail::ColorOverride);
				CHECK(In.Combined_RGBA.xyz == Detail::Any4DColor && In.Combined_RGBA.w == Detail::AnyAlpha);
				CHECK(In.Get(2, Detail::AnyBlink) == Detail::FullBaseColor);
				CHECK(In.Combined_RGBA.xyz != Detail::Any4DColor && In.Combined_RGBA.w == Detail::AnyAlpha);
				CHECK(In.Get(3, Detail::AnyBlink) == Detail::ColorOverride);
				CHECK(In.Combined_RGBA.xyz == Detail::Any4DColor && In.Combined_RGBA.w == Detail::AnyAlpha);
				});
			It("checks we get 4D color if non-zero, base otherwise (all tex present)", [this]() {
				Detail::InputsAggr In(*this, 2, 2, 2); // all 2x2 textures
				In.FillTex4D(Detail::Any4DColor, Detail::AnyAlpha);
				In.SetTex4D(2, Detail::No4DColor, Detail::Opaque); // reset just this one to "no 4D color"
				CHECK(In.Get(0, Detail::AnyBlink) == Detail::ColorOverride);
				CHECK(In.Combined_RGBA.xyz == Detail::Any4DColor && In.Combined_RGBA.w == Detail::AnyAlpha);
				CHECK(In.Get(1, Detail::AnyBlink) == Detail::ColorOverride);
				CHECK(In.Combined_RGBA.xyz == Detail::Any4DColor && In.Combined_RGBA.w == Detail::AnyAlpha);
				CHECK(In.Get(2, Detail::AnyBlink) == Detail::FullBaseColor);
				CHECK(In.Combined_RGBA.xyz != Detail::Any4DColor && In.Combined_RGBA.w == Detail::Opaque);
				CHECK(In.Get(3, Detail::AnyBlink) == Detail::ColorOverride);
				CHECK(In.Combined_RGBA.xyz == Detail::Any4DColor && In.Combined_RGBA.w == Detail::AnyAlpha);
				});
			It("checks we get the Plane value when no 4D nor selection", [this]() {
				Detail::InputsAggr In(*this, 1, 1, 2); // only Planes texture
				In.FillTexPlanes(.1, .2, .3);
				CHECK(In.Get(2, Detail::AnyBlink) == Detail::FullBaseColor);
				CHECK(In.Synchro4D_CutPlane == float4(.1, .2, .3, 1.));
				});
			It("checks we get the Plane value without interference from selection/hiding", [this]() {
				Detail::InputsAggr In(*this, 1, 2, 2); // only Planes & selection textures
				In.FillTexPlanes(.1, .2, .3);
				In.SetTexSelHide(0, Detail::NoSelColor, Detail::Opaque); // = default
				In.SetTexSelHide(1, Detail::NoSelColor, Detail::Hidden);
				In.SetTexSelHide(2, Detail::Selected, Detail::Opaque);
				In.SetTexSelHide(3, Detail::Selected, Detail::Hidden);
				CHECK(In.Get(0, Detail::AnyBlink) == Detail::FullBaseColor);
				CHECK(In.Synchro4D_CutPlane == float4(.1, .2, .3, 1.));
				CHECK(In.Get(1, Detail::AnyBlink) == Detail::FullBaseColor);
				CHECK(In.Synchro4D_CutPlane == float4(.1, .2, .3, 1.));
				CHECK(In.Get(2, 1.) == Detail::ColorOverride);
				CHECK(In.Synchro4D_CutPlane == float4(.1, .2, .3, 1.));
				CHECK(In.Get(3, 1.) == Detail::ColorOverride);
				CHECK(In.Synchro4D_CutPlane == float4(.1, .2, .3, 1.));
				});
			It("checks we get the right mix of 4D and selection color", [this]() {
				Detail::InputsAggr In(*this, 2, 2, 2); // all 2x2 textures
				In.FillTexPlanes(.1, .2, .3);
				In.FillTex4D(Detail::Any4DColor, Detail::AnyAlpha);
				In.SetTexSelHide(0, Detail::NoSelColor, Detail::Opaque); // = default
				In.SetTexSelHide(1, Detail::NoSelColor, Detail::Hidden);
				In.SetTexSelHide(2, Detail::Selected, Detail::Opaque);
				In.SetTexSelHide(3, Detail::Selected, Detail::Hidden);
				CHECK(In.Get(0, Detail::AnyBlink) == Detail::ColorOverride);
				// Shader could be optimized by not reading the color in this case, but currently it's not:
				CHECK(In.Combined_RGBA.xyz == Detail::Any4DColor);
				CHECK(In.Combined_RGBA.w == Detail::AnyAlpha);
				CHECK(In.Get(1, Detail::AnyBlink) == Detail::ColorOverride);
				CHECK(In.Combined_RGBA.xyz == Detail::Any4DColor);
				CHECK(In.Combined_RGBA.w == Detail::Hidden);
				CHECK(In.Get(2, Detail::AnyBlink) == Detail::ColorOverride);
				CHECK(Detail::CheckOutRGBA(In, Detail::SelAnd4DBlinkedMix, Detail::SelectedAnyAlpha));
				CHECK(In.Get(3, Detail::AnyBlink) == Detail::ColorOverride);
				CHECK(Detail::CheckOutRGBA(In, Detail::SelAnd4DBlinkedMix, Detail::HiddenButSelectedBlinkedAlpha));
				});
			It("checks the output ratios and 4D alphas with selection (no 4D color)", [this]() {
				Detail::InputsAggr In(*this, 2, 2, 2); // all 2x2 textures
				In.FillTexPlanes(.1, .2, .3);
				In.SetTex4D(2, Detail::No4DColor, Detail::AnyAlpha);
				In.SetTex4D(3, Detail::No4DColor, Detail::AnyAlpha);
				In.SetTexSelHide(0, Detail::Selected, Detail::Opaque); // = default
				In.SetTexSelHide(1, Detail::Selected, Detail::Hidden);
				In.SetTexSelHide(2, Detail::Selected, Detail::Opaque);
				In.SetTexSelHide(3, Detail::Selected, Detail::Hidden);
				CHECK(In.Get(0, Detail::AnyBlink) == Detail::AnyBlink);
				CHECK(In.Combined_RGBA.xyz == Detail::Selected);
				CHECK(In.Combined_RGBA.w == Detail::Opaque);
				CHECK(In.Get(1, Detail::AnyBlink) == Detail::AnyBlink);
				CHECK(Detail::CheckOutRGBA(In, Detail::Selected, Detail::HiddenButSelectedBlinkedAlpha));
				CHECK(In.Get(2, Detail::AnyBlink) == Detail::AnyBlink);
				CHECK(Detail::CheckOutRGBA(In, Detail::Selected, Detail::SelectedAnyAlpha));
				CHECK(In.Get(3, Detail::AnyBlink) == Detail::AnyBlink);
				CHECK(Detail::CheckOutRGBA(In, Detail::Selected, Detail::HiddenButSelectedBlinkedAlpha));
				});
		}); // end of Describe call
} // end of Define method

#endif // WITH_TESTS
