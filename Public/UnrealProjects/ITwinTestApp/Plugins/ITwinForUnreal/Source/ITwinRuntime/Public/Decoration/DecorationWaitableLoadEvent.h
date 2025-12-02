/*--------------------------------------------------------------------------------------+
|
|     $Source: DecorationWaitableLoadEvent.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <string>

//! Helper for synchronization between the loading of the scene and some iTwin requests (typically).
class ITWINRUNTIME_API FDecorationWaitableLoadEvent
{
public:
	virtual ~FDecorationWaitableLoadEvent() {}
	//! Return true if the scene loader thread should wait for us. Must be thread safe!
	virtual bool ShouldWait() const = 0;
	virtual std::string Describe() const = 0;
};
