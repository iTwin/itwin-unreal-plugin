/*--------------------------------------------------------------------------------------+
|
|     $Source: OcclusionMap.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "Basic2DMap.h"
#include "SplineDefines.h"


namespace BeUtils
{
	class Population2DPattern;

	class OcclusionMap : public BasicDouble2DMap
	{
		//------------------------------------------------------------------------
		// 2D Occlusion Map.
		//	based on a regular subdivision grid of the area to occlude.
		//	a value of 1 means no occlusion, whereas 0 means a total occlusion.
		//------------------------------------------------------------------------

		using Base = BasicDouble2DMap;
		using Base::pData_;
		using Base::nCells_;
		using Base::nWidth_;
		using Base::nHeight_;

	public:
		OcclusionMap();

		OcclusionMap(BoundingBox const& Box,
			int nCellsAlongX, int nCellsAlongY,
			int superSamplingFactor = 1, float fDistribQuality = 1.f, int nCustomCells = -1);

		virtual ~OcclusionMap();


		bool IsConstant() const;

		bool BuildFrom2DPattern(Population2DPattern const& p2DPath);

		size_t GetSampledPositions(std::vector<glm::dvec3>& outPositions,
			bool forceAligned, uint32_t randSeed) const;

#if IS_EON_DEV()
		//! Creates an image from the current map. (Debug purpose)
		void DumpToImage() const override;
#endif

	};


	// Intersection functions
	struct IntersectionPt2D
	{
		glm::dvec2 ptInter_ = glm::dvec2(0., 0.);
		glm::dvec2 normal_ = glm::dvec2(1., 0.);
	};

	typedef std::vector<IntersectionPt2D> Intersection2DVector;
	typedef Intersection2DVector::const_iterator Intersection2DIterator;

	struct Intersection2DSorter
	{
		Intersection2DSorter();

		void FindAndSort2DIntersectionsMatchingY(
			std::vector<Segment_2D> const& segments,
			double y);

		Intersection2DVector intersections_;
	};

}
