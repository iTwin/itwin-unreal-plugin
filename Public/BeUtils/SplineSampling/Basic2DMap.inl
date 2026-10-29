/*--------------------------------------------------------------------------------------+
|
|     $Source: Basic2DMap.inl $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "Basic2DMap.h"

#include <algorithm>

namespace BeUtils
{
	template <typename T>
	inline void TBasic2DMap<T>::Get2DBoxInfo(double& dMinX, double& dMaxX, double& dMinY, double& dMaxY) const
	{
		dMinX = dOrigX_;
		dMaxX = dOrigX_ + dBoxWidth_;
		dMinY = dOrigY_;
		dMaxY = dOrigY_ + dBoxHeight_;
	}

	template <typename T>
	inline T TBasic2DMap<T>::GetRawValueAt(double x, double y) const
	{
		BE_ASSERT((x > dOrigX_ - 1e-4) && (y > dOrigY_ - 1e-4));
		x = dWorldToX_ * (x - dStartX_);
		y = dWorldToY_ * (y - dStartY_);
		int x0 = (int)std::round(x);
		int y0 = (int)std::round(y);
		x0 = std::clamp(x0, 0, nWidth_ - 1);
		y0 = std::clamp(y0, 0, nHeight_ - 1);
		return pData_[x0 + y0 * nWidth_];
	}

	template <typename T>
	inline void TBasic2DMap<T>::SetValueAtCell(int cellIndex, const T& dValue)
	{
		pData_[cellIndex] = dValue;
	}

	template <typename T>
	inline T TBasic2DMap<T>::GetValueAtCell(int cellIndex) const
	{
		return pData_[cellIndex];
	}

	template <typename T>
	template <typename Filter>
	T TBasic2DMap<T>::ComputeMeanWithFilter(const Filter& filter, const T* pInBuffer, int x, int y) const
	{
		BE_ASSERT(x >= 0 && x < nWidth_ && y >= 0 && y < nHeight_);
		constexpr int NEIGHBOURHOOD = 1;

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

}
