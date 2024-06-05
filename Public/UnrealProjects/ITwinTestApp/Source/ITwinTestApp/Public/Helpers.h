/*--------------------------------------------------------------------------------------+
|
|     $Source: Helpers.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Kismet/BlueprintFunctionLibrary.h>
#include <Helpers.generated.h>

UCLASS()
class ITWINTESTAPP_API UHelpers: public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"))
	static void PickMouseElements(const UObject* WorldContextObject, bool& bValid, FString& ElementId);
};
