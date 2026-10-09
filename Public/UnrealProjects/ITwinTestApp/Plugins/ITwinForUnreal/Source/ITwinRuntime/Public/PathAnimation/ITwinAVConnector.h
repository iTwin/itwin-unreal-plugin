/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAVConnector.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <GameFramework/Actor.h>
#include <PathAnimation/ITwinAVPath.h>
#include "ITwinAVConnector.generated.h"

class UITwinAnimPathHelper;

UCLASS()
class ITWINRUNTIME_API AITwinAVConnector : public AITwinAVPath
{
	GENERATED_BODY()

public:
	virtual ~AITwinAVConnector() = default;
	virtual bool IsReady() { return false; }
	// Returns length of the loaded vehicle in centimeters, or 0.f if no vehicle is loaded
	virtual float LoadArticulatedVehicleForPath(UITwinAnimPathHelper* /*PathHelper*/, FString /*AssetPath*/, int32 /*Lane*/) { return 0.f; }
	virtual void DeleteArticulatedVehiclesForPath(UITwinAnimPathHelper* /*PathHelper*/, FString /*AssetPath*/) {}
	virtual void DeleteArticulatedVehicles(UITwinAnimPathHelper* /*PathHelper*/) {}
	virtual void SetTimeOffset(UITwinAnimPathHelper* /*PathHelper*/, int /*VehicleIdx*/, float /*TimeOffset*/) {}
	virtual FBox GetBBoxOfLoadedVehicle(FString /*AssetPath*/) { return FBox(); }
	virtual int GetArticulatedVehicleCount(const UITwinAnimPathHelper* /*PathHelper*/) const { return 0; }
	//virtual void UpdateArticulatedVehicles(float DeltaTime, bool bTimelineMode) {}
};
