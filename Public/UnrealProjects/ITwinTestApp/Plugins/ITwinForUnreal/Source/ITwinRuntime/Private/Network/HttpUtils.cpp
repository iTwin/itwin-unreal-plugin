/*--------------------------------------------------------------------------------------+
|
|     $Source: HttpUtils.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "HttpUtils.h"

#include <ITwinWebServices/ITwinWebServices.h>

#include <Interfaces/IHttpRequest.h>
#include <Interfaces/IHttpResponse.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Util/CleanUpGuard.h>
#	include <Core/ITwinAPI/ITwinWebServices.h>
#include <Compil/AfterNonUnrealIncludes.h>

namespace ITwinHttp {

FString DescribeTransportFailure(FHttpRequestPtr const& Request,
	ITwinHttp::ConnectionSuccess const bConnectedSuccessfully, FHttpResponsePtr const& Response/*= {}*/)
{
	TArray<FString> Details;
	Details.Reserve(8);
	Details.Emplace(FString::Printf(TEXT("connected=%s"), bConnectedSuccessfully ? TEXT("true") : TEXT("false")));
	if (Request)
	{
		Details.Emplace(FString::Printf(TEXT("verb=%s"), *Request->GetVerb()));
		Details.Emplace(FString::Printf(TEXT("url=%s"), *Request->GetURL()));
		Details.Emplace(FString::Printf(TEXT("status=%s"),
			EHttpRequestStatus::ToString(Request->GetStatus())));
		Details.Emplace(FString::Printf(TEXT("elapsed=%.3fs"), Request->GetElapsedTime()));
		FString const Correlation = Request->GetHeader(TEXT("X-Correlation-ID"));
		if (!Correlation.IsEmpty())
		{
			Details.Emplace(FString::Printf(TEXT("correlation=%s"), *Correlation));
		}
		if (Request->GetStatus() == EHttpRequestStatus::Failed)
		{
			Details.Emplace(FString::Printf(TEXT("reason=%s"),
				LexToString(Request->GetFailureReason())));
		}
	}
	if (Response.IsValid())
	{
		Details.Emplace(FString::Printf(TEXT("code=%d"), Response->GetResponseCode()));
	}
	else
	{
		Details.Emplace(TEXT("response=invalid"));
	}
	return FString::Printf(TEXT("Connection to the server failed (%s)"), *FString::Join(Details, TEXT(", ")));
}

/// Checks the request status, response code, and logs any failure (does not assert)
/// \return Whether the request's response is valid and can be processed further
/*static*/
bool CheckRequest(FHttpRequestPtr const& CompletedRequest, FHttpResponsePtr const& Response,
	ConnectionSuccess const bConnectedSuccessfully, FString* pstrError/*= nullptr*/, RetryQuery bWillRetry/*false*/)
{
	FString requestError;

	Be::CleanUpGuard FillErrorCleanup([&requestError, &CompletedRequest, pstrError, bWillRetry]
	{
		if (!requestError.IsEmpty() && UITwinWebServices::ShouldLogErrors())
		{
			FString const Correlation = CompletedRequest->GetHeader(TEXT("X-Correlation-ID"));
			if (bWillRetry)
			{
				BE_LOGW("ITwinAPI", "Request failed (but will retry), to "
					<< TCHAR_TO_UTF8(*CompletedRequest->GetURL()) << ", X-Correlation-ID="
					<< TCHAR_TO_UTF8(*Correlation) << ", with " << TCHAR_TO_UTF8(*requestError));
			}
			else
			{
				BE_LOGE("ITwinAPI", "Request to " << TCHAR_TO_UTF8(*CompletedRequest->GetURL())
					<< ", X-Correlation-ID=" << TCHAR_TO_UTF8(*Correlation)
					<< ", failed with " << TCHAR_TO_UTF8(*requestError));
			}
		}
		if (pstrError)
		{
			*pstrError = requestError;
		}
	});

	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		requestError = DescribeTransportFailure(CompletedRequest, bConnectedSuccessfully, Response);
		return false;
	}
	else if (!EHttpResponseCodes::IsOk(Response->GetResponseCode()))
	{
		requestError = FString::Printf(TEXT("code %d: %s"),
			(int)Response->GetResponseCode(),
			*EHttpResponseCodes::GetDescription(
				(EHttpResponseCodes::Type)Response->GetResponseCode()).ToString());

		// Used to investigate "401: unauthorized" errors (cause was apparently an obsolete token kept in the
		// FReusableJsonQueries. Might still be useful later for other 401 (or 403) errors:
		//if (401 == (int)Response->GetResponseCode())
		//	requestError += FString::Printf(TEXT(", with auth header: %s"),
		//									*CompletedRequest->GetHeader(TEXT("Authorization")));

		// see if we can get more information in the response
		std::string detailedError = AdvViz::SDK::ITwinWebServices::GetErrorDescriptionFromJson(
			TCHAR_TO_UTF8(*Response->GetContentAsString()), "\t");
		if (!detailedError.empty())
		{
			requestError += detailedError.c_str();
		}
		return false;
	}
	else
	{
		return true;
	}
}

bool EraseURLParameter(FString const& Url, FString const& EraseParam, FString* pRedatedUrl)
{
	int32 ViewPos = -1;
	if (Url.FindChar(TEXT('?'), ViewPos))
	{
		bool bFound = false;
		FStringView UrlView(Url);
		UrlView.RightChopInline(ViewPos +/*skip '?'*/1);
		int32 FullUrlPos = ViewPos + 1; // maintain matching pos in Url
		if (!UrlView.StartsWith(EraseParam))
		{
			while (UrlView.FindChar(TEXT('&'), ViewPos))
			{
				UrlView.RightChopInline(ViewPos +/*skip '&'*/1);
				FullUrlPos += ViewPos + 1;
				if (UrlView.StartsWith(EraseParam))
				{
					bFound = true;
					break;
				}
			}
		}
		else
			bFound = true;
		if (bFound)
		{
			if (pRedatedUrl)
			{
				if (pRedatedUrl != &Url)
					*pRedatedUrl = Url;
				if (UrlView.FindChar(TEXT('&'), ViewPos))
				{
					// more parameters follow: erase until and including the next '&'
					pRedatedUrl->RemoveAt(FullUrlPos, ViewPos + 1);
				}
				else
				{
					// Last param: erase it, as well as the '?' or '&' before it
					pRedatedUrl->LeftChopInline(FullUrlPos - 1);
				}
			}
			return true;
		}
	}
	return false;
}

bool EraseURLParameter(FString& Url, FString const& EraseParam)
{
	return EraseURLParameter(Url, EraseParam, &Url);
}

} // ns ITwinHttp
