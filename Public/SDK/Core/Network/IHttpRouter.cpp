/*--------------------------------------------------------------------------------------+
|
|     $Source: IHttpRouter.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#include "IHttpRouter.h"

#include <Core/Tools/Assert.h>

namespace SDK::Core
{
	template<>
	std::function<std::shared_ptr<IHttpRouter>()> Tools::Factory<IHttpRouter>::newFct_ = []() {
		BE_ISSUE("No Http router provided directly in SDK");
		std::shared_ptr<IHttpRouter> nullRouter;
		return nullRouter;
	};

	IHttpRouter::~IHttpRouter()
	{

	}

	IHttpRouter::RouteHandle::~RouteHandle()
	{

	}
}
