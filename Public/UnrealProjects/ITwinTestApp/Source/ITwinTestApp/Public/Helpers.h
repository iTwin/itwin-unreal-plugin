/*--------------------------------------------------------------------------------------+
|
|     $Source: Helpers.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Kismet/BlueprintFunctionLibrary.h>
#include <Helpers/ITwinPickingOptions.h>
#include <Helpers.generated.h>

UCLASS()
class ITWINTESTAPP_API UHelpers: public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"))
	static void PickMouseElements(const UObject* WorldContextObject, bool& bValid, FString& ElementId);

	UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"))
	static void PickUnderCursorWithOptions(const UObject* WorldContextObject, bool& bValid, FString& ElementId,
		FITwinPickingOptions const& Options);
};
