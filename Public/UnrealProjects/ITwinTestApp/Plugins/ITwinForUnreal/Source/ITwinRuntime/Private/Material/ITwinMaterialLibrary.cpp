/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMaterialLibrary.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Material/ITwinMaterialLibrary.h>

#include <Decoration/ITwinContentLibrarySettings.h>
#include <Decoration/ITwinDecorationHelper.h>
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
bool FITwinMaterialLibrary::MaterialExistsInDir(FString const& DestinationFolder, FString& OutExistingFilePath)
{
	std::error_code ec;
	std::filesystem::path const OutputFolder = TCHAR_TO_UTF8(*DestinationFolder);
	std::filesystem::path const jsonMaterialPath = OutputFolder / MATERIAL_JSON_BASENAME;
	if (std::filesystem::exists(jsonMaterialPath, ec))
	{
		OutExistingFilePath = UTF8_TO_TCHAR(jsonMaterialPath.generic_string().c_str());
		return true;
	}
	else
	{
		return false;
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
				FString const MatLibraryFullPath = GetBentleyLibraryPath();
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
	matSettings.displayName = std::string(reinterpret_cast<const char*>(StringCast<UTF8CHAR>(*MaterialName).Get()));

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
	AdvViz::SDK::ETextureSource& OutTexSource,
	MaterialPersistencePtr const& MatIOMngr,
	FString const* DestinationJsonPath /*= nullptr*/)
{
	using namespace AdvViz::SDK;

	if (!ensure(MatIOMngr))
	{
		return false;
	}

	KeyValueStringMap KeyValueMap;
	OutTexSource = ETextureSource::Library;

	std::optional<ETextureSource> enforcedTexSource;
	const bool bIsJsonFormat = AssetPath.EndsWith(TEXT(".json"));
	if (bIsJsonFormat)
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
	const bool bSuccess = MatIOMngr->GetMaterialSettingsFromKeyValueMap(KeyValueMap,
		OutMaterial, OutTexKeys, OutTextureUsageMap, enforcedTexSource);

#if RESAVE_ITWIN_MATERIAL_LIBRARY_AS_JSON()
	if (bSuccess && !bIsJsonFormat && DestinationJsonPath)
	{
		bool bCanSaveJson = true;
		for (auto const& TexKey : OutTexKeys)
		{
			if (TexKey.eSource != ETextureSource::Library)
			{
				BE_ISSUE("material should reference only textures from MatLibrary!", TexKey.id);
				bCanSaveJson = false;
			}
		}
		std::string const jsonMatStr = MatIOMngr->ExportAsJson(OutMaterial, "any_imodel", 999);
		if (jsonMatStr.empty())
		{
			BE_ISSUE("Failed to re-export material as JSON", OutMaterial.displayName);
			bCanSaveJson = false;
		}
		if (bCanSaveJson)
		{
			std::filesystem::path const OutputJsonPath = TCHAR_TO_UTF8(**DestinationJsonPath);
			std::ofstream(OutputJsonPath) << jsonMatStr;
			{
				std::error_code ec;
				if (!std::filesystem::exists(OutputJsonPath, ec))
				{
					BE_ISSUE("Failed writing material definition to file", OutputJsonPath.generic_string());
				}
			}
		}
	}
#endif // RESAVE_ITWIN_MATERIAL_LIBRARY_AS_JSON

	return bSuccess;
}

/*static*/ FString FITwinMaterialLibrary::BeMatLibraryRootPath;

/*static*/
void FITwinMaterialLibrary::InitPaths(AITwinDecorationHelper const& DecoHelper)
{
	const FString ExternalMatLibraryPath = DecoHelper.GetContentRootPath() / TEXT("Materials");
	if (FPaths::DirectoryExists(ExternalMatLibraryPath))
	{
		// New content paradigm (future compatibility with the Component Center).
		BeMatLibraryRootPath = ExternalMatLibraryPath;
	}
	else
	{
		// Previously, the MaterialLibrary content was packaged withing the Unreal application.
		BeMatLibraryRootPath = FPaths::ProjectContentDir() / ITwin::MAT_LIBRARY;
		// This path no longer exists in default iTwin applications => log and display error (the user did
		// not install the additional content in the right location...)
		static FString LastExtDirChecked;
		if (!FPaths::DirectoryExists(BeMatLibraryRootPath) && LastExtDirChecked != ExternalMatLibraryPath)
		{
			LastExtDirChecked = ExternalMatLibraryPath; // avoid displaying the same message several times.
			FString DefaultMatLibraryPath = ExternalMatLibraryPath;
			DefaultMatLibraryPath.ReplaceInline(TEXT("\\"), TEXT("/"), ESearchCase::CaseSensitive);
			FString const StrMessage =
				TEXT("No iTwin Material Library found: please install it in this directory: ")
				+ DefaultMatLibraryPath
				+ TEXT("\n\nIf you don\'t, you may get some missing textures when loading existing scenes.");
			std::string const StrMessage_utf8 = TCHAR_TO_UTF8(*StrMessage);
			//BE_ISSUE(StrMessage_utf8.c_str());
			BE_LOGE("ContentHelper", StrMessage_utf8);
			FMessageDialog::Open(EAppMsgCategory::Error, EAppMsgType::Ok,
				FText::FromString(StrMessage),
				FText::FromString(""));
		}
	}
}

/*static*/
const FString& FITwinMaterialLibrary::GetCustomLibraryPath()
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
const FString& FITwinMaterialLibrary::GetBentleyLibraryPath()
{
	ensureMsgf(!BeMatLibraryRootPath.IsEmpty(), TEXT("InitPaths must be called before!"));
	return BeMatLibraryRootPath;
}

/*static*/
FString FITwinMaterialLibrary::GetBeLibraryPathForLoading(const FString& RelativeMaterialName)
{
	ensure(FPathViews::GetExtension(RelativeMaterialName).IsEmpty());
	if (UseExternalPathForBentleyLibrary())
	{
		return GetBentleyLibraryPath() / RelativeMaterialName / TEXT(MATERIAL_JSON_BASENAME);
	}
	else
	{
		return FString::Printf(TEXT("/Game/%s/%s"), ITwin::MAT_LIBRARY, *RelativeMaterialName);
	}
}

/*static*/
bool FITwinMaterialLibrary::UseExternalPathForBentleyLibrary()
{
	static const auto UseExternalDir = []() -> bool
	{
		return !FITwinMaterialLibrary::GetBentleyLibraryPath().EndsWith(ITwin::MAT_LIBRARY);
	};
	static const bool bUseExternalDir = UseExternalDir();
	return bUseExternalDir;
}

/*static*/
bool FITwinMaterialLibrary::UseJsonFormatForBentleyLibrary()
{
	return UseExternalPathForBentleyLibrary();
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

/*static*/
AdvViz::expected<void, std::string> FITwinMaterialLibrary::RemoveCustomMaterial(const FString& AssetPath)
{
	// Recover the material's directory.
	FStringView DirectoryPathView, BaseNameNoExt, Ext;
	FPathViews::Split(AssetPath, DirectoryPathView, BaseNameNoExt, Ext);;

	// Check it's a custom material (the official Bentley library is read-only, loaded from pak file).
	FStringView RelativeDirView;
	FString RootCustomLibraryPath = GetCustomLibraryPath();
	if (!FPathViews::TryMakeChildPathRelativeTo(DirectoryPathView,
												RootCustomLibraryPath,
												RelativeDirView))
	{
		return AdvViz::make_unexpected(
			fmt::format("'{}' does not belong to custom material library directory ({})",
				TCHAR_TO_UTF8(*AssetPath), TCHAR_TO_UTF8(*RootCustomLibraryPath)));
	}
	const FString DirectoryPath(DirectoryPathView);
	if (!FPaths::DirectoryExists(DirectoryPath))
	{
		return AdvViz::make_unexpected(
			fmt::format("Directory '{}' does not exist", TCHAR_TO_UTF8(*DirectoryPath)));
	}
	if (!IFileManager::Get().DeleteDirectory(*DirectoryPath, /*requireExists*/false, /*recurse*/true))
	{
		return AdvViz::make_unexpected(
			fmt::format("[IFileManager] Failed to delete Directory '{}'", TCHAR_TO_UTF8(*DirectoryPath)));
	}
	return {};
}

/*static*/
AdvViz::expected<void, std::string> FITwinMaterialLibrary::RenameCustomMaterial(const FString& OldAssetPath,
	const FString& NewDisplayName, const FString& NewDirectory, bool bOverwrite)
{
	auto const& MatIOMngr = AITwinIModel::GetMaterialPersistenceManager();
	if (!MatIOMngr)
	{
		return AdvViz::make_unexpected("Cannot rename material - no persistence manager");
	}
	const bool bNewDirExists = FPaths::DirectoryExists(NewDirectory);
	if (bOverwrite && ensure(bNewDirExists))
	{
		if (!IFileManager::Get().DeleteDirectory(*NewDirectory, /*requireExists*/false, /*recurse*/true))
		{
			return AdvViz::make_unexpected(
				fmt::format("Could not delete directory '{}'", TCHAR_TO_UTF8(*NewDirectory)));
		}
	}
	std::filesystem::path NewFSDir = TCHAR_TO_UTF8(*NewDirectory);
	std::filesystem::path OldFSAssetPath = TCHAR_TO_UTF8(*OldAssetPath);
	if (!OldFSAssetPath.has_parent_path())
	{
		return AdvViz::make_unexpected(
			fmt::format("Could not recover old directory from {}", OldFSAssetPath.generic_string()));
	}
	const std::filesystem::path OldFSDir = OldFSAssetPath.parent_path();
	if (NewFSDir != OldFSDir)
	{
		std::error_code ec;
		std::filesystem::rename(OldFSDir, NewFSDir, ec);
		if (ec)
		{
			return AdvViz::make_unexpected(
				fmt::format("Could not rename directory {} to {}: {}",
					OldFSDir.generic_string(), NewFSDir.generic_string(), ec.message()));
		}
	}
	// Modify the material name in the material.json file
	std::string renameInJsonError;
	if (!MatIOMngr->RenameMaterialInJsonFile(NewFSDir / MATERIAL_JSON_BASENAME,
											 TCHAR_TO_UTF8(*NewDisplayName),
											 renameInJsonError))
	{
		return AdvViz::make_unexpected(
			fmt::format("Could not rename material in json file: {}", renameInJsonError));
	}
	return {};
}

#if WITH_EDITOR

/*static*/
bool FITwinMaterialLibrary::ImportJsonToLibrary(FString const& AssetPath)
{
	using namespace AdvViz::SDK;

	if (UseJsonFormatForBentleyLibrary())
	{
		ensure(false);
		// If we use the JSON format for materials, this import should be simplified much: no need to
		// create any Unreal asset (UITwinMaterialDataAsset) - instead we should just rewrite the json file so that
		// all textures have the <MatLibrary> tag (and point to the final location).
		return false;
	}
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

	FString const MatLibraryPath = GetBentleyLibraryPath();
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

/*static*/
bool FITwinMaterialLibrary::ConvertAssetToJson(FString const& AssetPath)
{
#if RESAVE_ITWIN_MATERIAL_LIBRARY_AS_JSON()
	std::filesystem::path CurFile(TCHAR_TO_UTF8(*AssetPath));

	// From <MatLibrary>/Folder.uasset, generate <MatLibrary>/Folder/material.json
	std::filesystem::path const MaterialSubDirectory = CurFile.parent_path() / CurFile.stem();
	std::error_code ec;
	if (!std::filesystem::is_directory(MaterialSubDirectory, ec))
	{
		BE_ISSUE("sub-directory for material does not exist", MaterialSubDirectory.generic_string());
		return false;
	}
	std::filesystem::path JsonForCurFile = MaterialSubDirectory / MATERIAL_JSON_BASENAME;
	if (std::filesystem::exists(JsonForCurFile, ec))
	{
		return false;
	}
	const FString StrJsonForCurFile = UTF8_TO_TCHAR(JsonForCurFile.generic_string().c_str());

	AdvViz::SDK::TextureKeySet NewTextures;
	AdvViz::SDK::TextureUsageMap NewTextureUsageMap;
	AdvViz::SDK::ITwinMaterial NewMaterial;
	AdvViz::SDK::ETextureSource TexSource = AdvViz::SDK::ETextureSource::Library;

	// We need a path in the form of "/Game/MaterialLibrary/xxx.uasset"
	FString const MatLibraryPath = GetBentleyLibraryPath();
	FStringView RelativePathView;
	if (!ensure(FPathViews::TryMakeChildPathRelativeTo(AssetPath, MatLibraryPath, RelativePathView)))
	{
		ensureMsgf(false, TEXT("Path %s not inside Material Library (%s)"), *AssetPath, *MatLibraryPath);
		return false;
	}
	FString RelativePath = FString(RelativePathView);
	RelativePath.ReplaceInline(TEXT(".uasset"), TEXT("")); // remove extension
	FString PackageName = FString::Printf(TEXT("/Game/%s/%s"), ITwin::MAT_LIBRARY, *RelativePath);

	if (LoadMaterialFromAssetPath(PackageName, NewMaterial,
		NewTextures, NewTextureUsageMap, TexSource,
		AITwinIModel::GetMaterialPersistenceManager(),
		&StrJsonForCurFile))
	{
		const bool bConverted = std::filesystem::exists(JsonForCurFile, ec);
		BE_ASSERT(bConverted);
		return bConverted;
	}
#else
	BE_ISSUE("RESAVE_ITWIN_MATERIAL_LIBRARY_AS_JSON not defined!");
#endif

	return false;
}

#endif // WITH_EDITOR
