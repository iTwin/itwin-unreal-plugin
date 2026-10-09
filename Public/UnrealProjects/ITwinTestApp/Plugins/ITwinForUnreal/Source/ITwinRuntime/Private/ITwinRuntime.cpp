/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinRuntime.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinRuntime.h>

#include <Helpers/ITwinStartup.h>
#include <ITwinStyle.h>
#include <Modules/ModuleManager.h>

#if WITH_EDITOR
	#include <EditorModeRegistry.h>
	#include <Helpers/ITwinPickingEdMode.h>
#endif

IMPLEMENT_MODULE(FITwinRuntimeModule, ITwinRuntime)

void FITwinRuntimeModule::StartupModule()
{
	FITwinStartup::CommonStartup(TEXT("ITwinRuntime"));

	Super::StartupModule();

	// By default, do not load any custom style (only used to configure the application icons appearing in
	// the title bars of created windows: this should not be done automatically by the plugin).
	//FITwinStyle::Initialize();

#if WITH_EDITOR
	FEditorModeRegistry::Get().RegisterMode<FITwinPickingEdMode>(FITwinPickingEdMode::EM_ITwinPicking);
#endif
}

void FITwinRuntimeModule::ShutdownModule()
{
#if WITH_EDITOR
	FEditorModeRegistry::Get().UnregisterMode(FITwinPickingEdMode::EM_ITwinPicking);
#endif

	FITwinStyle::Shutdown();

	Super::ShutdownModule();
}
