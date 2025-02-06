/*--------------------------------------------------------------------------------------+
|
|     $Source: DecorationAsyncIOHelper.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once


#include <CoreMinimal.h>
#include <ITwinLoadInfo.h>
#include <Containers/Map.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include "SDK/Core/Visualization/Decoration.h"
#	include "SDK/Core/Visualization/InstancesManager.h"
#	include "SDK/Core/Visualization/ScenePersistence.h"
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <atomic>

namespace SDK::Core
{
	class MaterialPersistenceManager;
	class ITimeline;
}

class AITwinDecorationHelper;
class AITwinIModel;

class FDecorationAsyncIOHelper
{
public:
	FDecorationAsyncIOHelper() = default;

	void RequestStop();
	bool IsInitialized() const;

	void InitDecorationService(const UObject* WorldContextObject);

	void SetLoadedITwinInfo(FITwinLoadInfo const& InLoadedSceneInfo);
	FITwinLoadInfo const& GetLoadedITwinInfo() const;

	bool LoadCustomMaterials(std::string const& accessToken, TMap<FString, TWeakObjectPtr<AITwinIModel>> const& idToIModel);
	bool LoadPopulationsFromServer(std::string const& accessToken);
	bool SaveDecorationToServer(std::string const& accessToken);
	bool LoadSceneFromServer(std::string const& accessToken, std::shared_ptr<SDK::Core::ITimeline>& timeline);
	bool LoadSceneFromServer(std::string const& sceneid,std::string const& accessToken);
	bool SaveSceneToServer(std::string const& accessToken, const std::shared_ptr<SDK::Core::ITimeline>& timeline);
	std::shared_ptr <SDK::Core::Link>  CreateLink(EITwinModelType ct, const FString& id);


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
	bool decorationIsLinked = false;
	std::map< std::pair<EITwinModelType,FString> , std::shared_ptr<SDK::Core::Link> > links;

	friend class AITwinDecorationHelper;
};
