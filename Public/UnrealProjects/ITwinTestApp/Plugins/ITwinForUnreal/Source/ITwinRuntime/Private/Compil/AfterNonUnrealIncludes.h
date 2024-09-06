/*--------------------------------------------------------------------------------------+
|
|     $Source: AfterNonUnrealIncludes.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

// NO => #pragma once

#if !defined BE_UE_THIRDPARTY_GUARD
    #error "Nested/misordered/inconsistent Unreal third-party inclusion headers usage!"
#else
    #undef BE_UE_THIRDPARTY_GUARD
#endif

THIRD_PARTY_INCLUDES_END
PRAGMA_POP_PLATFORM_DEFAULT_PACKING
#pragma pop_macro ("check")
#pragma pop_macro ("verify")

// Once upon a time, push_macro led to errors with TEXT and I had to just undef it and redef it here
// Let's see if it comes up again...
#pragma pop_macro ("TEXT")
//#define TEXT(x) TEXT_PASTE(x) // redefining it like in HAL/Platform.h worked better

// See comment on AllowMicrosoftPlatformTypes.h in BeforeNonUnrealIncludes.h: I moved this include here after the pop_macros
#ifdef WIN32
    #include "Microsoft/HideMicrosoftPlatformTypes.h"
    #undef OPAQUE // defined in wingdi.h but Cesium has a constant with that name
#endif
