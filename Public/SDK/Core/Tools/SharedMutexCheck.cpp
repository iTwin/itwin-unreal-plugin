/*--------------------------------------------------------------------------------------+
|
|     $Source: SharedMutexCheck.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "SharedMutexCheck.h"
#include <unordered_map>
#include "Assert.h"

namespace AdvViz::SDK::Tools
{
	struct LockCounter
	{
		int readCount = 0;
		int writeCount = 0;
	};

#ifndef RELEASE_CONFIG
	// members can't be thread_local, so we use a thread_local map instead to keep per-thread state
	thread_local std::unordered_map<SharedMutexCheck*, LockCounter> g_SharedMutexCheckState;
#endif

	SharedMutexCheck::SharedMutexCheck()
	{
	}

	SharedMutexCheck::~SharedMutexCheck()
	{
	}

	void SharedMutexCheck::lock()
	{
#ifndef RELEASE_CONFIG
		auto& state = g_SharedMutexCheckState[this];
		if (state.writeCount > 0 || state.readCount > 0)
		{
			BE_ISSUE("Deadlock detected: trying to acquire write lock while already holding write lock");
			return;
		}
#endif
		mutex_.lock();

#ifndef RELEASE_CONFIG
		state.writeCount = 1;
#endif
	}

	void SharedMutexCheck::unlock()
	{
#ifndef RELEASE_CONFIG
		auto& state = g_SharedMutexCheckState[this];
		state.writeCount--;

		if (state.writeCount == 0 && state.readCount == 0)//remove state if fully unlocked to prevent memory leak
			g_SharedMutexCheckState.erase(this);
#endif
		mutex_.unlock();
	}

	void SharedMutexCheck::lock_shared()
	{
#ifndef RELEASE_CONFIG
		auto& state = g_SharedMutexCheckState[this];
		if (state.writeCount > 0 || state.readCount > 0)
		{
			BE_ISSUE("Deadlock detected: trying to acquire write lock while already holding write lock");
			return;
		}
#endif

		// Normal read lock acquisition
		mutex_.lock_shared();
#ifndef RELEASE_CONFIG
		state.readCount = 1;
#endif
	}

	void SharedMutexCheck::unlock_shared()
	{
#ifndef RELEASE_CONFIG
		auto& state = g_SharedMutexCheckState[this];
		state.readCount--;
		if (state.writeCount == 0 && state.readCount == 0) //remove state if fully unlocked to prevent memory leak
			g_SharedMutexCheckState.erase(this);
#endif
		mutex_.unlock_shared();
	}

	bool SharedMutexCheck::try_lock_shared()
	{
#ifndef RELEASE_CONFIG
		auto& state = g_SharedMutexCheckState[this];
		if (state.writeCount > 0 || state.readCount > 0)
		{
			BE_ISSUE("Deadlock detected: trying to acquire write lock while already holding write lock");
			return false;
		}
#endif

		// Try to acquire read lock
		if (mutex_.try_lock_shared())
		{
#ifndef RELEASE_CONFIG
			state.readCount = 1;
#endif
			return true;
		}

		return false;
	}

	bool SharedMutexCheck::try_lock()
	{
#ifndef RELEASE_CONFIG
		auto& state = g_SharedMutexCheckState[this];
		if (state.writeCount > 0 || state.readCount > 0)
		{
			BE_ISSUE("Deadlock detected: trying to acquire write lock while already holding write lock");
			return false;
		}
#endif

		if (mutex_.try_lock())
		{
#ifndef RELEASE_CONFIG
			state.writeCount = 1;
#endif
			return true;
		}

		return false;
	}
}


