/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMaterialLibrary.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Material/ITwinMaterialLibrary.h>

#include <Material/ITwinMaterialDataAsset.h>
#include <ITwinIModel.h>

#include <Misc/MessageDialog.h>
#include <Misc/PathViews.h>


#if WITH_EDITOR
#include <AssetRegistry/AssetRegistryModule.h>
#include <Factories/DataAssetFactory.h>
#include <FileHelpers.h>
#include <ObjectTools.h>
#include <UObject/Package.h>
#endif // WITH_EDITOR

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <BeUtils/Gltf/GltfMaterialHelper.h>
#	include <BeUtils/Gltf/GltfMaterialTuner.h>
#	include <SDK/Core/Tools/Assert.h>
#	include <SDK/Core/Tools/Log.h>
#	include <SDK/Core/Visualization/MaterialPersistence.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <spdlog/fmt/fmt.h>
#include <fstream>

namespace ITwin
{
	static const TCHAR* MAT_LIBRARY_TAG = TEXT(ITWIN_MAT_LIBRARY_TAG);
}

/*static*/
FITwinMaterialLibrary::ExportResult FITwinMaterialLibrary::ExportMaterialToDisk(
	AITwinIModel const& IModel, uint64_t MaterialId, FString const& MaterialName,
	FString const& DestinationFolder, bool bPromptBeforeOverwrite /*= true*/)
{
	using namespace SDK::Core;

	auto const& MatIOMngr = AITwinIModel::GetMaterialPersistenceManager();
	if (!MatIOMngr)
	{
		return SDK::make_unexpected(ExportError{ "no material persistence manager!" });
	}

	std::string const IModelId = TCHAR_TO_UTF8(*IModel.IModelId);

	auto const& MatHelper = IModel.GetGltfMaterialHelper();
	if (!MatHelper)
	{
		return SDK::make_unexpected(ExportError{ std::string("no material helper in iMode l") + IModelId });
	}


	std::filesystem::path const OutputFolder = TCHAR_TO_UTF8(*DestinationFolder);

	std::error_code ec;
	if (!std::filesystem::is_directory(OutputFolder, ec)
		&& !std::filesystem::create_directories(OutputFolder, ec))
	{
		return SDK::make_unexpected(ExportError{
			fmt::format("Could not create directory {}: {}", OutputFolder.generic_string(), ec.message()) });
	}

	std::filesystem::path const jsonMaterialPath = OutputFolder / "material.json";
	if (bPromptBeforeOverwrite
		&& std::filesystem::exists(jsonMaterialPath, ec))
	{
		// Confirm before overwriting...
		FString const ExistingFilePath = UTF8_TO_TCHAR(jsonMaterialPath.generic_string().c_str());
		if (FMessageDialog::Open(EAppMsgCategory::Info,	EAppMsgType::YesNo,
			FText::FromString(
				FString::Printf(TEXT("Do you want to overwrite file %s?"), *ExistingFilePath)),
			FText::FromString(TEXT(""))) != EAppReturnType::Yes)
		{
			return SDK::make_unexpected(ExportError{ .message = "", .bIsUserCancel = true });
		}
	}

	
	// Fetch material full definition (including default values deduced from IModelRpc queries).

	ITwinMaterial matSettings;
	if (!MatHelper->GetMaterialFullDefinition(MaterialId, matSettings))
	{
		// Unknown material.
		return SDK::make_unexpected(ExportError{
			fmt::format("No material {} for iModel '{}'", MaterialId, IModelId) });
	}

	// If some textures were downloaded from iTwin API (decoration service or iModelRpc), copy them to the
	// destination folder.
	BeUtils::GltfMaterialHelper::Lock Lock(MatHelper->GetMutex());

	for (uint8_t i(0); i < (uint8_t)SDK::Core::EChannelType::ENUM_END; ++i)
	{
		SDK::Core::EChannelType const Channel = static_cast<SDK::Core::EChannelType>(i);

		std::optional<ITwinChannelMap> texMapOpt = matSettings.GetChannelMapOpt(Channel);
		if (texMapOpt && texMapOpt->HasTexture())
		{
			std::string TextureBasename = GetChannelName(Channel);
			std::filesystem::path TextureDstPath = OutputFolder;

			auto const TexAccess = MatHelper->GetTextureAccess(*texMapOpt, Lock);
			if (!TexAccess.filePath.empty())
			{
				// File is already present locally => just copy it
				TextureBasename += TexAccess.filePath.extension().generic_string();
				TextureDstPath /= TextureBasename;
				ec.clear();
				if (!std::filesystem::copy_file(TexAccess.filePath, TextureDstPath,
					std::filesystem::copy_options::overwrite_existing, ec))
				{
					return SDK::make_unexpected(ExportError{
						fmt::format("Could not copy '{}' to '{}': {} - for material {}",
							TexAccess.filePath.generic_string(),
							TextureDstPath.generic_string(),
							ec.message(),
							MaterialId) });
				}
			}
			else if (TexAccess.cesiumImage)
			{
				TextureBasename += ".png";
				TextureDstPath /= TextureBasename;
				auto SaveOpt = BeUtils::GltfMaterialTuner::SaveImageCesium(*TexAccess.cesiumImage, TextureDstPath);
				if (!SaveOpt)
				{
					return SDK::make_unexpected(ExportError{
						fmt::format("Could not save cesium image to '{}': {} - for material {}",
							 TextureDstPath.generic_string(),
							 SaveOpt.error().message,
							 MaterialId) });
				}
			}
			else
			{
				return SDK::make_unexpected(ExportError{
						fmt::format("Missing texture '{}' (source: {}) for material {}",
							 texMapOpt->texture, (uint8_t)texMapOpt->eSource, MaterialId) });
			}

			// Update the corresponding parameter in the material definition.
			// Use the 'Library' source mode.
			matSettings.SetChannelMap(Channel,
				ITwinChannelMap{
					.texture = TextureBasename,
					.eSource = ETextureSource::Library
				});
		}
	}
	// Enforce material display name
	matSettings.displayName = TCHAR_TO_UTF8(*MaterialName);

	std::string const jsonMatStr = MatIOMngr->ExportAsJson(matSettings, IModelId, MaterialId);
	if (jsonMatStr.empty())
	{
		return SDK::make_unexpected(ExportError{
			fmt::format("Failed to export material {} as JSON.", MaterialId) });
	}

	std::ofstream(jsonMaterialPath) << jsonMatStr;
	{
		ec.clear();
		if (!std::filesystem::exists(jsonMaterialPath, ec))
		{
			return SDK::make_unexpected(ExportError{
				fmt::format("Failed writing material definition in {}.", jsonMaterialPath.generic_string()) });
		}
	}
	return {};
}


//! Loads a material definition from an asset file.
/*static*/
bool FITwinMaterialLibrary::LoadMaterialFromAssetPath(FString const& AssetPath, ITwinMaterial& OutMaterial, TextureKeySet& OutTextureKeys)
{
	using namespace SDK::Core;

	auto const& MatIOMngr = AITwinIModel::GetMaterialPersistenceManager();
	if (!ensure(MatIOMngr))
	{
		return false;
	}

	UITwinMaterialDataAsset* MaterialDataAsset = LoadObject<UITwinMaterialDataAsset>(nullptr, *AssetPath);
	if (!IsValid(MaterialDataAsset))
	{
		return false;
	}

	KeyValueStringMap KeyValueMap;
	for (auto const& [StrKey, StrValue] : MaterialDataAsset->MaterialParameters)
	{
		KeyValueMap[TCHAR_TO_UTF8(*StrKey)] = TCHAR_TO_UTF8(*StrValue);
	}

	return MatIOMngr->GetMaterialSettingsFromKeyValueMap(KeyValueMap, OutMaterial, OutTextureKeys);
}


#if WITH_EDITOR

/*static*/
bool FITwinMaterialLibrary::ImportJsonToLibrary(FString const& AssetPath)
{
	using namespace SDK::Core;

	if (!AssetPath.EndsWith(TEXT(".json")))
	{
		ensureMsgf(false, *FString::Printf(TEXT("expecting a .json file and got %s"), *AssetPath));
		return false;
	}

	auto const& MatIOMngr = AITwinIModel::GetMaterialPersistenceManager();
	if (!ensure(MatIOMngr))
	{
		return false;
	}

	FStringView AssetDir, AssetName, AssetExt;
	FPathViews::Split(AssetPath, AssetDir, AssetName, AssetExt); // <asset_dir> / "material" / "json"

	FString const MatLibraryPath = FPaths::ProjectContentDir() / ITwin::MAT_LIBRARY;
	FStringView RelativePathView;
	if (!ensure(FPathViews::TryMakeChildPathRelativeTo(AssetDir, MatLibraryPath, RelativePathView)))
	{
		ensureMsgf(false, *FString::Printf(TEXT("Path %s not inside Material Library (%s)"),
			*AssetPath, *MatLibraryPath));
		return false;
	}
	FString const RelativePath = FString(RelativePathView);
	FString PackageName = FString::Printf(TEXT("/Game/%s/%s"), ITwin::MAT_LIBRARY, *RelativePath);
	PackageName = ObjectTools::SanitizeInvalidChars(PackageName, INVALID_LONGPACKAGE_CHARACTERS);

	UPackage* Package = CreatePackage(*PackageName);
	Package->FullyLoad();

	UITwinMaterialDataAsset* MaterialDataAsset = Cast<UITwinMaterialDataAsset>(Package->FindAssetInPackage());
	if (IsValid(MaterialDataAsset))
	{
		ensureMsgf(false, *FString::Printf(TEXT("Package already exists (%s) - please edit it directly"),
			*PackageName));
		return false;
	}


	// Convert JSON file exported previously for the creation of the Material Library.

	KeyValueStringMap KeyValueMap;
	if (!MatIOMngr->ConvertJsonFileToKeyValueMap(TCHAR_TO_UTF8(*AssetPath), KeyValueMap))
	{
		ensureMsgf(false, TEXT("could not parse Json material"));
		return false;
	}

	// Create and populate the map of strings containing all the material's parameters.
	UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
	MaterialDataAsset = Cast<UITwinMaterialDataAsset>(Factory->FactoryCreateNew(
		UITwinMaterialDataAsset::StaticClass(),
		Package,
		FName(FPathViews::GetPathLeaf(AssetDir)),
		RF_Public | RF_Standalone | RF_Transactional,
		nullptr,
		GWarn));
	for (auto const& [Key, Value] : KeyValueMap)
	{
		// For texture maps, add a prefix to identify them (we only export the basename on purpose,
		// so that we can change the folder hierarchy if we want, for final collection).
		FString StrValue = UTF8_TO_TCHAR(Value.c_str());
		if (Key.ends_with("Map") && !StrValue.Contains(ITwin::MAT_LIBRARY_TAG))
		{
			StrValue = FString::Printf(TEXT("\"%s/%s/%s\""),
				ITwin::MAT_LIBRARY_TAG,
				*RelativePath,
				*StrValue.TrimQuotes());
		}
		MaterialDataAsset->MaterialParameters.Emplace(
			UTF8_TO_TCHAR(Key.c_str()), StrValue);
	}

	FAssetRegistryModule::AssetCreated(MaterialDataAsset);

	Package->FullyLoad();
	Package->SetDirtyFlag(true);
	return UEditorLoadingAndSavingUtils::SavePackages({ Package }, true);
}

#endif // WITH_EDITOR
