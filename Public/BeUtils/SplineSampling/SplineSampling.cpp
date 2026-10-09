/*--------------------------------------------------------------------------------------+
|
|     $Source: SplineSampling.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "SplineSampling.h"

#include "OcclusionMap.h"
#include "SplineHelper.h"
#include "SplinePattern.h"

#include <BeUtils/Misc/Random.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>


namespace BeUtils
{
	namespace
	{
		void RemoveOverlappingPositions(
			std::vector<glm::dvec3>& positions,
			double exclusionX, double exclusionY)
		{
			if (positions.empty())
				return;

			const double cellSize = std::max(exclusionX, exclusionY);

			struct CellKey
			{
				int cx, cy;
				bool operator==(CellKey const& o) const { return cx == o.cx && cy == o.cy; }
			};
			struct CellKeyHash
			{
				size_t operator()(CellKey const& k) const
				{
					return std::hash<int>()(k.cx) ^ (std::hash<int>()(k.cy) << 16);
				}
			};
			std::unordered_map<CellKey, std::vector<size_t>, CellKeyHash> grid;

			auto getCell = [cellSize](double x, double y) -> CellKey
			{
				return { static_cast<int>(std::floor(x / cellSize)),
						 static_cast<int>(std::floor(y / cellSize)) };
			};

			std::vector<glm::dvec3> accepted;
			accepted.reserve(positions.size());

			for (auto const& pos : positions)
			{
				CellKey cell = getCell(pos.x, pos.y);
				bool hasOverlap = false;

				for (int dj = -1; dj <= 1 && !hasOverlap; ++dj)
				{
					for (int di = -1; di <= 1 && !hasOverlap; ++di)
					{
						CellKey neighbor{ cell.cx + di, cell.cy + dj };
						auto it = grid.find(neighbor);
						if (it == grid.end())
							continue;
						for (size_t idx : it->second)
						{
							if (std::abs(accepted[idx].x - pos.x) < exclusionX
								&& std::abs(accepted[idx].y - pos.y) < exclusionY)
							{
								hasOverlap = true;
								break;
							}
						}
					}
				}

				if (!hasOverlap)
				{
					grid[cell].push_back(accepted.size());
					accepted.push_back(pos);
				}
			}

			positions = std::move(accepted);
		}
	}
	Spline2DProjector::~Spline2DProjector()
	{

	}

	std::optional<Spline2DProjector::vec2_type> Spline2DProjector::Project2DOpt(vec3_type const& pos) const
	{
		BE_ISSUE("when overriding MayFail, please override Project2DOpt!");
		return Project2D(pos);
	}

	std::optional<Spline2DProjector::vec3_type> Spline2DProjector::Project3DOpt(vec3_type const& pos) const
	{
		BE_ISSUE("when overriding MayFail, please override Project3DOpt!");
		return Project3D(pos);
	}

	void SampleSplineInterior(SplineCurve const& spline,
		TransformHolder const& transform,
		BoundingBox const& samplingBox_World,
		glm::dvec3 const& averageInstanceDims_World,
		SplineSamplingParameters const& params,
		std::vector<SplineCurve::vector_type>& outPositions)
	{
		BE_ASSERT(params.samplingMode == ESplineSamplingMode::Interior);

		if (!IsInitialized(samplingBox_World))
		{
			BE_ISSUE("invalid sampling box");
			return;
		}
		if (averageInstanceDims_World.x <= 0. || averageInstanceDims_World.y <= 0.)
		{
			BE_ISSUE("invalid mean instance dimension");
			return;
		}
		auto const boxDims = GetBoxDimensions(samplingBox_World);
		const double areaToPopulate = boxDims.x * boxDims.y;
		const double objAvgSurface = averageInstanceDims_World.x * averageInstanceDims_World.y;
		const double objAvgLengthWidthRatio = averageInstanceDims_World.y / averageInstanceDims_World.x;

		outPositions.clear();

		if (areaToPopulate <= 0.)
		{
			// Degenerated (flat) sampling box: nothing to populate.
			return;
		}
		if (params.density <= 0.f)
		{
			// Null density => no instance at all (avoids a division by zero below).
			return;
		}

		double nInstances = ceil(areaToPopulate / objAvgSurface);

		// compute number of instances for current density
		double popDensity = static_cast<double>(params.density);
		popDensity *= 100.0;
		nInstances *= popDensity * popDensity;
		nInstances /= (100.0 * 100.0);

		// A tiny density may lead to a fractional count below 1: as sampling is expected, keep at
		// least one instance (=> a single cell covering the whole box).
		nInstances = std::max(nInstances, 1.0);

		// compute cellsAlongX and cellsAlongY
		// we assume here that the objects are right next to each other
		double cellSizeX = std::sqrt(areaToPopulate / nInstances);
		double cellSizeY = cellSizeX * objAvgLengthWidthRatio;
		if (params.forceAligned)
		{
			if (params.fixedSpacing)
			{
				// fixedSpacing is shared with the 1-D (path) mode, where only 'x' is meaningful: a null
				// or negative 'y' therefore means "same spacing along both axes" (square cells).
				cellSizeX = params.fixedSpacing->x;
				cellSizeY = (params.fixedSpacing->y > 0.) ? params.fixedSpacing->y : cellSizeX;
			}
			else
			{
				cellSizeX = averageInstanceDims_World.x;
				cellSizeY = averageInstanceDims_World.y;
			}
		}
		if (cellSizeX <= 0. || cellSizeY <= 0.)
		{
			BE_ISSUE("invalid cell size (null spacing?)", cellSizeX, cellSizeY);
			return;
		}
		const int cellsAlongX = static_cast<int>(std::ceil(boxDims.x / cellSizeX));
		const int cellsAlongY = static_cast<int>(std::ceil(boxDims.y / cellSizeY));

		OcclusionMap surfaceGrid(samplingBox_World, cellsAlongX, cellsAlongY);

		SplineHelper splineHelper(&spline);
		SplinePattern spline2DEffect(transform, splineHelper);
		spline2DEffect.SetOcclusion(false);
		if (!surfaceGrid.BuildFrom2DPattern(spline2DEffect))
		{
			// Invalid outline (degenerated spline) or cancelled build: nothing to populate.
			return;
		}

		// Positions are guaranteed to lie inside the spline outline: OcclusionMap only tests the
		// candidates generated in cells crossed by the outline (see OcclusionMap::ECellKind).
		surfaceGrid.GetSampledPositions(outPositions, params.forceAligned, params.randSeed, params.gridRotAngle);

		if (params.forbidOverlap && !params.forceAligned)
		{
			RemoveOverlappingPositions(outPositions,
				averageInstanceDims_World.x, averageInstanceDims_World.y);
		}
	}

	void SampleSplinePath(SplineCurve const& spline,
		TransformHolder const& transform,
		SplineSamplingParameters const& params,
		std::vector<SplineCurve::vector_type>& outPositions)
	{
		BE_ASSERT(params.samplingMode == ESplineSamplingMode::AlongPath);

		SplineHelper const splineHelper(&spline);
		Basic2DProjector const defaultProjector(E2DProjection::Z_Axis);
		Spline2DProjector const* pProjector = params.customProjector.get();
		if (!pProjector)
		{
			pProjector = &defaultProjector;
		}

		if (params.randomSpacing)
		{
			// Random-spacing mode: place instances at random distances in
			// [randomSpacing.x, randomSpacing.y] from each other.
			outPositions.clear();

			const double minDist = params.randomSpacing->x;
			const double maxDist = params.randomSpacing->y;
			if (maxDist <= 0.)
			{
				// Null spacing => nothing to place.
				return;
			}
			if (minDist <= 0. || maxDist < minDist)
			{
				// minDist must be strictly positive: it drives the fine sampling resolution below
				// (totalLength / minDist) and the minimum step when walking the curve.
				return;
			}

			auto const evalData = splineHelper.EvalSplineLength(transform, 0.01, *pProjector);
			if (evalData.totalLength <= 0.)
				return;

			// Sample the spline.
			using vector_type = SplineCurve::vector_type;
			using value_type = SplineCurve::value_type;
			const value_type u_start = evalData.u_start;
			const value_type u_end = evalData.u_end;
			const value_type u_range = u_end - u_start;
			if (u_range <= 0.)
				return;

			// Fine sampling resolution: ~10 samples per minimum step, bounded to avoid absurd
			// allocations when minDist is tiny compared to the curve length.
			constexpr size_t kMaxFineSamples = 1u << 20;
			const double fineSamplesWanted = std::ceil(evalData.totalLength / minDist) * 10.;
			const size_t numFineSamples = std::clamp<size_t>(
				(fineSamplesWanted < double(kMaxFineSamples)) ? static_cast<size_t>(fineSamplesWanted) : kMaxFineSamples,
				100, kMaxFineSamples);
			std::vector<double> cumulDist(numFineSamples, 0.0);
			std::vector<vector_type> fineSamples(numFineSamples);
			const value_type dU = u_range / value_type(numFineSamples - 1);

			fineSamples[0] = pProjector->Project3D(splineHelper.GetPosition_world(u_start, transform));
			value_type u = u_start + dU;
			for (size_t i = 1; i < numFineSamples; ++i, u += dU)
			{
				fineSamples[i] = pProjector->Project3D(splineHelper.GetPosition_world(u, transform));
				cumulDist[i] = cumulDist[i - 1] + glm::distance(fineSamples[i], fineSamples[i - 1]);
			}

			// Walk the cumulated distance table with random steps.
			RandomNumberGenerator rand(params.randSeed);
			const double totalLen = cumulDist.back();
			const double distRange = maxDist - minDist;

			outPositions.push_back(fineSamples[0]);
			double nextTargetDist = minDist + distRange * rand.RandDouble();

			auto itDist = cumulDist.begin();
			while (nextTargetDist <= totalLen)
			{
				auto itLower = std::lower_bound(itDist, cumulDist.end(), nextTargetDist);
				if (itLower == cumulDist.end())
					break;
				size_t idx = static_cast<size_t>(std::distance(cumulDist.begin(), itLower));
				outPositions.push_back(fineSamples[idx]);
				itDist = itLower;
				nextTargetDist += minDist + distRange * rand.RandDouble();
			}
		}
		else
		{
			// Fixed-spacing or fixed-count mode.
			SplineHelper::EPathRegularSamplingMode const mode = params.fixedSpacing
				? SplineHelper::EPathRegularSamplingMode::FixedSpacing
				: SplineHelper::EPathRegularSamplingMode::FixedNbSamples;
			std::variant<size_t, double> fixedCountOrDistance;
			if (params.fixedSpacing)
			{
				// See SplineSamplingParameters::fixedSpacing contract: 'x' must be > 0.
				if (!(params.fixedSpacing->x > 0.) || !std::isfinite(params.fixedSpacing->x))
				{
					outPositions.clear();
					return;
				}
				fixedCountOrDistance = params.fixedSpacing->x;
			}
			else if (params.fixedNbInstances)
			{
				fixedCountOrDistance = *params.fixedNbInstances;
			}
			else
			{
				BE_ISSUE("invalid sampling parameters");
				outPositions.clear();
				return;
			}
			splineHelper.GetRegularSamples(outPositions, mode, fixedCountOrDistance,
				transform,
				*pProjector);
		}
	}

	void SampleSpline(SplineCurve const& spline,
		TransformHolder const& transform,
		BoundingBox const& samplingBox_World,
		glm::dvec3 const& averageInstanceDims_World,
		SplineSamplingParameters const& params,
		std::vector<SplineCurve::vector_type>& outPositions)
	{
		if (params.samplingMode == ESplineSamplingMode::Interior)
		{
			SampleSplineInterior(spline, transform, samplingBox_World, averageInstanceDims_World, params, outPositions);
		}
		else
		{
			SampleSplinePath(spline, transform, params, outPositions);
		}
	}
}
