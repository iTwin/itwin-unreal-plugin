/*--------------------------------------------------------------------------------------+
|
|     $Source: EnumSwitchCoverage.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

// from vue.git/Vue/Source/eonLib/enumSwitchCoverage.h

/* Compiler-dependent enum handling (initially added for Clang compilation, to fix warnings) */

#pragma once

#include "Attributes.h"
#include "Break.h"

// used to be activated due to warnings, but was abandoned because it induces
// a very dangerous behavior when we cast an integer into an enum: not having
// a default case can be very dangerous in such case, and induces a big gap
// between the platforms...
// I don't know how Clang could have this idea (as long as c++ does not forbid
// such cast, which will probably never occur...)
#undef BE_HANDLE_FINITE_SWITCH_WITHOUT_DEFAULT_ON_CLANG

// we avoid #define X() defined(Y) because using it in macros (other than #ifdef) is UB.
#if defined __clang__ && defined BE_HANDLE_FINITE_SWITCH_WITHOUT_DEFAULT_ON_CLANG
#define BE_HANDLE_FINITE_SWITCH_WITHOUT_DEFAULT() 1
#else
#define BE_HANDLE_FINITE_SWITCH_WITHOUT_DEFAULT() 0
#endif

#if BE_HANDLE_FINITE_SWITCH_WITHOUT_DEFAULT()
	// CLang can warn when a switch over an enum does not cover all of the enum values
	// It's better to list here all explicitly uncovered cases, in order to be warned
	// at compile-time if a new value is added to the enum.

	#define BE_UNCOVERED_ENUM_EXEC(uncovered_cases, code) \
		uncovered_cases code;
#else
	// Other compilers will implement suboptimal "default: assert..." behaviour.
	#define BE_UNCOVERED_ENUM_EXEC(uncovered_cases, code)	\
		uncovered_cases										\
		default: code;
#endif

#if BE_HANDLE_FINITE_SWITCH_WITHOUT_DEFAULT()
	#define BE_NO_UNCOVERED_ENUM_EXEC(code) // nothing to do
#else // NOT __clang__
	// Required to avoid MSVC warnings about variables not initialized and some
	// return paths not returning anything, since MSVC (at least until 2010) does
	// not check against the enum definition to see whether all cases are covered
	// or not. Note that it also means that clang assumes you don't do things like
	// TEnum truc = (TEnum) 4212; where 4212 is not a valid TEnum value... Let's
	// hope it flags such hacks as an error!
	#define BE_NO_UNCOVERED_ENUM_EXEC(code) \
		default: code;
#endif // NOT __clang__

#if BE_HANDLE_FINITE_SWITCH_WITHOUT_DEFAULT()
	// Prevent the unreachable code warning when returning after a switch
	#define BE_RETURN(value) // nothing to do
#else
	#define BE_RETURN(value) return value;
#endif


#define BE_UNCOVERED_ENUM_ASSERT_AND_EXEC(uncovered_cases, code) \
	BE_UNCOVERED_ENUM_EXEC(uncovered_cases,BE_ISSUE("switch issue"); code)

#define BE_UNCOVERED_ENUM_ASSERT_AND_EXEC_AND_BREAK(code) \
	BE_UNCOVERED_ENUM_ASSERT_AND_EXEC(code; break);

#define BE_UNCOVERED_ENUM_ASSERT_AND_BREAK(uncovered_cases) \
	BE_UNCOVERED_ENUM_ASSERT_AND_EXEC(uncovered_cases, break)

#define BE_UNCOVERED_ENUM_ASSERT_AND_CONTINUE(uncovered_cases) \
	BE_UNCOVERED_ENUM_ASSERT_AND_EXEC(uncovered_cases,;)

#define BE_UNCOVERED_ENUM_ASSERT_AND_FALLTHROUGH(uncovered_cases) \
	BE_UNCOVERED_ENUM_ASSERT_AND_CONTINUE(uncovered_cases) BE_FALLTHROUGH

#define BE_UNCOVERED_ENUM_ASSERT_AND_RETURN_VOID(uncovered_cases) \
	BE_UNCOVERED_ENUM_ASSERT_AND_EXEC(uncovered_cases, return )

#define BE_UNCOVERED_ENUM_ASSERT_AND_RETURN(uncovered_cases, ret) \
	BE_UNCOVERED_ENUM_ASSERT_AND_EXEC(uncovered_cases, return ret)

#define BE_UNCOVERED_ENUM_CONTINUE(uncovered_cases) \
	BE_UNCOVERED_ENUM_EXEC(uncovered_cases,;)



#define BE_NO_UNCOVERED_ENUM_ASSERT_AND_EXEC(code) \
	BE_NO_UNCOVERED_ENUM_EXEC(BE_ISSUE("switch issue");code)

#define BE_NO_UNCOVERED_ENUM_ASSERT_AND_EXEC_AND_BREAK(code) \
	BE_NO_UNCOVERED_ENUM_ASSERT_AND_EXEC(code; break);

#define BE_NO_UNCOVERED_ENUM_ASSERT_AND_BREAK \
	BE_NO_UNCOVERED_ENUM_ASSERT_AND_EXEC(break)

#define BE_NO_UNCOVERED_ENUM_ASSERT_AND_CONTINUE \
	BE_NO_UNCOVERED_ENUM_ASSERT_AND_EXEC(;)

#define BE_NO_UNCOVERED_ENUM_ASSERT_AND_FALLTHROUGH	\
	BE_NO_UNCOVERED_ENUM_ASSERT_AND_CONTINUE BE_FALLTHROUGH

#define BE_NO_UNCOVERED_ENUM_ASSERT_AND_RETURN_VOID \
	BE_NO_UNCOVERED_ENUM_ASSERT_AND_EXEC(return)

#define BE_NO_UNCOVERED_ENUM_ASSERT_AND_RETURN(ret) \
	BE_NO_UNCOVERED_ENUM_ASSERT_AND_EXEC(return ret)

#define BE_NO_UNCOVERED_ENUM_CONTINUE \
	BE_NO_UNCOVERED_ENUM_EXEC(;)

