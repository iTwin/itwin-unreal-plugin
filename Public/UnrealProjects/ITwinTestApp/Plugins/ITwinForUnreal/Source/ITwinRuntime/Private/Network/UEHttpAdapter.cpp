/*--------------------------------------------------------------------------------------+
|
|     $Source: UEHttpAdapter.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "UEHttpAdapter.h"

#include <Interfaces/IHttpResponse.h>
#include <HttpModule.h>

class FUEHttpRequest::FImpl
{
public:
	FImpl()
		: UERequest(FHttpModule::Get().CreateRequest())
	{
	}

	void SetVerb(EVerb eVerb)
	{
		FString Verb;
		switch (eVerb)
		{
		case EVerb::Delete: Verb = TEXT("DELETE"); break;
		case EVerb::Get:	Verb = TEXT("GET"); break;
		case EVerb::Patch:	Verb = TEXT("PATCH"); break;
		case EVerb::Post:	Verb = TEXT("POST"); break;
		case EVerb::Put:	Verb = TEXT("PUT"); break;
		}
		UERequest->SetVerb(Verb);
	}

	static const long HTTP_CONNECT_ERR = -2;

	void SetResponseCallback(
		SDK::Core::HttpRequest::RequestPtr const& requestPtr,
		SDK::Core::HttpRequest::ResponseCallback const& callback)
	{
		UERequest->OnProcessRequestComplete().BindLambda(
			[this, ResultCallback = callback, RequestPtr = requestPtr]
			(FHttpRequestPtr UERequest, FHttpResponsePtr UEResponse, bool connectedSuccessfully)
		{
			SDK::Core::Http::Response Response;
			if (connectedSuccessfully)
			{
				Response.first = UEResponse->GetResponseCode();
				Response.second = TCHAR_TO_ANSI(*UEResponse->GetContentAsString());
				if (RequestPtr && RequestPtr->NeedRawData() && UEResponse->GetContentLength() > 0)
				{
					// In case we receive some binary data, we may want to get it and not just a (truncated)
					// string...
					const TArray<uint8>& content = UEResponse->GetContent();
					Response.rawdata_ = std::make_shared<SDK::Core::Http::RawData>();
					SDK::Core::Http::RawData& rawdata(*Response.rawdata_);
					rawdata.reserve(content.Num());
					for (uint8 b : content)
						rawdata.push_back(b);
				}
			}
			else
			{
				// Signal a connection error (see CheckResponse)
				Response.first = HTTP_CONNECT_ERR;
			}
			ResultCallback(RequestPtr, Response);
		});
	}

	void Process(SDK::Core::Http const& http,
				 std::string const& url,
				 std::string const& body,
				 SDK::Core::Http::Headers const& headers)
	{
		UERequest->SetURL((http.GetBaseUrl() + url).c_str());
		for (auto const& [Key, Value] : headers)
		{
			UERequest->SetHeader(Key.c_str(), Value.c_str());
		}
		if (!body.empty())
		{
			UERequest->SetContentAsString(body.c_str());
		}
		UERequest->ProcessRequest();
	}

	bool CheckResponse(
		SDK::Core::Http::Response const& response,
		std::string& requestError) const
	{
		if (response.first == HTTP_CONNECT_ERR)
		{
			FString const UEError = EHttpRequestStatus::ToString(UERequest->GetStatus());
			requestError = TCHAR_TO_ANSI(*UEError);
			return false;
		}
		else if (!EHttpResponseCodes::IsOk(response.first))
		{
			FString const UEError = FString::Printf(TEXT("code %d: %s"),
				(int)response.first,
				*EHttpResponseCodes::GetDescription(
					(EHttpResponseCodes::Type)response.first).ToString());
			requestError = TCHAR_TO_ANSI(*UEError);
			return false;
		}
		else
		{
			return true;
		}
	}

private:
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> const UERequest;
};

FUEHttpRequest::FUEHttpRequest()
	: Impl(MakePimpl<FUEHttpRequest::FImpl>())
{

}

void FUEHttpRequest::Process(
	SDK::Core::Http& http,
	std::string const& url,
	std::string const& body,
	SDK::Core::Http::Headers const& headers)
{
	Impl->Process(http, url, body, headers);
}

bool FUEHttpRequest::CheckResponse(Response const& response, std::string& requestError) const
{
	return Impl->CheckResponse(response, requestError);
}

void FUEHttpRequest::DoSetVerb(EVerb verb)
{
	Impl->SetVerb(verb);
}

void FUEHttpRequest::DoSetResponseCallback(ResponseCallback const& callback)
{
	Impl->SetResponseCallback(shared_from_this(), callback);
}

