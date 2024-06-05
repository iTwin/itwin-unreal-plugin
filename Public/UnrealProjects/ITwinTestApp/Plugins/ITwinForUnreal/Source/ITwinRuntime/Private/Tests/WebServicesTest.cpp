/*--------------------------------------------------------------------------------------+
|
|     $Source: WebServicesTest.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#if WITH_TESTS

#include <CoreMinimal.h>
#include <Misc/AutomationTest.h>
#include <Misc/LowLevelTestAdapter.h>

#include <ITwinWebServices/ITwinWebServices.h>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FITwinWebServicesTest, "Bentley.ITwinForUnreal.ITwinRuntime.WebServices", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinWebServicesTest::RunTest(const FString& /*Parameters*/)
{
	SECTION("TokenEncryption")
	{
		FString SrcToken;
		// Build a random token

		FString const ValidChars = TEXT("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
		int32 const NbValidChars = ValidChars.Len() - 1;
		SrcToken.Reserve(512);
		for (uint32 i = 0; i < 512; ++i)
		{
			SrcToken.AppendChar(ValidChars[FMath::RandRange(0, NbValidChars)]);
		}

		// avoid conflicting with the true application, and provide with a default iTwin App ID if there is
		// none currently
		UITwinWebServices::SetupTestMode(EITwinEnvironment::Prod);

		UTEST_TRUE("SaveToken", UITwinWebServices::SaveToken(SrcToken, EITwinEnvironment::Prod));
		FString ReadToken;
		UTEST_TRUE("LoadToken", UITwinWebServices::LoadToken(ReadToken, EITwinEnvironment::Prod));
		UTEST_EQUAL("Unchanged Token", ReadToken, SrcToken);
	}
	return true;
}

#endif // WITH_TESTS
