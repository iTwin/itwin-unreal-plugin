/*--------------------------------------------------------------------------------------+
|
|     $Source: TaggedValueFW.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

namespace Be
{
	template <typename VAL, typename STRONGTYPE> struct Testable;
	template <typename VAL, typename STRONGTYPE> struct PhysicalOps;
	template <typename VAL, typename STRONGTYPE> struct LogicOperations;

	template <class VALUE, class UNIT, template <typename, typename> class... Skills>
	class StrongValue;

	namespace StrongTypes
	{
		template <class VALUE, class UNIT>
		using Basic = StrongValue<VALUE, UNIT, Testable>;

		template <class VALUE, class UNIT>
		using Physical = StrongValue<VALUE, UNIT, Testable, PhysicalOps>;

		template <class VALUE, class UNIT>
		using Boolean = StrongValue<VALUE, UNIT, Testable, LogicOperations>;
	}
}

#define DEFINE_STRONG_INT(WHAT)\
	using WHAT = ::Be::StrongTypes::Physical<int, struct WHAT##Tag>;

#define DEFINE_STRONG_UINT32(WHAT)\
	using WHAT = ::Be::StrongTypes::Physical<uint32_t, struct WHAT##Tag>;

#define DEFINE_STRONG_UINT64(WHAT)\
	using WHAT = ::Be::StrongTypes::Physical<uint64_t, struct WHAT##Tag>;

#define DEFINE_STRONG_DBL(WHAT)\
	using WHAT = ::Be::StrongTypes::Physical<double, struct WHAT##Tag>;

#define DEFINE_STRONG_BOOL(WHAT)\
	using WHAT = ::Be::StrongTypes::Boolean<bool, struct WHAT##Tag>
