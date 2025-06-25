/*--------------------------------------------------------------------------------------+
|
|     $Source: Assert.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "../AdvVizLinkType.h"

#define LIBASSERT_PREFIX_ASSERTIONS
#define LIBASSERT_BREAK_ON_FAIL
#ifndef LIBASSERT_STATIC_DEFINE
	#define LIBASSERT_STATIC_DEFINE
#endif

#ifndef CPPTRACE_STATIC_DEFINE
	#define CPPTRACE_STATIC_DEFINE
#endif

#include <libassert/assert.hpp>

#if LIBASSERT_IS_CLANG || LIBASSERT_IS_GCC || !LIBASSERT_NON_CONFORMANT_MSVC_PREPROCESSOR
#	define BE_PANIC(...) LIBASSERT_PANIC(__VA_ARGS__)
#else
#	define BE_PANIC LIBASSERT_PANIC
#endif

#if defined(RELEASE_CONFIG) 
#	define BE_DEBUG_ASSERT(...) (void)0
#	define BE_ASSERT(...) (void)0
#	define BE_ASSUME(...) (void)0
#	define BE_UNREACHABLE(...) (void)0
#	define BE_DEBUG_ASSERT_VAL(...) (void)0
#	define BE_ASSUME_VAL(...) (void)0
#	define BE_ASSERT_VAL(...) (void)0
#	define BE_ASSERT_MSG(...) (void)0
#	define BE_ISSUE(...) (void)0
#else //RELEASE_CONFIG
#	if (LIBASSERT_IS_CLANG || LIBASSERT_IS_GCC || !LIBASSERT_NON_CONFORMANT_MSVC_PREPROCESSOR)
#		define BE_DEBUG_ASSERT(...) LIBASSERT_DEBUG_ASSERT(__VA_ARGS__)
#		define BE_ASSERT(...) LIBASSERT_ASSERT(__VA_ARGS__)
#		define BE_ASSUME(...) LIBASSERT_ASSUME(__VA_ARGS__)
#		define BE_UNREACHABLE(...) LIBASSERT_UNREACHABLE(__VA_ARGS__)
#		define BE_DEBUG_ASSERT_VAL(...) LIBASSERT_DEBUG_ASSERT_VAL(__VA_ARGS__)
#		define BE_ASSUME_VAL(...) LIBASSERT_ASSUME_VAL(__VA_ARGS__)
#		define BE_ASSERT_VAL(...) LIBASSERT_ASSERT_VAL(__VA_ARGS__)
#		define BE_ASSERT_MSG(...) LIBASSERT_ASSERT(__VA_ARGS__) // for compatibility with old code
#	else
// because of course msvc
#		define BE_DEBUG_ASSERT LIBASSERT_DEBUG_ASSERT
#		define BE_ASSERT LIBASSERT_ASSERT
#		define BE_ASSUME LIBASSERT_ASSUME
#		define BE_UNREACHABLE LIBASSERT_UNREACHABLE
#		define BE_DEBUG_ASSERT_VAL LIBASSERT_DEBUG_ASSERT_VAL
#		define BE_ASSUME_VAL LIBASSERT_ASSUME_VAL
#		define BE_ASSERT_VAL LIBASSERT_ASSERT_VAL
#		define BE_ASSERT_MSG LIBASSERT_ASSERT // for compatibility with old code
#	endif
#	define BE_ISSUE(...) LIBASSERT_ASSERT(false, __VA_ARGS__)
#endif //RELEASE_CONFIG



