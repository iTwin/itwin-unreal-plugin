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

#include <cstdint>
#include <vector>


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
		/// Classification of a cell with respect to the 2D outline used to build the map.
		enum class ECellKind : uint8_t
		{
			Outside,	//!< cell center is outside the outline, and no outline segment crosses the cell
			Interior,	//!< cell center is inside the outline, and no outline segment crosses the cell
						//!< => every point of the cell is guaranteed to be inside.
			Boundary	//!< at least one outline segment crosses the cell => points must be tested.
		};

		OcclusionMap();

		OcclusionMap(BoundingBox const& Box,
			int nCellsAlongX, int nCellsAlongY,
			int superSamplingFactor = 1, float fDistribQuality = 1.f, int nCustomCells = -1);

		virtual ~OcclusionMap();


		bool IsConstant() const;

		/// Rasterizes the given 2D pattern (closed outline) into the map.
		/// Returns false if the build was cancelled, if the pattern type is not supported (only
		/// Enclosure is), or if the outline is invalid/degenerated (less than 3 segments): in those
		/// cases the whole map is set as occluded (all cells at 0), so that #GetSampledPositions
		/// yields no position.
		bool BuildFrom2DPattern(Population2DPattern const& p2DPath);

		/// Returns sampled positions. When the map was built from a closed outline (see
		/// #BuildFrom2DPattern), positions are guaranteed to lie inside the outline: only candidates
		/// generated in #ECellKind::Boundary cells are tested against the outline, positions generated
		/// in #ECellKind::Interior cells are inside by construction.
		size_t GetSampledPositions(std::vector<glm::dvec3>& outPositions,
			bool forceAligned, uint32_t randSeed, float angle) const;

		/// Returns the classification of the given cell (only meaningful after #BuildFrom2DPattern).
		ECellKind GetCellKind(int cellIndex) const;

		/// Exact point-in-outline test against the 2D segments baked by #BuildFrom2DPattern.
		/// Returns true if no outline is available.
		bool IsInsideOutline(double x, double y) const;

		std::vector<Segment_2D> const& GetOutlineSegments() const { return segments_; }

#if IS_EON_DEV()
		//! Creates an image from the current map. (Debug purpose)
		void DumpToImage() const override;
#endif

	private:
		/// Marks as #ECellKind::Boundary every cell crossed by an outline segment, and classifies the
		/// remaining cells as Interior/Outside from #pData_.
		void ClassifyCells(double dOcclusionValue_outside);

		std::vector<uint8_t> cellKinds_;		//!< one #ECellKind per cell (empty if no outline)
		std::vector<Segment_2D> segments_;		//!< baked 2D outline (sorted by min Y)
		BoundingBox outlineBox_;				//!< 2D bounding box of the outline
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
