/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinIModelEventHandler.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

//! Interface for handling iModel events, such as offset detection.
//! Introduced for testing.
class FITwinIModelEventHandler
{
public:
	virtual ~FITwinIModelEventHandler() = default;
	//! Called when the iModel offset is detected.
	virtual bool HandleOffsetDetected() { return false; }
};
