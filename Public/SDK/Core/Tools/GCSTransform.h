/*--------------------------------------------------------------------------------------+
|
|     $Source: GCSTransform.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "Core/Tools/Tools.h"

namespace AdvViz::SDK::Tools
{
	class IGCSTransform : public Tools::Factory<IGCSTransform>,	public Tools::ExtensionSupport
	{
	public:
		virtual double4 PositionFromClient(const double4&) = 0;
		virtual double4 PositionToClient(const double4&) = 0;
	};

	// no default implementation yet, does nothing actually (identity transform)
	class ADVVIZ_LINK GCSTransform : public IGCSTransform, public Tools::TypeId<GCSTransform>
	{
	public:
		double4 PositionFromClient(const double4&) override;
		double4 PositionToClient(const double4&) override;

		class Impl;
		Impl& GetImpl();
		const Impl& GetImpl() const;

		GCSTransform();
		~GCSTransform();

	private:
		const std::shared_ptr<Impl> impl_;
	};

}