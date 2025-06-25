/*--------------------------------------------------------------------------------------+
|
|     $Source: CommonInterfaceClass.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once
#include "TypeId.h"
#include "Extension.h"
#include "FactoryClass.h"

namespace AdvViz::SDK::Tools
{
	template<typename T>
	class CommonInterfaceClass : public Tools::Factory<T>,
		public WithStrongTypeId<T>,
		public Tools::ExtensionSupport
	{};

}