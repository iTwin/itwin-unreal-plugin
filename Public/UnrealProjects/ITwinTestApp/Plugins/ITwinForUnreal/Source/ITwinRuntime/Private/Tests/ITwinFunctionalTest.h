/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinFunctionalTest.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <UE5Coro.h>
#include <ITwinFunctionalTest.generated.h>

//! Do not use this class directly, this is an implementation detail.
//! Note: initially this class would inherit AFunctionalTest and override StartTest(),
//! but it would lead to unsolvable errors when building "shipping" mode.
//! So as a workaround we use this class and register it on AFunctionalTest::OnTestStart.
UCLASS()
class AITwinFunctionalTestStarter: public AActor
{
	GENERATED_BODY()
public:
	class FImpl;
	TPimplPtr<FImpl> Impl;
	AITwinFunctionalTestStarter();
	UFUNCTION()
	void StartTest();
};

#if WITH_EDITOR

namespace ITwin::Tests
{
namespace Detail
{

using FFunctionalTestImpl = std::function<UE5Coro::TCoroutine<>(UWorld*)>;
void RegisterFunctionalTest(const FString& Name, const FFunctionalTestImpl& Test);

} // namespace Detail

//! Takes a screenshot for the current test, with the given name.
//! No need to put the name of the test in the Name parameters,
//! as it will be automatically inserted in the screenshot file name.
UE5Coro::TCoroutine<> TakeScreenshot(const FString Name);

} // namespace ITwin::Tests

//! Use this macro to implement your functional test.
//! Example:
//! ITWIN_FUNCTIONAL_TEST(MyTest)
//! {
//!     auto* const actor = World->SpawnActor<MyActor>(MyActor::StaticClass());
//!     co_await TakeScreenshot(TEXT("Screenshot1"));
//!     actor->ActivateCoolVisualizationFeatures();
//!     co_await TakeScreenshot(TEXT("Screenshot2"));
//! }
#define ITWIN_FUNCTIONAL_TEST_EX(Name, IsEnabled) \
	namespace ITwin::Tests \
	{ \
	class FFunctionalTest_##Name \
	{ \
	public: \
		static UE5Coro::TCoroutine<> Run(UWorld* World); \
	protected: \
		static inline const FString TestName = TEXT(#Name); \
		static UE5Coro::TCoroutine<> TakeScreenshot(const FString& Name2) \
		{ \
			return ITwin::Tests::TakeScreenshot(FString(TEXT(#Name))+TEXT("_")+Name2); \
		}; \
	private: \
		static int Registrar; \
	}; \
	int FFunctionalTest_##Name::Registrar = [] \
	{ \
		if constexpr (IsEnabled) \
			Detail::RegisterFunctionalTest(TEXT(#Name), FFunctionalTest_##Name::Run); \
		return 0; \
	}(); \
	} \
	UE5Coro::TCoroutine<> ITwin::Tests::FFunctionalTest_##Name::Run(UWorld* World)

#define ITWIN_FUNCTIONAL_TEST(Name) ITWIN_FUNCTIONAL_TEST_EX(Name, true)
	
#endif // WITH_EDITOR
