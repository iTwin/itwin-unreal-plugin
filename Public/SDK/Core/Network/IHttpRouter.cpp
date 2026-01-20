/*--------------------------------------------------------------------------------------+
|
|     $Source: IHttpRouter.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#include "IHttpRouter.h"

#include <Core/Tools/Assert.h>
#include "../Singleton/singleton.h"

namespace AdvViz::SDK
{
	template<>
	 Tools::Factory<IHttpRouter>::Globals::Globals()
	{
		newFct_ = []() {
			BE_ISSUE("No Http router provided directly in SDK");
			return nullptr;
			};
	}

	template<>
	 Tools::Factory<IHttpRouter>::Globals& Tools::Factory<IHttpRouter>::GetGlobals()
	{
		return singleton<Tools::Factory<IHttpRouter>::Globals>();
	}

	IHttpRouter::~IHttpRouter()
	{
	}

	IHttpRouter::RouteHandle::~RouteHandle()
	{

	}
}
