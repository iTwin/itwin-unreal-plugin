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

}
