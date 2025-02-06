/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinIModelSettings.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
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

	/// Whether to enable point-and-click selection on iModel meshes: this requires the creation of special
	/// "physics" meshes that can adversely impact performance and memory footprint on large models. Set to
	/// false if you know you won't need collision nor selection in the 3D viewport.
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	bool IModelCreatePhysicsMeshes = true;

	/**
	 * From AITwinCesium3DTileset::MaximumScreenSpaceError:
	 *
	 * The maximum number of pixels of error when rendering this tileset.
	 *
	 * This is used to select an appropriate level-of-detail: A low value
	 * will cause many tiles with a high level of detail to be loaded,
	 * causing a finer visual representation of the tiles, but with a
	 * higher performance cost for loading and rendering. A higher value will
	 * cause a coarser visual representation, with lower performance
	 * requirements.
	 */
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	double TilesetMaximumScreenSpaceError = 16.0;

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

	/// Enable prediction of materials based on an iTwin Machine Learning api. The api is still under
	/// development. It requires some specific scopes to be added to your iTwin App.
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin",
		meta = (ConfigRestartRequired = true))
	bool bEnableML_MaterialPrediction = false;

	/// Whether materials can be exported locally (internal tool).
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin",
		meta = (ConfigRestartRequired = true))
	bool bEnableMaterialExport = false;
};
