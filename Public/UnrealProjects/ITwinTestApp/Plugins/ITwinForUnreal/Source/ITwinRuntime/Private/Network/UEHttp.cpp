/*--------------------------------------------------------------------------------------+
|
|     $Source: UEHttp.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "UEHttp.h"

#include <HAL/PlatformProcess.h>
#include <HttpManager.h>
#include <HttpModule.h>
#include <Interfaces/IHttpResponse.h>
#include <Misc/Base64.h>
#include <Misc/EngineVersionComparison.h>
#include <Misc/FileHelper.h>
#include "Tasks/Task.h"

/*static*/
void FUEHttp::Init()
{
	Http::SetNewFct([]() {
		return static_cast<Http*>(new FUEHttp);
	});
}

FUEHttp::FUEHttp()
{}

void FUEHttp::SetExecuteAsyncCallbackInGameThread(bool bInExecAsyncCallbackInGameThread)
{
	bExecAsyncCallbackInGameThread = bInExecAsyncCallbackInGameThread;
}

namespace
{
	using FSharedRequest = TSharedRef<IHttpRequest, ESPMode::ThreadSafe>;

	static constexpr int MAX_REQUEST_RETRY = 4;

	inline bool ShouldAbort(FSharedRequest& HttpRequest, EHttpRequestStatus::Type& status, int& retryCount)
	{
		status = HttpRequest->GetStatus();

		// Retry request in case of connection error
	#if UE_VERSION_OLDER_THAN(5, 4, 0) // UE5.3
		if (status == EHttpRequestStatus::Failed_ConnectionError)
	#else // ie UE5.4+
		if (status == EHttpRequestStatus::Failed
			&& EHttpFailureReason::ConnectionError == HttpRequest->GetFailureReason())
	#endif
		{
			// POST is not safe to retry, it will potentially create new resources.
			if (retryCount < MAX_REQUEST_RETRY && HttpRequest->GetVerb() != "POST")
			{
				HttpRequest->ProcessRequest();
				retryCount++;
				return false;
			}
			else
				return true;
		}

		return (status == EHttpRequestStatus::Failed);
	}
}
FUEHttp::Response FUEHttp::Do(FString verb, const std::string& url, const BodyParams& bodyParams,
	const Headers& headers /*= {}*/, bool isFullUrl /*= false*/, std::function<void(const Response&)> callbackFct /*= {}*/)
{
	//auto HttpRequest = FHttpModule::Get().CreateRequest();
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetVerb(verb);
	HttpRequest->SetURL((isFullUrl ? url : (GetBaseUrlStr() + '/' + url)).c_str());
	for (auto const& [Key, Value] : headers)
	{
		HttpRequest->SetHeader(Key.c_str(), Value.c_str());
	}

	if (!bodyParams.empty())
	{
		if (bodyParams.GetEncoding() == AdvViz::SDK::Tools::EStringEncoding::Utf8)
			HttpRequest->SetContentAsString(UTF8_TO_TCHAR(bodyParams.str().c_str()));
		else
			HttpRequest->SetContentAsString(bodyParams.str().c_str());
	}

	if (callbackFct)
	{
		HttpRequest->OnProcessRequestComplete().BindLambda(
			[callbackFct, bExecAsyncCallbackInGT = this->bExecAsyncCallbackInGameThread](FHttpRequestPtr pRequest,
				FHttpResponsePtr pResponse,
				bool connectedSuccessfully){
					int32 code = 0;
					if (connectedSuccessfully && pRequest->GetStatus() == EHttpRequestStatus::Succeeded) {
						code = pResponse->GetResponseCode();
						FString outputString(std::move(pResponse->GetContentAsString()));
						// In Unreal, request callbacks are executed in game thread (which is fine, as a lot
						// of operations regarding actors and world require this...)
						// I'm not sure why we would prefer to start a thread here, but I added an option to
						// avoid breaking anything. TODO_JDE discuss this option.
						if (bExecAsyncCallbackInGT)
						{
							std::string s(TCHAR_TO_UTF8(*outputString));
							Response response(code, std::move(s));
							callbackFct(response);
						}
						else
						{
							UE::Tasks::Launch(UE_SOURCE_LOCATION,
								[callbackFct, code, outputString]() mutable {
								std::string s(TCHAR_TO_UTF8(*outputString));
								Response response(code, std::move(s));
								callbackFct(response); },
								UE::Tasks::ETaskPriority::Normal);
							}
					}
				}
		);

		bool bStartedRequest = HttpRequest->ProcessRequest();
		if (!bStartedRequest)
		{
			BE_LOGE("http", "Failed to start HTTP Request.");
		}
		return Response(0, std::string(""));
	}
	else
	{
		bool bStartedRequest = HttpRequest->ProcessRequest();
		if (!bStartedRequest)
		{
			BE_LOGE("http", "Failed to start HTTP Request.");
			return Response(0, std::string(""));
		}

		if (IsInGameThread())
		{
			FHttpModule::Get().GetHttpManager().Flush(EHttpFlushReason::Default);
		}

		TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> response;
		int32 code = 0;
		int32 counter = 60 * 60 * 1000; // 1h timeout to prevent potential infinite loop (FHttpModule has its own timeout)
		EHttpRequestStatus::Type status;
		int retryCount = 0;
		// wait a valid response
		while (true)
		{
			if (ShouldAbort(HttpRequest, status, retryCount))
				break;

			response = HttpRequest->GetResponse();
			if (response.IsValid())
				code = response->GetResponseCode();

			if (code != 0)
				break;
			FPlatformProcess::Sleep(0.001);

			counter--;
			if (!counter)
				break;
		}

		if (code && response.IsValid())
		{
			FString outputString = response->GetContentAsString();
			std::string s(TCHAR_TO_UTF8(*outputString));
			return Response(response->GetResponseCode(), std::move(s));
		}

		if (counter == 0)
			return Response(408, std::string(""));

		return Response(0, std::string(""));
	}
}

namespace
{
	static TArray<uint8> FStringToUint8(const FString& InString)
	{
		TArray<uint8> OutBytes;

		// Handle empty strings
		if (InString.Len() > 0)
		{
			FTCHARToUTF8 Converted(*InString); // Convert to UTF8
			OutBytes.Append(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
		}

		return OutBytes;
	}

	static FString AddData(FString BoundaryBegin, FString Name, FString Value) {
		return FString(TEXT("\r\n"))
			+ BoundaryBegin
			+ FString(TEXT("Content-Disposition: form-data; name=\""))
			+ Name
			+ FString(TEXT("\"\r\n\r\n"))
			+ Value;
	}
}

FUEHttp::Response FUEHttp::DoFile(FString verb, const std::string& url, const std::string& fileParamName, const std::string& filePath,
	const KeyValueVector& extraParams /*= {}*/, const Headers& headers /*= {}*/)
{
	// inspired from: https://dev.epicgames.com/community/learning/tutorials/R6rv/unreal-engine-upload-an-image-using-http-post-request-c
	FString FileName(UTF8_TO_TCHAR(fileParamName.c_str()));

	auto HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetVerb(verb);
	HttpRequest->SetURL((GetBaseUrlStr() + '/' + url).c_str());
	for (auto const& [Key, Value] : headers)
	{
		HttpRequest->SetHeader(Key.c_str(), Value.c_str());
	}

	FString BoundaryLabel = FString();
	FString BoundaryBegin = FString();
	FString BoundaryEnd = FString();

	// Create a boundary label, for the header
	BoundaryLabel = FString(TEXT("e543322540af456f9a3773049ca02529-")) + FString::FromInt(FMath::Rand());
	// boundary label for begining of every payload chunk 
	BoundaryBegin = FString(TEXT("--")) + BoundaryLabel + FString(TEXT("\r\n"));
	// boundary label for the end of payload
	BoundaryEnd = FString(TEXT("\r\n--")) + BoundaryLabel + FString(TEXT("--\r\n"));

	// Set the content-type for server to know what are we going to send
	HttpRequest->SetHeader(TEXT("Content-Type"), FString(TEXT("multipart/form-data; boundary=")) + BoundaryLabel);

	// This is binary content of the request
	TArray<uint8> CombinedContent;

	TArray<uint8> FileRawData;
	FString FullFilePath(UTF8_TO_TCHAR(filePath.c_str()));
	FFileHelper::LoadFileToArray(FileRawData, *FullFilePath);

	// First, we add the boundary for the file, which is different from text payload
	FString FileBoundaryString = FString(TEXT("\r\n"))
		+ BoundaryBegin
		//+ FString(TEXT("Content-Disposition: form-data; name=\"uploadFile\"; filename=\""))
		//+ FileName + "\"\r\n"
		+ FString(TEXT("Content-Disposition: form-data; name=\"file\"; filename=\"file\"")) 
		+ "\r\n"
		+ "Content-Type: application/octet-stream"+ TEXT("\r\n")
		+ "Content-Transfer-Encoding: binary"
		+ FString(TEXT("\r\n\r\n"));

	// Notice, we convert all strings into uint8 format using FStringToUint8
	CombinedContent.Append(FStringToUint8(FileBoundaryString));

	// Append the file data
	CombinedContent.Append(FileRawData);

	// Let's add couple of text values to the payload
	for (auto& k : extraParams)
		CombinedContent.Append(FStringToUint8(AddData(BoundaryBegin, UTF8_TO_TCHAR(k.first.c_str()), UTF8_TO_TCHAR(k.second.c_str()))));

	// Finally, add a boundary at the end of the payload
	CombinedContent.Append(FStringToUint8(BoundaryEnd));

	// Set the request content
	HttpRequest->SetContent(CombinedContent);


	// Send the request 
	bool bStartedRequest = HttpRequest->ProcessRequest();
	if (!bStartedRequest)
	{
		BE_LOGE("http", "Failed to start HTTP Request.");
		return Response(0, std::string(""));
	}

	if (IsInGameThread())
	{
		FHttpModule::Get().GetHttpManager().Flush(EHttpFlushReason::Default);
	}
		
	TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> response;
	int32 code = 0;
	int32 counter = 30*60 * 1000; // 30 minutes timeout

	EHttpRequestStatus::Type status;
	int retryCount = MAX_REQUEST_RETRY; // no retry for upload
	// wait a valid response
	while (true)
	{
		if (ShouldAbort(HttpRequest, status, retryCount))
			break;

		response = HttpRequest->GetResponse();
		if (response.IsValid())
			code = response->GetResponseCode();

		if (code != 0)
			break;
		FPlatformProcess::Sleep(0.001);

		counter--;
		if (!counter)
			break;
	}
	if (code && response.IsValid())
	{
		auto& content = response->GetContent();
		std::string s((char*)content.GetData(), content.Num());
		return Response(response->GetResponseCode(), std::move(s));
	}

	if (counter == 0)
		return Response(408, std::string(""));

	return Response(0, std::string(""));
}

bool FUEHttp::DecodeBase64(const std::string& SrcString, RawData& Buffer) const
{
	Buffer.clear();

	TArray<uint8> DataBuffer;
	if (!FBase64::Decode(FString(SrcString.c_str()), DataBuffer))
	{
		return false;
	}
	Buffer.reserve(DataBuffer.Num());
	for (uint8 c : DataBuffer)
	{
		Buffer.push_back(static_cast<uint8_t>(c));
	}
	return true;
}


