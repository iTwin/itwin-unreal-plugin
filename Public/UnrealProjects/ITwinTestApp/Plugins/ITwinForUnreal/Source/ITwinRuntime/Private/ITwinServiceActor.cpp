/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinServiceActor.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <ITwinServiceActor.h>
#include <ITwinServerConnection.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <Engine/World.h>


DEFINE_LOG_CATEGORY(LogITwin);

AITwinServiceActor::AITwinServiceActor()
{

}

void AITwinServiceActor::BeginDestroy()
{
	if (WebServices)
	{
		// Make sure we won't execute any Http request callback while the object is half-destroyed, as it can
		// make Unreal crash on exit, *included* during the packaging (and thus block it randomly...)
		WebServices->SetObserver(nullptr);
	}
	Super::BeginDestroy();
}

void AITwinServiceActor::UpdateWebServices()
{
	if (!ServerConnection)
	{
		// Happens when the requests are made from blueprints, typically in the previous 3DFT plugin
		// Also happens in Carrot, with the new startup panel (for good reasons).
		UITwinWebServices::GetActiveConnection(ServerConnection, GetWorld());
	}
	const bool bHasValidWebServices = WebServices && WebServices->IsValidLowLevel();
	const bool bHasChangedConnection =
		bHasValidWebServices && !WebServices->HasSameConnection(ServerConnection.Get());
	const bool bHasObserver =
		bHasValidWebServices && WebServices->HasObserver(this);
	if (!bHasValidWebServices || bHasChangedConnection || !bHasObserver)
	{
		WebServices = NewObject<UITwinWebServices>(this);
		WebServices->SetServerConnection(ServerConnection);
		WebServices->SetObserver(this);
	}
}

#if WITH_TESTS
void AITwinServiceActor::SetTestMode(FString const& ServerUrl)
{
	// Set a fake access token, to prevent the AuthorizationManager from trying to retrieve a real token
	FITwinAuthorizationManager::GetInstance(SDK::Core::EITwinEnvironment::Prod)->SetOverrideAccessToken("TestToken");
	// Create ServerConnection & WebServices pointing to the mock server.
	ServerConnection = NewObject<AITwinServerConnection>(this);
	ServerConnection->Environment = EITwinEnvironment::Prod;
	WebServices = NewObject<UITwinWebServices>(this);
	WebServices->SetServerConnection(ServerConnection);
	WebServices->SetTestServerURL(ServerUrl);
	WebServices->SetObserver(this);
}
#endif // WITH_TESTS

const UITwinWebServices* AITwinServiceActor::GetWebServices() const
{
	return WebServices.Get();
}

UITwinWebServices* AITwinServiceActor::GetMutableWebServices()
{
	return WebServices.Get();
}

const TCHAR* AITwinServiceActor::GetObserverName() const
{
	// NB: ideally, I would not have overridden it (but GENERATED_BODY() macro seems to impose that the class
	// can be instantiated...)
	checkNoEntry();
	return TEXT("<unknown>");
}

SDK::Core::EITwinAuthStatus AITwinServiceActor::CheckServerConnection(bool bRequestAuthorisationIfNeeded /*= true*/)
{
	UpdateWebServices();
	if (ServerConnection && ServerConnection->HasAccessToken())
	{
		// Assume the access token is valid (this is the case if the authorization is performed internally,
		// but not if the user types random character in the ServerConnection instance, of course...)
		return SDK::Core::EITwinAuthStatus::Success;
	}
	if (ensureMsgf(WebServices, TEXT("WebServices was not yet created")))
	{
		if (WebServices->IsAuthorizationInProgress())
		{
			return SDK::Core::EITwinAuthStatus::InProgress;
		}
		else if (bRequestAuthorisationIfNeeded)
		{
			return WebServices->CheckAuthorizationStatus();
		}
	}
	return SDK::Core::EITwinAuthStatus::None;
}

void AITwinServiceActor::UpdateOnSuccessfulAuthorization()
{

}

void AITwinServiceActor::OnAuthorizationDone(bool bSuccess, std::string const& AuthError)
{
	if (bSuccess)
	{
		UpdateWebServices();

		if (ServerConnection && ServerConnection->HasAccessToken())
		{
			UpdateOnSuccessfulAuthorization();
		}
	}
	else
	{
		BE_LOGE("ITwinAPI", "[" << TCHAR_TO_UTF8(GetObserverName()) << "] Authorization failure: " << AuthError);
	}
}

