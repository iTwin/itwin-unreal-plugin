/*--------------------------------------------------------------------------------------+
|
|     $Source: SplineHelper.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "SplineHelper.h"

#include "SplineUtils.h"

namespace BeUtils
{
	//-----------------------------------
	// class CachedSegmentsContainer
	//-----------------------------------

	bool CachedSegmentsContainer::RecordSegments(std::vector<Segment_2D> const& segments,
		BoundingBox const& bbox, E2DProjection projection, double dU, double dUTolerance /*=1e-8*/)
	{
		// Insert the given set of segments for future use.
		// inserts the new data sorted by resolution.
		CacheArray& vectorToFill(perProjectionData_[(int)projection]);

		CachedSegments recData;
		recData.segments_ = segments;
		recData.bbox_ = bbox;
		recData.dU_ = dU;

		// we could optimize a little by using a dichotomy here...
		size_t nSize = vectorToFill.size();

		bool bInsert = false;
		bool bDuplicate = false;
		size_t whereToInsert = 0;
		for (size_t i = 0; i < nSize && !bInsert; i++)
		{
			if (vectorToFill[i].dU_ >= dU)
			{
				bDuplicate = fabs(dU - vectorToFill[i].dU_) < dUTolerance;
				whereToInsert = i;
				bInsert = true;
			}
		}

		if (bInsert)
		{
			if (bDuplicate)
			{
				BE_ISSUE("This resolution already exists in cache");
				return false;
			}

			vectorToFill.insert(vectorToFill.begin() + whereToInsert, recData);
			return true;
		}
		else
		{
			// append new data at end.
			vectorToFill.push_back(recData);
			return true;
		}
	}

	//! Retrieves the given set of segments at given resolution and projection, if it exists.
	bool CachedSegmentsContainer::RetrieveSegments(std::vector<Segment_2D>& segments,
		BoundingBox& bbox, E2DProjection projection, double dU, double dUTolerance /*= 1e-8*/) const
	{
		auto it = perProjectionData_[(size_t)projection].begin();
		auto end = perProjectionData_[(size_t)projection].end();

		for (; it != end; it++)
		{
			if (fabs(dU - it->dU_) < dUTolerance)
			{
				segments = it->segments_;
				bbox = it->bbox_;
				return true;
			}

			if (it->dU_ > dU + dUTolerance) // optimization possible because each array is sorted by resolution (dU)
				return false;
		}
		return false;
	}

	void CachedSegmentsContainer::Clear()
	{
		// clear all cached sets (and free memory at once)
		for (int c = 0; c < 3; c++)
		{
			CacheArray().swap(perProjectionData_[c]);
		}
	}

	//-----------------------------------
	// class SplineHelper
	//-----------------------------------

	SplineHelper::SplineHelper(SplineCurve const* splineCurve)
		: pCurve_(splineCurve)
	{
	}

	SplineHelper::~SplineHelper()
	{
	}

	SplineHelper::SplineHelper(SplineHelper const& other)
		: pCurve_(other.pCurve_)
	{
	}

	SplineHelper& SplineHelper::operator= (const SplineHelper& rhs)
	{
		pCurve_ = rhs.pCurve_;
		return *this;
	}

	void SplineHelper::SetSplineCurve(SplineCurve const* splineCurve)
	{
		if (pCurve_ != splineCurve)
		{
			InvalidateCache();
		}
		pCurve_ = splineCurve;
	}

	SplineHelper::vector_type SplineHelper::GetPosition(value_type const& u) const
	{
		return pCurve_->GetPositionAtCoord(u);
	}

	SplineHelper::vector_type SplineHelper::GetPosition_world(
		value_type const& u, TransformHolder const& transform) const
	{
		return (transform.transfrom_ * GetPosition(u)) + transform.pos_;
	}

	SplineHelper::vector_type SplineHelper::GetTangentAtCoord(value_type const& u) const
	{
		return pCurve_->GetTangentAtCoord(u);
	}

	//void SplineHelper::GetWorldTBNAtCoord(value_type const& u,
	//	vector_type& tangent, vector_type& binormal, vector_type& normal,
	//	TransformHolder const& toWorld) const
	//{
	//	pCurve_->GetLocalTBN(u, tangent, binormal, normal, false);

	//	toWorld.invTransfrom_.MultTranspose(&tangent);
	//	toWorld.invTransfrom_.MultTranspose(&binormal);
	//	toWorld.invTransfrom_.MultTranspose(&normal);
	//}

	size_t SplineHelper::GetRegularSamples(
		std::vector<vector_type>& samples,
		EPathRegularSamplingMode samplingMode,
		std::variant<size_t, double> const& fixedCountOrDistance,
		TransformHolder const& transform,
		E2DProjection projection /*= E2DProjection::None*/) const
	{
		samples.clear();

		double maxVelocity(0.0);
		double const evalLen = EvalSplineLength(transform, maxVelocity,	0.01 /*dU*/, projection);
		if (evalLen <= 0.)
		{
			BE_ISSUE("degenerated spline");
			return 0;
		}

		Basic2DProjector const proj(projection);

		// distance between 2 consecutive samples
		double deltaLength = 1.0;

		size_t targetNbSamples = 1;
		if (samplingMode == EPathRegularSamplingMode::FixedSpacing)
		{
			const double* pFixedSpacing = std::get_if<double>(&fixedCountOrDistance);
			if (!pFixedSpacing || *pFixedSpacing <= 0.)
			{
				BE_ISSUE("missing or wrong spacing value for FixedSpacing mode");
				return 0;
			}
			deltaLength = *pFixedSpacing;
			targetNbSamples = 1 + (size_t)std::floor(evalLen / deltaLength);
		}
		else
		{
			const size_t* pFixedNbSamples = std::get_if<size_t>(&fixedCountOrDistance);
			if (!pFixedNbSamples)
			{
				BE_ISSUE("missing samples count for FixedNbSamples mode");
				return 0;
			}
			if (*pFixedNbSamples >= 2)
			{
				deltaLength = evalLen / (*pFixedNbSamples - 1);
			}
			else
			{
				deltaLength = evalLen;
			}
			targetNbSamples = *pFixedNbSamples;
		}
		samples.reserve(targetNbSamples);

		// first sample the curve
		const size_t numberOfSamples = std::max<size_t>(100, targetNbSamples * 10);
		std::vector<value_type> samplesCumulatedDist(numberOfSamples, 0.0);
		std::vector<vector_type> projSamples(numberOfSamples, vector_type(0.0));
		vector_type prevPosition = proj.Project(GetPosition_world(0.0, transform));
		projSamples[0] = prevPosition;
		value_type const dU = value_type(1.0) / value_type(numberOfSamples - 1);
		value_type u = dU;
		for (size_t i = 1; i < numberOfSamples; ++i, u += dU)
		{
			vector_type const curPosition = proj.Project(GetPosition_world(u, transform));
			projSamples[i] = curPosition;
			samplesCumulatedDist[i] = samplesCumulatedDist[i - 1] + glm::distance(curPosition, prevPosition);
			prevPosition = curPosition;
		}
		// Divide the length in equally-sized pieces

		if (samplingMode == EPathRegularSamplingMode::FixedNbSamples && targetNbSamples > 1)
		{
			// Adjust deltaLength
			deltaLength = samplesCumulatedDist.back() / value_type(targetNbSamples - 1);
		}

		// Always add the 1st sample
		samples.push_back(projSamples[0]);

		size_t iStart = 1, iLast = targetNbSamples;
		value_type distOffset = 0.;
		auto const distBegin = samplesCumulatedDist.begin();
		auto const distEnd = samplesCumulatedDist.end();
		auto itDist = distBegin;
		for (size_t iCoord = iStart; iCoord <= iLast; ++iCoord)
		{
			value_type const valueToReach = deltaLength * iCoord + distOffset;
			// Look for the smallest sample which length is bigger than the requested length
			auto itLower = std::lower_bound(itDist, distEnd, valueToReach);
			if (itLower != distEnd)
			{
				samples.push_back(projSamples[std::distance(distBegin, itLower)]);
				itDist = itLower;
			}
		}
		BE_ASSERT(samplingMode == EPathRegularSamplingMode::FixedSpacing
			|| samples.size() == targetNbSamples);
		return samples.size();
	}

	//	InvalidateCache();

	size_t SplineHelper::CountControlPoints() const
	{
		return pCurve_->PointCount(false);
	}

	SplineHelper::vector_type SplineHelper::GetControlPointPosition(size_t index) const
	{
		return pCurve_->GetPositionAtIndex(index);
	}

	SplineHelper::vector_type SplineHelper::GetControlPointPosition_world(size_t index, TransformHolder const& transform) const
	{
		return (transform.transfrom_ * GetControlPointPosition(index)) + transform.pos_;
	}


	//=========================================================================
	//	SplineSampler
	//=========================================================================
	size_t SplineSampler::Sample(
		SplineHelper const& splineHelper,
		TransformHolder const& splineObj,
		value_type dU,
		value_type dCurveLen)
	{
		BE_ASSERT(dU < 1 && dU > 0);
		pts_.clear();

		size_t numPts = (size_t)floor(1.0 / dU) + 1;
		pts_.reserve(numPts);

		value_type dMinSegmentLen = dCurveLen * dU;
		value_type dMinSegmentLen2 = dMinSegmentLen * dMinSegmentLen;

		value_type dTol2 = dMinSegmentLen2 * 0.0025; // +/- 5%
		value_type dMinUStep = 0.1 * dU;

		vector_type pos_prev = splineHelper.GetPosition_world(0.0, splineObj);

		double u_prev = 0.0;
		while (u_prev < 1)
		{
			bool validPoint(false);
			double u_min = u_prev;
			double u_max = std::min(u_prev + dU, 1.0);

			vector_type pos_next_umax = splineHelper.GetPosition_world(u_max, splineObj);
			double dist2 = glm::length2(pos_next_umax - pos_prev);
			if (dist2 < dMinSegmentLen2 || u_max >= 1.0)
			{
				pts_.push_back(SplinePointInfo(u_max, pos_next_umax));
				u_prev = u_max;
			}
			else
			{
				do {
					// dichotomy search.
					double u_middle = (u_min + u_max) * 0.5;

					vector_type pos_next = splineHelper.GetPosition_world(u_middle, splineObj);
					dist2 = glm::length2(pos_next - pos_prev);
					if (fabs(dist2 - dMinSegmentLen2) < dTol2 || (u_max - u_min > dMinUStep))
					{
						validPoint = true;
					}
					else if (dist2 > dTol2)
					{
						// decrease abscissa
						u_max = u_middle;
					}
					else
					{
						// increase abscissa
						u_min = u_middle;
					}

					if (validPoint)
					{
						pts_.push_back(SplinePointInfo(u_middle, pos_next));
						u_prev = u_middle;
					}

				} while (!validPoint);
			}
		}
		return pts_.size();
	}

	void SplineSampler::Compute2dNormals(
		Basic2DProjector const& projector,
		double dU_delta,
		SplineHelper const& splineHelper,
		TransformHolder const& transform)
	{
		int lastPtIndex = static_cast<int>(pts_.size()) - 1;
		for (int index(0); index < lastPtIndex; index++)
		{
			double u = pts_[index].uCoord_;
			auto const& curPos_3d = pts_[index].vWorldPos_;
			vector_type nextPos_3d = splineHelper.GetPosition_world(std::min(u + dU_delta, 1.0), transform);
			pts_[index].normal2d_ = Get2dNormal(projector(curPos_3d), projector(nextPos_3d));
		}

		// last normal computed differently
		if (lastPtIndex > 0)
		{
			auto const& lastPos_3d = pts_[lastPtIndex].vWorldPos_;
			vector_type prevPos_3d = splineHelper.GetPosition_world(std::min(1.0 - dU_delta, 1.0), transform);
			pts_[lastPtIndex].normal2d_ = Get2dNormal(projector(prevPos_3d), projector(lastPos_3d));
		}
	}


	size_t SplineHelper::ComputeSegments(
		std::vector<Segment_2D>& segments,
		value_type const& dU,
		value_type const& dCurveLen,
		BoundingBox& bbox,
		TransformHolder const& transform,
		E2DProjection projection /*= Z_Axis*/) const
	{
		BE_ASSERT(dU > 0 && dU < 1.0);

		if (cache_.RetrieveSegments(segments, bbox, projection, dU))
		{
			return segments.size();
		}

		segments.clear();

		// project (3D) positions to 2D
		Basic2DProjector projector(projection);

		size_t numControlPoints = CountControlPoints();

		if (numControlPoints >= 2)
		{
			vec2_type curPos, prevPos, pos2d_0;

			vector_type curPos_3d_0;
			curPos_3d_0 = GetPosition_world(0.0, transform);
			ExtendBox(bbox, curPos_3d_0);
			pos2d_0 = projector(curPos_3d_0);
			prevPos = pos2d_0;
			double prevU(0.0), curU(0.0);

			SplineSampler sampler;
			size_t numPts = sampler.Sample(*this, transform, dU, dCurveLen);

			segments.reserve(numPts);

			for (size_t curIndexPt = 1; curIndexPt < numPts; curIndexPt++)
			{
				auto const& curPos_3d = sampler.pts_[curIndexPt].vWorldPos_;
				ExtendBox(bbox, curPos_3d);
				curPos = projector(curPos_3d);
				curU = sampler.pts_[curIndexPt].uCoord_;

				segments.push_back(Segment_2D(
					prevPos,
					curPos,
					0.5 * (prevU + curU)));

				prevPos = curPos;
				prevU = curU;
			}

			// in Fill mode, we always need a closed curve, even though the spline is not:
			if (!pCurve_->IsCyclic() && numPts > 1)
			{
				segments.push_back(Segment_2D(prevPos, pos2d_0, 0.0));
			}

			// Record result for future use.
			cache_.RecordSegments(segments, bbox, projection, dU);
		}

		return segments.size();
	}

	void SplineHelper::InvalidateCache()
	{
		cache_.Clear();
	}

	SplineHelper::value_type SplineHelper::GetTotalDeltaU() const
	{
		// bezier class is based on range [0..1] (see spline/engine/Bezier.hpp)
		return 1.0; //GetEndTime() - GetStartTime();
	}

	SplineHelper::value_type SplineHelper::GetControlPointsPathLength(TransformHolder const& transform, E2DProjection projection /*= None*/) const
	{
		// Returns an approximation of the total length of the spline, based
		// on the control points only.
		Basic2DProjector projector(projection);

		double dTotalLen(0.0);

		size_t numControlPoints = CountControlPoints();
		if (numControlPoints >= 2)
		{
			vector_type vPrev = projector.Project(GetControlPointPosition_world(0, transform));
			for (size_t i = 1; i < numControlPoints; i++)
			{
				vector_type vPos = projector.Project(GetControlPointPosition_world(i, transform));
				dTotalLen += glm::length(vPos - vPrev);
				vPrev = vPos;
			}
		}

		return dTotalLen;
	}

	SplineHelper::value_type SplineHelper::EvalSplineLength(TransformHolder const& transform, double& dMaxVelocity,
		value_type const& dU, E2DProjection projection /*= None*/) const
	{
		// Returns an approximation of the total length of the spline
		// projected along given axis, with dS as time increment.
		Basic2DProjector projector(projection);

		double dTotalLen(0.0);
		dMaxVelocity = 0.0;

		size_t numControlPoints = CountControlPoints();

		if (numControlPoints >= 2)
		{
			double u = 0.0; //GetStartTime();
			double u_end = 1.0; //GetEndTime();

			vector_type vPrev = projector.Project(GetPosition_world(u, transform));
			u += dU;

			for (; u <= u_end; u += dU)
			{
				vector_type vPos = projector.Project(GetPosition_world(u, transform));

				double dSegmentLen = glm::length(vPos - vPrev);
				double localVelocity = dSegmentLen / dU;
				if (localVelocity > dMaxVelocity)
					dMaxVelocity = localVelocity;
				dTotalLen += dSegmentLen;

				vPrev = vPos;
			}
		}

		return dTotalLen;
	}


	SplineHelper::value_type SplineHelper::GetMeanVelocity(TransformHolder const& transform, E2DProjection projection /*= None*/) const
	{
		//Average speed along the spline projected along given axis, if the curvilinear abscissa is understood as a time) - based on the control points only.
		double dTotalTime = 1.0; // Bezier range is [0..1]
		return (GetControlPointsPathLength(transform, projection) / dTotalTime);
	}

	SplineHelper::value_type SplineHelper::EvalMaxVelocity(TransformHolder const& transform,
		value_type& splineLenEvaluation, E2DProjection projection /*= None*/) const
	{
		value_type dMaxVelocity(0.0);
		size_t numControlPoints = CountControlPoints();
		value_type dU = 1.0 / (3 * numControlPoints + 1);
		splineLenEvaluation = EvalSplineLength(transform, dMaxVelocity, dU, projection);
		return dMaxVelocity;
	}

	SplineHelper::value_type SplineHelper::EvalMeanVelocity(
		TransformHolder const& transform,
		double& dMaxVelocity,
		value_type const& dU,
		E2DProjection projection/*= None*/) const
	{
		// Average speed along the spline projected along given axis, if the curvilinear abscissa is understood as a time).
		value_type dTotalTime = 1.0; // Bezier range is [0..1]
		dMaxVelocity = 0.0;
		return (EvalSplineLength(transform, dMaxVelocity, dU, projection) / dTotalTime);
	}

}
