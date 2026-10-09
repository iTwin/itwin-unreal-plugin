/*--------------------------------------------------------------------------------------+
|
|     $Source: TestHelperSingleton.inl $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#if WITH_TESTS

#include <UObject/UObjectGlobals.h>

/*static*/
template <class TTestHelperClass>
TObjectPtr<TTestHelperClass> TTestHelperSingleton<TTestHelperClass>::Singleton;


/*static*/
template <class TTestHelperClass>
TTestHelperClass& TTestHelperSingleton<TTestHelperClass>::Instance()
{
	if (!Singleton)
	{
		Singleton = NewObject<TTestHelperClass>(GetTransientPackage(),
			TTestHelperClass::StaticClass()->GetFName(),
			RF_Standalone);
		Singleton->AddToRoot();
	}
	return *Singleton;
}

/*static*/
template <class TTestHelperClass>
void TTestHelperSingleton<TTestHelperClass>::ResetInstance()
{
	if (Singleton)
	{
		check(Singleton->PostCondition());

		// Perform custom reset logic if needed.
		Singleton->OnReset();

		Singleton->RemoveFromRoot();

		Singleton = nullptr;
	}
}

#endif // WITH_TESTS
