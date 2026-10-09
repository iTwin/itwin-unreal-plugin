/*--------------------------------------------------------------------------------------+
|
|     $Source: OcclusionMap.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "OcclusionMap.h"

#include "Poisson2D.h"
#include "Spline2DProjector.h"
#include "SplinePattern.h"

#include <BeUtils/Misc/Random.h>

#include <algorithm>
#include <cmath>
// Just a wrapper for async++.h that disables some warnings
#include <CesiumAsync/Impl/cesium-async++.h>


namespace BeUtils
{

	//---------------------------------------
	// class OcclusionMap
	//---------------------------------------

	OcclusionMap::OcclusionMap()
	{}

	OcclusionMap::OcclusionMap(BoundingBox const& Box, int nCellsAlongX, int nCellsAlongY,
		int superSamplingFactor /*= 1*/, float fDistribQuality /*1.f*/, int nCells /*= -1*/)
		: Base(Box, nCellsAlongX, nCellsAlongY, superSamplingFactor, fDistribQuality, nCells)
	{
	}

	OcclusionMap::~OcclusionMap()
	{
	}

	bool OcclusionMap::IsConstant() const
	{
		if (pData_.empty())
		{
			// an empty map can be considered as constant.
			return true;
		}
		else
		{
			// check that all values are the same.
			double dRefVal(pData_[0]);
			for (size_t i(1); i < pData_.size(); ++i)
			{
				if (std::fabs(pData_[i] - dRefVal) > 1e-4)
					return false;
			}
			return true;
		}
	}

#if IS_EON_DEV()
	void OcclusionMap::DumpToImage() const
	{
		if (!strDumpPath_.empty())
		{
			VUEImage img(nWidth_, nHeight_, IT_GrayScale8);
			img.SetFilePath(strDumpPath_);
			for (int i = 0; i < nWidth_; i++)
				for (int j = 0; j < nHeight_; j++)
					img.SetPixelGrayscaleNoGammaConversion(i, j, (float)pData_[i + j * nWidth_]);

			eon::modul2::WriteImage(img, strDumpPath_).leak();
		}
	}
#endif //DEV


	namespace
	{
		bool Compare2DSegments_Y(const Segment_2D& seg1, const Segment_2D& seg2)
		{
			return std::min(seg1.posStart_.y, seg1.posEnd_.y) < std::min(seg2.posStart_.y, seg2.posEnd_.y);
		}

		bool ComparePt2D_X(const IntersectionPt2D& p1, const IntersectionPt2D& p2)
		{
			return p1.ptInter_.x < p2.ptInter_.x;
		}

		size_t FindAll2DIntersectionsMatchingY(
			Intersection2DVector& intersections,
			std::vector<Segment_2D> const& segments,
			double y)
		{
			intersections.clear();

			for (Segment_2D const& seg : segments)
			{
				double fMaxY = std::max(seg.posStart_.y, seg.posEnd_.y);
				if (fMaxY < y)
					continue;
				double fMinY = std::min(seg.posStart_.y, seg.posEnd_.y);
				if (fMinY > y)
					break; // stop the visit (as the segments are sorted)

				// we have an intersection here. Let's compute it.
				IntersectionPt2D inter;
				inter.ptInter_.y = y;

				double dY_seg = seg.posEnd_.y - seg.posStart_.y;
				double dX_seg = seg.posEnd_.x - seg.posStart_.x;

				inter.normal_.x = -dY_seg;
				inter.normal_.y = dX_seg;
				// note: no need to normalize this normal: we just need to know the direction...

				if (dY_seg == 0)
				{
					// we have an horizontal segment. Add 2 intersections.
					inter.ptInter_.x = seg.posStart_.x;
					intersections.push_back(inter);

					inter.ptInter_.x = seg.posEnd_.x;
					intersections.push_back(inter);
				}
				else
				{
					inter.ptInter_.x = seg.posStart_.x + (y - seg.posStart_.y) * dX_seg / dY_seg;
					intersections.push_back(inter);
				}
			}
			return intersections.size();
		}
	} // unnamed namespace

	Intersection2DSorter::Intersection2DSorter()
	{
		intersections_.reserve(10);
	}

	void Intersection2DSorter::FindAndSort2DIntersectionsMatchingY(
		std::vector<Segment_2D> const& segments,
		double y)
	{
		// find all segments intersected by line (Y=y)
		FindAll2DIntersectionsMatchingY(intersections_, segments, y);
		std::sort(intersections_.begin(), intersections_.end(), ComparePt2D_X);
	}


	namespace
	{

		class VUEProgress
		{

		};

		class ProgressHelper
		{
			//-------------------------------------------------------------------------------
			// Helper used to encapsulate progress bar and interruption management
			// Does nothing for now (code NOT extracted from vue.git).
			//-------------------------------------------------------------------------------
		public:
			ProgressHelper(int /*nCells*/,
						   VUEProgress* pProgress = nullptr,
						   int /*nWorkingThreads*/ = 1,
						   bool /*bHandleUserEvent*/ = true,
						   int /*customInterruptorSlot*/ = -1)
				: pProgress_(pProgress)
			{

			}

			~ProgressHelper() {}

			bool Continue() { return true; }

		private:
			VUEProgress* pProgress_ = nullptr;
			//bool bContinue_ = true;
			//float incr_ = 0.f, curPos_ = 0.f, lastPosDisplayed_ = -1.f, maxPos_ = 1.f;
			//int nLoop_ = 0;
			//bool bHandleUserEvent_;
			//const eon::engine::Interruptor* pCustomInterruptor_ = nullptr;
		};


		struct SplineOcclMapData
		{
			std::vector<double>& pData_;
			OcclusionMap const& occlusionMap_;
			double dOcclusionValue_inside_;
			double dOcclusionValue_outside_;

			ProgressHelper& progHelper_;
			std::atomic_bool bCancelledPopulating_ = false;

			SplineOcclMapData(
				Population2DPattern const& p2DPath,
				std::vector<double>& pData,
				OcclusionMap const& occ,
				ProgressHelper& progHelper)
				: pData_(pData)
				, occlusionMap_(occ)
				, progHelper_(progHelper)
			{
				const double dInfluence = std::clamp(p2DPath.GetOcclusionInfluence(), 0., 1.);
				dOcclusionValue_inside_ = p2DPath.IsOcclusion() ? (1. - dInfluence) : dInfluence;
				dOcclusionValue_outside_ = p2DPath.IsOcclusion() ? 1. : 0.;
			}

			bool CancelledPopulating() const { return bCancelledPopulating_; }
		};

		class SplineOcclMapLoopIter :
			// public tools::multiproc::ParallelLoopIteration,
			public SplineOcclMapData
		{
			std::vector<Segment_2D> const& segments_;
			//std::vector<Intersection2DSorter> localIntersectionsVec_;

		public:
			SplineOcclMapLoopIter(
				Population2DPattern const& p2DPath,
				std::vector<double>& pData,
				OcclusionMap const& occ,
				ProgressHelper& progHelper,
				std::vector<Segment_2D> const& segments)
				: SplineOcclMapData(p2DPath, pData, occ, progHelper)
				, segments_(segments)
			{
				//masterThreadName_ = eon::ThreadNameInfo("ECO-SplinePop loop");
			}

			//void WillBecomeParallel(const int nbWorkingThreads) override
			//{
			//	localIntersectionsVec_.resize(nbWorkingThreads);
			//}
			//
			//void RunOnce(const int64_t j, tools::multiproc::ParallelContext const* ctxt) override
			//{

			void RunSubTask(const int j)
			{
				//if (bCancelledPopulating_)
				//	return;

				//const int threadId = ctxt->GetMyWorkingThreadIndex();
				//Intersection2DSorter & localIntersections = localIntersectionsVec_[threadId];

				thread_local static Intersection2DSorter localIntersections;
				const double y = occlusionMap_.GetStart2DPosY() + j * occlusionMap_.GetCellHeight();
				// find all segments intersected by line (Y=y)
				localIntersections.FindAndSort2DIntersectionsMatchingY(segments_, y);
				// we must know whether the starting point is inside the enclosure.
				// Note that we can revert the behavior using the SetOcclusion function
				bool bInside = false;
				Intersection2DIterator itInter = localIntersections.intersections_.begin(),
					endInter = localIntersections.intersections_.end();
				double x = occlusionMap_.GetStart2DPosX();
				int nCellIndex = occlusionMap_.GetWidth() * static_cast<int>(j);
				// const bool isMasterThread = ctxt->AmITheMasterThread();
				for (int i = 0; i < occlusionMap_.GetWidth(); ++i)
				{
					while (itInter != endInter && itInter->ptInter_.x <= x)
					{
						bInside = !bInside;
						itInter++;
					}
					pData_[nCellIndex] = bInside ? dOcclusionValue_inside_ : dOcclusionValue_outside_;
					x += occlusionMap_.GetCellWidth();
					nCellIndex++;
					//if (isMasterThread && !progHelper_.Continue())
					//{
					//	bCancelledPopulating_ = true;
					//	break;
					//}
					//if (bCancelledPopulating_)
					//{
					//	break;
					//}
				}
			}
		};

	}

	bool OcclusionMap::BuildFrom2DPattern(Population2DPattern const& p2DPath)
	{
		// initialize arrays
		pData_.clear();
		pData_.resize(nCells_, 1.0);

		BE_ASSERT(nHeight_ * nWidth_ <= nCells_, "map size too short for given cell subdivision");

		Basic2DProjector const projector(p2DPath.GetProjection());
		// setup sampling resolution
		// no need to sample the spline at a too high resolution (the higher
		// resolution should be that of the occlusion map)
		double dS = 1. / 16;
		double splineLen(0.0);
		double velocity = p2DPath.GetMaxVelocity(splineLen, projector);
		if (velocity > 0)
		{
			double dMap2DResolution = std::max(dCellWidth_, dCellHeight_);
			double dS_default = (2.0 * dMap2DResolution) / velocity;
			double dS_best = (0.5 * dMap2DResolution) / velocity;

			dS = dS_default;
			if (p2DPath.GetSamplingQuality() > 0)
			{
				dS /= p2DPath.GetSamplingQuality();
			}
			dS = std::max(dS, dS_best);
		}

		ProgressHelper progHelper(
			nCells_
			//, &progr,
			//vue::CountStandardRenderSlots(),
			//bHandleUserEvent,
			//customInterruptorSlot
		);

		std::vector<Segment_2D>& segments = segments_;
		segments.clear();
		cellKinds_.clear();
		outlineBox_ = BoundingBox();
		// Note: the bbox filled by Bake2DSegments is the *3D world* box of the sampled curve, not the
		// box of the projected 2D segments => we compute the 2D one ourselves below.
		BoundingBox bbox;
		bool const use2DSegments = (p2DPath.GetType() == Population2DPattern::EPatternType::Enclosure);

		if (use2DSegments)
		{
			// generate 2D segments (by projecting the pattern onto the selected
			// plane).

			p2DPath.Bake2DSegments(
				segments,
				dS,
				splineLen,
				bbox,
				projector);
			std::sort(segments.begin(), segments.end(), Compare2DSegments_Y);

			if (segments.size() < 3)
			{
				// Invalid/degenerated outline (empty spline?): nothing can be enclosed, so mark the
				// whole map as occluded. Leaving the initial 1.0 values would make callers populate
				// the entire box as if it were inside the (missing) outline.
				segments.clear();
				std::fill(pData_.begin(), pData_.end(), 0.0);
				return false;
			}

			// generate map from segments through a scan-line algorithm
			SplineOcclMapLoopIter iter(
				p2DPath,
				pData_,
				*this,
				progHelper,
				segments);

			async::parallel_for(async::irange(0LL, (int64_t)nHeight_), [&iter](auto y) {
				iter.RunSubTask((int)y);
			});

			if (iter.CancelledPopulating())
			{
				return false;
			}

			// Classify cells so that GetSampledPositions only needs to run the exact inside test on
			// cells actually crossed by the outline.
			ClassifyCells(iter.dOcclusionValue_outside_);
		}
		else
		{
			// Code not extracted from vue.git. Mark the whole map as occluded so that callers do not
			// populate the entire box from an unbuilt map (segments_/cellKinds_ were cleared above).
			BE_ISSUE("ribbon mode not supported");
			std::fill(pData_.begin(), pData_.end(), 0.0);
			return false;
		}

#if IS_EON_DEV()
		// convert maps to image for debug purpose
		DumpToImage();
#endif //DEV

		return true;
	}

	void OcclusionMap::ClassifyCells(double dOcclusionValue_outside)
	{
		const int nGridCells = nWidth_ * nHeight_;
		if (nGridCells <= 0 || (int)pData_.size() < nGridCells
			|| dCellWidth_ <= 0. || dCellHeight_ <= 0.)
		{
			// Empty or degenerated grid (null cell size would produce inf/NaN below): no
			// classification available => GetSampledPositions falls back to the exact test.
			cellKinds_.clear();
			return;
		}

		// 1. Interior / Outside from the scan-line result (cell center classification).
		cellKinds_.resize(nGridCells);
		for (int c = 0; c < nGridCells; ++c)
		{
			cellKinds_[c] = static_cast<uint8_t>(
				(pData_[c] != dOcclusionValue_outside) ? ECellKind::Interior : ECellKind::Outside);
		}

		// 2D bounding box of the projected outline (used as early-out in IsInsideOutline).
		outlineBox_ = BoundingBox();
		for (Segment_2D const& seg : segments_)
		{
			ExtendBox(outlineBox_, glm::dvec3(seg.posStart_, 0.));
			ExtendBox(outlineBox_, glm::dvec3(seg.posEnd_, 0.));
		}

		// 2. Boundary: every cell crossed by an outline segment.
		// For each segment and each row it spans, clip the segment to the row band and mark all cells
		// covered by the clipped X range. This is exact for axis-aligned cells (a segment crosses a
		// cell iff its X range clipped to the cell's Y band overlaps the cell's X range).
		const double invW = 1.0 / dCellWidth_;
		const double invH = 1.0 / dCellHeight_;
		const double gridMaxX = dOrigX_ + nWidth_ * dCellWidth_;
		const double gridMaxY = dOrigY_ + nHeight_ * dCellHeight_;
		// Small conservative margin to absorb floating-point error at cell borders.
		const double epsX = 1e-6 * dCellWidth_;
		const double epsY = 1e-6 * dCellHeight_;

		auto rowOf = [&](double y) -> int
		{
			return std::clamp(static_cast<int>(std::floor((y - dOrigY_) * invH)), 0, nHeight_ - 1);
		};
		auto colOf = [&](double x) -> int
		{
			return std::clamp(static_cast<int>(std::floor((x - dOrigX_) * invW)), 0, nWidth_ - 1);
		};

		for (Segment_2D const& seg : segments_)
		{
			const double x0 = seg.posStart_.x, y0 = seg.posStart_.y;
			const double x1 = seg.posEnd_.x, y1 = seg.posEnd_.y;
			const double segMinY = std::min(y0, y1) - epsY;
			const double segMaxY = std::max(y0, y1) + epsY;
			if (segMaxY < dOrigY_ || segMinY > gridMaxY)
				continue;

			const double dx = x1 - x0;
			const double dy = y1 - y0;
			const bool horizontal = (std::abs(dy) <= epsY);
			const double dxdy = horizontal ? 0. : (dx / dy);

			const int jMin = rowOf(segMinY);
			const int jMax = rowOf(segMaxY);
			for (int j = jMin; j <= jMax; ++j)
			{
				double xLo, xHi;
				if (horizontal)
				{
					xLo = std::min(x0, x1);
					xHi = std::max(x0, x1);
				}
				else
				{
					const double bandLo = std::clamp(dOrigY_ + j * dCellHeight_ - epsY, segMinY, segMaxY);
					const double bandHi = std::clamp(dOrigY_ + (j + 1) * dCellHeight_ + epsY, segMinY, segMaxY);
					const double xA = x0 + (bandLo - y0) * dxdy;
					const double xB = x0 + (bandHi - y0) * dxdy;
					xLo = std::min(xA, xB);
					xHi = std::max(xA, xB);
				}
				xLo -= epsX;
				xHi += epsX;
				if (xHi < dOrigX_ || xLo > gridMaxX)
					continue;

				const int iMin = colOf(xLo);
				const int iMax = colOf(xHi);
				uint8_t* row = cellKinds_.data() + static_cast<size_t>(j) * nWidth_;
				for (int i = iMin; i <= iMax; ++i)
				{
					row[i] = static_cast<uint8_t>(ECellKind::Boundary);
				}
			}
		}
	}

	OcclusionMap::ECellKind OcclusionMap::GetCellKind(int cellIndex) const
	{
		if (cellIndex < 0 || cellIndex >= (int)cellKinds_.size())
		{
			// No outline information: nothing can be excluded, consider everything interior.
			return ECellKind::Interior;
		}
		return static_cast<ECellKind>(cellKinds_[cellIndex]);
	}

	bool OcclusionMap::IsInsideOutline(double x, double y) const
	{
		if (segments_.empty())
			return true;

		if (IsInitialized(outlineBox_)
			&& (x < outlineBox_.min[0] || x > outlineBox_.max[0]
				|| y < outlineBox_.min[1] || y > outlineBox_.max[1]))
		{
			return false;
		}

		// Even-odd ray casting along +X. Segments are sorted by min Y (see Compare2DSegments_Y),
		// so we can stop as soon as a segment starts above y.
		int crossings = 0;
		for (Segment_2D const& seg : segments_)
		{
			const double y0 = seg.posStart_.y;
			const double y1 = seg.posEnd_.y;
			if (std::min(y0, y1) > y)
				break;
			// half-open test so that a vertex shared by 2 segments is counted once.
			if ((y0 <= y) == (y1 <= y))
				continue;
			const double ix = seg.posStart_.x + (y - y0) * (seg.posEnd_.x - seg.posStart_.x) / (y1 - y0);
			crossings += (x < ix) ? 1 : 0;
		}
		return (crossings & 1) != 0;
	}

	namespace
	{

		inline void AdjustPositionInCell(glm::dvec3& location, double cellCenterX, double cellCenterY,
			OcclusionMap const& map2d,
			const int poissonGridId, const size_t posId)
		{
			// use a slightly different position for each sample
			double cellWidth = map2d.GetCellWidth();
			double cellHeight = map2d.GetCellHeight();
			double cellX0 = cellCenterX - 0.5 * cellWidth; // left top corner of the cell
			double cellY0 = cellCenterY - 0.5 * cellHeight;
			location.x = cellX0 + cellWidth * Poisson2D::GetPoisson2DGridX(poissonGridId, 4 + posId);
			location.y = cellY0 + cellHeight * Poisson2D::GetPoisson2DGridY(poissonGridId, 4 + posId);
		}

		bool FindRandLocation(glm::dvec3& location,
			const double cellCenterX, const double cellCenterY, const int cellIndex,
			OcclusionMap const& map2d, RandomNumberGenerator& rand)
		{
			const int cellSeed = rand.Rand();
			const int poissonGridId = cellSeed % Poisson2D::NUM_POISSON_2DGRIDS;

			size_t posId(0);
			const size_t maxPoissonLevel = 10; //eco_poisson::GRID_SIZE - 4;
			while (posId < maxPoissonLevel)
			{
				AdjustPositionInCell(location, cellCenterX, cellCenterY, map2d, poissonGridId, posId);
				const double local_density = map2d.EvaluateValueAt(location.x, location.y, cellIndex);
				if (local_density >= rand.RandDouble())
				{
					location.z = 0.;
					return true;
				}
				posId++;
			}
			return false;
		}
	}

	size_t OcclusionMap::GetSampledPositions(std::vector<glm::dvec3>& outPositions,
		bool forceAligned, uint32_t randSeed, float angle) const
	{
		outPositions.clear();
		BE_ASSERT(nCells_ > 0 && (int)pData_.size() == nCells_);

		RandomNumberGenerator rand(randSeed);

		// Only candidates generated in cells crossed by the outline need the exact test: interior
		// cells are inside by construction, and cells with a null density are skipped anyway.
		// Without cell classification (degenerated grid), every candidate is tested exactly.
		const bool hasOutline = !segments_.empty();
		const bool hasCellKinds = hasOutline && ((int)cellKinds_.size() >= nWidth_ * nHeight_);
		auto acceptCandidate = [&](int cellIndex, double px, double py) -> bool
		{
			if (!hasOutline)
				return true;
			if (!hasCellKinds)
				return IsInsideOutline(px, py);
			switch (static_cast<ECellKind>(cellKinds_[cellIndex]))
			{
			case ECellKind::Interior:
				return true;
			case ECellKind::Outside:
				// Note: in occlusion mode, outside cells keep a non-null density (1.0), hence this
				// explicit rejection (the pData_ > 0 test done by the callers is not sufficient).
				return false;
			case ECellKind::Boundary:
			default:
				return IsInsideOutline(px, py);
			}
		};

		if (forceAligned)
		{
			double boxMinX, boxMaxX, boxMinY, boxMaxY;
			this->Get2DBoxInfo(boxMinX, boxMaxX, boxMinY, boxMaxY);
			const double centerX = (boxMinX + boxMaxX) * 0.5;
			const double centerY = (boxMinY + boxMaxY) * 0.5;
			const double cosA = std::cos(static_cast<double>(angle));
			const double sinA = std::sin(static_cast<double>(angle));

			const double halfW = (boxMaxX - boxMinX) * 0.5;
			const double halfH = (boxMaxY - boxMinY) * 0.5;
			const double absCos = std::abs(cosA);
			const double absSin = std::abs(sinA);
			const double extHalfW = halfW * absCos + halfH * absSin;
			const double extHalfH = halfW * absSin + halfH * absCos;

			const double cellW = GetCellWidth();
			const double cellH = GetCellHeight();

			const int halfCountX = static_cast<int>(std::ceil(extHalfW / cellW));
			const int halfCountY = static_cast<int>(std::ceil(extHalfH / cellH));

			for (int j = -halfCountY; j <= halfCountY; ++j)
			{
				const double ly = j * cellH;
				for (int i = -halfCountX; i <= halfCountX; ++i)
				{
					const double lx = i * cellW;

					const double wx = centerX + lx * cosA - ly * sinA;
					const double wy = centerY + lx * sinA + ly * cosA;

					if (wx < boxMinX || wx > boxMaxX || wy < boxMinY || wy > boxMaxY)
						continue;

					// Cell containing (wx, wy). dOrigX_/dOrigY_ are the grid *min corner* (see
					// TBasic2DMap::InitWith), not the center of cell (0,0) (that is dStartX_/dStartY_, i.e.
					// GetStart2DPosX/Y()). Hence floor from the min corner is the exact cell index, and is the
					// same mapping as ClassifyCells (and equivalent to GetRawValueAt's round from the cell
					// center, except on exact ties).
					const int ci = std::clamp(static_cast<int>(std::floor((wx - dOrigX_) / cellW)), 0, GetWidth() - 1);
					const int cj = std::clamp(static_cast<int>(std::floor((wy - dOrigY_) / cellH)), 0, GetHeight() - 1);
					const int cellIndex = ci + cj * GetWidth();
					if (pData_[cellIndex] <= 0)
						continue;
					if (!acceptCandidate(cellIndex, wx, wy))
						continue;

					outPositions.emplace_back(wx, wy, 0.);
				}
			}
		}
		else
		{
			int cellIndex = 0;
			double y = GetStart2DPosY();
			for (int j = 0; j < GetHeight(); ++j)
			{
				double x = GetStart2DPosX();
				for (int i = 0; i < GetWidth(); ++i)
				{
					if (pData_[cellIndex] > 0)
					{
						glm::dvec3 location;
						if (FindRandLocation(location, x, y, cellIndex, *this, rand)
							&& acceptCandidate(cellIndex, location.x, location.y))
						{
							outPositions.emplace_back(location);
						}
					}
					x += GetCellWidth();
					cellIndex++;
				}
				y += GetCellHeight();
			}
		}

		return outPositions.size();
	}

} // [end-of-NameSpace__BeUtils]
