/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinRuntime.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinRuntime.h>
#include <Modules/ModuleManager.h>

#include <ITwinRuntime/Private/ITwinUELogAdapter.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <Core/Tools/Tools.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

IMPLEMENT_MODULE(FITwinRuntimeModule, ITwinRuntime)

void FITwinRuntimeModule::StartupModule()
{
	Super::StartupModule();

#if WITH_EDITOR
	// Redirect BE_LOGX macros to Unreal Editor logs.
	using namespace SDK::Core;
	Tools::ILog::SetNewFct([](std::string s, Tools::Level level) {
		std::shared_ptr<Tools::ILog> p(static_cast<Tools::ILog*>(new FITwinUELogAdapter(s, level)));
		return p;
	});
#endif

	// We need to initialize the assertion handler here too (we have a different DLL for each Plugin, and
	// a DLL for the App as well...)
	SDK::Core::Tools::InitAssertHandler("ITwinRuntime");
	SDK::Core::Tools::CreateAdvVizLogChannels();
}
