/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinStartup.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Helpers/ITwinStartup.h>

#include <Annotations/ITwinAnnotation.h>
#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <Core/Visualization/Visualization.h>
#	include <Core/Tools/Tools.h>
#	include <filesystem>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>
#include <ITwinRuntime/Private/Helpers/UEDelayedCallHandler.h>
#include <ITwinRuntime/Private/ITwinUELogAdapter.h>
#include <ITwinRuntime/Private/Network/UEHttp.h>
#include <Network/UEAdvVizTask.h>
#include <Network/UEHttp.h>


// UE headers
#include <Misc/MessageDialog.h>


/*static*/
void FITwinStartup::ProposeAttachDebugger([[maybe_unused]] const FString& ContextInfo /*= {}*/)
{
#if WITH_EDITOR == 0 && (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT)
	static bool first = true;
	if (first && !FPlatformMisc::IsDebuggerPresent())
	{
		first = false;
		bool bWaitDebugger = FPlatformMisc::GetEnvironmentVariable(TEXT("BENTLEY_CARROT_WAIT_DEBUGGER")) != TEXT("OFF");
		if (bWaitDebugger)
		{
			FMessageDialog::Open(EAppMsgType::Ok,
				FText::FromString(TEXT("You can attach the debugger.")),
				FText::FromString(FString::Printf(TEXT("Carrot Debug %s"), *ContextInfo)));
		}
	}
#endif //DEV_GAME

	// enable this code to test crash report mechanism. Set the env var BENTLEY_CARROT_FORCECRASH=ON to trigger a crash in Private/ITwinStudioApp/carrot/.vscode/launch.json.
	//const bool bMakesCrash = FPlatformMisc::GetEnvironmentVariable(TEXT("BENTLEY_CARROT_FORCECRASH")) == TEXT("ON");
	//if (bMakesCrash)
	//{
	//	int* i = 0;
	//	*i = 5;
	//}
}

/*static*/
void FITwinStartup::CommonStartup(FString const& ModuleName)
{
	using namespace AdvViz::SDK;

	ProposeAttachDebugger(ModuleName);

#if WITH_EDITOR
	// Redirect BE_LOGX macros to Unreal Editor logs.
	Tools::ILog::SetNewFct([](std::string s, Level level) {
		return static_cast<Tools::ILog*>(new FITwinUELogAdapter(s, level));
	});
#endif

	CreateAdvVizLogChannels();
	CreateLogChannel("ContentHelper", Level::info); //unreal only logs

	// Remark: at this point, logs are not yet totally enabled: #InitLog is called a few lines below, through
	// Tools::#InitAssertHandler.

	FUEHttp::Init();
	FUETaskManager::Init();

	// Connect delayed call system
	IDelayedCallHandler::SetNewFct([]() {
		IDelayedCallHandler* p(static_cast<IDelayedCallHandler*>(new FUEDelayedCallHandler()));
		return p;
	});

	const std::string ModuleNameUTF8 = TCHAR_TO_UTF8(*ModuleName);
	Tools::InitAssertHandler(ModuleNameUTF8);
	BE_LOGI("App", "========== Starting Unreal '" << ModuleNameUTF8 << "' module ==========");
}

void FITwinStartup::EnableVR()
{
	AITwinAnnotation::EnableVR();
}
