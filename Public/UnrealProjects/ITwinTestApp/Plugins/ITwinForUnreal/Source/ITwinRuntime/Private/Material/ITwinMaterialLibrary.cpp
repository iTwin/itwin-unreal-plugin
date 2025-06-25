/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMaterialLibrary.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Material/ITwinMaterialLibrary.h>

#include <Decoration/ITwinContentLibrarySettings.h>
#include <Material/ITwinMaterialDataAsset.h>
#include <ITwinIModel.h>

#include <GenericPlatform/GenericPlatformFile.h>
#include <HAL/FileManager.h>
#include <Misc/MessageDialog.h>
#include <Misc/PathViews.h>
#include <Misc/FileHelper.h>
#include <Misc/Paths.h>

#include <CesiumRuntime.h>
#include <CesiumAsync/IAssetAccessor.h>
#include <CesiumAsync/IAssetResponse.h>
#include <CesiumGltfReader/GltfReader.h>


#if WITH_EDITOR
#	include <AssetRegistry/AssetRegistryModule.h>
#	include <Factories/DataAssetFactory.h>
#	include <FileHelpers.h>
#	include <ObjectTools.h>
#	include <UObject/Package.h>
#endif // WITH_EDITOR

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <BeUtils/Gltf/GltfMaterialHelper.h>
#	include <BeUtils/Gltf/GltfMaterialTuner.h>
#	include <SDK/Core/Tools/Assert.h>
#	include <SDK/Core/Tools/Log.h>
#	include <SDK/Core/Visualization/MaterialPersistence.h>
#	include <spdlog/fmt/fmt.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <fstream>

namespace ITwin
{
	static const TCHAR* MAT_LIBRARY_TAG = TEXT(ITWIN_MAT_LIBRARY_TAG);
}

namespace
{
	bool CopyBinaryFile(FString const& SrcFilename, FString const& DstFilename)
	{
		TArray64<uint8> Buffer;
		if (!FFileHelper::LoadFileToArray(Buffer, *SrcFilename))
		{
			return false;
		}
		return FFileHelper::SaveArrayToFile(Buffer, *DstFilename);
	}

	bool DownloadAndSaveTexture(BeUtils::GltfMaterialHelper const& GltfMatHelper,
		AITwinIModel const& IModel, AdvViz::SDK::ITwinChannelMap const& TexMap,
		std::filesystem::path const& TextureDstPath)
	{
		if (TexMap.texture.empty())
			return false;

		auto const AccessToken = IModel.GetAccessToken();
		if (AccessToken.IsEmpty())
			return false;

		std::vector<CesiumAsync::IAssetAccessor::THeader> const tHeaders =
		{
			{
				"Authorization",
				std::string("Bearer ") + TCHAR_TO_ANSI(*AccessToken)
			}
		};

		bool bSaveOK = false;
		// This call should be very fast, as the image, if available, is already in Cesium cache.
		// And since we test TexAccess.cesiumImage before coming here, we *know* that the image is indeed
		// available.
		const std::shared_ptr<CesiumAsync::IAssetAccessor>& pAssetAccessor = getAssetAccessor();
		const CesiumAsync::AsyncSystem& asyncSystem = getAsyncSystem();
		const std::string textureURI = GltfMatHelper.GetTextureURL(TexMap.texture, TexMap.eSource);
		pAssetAccessor
			->get(asyncSystem, textureURI, tHeaders)
			.thenImmediately([&bSaveOK, &TextureDstPath](
				std::shared_ptr<CesiumAsync::IAssetRequest>&& pRequest) {
			const CesiumAsync::IAssetResponse* pResponse = pRequest->response();
			if (pResponse) {
				TArray64<uint8> Buffer;
				Buffer.Append(
					reinterpret_cast<const uint8*>(pResponse->data().data()),
					pResponse->data().size());
				bSaveOK = FFileHelper::SaveArrayToFile(Buffer,
					UTF8_TO_TCHAR(TextureDstPath.generic_string().c_str()));
			}
		}).wait();

		return bSaveOK;
	}
}

/*static*/
FITwinMaterialLibrary::ExportResult FITwinMaterialLibrary::ExportMaterialToDisk(
	AITwinIModel const& IModel, uint64_t MaterialId, FString const& MaterialName,
	FString const& DestinationFolder, bool bPromptBeforeOverwrite /*= true*/)
{
	using namespace AdvViz::SDK;

	auto const& MatIOMngr = AITwinIModel::GetMaterialPersistenceManager();
	if (!MatIOMngr)
	{
		return AdvViz::make_unexpected(ExportError{ "no material persistence manager!" });
	}

	std::string const IModelId = TCHAR_TO_UTF8(*IModel.IModelId);

	auto const& MatHelper = IModel.GetGltfMaterialHelper();
	if (!MatHelper)
	{
		return AdvViz::make_unexpected(ExportError{ std::string("no material helper in iMode l") + IModelId });
	}


	std::filesystem::path const OutputFolder = TCHAR_TO_UTF8(*DestinationFolder);
	std::filesystem::path const DirName = OutputFolder.filename();

	std::error_code ec;
	if (!std::filesystem::is_directory(OutputFolder, ec)
		&& !std::filesystem::create_directories(OutputFolder, ec))
	{
		return AdvViz::make_unexpected(ExportError{
			fmt::format("Could not create directory {}: {}", OutputFolder.generic_string(), ec.message()) });
	}

	std::filesystem::path const jsonMaterialPath = OutputFolder / MATERIAL_JSON_BASENAME;
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
			return AdvViz::make_unexpected(ExportError{ .message = "", .bIsUserCancel = true });
		}
	}

	
	// Fetch material full definition (including default values deduced from IModelRpc queries).

	ITwinMaterial matSettings;
	if (!MatHelper->GetMaterialFullDefinition(MaterialId, matSettings))
	{
		// Unknown material.
		return AdvViz::make_unexpected(ExportError{
			fmt::format("No material {} for iModel '{}'", MaterialId, IModelId) });
	}

	// If some textures were downloaded from iTwin API (decoration service or iModelRpc), copy them to the
	// destination folder.
	BeUtils::RLock Lock(MatHelper->GetMutex());

	for (uint8_t i(0); i < (uint8_t)AdvViz::SDK::EChannelType::ENUM_END; ++i)
	{
		AdvViz::SDK::EChannelType const Channel = static_cast<AdvViz::SDK::EChannelType>(i);

		std::optional<ITwinChannelMap> texMapOpt = matSettings.GetChannelMapOpt(Channel);
		if (texMapOpt && texMapOpt->HasTexture())
		{
			std::string TextureBasename = GetChannelName(Channel);
			std::filesystem::path TextureDstPath = OutputFolder;

			std::filesystem::path TextureSrcPath;

			auto const TexAccess = MatHelper->GetTextureAccess(*texMapOpt, Lock);
			if (!TexAccess.filePath.empty())
			{
				// File is already present locally => just copy it
				TextureSrcPath = TexAccess.filePath;
				TextureBasename += TexAccess.filePath.extension().generic_string();
				TextureDstPath /= TextureBasename;
			}
			else if (texMapOpt->eSource == ETextureSource::Library)
			{
				FString const MatLibraryFullPath = FPaths::ProjectContentDir() / ITwin::MAT_LIBRARY;
				TextureSrcPath = TCHAR_TO_UTF8(*MatLibraryFullPath);
				TextureSrcPath /= texMapOpt->texture;

				TextureBasename += TextureSrcPath.extension().generic_string();
				TextureDstPath /= TextureBasename;
			}
			if (!TextureSrcPath.empty())
			{
				// When overwriting an existing custom material, do not try to copy a texture to itself...
				if (TextureSrcPath != TextureDstPath)
				{
					if (!CopyBinaryFile(
						UTF8_TO_TCHAR(TextureSrcPath.generic_string().c_str()),
						UTF8_TO_TCHAR(TextureDstPath.generic_string().c_str())))
					{
						return AdvViz::make_unexpected(ExportError{
							fmt::format("Could not copy '{}' to '{}': {} - for material {}",
								TextureSrcPath.generic_string(),
								TextureDstPath.generic_string(),
								ec.message(),
								MaterialId) });
					}
				}
			}
			else if (TexAccess.cesiumImage)
			{
				// Try to recover texture from its url.
				// Normally, the texture name should hold the extension in such case
				auto const ext = std::filesystem::path(texMapOpt->texture).extension().generic_string();
				BE_ASSERT(!ext.empty());
				TextureBasename += ext;
				TextureDstPath /= TextureBasename;
				if (!DownloadAndSaveTexture(*MatHelper, IModel, *texMapOpt, TextureDstPath))
				{
					return AdvViz::make_unexpected(ExportError{
						fmt::format("Could not download and save texture '{}' (source: {}) for material {}",
							 texMapOpt->texture, (uint8_t)texMapOpt->eSource, MaterialId) });
				}
			}
			else
			{
				return AdvViz::make_unexpected(ExportError{
						fmt::format("Missing texture '{}' (source: {}) for material {}",
							 texMapOpt->texture, (uint8_t)texMapOpt->eSource, MaterialId) });
			}

			// Update the corresponding parameter in the material definition.
			// In the json file we just put the basename (it will be converted afterwards if we re-import
			// the file to generate UE asset for the official Bentley Material Library).
			// We use Decoration here, so that the basename is actually dumped tp Json, but this has no other
			// impact.
			matSettings.SetChannelMap(Channel,
				ITwinChannelMap{
					.texture = TextureBasename,
					.eSource = ETextureSource::Decoration
				});
		}
	}
	// Enforce material display name
	matSettings.displayName = TCHAR_TO_UTF8(*MaterialName);

	std::string const jsonMatStr = MatIOMngr->ExportAsJson(matSettings, IModelId, MaterialId);
	if (jsonMatStr.empty())
	{
		return AdvViz::make_unexpected(ExportError{
			fmt::format("Failed to export material {} as JSON.", MaterialId) });
	}

	std::ofstream(jsonMaterialPath) << jsonMatStr;
	{
		ec.clear();
		if (!std::filesystem::exists(jsonMaterialPath, ec))
		{
			return AdvViz::make_unexpected(ExportError{
				fmt::format("Failed writing material definition in {}.", jsonMaterialPath.generic_string()) });
		}
	}
	return {};
}


//! Loads a material definition from an asset file.
/*static*/
bool FITwinMaterialLibrary::LoadMaterialFromAssetPath(FString const& AssetPath, ITwinMaterial& OutMaterial,
	TextureKeySet& OutTexKeys, TextureUsageMap& OutTextureUsageMap,
	AdvViz::SDK::ETextureSource& OutTexSource)
{
	using namespace AdvViz::SDK;

	auto const& MatIOMngr = AITwinIModel::GetMaterialPersistenceManager();
	if (!ensure(MatIOMngr))
	{
		return false;
	}

	KeyValueStringMap KeyValueMap;
	OutTexSource = ETextureSource::Library;

	std::optional<ETextureSource> enforcedTexSource;
	if (AssetPath.EndsWith(TEXT(".json")))
	{
		// The file was saved inside the packaged application (for the custom material library).
		// Try to parse the Json, and make the texture paths absolute.
		std::filesystem::path TextureDir = std::filesystem::path(*AssetPath).parent_path();
		if (!MatIOMngr->ConvertJsonFileToKeyValueMap(
			TCHAR_TO_UTF8(*AssetPath), TextureDir, KeyValueMap))
		{
			return false;
		}
		// Textures are stored locally on disk.
		OutTexSource = ETextureSource::LocalDisk;
		enforcedTexSource = OutTexSource;
	}
	else
	{
		// The file is part of Bentley's Material Library (packaged at build time).
		UITwinMaterialDataAsset* MaterialDataAsset = LoadObject<UITwinMaterialDataAsset>(nullptr, *AssetPath);
		if (!IsValid(MaterialDataAsset))
		{
			return false;
		}
		for (auto const& [StrKey, StrValue] : MaterialDataAsset->MaterialParameters)
		{
			KeyValueMap[TCHAR_TO_UTF8(*StrKey)] = TCHAR_TO_UTF8(*StrValue);
		}
	}
	return MatIOMngr->GetMaterialSettingsFromKeyValueMap(KeyValueMap,
		OutMaterial, OutTexKeys, OutTextureUsageMap, enforcedTexSource);
}

/*static*/
FString FITwinMaterialLibrary::GetCustomLibraryPath()
{
	static const auto GetUserMatLibraryPath = []() -> FString
	{
		UITwinContentLibrarySettings const* ContentSettings = GetDefault<UITwinContentLibrarySettings>();
		if (ContentSettings && !ContentSettings->CustomMaterialLibraryDirectory.IsEmpty())
		{
			return ContentSettings->CustomMaterialLibraryDirectory;
		}
		FString OutDir = FPlatformProcess::UserSettingsDir();
		if (OutDir.IsEmpty())
		{
			ensureMsgf(false, TEXT("No user settings directory"));
			return {};
		}
		return FPaths::Combine(OutDir, TEXT("Bentley"), TEXT("AdvViz"), TEXT("Materials"));
	};
	static const FString CustomMatLibraryPath = GetUserMatLibraryPath();
	return CustomMatLibraryPath;
}

/*static*/
int32 FITwinMaterialLibrary::ParseJsonMaterialsInDirectory(FString const& DirectoryPath,
	TArray<FAssetData>& OutAssetDataArray)
{
	struct FUserMaterialDirectoryVisitor : public IPlatformFile::FDirectoryVisitor
	{
		FString const& RootDirectoryPath;
		TArray<FAssetData>& AssetDataArray;
		AITwinIModel::MaterialPersistencePtr const& MatIOMngr = AITwinIModel::GetMaterialPersistenceManager();

		FUserMaterialDirectoryVisitor(FString const& InDirectoryPath,
									  TArray<FAssetData>& OutAssetDataArray)
			: RootDirectoryPath(InDirectoryPath)
			, AssetDataArray(OutAssetDataArray)
		{
		}

		virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory) override
		{
			if (!bIsDirectory)
				return true; // ignore individual files
			FStringView FileName(FilenameOrDirectory);
			FStringView SuffixPath;
			if (FileName.Len() > RootDirectoryPath.Len())
			{
				SuffixPath = FileName.RightChop(RootDirectoryPath.Len() + 1);
			}
			if (SuffixPath.Find(TEXT("/")) != INDEX_NONE)
			{
				// Here we don't need to recurse.
				return true;
			}

			// See if the directory contains a material definition (material.json file)
			FString JsonMatFile = FString(FileName) / TEXT(MATERIAL_JSON_BASENAME);
			if (FPaths::FileExists(JsonMatFile))
			{
				AdvViz::SDK::KeyValueStringMap KeyValueMap;
				if (MatIOMngr->ConvertJsonFileToKeyValueMap(
					TCHAR_TO_UTF8(*JsonMatFile), {}, KeyValueMap))
				{
					FAssetData& AssetData = AssetDataArray.Emplace_GetRef();
					AssetData.PackageName = FName(*JsonMatFile);
				}
			}
			else
			{
				// We may have a category
				TArray<FString> FileNames;
				IFileManager::Get().FindFilesRecursive(FileNames, FilenameOrDirectory, TEXT(MATERIAL_JSON_BASENAME),
					true /*Files*/, false /*Directories*/);
				if (!FileNames.IsEmpty())
				{
					FAssetData& AssetData = AssetDataArray.Emplace_GetRef();
					AssetData.PackageName = FName(FilenameOrDirectory);
				}
			}
			return true;
		}
	};

	FUserMaterialDirectoryVisitor DirIter(DirectoryPath, OutAssetDataArray);
	if (DirIter.MatIOMngr)
	{
		IFileManager::Get().IterateDirectory(*DirectoryPath, DirIter);
	}
	return OutAssetDataArray.Num();
}

#if WITH_EDITOR

/*static*/
bool FITwinMaterialLibrary::ImportJsonToLibrary(FString const& AssetPath)
{
	using namespace AdvViz::SDK;

	if (!AssetPath.EndsWith(TEXT(".json")))
	{
		ensureMsgf(false, TEXT("expecting a .json file and got %s"), *AssetPath);
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
		ensureMsgf(false, TEXT("Path %s not inside Material Library (%s)"), *AssetPath, *MatLibraryPath);
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
		ensureMsgf(false, TEXT("Package already exists (%s) - please edit it directly"), *PackageName);
		return false;
	}

	// Convert JSON file exported previously for the creation of the Material Library.
	// We always export materials in a 'flat' mode, all in a same directory, but me may reorganize the final
	// library, introducing a category such as "Wood", "Metals" etc. So we will ensure we can recover the
	// location of texture paths by making them absolute (with the <MatLibrary>/ prefix).
	std::string DirPrefix = std::string(ITWIN_MAT_LIBRARY_TAG "/") + TCHAR_TO_UTF8(*RelativePath);

	KeyValueStringMap KeyValueMap;
	if (!MatIOMngr->ConvertJsonFileToKeyValueMap(TCHAR_TO_UTF8(*AssetPath), DirPrefix, KeyValueMap))
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
