/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAutomationTestBaseNoLogs.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#if WITH_TESTS

#include <CoreMinimal.h>
#include <Misc/AutomationTest.h>

// Some tests can log at Error level in case of errors, which by default would flag the test as failed
// => Use an intermediate class to change this behavior
//
class FITwinAutomationTestBaseNoLogs : public FAutomationTestBase
{
public:
	FITwinAutomationTestBaseNoLogs(const FString& InName, const bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
	{

	}
	virtual bool SuppressLogErrors() override { return true; }
	virtual bool SuppressLogWarnings() override { return true; }
};

#endif // WITH_TESTS
