/*--------------------------------------------------------------------------------------+
|
|     $Source: CleanUpGuard.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "Break.h"
#include "Attributes.h"
#include <functional>
#include <type_traits>

namespace Be
{
	/** Allows to execute some action at scope exit
	 * unless the guard is released or cleaned before that.
	 *
	 * @code
	 {
		 CleanUpGuard guard( CleanUpAction() );
		 ... risky action, if this throws, CleanUpAction is done ...
		 guard.release();
		 ... but not if we reach this point ...
	 }
	 * @endcode
	 *
	 * You can get a result of cleanup if any,
	 * by using cleanup() before it runs automatically at end of scope !
	 * This can be useful for closing files for exemple.
	 */
	template< class T>
	class [[nodiscard]] CleanUpGuardT
	{
	private :
		typedef std::function<T (void)> Func;
		Func x ;

	public :

		CleanUpGuardT(Func x)
		: x( x )
		{ BE_ASSERT_MSG(x, "you must pass a payload"); }

		/// See https://stackoverflow.com/questions/3106110/what-are-move-semantics
		/// Implementing the assignment like that avoids reimplementing both ctor
		/// and dtor semantics in the assignment operator: since the parameter is
		/// a copy (either a full copy or a moved copy, depending on what it was
		/// assigned from), we just have to swap members and don't need to bother
		/// about anything else.
		CleanUpGuardT(CleanUpGuardT<T>&& From) { *this = From; }
		CleanUpGuardT<T>& operator=(CleanUpGuardT<T> From)
		{
			std::swap(x, From.x);
			From.release();
		}

		CleanUpGuardT(CleanUpGuardT<T> const&) = delete;
		CleanUpGuardT<T>& operator=(CleanUpGuardT<T> const&) = delete;
		CleanUpGuardT<T>& operator=(CleanUpGuardT<T>&&) = delete;

		~CleanUpGuardT()
		{
			if (x)
				AutoCleanup();
		}

		/** Call this to disable the cleanup
		 * the payload won't be run.
		 */
		void release()
		{
			x = Func();
		}

		/** Call this for early cleanup,
		 * that allows to get the payload return.
		 */
		T cleanup()
		{
			BE_ASSERT_MSG(x, "No x, cleanup already called ?");
			Func localF = x;
			x = Func();
			return localF();
		}

	private:
		void AutoCleanup()
		{
			//+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
			// this does not compile without /EHsc, and I don't think it's a good idea
			// to enforce the option in Unreal...
			// -> error C4530: C++ exception handler used, but unwind semantics are not enabled. Specify /EHsc
			//try
			//{
			//	(void)x();
			//}
			//catch(...)
			//{
			//	//BE_ISSUE(Be::diagnostic());
			//}
			//+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
			(void)x();
		}
	};

	/** The most common case is to return void */
	typedef CleanUpGuardT<void> CleanUpGuard;
}
