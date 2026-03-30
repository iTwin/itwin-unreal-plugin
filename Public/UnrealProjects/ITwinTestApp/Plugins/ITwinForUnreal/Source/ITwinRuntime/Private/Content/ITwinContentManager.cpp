/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinContentManager.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinContentManager.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/PlatformFile.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFilemanager.h"
#include "IPlatformFilePak.h"
#include <Decoration/ITwinDecorationHelper.h>
#include "Internationalization/Regex.h"
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

FString UITwinContentManager::HasComponentIDInPath(const FString& path) const
{
    FRegexPattern Pattern(TEXT("/CC/[0-9A-Fa-f\\-]+/[0-9A-Fa-f\\-]+/[0-9A-Za-z\\%-_ ]+/Game/"));
    FRegexMatcher Matcher(Pattern, path);
    if (Matcher.FindNext())
    {
        return Matcher.GetCaptureGroup(0).LeftChop(6); // Full match
    }
    return FString();

}

bool UITwinContentManager::DownloadedComponent(const FString& componentId) const
{

    BE_LOGI("ContentHelper", "DownloadedComponent : start downloading from component center: " << TCHAR_TO_UTF8(*componentId));

	bool res = false;
	if (DecorationHelperPtr && DecorationHelperPtr->OnDownloadRequest.IsBound())
	{
		res = DecorationHelperPtr->OnDownloadRequest.Execute(componentId);
	}
	if (!res)
	{
		BE_LOGE("ContentHelper", "DownloadedComponent: component " << TCHAR_TO_UTF8(*componentId) << " failed to download");
	}
    else
    {
        BE_LOGI("ContentHelper", "DownloadedComponent: component " << TCHAR_TO_UTF8(*componentId) << " success");
    }
	return res;
}

FString UITwinContentManager::ShouldDownloadComponent(const FString& path) const
{ 
    FString componentId = HasComponentIDInPath(path);
    if (!componentId.IsEmpty() && DownloadedComponents.find(componentId) == DownloadedComponents.end())
    {
        return componentId;
    }
	return FString();
}

FString UITwinContentManager::SanitizePath(const FString& path) const
{
    FString componentId = HasComponentIDInPath(path);
    if (!componentId.IsEmpty())
    {
		FString res = path.RightChop(componentId.Len());
		return res;
    }
    return path;
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


void UITwinContentManager::MountPak(const FString& path,const FString& id)
{
	BE_LOGI("ContentHelper", "Trying to mount pak file: " << TCHAR_TO_UTF8(*path));
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
    if(!id.IsEmpty())
	{
		DownloadedComponents.insert(id);
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