/*--------------------------------------------------------------------------------------+
|
|     $Source: ReadWriteHelper.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CoreMinimal.h>
#include <Kismet/BlueprintFunctionLibrary.h>
#include "ReadWriteHelper.generated.h"

class FJsonObject;

UCLASS()
class UJsonUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UJsonUtils();
	~UJsonUtils();
	
	//UFUNCTION(BlueprintCallable, Category = "Timeline data")
	//static FTimelineData ReadStructFromJsonFile(FString filePath);
	//
	//UFUNCTION(BlueprintCallable, Category = "Timeline data")
	//static bool WriteStructToJsonFile(FString filePath, FTimelineData data);

	UFUNCTION()
	static FString ReadStringFromFile(FString filePath);
	UFUNCTION()
	static bool WriteStringToFile(FString filePath, FString str);
	//UFUNCTION()
	static TSharedPtr<FJsonObject> ReadJson(FString filePath);
	//UFUNCTION()
	static bool WriteJson(FString filePath, TSharedPtr<FJsonObject> jsonObj);
};
