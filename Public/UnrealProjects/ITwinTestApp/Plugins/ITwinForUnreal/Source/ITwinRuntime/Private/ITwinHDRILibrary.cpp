/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinHDRILibrary.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinHDRILibrary.h>
#include <ITwinHDRIDataAsset.h>

#include <HAL/FileManager.h>
#include <Misc/Paths.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <fmt/format.h>
#	include <spdlog/fmt/fmt.h>
#	include <SDK/Core/ITwinAPI/ITwinScene.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <fstream>
#include <filesystem>

#if WITH_EDITOR
#	include <Factories/DataAssetFactory.h>
#	include <FileHelpers.h>
#	include <ObjectTools.h>
#	include <UObject/Package.h>
#endif // WITH_EDITOR
#	include <AssetRegistry/AssetRegistryModule.h>
#include <Engine/TextureCube.h>
#include "Content/ITwinContentManager.h"
ITWINRUNTIME_API void ITwin::GetAssetDataInDirectory(const FString& CurrentDirPath, const UClass& Class, TArray<FAssetData>& OutAssetData)
{
	FAssetRegistryModule& assetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	FName packageName = *Class.GetPackage()->GetName();
	FName assetName = Class.GetFName();
	FARFilter filter;
	filter.ClassPaths.Add(FTopLevelAssetPath(packageName, assetName));
	filter.PackagePaths.Add(FName(FString(TEXT("/Game/")) + CurrentDirPath));
	filter.bRecursivePaths = true;
	assetRegistryModule.Get().GetAssets(filter, OutAssetData);
}

/*static*/
FITwinHDRILibrary::ExportResult FITwinHDRILibrary::ExportHDRIToDisk(AITwinDecorationHelper const* persistanceMngr, ITwinHDRI const& HDRISettings, FString const& HDRIName, FString const& DestinationFolder) {
	std::filesystem::path OutputFolder = std::filesystem::path(TCHAR_TO_UTF8(*DestinationFolder)) / std::filesystem::path(TCHAR_TO_UTF8(*HDRIName));
	std::filesystem::path const jsonHDRIPath = OutputFolder / HDRI_JSON_BASENAME;

	if (!persistanceMngr)
	{
		return AdvViz::make_unexpected(ExportError{ "no scene persistence manager!" });
	}

	std::string jsonHDRIStr = persistanceMngr->ExportHDRIAsJson(HDRISettings);
	if (jsonHDRIStr.empty())
	{
		return AdvViz::make_unexpected(ExportError{
			fmt::format("Failed to export HDRI {} settings as JSON.", TCHAR_TO_UTF8(*HDRIName)) });
	}

	std::error_code ec;
	if (!std::filesystem::is_directory(OutputFolder, ec)
		&& !std::filesystem::create_directories(OutputFolder, ec))
	{
		return AdvViz::make_unexpected(ExportError{
			fmt::format("Could not create directory {}: {}", OutputFolder.generic_string(), ec.message()) });
	}

	std::ofstream(jsonHDRIPath) << jsonHDRIStr;
	{
		ec.clear();
		if (!std::filesystem::exists(jsonHDRIPath, ec))
		{
			return AdvViz::make_unexpected(ExportError{
				fmt::format("Failed writing hdri definition in {}.", jsonHDRIPath.generic_string()) });
		}
	}

	return {};
}

/*static*/
FString FITwinHDRILibrary::GetCustomHDRIPath() {
	static const auto GetUserHDRILibraryPath = []() -> FString
	{
		FString OutDir = FPlatformProcess::UserSettingsDir();
		if (OutDir.IsEmpty())
		{
			ensureMsgf(false, TEXT("No user settings directory"));
			return {};
		}
		return FPaths::Combine(OutDir, TEXT("Bentley"), TEXT("AdvViz"), TEXT("HDRI"));
	};
	static const FString CustomHDRILibraryPath = GetUserHDRILibraryPath();
	return CustomHDRILibraryPath;
}

FITwinHDRILibrary::ITwinHDRI FITwinHDRILibrary::ConvertKeyValueMapToDRISettings(const TMap<FString, FString>& HDRIParameters)
{
	ITwinHDRI settings;

	if (HDRIParameters.Find("hdriName"))
	{
		settings.hdriName = std::string(TCHAR_TO_UTF8(*HDRIParameters["hdriName"]));
	}
	if (HDRIParameters.Find("sunPitch"))
	{
		settings.sunPitch = FCString::Atod(*HDRIParameters["sunPitch"]);
	}
	if (HDRIParameters.Find("sunYaw"))
	{
		settings.sunYaw = FCString::Atod(*HDRIParameters["sunYaw"]);
	}
	if (HDRIParameters.Find("sunIntensity"))
	{
		settings.sunIntensity = FCString::Atod(*HDRIParameters["sunIntensity"]);
	}
	if (HDRIParameters.Find("rotation"))
	{
		settings.rotation = FCString::Atod(*HDRIParameters["rotation"]);
	}
	return settings;
}

FITwinHDRILibrary::LoadHdriResult FITwinHDRILibrary::GetHrdiFromName(AITwinDecorationHelper const* persistanceMngr, FString NewHDRIName)
{
	LoadHdriResult res;

	auto l = GetListOfHDRIPresets();
	std::string strname = TCHAR_TO_UTF8(*NewHDRIName);
	if (std::find_if(l.begin(), l.end(), [strname](const std::pair<std::string,bool> &item)
	{
		if(item.first == strname)
			return true;
		if(item.second && item.first + "_withsettings" == strname)
			return  true;
		return false;
	}
	
	) == l.end())
	{
		BE_LOGE("FITwinHDRILibrary", fmt::format("HDRI named {} not found in presets.", TCHAR_TO_UTF8(*NewHDRIName)));
		return res;
	}

	FString PathName = TEXT("/Game/") + FString(ITwin::HDRI_LIBRARY) + TEXT("/") + NewHDRIName;
	if (persistanceMngr && persistanceMngr->iTwinContentManager)
	{
		persistanceMngr->iTwinContentManager->DownloadFromAssetPath(PathName);
	}
	UTextureCube* DefaultTextureCube = LoadObject<UTextureCube>(nullptr, *PathName);
	if (!DefaultTextureCube)
	{
		UITwinHDRIDataAsset* hdriData = LoadObject <UITwinHDRIDataAsset>(nullptr, *PathName);
		if (hdriData && hdriData->HDRIParameters.Find("hdriName") != nullptr)
		{
			auto BaseName = hdriData->HDRIParameters["hdriName"];
			PathName = TEXT("/Game/") + FString(ITwin::HDRI_LIBRARY) + TEXT("/") + BaseName;
			if (persistanceMngr && persistanceMngr->iTwinContentManager)
			{
				persistanceMngr->iTwinContentManager->DownloadFromAssetPath(PathName);
			}
			DefaultTextureCube = LoadObject<UTextureCube>(nullptr, *PathName);
			if (DefaultTextureCube) {
				res.settings = FITwinHDRILibrary::ConvertKeyValueMapToDRISettings(hdriData->HDRIParameters);
				res.hasSettings = true;
			}
		}
	}
	res.tc = DefaultTextureCube;
	return res;
}

#if WITH_EDITOR
/*static*/
void FITwinHDRILibrary::ImportJsonToLibrary(AITwinDecorationHelper const* persistanceMngr) {
	// Export and import from the same directory.
	FString customHDRIDir = FITwinHDRILibrary::GetCustomHDRIPath();

	TArray<FString> JsonFiles;
	IFileManager::Get().FindFilesRecursive(JsonFiles, *customHDRIDir, TEXT("*.json"), true, false);

	for(const FString& AssetPath : JsonFiles)
	{
		FStringView AssetDir, AssetName, AssetExt;
		FPathViews::Split(AssetPath, AssetDir, AssetName, AssetExt); // <asset_dir> / "hdri" / "json"

		FStringView RelativePathView;
		if (!ensure(FPathViews::TryMakeChildPathRelativeTo(AssetDir, customHDRIDir, RelativePathView)))
		{
			ensureMsgf(false, TEXT("Path %s not inside HDRI Library (%s)"), *AssetPath, *customHDRIDir);
			return;
		}

		FString const RelativePath = FString(RelativePathView);
		FString PackageName = FString::Printf(TEXT("/Game/%s/%s_withsettings"), ITwin::HDRI_LIBRARY, *RelativePath);
		PackageName = ObjectTools::SanitizeInvalidChars(PackageName, INVALID_LONGPACKAGE_CHARACTERS);

		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();

		UITwinHDRIDataAsset* HDRIDataAsset = Cast<UITwinHDRIDataAsset>(Package->FindAssetInPackage());
		if (IsValid(HDRIDataAsset))
		{
			//ensureMsgf(false, TEXT("Package already exists (%s) - please edit it directly"), *PackageName); //for npw ignore, will rebake it if different later
			continue;
		}

		AdvViz::SDK::KeyValueStringMap KeyValueMap;
		if (!persistanceMngr->ConvertHDRIJsonFileToKeyValueMap(TCHAR_TO_UTF8(*AssetPath), KeyValueMap))
		{
			ensureMsgf(false, TEXT("could not parse Json material"));
			continue;
		}

		// Create and populate the map of strings containing all the hdri's parameters.
		UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
		HDRIDataAsset = Cast<UITwinHDRIDataAsset>(Factory->FactoryCreateNew(
			UITwinHDRIDataAsset::StaticClass(),
			Package,
			FName(FPathViews::GetPathLeaf(PackageName)),
			RF_Public | RF_Standalone | RF_Transactional,
			nullptr,
			GWarn));

		for (auto const& [Key, Value] : KeyValueMap)
		{
			FString StrValue = UTF8_TO_TCHAR(Value.c_str());
			HDRIDataAsset->HDRIParameters.Emplace(
				UTF8_TO_TCHAR(Key.c_str()), StrValue);
		}

		FAssetRegistryModule::AssetCreated(HDRIDataAsset);

		Package->FullyLoad();
		Package->SetDirtyFlag(true);


		UEditorLoadingAndSavingUtils::SavePackages({ Package }, true);
		/*
		// Save the package to disk so the asset appears as a .uasset file, still a problem of not able to see it in the content folder in editor.
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		bool saved = UPackage::SavePackage(
			Package,
			HDRIDataAsset,
			RF_Public | RF_Standalone,
			*PackageFileName,
			GError,
			nullptr,
			false,
			true,
			SAVE_None
		);

		if(!saved)
		{
			ensureMsgf(false, TEXT("Could not save package %s"), *PackageFileName);
		}
		*/
	}
}


#endif

std::vector<std::pair<std::string, bool>> FITwinHDRILibrary::GetListOfHDRIPresets()
{
	std::vector<std::pair<std::string, bool>> res;
	TArray<FAssetData> AssetDataArray;
	ITwin::GetAssetDataInDirectory(ITwin::HDRI_LIBRARY, *UTextureCube::StaticClass(), AssetDataArray);
	for (auto& AssetData : AssetDataArray)
	{
		FString CategoryPath = AssetData.AssetName.ToString();

		auto PathName = TEXT("/Game/") + FString(ITwin::HDRI_LIBRARY) + TEXT("/") + CategoryPath;
		auto DefaultTextureCube = LoadObject<UTextureCube>(nullptr, *PathName);
		UITwinHDRIDataAsset* hdriData = LoadObject<UITwinHDRIDataAsset>( nullptr, *(TEXT("/Game/") + FString(ITwin::HDRI_LIBRARY) + TEXT("/") + CategoryPath + TEXT("_withsettings")));
		bool enableSettings = false;
		if (hdriData && hdriData->HDRIParameters.Find("hdriName") != nullptr)
		{
			enableSettings = true;
		}
		std::string name = TCHAR_TO_UTF8(*CategoryPath);
		res.push_back(std::make_pair(name, enableSettings));

	}
	return res;
}