/*--------------------------------------------------------------------------------------+
|
|     $Source: UEHttp.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "UEHttp.h"
#include "UEHttpAdapter.h" // just for ConvertUnrealHttpResponse

#include <HAL/PlatformProcess.h>
#include <HttpManager.h>
#include <HttpModule.h>
#include <Interfaces/IHttpResponse.h>
#include <Misc/Base64.h>
#include <Misc/EngineVersionComparison.h>
#include <Misc/FileHelper.h>
#include "Tasks/Task.h"
#include "GenericPlatform/GenericPlatformHttp.h"

/*static*/
void FUEHttp::Init()
{
	Http::SetNewFct([]() {
		return static_cast<Http*>(new FUEHttp);
	});
}

FUEHttp::FUEHttp()
{}

namespace
{
	using FSharedRequest = TSharedRef<IHttpRequest, ESPMode::ThreadSafe>;

	static constexpr int MAX_REQUEST_RETRY = 4;
	static const long HTTP_CONNECT_ERR = -2; // use same code as in #FUEHttpRequest...

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
	const Headers& headers /*= {}*/, bool isFullUrl /*= false*/,
	std::function<void(const Response&)> callbackFct /*= {}*/,
	EAsyncCallbackExecutionMode asyncCBExecMode /*= Default*/)
{
	using namespace AdvViz::SDK;
	//auto HttpRequest = FHttpModule::Get().CreateRequest();
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetVerb(verb);
	if (!isFullUrl && (url.starts_with("http:") || url.starts_with("https:")))
		isFullUrl = true;

	HttpRequest->SetURL((isFullUrl ? url : (GetBaseUrlStr() + '/' + url)).c_str());
	for (auto const& [Key, Value] : headers)
	{
		HttpRequest->SetHeader(Key.c_str(), Value.c_str());
	}

	if (!bodyParams.empty())
	{
		if (bodyParams.GetEncoding() == Tools::EStringEncoding::Utf8)
			HttpRequest->SetContentAsString(UTF8_TO_TCHAR(bodyParams.str().c_str()));
		else
			HttpRequest->SetContentAsString(bodyParams.str().c_str());
	}

	if (callbackFct)
	{
		// By default in Unreal, request callbacks are executed in game thread (which is fine, as a lot
		// of operations regarding actors and world require this...). Keep this policy if the requested
		// callback execution mode is 'GameThread'.
		if (asyncCBExecMode == EAsyncCallbackExecutionMode::GameThread)
		{
			HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
		}
		else
		{
			// Reduce latency, doesn't wait the next GT tick
			HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
		}

		HttpRequest->OnProcessRequestComplete().BindLambda([callbackFct, asyncCBExecMode]
			(FHttpRequestPtr pRequest, FHttpResponsePtr pResponse, bool connectedSuccessfully)
				{
					auto Response = ConvertUnrealHttpResponse({}, pResponse, connectedSuccessfully);
					if (asyncCBExecMode == EAsyncCallbackExecutionMode::GameThread)
					{
						callbackFct(std::move(Response));
					}
					else
					{
						UE::Tasks::Launch(UE_SOURCE_LOCATION,
							[callbackFct, Response = std::move(Response)]() mutable
							{
								callbackFct(std::move(Response));
							},
							UE::Tasks::ETaskPriority::Normal);
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
		BE_ASSERT(!IsInGameThread()); // bad practice to do sync requests in the game thread!
		std::mutex mtx;
		std::condition_variable cv;
		bool completed = false;
		bool connectedSuccessfully = false;

		HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
		HttpRequest->OnProcessRequestComplete().BindLambda(
			[&cv, &mtx, &completed, &connectedSuccessfully](FHttpRequestPtr pRequest,
				FHttpResponsePtr pResponse,
				bool connectedSuccessfully2) {
				{
					std::lock_guard<std::mutex> lock(mtx);
					completed = true;
					connectedSuccessfully = connectedSuccessfully2;
				}
				cv.notify_one();
			});

		bool bStartedRequest = HttpRequest->ProcessRequest();
		if (!bStartedRequest)
		{
			BE_LOGE("http", "Failed to start HTTP Request:" << url);
			return Response(0, std::string(""));
		}

		std::unique_lock<std::mutex> lock(mtx);
		cv.wait_for(lock, std::chrono::hours(1), [&completed]() { return completed; });

		return ConvertUnrealHttpResponse({}, HttpRequest->GetResponse(), connectedSuccessfully);
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
	const KeyValueVector& extraParams /*= {}*/, const Headers& headers /*= {}*/,
	std::function<void(const Response&)> callbackFct /*= {}*/,
	EAsyncCallbackExecutionMode asyncCBExecMode /*= Default*/)
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


	if (callbackFct)
	{
		// Asynchronous mode.
		if (asyncCBExecMode == EAsyncCallbackExecutionMode::GameThread)
		{
			HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
		}
		else
		{
			HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
		}

		HttpRequest->OnProcessRequestComplete().BindLambda(
			[callbackFct, asyncCBExecMode](FHttpRequestPtr pRequest,
										   FHttpResponsePtr pResponse,
										   bool connectedSuccessfully) 
		{
			const int32 code = (connectedSuccessfully && pResponse)
				? pResponse->GetResponseCode()
				: HTTP_CONNECT_ERR;
			FString outputString;
			if (connectedSuccessfully && pRequest->GetStatus() == EHttpRequestStatus::Succeeded)
			{
				outputString = pResponse->GetContentAsString();
			}
			// Same remark as in FUEHttp::Do...
			if (asyncCBExecMode == EAsyncCallbackExecutionMode::GameThread)
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
		);

		bool bStartedRequest = HttpRequest->ProcessRequest();
		if (!bStartedRequest)
		{
			BE_LOGE("http", "Failed to start HTTP File Request.");
		}
		return Response(0, std::string(""));
	}

	BE_ASSERT(!IsInGameThread()); // bad practice to do sync requests in the game thread!

	// Start the request and run it in a synchronous way
	bool bStartedRequest = HttpRequest->ProcessRequest();
	if (!bStartedRequest)
	{
		BE_LOGE("http", "Failed to start HTTP File Request.");
		return Response(0, std::string(""));
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
		FPlatformProcess::Sleep(0.01);
		if (IsInGameThread())
			FHttpModule::Get().GetHttpManager().Tick(0.01);

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

std::string FUEHttp::EncodeForUrl(const std::string& str) const
{
	FString outputString = FGenericPlatformHttp::UrlEncode(UTF8_TO_TCHAR(str.c_str()));
	std::string s(TCHAR_TO_UTF8(*outputString));
	return s;
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


