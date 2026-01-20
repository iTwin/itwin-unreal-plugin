/*--------------------------------------------------------------------------------------+
|
|     $Source: ControlledCurve.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "ControlledCurve.h"

namespace BeUtils::path
{
	template <typename V>
	GenericCurve<V>::~GenericCurve()
	{

	}

	template class GenericCurve<glm::dvec3>;
	template class ControlledCurve<glm::dvec3>;

} // namespace BeUtils::path
