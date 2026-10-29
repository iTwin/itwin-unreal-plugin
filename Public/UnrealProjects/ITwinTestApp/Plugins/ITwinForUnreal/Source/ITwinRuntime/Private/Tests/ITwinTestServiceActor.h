/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTestServiceActor.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#if WITH_TESTS

#include <ITwinServiceActor.h>

#include "ITwinTestServiceActor.generated.h"

//! Test implementation of AITwinServiceActor, used in tests to check authorization status.
//! Important: due to Unreal Engine's reflection system, this class must be defined in a header file, not
//! directly in a .cpp file.
UCLASS(NotPlaceable, Transient, Hidden)
class AITwinTestServiceActor : public AITwinServiceActor
{
	GENERATED_BODY()
public:
	virtual void UpdateOnSuccessfulAuthorization() override
	{
		bHasReceivedAuthorizationDone = true;
	}
	virtual const TCHAR* GetObserverName() const override { return TEXT("TestServiceActor"); }

	//! Just to expose CheckAuthorizationStatus (which is protected).
	AdvViz::SDK::EITwinAuthStatus CheckAuthStatus() { return CheckAuthorizationStatus(); }

	bool bHasReceivedAuthorizationDone = false;
};

#endif // WITH_TESTS
