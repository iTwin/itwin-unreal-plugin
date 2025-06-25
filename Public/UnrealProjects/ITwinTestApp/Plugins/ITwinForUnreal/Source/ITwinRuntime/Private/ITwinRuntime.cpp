/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinRuntime.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinRuntime.h>

#include <ITwinRuntime/Private/Helpers/UEDelayedCallHandler.h>
#include <ITwinRuntime/Private/ITwinUELogAdapter.h>
#include <Network/UEHttp.h>
#include <Network/UEAdvVizTask.h>

#include <ITwinStyle.h>
#include <Modules/ModuleManager.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <Core/Tools/Tools.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

IMPLEMENT_MODULE(FITwinRuntimeModule, ITwinRuntime)

void FITwinRuntimeModule::StartupModule()
{
	Super::StartupModule();

	// By default, do not load any custom style (only used to configure the application icons appearing in
	// the title bars of created windows: this should not be done automatically by the plugin).
	//FITwinStyle::Initialize();

	using namespace AdvViz::SDK;

#if WITH_EDITOR
	// Redirect BE_LOGX macros to Unreal Editor logs.
	Tools::ILog::SetNewFct([](std::string s, Tools::Level level) {
		Tools::ILog* p(static_cast<Tools::ILog*>(new FITwinUELogAdapter(s, level)));
		return p;
	});
#endif

	FUEHttp::Init();
	FUETaskManager::Init();

	// Connect delayed call system
	IDelayedCallHandler::SetNewFct([]() {
		IDelayedCallHandler* p(static_cast<IDelayedCallHandler*>(new FUEDelayedCallHandler()));
		return p;
	});

	// We need to initialize the assertion handler here too (we have a different DLL for each Plugin, and
	// a DLL for the App as well...)
	AdvViz::SDK::Tools::InitAssertHandler("ITwinRuntime");
	AdvViz::SDK::Tools::CreateAdvVizLogChannels();
}

void FITwinRuntimeModule::ShutdownModule()
{
	FITwinStyle::Shutdown();

	Super::ShutdownModule();
}
