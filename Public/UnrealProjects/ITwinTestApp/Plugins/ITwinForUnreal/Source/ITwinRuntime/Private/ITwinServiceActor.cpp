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


DEFINE_LOG_CATEGORY(LogITwin);

AITwinServiceActor::AITwinServiceActor()
{

}

void AITwinServiceActor::Destroyed()
{
	Super::Destroyed();
	if (WebServices)
	{
		WebServices->SetObserver(nullptr);
	}
}

void AITwinServiceActor::UpdateWebServices()
{
	if (!ServerConnection && UITwinWebServices::GetWorkingInstance())
	{
		// Happens when the requests are made from blueprints, typically in the previous 3DFT plugin
		UITwinWebServices::GetWorkingInstance()->GetServerConnection(ServerConnection);
	}
	const bool bHasValidWebServices = WebServices && WebServices->IsValidLowLevel();
	const bool bHasChangedConnection =
		bHasValidWebServices && !WebServices->HasSameConnection(ServerConnection.Get());
	if (!bHasValidWebServices || bHasChangedConnection)
	{
		WebServices = NewObject<UITwinWebServices>(this);
		WebServices->SetServerConnection(ServerConnection);
		WebServices->SetObserver(this);
	}
}

const TCHAR* AITwinServiceActor::GetObserverName() const
{
	// NB: ideally, I would not have overridden it (but GENERATED_BODY() macro seems to impose that the class
	// can be instantiated...)
	checkNoEntry();
	return TEXT("<unknown>");
}

AITwinServiceActor::EConnectionStatus
AITwinServiceActor::CheckServerConnection(bool bRequestAuthorisationIfNeeded /*= true*/)
{
	UpdateWebServices();
	if (ServerConnection && !ServerConnection->AccessToken.IsEmpty())
	{
		// Assume the access token is valid (this is the case if the authorization is performed internally,
		// but not if the user types random character in the ServerConnection instance, of course...)
		return EConnectionStatus::Connected;
	}
	if (ensureMsgf(WebServices, TEXT("WebServices was not yet created")))
	{
		if (WebServices->IsAuthorizationInProgress())
		{
			return EConnectionStatus::InProgress;
		}
		else if (bRequestAuthorisationIfNeeded)
		{
			WebServices->CheckAuthorization();
			return EConnectionStatus::InProgress;
		}
	}
	return EConnectionStatus::NotConnected;
}

void AITwinServiceActor::UpdateOnSuccessfulAuthorization()
{

}

void AITwinServiceActor::OnAuthorizationDone(bool bSuccess, FString const& AuthError)
{
	if (bSuccess)
	{
		UpdateWebServices();

		if (ServerConnection && !ServerConnection->AccessToken.IsEmpty())
		{
			UpdateOnSuccessfulAuthorization();
		}
	}
	else
	{
		UE_LOG(LogITwinHttp, Error, TEXT("[%s] Authorization failure (%s)"), GetObserverName(), *AuthError);
	}
}

