/*--------------------------------------------------------------------------------------+
|
|     $Source: SanitizedPlatformHeaders.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

// This header file includes "platform" headers (Windows.h at the moment)
// and makes sure to disable some macros (eg. min(), max() etc) that could cause errors
// in third-party code.
#ifdef WIN32
#include <Windows/MinWindows.h>
#undef OPAQUE // defined in wingdi.h but Cesium has a constant with that name
#undef GetCurrentTime
#endif