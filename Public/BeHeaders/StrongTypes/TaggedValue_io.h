/*--------------------------------------------------------------------------------------+
|
|     $Source: TaggedValue_io.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "TaggedValue.h"
#include <iosfwd>

namespace Be
{
	template <
		class CHARTYPE, class TRAIT,
		class VALUE, class UNIT,
		template <typename, typename> class... Skills>
	std::basic_ostream<CHARTYPE, TRAIT> & operator<<(
		std::basic_ostream<CHARTYPE, TRAIT> & os,
		StrongValue<VALUE,UNIT,Skills...> const& v)
	{
		os << v.value();
		return os ;
	}
}
