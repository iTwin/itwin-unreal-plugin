/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMaterialLibrary.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CoreMinimal.h>
#include <AssetRegistry/AssetData.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Tools/Error.h>
#	include <SDK/Core/Visualization/TextureKey.h>
#	include <SDK/Core/Visualization/TextureUsage.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <set>

class AITwinIModel;

namespace AdvViz::SDK
{
	struct ITwinMaterial;
	class MaterialPersistenceManager;
}

namespace ITwin
{
	static const TCHAR* MAT_LIBRARY = TEXT("MaterialLibrary");

	#define MATERIAL_JSON_BASENAME "material.json"
	static const TCHAR* MAT_ICON_BASENAME = TEXT("icon.png");
}

class ITWINRUNTIME_API FITwinMaterialLibrary
{
public:
	using ITwinMaterial = AdvViz::SDK::ITwinMaterial;
	using TextureKeySet = AdvViz::SDK::TextureKeySet;
	using TextureUsageMap = AdvViz::SDK::TextureUsageMap;
	using MaterialPersistencePtr = std::shared_ptr<AdvViz::SDK::MaterialPersistenceManager>;

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

	//! Returns true if a material file already exists in given directory.
	//! In such case, OutExistingFilePath will be assigned the corresponding file path.
	static bool MaterialExistsInDir(FString const& MaterialFolder, FString& OutExistingFilePath);

	using ExportResult = AdvViz::expected<
		void,
		ExportError>;

	//! Export the given material to disk. If the process succeeds, a .json description will be generated
	//! as well as textures in the destination folder passed as parameter.
	static ExportResult ExportMaterialToDisk(AITwinIModel const& OwnerIModel, uint64_t MaterialId,
		FString const& MaterialName, FString const& DestinationFolder, bool bPromptBeforeOverwrite = true);

	//! Loads a material definition from an asset file.
	static bool LoadMaterialFromAssetPath(FString const& AssetPath, ITwinMaterial& OutMaterial,
		TextureKeySet& OutTexKeys, TextureUsageMap& OutTextureUsageMap,
		AdvViz::SDK::ETextureSource& OutTexSource,
		MaterialPersistencePtr const& MatIOMngr);

	//! Returns the path to user-defined material library. The folder may not exist.
	static FString GetCustomLibraryPath();

	//! Parse the given directory, detecting custom material definitions (valid material.json files) if any.
	static int32 ParseJsonMaterialsInDirectory(FString const& DirectoryPath,
		TArray<FAssetData>& OutAssetDataArray);

	static AdvViz::expected<void, std::string> RemoveCustomMaterial(const FString& AssetPath);

	static AdvViz::expected<void, std::string> RenameCustomMaterial(const FString& OldAssetPath,
		const FString& NewDisplayName, const FString& NewDirectory, bool bOverwrite);

#if WITH_EDITOR
	static bool ImportJsonToLibrary(FString const& JsonPath);
#endif
};
