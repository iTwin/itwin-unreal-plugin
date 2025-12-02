/*--------------------------------------------------------------------------------------+
|
|     $Source: UEHttpAdapter.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "UEHttpAdapter.h"
#include "HttpUtils.h"

#include <Interfaces/IHttpResponse.h>
#include <HttpModule.h>

class FUEHttpRequest::FImpl
{
public:
	FImpl()
		: UERequest(FHttpModule::Get().CreateRequest())
	{
	}

	void SetVerb(AdvViz::SDK::EVerb eVerb)
	{
		UERequest->SetVerb(ITwinHttp::GetVerbString(eVerb));
	}

	static const long HTTP_CONNECT_ERR = -2;

	void SetResponseCallback(
		AdvViz::SDK::HttpRequest::RequestPtr const& requestPtr,
		AdvViz::SDK::HttpRequest::ResponseCallback const& callback)
	{
		UERequest->OnProcessRequestComplete().BindLambda(
			[this, ResultCallback = callback, RequestPtr = requestPtr]
			(FHttpRequestPtr pUERequest, FHttpResponsePtr UEResponse, bool connectedSuccessfully)
		{
			AdvViz::SDK::Http::Response Response;
			if (connectedSuccessfully)
			{
				Response.first = UEResponse->GetResponseCode();
				Response.second = TCHAR_TO_UTF8(*UEResponse->GetContentAsString());
				if (RequestPtr && RequestPtr->NeedRawData() && UEResponse->GetContentLength() > 0)
				{
					// In case we receive some binary data, we may want to get it and not just a (truncated)
					// string...
					const TArray<uint8>& content = UEResponse->GetContent();
					Response.rawdata_ = std::make_shared<AdvViz::SDK::Http::RawData>();
					AdvViz::SDK::Http::RawData& rawdata(*Response.rawdata_);
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

	void Process(AdvViz::SDK::Http const& http,
				 std::string const& url,
				 AdvViz::SDK::Http::BodyParams const& bodyParams,
				 AdvViz::SDK::Http::Headers const& headers,
				 bool isFullUrl)
	{
		std::string FullURL;
		if (isFullUrl)
			FullURL = url;
		else
			FullURL = http.GetBaseUrl() + url;
		UERequest->SetURL(FullURL.c_str());
		for (auto const& [Key, Value] : headers)
		{
			UERequest->SetHeader(Key.c_str(), Value.c_str());
		}
		if (!bodyParams.empty())
		{
			if (bodyParams.GetEncoding() == AdvViz::SDK::Tools::EStringEncoding::Utf8)
				UERequest->SetContentAsString(UTF8_TO_TCHAR(bodyParams.str().c_str()));
			else
				UERequest->SetContentAsString(bodyParams.str().c_str());
		}
		UERequest->ProcessRequest();
	}

	bool CheckResponse(
		AdvViz::SDK::Http::Response const& response,
		std::string& requestError) const
	{
		if (response.first == HTTP_CONNECT_ERR)
		{
			FString const UEError = EHttpRequestStatus::ToString(UERequest->GetStatus());
			requestError = TCHAR_TO_UTF8(*UEError);
			return false;
		}
		else if (!EHttpResponseCodes::IsOk(response.first))
		{
			FString const UEError = FString::Printf(TEXT("code %d: %s"),
				(int)response.first,
				*EHttpResponseCodes::GetDescription(
					(EHttpResponseCodes::Type)response.first).ToString());
			requestError = TCHAR_TO_UTF8(*UEError);
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
	AdvViz::SDK::Http& http,
	std::string const& url,
	AdvViz::SDK::Http::BodyParams const& body,
	AdvViz::SDK::Http::Headers const& headers,
	bool isFullUrl /*= false*/)
{
	Impl->Process(http, url, body, headers, isFullUrl);
}

bool FUEHttpRequest::CheckResponse(Response const& response, std::string& requestError) const
{
	return Impl->CheckResponse(response, requestError);
}

void FUEHttpRequest::DoSetVerb(AdvViz::SDK::EVerb verb)
{
	Impl->SetVerb(verb);
}

void FUEHttpRequest::DoSetResponseCallback(ResponseCallback const& callback)
{
	Impl->SetResponseCallback(shared_from_this(), callback);
}

