/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDecorationServiceSettings.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ITwinDecorationServiceSettings.generated.h"


/// The iTwin scope needed to access iTwin Decorations.
#define ITWIN_DECORATIONS_SCOPE "itwin-decorations"


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
	bool bUseLocalServer = false;

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
		Category = "Connection",
		meta = (ConfigRestartRequired = true))
	FString CustomEnv = {};

	/// In case your application requires an additional iTwin scope, you can set it here. If several scopes
	/// are needed, use a white space as separator.
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Access",
		meta = (ConfigRestartRequired = true))
	FString AdditionalITwinScope = {};

	/// Enable the visualization of iTwin decorations (populations, custom materials...) attached to your
	/// iModels inside your plugin.
	/// This requires the `itwin-decorations` scope to be added to your iTwin App beforehand.
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Access",
		meta = (ConfigRestartRequired = true))
	bool bLoadDecorationsInPlugin = false;
};
