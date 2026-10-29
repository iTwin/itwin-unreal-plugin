/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinRendering.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinRendering.h>

#include "ITwinSelectionOutlineViewExtension.h"

#include <Modules/ModuleManager.h>

IMPLEMENT_MODULE(FITwinRenderingModule, ITwinRendering)

void FITwinRenderingModule::StartupModule()
{
	// Note: the "/Plugin/ITwinForUnreal" shader directory mapping is registered by the CesiumRuntime module
	// (also loaded at PostConfigInit), which maps it to <Plugin>/Shaders.
}

void FITwinRenderingModule::ShutdownModule()
{
	FITwinSelectionOutlineViewExtension::Shutdown();
}
