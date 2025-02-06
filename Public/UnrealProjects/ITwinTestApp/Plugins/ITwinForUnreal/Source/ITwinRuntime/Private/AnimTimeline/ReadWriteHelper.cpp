/*--------------------------------------------------------------------------------------+
|
|     $Source: ReadWriteHelper.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ReadWriteHelper.h"
#include <HAL/PlatformFileManager.h>
#include "Serialization/JsonSerializer.h" // Json
#include "JsonObjectConverter.h" // JsonUtilities
#include <Misc/FileHelper.h>

UJsonUtils::UJsonUtils()
{

}

UJsonUtils::~UJsonUtils()
{

}

//FTimelineData UJsonUtils::ReadStructFromJsonFile(FString filePath)
//{
//	TSharedPtr<FJsonObject> pJson = ReadJson(filePath);
//	FTimelineData ret;
//	if (pJson == nullptr)
//		return FTimelineData();
//	if (!FJsonObjectConverter::JsonObjectToUStruct<FTimelineData>(pJson.ToSharedRef(), &ret))
//		return FTimelineData();
//	return ret;
//}
//
//bool UJsonUtils::WriteStructToJsonFile(FString filePath, FTimelineData data)
//{
//	TSharedPtr<FJsonObject> pJson = FJsonObjectConverter::UStructToJsonObject(data);
//	if (pJson == nullptr)
//		return false;
//	return WriteJson(filePath, pJson);
//}

FString UJsonUtils::ReadStringFromFile(FString filePath)
{
	if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*filePath))
		return "";
		FString retString = "";
		if (!FFileHelper::LoadFileToString(retString, *filePath))
			return "";
			return retString;
}

bool UJsonUtils::WriteStringToFile(FString filePath, FString str)
{
	if (!FFileHelper::SaveStringToFile(str, *filePath))
		return false;
	return true;
}

TSharedPtr<FJsonObject> UJsonUtils::ReadJson(FString filePath)
{
	FString jsonStr = ReadStringFromFile(filePath);
	if (jsonStr.IsEmpty())
		return nullptr;
	TSharedPtr<FJsonObject> retObj;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(jsonStr), retObj))
		return nullptr;
	return retObj;
}

bool UJsonUtils::WriteJson(FString filePath, TSharedPtr<FJsonObject> jsonObj)
{
	FString jsonStr;
	if (!FJsonSerializer::Serialize(jsonObj.ToSharedRef(), TJsonWriterFactory<>::Create(&jsonStr, 0)))
		return false;
	return WriteStringToFile(filePath, jsonStr);
}
