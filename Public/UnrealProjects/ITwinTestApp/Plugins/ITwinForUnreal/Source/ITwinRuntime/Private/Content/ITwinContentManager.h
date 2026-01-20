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
	void DownloadFromAssetPath(const FString& path);
	UFUNCTION(BlueprintCallable, Category = "ITwinContent")
	void DownloadFromComponentId(const FString& componentId);
	UFUNCTION(BlueprintCallable, Category = "ITwinContent")
	void SetContentRootPath(const FString& rootPath);
	UFUNCTION(BlueprintCallable, Category = "ITwinContent")
	FString GetContentRootPath() const;
	UFUNCTION(BlueprintCallable, Category = "ITwinContent")
	void MountPak(const FString& path);
	UFUNCTION(BlueprintCallable, Category = "ITwinContent")
	void LoadContentJsonFile();

	void InitializePakPlatformFile();
	void DeinitializePakPlatformFile();
	bool IsRunningPIE();

	struct SContentInfo {
		std::string name;
		std::string category;
		std::uint32_t chunkId;
		std::string path;
	};

protected:
	FString ContentRootPath;
	std::map<FString, SContentInfo> ContentInfoMap;
	std::set<FString> MountedPaks;
	class FPakPlatformFile* PakPlatformFile = nullptr;
	class IPlatformFile* PlatformFileRef = nullptr;

};

