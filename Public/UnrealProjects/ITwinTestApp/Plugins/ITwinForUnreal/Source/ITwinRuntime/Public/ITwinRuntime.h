/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinRuntime.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CoreMinimal.h>
#include <Modules/ModuleInterface.h>

class FITwinRuntimeModule : public IModuleInterface
{
	using Super = IModuleInterface;
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
