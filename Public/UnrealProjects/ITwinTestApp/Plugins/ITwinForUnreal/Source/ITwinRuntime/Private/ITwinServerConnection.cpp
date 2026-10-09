/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinServerConnection.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinServerConnection.h>
#include <ITwinServerEnvironment.h>

#include <ITwinWebServices/ITwinAuthorizationManager.h>
#include <ITwinWebServices/ITwinWebServices.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/ITwinAPI/ITwinWebServices.h>
#include <Compil/AfterNonUnrealIncludes.h>

DEFINE_LOG_CATEGORY(LogITwinHttp);

/*static*/
std::shared_ptr<AdvViz::SDK::ThreadSafeAccessToken> AITwinServerConnection::GetAccessTokenPtrForEnv(EITwinEnvironment Environment)
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

std::shared_ptr<AdvViz::SDK::ThreadSafeAccessToken> AITwinServerConnection::GetAccessTokenPtr() const
{
	return GetAccessTokenPtrForEnv(this->Environment);
}

FString AITwinServerConnection::GetAccessToken() const
{
	auto token = GetAccessTokenPtr();
	if (token)
	{
		auto tokenPtr = token->Get();
		if (tokenPtr)
			return tokenPtr->c_str();
	}
	return {};
}

/*static*/
void AITwinServerConnection::SetITwinAppIDArray(ITwin::AppIDArray const& ITwinAppIDs, bool bLogIDs /*= true*/)
{
	UITwinWebServices::SetITwinAppIDArray(ITwinAppIDs, bLogIDs);
}

/*static*/
void AITwinServerConnection::SetITwinAppID(const FString& AppID)
{
	UITwinWebServices::SetITwinAppIDArray({ TCHAR_TO_UTF8(*AppID) });
}

/*static*/
int AITwinServerConnection::GetAuthRedirectUriPort()
{
	return AdvViz::SDK::ITwinAuthManager::GetRedirectUriPort();
}
/*static*/
void AITwinServerConnection::SetAuthRedirectUriPort(int Port)
{
	AdvViz::SDK::ITwinAuthManager::SetRedirectUriPort(Port);
}

/*static*/
void AITwinServerConnection::Logout()
{
	const AdvViz::SDK::EITwinEnvironment Env =
		static_cast<AdvViz::SDK::EITwinEnvironment>(UITwinWebServices::GetDefaultEnvironment());
	if (Env == AdvViz::SDK::EITwinEnvironment::Invalid)
	{
		ensureMsgf(false, TEXT("wrong default env"));
		BE_LOGE("ITwinAPI", "Logout failure: invalid environment");
		return;
	}
	auto const& AuthMngr = FITwinAuthorizationManager::GetInstance(Env);
	if (!ensure(AuthMngr))
	{
		BE_LOGE("ITwinAPI", "Logout failure: no authorization manager");
		return;
	}
	if (AuthMngr->Logout())
	{
		BE_LOGI("ITwinAPI", "Successfully logged out");
	}
	else
	{
		BE_LOGE("ITwinAPI", "Logout failure");
	}
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
