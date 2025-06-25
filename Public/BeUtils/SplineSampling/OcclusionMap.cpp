/*--------------------------------------------------------------------------------------+
|
|     $Source: OcclusionMap.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "OcclusionMap.h"

#include "Poisson2D.h"
#include "SplinePattern.h"

#include <BeUtils/Misc/Random.h>

#include <algorithm>
#include <async++.h>


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
				if (fabs(pData_[i] - dRefVal) > 1e-4)
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

		// setup sampling resolution
		// no need to sample the spline at a too high resolution (the higher
		// resolution should be that of the occlusion map)
		double dS = 1. / 16;
		double splineLen(0.0);
		double velocity = p2DPath.GetMaxVelocity(splineLen);
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

		std::vector<Segment_2D> segments;
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
				p2DPath.GetProjection());
			std::sort(segments.begin(), segments.end(), Compare2DSegments_Y);

			if (segments.size() < 3)
			{
				// invalid curve (empty spline?)
				return true;
			}

			// generate map from segments through a scan-line algorithm
			SplineOcclMapLoopIter iter(
				p2DPath,
				pData_,
				*this,
				progHelper,
				segments);

			async::parallel_for(async::irange(0, nHeight_), [&iter](int y) {
				iter.RunSubTask(y);
			});

			if (iter.CancelledPopulating())
			{
				return false;
			}
		}
		else
		{
			// Code not extracted from vue.git
			BE_ISSUE("ribbon mode not supported");
		}

#if IS_EON_DEV()
		// convert maps to image for debug purpose
		DumpToImage();
#endif //DEV

		return true;
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
		bool forceAligned, uint32_t randSeed) const
	{
		outPositions.clear();
		BE_ASSERT(nCells_ > 0 && (int)pData_.size() == nCells_);

		RandomNumberGenerator rand(randSeed);

		int cellIndex = 0;
		double y = GetStart2DPosY();
		for (int j = 0; j < GetHeight(); ++j)
		{
			double x = GetStart2DPosX();
			for (int i = 0; i < GetWidth(); ++i)
			{
				if (pData_[cellIndex] > 0)
				{
					// The current cell belongs to the spline's interior => add a sample.
					if (forceAligned)
					{
						outPositions.emplace_back(x, y, 0.);
					}
					else
					{
						glm::dvec3 location;
						if (FindRandLocation(location, x, y, cellIndex, *this, rand))
						{
							outPositions.emplace_back(location);
						}
					}
				}
				x += GetCellWidth();
				cellIndex++;
			}
			y += GetCellHeight();
		}

		return outPositions.size();
	}

} // [end-of-NameSpace__BeUtils]
