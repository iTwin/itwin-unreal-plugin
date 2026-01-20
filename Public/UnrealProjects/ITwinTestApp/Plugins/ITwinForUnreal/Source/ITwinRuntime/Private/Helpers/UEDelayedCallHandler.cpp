/*--------------------------------------------------------------------------------------+
|
|     $Source: UEDelayedCallHandler.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#include "UEDelayedCallHandler.h"

#include <Containers/Ticker.h>
#include <map>


class FUEDelayedCallHandler::FImpl
{
public:
	FImpl()
	{
	}

	~FImpl()
	{
		// Stop remaining timers
		for (auto& [timerId, tickerHandle] : tickerHandlesMap_)
		{
			if (tickerHandle.IsValid())
			{
				FTSTicker::GetCoreTicker().RemoveTicker(tickerHandle);
				tickerHandle.Reset();
			}
		}
		tickerHandlesMap_.clear();
	}

	void UniqueDelayedCall(std::string const& UniqueId,
		AdvViz::SDK::DelayedCallFunc&& Func, float DelayInSeconds)
	{
		FTSTicker::FDelegateHandle& tickerHandle = tickerHandlesMap_[UniqueId];
		if (tickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(tickerHandle);
			tickerHandle.Reset();
		}
		if (Func)
		{
			tickerHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateLambda([Func = std::move(Func)](float Delta) -> bool
			{
				using namespace AdvViz::SDK;

				DelayedCall::EReturnedValue const ret = Func();
				// See FTSTicker documentation: return true to fire after another delay.
				return ret == DelayedCall::EReturnedValue::Repeat;
			}), DelayInSeconds);
		}
	}

private:
	std::map<std::string, FTSTicker::FDelegateHandle> tickerHandlesMap_;
};


FUEDelayedCallHandler::FUEDelayedCallHandler()
	: Impl(MakePimpl<FUEDelayedCallHandler::FImpl>())
{

}

void FUEDelayedCallHandler::UniqueDelayedCall(std::string const& UniqueId,
	AdvViz::SDK::DelayedCallFunc&& Func, float DelayInSeconds)
{
	Impl->UniqueDelayedCall(UniqueId, std::move(Func), DelayInSeconds);
}
