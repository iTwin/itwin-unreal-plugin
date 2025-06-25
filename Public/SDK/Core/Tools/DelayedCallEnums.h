/*--------------------------------------------------------------------------------------+
|
|     $Source: DelayedCallEnums.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#pragma once

namespace AdvViz::SDK::DelayedCall
{
	/// Whether the callback should be repeated after its execution.

	enum class EReturnedValue : uint8_t
	{
		Done /* the callback won't be repeated */,
		Repeat /* the callback should be repeated (after the same delay) */
	};

}
