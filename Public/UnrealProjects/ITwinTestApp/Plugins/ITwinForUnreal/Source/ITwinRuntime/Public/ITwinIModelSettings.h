/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinIModelSettings.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ITwinIModelSettings.generated.h"

/// Stores runtime settings for iModels, including 4D scheduling
UCLASS(Config = Engine, GlobalUserConfig, meta = (DisplayName = "iTwin iModel and 4D"))
class ITWINRUNTIME_API UITwinIModelSettings : public UDeveloperSettings
{
	GENERATED_UCLASS_BODY()

public:
	/// Maximum Cesium memory cache size in megabytes, used to initialize Cesium tilesets' MaximumCachedBytes
	/// setting
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	int CesiumMaximumCachedMegaBytes = 1024;

	/// Maximum iModel Elements metadata & schedule data filesystem cache size in megabytes, used to cache
	/// on the local disk the data queried from the web apis
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	int IModelMaximumCachedMegaBytes = 4096;

	/// Split applying animation on Elements among subsequent ticks to avoid spending more than this amount
	/// of time each time. Visual update only occurs once the whole iModel (?) has been updated, though.
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	int Synchro4DMaxTimelineUpdateMilliseconds = 50;

	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	int Synchro4DQueriesDefaultPagination = 10000;

	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	int Synchro4DQueriesBindingsPagination = 30000;
	
	/// Disable application of color highlights on animated Elements
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	bool bSynchro4DDisableColoring = false;

	/// Disable application of partial visibility on animated Elements
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	bool bSynchro4DDisableVisibilities = false;

	/// Disable the cutting planes used to simulate the Elements' "growth" (construction/removal/...)
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	bool bSynchro4DDisableCuttingPlanes = false;

	/// Disable the scheduled animation of Elements' transformations (like movement along 3D paths)
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	bool bSynchro4DDisableTransforms = false;
	
	/// Mask out entirely the tiles just loaded that do not have the 4D animation fully applied. This is
	/// actually a global flag that will apply to all iModels.
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	bool bSynchro4DMaskTilesUntilFullyAnimated = false;
};
