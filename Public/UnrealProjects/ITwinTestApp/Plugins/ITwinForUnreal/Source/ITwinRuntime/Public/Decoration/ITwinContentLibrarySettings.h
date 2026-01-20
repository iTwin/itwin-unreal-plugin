/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinContentLibrarySettings.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ITwinContentLibrarySettings.generated.h"


/**
 * Stores runtime settings for the iTwin Content Library.
 */
UCLASS(Config = Engine, GlobalUserConfig, meta = (DisplayName = "iTwin Content Library"))
class ITWINRUNTIME_API UITwinContentLibrarySettings : public UDeveloperSettings {
	GENERATED_UCLASS_BODY()

public:
	/// Custom path to user-defined material collections (where individual materials can be saved for use
	/// in different projects).
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Material",
		meta = (ConfigRestartRequired = true))
	FString CustomMaterialLibraryDirectory = {};

	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "Debug",
		meta = (ConfigRestartRequired = true))
	bool DisplayAnimPathDebug = false;
};
