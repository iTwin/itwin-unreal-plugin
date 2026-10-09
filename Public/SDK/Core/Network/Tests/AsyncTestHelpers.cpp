/*--------------------------------------------------------------------------------------+
|
|     $Source: AsyncTestHelpers.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "AsyncTestHelpers.h"

#include <chrono>
#include <thread>

namespace AdvViz::SDK
{
	bool WaitForAsyncTask(std::atomic_bool& taskFinished, int maxSeconds)
	{
		using namespace std::chrono_literals;

		int elapsedMilliSec = 0;
		while (!taskFinished && elapsedMilliSec < maxSeconds * 1000)
		{
			std::this_thread::sleep_for(100ms);
			elapsedMilliSec += 100;
		}
		return taskFinished;
	}

} // namespace AdvViz::SDK
