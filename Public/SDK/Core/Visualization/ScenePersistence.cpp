/*--------------------------------------------------------------------------------------+
|
|     $Source: ScenePersistence.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ScenePersistence.h"

#include <ostream>

namespace AdvViz::SDK {

	std::ostream& operator<< (std::ostream& os, ILink const& link)
	{
		os << "ID: " << link.GetDBIdentifier() << " type: " << link.GetType();
		if (!link.GetRef().empty())
		{
			os << " ref: " << link.GetRef();
		}
		if (!link.GetName().empty())
		{
			os << " name: " << link.GetName();
		}
		return os;
	}
}
