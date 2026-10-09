/*--------------------------------------------------------------------------------------+
|
|     $Source: AsyncTestHelpers.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <atomic>

namespace AdvViz::SDK
{
	//! Wait for an asynchronous task to complete, during a maximum of maxSeconds.
	//! The atomic_bool taskFinished should be set to true by the asynchronous task when it is done.
	bool WaitForAsyncTask(std::atomic_bool& taskFinished, int maxSeconds);

} // namespace AdvViz::SDK
