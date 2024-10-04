/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAuthManager.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#pragma once


#ifndef SDK_CPPMODULES
	#include <array>
	#include <memory>
	#include <mutex>
	#ifndef MODULE_EXPORT
		#define MODULE_EXPORT
	#endif // !MODULE_EXPORT
#endif

#include "ITwinEnvironment.h"
#include "ITwinTypes.h"

MODULE_EXPORT namespace SDK::Core
{

	/// In the future, the whole authorization process will be moved here.
	/// For now, we only centralize the access token.

	class ITwinAuthManager
	{
	public:
		using SharedInstance = std::shared_ptr<ITwinAuthManager>;

		static SharedInstance& GetInstance(EITwinEnvironment Env);

		~ITwinAuthManager();

		bool HasAccessToken() const;
		void GetAccessToken(std::string& outAccessToken) const;

		//! Sets the regular access token for this environment.
		void SetAccessToken(std::string const& accessToken);

		//! Used by the "Open shared iTwin" feature: overrides the regular access token
		//! with the one provided as argument, so that GetAccessToken() returns the
		//! "override" token instead of the regular one.
		//! Pass an empty string to restore the regular token.
		void SetOverrideAccessToken(std::string const& accessToken);


	private:
		ITwinAuthManager(EITwinEnvironment Env);

		//! Returns the access token to use, it may be the "override" or regular one.
		std::string const& GetCurrentAccessToken() const;


	private:
		const EITwinEnvironment env_;

		using FMutex = std::recursive_mutex;
		using FLock = std::lock_guard<std::recursive_mutex>;
		mutable FMutex mutex_;
		std::string accessToken_;
		std::string overrideAccessToken_;

		using Pool = std::array<SharedInstance, (size_t)EITwinEnvironment::Invalid>;
		static Pool instances_;
	};
}
