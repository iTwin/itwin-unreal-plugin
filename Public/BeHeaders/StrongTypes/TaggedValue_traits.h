/*--------------------------------------------------------------------------------------+
|
|     $Source: TaggedValue_traits.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "TaggedValue.h"

namespace Be
{
	/** Allows to define types / functions that have to work both
	 * on StrongValue and other types (int, float, ...)
	 */
	template<class T>
	struct StrongValueTrait
	{
		// Should be the type used to make T
		using InnerType = T;

		// Should be the first inner type that is not StrongValue itself
		using ValueType = T;

		static T const& GetWeak(T const& o) { return o; }
	};

	template<class VALUE, class UNIT, template <typename, typename> class... SKILLS>
	struct StrongValueTrait<Be::StrongValue<VALUE, UNIT, SKILLS...>>
	{
		using InnerType = VALUE;

		// Note this definition is recursive until we find a suitable type
		using ValueType = typename StrongValueTrait<VALUE>::ValueType;

		static ValueType GetWeak(Be::StrongValue<VALUE, UNIT, SKILLS...> const& o)
		{ return StrongValueTrait<VALUE>::GetWeak(o.value()); }
	};

	/** Shortcut as function.
	 * Returns the "weak" value of anything (the raw type)
	 * whether it is a strong type or not.
	 */
	template<class T>
	typename StrongValueTrait<T>::ValueType WeakValue(T const& k)
	{
		return StrongValueTrait<T>::GetWeak(k);
	};

	/** Shortcut to cast as a specific type.
	 * x = WeakValueAs<int>(k)
	 */
	template<class R, class T>
	R WeakValueAs(T const& k)
	{
		return static_cast<R>(StrongValueTrait<T>::GetWeak(k));
	};
}


