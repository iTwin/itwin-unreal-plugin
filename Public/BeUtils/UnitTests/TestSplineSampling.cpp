/*--------------------------------------------------------------------------------------+
|
|     $Source: TestSplineSampling.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <catch2/catch_all.hpp>
#include <BeUtils/SplineSampling/Basic2DMap.h>
#include <BeUtils/SplineSampling/SplineSampling.h>
#include <BeUtils/SplineSampling/OcclusionMap.h>
#include <BeUtils/SplineSampling/SplineHelper.h>
#include <BeUtils/SplineSampling/SplinePattern.h>

#include <cmath>
#include <numbers>


class ParabolaSplineCurve final : public BeUtils::SplineCurve
{
public:
	ParabolaSplineCurve() {}

	glm::dvec3 GetPositionAtCoord(value_type const& u) const override
	{
		return glm::dvec3(
			u,
			2. * u * u - 1.,
			5.);
	}
	glm::dvec3 GetTangentAtCoord(value_type const& u) const override
	{
		// Not used for test, can return anything.
		return glm::dvec3(1., u, 0.);
	}
	size_t PointCount(const bool /*accountForCyclicity*/) const override { return 2; }
	glm::dvec3 GetPositionAtIndex(size_t idx) const override
	{
		if (idx == 0)
			return glm::dvec3(0., -1., 5.);
		else if (idx == 1)
			return glm::dvec3(1., 1., 5.);
		else
		{
			BE_ISSUE("out of range", idx);
			return glm::dvec3(0.);
		}
	}
	bool IsCyclic() const override { return false; }

	static glm::dvec2 GetParabolaPosition(double u)
	{
		return glm::dvec2(
			u,
			2. * u * u - 1);
	}
};

class ClosedParabolaSplineCurve final : public BeUtils::SplineCurve
{
public:
	ClosedParabolaSplineCurve() {}

	glm::dvec3 GetPositionAtCoord(value_type const& u) const override
	{
		if (u < 0.5)
		{
			value_type const v = 2. * u;
			return glm::dvec3(
				v,
				2. * v * v - 1.,
				5.);
		}
		else if (u < 0.75)
		{
			double v = u - 0.5;
			return glm::dvec3(
				1. - (4. * v),
				1.,
				5.);
		}
		else if (u <= 1.0)
		{
			double v = u - 0.75;
			return glm::dvec3(
				0.,
				1. - (8. * v),
				5.);
		}
		else
		{
			BE_ISSUE("out of range", u);
			return glm::dvec3(0.);
		}
	}
	glm::dvec3 GetTangentAtCoord(value_type const& u) const override
	{
		// Not used for test, can return anything.
		return glm::dvec3(1., u, 0.);
	}

	bool IsCyclic() const override
	{
		return true;
	}
	size_t PointCount(const bool accountForCyclicity) const override
	{
		return 3 + (accountForCyclicity ? 1 : 0);
	}

	glm::dvec3 GetPositionAtIndex(size_t idx) const override
	{
		if (idx == 0 || idx == 3)
			return glm::dvec3(0., -1., 5.);
		else if (idx == 1)
			return glm::dvec3(1., 1., 5.);
		else if (idx == 2)
			return glm::dvec3(0., 1., 5.);
		else
		{
			BE_ISSUE("out of range", idx);
			return glm::dvec3(0.);
		}
	}
};

static inline glm::dvec3 GetProjectedPosition(double u)
{
	return glm::dvec3(ParabolaSplineCurve::GetParabolaPosition(u), 0.);
}

namespace
{
	static constexpr double InvalidProjectionValue = -100000.;

	/// Adapts a 3D world-to-screen projector (like FScreenSpaceProjector) to the Spline2DProjector interface
	/// expected by BeUtils::SampleSplinePath.
	class Test2DProjector final : public BeUtils::Spline2DProjector
	{
	public:
		Test2DProjector(std::optional<double> const& inOptYBound)
			: optYBound_(inOptYBound)
		{}

		bool MayFail() const override { return optYBound_.has_value(); }

		std::optional<vec2_type> Project2DOpt(vec3_type const& pos) const override
		{
			if (optYBound_ && pos.y < *optYBound_)
			{
				return std::nullopt;
			}
			else
			{
				return vec2_type(pos.x, pos.y);
			}
		}

		std::optional<vec3_type> Project3DOpt(vec3_type const& pos) const override
		{
			std::optional<vec2_type> Proj2D = Project2DOpt(pos);
			if (Proj2D)
			{
				return vec3_type(*Proj2D, 0.);
			}
			else
			{
				return std::nullopt;
			}
		}

		vec2_type Project2D(vec3_type const& pos) const override
		{
			return Project2DOpt(pos).value_or(vec2_type(InvalidProjectionValue, InvalidProjectionValue));
		}

		vec3_type Project3D(vec3_type const& pos) const override
		{
			return vec3_type(Project2D(pos), 0.);
		}

		BeUtils::E2DProjection GetProjection() const override
		{
			return BeUtils::E2DProjection::Z_Axis;
		}

	private:
		// If provided, this will cause the projector to fail for any point with a y coordinate below the
		// given bound. This allows testing how SampleSplinePath handles projection failures.
		const std::optional<double> optYBound_;
	};
}

static void SampleTestSplineStroke(
	std::vector<glm::dvec3>& outSampledPositions,
	std::optional<uint32_t> const& fixedNbInstances,
	std::optional<double> const& fixedSpacing = std::nullopt,
	std::optional<double> const& optYBound = std::nullopt)
{
	ParabolaSplineCurve const testCurve;

	BeUtils::SplineSamplingParameters samplingParams;
	samplingParams.samplingMode = BeUtils::ESplineSamplingMode::AlongPath;
	samplingParams.fixedNbInstances = fixedNbInstances;
	if (fixedSpacing)
	{
		samplingParams.fixedSpacing = glm::dvec2(*fixedSpacing, 0.);
	}

	BeUtils::TransformHolder const identityTsf;

	samplingParams.customProjector = std::make_unique<Test2DProjector>(optYBound);

	outSampledPositions.clear();
	BeUtils::SampleSplinePath(testCurve, identityTsf, samplingParams, outSampledPositions);
}

static void SampleTestSplineStrokeRandomSpacing(
	std::vector<glm::dvec3>& outSampledPositions,
	double minDist, double maxDist,
	uint32_t randSeed = 0xbac1981)
{
	ParabolaSplineCurve const testCurve;

	BeUtils::SplineSamplingParameters samplingParams;
	samplingParams.samplingMode = BeUtils::ESplineSamplingMode::AlongPath;
	samplingParams.randomSpacing = glm::dvec2(minDist, maxDist);
	samplingParams.randSeed = randSeed;

	BeUtils::TransformHolder const identityTsf;

	samplingParams.customProjector = std::make_unique<Test2DProjector>(std::nullopt);

	// Pre-fill to check that the output is always cleared, even on early-outs.
	outSampledPositions.assign(3, glm::dvec3(42.));
	BeUtils::SampleSplinePath(testCurve, identityTsf, samplingParams, outSampledPositions);
}

static void SampleTestSplineInterior(
	std::vector<glm::dvec3>& outSampledPositions,
	bool forceAligned = false,
	std::optional<double> const& fixedSpacing = std::nullopt,
	std::optional<double> const& optYBound = std::nullopt,
	double instanceDim = 0.05,
	float gridRotAngle = 0.f,
	std::optional<float> const& density = std::nullopt)
{
	ClosedParabolaSplineCurve const testCurve;

	BeUtils::SplineSamplingParameters samplingParams;
	samplingParams.samplingMode = BeUtils::ESplineSamplingMode::Interior;
	samplingParams.forceAligned = forceAligned;
	samplingParams.gridRotAngle = gridRotAngle;
	if (density)
	{
		samplingParams.density = *density;
	}
	if (fixedSpacing)
	{
		samplingParams.fixedSpacing = glm::dvec2(*fixedSpacing, 0.);
	}

	BeUtils::TransformHolder const identityTsf;

	samplingParams.customProjector = std::make_unique<Test2DProjector>(optYBound);

	AdvViz::SDK::BoundingBox const samplingBox_World = {
		{ 0., -1., 4. },
		{ 1., 1., 6. }
	};
	glm::dvec3 const averageInstanceDims_World = { instanceDim, instanceDim, instanceDim };

	outSampledPositions.clear();
	BeUtils::SampleSplineInterior(testCurve, identityTsf, samplingBox_World, averageInstanceDims_World, samplingParams, outSampledPositions);
}

/// Analytical inside test for #ClosedParabolaSplineCurve: the enclosed region is bounded by the
/// parabola y = 2x^2 - 1 (below), the line y = 1 (above) and the line x = 0 (left).
/// The baked polyline approximating the (convex) parabola lies above the true curve, so any point
/// inside the polygon is also inside the analytical region: no tolerance is needed on that side.
static bool IsInsideClosedParabola(glm::dvec3 const& pos, double tol = 1e-9)
{
	return pos.x >= -tol && pos.x <= 1. + tol
		&& pos.y <= 1. + tol
		&& pos.y >= (2. * pos.x * pos.x - 1.) - tol;
}

static void CheckAllInsideClosedParabola(std::vector<glm::dvec3> const& sampledPositions)
{
	REQUIRE(sampledPositions.size() > 0);
	size_t nOutside = 0;
	for (const auto& pos : sampledPositions)
	{
		if (!IsInsideClosedParabola(pos))
			++nOutside;
	}
	CHECK(nOutside == 0);
}

static void CheckSampling(std::vector<glm::dvec3> const& sampledPositions,
	double x_start, double x_end,
	double toleranceForXbounds = 1e-8,
	std::optional<double> const& y_bound = std::nullopt)
{
	REQUIRE(sampledPositions.size() > 0);
	CHECK(std::fabs(sampledPositions.front().x - x_start) < toleranceForXbounds);
	double prevU = x_start - toleranceForXbounds;
	for (const auto& pos : sampledPositions)
	{
		if (y_bound)
		{
			CHECK(pos.y >= *y_bound); // check the effect of the projection failure bound
		}
		CHECK(glm::distance(pos, GetProjectedPosition(pos.x)) < 1e-6);
		CHECK(pos.x > prevU);
		CHECK(pos.x <= x_end);
		prevU = pos.x;
	}
	CHECK(std::fabs(sampledPositions.back().x - x_end) < toleranceForXbounds);
}

TEST_CASE("TestStroke_FixedNbSamples")
{
	{
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineStroke(sampledPositions, 16);
		REQUIRE(sampledPositions.size() == 16);
		CheckSampling(sampledPositions, 0., 1.);
		CHECK(glm::distance(sampledPositions.front(), glm::dvec3(0., -1., 0.)) < 1e-6);
		CHECK(glm::distance(sampledPositions[4], GetProjectedPosition(0.4465408805)) < 1e-6);
		CHECK(glm::distance(sampledPositions[10], GetProjectedPosition(0.7924528301886)) < 1e-6);
		CHECK(glm::distance(sampledPositions.back(), glm::dvec3(1., 1., 0.)) < 1e-6);
	}
	{
		// Test with just 2 samples to ensure that the code correctly handles the case where the fixed number
		// of instances equals the number of points in the curve.
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineStroke(sampledPositions, 2);
		REQUIRE(sampledPositions.size() == 2);
		CHECK(glm::distance(sampledPositions.front(), glm::dvec3(0., -1., 0.)) < 1e-6);
		CHECK(glm::distance(sampledPositions.back(), glm::dvec3(1., 1., 0.)) < 1e-6);
	}
}

TEST_CASE("TestStroke_FixedSpacing")
{
	{
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineStroke(sampledPositions, std::nullopt, 0.05);
		REQUIRE(sampledPositions.size() == 47);
		CheckSampling(sampledPositions, 0., 1., 1e-2);
		CHECK(glm::distance(sampledPositions.front(), glm::dvec3(0., -1., 0.)) < 1e-6);
		CHECK(glm::distance(sampledPositions[4], GetProjectedPosition(0.185185185)) < 1e-6);
		CHECK(glm::distance(sampledPositions[10], GetProjectedPosition(0.38344226579)) < 1e-6);
		CHECK(glm::distance(sampledPositions[37], GetProjectedPosition(0.880174291939)) < 1e-6);
	}
	{
		// Test with a too large spacing => this should result in only one point (start).
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineStroke(sampledPositions, std::nullopt, 5.0);
		REQUIRE(sampledPositions.size() == 1);
		CHECK(glm::distance(sampledPositions.front(), glm::dvec3(0., -1., 0.)) < 1e-6);
	}
	{
		// Invalid spacing (contract: x must be > 0) => fail safely with an empty result.
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineStroke(sampledPositions, std::nullopt, 0.0);
		CHECK(sampledPositions.empty());
		SampleTestSplineStroke(sampledPositions, std::nullopt, -0.1);
		CHECK(sampledPositions.empty());
	}
}

TEST_CASE("TestStroke_FixedNbSamples_WithProjectionFailure")
{
	{
		std::vector<glm::dvec3> sampledPositions;
		const double yBound = 0.;
		SampleTestSplineStroke(sampledPositions, 16, std::nullopt, yBound);
		REQUIRE(sampledPositions.size() == 16);

		// the first point should be close to that at u=0.71, (~ sqrt(2) / 2) which is the first point on the
		// curve that has a y coordinate above 0.
		CheckSampling(sampledPositions, 0.71, 1., 1e-2, yBound);
	}
	{
		// Test what happens when all points fail to project: in this case, the code should return an empty
		// vector.
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineStroke(sampledPositions, 64, std::nullopt, 50.);
		REQUIRE(sampledPositions.empty());
	}
	{
		// If the bound does not filter out anything, we should get the same result as with no failure.
		std::vector<glm::dvec3> sampledPositions;
		const double yBound = -50.;
		SampleTestSplineStroke(sampledPositions, 16, std::nullopt, yBound);
		REQUIRE(sampledPositions.size() == 16);
		CheckSampling(sampledPositions, 0., 1., 1e-8, yBound);
	}
}

TEST_CASE("TestStroke_RandomSpacing")
{
	// Reference length of the (projected) test parabola, evaluated with the same helper as the
	// sampling code, so that the count bounds below do not depend on a hard-coded value.
	const double curveLen = [&]
	{
		ParabolaSplineCurve const testCurve;
		BeUtils::SplineHelper const splineHelper(&testCurve);
		BeUtils::TransformHolder const identityTsf;
		Test2DProjector const projector(std::nullopt);
		return splineHelper.EvalSplineLength(identityTsf, 0.01, projector).totalLength;
	}();
	REQUIRE(curveLen > 1.);

	// Bounds on the number of samples: the first sample is always the start point, then each step
	// is in [minDist, maxDist] along the (polyline approximation of the) curve. A small relative
	// tolerance absorbs the polyline vs. exact length difference.
	auto minCount = [curveLen](double maxDist) -> size_t
	{
		return static_cast<size_t>(std::floor(curveLen * 0.95 / maxDist));
	};
	auto maxCount = [curveLen](double minDist) -> size_t
	{
		return static_cast<size_t>(std::ceil(curveLen * 1.05 / minDist)) + 1;
	};

	// Nominal case: consecutive samples must be spaced in [minDist, maxDist] along the curve; as
	// the curve is convex the chord is always shorter than the arc, so only the upper bound can be
	// checked exactly on chord lengths; the lower bound is checked with a tolerance accounting for
	// the fine sampling resolution.
	{
		const double minDist = 0.1, maxDist = 0.3;
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineStrokeRandomSpacing(sampledPositions, minDist, maxDist);

		REQUIRE(sampledPositions.size() >= 2);
		CHECK(sampledPositions.size() >= minCount(maxDist));
		CHECK(sampledPositions.size() <= maxCount(minDist));

		// first point is the start of the curve, all points lie on the (projected) curve
		CHECK(glm::distance(sampledPositions.front(), glm::dvec3(0., -1., 0.)) < 1e-6);
		double prevX = -1.;
		for (size_t i = 0; i < sampledPositions.size(); ++i)
		{
			auto const& pos = sampledPositions[i];
			CHECK(glm::distance(pos, GetProjectedPosition(pos.x)) < 1e-6);
			CHECK(pos.x > prevX); // monotonic along the path
			prevX = pos.x;
			if (i > 0)
			{
				const double chord = glm::distance(pos, sampledPositions[i - 1]);
				CHECK(chord <= maxDist + 1e-6);
				CHECK(chord >= minDist * 0.9);
			}
		}
	}

	// Determinism: same seed => same result; different seed => (very likely) different result.
	{
		std::vector<glm::dvec3> a, b, c;
		SampleTestSplineStrokeRandomSpacing(a, 0.1, 0.3, 1234);
		SampleTestSplineStrokeRandomSpacing(b, 0.1, 0.3, 1234);
		SampleTestSplineStrokeRandomSpacing(c, 0.1, 0.3, 5678);
		REQUIRE(a.size() == b.size());
		for (size_t i = 0; i < a.size(); ++i)
		{
			CHECK(glm::distance(a[i], b[i]) < 1e-12);
		}
		CHECK(a != c);
	}

	// minDist == maxDist degenerates to a regular spacing.
	{
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineStrokeRandomSpacing(sampledPositions, 0.2, 0.2);
		REQUIRE(sampledPositions.size() >= 2);
		CHECK(sampledPositions.size() >= minCount(0.2));
		CHECK(sampledPositions.size() <= maxCount(0.2));
	}

	// Spacing larger than the whole curve => only the start point.
	{
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineStrokeRandomSpacing(sampledPositions, curveLen * 2., curveLen * 4.);
		REQUIRE(sampledPositions.size() == 1);
		CHECK(glm::distance(sampledPositions.front(), glm::dvec3(0., -1., 0.)) < 1e-6);
	}

	// Invalid / degenerated ranges must fail safely (empty output, no crash, no huge allocation).
	{
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineStrokeRandomSpacing(sampledPositions, 0., 0.); // null range
		CHECK(sampledPositions.empty());
	}
	{
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineStrokeRandomSpacing(sampledPositions, 0., 0.3); // minDist == 0
		CHECK(sampledPositions.empty());
	}
	{
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineStrokeRandomSpacing(sampledPositions, -0.1, 0.3); // minDist < 0
		CHECK(sampledPositions.empty());
	}
	{
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineStrokeRandomSpacing(sampledPositions, 0.3, 0.1); // maxDist < minDist
		CHECK(sampledPositions.empty());
	}
	{
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineStrokeRandomSpacing(sampledPositions, 0.1, -0.3); // maxDist < 0
		CHECK(sampledPositions.empty());
	}

	// Extremely small minDist: the fine sampling buffer is capped, the call must complete and
	// return a bounded, finite set of samples (steps are uniform in ]0, 0.5], so with a fixed seed
	// the count is deterministic; we only check a generous hard upper bound here).
	{
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineStrokeRandomSpacing(sampledPositions, 1e-9, 0.5);
		CHECK(sampledPositions.size() >= 2);
		CHECK(sampledPositions.size() <= 200);
		for (auto const& pos : sampledPositions)
		{
			CHECK(std::isfinite(pos.x));
			CHECK(std::isfinite(pos.y));
		}
	}
}

TEST_CASE("TestInteriorSampling")
{
	{
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineInterior(sampledPositions, true /*forceAligned*/);
		CHECK(sampledPositions.size() > 0);
		for (const auto& pos : sampledPositions)
		{
			CHECK(pos.x >= 0.);
			CHECK(pos.x <= 1.);
			CHECK(pos.y >= -1.);
			CHECK(pos.y <= 1.);
		}
	}

	{
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineInterior(sampledPositions, false /*forceAligned*/);
		CHECK(sampledPositions.size() > 0);
		for (const auto& pos : sampledPositions)
		{
			CHECK(pos.x >= 0.);
			CHECK(pos.x <= 1.);
			CHECK(pos.y >= -1.);
			CHECK(pos.y <= 1.);
		}
	}
}

TEST_CASE("TestInteriorSampling_NullOrTinyDensity")
{
	// Null density: no instance at all, and no division by zero when computing the cell size.
	{
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineInterior(sampledPositions, false /*forceAligned*/, std::nullopt, std::nullopt,
			0.05, 0.f, 0.f /*density*/);
		CHECK(sampledPositions.empty());
	}
	{
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineInterior(sampledPositions, true /*forceAligned*/, std::nullopt, std::nullopt,
			0.05, 0.f, 0.f /*density*/);
		CHECK(sampledPositions.empty());
	}
	// Negative density is treated as null.
	{
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineInterior(sampledPositions, false /*forceAligned*/, std::nullopt, std::nullopt,
			0.05, 0.f, -1.f /*density*/);
		CHECK(sampledPositions.empty());
	}
	// Extremely small density (random mode, where density drives the cell size): the raw instance
	// count would be far below 1 (box area = 2, instance area = 0.0025 => 800 instances at density
	// 1, times (1e-4)^2 => 8e-6). This used to yield a division by zero / NaN cell size; we now
	// clamp to a single cell covering the whole box and expect finite positions inside the outline.
	{
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineInterior(sampledPositions, false /*forceAligned*/, std::nullopt, std::nullopt,
			0.05, 0.f, 1e-4f /*density*/);
		for (const auto& pos : sampledPositions)
		{
			CHECK(std::isfinite(pos.x));
			CHECK(std::isfinite(pos.y));
			CHECK(std::isfinite(pos.z));
			CHECK(IsInsideClosedParabola(pos));
		}
		// With a single cell covering the whole box, at most a handful of positions can be produced.
		CHECK(sampledPositions.size() <= 4);
	}
}

TEST_CASE("TestInteriorSampling_AllPositionsInsideOutline")
{
	// Fine grid: random (Poisson-jittered) and aligned modes.
	{
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineInterior(sampledPositions, false /*forceAligned*/);
		CheckAllInsideClosedParabola(sampledPositions);
	}
	{
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineInterior(sampledPositions, true /*forceAligned*/);
		CheckAllInsideClosedParabola(sampledPositions);
	}
	// Coarse grid (large instances => few, big cells): this is the case where jittered positions
	// used to escape the outline.
	{
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineInterior(sampledPositions, false /*forceAligned*/, std::nullopt, std::nullopt, 0.3);
		CheckAllInsideClosedParabola(sampledPositions);
	}
	// Rotated aligned grid: positions are snapped into cells that do not match the grid lattice.
	{
		std::vector<glm::dvec3> sampledPositions;
		SampleTestSplineInterior(sampledPositions, true /*forceAligned*/, std::nullopt, std::nullopt, 0.1,
			static_cast<float>(std::numbers::pi / 5.));
		CheckAllInsideClosedParabola(sampledPositions);
	}
}

TEST_CASE("TestOcclusionMap_CellClassification")
{
	using BeUtils::OcclusionMap;

	ClosedParabolaSplineCurve const testCurve;
	BeUtils::TransformHolder const identityTsf;
	AdvViz::SDK::BoundingBox const box = { { 0., -1., 4. }, { 1., 1., 6. } };

	const int nX = 20, nY = 40;
	OcclusionMap map(box, nX, nY);

	BeUtils::SplineHelper const splineHelper(&testCurve);
	BeUtils::SplinePattern pattern(identityTsf, splineHelper);
	pattern.SetOcclusion(false);
	REQUIRE(map.BuildFrom2DPattern(pattern));
	REQUIRE(map.GetOutlineSegments().size() >= 3);

	size_t nInterior = 0, nBoundary = 0, nOutside = 0;
	for (int j = 0; j < nY; ++j)
	{
		for (int i = 0; i < nX; ++i)
		{
			const int cellIndex = i + j * nX;
			const double cx = map.GetStart2DPosX() + i * map.GetCellWidth();
			const double cy = map.GetStart2DPosY() + j * map.GetCellHeight();
			const double hw = 0.5 * map.GetCellWidth();
			const double hh = 0.5 * map.GetCellHeight();

			switch (map.GetCellKind(cellIndex))
			{
			case OcclusionMap::ECellKind::Interior:
				++nInterior;
				// All 4 corners of an interior cell must be inside the outline.
				CHECK(map.IsInsideOutline(cx - hw, cy - hh));
				CHECK(map.IsInsideOutline(cx + hw, cy - hh));
				CHECK(map.IsInsideOutline(cx - hw, cy + hh));
				CHECK(map.IsInsideOutline(cx + hw, cy + hh));
				CHECK(map.GetValueAtCell(cellIndex) > 0.);
				break;
			case OcclusionMap::ECellKind::Outside:
				++nOutside;
				CHECK(!map.IsInsideOutline(cx, cy));
				CHECK(map.GetValueAtCell(cellIndex) <= 0.);
				break;
			case OcclusionMap::ECellKind::Boundary:
				++nBoundary;
				break;
			}
		}
	}
	// The parabola region covers a good part of the box: all three kinds must be present, and the
	// boundary must be a thin band (far fewer cells than the interior).
	CHECK(nInterior > 0);
	CHECK(nOutside > 0);
	CHECK(nBoundary > 0);
	CHECK(nBoundary < nInterior);

	// Sampled positions must all be inside, in both modes.
	{
		std::vector<glm::dvec3> positions;
		map.GetSampledPositions(positions, false, 1234u, 0.f);
		REQUIRE(!positions.empty());
		for (auto const& pos : positions)
		{
			CHECK(map.IsInsideOutline(pos.x, pos.y));
			CHECK(IsInsideClosedParabola(pos));
		}
	}
	{
		std::vector<glm::dvec3> positions;
		map.GetSampledPositions(positions, true, 1234u, 0.3f);
		REQUIRE(!positions.empty());
		for (auto const& pos : positions)
		{
			CHECK(map.IsInsideOutline(pos.x, pos.y));
			CHECK(IsInsideClosedParabola(pos));
		}
	}
}

//---------------------------------------------------------------------------------------
// TBasic2DMap tests.
// #BasicDouble2DMap is the only explicitly instantiated specialization (see the
// 'template class TBasic2DMap<double>;' at the end of Basic2DMap.cpp), and it is not
// abstract here since IS_EON_DEV() is 0 => it can be used directly.
//---------------------------------------------------------------------------------------

namespace
{
	using BeUtils::BasicDouble2DMap;

	/// Axis-aligned box of the given 2D dimensions, starting at the origin (the Z extent is
	/// irrelevant for TBasic2DMap, which only uses min/max on X and Y).
	static AdvViz::SDK::BoundingBox MakeMapBox(double width, double height)
	{
		return { { 0., 0., 0. }, { width, height, 1. } };
	}

	/// Fills the map cells with their own index, cast to double (cell (i,j) => i + j * width).
	static void FillWithCellIndices(BasicDouble2DMap& map)
	{
		map.AllocateData(0.);
		for (int cellIndex = 0; cellIndex < map.CountCells(); ++cellIndex)
		{
			map.SetValueAtCell(cellIndex, static_cast<double>(cellIndex));
		}
	}

	/// Filter for #ComputeMeanWithFilter: only non-negative values contribute to the mean.
	struct NonNegative2DFilter
	{
		bool ShouldAddContribution(double value) const { return value >= 0.; }
	};
}

TEST_CASE("TestBasic2DMap_Init")
{
	// Nominal case: one cell per unit, no supersampling.
	{
		BasicDouble2DMap const map(MakeMapBox(2., 3.), 2, 3);
		CHECK(map.GetWidth() == 2);
		CHECK(map.GetHeight() == 3);
		CHECK(map.CountCells() == 6);
		CHECK(map.GetSuperSamplingFactor() == 1);
		CHECK(map.GetCellWidth() == Catch::Approx(1.));
		CHECK(map.GetCellHeight() == Catch::Approx(1.));
		// The map always starts at the *center* of the first cell.
		CHECK(map.GetStart2DPosX() == Catch::Approx(0.5));
		CHECK(map.GetStart2DPosY() == Catch::Approx(0.5));
		CHECK(map.GetResolution() == glm::vec2(2.f, 3.f));

		double minX = 0., maxX = 0., minY = 0., maxY = 0.;
		map.Get2DBoxInfo(minX, maxX, minY, maxY);
		CHECK(minX == Catch::Approx(0.));
		CHECK(maxX == Catch::Approx(2.));
		CHECK(minY == Catch::Approx(0.));
		CHECK(maxY == Catch::Approx(3.));
	}

	// Degenerated cell counts are clamped to at least one cell (no division by zero).
	{
		BasicDouble2DMap const map(MakeMapBox(2., 2.), 0, -4);
		CHECK(map.GetWidth() == 1);
		CHECK(map.GetHeight() == 1);
		CHECK(map.CountCells() == 1);
		CHECK(map.GetCellWidth() == Catch::Approx(2.));
		CHECK(map.GetCellHeight() == Catch::Approx(2.));
	}

	// InitWith must fully reset a previously initialized map.
	{
		BasicDouble2DMap map(MakeMapBox(2., 3.), 2, 3);
		map.InitWith(MakeMapBox(4., 4.), 4, 2);
		CHECK(map.GetWidth() == 4);
		CHECK(map.GetHeight() == 2);
		CHECK(map.CountCells() == 8);
		CHECK(map.GetCellWidth() == Catch::Approx(1.));
		CHECK(map.GetCellHeight() == Catch::Approx(2.));
	}
}

TEST_CASE("TestBasic2DMap_SuperSamplingAndDistribQuality")
{
	// Supersampling multiplies the resolution along both axes.
	{
		BasicDouble2DMap const map(MakeMapBox(2., 3.), 2, 3, 3 /*superSamplingFactor*/);
		CHECK(map.GetSuperSamplingFactor() == 3);
		CHECK(map.GetWidth() == 6);
		CHECK(map.GetHeight() == 9);
		CHECK(map.CountCells() == 54);
		CHECK(map.GetCellWidth() == Catch::Approx(2. / 6.));
		CHECK(map.GetCellHeight() == Catch::Approx(3. / 9.));
		CHECK(map.GetStart2DPosX() == Catch::Approx(1. / 6.));
		CHECK(map.GetStart2DPosY() == Catch::Approx(1. / 6.));
	}

	// A quality of 1 (the default) leaves the requested resolution untouched.
	{
		BasicDouble2DMap const map(MakeMapBox(4., 6.), 4, 6, 1, 1.f /*fDistribQuality*/);
		CHECK(map.GetWidth() == 4);
		CHECK(map.GetHeight() == 6);
		CHECK(map.CountCells() == 24);
	}

	// A quality below 1 undersamples the map (rounded up).
	{
		BasicDouble2DMap const map(MakeMapBox(4., 6.), 4, 6, 1, 0.5f);
		CHECK(map.GetWidth() == 2);
		CHECK(map.GetHeight() == 3);
		CHECK(map.CountCells() == 6);
		CHECK(map.GetCellWidth() == Catch::Approx(2.));
		CHECK(map.GetCellHeight() == Catch::Approx(2.));
	}
	{
		// ceilf(0.3 * 2) = 1 along both axes.
		BasicDouble2DMap const map(MakeMapBox(2., 2.), 2, 2, 1, 0.3f);
		CHECK(map.GetWidth() == 1);
		CHECK(map.GetHeight() == 1);
	}

	// A null or negative quality must not produce an empty map.
	{
		BasicDouble2DMap const mapNull(MakeMapBox(2., 2.), 8, 8, 1, 0.f);
		CHECK(mapNull.GetWidth() == 1);
		CHECK(mapNull.GetHeight() == 1);
		CHECK(mapNull.CountCells() == 1);

		BasicDouble2DMap const mapNeg(MakeMapBox(2., 2.), 8, 8, 1, -1.f);
		CHECK(mapNeg.GetWidth() == 1);
		CHECK(mapNeg.GetHeight() == 1);
		CHECK(mapNeg.CountCells() == 1);
	}

	// Supersampling takes precedence over the (lower) quality factor.
	{
		BasicDouble2DMap const map(MakeMapBox(2., 2.), 2, 2, 2 /*superSamplingFactor*/, 0.5f);
		CHECK(map.GetWidth() == 4);
		CHECK(map.GetHeight() == 4);
		CHECK(map.CountCells() == 16);
	}

	// The data buffer follows the (super/under)sampled cell count.
	{
		BasicDouble2DMap map(MakeMapBox(2., 2.), 2, 2, 4);
		map.AllocateData(0.5);
		REQUIRE(map.CountCells() == 64);
		CHECK(map.GetValueAtCell(63) == Catch::Approx(0.5));
		CHECK(map.ComputeMeanValue() == Catch::Approx(0.5));
	}

	// Custom cell count (3D populator mode: one cell per mesh face) overrides the grid count,
	// while the 2D grid dimensions are left unchanged.
	{
		BasicDouble2DMap map(MakeMapBox(2., 2.), 2, 2, 1, 1.f, 10 /*nCustomCells*/);
		CHECK(map.GetWidth() == 2);
		CHECK(map.GetHeight() == 2);
		CHECK(map.CountCells() == 10);
		map.AllocateData(1.);
		map.SetValueAtCell(9, 11.);
		CHECK(map.ComputeMeanValue() == Catch::Approx(2.)); // (9 * 1 + 11) / 10
	}
	{
		// A custom count matching the grid count is a no-op.
		BasicDouble2DMap const map(MakeMapBox(2., 2.), 2, 2, 1, 1.f, 4);
		CHECK(map.CountCells() == 4);
	}
}

TEST_CASE("TestBasic2DMap_AllocateAndFill")
{
	BasicDouble2DMap map(MakeMapBox(2., 3.), 2, 3);
	map.AllocateData(0.25);
	REQUIRE(map.CountCells() == 6);
	for (int cellIndex = 0; cellIndex < map.CountCells(); ++cellIndex)
	{
		CHECK(map.GetValueAtCell(cellIndex) == Catch::Approx(0.25));
	}
	CHECK(map.ComputeMeanValue() == Catch::Approx(0.25));

	// FillMapWithValue overwrites every cell, keeping the allocated size.
	map.SetValueAtCell(0, 42.);
	map.FillMapWithValue(2.);
	for (int cellIndex = 0; cellIndex < map.CountCells(); ++cellIndex)
	{
		CHECK(map.GetValueAtCell(cellIndex) == Catch::Approx(2.));
	}
	CHECK(map.ComputeMeanValue() == Catch::Approx(2.));

	// Mean of heterogeneous values.
	FillWithCellIndices(map);
	CHECK(map.ComputeMeanValue() == Catch::Approx(2.5)); // (0+1+2+3+4+5) / 6

	// Re-allocating resets all the values.
	map.AllocateData(-1.);
	CHECK(map.ComputeMeanValue() == Catch::Approx(-1.));

	// FillMapWithValue on a non-allocated map is a no-op (it must not allocate nor crash).
	BasicDouble2DMap emptyMap(MakeMapBox(2., 2.), 2, 2);
	emptyMap.FillMapWithValue(1.);
	CHECK(emptyMap.CountCells() == 4); // cell count is independent from the actual allocation
}

TEST_CASE("TestBasic2DMap_EvaluateAndGetRawValueAt")
{
	// 2x2 cells over a 2x2 box => cell size 1, cell centers at 0.5 and 1.5 on both axes.
	BasicDouble2DMap map(MakeMapBox(2., 2.), 2, 2);
	FillWithCellIndices(map); // values: 0 1 / 2 3

	// Raw value: nearest cell, no blending.
	CHECK(map.GetRawValueAt(0.5, 0.5) == Catch::Approx(0.));
	CHECK(map.GetRawValueAt(1.6, 0.4) == Catch::Approx(1.));
	CHECK(map.GetRawValueAt(0.4, 1.6) == Catch::Approx(2.));
	CHECK(map.GetRawValueAt(1.9, 1.9) == Catch::Approx(3.));
	// Positions outside the box are clamped to the border cells.
	CHECK(map.GetRawValueAt(10., 10.) == Catch::Approx(3.));

	// With interpolation enabled (the default), EvaluateValueAt blends the 4 neighbors and
	// ignores the cell index.
	CHECK(map.EvaluateValueAt(1., 1., -1) == Catch::Approx(1.5)); // (0+1+2+3) / 4
	CHECK(map.EvaluateValueAt(0.5, 0.5, -1) == Catch::Approx(0.));

	// With interpolation disabled, the cell index is used verbatim.
	map.EnableInterpolation(false);
	CHECK(map.EvaluateValueAt(1., 1., 3) == Catch::Approx(3.));
	CHECK(map.EvaluateValueAt(0., 0., 2) == Catch::Approx(2.));
	map.EnableInterpolation(true);
	CHECK(map.EvaluateValueAt(1., 1., 3) == Catch::Approx(1.5));
}

TEST_CASE("TestBasic2DMap_InterpolateValidValueAt")
{
	BasicDouble2DMap map(MakeMapBox(2., 2.), 2, 2);
	FillWithCellIndices(map); // values: 0 1 / 2 3

	// Discarded value high enough to never trigger => plain bilinear interpolation.
	{
		const double noDiscard = 1000.;
		// On a cell center, the weights collapse on that cell.
		CHECK(map.InterpolateValidValueAt(0.5, 0.5, noDiscard) == Catch::Approx(0.));
		CHECK(map.InterpolateValidValueAt(1.5, 0.5, noDiscard) == Catch::Approx(1.));
		CHECK(map.InterpolateValidValueAt(0.5, 1.5, noDiscard) == Catch::Approx(2.));
		CHECK(map.InterpolateValidValueAt(1.5, 1.5, noDiscard) == Catch::Approx(3.));
		// Halfway between two cells along X, and at the center of the 4 cells.
		CHECK(map.InterpolateValidValueAt(1., 0.5, noDiscard) == Catch::Approx(0.5));
		CHECK(map.InterpolateValidValueAt(0.5, 1., noDiscard) == Catch::Approx(1.));
		CHECK(map.InterpolateValidValueAt(1., 1., noDiscard) == Catch::Approx(1.5));
		// Out-of-box positions are clamped onto the border, never out of the buffer.
		CHECK(map.InterpolateValidValueAt(-100., -100., noDiscard) == Catch::Approx(0.));
		CHECK(map.InterpolateValidValueAt(100., 100., noDiscard) == Catch::Approx(3.));
	}

	// As soon as one of the 4 neighbors is >= the discarded value, the discarded value itself is
	// returned (no partial blending with invalid cells).
	{
		CHECK(map.InterpolateValidValueAt(1., 1., 3.) == Catch::Approx(3.));
		CHECK(map.InterpolateValidValueAt(1., 1., 2.) == Catch::Approx(2.));
		// Even on the center of cell 0 (raw value 0), since cells 2 and 3 are neighbors.
		CHECK(map.InterpolateValidValueAt(0.5, 0.5, 2.) == Catch::Approx(2.));
		// ...but on the center of cell 3 the neighborhood collapses on cell 3 only.
		CHECK(map.InterpolateValidValueAt(1.5, 1.5, 4.) == Catch::Approx(3.));
	}

	// A uniform map interpolates to its unique value everywhere, unless it is itself discarded.
	{
		BasicDouble2DMap uniformMap(MakeMapBox(2., 2.), 4, 4);
		uniformMap.AllocateData(0.75);
		CHECK(uniformMap.InterpolateValidValueAt(0.3, 1.7, 1.) == Catch::Approx(0.75));
		CHECK(uniformMap.InterpolateValidValueAt(0.3, 1.7, 0.75) == Catch::Approx(0.75));
		CHECK(uniformMap.InterpolateValidValueAt(0.3, 1.7, 0.5) == Catch::Approx(0.5));
	}
}

TEST_CASE("TestBasic2DMap_ComputeMean")
{
	// Square map: the 3x3 neighborhood is clamped at the borders.
	{
		BasicDouble2DMap const map(MakeMapBox(3., 3.), 3, 3);
		const std::vector<double> buffer = { 0., 1., 2., 3., 4., 5., 6., 7., 8. };
		REQUIRE(static_cast<int>(buffer.size()) == map.CountCells());

		CHECK(map.ComputeMean(buffer.data(), 1, 1) == Catch::Approx(4.));  // all 9 cells
		CHECK(map.ComputeMean(buffer.data(), 0, 0) == Catch::Approx(2.));  // 0,1,3,4
		CHECK(map.ComputeMean(buffer.data(), 2, 0) == Catch::Approx(3.));  // 1,2,4,5
		CHECK(map.ComputeMean(buffer.data(), 0, 2) == Catch::Approx(5.));  // 3,4,6,7
		CHECK(map.ComputeMean(buffer.data(), 2, 2) == Catch::Approx(6.));  // 4,5,7,8
		CHECK(map.ComputeMean(buffer.data(), 1, 0) == Catch::Approx(2.5)); // 0..5
		CHECK(map.ComputeMean(buffer.data(), 1, 2) == Catch::Approx(5.5)); // 3..8
	}

	// Non-square map: checks the row stride used to walk the buffer.
	{
		BasicDouble2DMap const map(MakeMapBox(4., 2.), 4, 2);
		const std::vector<double> buffer = { 0., 1., 2., 3., 4., 5., 6., 7. };
		REQUIRE(static_cast<int>(buffer.size()) == map.CountCells());

		CHECK(map.ComputeMean(buffer.data(), 0, 0) == Catch::Approx(2.5)); // 0,1,4,5
		CHECK(map.ComputeMean(buffer.data(), 3, 0) == Catch::Approx(4.5)); // 2,3,6,7
		CHECK(map.ComputeMean(buffer.data(), 1, 1) == Catch::Approx(3.));  // 0,1,2,4,5,6
	}

	// Single cell map: the mean is the cell itself.
	{
		BasicDouble2DMap const map(MakeMapBox(1., 1.), 1, 1);
		const double buffer[1] = { 7. };
		CHECK(map.ComputeMean(buffer, 0, 0) == Catch::Approx(7.));
	}

	// A uniform buffer has a constant mean, whatever the position in the map.
	{
		BasicDouble2DMap const map(MakeMapBox(3., 3.), 3, 3);
		const std::vector<double> buffer(9, 1.5);
		for (int y = 0; y < 3; ++y)
		{
			for (int x = 0; x < 3; ++x)
			{
				CHECK(map.ComputeMean(buffer.data(), x, y) == Catch::Approx(1.5));
			}
		}
	}
}

TEST_CASE("TestBasic2DMap_ComputeMeanWithFilter")
{
	BasicDouble2DMap const map(MakeMapBox(3., 3.), 3, 3);
	const NonNegative2DFilter filter;

	// Only the non-negative neighbors contribute, and the divisor is the number of actual
	// contributions (not the neighborhood size).
	{
		const std::vector<double> buffer = { -1., -1., 2., -1., 4., 5., 6., 7., 8. };
		REQUIRE(static_cast<int>(buffer.size()) == map.CountCells());

		CHECK(map.ComputeMeanWithFilter(filter, buffer.data(), 1, 1) == Catch::Approx(32. / 6.));
		CHECK(map.ComputeMeanWithFilter(filter, buffer.data(), 0, 0) == Catch::Approx(4.));  // only 4
		// 4,5,7,8: no value filtered out here, so this matches ComputeMean.
		CHECK(map.ComputeMeanWithFilter(filter, buffer.data(), 2, 2) == Catch::Approx(6.));
		CHECK(map.ComputeMeanWithFilter(filter, buffer.data(), 2, 0) == Catch::Approx(11. / 3.)); // 2,4,5
	}

	// When nothing is filtered out, the result matches ComputeMean.
	{
		const std::vector<double> buffer = { 0., 1., 2., 3., 4., 5., 6., 7., 8. };
		for (int y = 0; y < 3; ++y)
		{
			for (int x = 0; x < 3; ++x)
			{
				CHECK(map.ComputeMeanWithFilter(filter, buffer.data(), x, y)
					== Catch::Approx(map.ComputeMean(buffer.data(), x, y)));
			}
		}
	}

	// When no neighbor contributes, the original value is kept.
	{
		const std::vector<double> buffer(9, -5.);
		CHECK(map.ComputeMeanWithFilter(filter, buffer.data(), 1, 1) == Catch::Approx(-5.));
		CHECK(map.ComputeMeanWithFilter(filter, buffer.data(), 0, 2) == Catch::Approx(-5.));
	}
	{
		// The center is filtered out too: the kept value is the (invalid) original one.
		std::vector<double> buffer(9, -1.);
		buffer[4] = -3.;
		CHECK(map.ComputeMeanWithFilter(filter, buffer.data(), 1, 1) == Catch::Approx(-3.));
	}
}
