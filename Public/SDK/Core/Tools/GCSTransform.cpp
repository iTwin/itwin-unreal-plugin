/*--------------------------------------------------------------------------------------+
|
|     $Source: GCSTransform.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "GCSTransform.h"
#include "Core/Tools/FactoryClassInternalHelper.h"

#include <glm/vec3.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace AdvViz::SDK::Tools
{
	static_assert(sizeof(dmat4x4) == sizeof(glm::dmat4x4));
	static_assert(sizeof(double4) == sizeof(glm::dvec4));

	class GCSTransform::Impl
	{
	};

	double4 GCSTransform::PositionFromClient(const double4& v)
	{
		return v;
	}
	
	double4 GCSTransform::PositionToClient(const double4& v)
	{
		return v;
	}

	GCSTransform::Impl& GCSTransform::GetImpl()
	{
		return *impl_;
	}

	const GCSTransform::Impl& GCSTransform::GetImpl() const
	{
		return *impl_;
	};


	GCSTransform::GCSTransform() :impl_(new Impl())
	{
	}

	GCSTransform::~GCSTransform()
	{
	}

	DEFINEFACTORYGLOBALS(GCSTransform);
}