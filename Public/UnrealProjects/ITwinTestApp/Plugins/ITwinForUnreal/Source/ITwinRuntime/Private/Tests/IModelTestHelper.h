/*--------------------------------------------------------------------------------------+
|
|     $Source: IModelTestHelper.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#if WITH_TESTS

#include <CoreMinimal.h>
#include <ITwinIModel.h>
#include <Decoration/ITwinDecorationHelper.h>
#include <filesystem>
#include <memory>
#include <BeHeaders/Util/CleanUpGuard.h>

struct FITwinExportInfo;
class FITwinFileBasedMockServer;

struct FIModelTestOptions
{
	bool bEnableRendering = true;
	bool bEnableMaterialTuning = true;
	bool bDisableSynchro4DAutoLoad = false;

	bool bUseLatestChangeset = false;
	bool bDiscoverExport = false;
	bool bDiscoverDefaultScene = false;
	bool bAutoLoadSavedViews = false;
};

class FIModelTestHelper
{
public:
	struct FIModelLoadingResult
	{
		TObjectPtr<AITwinIModel> IModel;
		TObjectPtr<AITwinDecorationHelper> DecoHelper;
		bool bShouldLoadScene = false;
		std::unique_ptr<FITwinFileBasedMockServer> MockServer;

		std::unique_ptr<Be::CleanUpGuard> CleanupGuard;
	};
	using FOptions = FIModelTestOptions;

	//! Initiates loading of an iModel in the given world, returning a result that contains a cleanup guard
	//! and the decoration helper.
	//! \param InRelativeCacheFolder Relative path to the folder containing the cached responses for the mock
	//! server, relative to the plugin's Resources folder.
	//! \param SceneId The scene ID to load. If empty, the default scene will be loaded.
	//! \param bEnableRendering If true, a camera and a light will be added to the world to enable rendering.
	static FIModelLoadingResult InitiateIModelLoading(UWorld* World,
		FITwinExportInfo const& IModelInfo,
		std::filesystem::path const& InRelativeCacheFolder,
		FString const& SceneId,
		FOptions const& Options = {});
};

#endif // WITH_TESTS
