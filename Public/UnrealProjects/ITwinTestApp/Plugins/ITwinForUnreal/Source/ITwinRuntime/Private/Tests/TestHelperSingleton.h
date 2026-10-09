/*--------------------------------------------------------------------------------------+
|
|     $Source: TestHelperSingleton.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#if WITH_TESTS

#include <CoreMinimal.h>

template <class TTestHelperClass>
class TTestHelperSingleton
{
public:
	static TTestHelperClass& Instance();
	static void ResetInstance();

private:
	static TObjectPtr<TTestHelperClass> Singleton;
};

#include "TestHelperSingleton.inl"

#endif // WITH_TESTS
