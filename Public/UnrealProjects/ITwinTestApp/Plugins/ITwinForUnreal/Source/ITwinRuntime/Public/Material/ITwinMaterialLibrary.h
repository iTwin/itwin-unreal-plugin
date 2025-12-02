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
class AITwinDecorationHelper;

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

	//! Must be called before loading any material.
	static void InitPaths(AITwinDecorationHelper const& DecoHelper);

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
		MaterialPersistencePtr const& MatIOMngr,
		FString const* DestinationJsonPath = nullptr);

	//! Returns the path to user-defined material library. The folder may not exist.
	static const FString& GetCustomLibraryPath();

	//! Returns the path to iTwinEngage's material library.
	static const FString& GetBentleyLibraryPath();

	//! Given a relative path (eg. "Landscape/Grass"), returns the path to be used to load the material
	//! asset - depending on the current content installation mode, it can be either an Unreal path (starting
	//! with "/Game/", and without any extension), or an absolute path to a material.json file.
	static FString GetBeLibraryPathForLoading(const FString& RelativeMaterialName);

	//! Returns whether the Bentley Material Library is installed in a separate directory, and not
	//! packaged withing the application as before.
	static bool UseExternalPathForBentleyLibrary();

	//! Returns true if JSON format should be used for Bentley Material Library.
	static bool UseJsonFormatForBentleyLibrary();

	//! Parse the given directory, detecting custom material definitions (valid material.json files) if any.
	static int32 ParseJsonMaterialsInDirectory(FString const& DirectoryPath,
		TArray<FAssetData>& OutAssetDataArray);

	static AdvViz::expected<void, std::string> RemoveCustomMaterial(const FString& AssetPath);

	static AdvViz::expected<void, std::string> RenameCustomMaterial(const FString& OldAssetPath,
		const FString& NewDisplayName, const FString& NewDirectory, bool bOverwrite);

#if WITH_EDITOR
	static bool ImportJsonToLibrary(FString const& JsonPath);

	static bool ConvertAssetToJson(FString const& AssetPath);
#endif

private:
	//! Root path for the official iTwinEngage material library.
	static FString BeMatLibraryRootPath;
};

// Can be activated to generate the material library from a collection previously exported in iTwinEngage.
#define CREATE_ITWIN_MATERIAL_LIBRARY() 0

// dev tool to convert the .uasset material definitions back to Json (for future refactoring with
// the component center, because individual .pak files do not seem to support non-Unreal files such
// as png/jpg textures, so in the end we will totally bypass the Unreal asset format for our
// basic material collection)
#define RESAVE_ITWIN_MATERIAL_LIBRARY_AS_JSON() 0
