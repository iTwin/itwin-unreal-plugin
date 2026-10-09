/*--------------------------------------------------------------------------------------+
|
|     $Source: SceneMappingTestHelper.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#if WITH_TESTS

#include <Tests/TestHelperSingleton.h>
#include <CoreMinimal.h>
#include <memory>

#include "SceneMappingTestHelper.generated.h"


class UStaticMeshComponent;
class USceneMappingTestHelper;

UCLASS()
class USceneMappingTestHelper : public UObject, public TTestHelperSingleton<USceneMappingTestHelper>
{
	GENERATED_BODY()
public:
	USceneMappingTestHelper();

	UStaticMeshComponent* GetMeshComponent() const { return MeshComponent; }

	bool Init();
	bool PostCondition() const;
	void OnReset();

private:
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	class FImpl;
	TPimplPtr<FImpl> Impl;
};

#endif // WITH_TESTS
