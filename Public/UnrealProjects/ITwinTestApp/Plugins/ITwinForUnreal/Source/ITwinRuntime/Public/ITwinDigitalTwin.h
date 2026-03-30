/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDigitalTwin.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinDigitalTwinManager.h>

#include <ITwinDigitalTwin.generated.h>


UCLASS()
class ITWINRUNTIME_API AITwinDigitalTwin : public AITwinDigitalTwinManager
{
	GENERATED_BODY()
public:
	AITwinDigitalTwin();

private:
	/// overridden from IITwinWebServicesObserver:
	virtual void OnITwinInfoRetrieved(bool bSuccess, AdvViz::SDK::ITwinInfo const& Info) override;
};
