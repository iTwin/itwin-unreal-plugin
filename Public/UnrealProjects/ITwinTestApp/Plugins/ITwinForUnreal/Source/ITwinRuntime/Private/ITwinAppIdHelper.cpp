/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAppIdHelper.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinAppIdHelper.h>
#include <ITwinServerConnection.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/Tools/Log.h>
#include <Compil/AfterNonUnrealIncludes.h>

/*static*/
bool AITwinAppIdHelper::bFreezeAppId = false;

/*static*/
void AITwinAppIdHelper::FreezeAppId()
{
	AITwinAppIdHelper::bFreezeAppId = true;
}

AITwinAppIdHelper::AITwinAppIdHelper()
{
	// Store the current value of the port.
	AuthRedirectUriPort = AITwinServerConnection::GetAuthRedirectUriPort();
}

void AITwinAppIdHelper::PostLoad()
{
	Super::PostLoad();
	// Actor has been loaded from disk.
	// Initialize the app ID only if this actor actually stores an app ID,
	// otherwise we would risk to overwrite the already-set app ID (done through external c++/blueprint call)
	// with an empty one.
	// Also test whether the AppID was not frozen (important when the authorization has been processed, with
	// the possibility to refresh the access token in background...)
	if (!AITwinAppIdHelper::bFreezeAppId)
	{
		if (!AppId.IsEmpty())
		{
			BE_LOGI("ITwinAPI", "Reloading AppID from level");

			AITwinServerConnection::SetITwinAppID(AppId);
		}
		if (AuthRedirectUriPort > 0)
		{
			BE_LOGI("ITwinAPI", "Reloading Redirect Uri Port from level: " << AuthRedirectUriPort);

			AITwinServerConnection::SetAuthRedirectUriPort(AuthRedirectUriPort);
		}
	}
}

#if WITH_EDITOR
void AITwinAppIdHelper::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// This function is called after a property has been manually changed in the Editor UI.
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (AITwinAppIdHelper::bFreezeAppId)
		return;
	if (!PropertyChangedEvent.Property)
		return;
	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AITwinAppIdHelper, AppId))
	{
		BE_LOGI("ITwinAPI", "Setting AppID from Editor");

		AITwinServerConnection::SetITwinAppID(AppId);
	}
	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AITwinAppIdHelper, AuthRedirectUriPort))
	{
		BE_LOGI("ITwinAPI", "Setting Redirect Uri Port from Editor");

		AITwinServerConnection::SetAuthRedirectUriPort(AuthRedirectUriPort);
	}
}
#endif //#if WITH_EDITOR
