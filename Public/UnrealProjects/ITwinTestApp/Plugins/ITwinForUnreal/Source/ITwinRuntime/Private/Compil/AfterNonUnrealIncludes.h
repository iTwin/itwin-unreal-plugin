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
#ifdef WIN32
    #include "Microsoft/HideMicrosoftPlatformTypes.h"
    #undef OPAQUE // defined in wingdi.h but Cesium has a constant with that name
#endif
#pragma pop_macro ("check")
#pragma pop_macro ("TEXT")
