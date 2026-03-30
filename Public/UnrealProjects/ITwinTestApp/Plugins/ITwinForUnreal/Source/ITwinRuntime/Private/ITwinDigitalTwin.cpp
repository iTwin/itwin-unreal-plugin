/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDigitalTwin.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinDigitalTwin.h>


AITwinDigitalTwin::AITwinDigitalTwin()
{
	// By default, we load all the models discovered in the iTwin. This can now be changed from the Editor,
	// Blueprint or C++
	SetAutoLoadAllComponents(true);
}

void AITwinDigitalTwin::OnITwinInfoRetrieved(bool bSuccess, AdvViz::SDK::ITwinInfo const& Info)
{
	Super::OnITwinInfoRetrieved(bSuccess, Info);

#if WITH_EDITOR
	// In Editor, we will name the actor like the iTwin.
	if (!GetITwinName().IsEmpty())
	{
		SetActorLabel(GetITwinName());
	}
#endif
}
