/*--------------------------------------------------------------------------------------+
|
|     $Source: Basic2DMap.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "SplineDefines.h"

#include <BeUtils/SplineSampling/MathTypes.h>

namespace BeUtils
{
	template <typename T>
	class TBasic2DMap
	{
		//------------------------------------------------------------------------
		// Basic 2D Map.
		//	Base class for 2D occlusion map and gradient map used to populate
		//	a zone.
		//------------------------------------------------------------------------
		typedef T data_type;

	public:
		TBasic2DMap() {}

		TBasic2DMap(BoundingBox const& Box,
			int nCellsAlongX, int nCellsAlongY,
			int superSamplingFactor = 1,
			float fDistribQuality = 1.f,
			int nCustomCells = -1);

		virtual ~TBasic2DMap() {}

		void InitWith(BoundingBox const& Box,
			int nCellsAlongX, int nCellsAlongY,
			int superSamplingFactor = 1,
			float fDistribQuality = 1.f,
			int nCustomCells = -1);

		//! Returns the occlusion at given world (X,Y) position.
		T EvaluateValueAt(double x, double y, int cellIndex) const;
		//! Returns raw value at given world (X,Y) position (without blending with neighbors).
		inline T GetRawValueAt(double x, double y) const;

		//! Returns the interpolated value at given world (X,Y) position - discards values not valid.
		T InterpolateValidValueAt(double x, double y, T const& discarded_value) const;

		[[nodiscard]] T ComputeMeanValue() const;

		void AllocateData(T const& dInitialValue);
		void FillMapWithValue(T const& dValue);
		inline void SetValueAtCell(int cellIndex, T const& dValue);

		inline T GetValueAtCell(int cellIndex) const;

		//! Switches interpolation with neighboring cells On/Off. \see EvaluateValueAt.
		void EnableInterpolation(bool bInterpolate) { bInterpolate_ = bInterpolate; }

		int CountCells() const { return nCells_; }
		int GetWidth() const { return nWidth_; }
		int GetHeight() const { return nHeight_; }
		glm::vec2 GetResolution() const { return glm::vec2(1.f * GetWidth(), 1.f * GetHeight()); }

		double GetCellWidth() const { return dCellWidth_; }
		double GetCellHeight() const { return dCellHeight_; }

		double GetStart2DPosX() const { return dStartX_; }
		double GetStart2DPosY() const { return dStartY_; }
		int GetSuperSamplingFactor() const { return superSamplingFactor_; }

		inline void Get2DBoxInfo(double& dMinX, double& dMaxX, double& dMinY, double& dMaxY) const;


		[[nodiscard]] T ComputeMean(const T* pInBuffer, int x, int y) const;

		template <typename Filter>
		[[nodiscard]] T ComputeMeanWithFilter(const Filter& filter, const T* pInBuffer, int x, int y) const;


#if IS_EON_DEV()
		//-----------------------------------------------
		// Additional methods to help debugging.
		//-----------------------------------------------
		void SetDumpToImagePath(const std::filesystem::path& strImageName) { strDumpPath_ = strImageName; }
		virtual void DumpToImage() const = 0; //!< Creates an image from the current map. (Debug purpose)
#endif //DEV


	protected:
		template <typename Filter>
		T TInterpolateValueAt(const Filter& filter, double x, double y) const;

	protected:
		/// The actual occlusion values (in range [0..1], 0 for total occlusion.
		std::vector<T> pData_;

		BoundingBox Box_;
		double dOrigX_ = -1., dOrigY_ = -1.;
		double dBoxWidth_ = 1., dBoxHeight_ = 1.;
		/// original position shifted of (1/2W, 1/2H) -> center of cell (0,0)
		double dStartX_ = -1., dStartY_ = -1.;

		int superSamplingFactor_ = 1;
		int nWidth_ = 0, nHeight_ = 0;
		int nCells_ = 1;
		double dWorldToX_ = -1., dWorldToY_ = -1.;
		double dCellWidth_ = 1., dCellHeight_ = 1.;
		/// whether we interpolate value with neighborhood (true except for 3D population)
		bool bInterpolate_ = true;

#if IS_EON_DEV()
		std::filesystem strDumpPath_; // (dev-only)
#endif
	};

	using BasicDouble2DMap = TBasic2DMap<double>;
}


#include "Basic2DMap.inl"

