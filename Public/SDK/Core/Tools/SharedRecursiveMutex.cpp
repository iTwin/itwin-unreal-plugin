/*--------------------------------------------------------------------------------------+
|
|     $Source: SharedRecursiveMutex.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "SharedRecursiveMutex.h"
#include <unordered_map>
#include "Assert.h"
#include <memory_resource>
#include <array>

namespace AdvViz::SDK::Tools
{
	struct LockCounter
	{
		int readCount = 0;
		int writeCount = 0;
	};

	// use local memory resource to avoid heap allocations on each thread
	static const size_t ReservedMemory = 1024;
	thread_local std::array<std::byte, ReservedMemory> buf1; 
	thread_local std::pmr::monotonic_buffer_resource pool1{ buf1.data(), buf1.size() };
	// members can't be thread_local, so we use a thread_local map instead to keep per-thread state
	thread_local std::pmr::unordered_map<SharedRecursiveMutex*, LockCounter> g_SharedRecursiveMutexState{ &pool1 };

#ifndef RELEASE_CONFIG
	bool SharedRecursiveMutex::stateTrackingEnabled_ = false;
#endif

	SharedRecursiveMutex::SharedRecursiveMutex()
	{
		SetState(0);
	}

	SharedRecursiveMutex::~SharedRecursiveMutex()
	{
	}

#ifndef RELEASE_CONFIG
	void SharedRecursiveMutex::AssertState(int expectedState, const char* message) const
	{
		if (stateTrackingEnabled_)
		{
			BE_ASSERT(state_.load() == expectedState && message);
		}
	}
#endif

	void SharedRecursiveMutex::lock()
	{
		auto& state = g_SharedRecursiveMutexState[this];

		if (state.writeCount > 0)
		{
			// Already have write lock - just increment recursion count
			AssertState(1, "State should be write-locked when writeCount > 0");
			state.writeCount++;
			return;
		}

		if (state.readCount > 0)
		{
			// Need to promote from read to write lock
			AssertState(2, "State should be read-locked when readCount > 0");

			// Save the read count - we'll restore it when write lock is released
			// but DON'T clear readCount here, we need to remember we had a read lock

			// Release the ONE actual read lock
			mutex_.unlock_shared();
			SetState(0);

			// Acquire write lock
			mutex_.lock();
			SetState(1);
			state.writeCount = 1;

			// Note: readCount is preserved so when we unlock the write lock,
			// we can restore the read lock
			return;
		}

		// Normal write lock acquisition
		AssertState(0, "State should be unlocked when no locks are held");
		mutex_.lock();
		SetState(1);
		state.writeCount = 1;
	}

	void SharedRecursiveMutex::unlock()
	{
		auto& state = g_SharedRecursiveMutexState[this];

		if (state.writeCount == 0)
		{
			// Logic error - trying to unlock when we don't have write lock
			BE_ISSUE("unlock() called without holding write lock");
			return;
		}

		state.writeCount--;

		if (state.writeCount == 0)
		{
			// Release the actual write lock
			mutex_.unlock();

			if (state.readCount > 0)
			{
				// Restore the read lock that existed before promotion
				mutex_.lock_shared();
				SetState(2);
			}
			else
			{
				SetState(0);
				g_SharedRecursiveMutexState.erase(this); // writeCount == 0 and readCount == 0, clean up state
			}
		}
		BE_ASSERT(state.writeCount >= 0);
	}

	void SharedRecursiveMutex::lock_shared()
	{
		auto& state = g_SharedRecursiveMutexState[this];

		if (state.writeCount > 0)
		{
			// Already have write lock - write access includes read access
			// Just increment read count for proper unlock_shared() calls
			state.readCount++;
			return;
		}

		if (state.readCount > 0)
		{
			// Already have read lock - just increment recursion count
			state.readCount++;
			return;
		}

		// Normal read lock acquisition
		mutex_.lock_shared();
		state.readCount = 1;
		SetState(2);
	}

	void SharedRecursiveMutex::unlock_shared()
	{
		auto& state = g_SharedRecursiveMutexState[this];

		if (state.readCount == 0)
		{
			// Logic error - trying to unlock when we don't have read lock
			BE_ISSUE("unlock_shared() called without holding read lock");
			return;
		}

		state.readCount--;

		if (state.readCount == 0 && state.writeCount == 0)
		{
			// Release the actual read lock only if we don't have write lock
			mutex_.unlock_shared();
			SetState(0);
			// Clean up state if no locks are held
			g_SharedRecursiveMutexState.erase(this);
		}
		// Note: If we have a write lock (writeCount > 0), we don't actually 
		// unlock the underlying mutex because write access includes read access
	}

	bool SharedRecursiveMutex::try_lock_shared()
	{
		auto& state = g_SharedRecursiveMutexState[this];

		if (state.writeCount > 0)
		{
			// Already have write lock - write access includes read access
			state.readCount++;
			return true;
		}

		if (state.readCount > 0)
		{
			// Already have read lock - just increment recursion count
			state.readCount++;
			return true;
		}

		// Try to acquire read lock
		if (mutex_.try_lock_shared())
		{
			SetState(2);
			state.readCount = 1;
			return true;
		}

		return false;
	}

	bool SharedRecursiveMutex::try_lock()
	{
		auto& state = g_SharedRecursiveMutexState[this];

		if (state.writeCount > 0)
		{
			// Already have write lock - just increment recursion count
			AssertState(1, "State should be write-locked when writeCount > 0");
			state.writeCount++;
			return true;
		}

		if (state.readCount > 0)
		{
			// Need to promote from read to write lock
			AssertState(2, "State should be read-locked when readCount > 0");

			// Release the ONE actual read lock
			mutex_.unlock_shared();
			SetState(0);

			// Try to acquire write lock
			if (mutex_.try_lock())
			{
				SetState(1);
				state.writeCount = 1;
				// Keep readCount so it can be restored when write lock is released
				return true;
			}
			else
			{
				// Failed to acquire write lock - restore the ONE read lock
				mutex_.lock_shared();
				SetState(2);
				return false;
			}
		}

		// Try normal write lock acquisition
		AssertState(0, "State should be unlocked when no locks are held");
		if (mutex_.try_lock())
		{
			SetState(1);
			state.writeCount = 1;
			AssertState(1, "State should be write-locked after successful try_lock");
			return true;
		}

		AssertState(0, "State should remain unlocked after failed try_lock");
		return false;
	}

}