/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinWebServicesSubsystem.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CoreMinimal.h>
#include <Subsystems/EngineSubsystem.h>

#include "ITwinWebServicesSubsystem.generated.h"


UCLASS()
class ITWINRUNTIME_API UITwinWebServicesSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
private:
	FDelegateHandle WorldCleanupDelegate;
};
