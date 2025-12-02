/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinHDRILibrary.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CoreMinimal.h>
#include <AssetRegistry/AssetData.h>
#include <ItwinRuntime/public/Decoration/ItwinDecorationHelper.h>
#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Tools/Error.h>
#	include <SDK/Core/ITwinAPI/ITwinScene.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

class UTextureCube;
namespace ITwin
{
	static const TCHAR* HDRI_LIBRARY = TEXT("HDRILibrary");

#define HDRI_JSON_BASENAME "hdri.json"
	static const TCHAR* HDRI_ICON_BASENAME = TEXT("icon.png");

	ITWINRUNTIME_API void GetAssetDataInDirectory(const FString& CurrentDirPath, const UClass& Class, TArray<FAssetData>& OutAssetData);
}

class ITWINRUNTIME_API FITwinHDRILibrary
{
public:
	using ITwinHDRI = AdvViz::SDK::ITwinHDRISettings;

	struct ExportError
	{
		/**
		 * @brief A human-readable explanation of what failed.
		 */
		std::string message = "";
		/**
		 * @brief Whether the export was aborted by user.
		 */
		bool bIsUserCancel = false;
	};

	using ExportResult = AdvViz::expected<void,ExportError>;

	//! Export the given HDRI settings to disk. If the process succeeds, a .json description will be generated
	static ExportResult ExportHDRIToDisk(AITwinDecorationHelper const* persistanceMngr, ITwinHDRI const& HDRISettings, FString const& HDRIName, FString const& DestinationFolder);

	//! Returns the path to user-defined HDRI library. The folder may not exist.
	static FString GetCustomHDRIPath();
	static ITwinHDRI ConvertKeyValueMapToDRISettings(const TMap<FString, FString> &HDRIParameters);

	struct LoadHdriResult
	{
		UTextureCube* tc = nullptr;
		bool hasSettings = false;
		ITwinHDRI settings;
	};
	static LoadHdriResult GetHrdiFromName(AITwinDecorationHelper const* persistanceMngr, FString NewHDRIName);

#if WITH_EDITOR
	// Import all stom HDRIs found in the user HDRI library path into the project HDRI library
	static void ImportJsonToLibrary(AITwinDecorationHelper const* persistanceMngr);
#endif

static std::vector<std::pair<std::string,bool>> GetListOfHDRIPresets();
};