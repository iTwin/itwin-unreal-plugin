/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDecorationServiceSettings.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ITwinDecorationServiceSettings.generated.h"

/**
 * Stores runtime settings for the iTwin Decoration Service.
 */
UCLASS(Config = Engine, GlobalUserConfig, meta = (DisplayName = "iTwin Decoration Service"))
class ITWINRUNTIME_API UITwinDecorationServiceSettings : public UDeveloperSettings {
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Connection",
		meta = (ConfigRestartRequired = true))
	bool UseLocalServer = false;

	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Connection",
		meta = (ConfigRestartRequired = true))
	int LocalServerPort = 8080;

	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Connection",
		meta = (ConfigRestartRequired = true))
	FString CustomServer = {};


	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Connection",
		meta = (ConfigRestartRequired = true))
	FString CustomUrlApiPrefix = {};

	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Access",
		meta = (ConfigRestartRequired = true))
	FString ExtraITwinScope;

	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Access",
		meta = (ConfigRestartRequired = true))
	bool EarlyAccessProgram = false;
};
