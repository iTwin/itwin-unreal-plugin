/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAppIdHelper.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinAppIdHelper.h>
#include <ITwinServerConnection.h>

void AITwinAppIdHelper::PostLoad()
{
	Super::PostLoad();
	// Actor has been loaded from disk.
	// Initialize the app ID only if this actor actually stores an app ID,
	// otherwise we would risk to overwrite the already-set app ID (done through external c++/blueprint call)
	// with an empty one.
	if (!AppId.IsEmpty())
		AITwinServerConnection::SetITwinAppID(AppId);
}

#if WITH_EDITOR
void AITwinAppIdHelper::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// This function is called after a property has been manually changed in the Editor UI.
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (!PropertyChangedEvent.Property)
		return;
	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AITwinAppIdHelper, AppId))
		AITwinServerConnection::SetITwinAppID(AppId);
}
#endif //#if WITH_EDITOR
