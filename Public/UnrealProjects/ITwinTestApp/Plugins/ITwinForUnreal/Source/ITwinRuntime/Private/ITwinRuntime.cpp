/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinRuntime.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinRuntime.h>

#include <Helpers/ITwinStartup.h>
#include <ITwinStyle.h>
#include <Modules/ModuleManager.h>


IMPLEMENT_MODULE(FITwinRuntimeModule, ITwinRuntime)

void FITwinRuntimeModule::StartupModule()
{
	FITwinStartup::CommonStartup(TEXT("ITwinRuntime"));

	Super::StartupModule();

	// By default, do not load any custom style (only used to configure the application icons appearing in
	// the title bars of created windows: this should not be done automatically by the plugin).
	//FITwinStyle::Initialize();
}

void FITwinRuntimeModule::ShutdownModule()
{
	FITwinStyle::Shutdown();

	Super::ShutdownModule();
}
