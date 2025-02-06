/*--------------------------------------------------------------------------------------+
|
|     $Source: TypeOperations.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "TaggedValue_traits.h"
#include <BeHeaders/Compil/Attributes.h>

namespace Be
{
	/** This trait allows to define type relations,
	 * with respect to multiplication or division:
	 *
	 * For instance:
	 *
	 * DEFINE_STRONG_DBL(Volt)
	 * DEFINE_STRONG_DBL(Ampere)
	 * DEFINE_STRONG_DBL(Watt)
	 *
	 * template<>
	 * struct MultiplyTrait<Volt,Ampere>
	 * {
	 *		using MultType = Watt;
	 * };
	 *
	 * Volt u(12);
	 * Ampere i(10);
	 * Watt p = u * i:
	 *
	 * Note 1: the types you use are expected to have the PhysicalOps 'skill'.
	 * Otherwise there is not much point in using only this.
	 * Mult and div by a scalar are already handled by PhysicalOps.
	 *
	 * Note 2: This does not prevent you from defining wrong unit relations,
	 * (we'd need to count base units, like in boost unit)
	 */
	template<typename T, typename U, class Enable = void>
	struct MultiplyTrait
	{
		// Just define what you need, no need for both
		// using MultType = W;
		// using DivType = K;
	};

	// Basic arithmetic types are dimensionless, type stays the name
	template<typename T>
	struct MultiplyTrait<T, T,
		typename std::enable_if<std::is_arithmetic<T>::value>::type>
	{
		using MultType = T;
		using DivType = T;
	};
}

namespace Be::StrongTypes
{
	// Allows TaggedValue multiplication using MultiplyTrait

	template<
		class VALUE1, class UNIT1, template <typename, typename> class... SKILLS1,
		class VALUE2, class UNIT2, template <typename, typename> class... SKILLS2>
	[[nodiscard]] inline typename Be::MultiplyTrait<
		StrongValue<VALUE1, UNIT1, SKILLS1...>,
		StrongValue<VALUE2, UNIT2, SKILLS2...>>::MultType
	operator*(
		StrongValue<VALUE1, UNIT1, SKILLS1...> const& a,
		StrongValue<VALUE2, UNIT2, SKILLS2...> const& b)
	{
		return typename Be::MultiplyTrait<
			StrongValue<VALUE1, UNIT1, SKILLS1...>,
			StrongValue<VALUE2, UNIT2, SKILLS2...>>::MultType(
				a.value() * b.value());
	}

	// Allows TaggedValue division using MultiplyTrait

	template<
		class VALUE1, class UNIT1, template <typename, typename> class... SKILLS1,
		class VALUE2, class UNIT2, template <typename, typename> class... SKILLS2>
	[[nodiscard]] inline typename Be::MultiplyTrait<
	StrongValue<VALUE1, UNIT1, SKILLS1...>,
		StrongValue<VALUE2, UNIT2, SKILLS2...>>::DivType
	operator/(
		StrongValue<VALUE1, UNIT1, SKILLS1...> const& a,
		StrongValue<VALUE2, UNIT2, SKILLS2...> const& b)
	{
		return typename Be::MultiplyTrait<
			StrongValue<VALUE1, UNIT1, SKILLS1...>,
			StrongValue<VALUE2, UNIT2, SKILLS2...>>::DivType(
				a.value() / b.value());
	}
}

