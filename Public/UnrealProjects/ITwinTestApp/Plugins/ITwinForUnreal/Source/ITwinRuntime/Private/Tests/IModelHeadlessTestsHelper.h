/*--------------------------------------------------------------------------------------+
|
|     $Source: IModelHeadlessTestsHelper.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#if WITH_TESTS

#include <Tests/TestHelperSingleton.h>
#include <CoreMinimal.h>
#include <memory>

#include "IModelHeadlessTestsHelper.generated.h"


struct FElementProperties;
struct FIModelTestOptions;

UCLASS()
class UIModelHeadlessTestsHelper : public UObject, public TTestHelperSingleton<UIModelHeadlessTestsHelper>
{
	GENERATED_BODY()
public:
	UIModelHeadlessTestsHelper();

	bool Init(FIModelTestOptions const& Options);
	void RegisterEvents();
	bool PostCondition() const;
	void OnReset();

	UFUNCTION()
	void OnIModelLoaded(bool bSuccess, FString StringId);

	UFUNCTION()
	void OnDecorationLoaded();

	UFUNCTION()
	void OnSavedViewsRetrieved(const FString& ID);

	UFUNCTION()
	void OnElementPropertiesRetrieved(bool bSuccess, const FElementProperties& ElementProps, const FString& ElementId);

	class FImpl;
	FImpl& GetImpl() { return *Impl; }

private:
	TPimplPtr<FImpl> Impl;
};

#endif // WITH_TESTS
