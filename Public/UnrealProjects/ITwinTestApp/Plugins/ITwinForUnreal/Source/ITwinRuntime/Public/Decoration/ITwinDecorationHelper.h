/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDecorationHelper.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <GameFramework/Actor.h>
#include <Templates/PimplPtr.h>

#include <ITwinLoadInfo.h>

#include <functional>
#include <memory>
#include <string>
#include <optional>
#include <vector>
#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Tools/Error.h>
#	include <SDK/Core/Visualization/MaterialPersistence.h>
#	include <SDK/Core/Visualization/ScenePersistence.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <ITwinDecorationHelper.generated.h>

class FDecorationAsyncIOHelper;
class FDecorationWaitableLoadEvent;
class FViewport;
class AITwinIModel;
class AITwinPopulation;
class AITwinPopulationWithPath;
class AITwinKeyframePath;
class AITwinSplineTool;
class AITwinSplineHelper;
class UITwinContentManager;
class AITwinAnimPathManager;

namespace AdvViz::SDK {
	struct ITwinAtmosphereSettings;
	struct ITwinSceneSettings;
	struct ITwinHDRISettings;
	class RefID;
	class IScenePersistence;
	class IAnnotationsManager;
}

struct ITwinSceneInfo
{
	std::optional<bool> Visibility;
	std::optional<double> Quality;
	std::optional<FTransform> Offset;
};

USTRUCT(BlueprintType)
struct FTwinSceneBasicInfo
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "iTwinInfo")
		FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "iTwinInfo")
		FString ITwinId;

	UPROPERTY(BlueprintReadOnly, Category = "iTwinInfo", VisibleAnywhere)
		FString DisplayName;
};

USTRUCT(BlueprintType)
struct FTwinSceneBasicInfos
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "iTwinInfo")
		TArray<FTwinSceneBasicInfo> Scenes;
};

UENUM(BlueprintType)
enum class EITwinDecorationClientMode : uint8
{
	/**
	 * Unspecified client. Default behavior will be used.
	 */
	Unknown,
	/**
	 * Advanced Visualization Application.
	 */
	AdvVizApp,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDecorationIOStartStop, bool, bStart);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDecorationIODone, bool, bSuccess);
DECLARE_DELEGATE_RetVal_OneParam(bool, FOnDownloadRequest, const FString&);
UCLASS()
class ITWINRUNTIME_API AITwinDecorationHelper : public AActor
{
	GENERATED_BODY()

public:

	class [[nodiscard]] SaveLocker
	{
	protected:
		SaveLocker();
	public:
		virtual ~SaveLocker();

	};

	/// Can be used to avoid refreshing links in a given scope.
	class [[nodiscard]] RefreshLinksDisabler
	{
	public:
		RefreshLinksDisabler(AITwinDecorationHelper& InHelper);
		~RefreshLinksDisabler();
	private:
		class FImpl;
		TPimplPtr<FImpl> Impl;
	};

	/// Sets whether the Component Center is used (must be called before InitContentManager, as it does
	/// condition some error messages when checking some paths that should exist if we don't use it).
	static void SetUseComponentCenter(bool bComponentCenter);
	/// Returns whether the Component Center is currently used.
	static bool UseComponentCenter();

	AITwinDecorationHelper();

	// Callbacks for the different I/O operations

	UPROPERTY(BlueprintAssignable)
	FOnDecorationIODone OnSceneSaved;

	UPROPERTY(BlueprintAssignable)
	FOnDecorationIODone OnPopulationsLoaded;

	UPROPERTY(BlueprintAssignable)
	FOnDecorationIODone OnMaterialsLoaded;

	UPROPERTY(BlueprintAssignable)
	FOnDecorationIODone OnSceneLoaded;

	UPROPERTY(BlueprintAssignable)
	FOnDecorationIOStartStop OnSceneLoadingStartStop;

	UPROPERTY(BlueprintAssignable)
	FOnDecorationIODone OnSplinesLoaded;

	UPROPERTY(BlueprintAssignable)
	FOnDecorationIODone OnAnnotationsLoaded;

	FOnDownloadRequest OnDownloadRequest;

	/** Delegate when decoration is fully loaded. */
	FSimpleMulticastDelegate OnDecorationLoaded;

	UPROPERTY()
	UITwinContentManager* iTwinContentManager = nullptr;

	//! Sets the decoration client mode. Can be used to customize the handling of decorations for specific
	//! usages.
	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void SetDecorationClientMode(EITwinDecorationClientMode ClientMode);

	//! Returns the current decoration client mode.
	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	EITwinDecorationClientMode GetDecorationClientMode() const;


	//! Set information about the associated iTwin/iModel
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetLoadedITwinId(FString InLoadedSceneId);

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	FString GetLoadedITwinId() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetLoadedSceneId(FString InLoadedSceneId, bool inNewsScene = false);

	//! Start loading the scene selected by SetLoadedSceneId(asynchronous).
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void LoadScene();

	//! Returns the root directory path for the official content.
	FString GetContentRootPath() const;

	void LoadIModelMaterials(AITwinIModel& IModel);

	//! Returns true if the loading of a scene is in progress.
	bool IsLoadingScene() const;

	//! Registers an event to wait at the end of the scene loading, for synchronization between the loading
	//! of the scene and some iTwin requests.
	void RegisterWaitableLoadEvent(std::unique_ptr<FDecorationWaitableLoadEvent>&& LoadEventPtr);

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool IsPopulationEnabled() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool IsMaterialEditionEnabled() const;

	//! Start saving the Scene attached to current model
	//! If bPromptUser is true, a message box will be displayed to confirm he wants to save his editions.
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void SaveScene(bool bPromptUser = true);

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool ShouldSaveScene() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SaveSceneOnExit(bool bPromptUser = true);

	struct FSaveRequestOptions
	{
		bool bPromptUser = true;
		bool bUponExit = false;
		bool bUponCustomMaterialsDeletion = false;
		std::function<void()> OnSceneSavedCallback;
	};
	void SaveSceneWithOptions(FSaveRequestOptions const& Options);

	//! Permanently deletes all material customizations for current model (cannot be undone!)
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void DeleteAllCustomMaterials();

	AITwinPopulation* GetPopulation(FString assetPath, const AdvViz::SDK::RefID& groupId) const;
	AITwinKeyframePath* CreateKeyframePath() const;
	bool MountPak(const std::string & file, const std::string& id) const;
	AITwinPopulation* CreatePopulation(FString assetPath, const AdvViz::SDK::RefID& groupId) const;
	AITwinPopulation* GetOrCreatePopulation(FString assetPath, const AdvViz::SDK::RefID& groupId) const;
	int32 GetPopulationInstanceCount(FString assetPath, const AdvViz::SDK::RefID& groupId) const;
	AdvViz::SDK::RefID GetStaticInstancesGroupId() const;
	AdvViz::SDK::RefID GetInstancesGroupIdForSpline(const AITwinSplineHelper& Spline) const;

	AdvViz::SDK::ITwinAtmosphereSettings GetAtmosphereSettings() const;
	void SetAtmosphereSettings(const AdvViz::SDK::ITwinAtmosphereSettings&) const;

	AdvViz::SDK::ITwinSceneSettings GetSceneSettings() const;
	void SetSceneSettings(const AdvViz::SDK::ITwinSceneSettings& as) const;

	using ModelIdentifier = ITwin::ModelDecorationIdentifier;

	void GetSceneInfoThen(const ModelIdentifier& Key, std::function<void(ITwinSceneInfo)>&& Callback) const;
	std::shared_ptr<AdvViz::SDK::ILink> GetRawSceneInfoSynchronous(const ModelIdentifier& Key) const;

	inline void GetSceneInfoThen(EITwinModelType ct, const FString& id, std::function<void(ITwinSceneInfo)>&& Callback) const {
		GetSceneInfoThen(std::make_pair(ct, id), std::move(Callback));
	}
	void SetSceneInfo(const ModelIdentifier& Key, const ITwinSceneInfo&) const;

	inline void SetSceneInfo(EITwinModelType ct, const FString& id, const ITwinSceneInfo& InInfo) const {
		SetSceneInfo(std::make_pair(ct, id), InInfo);
	}
	void CreateLinkIfNeeded(EITwinModelType ct, const FString& id) const;
	bool DeleteLoadedScene(); //if false, abort 
	void RemoveComponent(EITwinModelType ct, const FString& id) const;

	void ConnectSplineToolToSplinesManager(AITwinSplineTool* splineTool);
	void ConnectPathAnimator(AITwinAnimPathManager* manager);

	// return link identifiers found in scene
	std::vector<ITwin::ModelLink> GetLinkedElements() const;

	[[nodiscard]] std::shared_ptr<SaveLocker> LockSave();
	bool IsSaveLocked() const;

	void OverrideOnSceneClose(bool bOverride) { bOverrideOnSceneClose_ = bOverride; }

	void SetHomeCamera(const FTransform&);
	FTransform GetHomeCamera() const;

	FString GetSceneID() const;
	void InitDecorationService();

	void SetDecoGeoreference(const FVector& latLongHeight);
	AdvViz::expected<void, std::string> InitDecoGeoreference();

	AdvViz::expected<std::vector<std::shared_ptr< AdvViz::SDK::IScenePersistence>>, AdvViz::SDK::HttpError> GetITwinScenes(const FString& itwinid);

	void AsyncGetITwinSceneInfos(const FString& itwinid,
		std::function<void(AdvViz::expected<FTwinSceneBasicInfos, AdvViz::SDK::HttpError> const&)>&& Callback,
		bool bExecuteCallbackInGameThread);


	std::shared_ptr<AdvViz::SDK::IAnnotationsManager> GetAnnotationManager() const;

	/// Export the given HDRI definition to json format.
	std::string ExportHDRIAsJson(AdvViz::SDK::ITwinHDRISettings const& hdri) const;

	bool ConvertHDRIJsonFileToKeyValueMap(std::string assetPath, AdvViz::SDK::KeyValueStringMap& keyValueMap) const;


	//allow disable export of resources (IModels/RealityData) when saving the scene API. Scene API is not responsible for managing these resources in Itwin Engage and want to avoid sending them to the server when saving the scene 
	static void EnableExportOfResourcesInSceneApI(bool bEnable);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void OnIModelLoaded(bool bSuccess, FString StringId);
	UFUNCTION()
	void OnRealityDataLoaded(bool bSuccess, FString StringId);
	void OnCloseRequested(FViewport* Viewport);
	void InitContentManager();


	static std::optional<bool> bHasComponentCenterOpt;

	// Enabled/disables windows system FMessageDialog so that it can be replaced with an Unreal widget
	bool bOverrideOnSceneClose_ = false;

	class FImpl;
	TPimplPtr<FImpl> Impl;

	// save lock
	class SaveLockerImpl;

	void Lock(SaveLockerImpl&);
	void Unlock(SaveLockerImpl const&);
	

public:
	void SetReadOnlyMode(bool value);
};
