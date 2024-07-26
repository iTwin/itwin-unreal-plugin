/*--------------------------------------------------------------------------------------+
|
|     $Source: ReusableJsonQueriesRecording.inl $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "ReusableJsonQueriesImpl.h"

#include <Interfaces/IHttpResponse.h>
#include <Misc/FileHelper.h>

bool FRecordDirIterator::Visit(const TCHAR* Filename, bool bIsDirectory) /*override*/
{
	if (bIsDirectory)
		return true;
	TArray<FString> OutArray;
	if (FPaths::GetBaseFilename(FString(Filename)).ParseIntoArray(OutArray, TEXT("_")) <= 1)
		return false;
	errno = 0;
	auto const Timestamp = FCString::Strtoi(*OutArray[0], nullptr, 10);
	if (errno != 0)
		return false;
	FString FileContent;
	if (!FFileHelper::LoadFileToString(FileContent, Filename))
		return false;
	auto Reader = TJsonReaderFactory<TCHAR>::Create(FileContent);
	TSharedPtr<FJsonObject> JsonObject;
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
		return false;
	if (OutArray.Num() == 2 && OutArray[1] == TEXT("query")) // query
	{
		FString Url, Verb, Payload;
		if (!JsonObject->TryGetStringField(TEXT("url"), Url)
			|| !JsonObject->TryGetStringField(TEXT("verb"), Verb)
			|| (Verb != TEXT("GET") && Verb != TEXT("POST")))
		{
			return false;
		}
		if (Verb == TEXT("POST"))
		{
			if (!JsonObject->TryGetStringField(TEXT("payload"), Payload))
				return false;
			if (!ReplayMap.try_emplace(Timestamp, ReusableJsonQueries::FQueryKey(Url, Payload)).second)
				return false;
			if (!SimulationMap.try_emplace(ReusableJsonQueries::FQueryKey(Url, Payload), 
											TSharedPtr<FJsonObject>())
				.second)
			{
				return false;
			}
		}
		else // GET
		{
			if (!ReplayMap.try_emplace(Timestamp, Url).second)
				return false;
			if (!SimulationMap.try_emplace(Url, TSharedPtr<FJsonObject>()).second)
				return false;
		}
	}
	else if (OutArray.Num() == 4 && OutArray[1] == TEXT("reply") && OutArray[2] == TEXT("to")) // reply
	{
		if (!ReplayMap.try_emplace(Timestamp, JsonObject).second)
			return false;
		errno = 0;
		auto const ReplyToTimestamp = FCString::Strtoi(*OutArray[3], nullptr, 10);
		if (errno != 0 || ReplyToTimestamp <= Timestamp)
			return false;
		int32 ToQueryPersisted = -1;
		if (!JsonObject->TryGetNumberField(TEXT("toQuery"), ToQueryPersisted))
			return false;
		if (ReplyToTimestamp != ToQueryPersisted)
			return false;
		auto QueryInReplay = ReplayMap.find(ReplyToTimestamp);
		if (ReplayMap.end() == QueryInReplay)
			return false;
		bool bWrongType = false;
		ReusableJsonQueries::FSimulationMap::iterator SimuMapIt = SimulationMap.end();
		std::visit([&bWrongType, &SimuMapIt, this](auto&& Var)
			{
				using T = std::decay_t<decltype(Var)>;
				if constexpr (std::is_same_v<T, FString>)
					SimuMapIt = SimulationMap.find(Var);
				else if constexpr (std::is_same_v<T, ReusableJsonQueries::FQueryKey>)
					SimuMapIt = SimulationMap.find(Var);
				else if constexpr (std::is_same_v<T, TSharedPtr<FJsonObject>>)
					bWrongType = true;
				else static_assert(always_false_v<T>, "non-exhaustive visitor!");
			},
			QueryInReplay->second);
		if (bWrongType || SimulationMap.end() == SimuMapIt)
			return false;
		bool bConnectionWasSuccessful = false;
		if (!JsonObject->TryGetBoolField(TEXT("connectedSuccessfully"), bConnectionWasSuccessful))
			return false;
		if (bConnectionWasSuccessful)
		{
			TSharedPtr<FJsonObject> const* ReplyObj;
			if (JsonObject->TryGetObjectField(TEXT("reply"), ReplyObj))
				SimuMapIt->second = *ReplyObj;
			else
				return false;
		}
	}
	else
		return false;
	return true;
}

template<uint16_t SimultaneousRequestsT>
void FReusableJsonQueries<SimultaneousRequestsT>::FImpl::RecordReply(FHttpResponsePtr const Response,
	bool const bConnectedSuccessfully, int const QueryTimestamp, ReusableJsonQueries::FLock&)
{
	auto JsonObj = MakeShared<FJsonObject>();
	JsonObj->SetNumberField(TEXT("toQuery"), QueryTimestamp);
	JsonObj->SetBoolField(TEXT("connectedSuccessfully"), bConnectedSuccessfully);
	if (Response)
		JsonObj->SetNumberField(TEXT("responseCode"), Response->GetResponseCode());
	FString JsonString;
	auto JsonWriter = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonString);
	FJsonSerializer::Serialize(JsonObj, JsonWriter);
	if (Response && bConnectedSuccessfully) // otherwise Response->GetContentAsString is html, not json...
	{
		int32 FinalClosingBrace;
		if (JsonString.FindLastChar(TEXT('}'), FinalClosingBrace))
		{
			JsonString[FinalClosingBrace] = TEXT(',');
			FString ContentAsString = Response->GetContentAsString();
			JsonString.Reserve(JsonString.Len() + 20/*actually a bit less*/ + ContentAsString.Len());
			JsonString += LINE_TERMINATOR;
			JsonString += TEXT("\t\"reply\": ");
			JsonString += LINE_TERMINATOR;
			JsonString += std::move(ContentAsString);
			JsonString += LINE_TERMINATOR;
			JsonString += TEXT('}');
		}
		else check(false);
	}
	FString const Path = RecorderPathBase + FString::Printf(TEXT("%06d_reply_to_%06d.json"),
															 RecorderTimestamp++, QueryTimestamp);
	FFileHelper::SaveStringToFile(JsonString, *Path);
}

template<uint16_t SimultaneousRequestsT>
TSharedRef<FJsonObject> FReusableJsonQueries<SimultaneousRequestsT>::FImpl::ToJson(FRequestPtr const& Req,
	FString const& FullUrl, FString const& PostContentString)
{
	auto JsonObj = MakeShared<FJsonObject>();
	JsonObj->SetStringField(TEXT("url"), FullUrl);
	JsonObj->SetStringField(TEXT("verb"), Req->GetVerb());
	if (!PostContentString.IsEmpty())
		JsonObj->SetStringField(TEXT("payload"), PostContentString);
	TArray<TSharedPtr<FJsonValue>> HeadersJson;
	// Saving "Authorization" header may not be a good idea + they're all the same anyway so let's save only
	// for the very first query of each session:
	if (0 == RecorderTimestamp)
	{
		for (auto&& Header : Req->GetAllHeaders())
		{
			static const FString BEARER = TEXT("bearer ");
			static const FString AUTH = TEXT("authorization:");
			int32 const BearerFound = Header.Find(BEARER);
			if (INDEX_NONE != BearerFound)
			{
				Header.LeftInline(BearerFound + BEARER.Len());
				Header += TEXT(" _bearer token expurgated from json_");
			}
			else
			{
				int32 const AuthFound = Header.Find(AUTH);
				if (INDEX_NONE != AuthFound)
				{
					Header.LeftInline(AuthFound + AUTH.Len());
					Header += TEXT(" _authorization expurgated from json_");
				}
			}
			HeadersJson.Add(MakeShared<FJsonValueString>(Header));
		}
	}
	JsonObj->SetArrayField(TEXT("headers"), HeadersJson);
	return JsonObj;
}

template<uint16_t SimultaneousRequestsT>
void FReusableJsonQueries<SimultaneousRequestsT>::FImpl::RecordQuery(TSharedRef<FJsonObject> const JsonObj,
																	 ReusableJsonQueries::FLock&)
{
	FString const Path = RecorderPathBase + FString::Printf(TEXT("%06d_query.json"), RecorderTimestamp++);
	FString JsonString;
	auto JsonWriter = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonString);
	FJsonSerializer::Serialize(JsonObj, JsonWriter);
	FFileHelper::SaveStringToFile(JsonString, *Path);
}

template<uint16_t SimultaneousRequestsT>
std::function<TSharedPtr<FJsonObject>(FHttpResponsePtr)> FReusableJsonQueries<SimultaneousRequestsT>::FImpl
	::GetJsonObjGetterForSimulation(FPoolRequest& FromPool, bool const bVerbIsGET)
{
	// We can capture FromPool by ref, its scope is that of the FReusableJsonQueries itself
	return [this, &FromPool, bVerbIsGET] (FHttpResponsePtr) -> TSharedPtr<FJsonObject>
		{
			ReusableJsonQueries::FSimulationMap::iterator It;
			if (bVerbIsGET)
			{
				It = SimulationMap.find(FromPool.Request->GetURL());
			}
			else
			{
				auto&& ContentAsArray = FromPool.Request->GetContent();
				It = SimulationMap.find(ReusableJsonQueries::FQueryKey(
					FromPool.Request->GetURL(),
					FString(ContentAsArray.Num(), UTF8_TO_TCHAR(ContentAsArray.GetData()))));
			}
			if (SimulationMap.end() != It)
				return It->second;
			else
			{
				UE_LOG(ITwinS4DQueries, Warning, TEXT("SimulationMode: no reply found for '%s %s'!"),
					*FromPool.Request->GetVerb(), *FromPool.Request->GetURL());
				return TSharedPtr<FJsonObject>();
			}
		};
}
