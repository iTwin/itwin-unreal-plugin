/*--------------------------------------------------------------------------------------+
|
|     $Source: Attributes.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

// from vue.git/Vue/Source/eonLib/eonAttributes.h

#pragma once

/// Use this when ignoring the return value of a function means there is a
/// coding mistake, ie. everytime the function has no other role than computing
/// and returning it.
/// !! WARNING !! - When the returned type is a struct/class, the copy ctor
///					must be explicitly defined, otherwise returning the value
///					calls a ctor which seems to fool GCC into NOT spawning the warning :-(
/// For an example, see vue::TransformObject::AppliquerTransformationPosition
/// for which it is very useful because it can easily be mistaken with the more
/// widely used vue::TransformObject::AppliquerTransformation which modifies the parameter
/// and returns void...
#if defined(__cplusplus) && (__cplusplus >= 201703L)
	#define BE_MAYBE_UNUSED	[[maybe_unused]]
#elif defined __GNUC__	//	Assuming clang or GCC >= 4.2
	#if defined(__clang__) && !__has_attribute(warn_unused_result)
		#error "We assumed that CLANG would alway have the warn_unused_result attribute available. That's not the case, please fix the following code."
	#endif

	#define BE_MAYBE_UNUSED	__attribute__((unused))
#elif defined(_MSC_VER) && (_MSC_VER >= 1700)
	// This is useless without compil option /analyze (with itself reports a zillion other warnings)
	#define BE_MAYBE_UNUSED
#else
	#define BE_MAYBE_UNUSED
#endif

/// Use this to mark explicitly that you don't want a break at the end of a 'case' in a 'switch'
/// statement because you indeed intend the program to fall through to the next 'case'.
/// C++17 extension. Note: the semi-colon at the end is required!
/// Note 2: __has_attribute does not work for attributes to be put in [[]], only for those used
/// with __attribute__(()) it seems.
#if defined(__cplusplus) && (__cplusplus >= 201703L)
	#define BE_FALLTHROUGH [[fallthrough]];
#else
	#define BE_FALLTHROUGH
#endif

#if !defined(SWIG) && defined(__has_cpp_attribute)
	#if __has_cpp_attribute(likely)
		#define	BE_LIKELY		[[likely]]
		#define	BE_UNLIKELY	[[unlikely]]
	#endif
#endif

#ifndef BE_LIKELY
	#define	BE_LIKELY
	#define	BE_UNLIKELY
#endif

#if defined __GNUC__
	// Not on same line as above otherwise VS emits a warning...
	#if __has_attribute(no_sanitize)
		#define BE_NO_SANITIZE_SIGNED_INT_OVERFLOW __attribute__((no_sanitize("signed-integer-overflow")))
		#define BE_NO_SANITIZE_UNSIGNED_INT_OVERFLOW __attribute__((no_sanitize("unsigned-integer-overflow")))
	#endif
#endif
// Set default definition if it has not been defined above.
#if !defined BE_NO_SANITIZE_SIGNED_INT_OVERFLOW
	#define BE_NO_SANITIZE_SIGNED_INT_OVERFLOW
	#define BE_NO_SANITIZE_UNSIGNED_INT_OVERFLOW
#endif
