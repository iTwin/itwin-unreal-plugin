/*--------------------------------------------------------------------------------------+
|
|     $Source: Error.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "nonstd/expected.hpp"

namespace AdvViz
{
    // we will switch to std::expected when c++23 compiler will be used.
	using nonstd::expected; 
    using nonstd::unexpected_type;
    using nonstd::bad_expected_access;
    using nonstd::unexpect_t;
    using nonstd::make_unexpected;
}
