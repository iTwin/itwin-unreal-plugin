/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAppIdHelper.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
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

void AITwinAppIdHelper::PostLoad()
{
	Super::PostLoad();
	// Actor has been loaded from disk.
	// Initialize the app ID only if this actor actually stores an app ID,
	// otherwise we would risk to overwrite the already-set app ID (done through external c++/blueprint call)
	// with an empty one.
	// Also test whether the AppID was not frozen (important when the authorization has been processed, with
	// the possibility to refresh the access token in background...)
	if (!AppId.IsEmpty() && !AITwinAppIdHelper::bFreezeAppId)
	{
		BE_LOGI("ITwinAPI", "Reloading AppID from level");

		AITwinServerConnection::SetITwinAppID(AppId);
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
}
#endif //#if WITH_EDITOR
