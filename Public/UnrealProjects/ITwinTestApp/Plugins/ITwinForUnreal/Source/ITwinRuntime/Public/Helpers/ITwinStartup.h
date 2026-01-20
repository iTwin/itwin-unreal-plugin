/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinStartup.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"

class ITWINRUNTIME_API FITwinStartup
{
public:
	//! Common initialization routines between the iTwinRuntime module and any GameMode's module based on
	//! ITwinForUnreal plugin.
	static void CommonStartup(const FString& ModuleName);

	//! Propose to attach the debugger if applicable (restricted to development/debug builds).
	static void ProposeAttachDebugger(const FString& ContextInfo = {});

	//! Enable VR mode.
	static void EnableVR();
};
