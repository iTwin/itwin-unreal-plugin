/*--------------------------------------------------------------------------------------+
|
|     $Source: SharedMutexCheck.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <shared_mutex>
#include <atomic>

// This class is used to check nested lock acquisitions on a shared mutex to prevent deadlocks.
// It is used in debug builds to verify correct usage of shared mutexes.
namespace AdvViz::SDK::Tools
{
	class SharedMutexCheck
	{
	public:
		SharedMutexCheck();
		~SharedMutexCheck();

		// Non-copyable, non-movable
		SharedMutexCheck(const SharedMutexCheck&) = delete;
		SharedMutexCheck& operator=(const SharedMutexCheck&) = delete;
		SharedMutexCheck(SharedMutexCheck&&) = delete;
		SharedMutexCheck& operator=(SharedMutexCheck&&) = delete;

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

	private:

		std::shared_mutex mutex_;
	};

}