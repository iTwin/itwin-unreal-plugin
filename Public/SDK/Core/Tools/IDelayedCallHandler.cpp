/*--------------------------------------------------------------------------------------+
|
|     $Source: IDelayedCallHandler.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#include "IDelayedCallHandler.h"

#include <Core/Tools/DelayedCall.h>
#include <Core/Tools/Assert.h>
#include "../Singleton/singleton.h"

#include <mutex>

namespace AdvViz::SDK
{
	template<>
	Tools::Factory<IDelayedCallHandler>::Globals::Globals()
	{
		newFct_ = []() {
			BE_ISSUE("No IDelayedCallHandler provided directly in SDK");
			return nullptr;
		};
	}

	template<>
	Tools::Factory<IDelayedCallHandler>::Globals& Tools::Factory<IDelayedCallHandler>::GetGlobals()
	{
		return singleton<Tools::Factory<IDelayedCallHandler>::Globals>();
	}

	/*static*/
	IDelayedCallHandler::Globals& IDelayedCallHandler::GetGlobals()
	{
		return singleton<IDelayedCallHandler::Globals>();
	}

	/*static*/
	IDelayedCallHandler::SharedInstance& IDelayedCallHandler::GetInstance()
	{
		static std::mutex PoolMutex;

		Globals& globals = GetGlobals();

		std::unique_lock<std::mutex> lock(PoolMutex);

		if (!globals.instance_)
		{
			globals.instance_.reset(IDelayedCallHandler::New());
			BE_ASSERT(globals.instance_, "no delayed call support");
		}
		return globals.instance_;
	}

	IDelayedCallHandler::~IDelayedCallHandler()
	{

	}


	bool UniqueDelayedCall(std::string const& uniqueId, DelayedCallFunc&& func, float delayInSeconds)
	{
		auto& pInstance = IDelayedCallHandler::GetInstance();
		if (pInstance)
		{
			pInstance->UniqueDelayedCall(uniqueId, std::move(func), delayInSeconds);
			return true;
		}
		else
		{
			return false;
		}
	}
}
