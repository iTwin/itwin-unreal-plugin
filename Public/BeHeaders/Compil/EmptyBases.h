/*--------------------------------------------------------------------------------------+
|
|     $Source: EmptyBases.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

// from vue.git/Vue/Source/eonLib/EmptyBases.h

#pragma once

// Enable empty base optimization in MSVC,
// see https://blogs.msdn.microsoft.com/vcblog/2016/03/30/optimizing-the-layout-of-empty-base-classes-in-vs2015-update-2-3/
#if defined _MSC_VER
#define BE_EMPTY_BASES __declspec(empty_bases)
#else
#define BE_EMPTY_BASES
#endif
