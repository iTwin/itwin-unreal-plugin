/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinIModelSettings.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ITwinIModelSettings.generated.h"

UENUM(BlueprintType)
enum class EITwin4DGlTFTranslucencyRule : uint8
{
	/// Emit a separate glTF tuner rule per translucent Element (no grouping)
	PerElement,
	/// Emit separate glTF tuner rules so that Elements are grouped when they are animated by the same set
	/// of translucency-needing timelines
	PerTimeline,
	/// All non-transformed translucency-needing Elements can be grouped together by the glTF tuner
	Unlimited,
};

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

	/// Used to initialize Cesium tilesets' ForbidHoles setting
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	bool CesiumForbidHoles = false;

	/// Used to initialize Cesium tilesets' MaximumSimultaneousTileLoads setting
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	int CesiumMaximumSimultaneousTileLoads = 20;

	/// Used to initialize Cesium tilesets' LoadingDescendantLimit setting
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	int CesiumLoadingDescendantLimit = 20;

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

	/// When replaying a 4D animation, shadows need to be updated regularly to keep in sync with Elements
	/// visibility. This is the minimum delay between two such updates, to control the trade-off between
	/// graphics performance and shadows consistency.
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	int IModelForceShadowUpdatesMillisec = 1000;

	//! When true, Synchro4D schedule queries and loading will not happen unless the auto-load flag is checked again
	//! on the iModel after it has been loaded or created
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	bool bIModelOverrideDisableAutoLoad4DSchedules = false;

	/**
	 * From ACesium3DTileset::MaximumScreenSpaceError:
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

	/// Maximum number of items per page for 4D queries that support pagination.
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	int Synchro4DQueriesDefaultPagination = 10000;

	/// Maximum number of items per page for 4D animation binding queries (maximized at 50,000 for v6.5 schedules,
	/// but note that v10+ schedules only support a maximum of 10,000).
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	int Synchro4DQueriesBindingsPagination = 50000;

	/// Maximum number of items per page for iModel Elements metadata queries: 32,000 is the maximum in order to
	/// stay below the server's 8MB reply size limit, but it may be necessary to use a lower value when requests
	/// failures occur because of poor connectivity or server load (the codename for these requests to look for in
	/// the logs is "InfosToQueryIModel"). Leave the default value of "0" to let the plugin decide the best pagination
	/// depending on the configuration of the SQL queries.
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	int IModelDataQueriesPagination = 0;

	/// Defines grouping of translucency-needing Elements
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	EITwin4DGlTFTranslucencyRule Synchro4DGlTFTranslucencyRule = EITwin4DGlTFTranslucencyRule::Unlimited;

	/// Disable application of color highlights on animated Elements
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	bool bSynchro4DDisableColoring = false;

	/// Disable application of all visibility effects on animated Elements: see details
	/// on UITwinSynchro4DSchedules::bDisableVisibilities
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	bool bSynchro4DDisableVisibilities = false;

	/// Disable application of partial visibility (translucency) effects on animated Elements
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	bool bSynchro4DDisablePartialVisibilities = false;

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

	/// When both a Legacy and a NextGen schedules are available for an iModel actor,
	/// we cannot use both so we have to choose.
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	bool bSynchro4DFavorNextGenSchedule = false;

	/// Enable material tuning features (the possibility to tweak some material settings such as color or
	/// textures, based on the original material definitions existing in the iModel).
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin",
		meta = (ConfigRestartRequired = true))
	bool bEnableMaterialTuning = false;

	/// Work-in-progress features.
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin",
		meta = (ConfigRestartRequired = true))
	bool bEnableWIPFeatures = false;
};
