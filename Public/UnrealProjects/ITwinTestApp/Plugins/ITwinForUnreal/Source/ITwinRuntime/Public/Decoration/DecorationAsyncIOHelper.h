/*--------------------------------------------------------------------------------------+
|
|     $Source: DecorationAsyncIOHelper.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once


#include <CoreMinimal.h>
#include <Containers/Map.h>
#include <ITwinLoadInfo.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include "SDK/Core/Visualization/Decoration.h"
#	include "SDK/Core/Visualization/InstancesManager.h"
#	include "SDK/Core/Visualization/SplinesManager.h"
#	include "SDK/Core/Visualization/AnnotationsManager.h"
#	include "SDK/Core/Visualization/ScenePersistence.h"
#	include "SDK/Core/Visualization/KeyframeAnimation.h"
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <atomic>
#include <set>

namespace AdvViz::SDK
{
	class MaterialPersistenceManager;
	class ITimeline;
	class ILink;
}

class AITwinDecorationHelper;
class AITwinIModel;

class FDecorationAsyncIOHelper
{
public:
	using LinkSharedPtr = std::shared_ptr<AdvViz::SDK::ILink>;
	using ModelIdentifier = ITwin::ModelDecorationIdentifier;

	FDecorationAsyncIOHelper() = default;

	void RequestStop();
	bool IsInitialized() const;

	void InitDecorationService(const UObject* WorldContextObject);

	void SetLoadedITwinId(const FString& ITwinId);	
	FString GetLoadedITwinId() const;
	void SetLoadedSceneId(FString InLoadedSceneId, bool inNewsScene = false);

	bool LoadCustomMaterials(TMap<FString, TWeakObjectPtr<AITwinIModel>> const& idToIModel,
		std::set<std::string> const& specificModels = {});
	bool LoadPopulationsFromServer();
	bool LoadAnimationKeyframesFromServer();
	bool SaveDecorationToServer();
	bool LoadSceneFromServer();
	bool SaveSceneToServer();
	bool LoadAnnotationsFromServer();

	LinkSharedPtr CreateLink(ModelIdentifier const& Key);
	bool LoadSplinesFromServer();

	std::shared_ptr<AdvViz::SDK::ISplinesManager> const& GetSplinesManager();

	AdvViz::expected<std::vector<std::shared_ptr< AdvViz::SDK::IScenePersistence>>, int>  GetITwinScenes(const FString& itwinid);

private:
	bool LoadITwinDecoration();

private:
	FString LoadedITwinId;
	FString LoadedSceneID;
	bool bSceneIdIsForNewScene = false;

	std::shared_ptr<AdvViz::SDK::IDecoration> decoration;
	std::shared_ptr<AdvViz::SDK::IInstancesManager> instancesManager_;
	std::map<AdvViz::SDK::IAnimationKeyframe::Id, AdvViz::SDK::IAnimationKeyframePtr> animationKeyframes;
	std::shared_ptr<AdvViz::SDK::IInstancesGroup> staticInstancesGroup;
	std::shared_ptr<AdvViz::SDK::MaterialPersistenceManager> materialPersistenceMngr;
	std::shared_ptr<FString> decorationITwin; // iTwin ID corresponded to loaded decoration, if any.
	std::shared_ptr<AdvViz::SDK::IScenePersistence> scene;
	// std::shared_ptr<AdvViz::SDK::IScenePersistence> DSscene; //scene from decoration service if we use sceneAPI obsolete
	std::shared_ptr<AdvViz::SDK::ISplinesManager> splinesManager;
	std::shared_ptr<AdvViz::SDK::IAnnotationsManager> annotationsManager;

	std::shared_ptr<std::atomic_bool> shouldStop = std::make_shared<std::atomic_bool>(false);
	bool decorationIsLinked = false;
	bool bUseDecorationService = false;
	std::map<ModelIdentifier, LinkSharedPtr> links;

	friend class AITwinDecorationHelper;
	void PostLoadSceneFromServer();
};
