/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinRendering.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Modules/ModuleInterface.h>

/// Module holding global shaders and scene view extensions used by ITwinRuntime. Loaded at PostConfigInit
/// (global shader types must be registered before the global shader map is compiled).
class FITwinRenderingModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
