/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinServerConnection.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinServerConnection.h>
#include <ITwinServerEnvironment.h>

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/Compil/CleanUpGuard.h>
#include <Compil/AfterNonUnrealIncludes.h>

#define LRTUHTTP_LOG(FORMAT, ...) UE_LOG(LrtuServer, Display, FORMAT, ##__VA_ARGS__)
DEFINE_LOG_CATEGORY(LrtuServer);

/// Checks the request status, response code, and logs any failure (does not assert)
/// \return Whether the request's response is valid and can be processed further
/*static*/
bool AITwinServerConnection::CheckRequest(FHttpRequestPtr const& CompletedRequest,
										  FHttpResponsePtr const& Response,
										  bool connectedSuccessfully,
										  FString* pstrError /*= nullptr*/)
{
	FString requestError;

	Be::CleanUpGuard FillErrorCleanup([&requestError, &CompletedRequest, pstrError]
	{
		if (!requestError.IsEmpty())
		{
			UE_LOG(LrtuServer, Error, TEXT("Request to %s failed with %s"),
				*CompletedRequest->GetURL(), *requestError);
		}
		if (pstrError)
		{
			*pstrError = requestError;
		}
	});

	if (!connectedSuccessfully) // Response is nullptr
	{
		requestError = EHttpRequestStatus::ToString(CompletedRequest->GetStatus());
		return false;
	}
	else if (!EHttpResponseCodes::IsOk(Response->GetResponseCode()))
	{
		requestError = FString::Printf(TEXT("code %d: %s"),
			(int)Response->GetResponseCode(),
			*EHttpResponseCodes::GetDescription(
				(EHttpResponseCodes::Type)Response->GetResponseCode()).ToString());
		return false;
	}
	else
	{
		return true;
	}
}

/*static*/
void AITwinServerConnection::SetITwinAppIDArray(ITwin::AppIDArray const& ITwinAppIDs)
{
	UITwinWebServices::SetITwinAppIDArray(ITwinAppIDs);
}

void AITwinServerConnection::SetITwinAppID(const FString& AppID)
{
	UITwinWebServices::SetITwinAppIDArray({AppID});
}

FString AITwinServerConnection::UrlPrefix() const
{
	return ITwinServerEnvironment::GetUrlPrefix(Environment);
}
