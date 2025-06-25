/*--------------------------------------------------------------------------------------+
|
|     $Source: FactoryClass.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "FactoryClass.h"

namespace AdvViz::SDK::Tools::Internal
{
	ADVVIZ_LINK void DefaultDelete(void* p)
	{
		::operator delete(p);
	}
}