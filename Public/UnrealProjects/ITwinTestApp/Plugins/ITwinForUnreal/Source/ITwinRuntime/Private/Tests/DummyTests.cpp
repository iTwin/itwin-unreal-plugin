/*--------------------------------------------------------------------------------------+
|
|     $Source: DummyTests.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Misc/AutomationTest.h>

#if WITH_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(Test1, "Bentley.ITwinForUnreal.ITwinRuntime.Test1", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool Test1::RunTest(const FString& Parameters)
{
	return true;
}

#endif // WITH_TESTS