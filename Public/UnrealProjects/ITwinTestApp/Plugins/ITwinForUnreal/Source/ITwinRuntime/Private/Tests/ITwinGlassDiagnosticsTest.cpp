/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinGlassDiagnosticsTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <HAL/IConsoleManager.h>
#include <Misc/AutomationTest.h>

#if WITH_TESTS && !UE_BUILD_SHIPPING
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FITwinGlassDumpRegisteredTest,
	"Bentley.ITwinForUnreal.ITwinRuntime.Glass.DumpRegistered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinGlassDumpRegisteredTest::RunTest(const FString& Parameters)
{
	TestNotNull(TEXT("ITwin.Glass.Dump is registered"),
		IConsoleManager::Get().FindConsoleObject(TEXT("ITwin.Glass.Dump")));
	return true;
}
#endif // WITH_TESTS && !UE_BUILD_SHIPPING
