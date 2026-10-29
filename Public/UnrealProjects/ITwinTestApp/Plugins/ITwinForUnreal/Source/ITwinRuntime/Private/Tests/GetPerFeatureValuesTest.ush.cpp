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
// glm only accepts a same-typed scalar: emulate HLSL's 'float * float3' with our double-based 'rgb'
inline rgb operator*(float s, rgb const& v) { return (double)s * v; }
inline rgb operator*(rgb const& v, float s) { return v * (double)s; }

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

// this test CPP uses doubles everywhere whereas the shader has some explicitly float constants...
// But what precision does it use for calculations? Use floats in this CPP as well? (cf. float4 etc. above)
namespace workaround
{
	template<typename T, typename U>
	double min(T t, U u) { return std::min((double)t, (double)u); }
	template<typename T, typename U>
	double max(T t, U u) { return std::max((double)t, (double)u); }
}

#pragma warning (push)
#pragma warning (disable:4701) // potentially uninitialized local variable
#define MATERIALBLENDING_ANY_TRANSLUCENT 0
double GetPerFeatureValueWrapped(TextureLoader const& Synchro4D_RGBA_DATA,
	TextureLoader const& Synchro4D_CutPlanes_DATA, TextureLoader const& Selection_RGBA_DATA,
	uint const FeatureID, double BlinkingFactor, float4& Combined_RGBA, float4& Synchro4D_CutPlane,
	float& HoverMask)
{
	using workaround::min;
	using workaround::max;
	HoverMask = 0.f;
	#include "GeneratedShaderTest/GetPerFeatureValues.ush.inl"
}
#undef MATERIALBLENDING_ANY_TRANSLUCENT
#pragma warning (pop)

struct InputsAggr
{
	FAutomationTestBase& Test;
	TextureLoader tex4D, texSelHide, texPlanes;
	mutable float4 Combined_RGBA, Synchro4D_CutPlane;
	mutable float HoverMask = 0.f;
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
		HoverMask = 0.f;
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
										 Combined_RGBA, Synchro4D_CutPlane, HoverMask);
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
constexpr double Opaque = 1.;
constexpr double Hidden = 0.;
constexpr double SolidSelection = 150. / 255.; // SELECTION_SOLID_ALPHA: selected without blinking, = mix ratio
constexpr double AnyAlpha = 0.8;
constexpr double UseOriginalAlphaIn = 0.0039; // no need to be exactly 1/255
constexpr double UseOriginalAlphaOut = 0.0049; // value hardcoded in shader
constexpr double SelectedAnyAlpha = 0.9; // when selected, translucency is halved

constexpr double tolerance = 1e-8;
// For values the shader stores in 'float' variables (the C++ build uses /fp:fast, so whether an explicit
// float rounding actually happens is not even predictable)
constexpr double floatTolerance = 1e-6;

bool ApproxEqual(double const a, double const b, double const tol = tolerance)
{
	return std::abs(a - b) < tol;
}

bool ApproxEqual(rgb const& a, rgb const& b, double const tol = tolerance)
{
	return ApproxEqual(a.x, b.x, tol) && ApproxEqual(a.y, b.y, tol) && ApproxEqual(a.z, b.z, tol);
}

bool CheckOutRGBA(InputsAggr const& v, rgb const& col, double const alpha, double const tol = tolerance)
{
	return ApproxEqual(v.Combined_RGBA.xyz, col, tol) && ApproxEqual(v.Combined_RGBA.w, alpha, tol);
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
			It("classifies only the reserved hover alpha byte", [this]() {
				constexpr double HoverMarkerAlpha = 2. / 255.;
				Detail::InputsAggr In(*this, 2, 2);
				In.SetTexSelHide(0, Detail::NoSelColor, HoverMarkerAlpha);
				In.Get(0, Detail::AnyBlink);
				CHECK(In.HoverMask == 1.f);
				In.SetTexSelHide(1, Detail::NoSelColor, 1. / 255.);
				In.Get(1, Detail::AnyBlink);
				CHECK(In.HoverMask == 0.f);
				In.SetTexSelHide(1, Detail::NoSelColor, 3. / 255.);
				In.Get(1, Detail::AnyBlink);
				CHECK(In.HoverMask == 0.f);
				In.SetTexSelHide(1, Detail::Selected, HoverMarkerAlpha);
				In.Get(1, Detail::AnyBlink);
				CHECK(In.HoverMask == 0.f);
				In.SetTexSelHide(1, Detail::NoSelColor, HoverMarkerAlpha);
				In.SetTex4D(1, Detail::Any4DColor);
				In.Get(1, Detail::AnyBlink);
				CHECK(In.HoverMask == 0.f);
				});
			It("preserves original opacity through selection without a 4D override", [this]() {
				Detail::InputsAggr In(*this, NoTexture, 4);
				CHECK(In.Get(2, Detail::AnyBlink) == Detail::FullBaseColor);
				CHECK(Detail::ApproxEqual(In.Combined_RGBA.w, Detail::UseOriginalAlphaOut,
					Detail::floatTolerance));
				for (double const SelectionAlpha : { Detail::Opaque, Detail::SolidSelection, 120. / 255. })
				{
					In.SetTexSelHide(2, Detail::Selected, SelectionAlpha);
					for (double const Blink : { 0., Detail::AnyBlink, 1. })
					{
						double const ExpectedRatio = SelectionAlpha == Detail::Opaque ? Blink : SelectionAlpha;
						CHECK(Detail::ApproxEqual(In.Get(2, Blink), ExpectedRatio, Detail::floatTolerance));
						CHECK(Detail::CheckOutRGBA(In, Detail::Selected, Detail::UseOriginalAlphaOut,
							Detail::floatTolerance));
					}
				}
				In.SetTexSelHide(2, Detail::NoSelColor, Detail::Opaque);
				CHECK(In.Get(2, Detail::AnyBlink) == Detail::FullBaseColor);
				CHECK(Detail::ApproxEqual(In.Combined_RGBA.w, Detail::UseOriginalAlphaOut,
					Detail::floatTolerance));
				In.SetTexSelHide(2, Detail::Selected, Detail::Hidden);
				CHECK(In.Get(2, Detail::AnyBlink) == Detail::FullBaseColor);
				CHECK(In.Combined_RGBA.w == Detail::Hidden);
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
				CHECK(In.Get(3, 1.) == Detail::FullBaseColor);
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
				CHECK(In.Get(1, Detail::AnyBlink) == Detail::FullBaseColor);
				CHECK(In.Combined_RGBA.xyz == Detail::Any4DColor);
				CHECK(In.Combined_RGBA.w == Detail::Hidden);
				CHECK(In.Get(2, Detail::AnyBlink) == Detail::ColorOverride);
				CHECK(Detail::CheckOutRGBA(In, Detail::SelAnd4DBlinkedMix, Detail::SelectedAnyAlpha));
				CHECK(In.Get(3, Detail::AnyBlink) == Detail::FullBaseColor);
				// Note: no longer displaying hidden elements when they are selected:
				CHECK(Detail::CheckOutRGBA(In, Detail::SelAnd4DBlinkedMix, Detail::Hidden));
				});
			It("checks the output ratios and 4D alphas with selection (no 4D color)", [this]() {
				Detail::InputsAggr In(*this, 2, 2, 2); // all 2x2 textures
				In.FillTexPlanes(.1, .2, .3);
				In.SetTex4D(2, Detail::No4DColor, Detail::AnyAlpha);
				In.SetTex4D(3, Detail::No4DColor, Detail::AnyAlpha);
				In.SetTexSelHide(0, Detail::Selected, Detail::Opaque);
				In.SetTexSelHide(1, Detail::Selected, Detail::Hidden);
				In.SetTexSelHide(2, Detail::Selected, Detail::Opaque);
				In.SetTexSelHide(3, Detail::Selected, Detail::Hidden);
				CHECK(In.Get(0, Detail::AnyBlink) == Detail::AnyBlink);
				CHECK(In.Combined_RGBA.xyz == Detail::Selected);
				CHECK(In.Combined_RGBA.w == Detail::Opaque);
				CHECK(In.Get(1, Detail::AnyBlink) == Detail::FullBaseColor);
				// Note: no longer displaying hidden elements when they are selected:
				CHECK(Detail::CheckOutRGBA(In, Detail::Selected, Detail::Hidden));
				CHECK(In.Get(2, Detail::AnyBlink) == Detail::AnyBlink);
				CHECK(Detail::CheckOutRGBA(In, Detail::Selected, Detail::SelectedAnyAlpha));
				CHECK(In.Get(3, Detail::AnyBlink) == Detail::FullBaseColor);
				// Note: no longer displaying hidden elements when they are selected:
				CHECK(Detail::CheckOutRGBA(In, Detail::Selected, Detail::Hidden));
				});
			It("checks the 'solid selection' alpha disables blinking and sets the mix ratio", [this]() {
				Detail::InputsAggr In(*this, 2, 2, 2); // all 2x2 textures
				In.SetTex4D(0, Detail::No4DColor, Detail::Opaque);
				In.SetTex4D(1, Detail::Any4DColor, Detail::AnyAlpha);
				In.SetTex4D(2, Detail::No4DColor, Detail::Opaque);
				In.SetTexSelHide(0, Detail::Selected, Detail::SolidSelection);
				In.SetTexSelHide(1, Detail::Selected, Detail::SolidSelection);
				In.SetTexSelHide(2, Detail::NoSelColor, Detail::SolidSelection);
				// No 4D color: the alpha is the base/selection mix ratio, whatever the blinking phase
				CHECK(ApproxEqual(In.Get(0, Detail::AnyBlink), Detail::SolidSelection, Detail::floatTolerance));
				CHECK(Detail::CheckOutRGBA(In, Detail::Selected, Detail::Opaque));
				CHECK(ApproxEqual(In.Get(0, 0.), Detail::SolidSelection, Detail::floatTolerance));
				// 4D color: mixed with the selection color by the same ratio
				CHECK(In.Get(1, Detail::AnyBlink) == Detail::ColorOverride);
				CHECK(Detail::CheckOutRGBA(In,
					Detail::Any4DColor * (1. - Detail::SolidSelection) + Detail::Selected * Detail::SolidSelection,
					Detail::SelectedAnyAlpha, Detail::floatTolerance));
				// Not selected: the intermediate alpha is just "visible"
				CHECK(In.Get(2, Detail::AnyBlink) == Detail::FullBaseColor);
				CHECK(In.Combined_RGBA.w == Detail::Opaque);
				});
			It("checks the 'use original alpha' value is handled", [this]() {
				Detail::InputsAggr In(*this, 2, 2, 2); // all 2x2 textures
				In.FillTexPlanes(.1, .2, .3);
				In.SetTex4D(0, Detail::Any4DColor, Detail::UseOriginalAlphaIn);
				In.SetTex4D(1, Detail::No4DColor, Detail::UseOriginalAlphaIn);
				In.SetTex4D(2, Detail::Any4DColor, Detail::UseOriginalAlphaIn);
				In.SetTex4D(3, Detail::No4DColor, Detail::UseOriginalAlphaIn);
				In.SetTexSelHide(0, Detail::NoSelColor, Detail::Opaque); // = default
				In.SetTexSelHide(1, Detail::NoSelColor, Detail::Hidden);
				In.SetTexSelHide(2, Detail::Selected, Detail::Opaque);
				In.SetTexSelHide(3, Detail::Selected, Detail::Hidden);
				CHECK(In.Get(0, Detail::AnyBlink) == Detail::ColorOverride);
				CHECK(ApproxEqual(In.Combined_RGBA.w, Detail::UseOriginalAlphaOut));
				CHECK(In.Get(1, Detail::AnyBlink) == Detail::FullBaseColor);
				CHECK(In.Combined_RGBA.w == Detail::Hidden);
				CHECK(In.Get(2, Detail::AnyBlink) == Detail::ColorOverride);
				CHECK(ApproxEqual(In.Combined_RGBA.w, Detail::UseOriginalAlphaOut));
				CHECK(In.Get(3, Detail::AnyBlink) == Detail::FullBaseColor);
				CHECK(In.Combined_RGBA.w == Detail::Hidden);
				});
		}); // end of Describe call
} // end of Define method

#endif // WITH_TESTS
