/*--------------------------------------------------------------------------------------+
|
|     $Source: SanitizedPlatformHeaders.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

// This header file includes "platform" headers (Windows.h at the moment)
// and makes sure to disable some macros (eg. min(), max() etc) that could cause errors
// in third-party code.
#ifdef WIN32
#include <Windows/AllowWindowsPlatformTypes.h>
#include <Windows/MinWindows.h>
#include <Windows/HideWindowsPlatformTypes.h>
#undef OPAQUE // defined in wingdi.h but Cesium has a constant with that name
#undef GetCurrentTime
#undef GetObject
#undef InterlockedAnd
#undef Yield
#endif