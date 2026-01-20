/*--------------------------------------------------------------------------------------+
|
|     $Source: Skills.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <type_traits>

/* Templates that can be used to give strong types specific operations sets */

namespace Be
{
	/** Basic operations, most likely needed for parameter/config types.
	 * T is the StrongType, VAL is its value type.
	 */
	template <typename VAL, typename T>
	struct Testable
	{
		typedef VAL ValueType;
		typedef T Type;
		T& FullType() { return static_cast<Type&>(*this); }
		T const& FullType() const { return static_cast<Type const&>(*this); }

		/// Note: marking it explicit does NOT mean that you will have to cast to use it inside an
		/// 'if' condition, only that the operator will not participate in expressions where you
		/// don't want them to, like comparisons.
		explicit operator bool () const { return Testable::FullType().value() != VAL(0); }
		bool operator<(T const& o) const { return Testable::FullType().value() < o.value(); }
		bool operator>(T const& o) const { return Testable::FullType().value() > o.value(); }
		bool operator<=(T const& o) const { return Testable::FullType().value() <= o.value(); }
		bool operator>=(T const& o) const { return Testable::FullType().value() >= o.value(); }
	};

	template <typename VAL, typename T>
	struct LogicOperations
	{
		typedef T Type; // the strong type
		typedef VAL ValueType; // the underlying type

		T& FullType() { return static_cast<Type&>(*this); }
		T const& FullType() const { return static_cast<Type const&>(*this); }

		bool operator|=(T const& o)
		{ return LogicOperations::FullType().ref() |= o.value(); }

		bool operator&=(T const& o)
		{ return LogicOperations::FullType().ref() &= o.value(); }
		
		T operator|(T const& o) const
		{ return T(LogicOperations::FullType().value() | o.value()); }
		
		T operator&(T const& o) const
		{ return T(LogicOperations::FullType().value() & o.value()); }
	};

	/** Physics-like operations, to avoid non homogeneous operations.
	 * So don't add operation that are not allowed (like T + float)
	 * or that needed different types as result (T * T).
	 */
	template <typename VAL, typename T>
	struct PhysicalOps
	{
		typedef VAL ValueType;
		typedef T Type;
		T& FullType() { return static_cast<Type&>(*this); }
		T const& FullType() const { return static_cast<Type const&>(*this); }

		void operator +=(T const& o)
		{
			PhysicalOps::FullType().ref() += o.PhysicalOps::FullType().value();
		}

		void operator -=(T const& o)
		{
			PhysicalOps::FullType().ref() -= o.PhysicalOps::FullType().value();
		}

		// multiplication by dimensionless number okay
		void operator *=(float        o) { PhysicalOps::FullType().ref() *= o; }
		void operator *=(double       o) { PhysicalOps::FullType().ref() *= o; }
		void operator *=(int          o) { PhysicalOps::FullType().ref() *= o; }
		void operator *=(unsigned int o) { PhysicalOps::FullType().ref() *= o; }

		/// division by dimensionless number okay
		void operator /=(float        o) { PhysicalOps::FullType().ref() /= o; }
		void operator /=(double       o) { PhysicalOps::FullType().ref() /= o; }
		void operator /=(int          o) { PhysicalOps::FullType().ref() /= o; }
		void operator /=(unsigned int o) { PhysicalOps::FullType().ref() /= o; }

		/// opposite okay, same unit
		T operator -() const
		{
			return T( -PhysicalOps::FullType().value() );
		}

		/// ++x okay, it adds 1 of the same unit
		T & operator++()
		{
			++PhysicalOps::FullType().ref();
			return PhysicalOps::FullType();
		}

		/// x++ okay, it adds 1 of the same unit
		T operator++(int)
		{
			T tmp(PhysicalOps::FullType());
			operator++();
			return tmp;
		}

		/// --x okay, it subtracts 1 of the same unit
		T & operator--()
		{
			--PhysicalOps::FullType().ref();
			return PhysicalOps::FullType();
		}

		/// x-- okay, it subtracts 1 of the same unit
		T operator--(int)
		{
			T tmp(PhysicalOps::FullType());
			operator--();
			return tmp;
		}

		// UNIT + UNIT
	    T operator+(T const& x) const
	    { return T(PhysicalOps::FullType().value() + x.value());   }

		// UNIT - UNIT
		T operator-(T const& x) const
		{ return T(PhysicalOps::FullType().value() - x.value()); }

		// UNIT * scalar
		// note there is a global operator for the left mult : scalar * UNIT
		template<class U, typename = std::enable_if_t<std::is_arithmetic<U>::value>>
		T operator * (U const& x) const
		{ return T(PhysicalOps::FullType().value() * x); }

		// UNIT / scalar
		template<class U, typename = std::enable_if_t<std::is_arithmetic<U>::value>>
		T operator / (U const& x) const
		{ return T(PhysicalOps::FullType().value() / x); }

		// UNIT / UNIT returns a dimensionless value
		VAL operator/(T const& x) const
		{ return PhysicalOps::FullType().value() / x.value(); }

		/** Modulo operator with int yield the same unit */
		template<class U, typename = std::enable_if_t<std::is_integral<U>::value>>
		T operator%(U const& x) const
		{ return T(PhysicalOps::FullType().value() % x); }

		T operator%(T const& x) const
		{ return T(PhysicalOps::FullType().value() % x.value()); }
	};
}
