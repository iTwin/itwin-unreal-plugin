/*--------------------------------------------------------------------------------------+
|
|     $Source: TaggedValue.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "TaggedValueFW.h"
#include "Skills.h"

#include <BeHeaders/Compil/EmptyBases.h>
#include <BeHeaders/Compil/Attributes.h>

#include <cstdint>
#include <limits>
#include <type_traits> // for std::is_same
#include <utility> // for std::move

namespace Be
{
	namespace StrongTypes
	{
		/// Convert from Src to Dst
		template <class Dst, class Src>
		struct Convert
		{
			// static Dst convert(typename Src::ValueType val);
		};

		// same type conversion
		template <class A>
		struct Convert<A, A>
		{
			static A convert(typename A::ValueType a) { return A(a); }
		};

		/** Helper to call the class template */
		template <class Dst, class Src>
		Dst convert(Src value)
		{
			// Pass raw value to converter to avoid forgetting doing it in the convert impl, leading
			// to a runtime stack overflow
			return Convert<Dst,Src>::convert(value.getValue());
		}

		template <class A>
		struct Init
		{
			static A value() { return A(); }
		};

		template <> struct Init<float> { static float value() { return 0.f; } };
		template <> struct Init<double> { static double value() { return 0.; } };
		template <> struct Init<int8_t> { static int8_t value() { return 0; } };
		template <> struct Init<int16_t> { static int16_t value() { return 0; } };
		template <> struct Init<int32_t> { static int32_t value() { return 0; } };
		template <> struct Init<int64_t> { static int64_t value() { return 0; } };
		template <> struct Init<uint8_t> { static uint8_t value() { return 0; } };
		template <> struct Init<uint16_t> { static uint16_t value() { return 0; } };
		template <> struct Init<uint32_t> { static uint32_t value() { return 0; } };
		template <> struct Init<uint64_t> { static uint64_t value() { return 0; } };
	} // ns StrongTypes

	/** This template allows to associate constants to
	 * TaggedValue. Just specialize it for the types.
	 * See Angles for instance.
	 */
	template <class T>
	struct ConstantsT
	{};

	/** StrongValue provides strongly typed values, with physics-like
	 * semantics to avoid non homogeneous operations.
	 *
	 * Note the external operation are in TaggedValue_operations.h
	 * Include it only in the files which really need it.
	 *
	 * Skills are groups of methods that can be added as desired to
	 * the type. See TaggedValueFW for the default ones.
	 */

	template <
		class VALUE, class UNIT,
		template <typename, typename> class... SKILLS>
	class BE_EMPTY_BASES StrongValue :
		public SKILLS<VALUE, StrongValue<VALUE, UNIT, SKILLS...>>...
	{
	public :
		typedef VALUE ValueType;
		typedef UNIT  UnitType;

		typedef ConstantsT<StrongValue> Constants;

		constexpr explicit StrongValue(ValueType v)
		: value_(v)
		{}

		constexpr StrongValue()
		: value_(StrongTypes::Init<ValueType>::value())
		{}

		constexpr StrongValue(StrongValue const& other)
		: value_(other.value_)
		{}

		constexpr StrongValue(StrongValue && other)
		: value_(std::move(other.value_))
		{}

		/// we use explicit constructor to force you to notice conversions !
		template <
			class V, class U,
			template <typename, typename> class... S>
		constexpr explicit StrongValue(StrongValue<V, U, S...> other)
		: value_(StrongTypes::convert<StrongValue>(other).value_)
		{}

		void operator=(StrongValue const& other)
		{
			value_ = other.value_;
		}

		void operator=(StrongValue && other)
		{
			value_ = std::move(other.value_);
		}

		template<class T>
		StrongValue<T, UNIT, SKILLS...> CastTo() const
		{
			return StrongValue<T, UNIT, SKILLS...>(static_cast<T>(value_));
		}

		[[nodiscard]] constexpr ValueType const& value() const { return value_; }
		[[nodiscard]] constexpr ValueType const& getValue() const { return value(); }

		// for direct write access
		[[nodiscard]] ValueType & ref() { return value_; }

		[[nodiscard]] bool operator==(StrongValue const& o) const
		{ return value() == o.value(); }

		[[nodiscard]] bool operator!=(StrongValue const& o) const
		{ return value() != o.value(); }

	private:
		ValueType value_;
	};

	template <
		class ARITHMETIC, class VALUE, class UNIT,
		template <typename, typename> class... SKILLS>
	StrongValue<VALUE,UNIT,SKILLS...> operator * (
		ARITHMETIC const& b,
		StrongValue<VALUE,UNIT,SKILLS...> const& a)
	{
		// assuming b * a is commutative
		return a * b;
	}

	// CONVERSIONS

	namespace StrongTypes
	{
		// same unit conversion
		template <
			class AV, class BV, class UNIT,
			template <typename, typename> class... SKILLS>
		struct Convert<StrongValue<AV, UNIT, SKILLS...>, StrongValue<BV, UNIT, SKILLS...>>
		{
			static StrongValue<AV, UNIT> convert(StrongValue<BV, UNIT> a)
			{
				return StrongValue<AV, UNIT>(a.getValue());
			}
		};
	}
} // ns Be

namespace std
{
	/// Need to specialize this, otherwise std::numeric_limits<SomeStrongType>::max() silently
	/// returns the default implementation, ie (for MSVC) "return _Ty();" ie 0!
	template <
		class VALUE, class UNIT,
		template <typename, typename> class... SKILLS>
	class numeric_limits<Be::StrongValue<VALUE, UNIT, SKILLS...>>
	{
		using Type = Be::StrongValue<VALUE, UNIT, SKILLS...>;
		using Fwd = numeric_limits<VALUE>;

	public:

		[[nodiscard]] static constexpr Type min() noexcept { return Type(Fwd::min()); }
		[[nodiscard]] static constexpr Type max() noexcept { return Type(Fwd::max()); }
		[[nodiscard]] static constexpr Type lowest() noexcept { return Type(Fwd::lowest()); }
		[[nodiscard]] static constexpr Type epsilon() noexcept { return Type(Fwd::epsilon()); }
		[[nodiscard]] static constexpr Type round_error() noexcept { return Type(Fwd::round_error()); }
		[[nodiscard]] static constexpr Type denorm_min() noexcept { return Type(Fwd::denorm_min()); }
		[[nodiscard]] static constexpr Type infinity() noexcept { return Type(Fwd::infinity()); }
		[[nodiscard]] static constexpr Type quiet_NaN() noexcept { return Type(Fwd::quiet_NaN()); }
		[[nodiscard]] static constexpr Type signaling_NaN() noexcept { return Type(Fwd::signaling_NaN()); }

		static constexpr float_denorm_style has_denorm = Fwd::has_denorm;
		static constexpr bool has_denorm_loss = Fwd::has_denorm_loss;
		static constexpr bool has_infinity = Fwd::has_infinity;
		static constexpr bool has_quiet_NaN = Fwd::has_quiet_NaN;
		static constexpr bool has_signaling_NaN = Fwd::has_signaling_NaN;
		static constexpr bool is_bounded = Fwd::is_bounded;
		static constexpr bool is_exact = Fwd::is_exact;
		static constexpr bool is_iec559 = Fwd::is_iec559;
		static constexpr bool is_integer = Fwd::is_integer;
		static constexpr bool is_modulo = Fwd::is_modulo;
		static constexpr bool is_signed = Fwd::is_signed;
		static constexpr bool is_specialized = Fwd::is_specialized;
		static constexpr bool tinyness_before = Fwd::tinyness_before;
		static constexpr bool traps = Fwd::traps;
		static constexpr float_round_style round_style = Fwd::round_style;
		static constexpr int digits = Fwd::digits;
		static constexpr int digits10 = Fwd::digits10;
		static constexpr int max_digits10 = Fwd::max_digits10;
		static constexpr int max_exponent = Fwd::max_exponent;
		static constexpr int max_exponent10 = Fwd::max_exponent10;
		static constexpr int min_exponent = Fwd::min_exponent;
		static constexpr int min_exponent10 = Fwd::min_exponent10;
		static constexpr int radix = Fwd::radix;
	};
}
