/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAuthManager.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "ITwinAuthManager.h"

#include <Core/Tools/Assert.h>

namespace SDK::Core
{

	/*static*/
	ITwinAuthManager::Pool ITwinAuthManager::instances_;

	/*static*/
	ITwinAuthManager::SharedInstance& ITwinAuthManager::GetInstance(EITwinEnvironment env)
	{
		static std::mutex PoolMutex;

		const size_t envIndex = (size_t)env;
		BE_ASSERT(envIndex < instances_.size(), "Invalid environment", envIndex);

		std::unique_lock<std::mutex> lock(PoolMutex);
		if (!instances_[envIndex])
		{
			instances_[envIndex].reset(new ITwinAuthManager(env));
		}
		return instances_[envIndex];
	}


	ITwinAuthManager::ITwinAuthManager(EITwinEnvironment env)
		: env_(env)
	{
	}

	ITwinAuthManager::~ITwinAuthManager()
	{
	}

	bool ITwinAuthManager::HasAccessToken() const
	{
		FLock Lock(mutex_);
		return !GetCurrentAccessToken().empty();
	}

	void ITwinAuthManager::GetAccessToken(std::string& outAccessToken) const
	{
		FLock Lock(mutex_);
		outAccessToken = GetCurrentAccessToken();
	}

	void ITwinAuthManager::SetAccessToken(std::string const& accessToken)
	{
		FLock Lock(mutex_);
		accessToken_ = accessToken;
	}

	void ITwinAuthManager::SetOverrideAccessToken(std::string const& accessToken)
	{
		FLock Lock(mutex_);
		overrideAccessToken_ = accessToken;
	}

	std::string const& ITwinAuthManager::GetCurrentAccessToken() const
	{
		return !overrideAccessToken_.empty() ? overrideAccessToken_ : accessToken_;
	}
}
