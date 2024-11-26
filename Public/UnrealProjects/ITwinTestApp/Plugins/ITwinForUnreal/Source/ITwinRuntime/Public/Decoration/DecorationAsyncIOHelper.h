/*--------------------------------------------------------------------------------------+
|
|     $Source: DecorationAsyncIOHelper.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once


#include <CoreMinimal.h>
#include <ITwinLoadInfo.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include "SDK/Core/Visualization/Decoration.h"
#	include "SDK/Core/Visualization/InstancesManager.h"
#	include "SDK/Core/Visualization/ScenePersistence.h"
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <atomic>

namespace SDK::Core
{
	class MaterialPersistenceManager;
}

class AITwinDecorationHelper;


class FDecorationAsyncIOHelper
{
public:
	FDecorationAsyncIOHelper() = default;

	void RequestStop();
	bool IsInitialized() const;

	void InitDecorationService(const UObject* WorldContextObject);

	void SetLoadedITwinInfo(FITwinLoadInfo const& InLoadedSceneInfo);
	FITwinLoadInfo const& GetLoadedITwinInfo() const;

	bool LoadCustomMaterials(std::string const& accessToken);
	bool LoadPopulationsFromServer(std::string const& accessToken);
	bool SaveDecorationToServer(std::string const& accessToken);
	bool LoadSceneFromServer(std::string const& accessToken);
	bool SaveSceneToServer(std::string const& accessToken);


private:
	bool LoadITwinDecoration(std::string const& accessToken);

private:
	FITwinLoadInfo LoadedITwinInfo;

	std::shared_ptr<SDK::Core::IDecoration> decoration;
	std::shared_ptr<SDK::Core::IInstancesManager> instancesManager;
	std::shared_ptr<SDK::Core::IInstancesGroup> instancesGroup;
	std::shared_ptr<SDK::Core::MaterialPersistenceManager> materialPersistenceMngr;
	std::shared_ptr<FString> decorationITwin; // iTwin ID corresponded to loaded decoration, if any.
	std::shared_ptr<SDK::Core::IScenePersistence> scene;

	std::shared_ptr<std::atomic_bool> shouldStop = std::make_shared<std::atomic_bool>(false);

	friend class AITwinDecorationHelper;
};
