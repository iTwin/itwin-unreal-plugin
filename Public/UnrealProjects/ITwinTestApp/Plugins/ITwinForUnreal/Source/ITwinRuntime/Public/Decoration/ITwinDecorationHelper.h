/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDecorationHelper.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <GameFramework/Actor.h>
#include <Templates/PimplPtr.h>

#include <ITwinLoadInfo.h>

#include <memory>
#include <string>
#include <optional>
#include <vector>
#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Tools/Error.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <ITwinDecorationHelper.generated.h>

class FDecorationAsyncIOHelper;
class FViewport;
class AITwinIModel;
class AITwinPopulation;
class AITwinPopulationWithPath;
class AITwinKeyframePath;
class AITwinSplineTool;
class AITwinSplineHelper;

namespace AdvViz::SDK {
	struct ITwinAtmosphereSettings;
	struct ITwinSceneSettings;
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

	AITwinDecorationHelper();

	// Callbacks for the different I/O operations

	UPROPERTY(BlueprintAssignable)
	FOnDecorationIODone OnDecorationSaved;

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

	/** Delegate when decoration is fully loaded. */
	FSimpleMulticastDelegate OnDecorationLoaded;


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

	//! Start loading the decoration attached to current model, if any (asynchronous).
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void LoadDecoration();

	void LoadIModelMaterials(AITwinIModel& IModel);

	//! Returns true if the loading of a decoration is in progress.
	bool IsLoadingDecoration() const;

	//! Returns true if the loading of a scene is in progress.
	bool IsLoadingScene() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool IsPopulationEnabled() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool IsMaterialEditionEnabled() const;

	//! Start saving the decoration attached to current model, if some modifications were applied.
	//! If bPromptUser is true, a message box will be displayed to confirm he wants to save his editions.
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void SaveDecoration(bool bPromptUser = true);

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool ShouldSaveDecoration(bool bPromptUser = true) const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SaveDecorationOnExit(bool bPromptUser = true);

	//! Permanently deletes all material customizations for current model (cannot be undone!)
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void DeleteAllCustomMaterials();

	AITwinPopulation* GetPopulation(FString assetPath, const AdvViz::SDK::RefID& groupId) const;
	AITwinKeyframePath* CreateKeyframePath() const;
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

	ITwinSceneInfo GetSceneInfo(const ModelIdentifier& Key) const;

	inline ITwinSceneInfo GetSceneInfo(EITwinModelType ct, const FString& id) const {
		return GetSceneInfo(std::make_pair(ct, id));
	}
	void SetSceneInfo(const ModelIdentifier& Key, const ITwinSceneInfo&) const;

	inline void SetSceneInfo(EITwinModelType ct, const FString& id, const ITwinSceneInfo& InInfo) const {
		SetSceneInfo(std::make_pair(ct, id), InInfo);
	}
	void CreateLinkIfNeeded(EITwinModelType ct, const FString& id) const;
	bool DeleteLoadedScene(); //if false, abort 
	void RemoveComponent(EITwinModelType ct, const FString& id) const;

	void ConnectSplineToolToSplinesManager(AITwinSplineTool* splineTool);

	// return link identifiers found in scene
	std::vector<ITwin::ModelLink> GetLinkedElements() const;

	[[nodiscard]] std::shared_ptr<SaveLocker> LockSave();
	bool IsSaveLocked();

	void OverrideOnSceneClose(bool bOverride) { bOverrideOnSceneClose_ = bOverride; }

	void SetHomeCamera(const FTransform&);
	FTransform GetHomeCamera() const;

	FString GetSceneID() const;
	void InitDecorationService();


	AdvViz::expected<std::vector<std::shared_ptr< AdvViz::SDK::IScenePersistence>>, int>  GetITwinScenes(const FString& itwinid);

	std::shared_ptr<AdvViz::SDK::IAnnotationsManager> GetAnnotationManager() const;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void OnIModelLoaded(bool bSuccess, FString StringId);
	UFUNCTION()
	void OnRealityDataLoaded(bool bSuccess, FString StringId);
	void OnCloseRequested(FViewport* Viewport);

	// Enabled/disables windows system FMessageDialog so that it can be replaced with an Unreal widget
	bool bOverrideOnSceneClose_ = false;

	class FImpl;
	TPimplPtr<FImpl> Impl;

	//save lock
	class SaveLockerImpl;

	void Lock(SaveLockerImpl*);
	void Unlock(SaveLockerImpl*);
	

};
