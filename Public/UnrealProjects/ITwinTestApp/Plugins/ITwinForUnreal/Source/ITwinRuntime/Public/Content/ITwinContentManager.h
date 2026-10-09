/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinContentManager.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <map>
#include <set>
#include <string>
#include "ITwinContentManager.generated.h"

UCLASS()
class UITwinContentManager : public UObject
{
	GENERATED_BODY()
public:
	UITwinContentManager();
	~UITwinContentManager();

	UFUNCTION(BlueprintCallable, Category = "ITwinContent")
	void Mount(const FString& path);
	UFUNCTION(BlueprintCallable, Category = "ITwinContent")
	void DownloadFromComponentId(const FString& componentId);
	UFUNCTION(BlueprintCallable, Category = "ITwinContent")
	void SetContentRootPath(const FString& rootPath);
	UFUNCTION(BlueprintCallable, Category = "ITwinContent")
	FString GetContentRootPath() const;
	UFUNCTION(BlueprintCallable, Category = "ITwinContent")
	void MountPak(const FString& path, const FString& id = TEXT(""));
	UFUNCTION(BlueprintCallable, Category = "ITwinContent")
	void LoadContentJsonFile();
	
	FString ShouldDownloadComponent(const FString& path) const;
	FString SanitizePath(const FString& path) const;
	FString HasComponentIDInPath(const FString& path) const;
	bool DownloadedComponent(const FString& componentId) const;

	/// Stores the resolved object path produced when a component is added (see ComponentCenter::AddComponent).
	void SetComponentObjectPath(const FString& componentId, const FString& objectPath);
	/// Returns the resolved object path previously registered for the given component, or an empty string.
	FString GetComponentObjectPath(const FString& componentId) const;

	void InitializePakPlatformFile();
	void DeinitializePakPlatformFile();
	bool IsRunningPIE();

	struct SContentInfo {
		std::string name;
		std::string category;
		std::uint32_t chunkId;
		std::string path;
	};
	class AITwinDecorationHelper* DecorationHelperPtr = nullptr;
protected:
	FString ContentRootPath;
	std::map<FString, SContentInfo> ContentInfoMap;
	std::set<FString> MountedPaks;
	class FPakPlatformFile* PakPlatformFile = nullptr;
	class IPlatformFile* PlatformFileRef = nullptr;
	std::set<FString> DownloadedComponents;
	std::map<FString, FString> ComponentObjectPaths;

};

