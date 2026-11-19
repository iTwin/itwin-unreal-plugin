/*--------------------------------------------------------------------------------------+
|
|     $Source: GCSTransform.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "Core/Tools/Tools.h"

namespace AdvViz::SDK::Tools
{
	class IGCSTransform : public Tools::Factory<IGCSTransform>,	public Tools::ExtensionSupport
	{
	public:
		virtual double3 PositionFromClient(const double3&) const = 0;
		virtual double3 PositionToClient(const double3&) const = 0;
		virtual dmat4x4 MatrixFromClient(const dmat4x4& m) const = 0;
		virtual dmat4x4 MatrixToClient(const dmat4x4& m) const = 0;

		//! Returns +1 if the client -> ECEF map preserves orientation, -1 if it reverses it
		//! (i.e. the map is a reflection, which is the case when the client space is
		//! left-handed, as in Unreal). This drives the convention used for the 'right'
		//! column of camera placements: right = cross(direction, up) when negative,
		//! cross(up, direction) when positive.
		//! The default implementation measures it on the linear part of MatrixFromClient,
		//! so it is correct for any implementation without extra bookkeeping.
		virtual double GetHandedness() const;
	};

	// no default implementation yet, does nothing actually (identity transform)
	class ADVVIZ_LINK GCSTransform : public IGCSTransform, public Tools::TypeId<GCSTransform>
	{
	public:
		double3 PositionFromClient(const double3&) const override;
		double3 PositionToClient(const double3&) const override;
		
		dmat4x4 MatrixFromClient(const dmat4x4& m) const override;
		dmat4x4 MatrixToClient(const dmat4x4& m) const override;

		class Impl;
		Impl& GetImpl();
		const Impl& GetImpl() const;

		GCSTransform();
		~GCSTransform();

		static double3 WGS84GeodeticToECEF(const double3& latLonHeightRad);
		static dmat4x4 WGS84ECEFToENUMatrix(const double3& latLonHeightRad);
		static dmat4x4 WGS84ENUToECEFMatrix(const double3& latLonHeightRad);

		static double3 East(const dmat4x4& enuToEcef);
		static double3 North(const dmat4x4& enuToEcef);
		static double3 Up(const dmat4x4& enuToEcef);

		static const GCS& GetECEFWGS84WKT();

	private:
		const std::shared_ptr<Impl> impl_;
	};

	typedef std::shared_ptr<IGCSTransform> IGCSTransformPtr;
}