/*--------------------------------------------------------------------------------------+
|
|     $Source: DecorationAsyncIOHelper.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once


#include <CoreMinimal.h>
#include <Containers/Map.h>
#include <ITwinLoadInfo.h>
#include <ITwinServerConnection.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include "SDK/Core/Visualization/Decoration.h"
#	include "SDK/Core/Visualization/InstancesManager.h"
#	include "SDK/Core/Visualization/SplinesManager.h"
#	include "SDK/Core/Visualization/AnnotationsManager.h"
#	include "SDK/Core/Visualization/ScenePersistence.h"
#	include "SDK/Core/Visualization/KeyframeAnimation.h"
#	include "SDK/Core/Visualization/PathAnimation.h"
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <atomic>
#include <functional>
#include <memory>
#include <set>
#include <shared_mutex>

namespace AdvViz::SDK
{
	class MaterialPersistenceManager;
	class ITimeline;
	class ILink;
	class AsyncRequestGroupCallback;
}

class FDecorationWaitableLoadEvent;
class AITwinDecorationHelper;
class AITwinIModel;

class FDecorationAsyncIOHelper : public std::enable_shared_from_this<FDecorationAsyncIOHelper>
{
public:
	using LinkSharedPtr = std::shared_ptr<AdvViz::SDK::ILink>;
	using ModelIdentifier = ITwin::ModelDecorationIdentifier;
	using WaitableLoadEventUPtr = std::unique_ptr<FDecorationWaitableLoadEvent>;

	FDecorationAsyncIOHelper() = default;
	~FDecorationAsyncIOHelper();

	void RequestStop();
	bool IsInitialized() const;

	void InitDecorationService(UWorld* WorldContextObject);

	void SetLoadedITwinId(const FString& ITwinId);	
	FString GetLoadedITwinId() const;
	void SetLoadedSceneId(FString InLoadedSceneId, bool inNewsScene = false);

	void RegisterWaitableLoadEvent(WaitableLoadEventUPtr&& LoadEventPtr);
	void WaitForExternalLoadEvents(int MaxSecondsToWait);

	using CallbackWithBool = std::function<void(AdvViz::expected<bool, std::string> const&)>;
	using LoadCallback = std::function<void(AdvViz::expected<void, std::string> const&)>;

	void AsyncLoadScene(CallbackWithBool callback);

	void AsyncLoadMaterials(TMap<FString, TWeakObjectPtr<AITwinIModel>> && idToIModel,
		LoadCallback OnFinishCallback,
		std::set<std::string> const& specificModels = {});
	void AsyncLoadPopulations(LoadCallback OnFinishCallback);
	void AsyncLoadAnimationKeyframes(const LoadCallback& onFinish);
	void AsyncLoadAnnotations(LoadCallback Callback);
	void AsyncLoadSplines(LoadCallback Callback);
	void AsyncLoadPathAnimations(LoadCallback Callback);


	/// Asynchronous save methods: return true if some requests were actually started.
	bool AsyncSaveSceneToServer(std::shared_ptr<AdvViz::SDK::AsyncRequestGroupCallback> CallbackPtr);
	bool AsyncSaveDecorationToServer(std::shared_ptr<AdvViz::SDK::AsyncRequestGroupCallback> CallbackPtr);
	/// Initiate the asynchronous saving of both scene and decoration.
	bool AsyncSave(std::function<void(bool)>&& OnDataSavedFunc = {});

	std::shared_ptr<AdvViz::SDK::ISplinesManager> const& GetSplinesManager();
	std::shared_ptr<AdvViz::SDK::IPathAnimator> const& GetPathAnimator();

	using ScenePtrVector = AdvViz::SDK::ScenePtrVector;
	AdvViz::expected<ScenePtrVector, AdvViz::SDK::HttpError> GetITwinScenes(const FString& itwinid);

	using SceneInfoVec = AdvViz::SDK::SceneInfoVec;
	void AsyncGetITwinSceneInfos(const FString& itwinid,
		std::function<void(AdvViz::expected<SceneInfoVec, AdvViz::SDK::HttpError> const&)>&& Callback,
		bool bExecuteCallbackInGameThread);

	void SetDecoGeoreference(const FVector& latLongHeightDeg);
	AdvViz::expected<void, std::string> InitDecoGeoreference();


	LinkSharedPtr CreateLink(ModelIdentifier const& Key);
	void CreateLinkIfNeeded(ModelIdentifier const& Key, std::function<void(LinkSharedPtr)> const& linkInitor);

	void AsyncRefreshLinks(LoadCallback&& InCallback);

	// which service to use for saving/loading scenes and decorations. This is set by default to the same value as the one in AITwinServerConnection, but can be overridden.
	EITwinSceneService InitConnexionService = EITwinSceneService::Invalid;

private:
	bool LoadITwinDecoration(std::string& OutError);
	bool LoadDecorationOrFinish(std::function<void(AdvViz::expected<void, std::string> const&)> const& OnFinishCallback);
	bool ShouldWaitForLoadEvent(bool bLogInfo = false) const;
	void ResetWaitableLoadEvents();

	struct SDecorationPartsToSave;
	SDecorationPartsToSave GetDecorationPartsToSave() const;
	bool DoAsyncSaveDecorationToServer(SDecorationPartsToSave const& WhatToSave,
		std::shared_ptr<AdvViz::SDK::AsyncRequestGroupCallback> CallbackPtr);

	bool AsyncCreateDecorationThen(
		std::shared_ptr<AdvViz::SDK::AsyncRequestGroupCallback> CallbackPtr,
		std::function<void(bool)>&& OnDecorationCreatedFunc);

private:
	FString LoadedITwinId;
	FString LoadedSceneID;
	bool bSceneIdIsForNewScene = false;

	std::shared_ptr<AdvViz::SDK::IDecoration> decoration;
	std::shared_ptr<AdvViz::SDK::IInstancesManager> instancesManager_;
	AdvViz::SDK::Tools::TSharedLockableDataPtr<std::map<AdvViz::SDK::IAnimationKeyframe::Id, AdvViz::SDK::IAnimationKeyframePtr>> animationKeyframesPtr;
	AdvViz::SDK::IInstancesGroupPtr staticInstancesGroup;
	std::shared_ptr<AdvViz::SDK::MaterialPersistenceManager> materialPersistenceMngr;
	std::shared_ptr<FString> decorationITwin; // iTwin ID corresponded to loaded decoration, if any.
	std::shared_ptr<AdvViz::SDK::IScenePersistence> scene;
	std::shared_ptr<AdvViz::SDK::ISplinesManager> splinesManager;
	std::shared_ptr<AdvViz::SDK::IAnnotationsManager> annotationsManager;
	std::shared_ptr<AdvViz::SDK::IPathAnimator> pathAnimator;

	std::shared_ptr<std::atomic_bool> shouldStop = std::make_shared<std::atomic_bool>(false);
	std::shared_ptr<std::atomic_bool> isThisValid = std::make_shared<std::atomic_bool>(true);
	bool decorationIsLinked = false;
	bool bUseDecorationService = false;
	typedef AdvViz::SDK::Tools::TLockableRWData<std::map<ModelIdentifier, LinkSharedPtr>> TLinksMap;
	TLinksMap links;

	friend class AITwinDecorationHelper;
	void PostLoadSceneFromServer();
	void InitDecorationServiceConnection(const UWorld* WorldContextObject);
	bool bNeedInitConfig = true;

	mutable std::shared_mutex WaitableLoadEventsMutex;
	std::vector<WaitableLoadEventUPtr> WaitableLoadEvents;
};
