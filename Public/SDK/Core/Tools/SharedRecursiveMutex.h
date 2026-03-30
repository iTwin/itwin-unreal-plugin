/*--------------------------------------------------------------------------------------+
|
|     $Source: SharedRecursiveMutex.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <shared_mutex>
#include <atomic>

namespace AdvViz::SDK::Tools
{
	// This class implements a shared recursive mutex supporting both shared (read) and exclusive (write) locks.
	// It is intended to be used in scoped based locking patterns (e.g., std::lock_guard, std::shared_lock).
	// Should not be used to lock in one thread and unlock in another thread.
	// Use this with care. Even we have unit test passed, it's not guaranti to be safe in all usage scenarii.

	class SharedRecursiveMutex
	{
	public:
		SharedRecursiveMutex();
		~SharedRecursiveMutex();

		// Non-copyable, non-movable
		SharedRecursiveMutex(const SharedRecursiveMutex&) = delete;
		SharedRecursiveMutex& operator=(const SharedRecursiveMutex&) = delete;
		SharedRecursiveMutex(SharedRecursiveMutex&&) = delete;
		SharedRecursiveMutex& operator=(SharedRecursiveMutex&&) = delete;

		/// Acquire exclusive (write) lock
		/// Supports recursion and promotion from read lock
		void lock();

		/// Try to acquire exclusive (write) lock without blocking
		bool try_lock();

		/// Release exclusive (write) lock
		void unlock();

		/// Acquire shared (read) lock
		/// Supports recursion
		void lock_shared();

		/// Try to acquire shared (read) lock without blocking
		bool try_lock_shared();

		/// Release shared (read) lock
		void unlock_shared();

#ifdef RELEASE_CONFIG
	private:
		void AssertState(int, const char*) const noexcept {}
		void SetState(int) const noexcept {}

		std::shared_mutex mutex_;
#else
		/// Get current state of the mutex, for unit test only!!!
		/// Returns: 0 = unlocked, 1 = write locked, 2 = read locked
		/// Returns -1 if state tracking is disabled
		/// Is only valid when called from the thread that owns the mutex
		int GetState() const
		{
			if (stateTrackingEnabled_)
				return state_.load();
			return -1;
		}

		/// Enable state tracking for testing purposes
		/// Call this at the start of your tests, before creating any mutex instances
		static void EnableStateTracking(bool enable = true)
		{
			stateTrackingEnabled_ = enable;
		}

	private:
		void AssertState(int expectedState, const char* message) const;

		void SetState(int newState) noexcept
		{
			if (stateTrackingEnabled_)
				state_.store(newState);
		}

		std::shared_mutex mutex_;
		mutable std::atomic<int> state_; // 0: not locked, 1: write locked, 2: read locked

		static bool stateTrackingEnabled_;
#endif
	};
}