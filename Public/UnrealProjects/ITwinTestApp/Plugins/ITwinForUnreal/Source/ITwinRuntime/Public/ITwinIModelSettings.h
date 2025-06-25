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

	//! When false, Synchro4D schedule queries and loading will not happen. If some queries have been already
	//! started, setting to false will not prevent their replies from being handled, but no new query will be
	//! emitted: they will be stacked and should restart correctly when the flag is set to true again
	//! (UNTESTED though). It is recommended to set to false before the actor starts ticking, or at least
	//! before the iModel Elements metadata have finished querying/loading.
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	bool bIModelAutoLoadSynchro4DSchedules = true;

	/// Use official api.bentley.com 4D endpoints rather than the legacy internal ES-API endpoints.
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	bool bSynchro4DUseAPIM = true;

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
	int Synchro4DQueriesBindingsPagination = 50000;
	
	/// Use glTF tuning for animation of translucent or transformed Elements
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin")
	bool bSynchro4DUseGltfTunerInsteadOfMeshExtraction = true;

	/// Defines grouping of translucency-needing Elements when using
	/// bSynchro4DUseGltfTunerInsteadOfMeshExtraction
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

	/// Enable prediction of materials based on an iTwin Machine Learning api. The api is still under
	/// development. It requires some specific scopes to be added to your iTwin App.
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin",
		meta = (ConfigRestartRequired = true))
	bool bEnableML_MaterialPrediction = false;

	/// Work-in-progress features.
	UPROPERTY(
		Config,
		EditAnywhere,
		BlueprintReadOnly,
		Category = "iTwin",
		meta = (ConfigRestartRequired = true))
	bool bEnableWIPFeatures = false;
};
