/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinContentManager.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "Content/ITwinContentManager.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/PlatformFile.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFilemanager.h"
#include "IPlatformFilePak.h"
#include <Decoration/ITwinDecorationHelper.h>
#include "Internationalization/Regex.h"

#include <Compil/BeforeNonUnrealIncludes.h>
#include <Core/Tools/Tools.h>
#include <Core/Json/Json.h>
#include <Core/Tools/Log.h>
#include <Compil/AfterNonUnrealIncludes.h>

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
    std::error_code ec;
    if (!std::filesystem::exists(jsonPath, ec))
    {
        BE_LOGV("ContentHelper", "File content.json does not exist");
        return;
    }
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
	if (!NormalizeComponentId(componentId).IsEmpty())
		return RequestComponentDownload(componentId, false);

	const bool accepted = DecorationHelperPtr && DecorationHelperPtr->OnDownloadRequest.IsBound()
		&& DecorationHelperPtr->OnDownloadRequest.Execute(componentId);
	if (!accepted)
		BE_LOGE("ContentHelper", "Component download request was not accepted");
	return accepted;
}

bool UITwinContentManager::IsComponentDownloaded(const FString& componentId) const
{
	const FString normalized = NormalizeComponentId(componentId);
	return !componentId.IsEmpty()
		&& DownloadedComponents.find(normalized.IsEmpty() ? componentId : normalized) != DownloadedComponents.end();
}

bool UITwinContentManager::RequestComponentDownload(const FString& componentId) const
{
	return !componentId.IsEmpty() && !IsComponentDownloaded(componentId) && DownloadedComponent(componentId);
}

FString UITwinContentManager::NormalizeComponentId(const FString& idOrPath) const
{
	FString id = HasComponentIDInPath(idOrPath);
	if (id.IsEmpty() && HasComponentIDInPath(idOrPath + TEXT("/Game/")) == idOrPath)
		id = idOrPath;
	if (id.IsEmpty())
		return {};

	// The catalog and component identifiers are hexadecimal; the name is not
	// necessarily case-insensitive. Keep the latter exactly as supplied.
	const int32 catalogEnd = id.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 4);
	const int32 componentEnd = catalogEnd == INDEX_NONE ? INDEX_NONE
		: id.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, catalogEnd + 1);
	if (catalogEnd != INDEX_NONE && componentEnd != INDEX_NONE)
		return id.Left(4) + id.Mid(4, componentEnd - 4).ToLower() + id.Mid(componentEnd);
	return id;
}

EITwinComponentLoadState UITwinContentManager::GetComponentLoadState(const FString& idOrPath) const
{
	const FString id = NormalizeComponentId(idOrPath);
	if (id.IsEmpty())
		return EITwinComponentLoadState::NotComponent;
	if (DownloadedComponents.find(id) != DownloadedComponents.end())
		return EITwinComponentLoadState::Ready;
	const auto it = ComponentAttempts.find(id);
	return it == ComponentAttempts.end() ? EITwinComponentLoadState::NotRequested : it->second.State;
}

FString UITwinContentManager::GetComponentLoadFailureReason(const FString& idOrPath) const
{
	const auto it = ComponentAttempts.find(NormalizeComponentId(idOrPath));
	return it == ComponentAttempts.end() ? FString() : it->second.Reason;
}

uint64 UITwinContentManager::GetComponentLoadGeneration(const FString& idOrPath) const
{
	const auto it = ComponentAttempts.find(NormalizeComponentId(idOrPath));
	return it != ComponentAttempts.end() && it->second.State == EITwinComponentLoadState::InFlight
		? it->second.Generation : 0;
}

bool UITwinContentManager::IsCurrentComponentLoad(const FString& idOrPath, uint64 generation) const
{
	return generation != 0 && GetComponentLoadState(idOrPath) == EITwinComponentLoadState::InFlight
		&& GetComponentLoadGeneration(idOrPath) == generation;
}

bool UITwinContentManager::RequestComponentDownload(const FString& idOrPath, bool bRetry) const
{
	const FString id = NormalizeComponentId(idOrPath);
	if (id.IsEmpty())
		return false;
	const EITwinComponentLoadState state = GetComponentLoadState(id);
	if (state == EITwinComponentLoadState::Ready || state == EITwinComponentLoadState::InFlight)
		return true;
	if (!bRetry && (state == EITwinComponentLoadState::RetryableFailure
		|| state == EITwinComponentLoadState::TerminalFailure))
		return false;

	FComponentAttempt& attempt = ComponentAttempts[id];
	attempt.State = EITwinComponentLoadState::InFlight;
	attempt.Generation = ++NextComponentGeneration;
	attempt.Reason.Empty();
	const uint64 generation = attempt.Generation;
	BE_LOGI("ContentHelper", "Requesting component download: " << TCHAR_TO_UTF8(*id));

	bool accepted = false;
	if (DecorationHelperPtr && DecorationHelperPtr->OnDownloadRequest.IsBound())
	{
		accepted = DecorationHelperPtr->OnDownloadRequest.Execute(id);
	}
	if (!accepted)
	{
		ReportComponentLoadFailure(id, generation, EITwinComponentLoadFailure::Retryable,
			TEXT("Component download request was not accepted"));
	}
	else
	{
		BE_LOGI("ContentHelper", "Component download request accepted: " << TCHAR_TO_UTF8(*id));
	}
	return accepted;
}

bool UITwinContentManager::ClearComponentLoadAttempt(const FString& idOrPath, uint64 generation) const
{
	const FString id = NormalizeComponentId(idOrPath);
	if (!IsCurrentComponentLoad(id, generation))
		return false;
	ComponentAttempts.erase(id);
	return true;
}

void UITwinContentManager::ReportComponentLoadFailure(const FString& idOrPath, uint64 generation,
	EITwinComponentLoadFailure failure, const FString& reason) const
{
	const FString id = NormalizeComponentId(idOrPath);
	if (!IsCurrentComponentLoad(id, generation))
		return;
	FComponentAttempt& attempt = ComponentAttempts[id];
	attempt.State = failure == EITwinComponentLoadFailure::Terminal
		? EITwinComponentLoadState::TerminalFailure : EITwinComponentLoadState::RetryableFailure;
	attempt.Reason = reason;
	BE_LOGE("ContentHelper", "Component download failed for " << TCHAR_TO_UTF8(*id)
		<< ": " << TCHAR_TO_UTF8(*reason));
}

void UITwinContentManager::RegisterDownloadedComponent(const FString& id)
{
	if (id.IsEmpty())
		return;
	const FString normalized = NormalizeComponentId(id);
	const FString& key = normalized.IsEmpty() ? id : normalized;
	DownloadedComponents.insert(key);
	FComponentAttempt& attempt = ComponentAttempts[key];
	attempt.State = EITwinComponentLoadState::Ready;
	attempt.Generation = ++NextComponentGeneration;
	attempt.Reason.Empty();
}

FString UITwinContentManager::ShouldDownloadComponent(const FString& path) const
{ 
    FString componentId = NormalizeComponentId(HasComponentIDInPath(path));
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

void UITwinContentManager::SetComponentObjectPath(const FString& componentId, const FString& objectPath)
{
	if (componentId.IsEmpty())
		return;
	const FString normalized = NormalizeComponentId(componentId);
	ComponentObjectPaths[normalized.IsEmpty() ? componentId : normalized] = objectPath;
}

FString UITwinContentManager::GetComponentObjectPath(const FString& componentId) const
{
	const FString normalized = NormalizeComponentId(componentId);
	auto it = ComponentObjectPaths.find(normalized.IsEmpty() ? componentId : normalized);
	if (it != ComponentObjectPaths.end())
	{
		return it->second;
	}
	return FString();
}

void UITwinContentManager::Mount(const FString& path)
{
    if (IsRunningPIE())
        return;

    BE_LOGI("ContentHelper", "Mount: " << TCHAR_TO_UTF8(*path));
    // find chunkid base on path
	auto it = ContentInfoMap.find(path);
    if (it == ContentInfoMap.end())
    {
        // Clipping primitives are a special case (embedded in standard iTwin Engage content).
        //obsolete log: most of the content is downloaded and does not rely on infoMap
		/*if (!path.Contains(TEXT("Clipping/Clipping")))
		{
			BE_LOGE("ContentHelper", "Cannot find content info for path: " << TCHAR_TO_UTF8(*path));
		}*/
        return;
    }
    // mount pak file
    FString pakPath = FPaths::Combine(ContentRootPath, FString::Printf(TEXT("pakchunk%d-Windows.pak"), it->second.chunkId));
    MountPak(pakPath);
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
    PakPlatformFile = nullptr;
}


bool UITwinContentManager::MountPak(const FString& path,const FString& id)
{
	BE_LOGI("ContentHelper", "Trying to mount pak file: " << TCHAR_TO_UTF8(*path));
    // Check if the pak file exists
    if (path.IsEmpty())
    {
        // fake pak to add id to DownloadedComponents
        RegisterDownloadedComponent(id);
        BE_LOGI("ContentHelper", TCHAR_TO_UTF8(*id) << " added to DownloadedComponents ( fake pak).");
        return true;
    }
	if (!FPaths::FileExists(path))
	{
        BE_LOGE("ContentHelper", "Pak file " << TCHAR_TO_UTF8(*path) << " does not exist.");
		return false;
	}

    // Mount Unreal pak file
    if (PakPlatformFile)
    {
        // Consult the platform file, not a local cache: another manager may have mounted
        // (or unmounted) this pak, and this manager may have been reinitialized.
        const FString fullPath = FPaths::ConvertRelativePathToFull(path);
        TArray<FString> mountedPaks;
        PakPlatformFile->GetMountedPakFilenames(mountedPaks);
        const bool bAlreadyMounted = mountedPaks.ContainsByPredicate([&fullPath](const FString& mountedPath)
        {
            return FPaths::IsSamePath(fullPath, FPaths::ConvertRelativePathToFull(mountedPath));
        });
        FString mountPoint = FPaths::ProjectContentDir();
        bool bMounted = bAlreadyMounted || PakPlatformFile->Mount(*fullPath, 0, *mountPoint);
        if (!bMounted)
        {
            BE_LOGE("ContentHelper", "Failed to mount pak file: " << TCHAR_TO_UTF8(*path));
            return false;
        }
    }
    else
    {
        BE_LOGE("ContentHelper", "PakPlatformFile not available");
        return false;
    }
	if(!id.IsEmpty())
	{
		RegisterDownloadedComponent(id);
		BE_LOGI("ContentHelper", TCHAR_TO_UTF8(*id) << " added to DownloadedComponents.");
    }
    BE_LOGI("ContentHelper", TCHAR_TO_UTF8(*path) << " successfully mounted.");
    return true;
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