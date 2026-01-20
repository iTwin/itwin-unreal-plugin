/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDecorationServiceSettings.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
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

	/// Whether an external web browser will be opened to perform the iTwin authorization. This is the
	/// default mode. If you disable it, you'll have to retrieve the URL from the instance of
	/// UITwinWebServices initiating the authorization, and process the URL in another way (using a web
	/// browser widget typically...)
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Connection",
		meta = (ConfigRestartRequired = true))
	bool bUseExternalBrowserForAuthorization = true;

	/// Can be used to customize the environments where the Decoration Service is used for scene persistence.
	/// Note that scene persistence is being migrated to a different service, so this option is just here for
	/// transitioning.
	/// Example of valid value: "DEV+QA" to enable the legacy scene persistence on both DEV and QA
	/// environments.
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Access",
		meta = (ConfigRestartRequired = true))
	FString CustomEnvsWithScenePersistenceDS = {};
};
