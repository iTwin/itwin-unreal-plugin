/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMaterialLibrary.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#pragma once

#include <CoreMinimal.h>

#include <SDK/Core/Tools/Error.h>
#include <SDK/Core/Visualization/TextureKey.h>
#include <set>

class AITwinIModel;

namespace SDK::Core
{
	struct ITwinMaterial;
}

namespace ITwin
{
	static const TCHAR* MAT_LIBRARY = TEXT("MaterialLibrary");
}

class ITWINRUNTIME_API FITwinMaterialLibrary
{
public:
	using ITwinMaterial = SDK::Core::ITwinMaterial;
	using TextureKeySet = SDK::Core::TextureKeySet;

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

	using ExportResult = SDK::expected<
		void,
		ExportError>;

	//! Export the given material to disk. If the process succeeds, a .json description will be generated
	//! as well as textures in the destination folder passed as parameter.
	static ExportResult ExportMaterialToDisk(AITwinIModel const& OwnerIModel, uint64_t MaterialId,
		FString const& MaterialName, FString const& DestinationFolder,
		bool bPromptBeforeOverwrite = true);

	//! Loads a material definition from an asset file.
	static bool LoadMaterialFromAssetPath(FString const& AssetPath, ITwinMaterial& OutMaterial,
		TextureKeySet& OutTextureKeys);

#if WITH_EDITOR
	static bool ImportJsonToLibrary(FString const& JsonPath);
#endif
};
