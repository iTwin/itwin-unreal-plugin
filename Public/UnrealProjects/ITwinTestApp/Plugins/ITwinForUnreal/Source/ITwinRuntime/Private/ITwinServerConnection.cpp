/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinServerConnection.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinServerConnection.h>
#include <ITwinServerEnvironment.h>

#include <Dom/JsonObject.h>
#include <Dom/JsonValue.h>
#include <Interfaces/IHttpResponse.h>
#include <ITwinWebServices/ITwinAuthorizationManager.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Compil/CleanUpGuard.h>
#	include <Core/ITwinAPI/ITwinWebServices.h>
#include <Compil/AfterNonUnrealIncludes.h>

DEFINE_LOG_CATEGORY(LogITwinHttp);


std::shared_ptr<std::string> AITwinServerConnection::GetAccessTokenPtr() const
{
	if (Environment == EITwinEnvironment::Invalid)
	{
		ensureMsgf(false, TEXT("Invalid environment in server connection"));
		return {};
	}
	auto const& AuthMngr = FITwinAuthorizationManager::GetInstance(
		static_cast<AdvViz::SDK::EITwinEnvironment>(Environment));
	if (!ensure(AuthMngr))
	{
		return {};
	}
	return AuthMngr->GetAccessToken();
}

FString AITwinServerConnection::GetAccessToken() const
{
	auto token = GetAccessTokenPtr();
	if (token)
		return token->c_str();
	else
		return{};

}

/// Checks the request status, response code, and logs any failure (does not assert)
/// \return Whether the request's response is valid and can be processed further
/*static*/
bool AITwinServerConnection::CheckRequest(FHttpRequestPtr const& CompletedRequest,
	FHttpResponsePtr const& Response, bool connectedSuccessfully, FString* pstrError /*= nullptr*/,
	bool const bWillRetry /*= false*/)
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

		// Used to investigate "401: unauthorized" errors (cause was apparently an obsolete token kept in the
		// FReusableJsonQueries. Might still be useful later for other 401 (or 403) errors:
		//if (401 == (int)Response->GetResponseCode())
		//	requestError += FString::Printf(TEXT(", with auth header: %s"),
		//									*CompletedRequest->GetHeader(TEXT("Authorization")));

		// see if we can get more information in the response
		std::string detailedError = AdvViz::SDK::ITwinWebServices::GetErrorDescriptionFromJson(
			TCHAR_TO_ANSI(*Response->GetContentAsString()), "\t");
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

/*static*/
void AITwinServerConnection::SetITwinAppIDArray(ITwin::AppIDArray const& ITwinAppIDs, bool bLogIDs /*= true*/)
{
	UITwinWebServices::SetITwinAppIDArray(ITwinAppIDs, bLogIDs);
}

void AITwinServerConnection::SetITwinAppID(const FString& AppID)
{
	UITwinWebServices::SetITwinAppIDArray({ TCHAR_TO_UTF8(*AppID) });
}

FString AITwinServerConnection::UrlPrefix() const
{
	return ITwinServerEnvironment::GetUrlPrefix(Environment);
}

void AITwinServerConnection::PostLoad()
{
	Super::PostLoad();

	if (Environment == EITwinEnvironment::Invalid)
	{
		Environment = UITwinWebServices::GetDefaultEnvironment();

		const FString EnvName = ITwinServerEnvironment::ToName(Environment).ToString();
		BE_LOGI("ITwinAPI", "Using iTwin environment: " << TCHAR_TO_UTF8(*EnvName));
	}
}

void AITwinServerConnection::FillAuthorizationURL()
{
	if (Environment == EITwinEnvironment::Invalid)
	{
		ensureMsgf(false, TEXT("Invalid environment in server connection"));
		return;
	}
	auto const& AuthMngr = FITwinAuthorizationManager::GetInstance(
		static_cast<AdvViz::SDK::EITwinEnvironment>(Environment));
	if (!ensure(AuthMngr))
	{
		return;
	}
	FITwinAuthorizationManager::FExternalBrowserDisabler ExternalBrowserDisabler;
	if (!AuthMngr->IsAuthorizationInProgress() && !HasAccessToken())
	{
		// No authorization started yet => initiate it now.
		AuthMngr->CheckAuthorization();
	}
	AuthorizationURL = UTF8_TO_TCHAR(AuthMngr->GetCurrentAuthorizationURL().c_str());
}

#if WITH_EDITOR
void AITwinServerConnection::PostEditChangeProperty(FPropertyChangedEvent& e)
{
	Super::PostEditChangeProperty(e);

	FName const PropertyName = (e.Property != nullptr) ? e.Property->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AITwinServerConnection, Environment)
		&& Environment != EITwinEnvironment::Invalid)
	{
		// When we explicitly modify the iTwin environment of a connection from the Editor, make it
		// the preferred environment for next PIE session...
		UITwinWebServices::SetPreferredEnvironment(Environment);
	}
}
#endif // WITH_EDITOR
