/*--------------------------------------------------------------------------------------+
|
|     $Source: AutoOperators.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

// from vue.git/Vue/Source/eonLib/eonAutoOperators.h

#pragma once

#include "Attributes.h"

#include <type_traits>

template <class T>
using ConstMaybeRef = std::conditional_t<(sizeof(T) > sizeof(void*)), T const&, T const>;

#if defined(__cpp_impl_three_way_comparison) && __cpp_impl_three_way_comparison >= 201907L
	#define	BE_AUTOOPERATORS_THREEWAY_SPACESHIP(typename, cmp)	BE_MAYBE_UNUSED friend inline auto operator<=>(const typename& first, const typename& second)	{ return (cmp); }
	#define	BE_AUTOOPERATORS_THREEWAY_WITHOTHERTYPE_SPACESHIP(typename1, typename2, cmp)	BE_MAYBE_UNUSED friend inline auto operator<=>(const typename1 first, ConstMaybeRef<typename2> second)	{ return (cmp); }
#else
	#define	BE_AUTOOPERATORS_THREEWAY_SPACESHIP(typename, cmp)
	#define	BE_AUTOOPERATORS_THREEWAY_WITHOTHERTYPE_SPACESHIP(typename1, typename2, cmp)
#endif

#define	BE_AUTOOPERATORS_EQ_NOTEQ(typename, cmp)\
	BE_MAYBE_UNUSED friend inline bool	operator==(const typename& first, const typename& second)	{ return (cmp); }		\
	BE_MAYBE_UNUSED friend inline bool	operator!=(const typename& first, const typename& second)	{ return !(cmp); }

#define	BE_AUTOOPERATORS_THREEWAY(typename, cmp)	BE_AUTOOPERATORS_THREEWAY_SPACESHIP(typename, cmp)		\
	BE_MAYBE_UNUSED friend inline bool	operator< (const typename& first, const typename& second)	{ return (cmp) <  0; }	\
	BE_MAYBE_UNUSED friend inline bool	operator> (const typename& first, const typename& second)	{ return (cmp) >  0; }	\
	BE_MAYBE_UNUSED friend inline bool	operator<=(const typename& first, const typename& second)	{ return (cmp) <= 0; }	\
	BE_MAYBE_UNUSED friend inline bool	operator>=(const typename& first, const typename& second)	{ return (cmp) >= 0; }	\
	BE_MAYBE_UNUSED friend inline bool	operator==(const typename& first, const typename& second)	{ return (cmp) == 0; }	\
	BE_MAYBE_UNUSED friend inline bool	operator!=(const typename& first, const typename& second)	{ return (cmp) != 0; }

#define	BE_AUTOOPERATORS_THREEWAY_WITHOTHERTYPE(typename1, typename2, cmp)												\
	BE_AUTOOPERATORS_THREEWAY_WITHOTHERTYPE_SPACESHIP(typename1, typename2, cmp)										\
	BE_MAYBE_UNUSED friend inline bool	operator< (const typename1& first, ConstMaybeRef<typename2> second) { return ((cmp) <  0); }	\
	BE_MAYBE_UNUSED friend inline bool	operator> (const typename1& first, ConstMaybeRef<typename2> second) { return ((cmp) >  0); }	\
	BE_MAYBE_UNUSED friend inline bool	operator<=(const typename1& first, ConstMaybeRef<typename2> second) { return ((cmp) <= 0); }	\
	BE_MAYBE_UNUSED friend inline bool	operator>=(const typename1& first, ConstMaybeRef<typename2> second) { return ((cmp) >= 0); }	\
	BE_MAYBE_UNUSED friend inline bool	operator==(const typename1& first, ConstMaybeRef<typename2> second) { return ((cmp) == 0); }	\
	BE_MAYBE_UNUSED friend inline bool	operator!=(const typename1& first, ConstMaybeRef<typename2> second) { return ((cmp) != 0); }	\
	BE_MAYBE_UNUSED friend inline bool	operator< (ConstMaybeRef<typename2> second, const typename1& first) { return ((cmp) >  0); }	\
	BE_MAYBE_UNUSED friend inline bool	operator> (ConstMaybeRef<typename2> second, const typename1& first) { return ((cmp) <  0); }	\
	BE_MAYBE_UNUSED friend inline bool	operator<=(ConstMaybeRef<typename2> second, const typename1& first) { return ((cmp) >= 0); }	\
	BE_MAYBE_UNUSED friend inline bool	operator>=(ConstMaybeRef<typename2> second, const typename1& first) { return ((cmp) <= 0); }	\
	BE_MAYBE_UNUSED friend inline bool	operator==(ConstMaybeRef<typename2> second, const typename1& first) { return ((cmp) == 0); }	\
	BE_MAYBE_UNUSED friend inline bool	operator!=(ConstMaybeRef<typename2> second, const typename1& first) { return ((cmp) != 0); }
