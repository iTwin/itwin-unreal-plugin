/*--------------------------------------------------------------------------------------+
|
|     $Source: AsyncHelpers.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "AsyncHelpers.h"

#include <Core/Network/Network.h>
#include <optional>
#include <thread>

namespace AdvViz::SDK
{

	static std::optional<std::thread::id> mainThreadId;
	static std::optional<bool> bSupportAsyncCallbacksInMainThread;

	void InitMainThreadId()
	{
		mainThreadId = std::this_thread::get_id();
	}
	bool IsMainThread()
	{
		if (!mainThreadId)
		{
			BE_ISSUE("please call InitMainThreadId before calling IsMainThread");
			return false;
		}
		return *mainThreadId == std::this_thread::get_id();
	}
	void SetSupportAsyncCallbacksInMainThread(bool bHasFeature)
	{
		bSupportAsyncCallbacksInMainThread = bHasFeature;
	}

	inline bool SupportAsyncCallbacksInMainThread()
	{
		BE_ASSERT(bSupportAsyncCallbacksInMainThread.has_value(),
			"please call SetSupportAsyncCallbacksInMainThread before running async saving");
		return bSupportAsyncCallbacksInMainThread.value_or(false);
	}

	bool IsValidThreadForAsyncSaving()
	{
		if (SupportAsyncCallbacksInMainThread())
		{
			return IsMainThread();
		}
		else
		{
			// Callback in game thread not supported - then it's the responsibility of the client application
			// to guarantee thread safety...
			// Only check that the main thread ID has been initialized...
			BE_ASSERT(mainThreadId.has_value(), "missing call to InitMainThreadId");
			return true;
		}
	}


	class AsyncRequestGroupCallback::Impl
	{
	public:
		Impl(CallbackFunc&& inCallbackFunc, ValidityPtr const& isValid)
			: callbackFunc_(std::move(inCallbackFunc))
			, isThisValid_(isValid)
		{
			BE_ASSERT(IsValidThreadForAsyncSaving());
			BE_ASSERT(IsValid()); // Initiating a callback already invalid?
		}

		bool IsValid() const
		{
			BE_ASSERT(IsValidThreadForAsyncSaving());
			return (isThisValid_ && isThisValid_->load());
		}

		void AddRequestToWait()
		{
			BE_ASSERT(IsValidThreadForAsyncSaving());
			BE_ASSERT(!hasExecutedCallback_, "late registration");

			requestsToWait_++;
		}

		void OnFirstLevelRequestsRegistered()
		{
			hasRegistered1stLevelRequests_ = true;
			ExecuteCallbackIfNeeded();
		}

		void OnRequestDone(bool hasSucceeded)
		{
			BE_ASSERT(requestsToWait_ > 0);

			requestsToWait_--;
			if (!hasSucceeded)
				hasError_ = true;
			ExecuteCallbackIfNeeded();
		}

		void ExecuteCallbackIfNeeded()
		{
			BE_ASSERT(IsValidThreadForAsyncSaving());
			if (requestsToWait_ == 0 && hasRegistered1stLevelRequests_ && callbackFunc_ && !hasExecutedCallback_)
			{
				callbackFunc_(hasError_.load() == false);
				hasExecutedCallback_ = true;
			}
		}

	private:
		CallbackFunc callbackFunc_;
		ValidityPtr isThisValid_;
		std::atomic_int requestsToWait_ = 0;
		std::atomic_bool hasRegistered1stLevelRequests_ = false;
		std::atomic_bool hasExecutedCallback_ = false;
		std::atomic_bool hasError_ = false;
	};


	AsyncRequestGroupCallback::AsyncRequestGroupCallback(
		CallbackFunc&& inCallbackFunc, ValidityPtr const& isValid)
		: impl_(new Impl(std::move(inCallbackFunc), isValid))
	{
	}

	AsyncRequestGroupCallback::~AsyncRequestGroupCallback()
	{

	}

	// Due to the asynchronous system, the callback may have become invalid when the request is
	// done (typically in case of scene exit).
	bool AsyncRequestGroupCallback::IsValid() const
	{
		return GetImpl().IsValid();
	}

	void AsyncRequestGroupCallback::AddRequestToWait()
	{
		GetImpl().AddRequestToWait();
	}

	void AsyncRequestGroupCallback::OnFirstLevelRequestsRegistered()
	{
		GetImpl().OnFirstLevelRequestsRegistered();
	}

	void AsyncRequestGroupCallback::OnRequestDone(bool hasSucceeded)
	{
		GetImpl().OnRequestDone(hasSucceeded);
	}

}
