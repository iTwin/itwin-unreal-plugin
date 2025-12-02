/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinContentManager.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinContentManager.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/PlatformFile.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFilemanager.h"
#include "IPlatformFilePak.h"
#include <map>
#include <set>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#include <Core/Tools/Tools.h>
#include <Core/Json/Json.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

UITwinContentManager::UITwinContentManager()
{
}

UITwinContentManager::~UITwinContentManager()
{
    DeinitializePakPlatformFile();
}

void UITwinContentManager::LoadContentJsonFile()
{
    std::filesystem::path jsonPath = std::filesystem::path(TCHAR_TO_UTF8(*ContentRootPath)) / "content.json";
    std::string parseError;
    std::vector<SContentInfo> ContentData;
    if (AdvViz::SDK::Json::LoadFile(ContentData, jsonPath, parseError))
    {
        for (const auto& content : ContentData)
        {
            ContentInfoMap.emplace(UTF8_TO_TCHAR(content.path.c_str()), content);
        }
    }
    else
    {
        BE_LOGE("ContentHelper", "Failed to load content.json: " << parseError);
    }
}

void UITwinContentManager::DownloadFromAssetPath(const FString& path)
{
    if (IsRunningPIE())
        return;

    BE_LOGI("ContentHelper", "DownloadFromAssetPath: " << TCHAR_TO_UTF8(*path));
    // find chunkid base on path
	auto it = ContentInfoMap.find(path);
    if (it == ContentInfoMap.end())
    {
        // Clipping primitives are a special case (embedded in standard iTwin Engage content).
        if (!path.Contains(TEXT("Clipping/Clipping")))
        {
            BE_LOGE("ContentHelper", "Cannot find content info for path: " << TCHAR_TO_UTF8(*path));
        }
        return;
    }
    // mount pak file
    FString pakPath = FPaths::Combine(ContentRootPath, FString::Printf(TEXT("pakchunk%d-Windows.pak"), it->second.chunkId));
    if (MountedPaks.find(pakPath) == MountedPaks.end())
    {
        MountPak(pakPath);
        MountedPaks.insert(pakPath);
    }
}

void UITwinContentManager::InitializePakPlatformFile()
{
    IPlatformFile* ExistingPlatformFile = FPlatformFileManager::Get().FindPlatformFile(TEXT("PakFile"));
    if (ExistingPlatformFile)
    {
        PakPlatformFile = static_cast<FPakPlatformFile*>(ExistingPlatformFile);
    }
    else
    {
        PakPlatformFile = new FPakPlatformFile();
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        PlatformFileRef = &PlatformFile;
        PakPlatformFile->Initialize(&PlatformFile, TEXT(""));
        FPlatformFileManager::Get().SetPlatformFile(*PakPlatformFile);
    }

    if (PakPlatformFile == nullptr)
    {
        BE_LOGE("ContentHelper", "Unable to get PakPlatformFile");
        return;
    }

    BE_LOGI("ContentHelper", "PakPlatformFile is valid");
}

void UITwinContentManager::DeinitializePakPlatformFile()
{
    if (PlatformFileRef)
    {
        FPlatformFileManager::Get().SetPlatformFile(*PlatformFileRef);
		PlatformFileRef = nullptr;
    }
}


void UITwinContentManager::MountPak(const FString& path)
{
	BE_LOGI("ContentHelper", "Tyring to mount pak file: " << TCHAR_TO_UTF8(*path));
    // Check if the pak file exists
	if (!FPaths::FileExists(path))
	{
        BE_LOGE("ContentHelper", "Pak file " << TCHAR_TO_UTF8(*path) << " does not exist.");
		return;
	}

    // Mount Unreal pak file
    if (PakPlatformFile)
    {
        FString mountPoint = FPaths::ProjectContentDir();
        bool bMounted = PakPlatformFile->Mount(*path, 0, *mountPoint);
        if (!bMounted)
        {
            BE_LOGE("ContentHelper", "Failed to mount pak file: " << TCHAR_TO_UTF8(*path));
            return;
        }
    }
    else
    {
        BE_LOGE("ContentHelper", "PakPlatformFile not available");
        return;
    }
    BE_LOGI("ContentHelper", TCHAR_TO_UTF8(*path) << " successfully mounted.");
}

void UITwinContentManager::SetContentRootPath(const FString& rootPath)
{
    ContentRootPath = rootPath;
    LoadContentJsonFile();
    InitializePakPlatformFile();
}

FString UITwinContentManager::GetContentRootPath() const
{
    return ContentRootPath;
}

void UITwinContentManager::DownloadFromComponentId(const FString& componentId)
{
}

bool UITwinContentManager::IsRunningPIE()
{
#if WITH_EDITOR
    return true;
#else
    return false;
#endif
}