/*--------------------------------------------------------------------------------------+
|
|     $Source: RWLock.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <mutex>
#include <shared_mutex>

namespace BeUtils
{
	/// Using std::shared_mutex to implement a reader/writer locking policy is standard. The only purpose of
	/// those utility classes it to make it possible to pass a const reference to the current lock, whether
	/// it's exclusive or shared, typically if a modifier method needs to *access* data before editing it,
	/// and thus needs to call another method which normally supposes to lock the data with a reader.

	class RLock;
	class WLock;

	/// Base class for R/W lock.
	class RWLockBase
	{
		// Making constructor private so that one cannot extend the class with a dummy implementation :-)
	private:
		RWLockBase(){}

		friend class RLock;
		friend class WLock;
	};

	class RLock : public RWLockBase
	{
	public:
		RLock(std::shared_mutex& mutex)
			: rlock_(mutex)
		{}

		void unlock() { rlock_.unlock(); }

	private:
		std::shared_lock<std::shared_mutex> rlock_;
	};

	class WLock : public RWLockBase
	{
	public:
		WLock(std::shared_mutex& mutex)
			: wlock_(mutex)
		{}

		void unlock() { wlock_.unlock(); }
		[[nodiscard]] std::shared_mutex* mutex() const { return wlock_.mutex(); }

	private:
		std::unique_lock<std::shared_mutex> wlock_;
	};

} // namespace BeUtils
