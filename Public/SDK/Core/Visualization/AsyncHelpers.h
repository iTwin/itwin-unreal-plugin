/*--------------------------------------------------------------------------------------+
|
|     $Source: AsyncHelpers.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Core/Tools/Assert.h>
#include <atomic>
#include <functional>
#include <memory>

namespace AdvViz::SDK
{
	/// The notion of main thread typically refers to the thread in which user interactions are performed.
	/// This may has no meaning at all of the application has no UI.
	/// In Unreal, this will be an equivalent for the 'Game' thread.
	/// InitMainThreadId should be called once at the beginning of the session, by the thread which should
	/// be considered as the main one.
	ADVVIZ_LINK void InitMainThreadId();
	ADVVIZ_LINK bool IsMainThread();

	/// Globally set whether the execution of callbacks for asynchronous requests in game/main thread is
	/// supported (just to disable some asserts in case it is not...)
	/// Typically, the CPR implementation of Http does not support it.
	ADVVIZ_LINK void SetSupportAsyncCallbacksInMainThread(bool bHasFeature);
	/// If the execution of callbacks for asynchronous requests in game/main thread is supported, returns
	/// whether the current thread is allowed to process the callback. Else return true.
	ADVVIZ_LINK bool IsValidThreadForAsyncSaving();


	/// Store a callback and execute it only when a group of asynchronous requests are *all* processed.
	class ADVVIZ_LINK AsyncRequestGroupCallback
	{
	public:
		/// Callback when the group of asynchronous requests is fully done.
		using CallbackFunc = std::function<void(bool /*bSuccess*/)>;

		/// Shared pointer allowing to detect when the callback has been made impossible to call (typically
		/// when some captured variables have become invalid, upon application exit).
		using ValidityPtr = std::shared_ptr<std::atomic_bool>;

		AsyncRequestGroupCallback(CallbackFunc&& inCallbackFunc, ValidityPtr const& isValid);
		~AsyncRequestGroupCallback();

		/// Returns whether the callback is still valid: due to the asynchronous nature of the process, the
		/// callback may have become invalid when the request is done (typically in case of application or
		/// game session exit).
		bool IsValid() const;

		/// Increment the number of requests in the group.
		void AddRequestToWait();

		// Notifies the fact the first level of request have been registered (this is needed for some cases
		// where the initial requests could return at once, before the other ones are registered (early exit
		// cases): in such case, we need to wait until the 1st level of requests are *all* registered before
		// executing the callback.
		void OnFirstLevelRequestsRegistered();

		/// Should be called when one request has finished, with a boolean indicating its success.
		void OnRequestDone(bool hasSucceeded);


	private:
		class Impl;
		Impl& GetImpl() { return *impl_; }
		const Impl& GetImpl() const { return *impl_; }

		const std::unique_ptr<Impl> impl_;
	};
}
