/*--------------------------------------------------------------------------------------+
|
|     $Source: BeforeNonUnrealIncludes.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

// NO => #pragma once

#ifdef BE_UE_THIRDPARTY_GUARD
    #error "Nested/misordered/inconsistent Unreal third-party inclusion headers usage!"
#else
    #define BE_UE_THIRDPARTY_GUARD 1
#endif

#include "HAL/Platform.h"

// This must be before "undef TEXT"!
#ifdef WIN32
    #include "Microsoft/AllowMicrosoftPlatformTypes.h"
#endif

// Once upon a time, push_macro led to errors with TEXT and I had to just undef it and redef in AfterNonUnrealIncludes.h
// I'm putting it back see if it comes up again...
#pragma push_macro ("TEXT") // defined in winnt.h but also in UE
#undef TEXT
#pragma push_macro ("check")
#undef check
#pragma push_macro ("verify")
#undef verify

PRAGMA_PUSH_PLATFORM_DEFAULT_PACKING
THIRD_PARTY_INCLUDES_START
