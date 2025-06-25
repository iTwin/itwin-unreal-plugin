/*--------------------------------------------------------------------------------------+
|
|     $Source: Basic2DMap.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "Basic2DMap.h"
#include "Basic2DMap.inl"

namespace BeUtils
{
	template <typename T>
	TBasic2DMap<T>::TBasic2DMap(BoundingBox const& box, int nCellsAlongX, int nCellsAlongY,
		int superSamplingFactor /*= 1*/, float fDistribQuality /*1.f*/, int nCustomCells /*= -1*/)
	{
		InitWith(box, nCellsAlongX, nCellsAlongY, superSamplingFactor, fDistribQuality, nCustomCells);
	}

	template <typename T>
	void TBasic2DMap<T>::InitWith(BoundingBox const& box, int nCellsAlongX, int nCellsAlongY,
		int superSamplingFactor /*= 1*/, float fDistribQuality /*1.f*/, int nCustomCells /*= -1*/)
	{
		Box_ = box;
		nWidth_ = nCellsAlongX;
		nHeight_ = nCellsAlongY;
		superSamplingFactor_ = superSamplingFactor;

		if (nWidth_ < 1)
		{
			nWidth_ = 1;
		}
		if (nHeight_ < 1)
		{
			nHeight_ = 1;
		}

		int nCells_NoBoost = nWidth_ * nHeight_;

		if (superSamplingFactor > 1)
		{
			// supersampling
			nWidth_ *= superSamplingFactor;
			nHeight_ *= superSamplingFactor;
		}
		else if (fDistribQuality < 1)
		{
			// undersampling
			nWidth_ = (int)ceilf(fDistribQuality * nWidth_);
			nHeight_ = (int)ceilf(fDistribQuality * nHeight_);

			if (nWidth_ < 1) nWidth_ = 1;
			if (nHeight_ < 1) nHeight_ = 1;
		}

		nCells_ = nWidth_ * nHeight_;

		if (nCustomCells > 0 && nCells_NoBoost != nCustomCells)
		{
			// this is used for 3D populator mode (one cell per face in the mesh).
			nCells_ = nCustomCells;
		}

		// initialize size and position helpers.
		dOrigX_ = Box_.min[0];
		dOrigY_ = Box_.min[1];

		dBoxWidth_ = Box_.max[0] - Box_.min[0];
		dBoxHeight_ = Box_.max[1] - Box_.min[1];

		dCellWidth_ = dBoxWidth_ / nWidth_;
		dCellHeight_ = dBoxHeight_ / nHeight_;

		dWorldToX_ = dWorldToY_ = 0.0;
		if (dBoxWidth_ > 0 && dBoxHeight_ > 0)
		{
			// cache division needed in EvaluateOcclusion
			dWorldToX_ = 1.0 * nWidth_ / dBoxWidth_;
			dWorldToY_ = 1.0 * nHeight_ / dBoxHeight_;
		}

		// always start from the center of 1st cell.
		dStartX_ = dOrigX_ + (0.5 * dCellWidth_);
		dStartY_ = dOrigY_ + (0.5 * dCellHeight_);
	}


	template <typename T>
	struct Null2DFilter
	{
		bool DiscardValue(const T&) const { return false; }
		static T GetDiscardedValue() { return T(0); }
	};


	template <typename T>
	struct DiscardHighValue2DFilter
	{
		DiscardHighValue2DFilter(const T& max_value)
			: max_value_(max_value)
		{
		}

		bool DiscardValue(const T& val) const
		{
			return val >= max_value_;
		}

		T GetDiscardedValue() const
		{
			return max_value_;
		}

		T max_value_;
	};

	template <typename T>
	template <typename Filter>
	T TBasic2DMap<T>::TInterpolateValueAt(const Filter& filter, double x, double y) const
	{
		//BE_ASSERT_PERF((x > dOrigX_ - 1e-4) && (y > dOrigY_ - 1e-4));

		x = dWorldToX_ * (x - dStartX_);
		y = dWorldToY_ * (y - dStartY_);

		if (x < 0) x = 0;
		if (x > nWidth_) x = nWidth_;
		if (y < 0) y = 0;
		if (y > nHeight_) y = nHeight_;

		int x0 = (int)floor(x);
		int y0 = (int)floor(y);
		if (x0 == nWidth_) x0 = nWidth_ - 1;
		if (y0 == nHeight_) y0 = nHeight_ - 1;

		int x1 = (x0 == nWidth_ - 1) ? x0 : x0 + 1;
		int y1 = (y0 == nHeight_ - 1) ? y0 : y0 + 1;

		T c00 = pData_[x0 + y0 * nWidth_];
		T c10 = pData_[x1 + y0 * nWidth_];
		T c01 = pData_[x0 + y1 * nWidth_];
		T c11 = pData_[x1 + y1 * nWidth_];

		double fx = x - x0;
		double fy = y - y0;
		double fx1 = 1.0f - fx;
		double fy1 = 1.0f - fy;

		double w00 = fx1 * fy1;
		double w01 = fx1 * fy;
		double w10 = fx * fy1;
		double w11 = fx * fy;

		if (filter.DiscardValue(c00) ||
			filter.DiscardValue(c01) ||
			filter.DiscardValue(c10) ||
			filter.DiscardValue(c11))
		{
			return filter.GetDiscardedValue();
		}
		return (c00 * w00) + (c01 * w01) + (c10 * w10) + (c11 * w11);
	}

	template <typename T>
	T TBasic2DMap<T>::EvaluateValueAt(double x, double y, int cellIndex) const
	{
		if (bInterpolate_)
		{
			typedef Null2DFilter<T> NullFilter;
			return TInterpolateValueAt<NullFilter>(NullFilter(), x, y);
		}
		else
		{
			// no interpolation
			BE_ASSERT(cellIndex >= 0);
			return GetValueAtCell(cellIndex);
		}
	}

	template <typename T>
	T TBasic2DMap<T>::InterpolateValidValueAt(double x, double y, const T& discarded_value) const
	{
		DiscardHighValue2DFilter<T> filter(discarded_value);
		return TInterpolateValueAt(filter, x, y);
	}

	template <typename T>
	T TBasic2DMap<T>::ComputeMeanValue() const
	{
		if (pData_.empty())
		{
			BE_ISSUE("emBEpty map");
			return T(0);
		}
		T accum = T(0);
		for (auto const& v : pData_)
		{
			accum += v;
		}
		return (accum / pData_.size());
	}

	template <typename T>
	void TBasic2DMap<T>::FillMapWithValue(const T& value)
	{
		pData_.assign(pData_.size(), value);
	}

	template <typename T>
	void TBasic2DMap<T>::AllocateData(const T& dInitialValue)
	{
		pData_.clear();
		if (nCells_ > 0)
		{
			pData_.resize(nCells_, dInitialValue);
		}
	}

	template <typename T>
	T TBasic2DMap<T>::ComputeMean(const T* pInBuffer, int x, int y) const
	{
		const int NEIGHBOURHOOD = 1;

		int nMinX = std::max(x - NEIGHBOURHOOD, 0);
		int nMaxX = std::min(x + NEIGHBOURHOOD, nWidth_ - 1);
		int nMinY = std::max(y - NEIGHBOURHOOD, 0);
		int nMaxY = std::min(y + NEIGHBOURHOOD, nHeight_ - 1);

		int w = nMaxX - nMinX + 1;
		int h = nMaxY - nMinY + 1;

		T dSum(0.0);

		pInBuffer += nMinX + nWidth_ * nMinY;

		int nShift = nWidth_ - w;

		for (int j = h; j; --j)
		{
			for (int i = w; i; --i)
				dSum += *pInBuffer++;

			pInBuffer += nShift;
		}

		return dSum / (w * h);
	}


	template <typename T>
	template <typename Filter>
	T TBasic2DMap<T>::ComputeMeanWithFilter(const Filter& filter, const T* pInBuffer, int x, int y) const
	{
		BE_ASSERT(x >= 0 && x < nWidth_ && y >= 0 && y < nHeight_);
		const int NEIGHBOURHOOD = 1;

		int nMinX = std::max(x - NEIGHBOURHOOD, 0);
		int nMaxX = std::min(x + NEIGHBOURHOOD, nWidth_ - 1);
		int nMinY = std::max(y - NEIGHBOURHOOD, 0);
		int nMaxY = std::min(y + NEIGHBOURHOOD, nHeight_ - 1);

		int w = nMaxX - nMinX + 1;
		int h = nMaxY - nMinY + 1;

		T dSum(0.0);
		int nContribs(0);

		T origVal = *(pInBuffer + x + nWidth_ * y);
		pInBuffer += nMinX + nWidth_ * nMinY;

		int nShift = nWidth_ - w;

		for (int j = h; j; --j)
		{
			for (int i = w; i; --i)
			{
				if (filter.ShouldAddContribution(*pInBuffer))
				{
					dSum += *pInBuffer;
					nContribs++;
				}
				pInBuffer++;
			}
			pInBuffer += nShift;
		}
		if (nContribs > 0)
		{
			return dSum / nContribs;
		}
		else
		{
			// keep original value.
			return origVal;
		}
	}

	template class TBasic2DMap<double>;
}

