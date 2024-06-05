/*--------------------------------------------------------------------------------------+
|
|     $Source: Break.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "Misc/AssertionMacros.h"

#define BE_ASSERT(x) check(x)
#define BE_ASSERT_PERF(x) checkSlow(x)
#define BE_ASSERT_MSG(x, msg) checkf(x, TEXT(msg))
#define BE_ISSUE(msg) BE_ASSERT_MSG(false, msg)
