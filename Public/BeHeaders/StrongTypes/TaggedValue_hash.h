/*--------------------------------------------------------------------------------------+
|
|     $Source: TaggedValue_hash.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "TaggedValue.h"

#include <boost/container_hash/hash.hpp>

namespace std
{
	template <class VALUE, class UNIT,
		template <typename, typename> class... SKILLS>
	struct hash<::Be::StrongValue<VALUE, UNIT, SKILLS...>>
	{
		using argument_type = ::Be::StrongValue<VALUE, UNIT, SKILLS...>;
		using result_type = std::size_t;

		result_type operator ()(argument_type const& key) const
		{
			return std::hash<VALUE>()(key.getValue());
		}
	};
}

/// For boost collections (or for std collections using a boost::hasher on those types for which std
/// does not defined a standard hasher for, for example std::pair...).
/// See comment for #hash_value(VECFLOAT const&)
/// Must be in namespace Be for ADL to work.

namespace Be
{
	template <class VALUE, class UNIT,
		template <typename, typename> class... SKILLS>
	std::size_t hash_value(Be::StrongValue<VALUE, UNIT, SKILLS...> const& key)
	{
		return boost::hash<VALUE>()(key.value());
	}
}
