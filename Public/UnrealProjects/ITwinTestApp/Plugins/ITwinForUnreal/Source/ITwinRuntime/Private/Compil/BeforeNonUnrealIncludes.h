/*--------------------------------------------------------------------------------------+
|
|     $Source: BeforeNonUnrealIncludes.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

// NO => #pragma once

#ifdef BE_UE_THIRDPARTY_GUARD
    #error "Nested/misordered/inconsistent Unreal third-party inclusion headers usage!"
#else
    #define BE_UE_THIRDPARTY_GUARD 1
#endif

#pragma push_macro ("check")
#pragma push_macro ("verify")
#pragma push_macro ("TEXT") // defined in winnt.h but also in UE
#undef check
#undef verify
#undef TEXT
#ifdef WIN32
    #include "Microsoft/AllowMicrosoftPlatformTypes.h"
#endif
PRAGMA_PUSH_PLATFORM_DEFAULT_PACKING
THIRD_PARTY_INCLUDES_START
