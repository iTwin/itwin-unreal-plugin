/*--------------------------------------------------------------------------------------+
|
|     $Source: SavableItemManager.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#include "SavableItemManager.h"

#include "AsyncHelpers.h"
#include "Config.h"

#include <Core/Network/http.h>

namespace AdvViz::SDK
{
	SavableItemManager::SavableItemManager()
	{
		isThisValid_ = std::make_shared<std::atomic_bool>(true);

		SetHttp(GetDefaultHttp());
	}

	SavableItemManager::~SavableItemManager()
	{
		*isThisValid_ = false;
	}

}
