/*--------------------------------------------------------------------------------------+
|
|     $Source: TaggedValue_operations.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <algorithm>
#include <cmath> // we need std::abs(float)
#include "TaggedValue.h"

namespace std
{
	template< class V, class U >
	Be::StrongTypes::Physical<V,U> max(
		Be::StrongTypes::Physical<V,U> const& a,
		Be::StrongTypes::Physical<V,U> const& b)
	{
		return Be::StrongTypes::Physical<V,U>(std::max(a.value(), b.value()));
	}

	template< class V, class U >
	Be::StrongTypes::Physical<V,U> min(
		Be::StrongTypes::Physical<V,U> const& a,
		Be::StrongTypes::Physical<V,U> const& b)
	{
		return Be::StrongTypes::Physical<V,U>(std::min(a.value(), b.value()));
	}
}

template< class V, class U >
Be::StrongTypes::Physical<V,U> abs(Be::StrongTypes::Physical<V,U> a)
{
	return Be::StrongTypes::Physical<V,U>(std::abs(a.getValue()));
}

template< class V, class U >
Be::StrongTypes::Physical<V,U> floor(Be::StrongTypes::Physical<V,U> a)
{
	return Be::StrongTypes::Physical<V,U>(std::floor(a.getValue()));
}

template< class V, class U >
Be::StrongTypes::Physical<V,U> ceil(Be::StrongTypes::Physical<V,U> a)
{
	return Be::StrongTypes::Physical<V,U>(std::ceil(a.getValue()));
}

template< class V, class U >
Be::StrongTypes::Physical<V,U> fmod(
	Be::StrongTypes::Physical<V,U> a,
	Be::StrongTypes::Physical<V,U> b)
{
	return Be::StrongTypes::Physical<V,U>(fmod(a.getValue(), b.getValue()));
}
