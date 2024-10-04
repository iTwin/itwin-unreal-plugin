/*--------------------------------------------------------------------------------------+
|
|     $Source: LockableObject.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

namespace SDK::Core::Tools
{
	/** Scoped lock of a LockableObject:
	 * @code
	 * LockableObject<Object> object;
	 * {
	 *     AutoLockObject<LockableObject<Object>> objects_withlock(objects);
	 *     //or AutoLockObject<decltype(objects)> objects_withlock(updatedObjs);
	 *     //or auto objects_withlock(updatedObjs.GetAutoLock());
	 *     objects_withlock.Get().DoSomeWork();
	 * }
	 * @endcode
	 */
	template<typename TLockableObject>
	class AutoLockObject
	{
	public:
		AutoLockObject(TLockableObject &lockable) :lockable_(lockable)
		{
			obj_ = &lockable_.Lock();
		}

		AutoLockObject(const AutoLockObject<TLockableObject> &&p)
		: obj_(p.obj_)
		, lockable_(p.lockable_)
		{}

		~AutoLockObject() { lockable_.Unlock(); }

		typename TLockableObject::subtype &Get() { return *obj_; }
		const typename TLockableObject::subtype &Get() const { return *obj_; }

		operator typename TLockableObject::subtype &() { return *obj_; }

		//non-copyable
		AutoLockObject(const AutoLockObject&) = delete;
		AutoLockObject& operator=(const AutoLockObject&) = delete;

	private:
		typename TLockableObject::subtype *obj_;
		TLockableObject &lockable_;
	};

	/** Associates a mutex with an object to protect it with RAII style.
	 * @code
	 * LockableObject<std::vector<MyObject*>> objects(10); // use standard constructor arguments
	 * @endcode
	 */

	template<typename Type, typename Mutex>
	class LockableObject
	{
	public:
		typedef Type subtype;
		typedef LockableObject<Type, Mutex> thisType;
		typedef AutoLockObject<LockableObject<Type, Mutex>> AutoLock;

		LockableObject()
		{}

		template<typename... Args>
		LockableObject(Args&&... args)
		: obj_(std::forward<Args>(args)...)
		{}

		LockableObject(LockableObject&&) = default;
		LockableObject(const LockableObject&) = delete;
		LockableObject& operator=(const LockableObject&) = default;

		const Type &Lock() const
		{
			mutex_.lock(); 
			return obj_;
		}

		Type& Lock()
		{
			mutex_.lock();
			return obj_;
		}

		void Unlock() const
		{ 
			mutex_.unlock();
		}

		const Type* TryLock() const
		{
			bool b = mutex_.try_lock();
			if (b)
				return &obj_;
			return nullptr;
		}

		Type* TryLock()
		{
			bool b = mutex_.try_lock();
			if (b)
				return &obj_;
			return nullptr;
		}

		AutoLock GetAutoLock()
		{
			return AutoLock(*this);
		}

		const AutoLock GetAutoLock() const
		{
			return AutoLock(*(const_cast<thisType*>(this)));
		}

		Mutex& GetMutex() { return mutex_; }

		Type &UnsafeAccess() { return obj_; }

	protected:
		mutable Mutex mutex_;
		Type obj_;
	};
	
	/** Scoped "read" lock of a RWLockableObject.
	 * Use AutoLockObject for "write" lock.
	 */
	template<typename TLockableObject>
	class RAutoLockObject
	{
	public:
		RAutoLockObject(TLockableObject& lockable) :lockable_(lockable)
		{
			obj_ = &lockable_.LockShared();
		}

		RAutoLockObject(const RAutoLockObject<TLockableObject>&& p)
			: obj_(p.obj_)
			, lockable_(p.lockable_)
		{}

		~RAutoLockObject() { lockable_.UnlockShared(); }
		const typename TLockableObject::subtype& Get() const { return *obj_; }
		operator const typename TLockableObject::subtype& () { return *obj_; }

		//non-copyable
		RAutoLockObject(const RAutoLockObject&) = delete;
		RAutoLockObject& operator=(const RAutoLockObject&) = delete;

	private:
		typename TLockableObject::subtype* obj_;
		TLockableObject& lockable_;
	};

	/** Can be used with RW lock access,
	 * requires a mutex that has lock_shared method.
	 */
	template<typename Type, typename SharedMutex>
	class RWLockableObject : public LockableObject<Type, SharedMutex>
	{
	public:
		typedef Type subtype;
		typedef RWLockableObject<Type, SharedMutex> thisType;
		typedef LockableObject<Type, SharedMutex> ParentType;
		typedef RAutoLockObject<RWLockableObject<Type, SharedMutex>> RAutoLock;

		template<typename... Args>
		RWLockableObject(Args&&... args)
		: LockableObject<Type, SharedMutex>(std::forward<Args>(args)...)
		{}

		RWLockableObject(RWLockableObject&&) = default;
		RWLockableObject(const RWLockableObject&) = delete;
		RWLockableObject& operator=(const RWLockableObject&) = default;

		Type& LockShared()
		{
			ParentType::mutex_.lock_shared();
			return ParentType::obj_;
		}

		void UnlockShared()
		{
			ParentType::mutex_.unlock_shared();
		}

		const Type* TryLockShared() const
		{
			bool b = ParentType::mutex_.try_lock_shared();
			if (b)
				return &ParentType::obj_;
			return nullptr;
		}

		Type* TryLockShared()
		{
			bool b = ParentType::mutex_.try_lock_shared();
			if (b)
				return &ParentType::obj_;
			return nullptr;
		}

		RAutoLock GetRAutoLock()
		{
			return RAutoLock(*this);
		}

		const RAutoLock GetRAutoLock() const
		{
			return RAutoLock(*(const_cast<thisType*>(this)));
		}
	};
}


