/*--------------------------------------------------------------------------------------+
|
|     $Source: SaveStatus.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

MODULE_EXPORT namespace AdvViz::SDK
{
	enum class ESaveStatus : uint8_t
	{
		/* The item was never saved. */
		NeverSaved,
		/* The item needs to be saved. */
		ShouldSave,
		/* The item has been collected for saving, and the operation is being performed (usually in a
		 * background thread). */
		InProgress,
		/* The item was saved and is up-to-date. */
		Done
	};

}
