/*--------------------------------------------------------------------------------------+
|
|     $Source: CutoutPersistenceTestHelper.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#if WITH_TESTS

#include <Tests/TestHelperSingleton.h>
#include <CoreMinimal.h>
#include <memory>

#include "CutoutPersistenceTestHelper.generated.h"


UCLASS()
class UCutoutPersistenceTestHelper : public UObject, public TTestHelperSingleton<UCutoutPersistenceTestHelper>
{
	GENERATED_BODY()
public:
	UCutoutPersistenceTestHelper();

	bool Init();
	bool PostCondition() const;
	void OnReset();

	UFUNCTION()
	void OnSceneSaved(bool bSuccess);

	class FImpl;
	FImpl& GetImpl() { return *Impl; }

private:
	TPimplPtr<FImpl> Impl;
};

#endif // WITH_TESTS
