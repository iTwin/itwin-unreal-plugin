/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDigitalTwinManager.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinLoadableLayer.h>
#include <ITwinServiceActor.h>

#include <Containers/Map.h>
#include <Templates/PimplPtr.h>

#include <ITwinDigitalTwinManager.generated.h>

class AITwinIModel;
class AITwinRealityData;
class FITwinTilesetAccess;
class UDirectionalLightComponent;
struct FITwinLoadInfo;


UENUM()
enum class EITwinLoadContext : uint8
{
	/**
	 * The model or reality data being loaded are part of a scene, currently loaded from the decoration
	 * service.
	 */
	Scene,
	/**
	 * The model or reality data is being loaded individually, typically after a click in the model panel.
	 */
	Single,
	/**
	 * Context not specified. Should be avoided - may be used though for reality data, where the context
	 * makes no difference for now...
	 */
	Unknown,
};


/// This class gathers information about an iTwin, allowing to load required components on-demand later.
UCLASS()
class ITWINRUNTIME_API AITwinDigitalTwinManager : public AITwinServiceActor
{
	GENERATED_BODY()
public:
	// Broadcasted when new information about an iModel or RealityData has been received
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FComponentInfoReceivedEvent);
	UPROPERTY()
	FComponentInfoReceivedEvent ComponentInfoReceivedEvent;

	// Triggered when the information about *all* components (iModels and RealityData) has been received.
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FComponentInfoRetrievalDoneEvent);
	UPROPERTY()
	FComponentInfoRetrievalDoneEvent ComponentInfoRetrievalDoneEvent;

	// Broadcasted when an iModel or RealityData has finished loading
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FComponentLoadedEvent, AActor*, LoadedComponent, EITwinModelType, ModelType, const FString&, LayerId);
	UPROPERTY()
	FComponentLoadedEvent ComponentLoadedEvent;

	// Broadcasted just before an iModel or RealityData is removed
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FComponentWillBeRemovedEvent, AActor*, ComponentWillBeRemoved, EITwinModelType, ComponentType);
	UPROPERTY()
	FComponentWillBeRemovedEvent ComponentWillBeRemovedEvent;

	// Broadcasted when an iModel or RealityData has been removed
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FComponentRemovedEvent, FString, StringId, EITwinModelType, ComponentType);
	UPROPERTY()
	FComponentRemovedEvent ComponentRemovedEvent;

	// Broadcasted when an iModel is about to be loaded
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLoadIModelEvent);
	UPROPERTY()
	FOnLoadIModelEvent OnLoadIModelEvent;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FITwinInfoReceivedEvent);
	UPROPERTY()
	FITwinInfoReceivedEvent ITwinInfoReceivedEvent;

	//! Triggered when the scene is loaded.
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSceneLoadedEvent, bool, bSuccess);
	UPROPERTY()
	FSceneLoadedEvent SceneLoadedEvent;

	//! Triggered when the geo-location is set (it is set at most once for the whole scene).
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGeoLocationSetEvent, bool, bFromScene, bool, bFromElevationRequest);
	UPROPERTY()
	FGeoLocationSetEvent GeoLocationSetEvent;

	//! Triggered when another the geo-located layer is loaded (can be used to to display a warning in case
	//! of large gap).
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGeoLocationGapInMeters, double, Distance, const FString&, LayerId);
	UPROPERTY()
	FGeoLocationGapInMeters GeoLocationGapInMetersEvent;

	// Helper to broadcast component info only once in a given scope.
	class [[nodiscard]] ITWINRUNTIME_API FLoadingScope
	{
	public:
		FLoadingScope(AITwinDigitalTwinManager& InMngr);
		~FLoadingScope();
	private:
		class FImpl;
		TPimplPtr<FImpl> Impl;
	};


	AITwinDigitalTwinManager();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override;
	virtual void Destroyed() override;

	/// Starts the requests fetching the content of the current iTwin.
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void UpdateITwin();

	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void ResetITwin();

	// Gather all the information about the iTwin and its components
	void Init(FString const& InITwinId, FString const& InDisplayName, bool bNoWebServicesForTesting = false);

	/// Load specified iModel or Reality Data into the scene.
	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void LoadComponent(FString const& StringId, EITwinLoadContext LoadContext);

	// Specialized method for loading iModel when export ID and changeset ID are provided (typically, when loading from browser)
	void Load(FITwinLoadInfo const& Info, EITwinLoadContext LoadContext);

	// Add iModel that is already present in the scene (like in presentations)
	void Add(FITwinLoadInfo const& Info, AITwinIModel* IModel);

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void OnIModelLoaded(bool bSuccess, FString StringId);

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void OnRealityDataInfoLoaded(bool bSuccess, FString StringId);

	/// overridden from AITwinServiceActor
	virtual void UpdateOnSuccessfulAuthorization() override;

	/// overridden from IITwinWebServicesObserver
	virtual void OnITwinInfoRetrieved(bool bSuccess, AdvViz::SDK::ITwinInfo const& Info) override;
	virtual void OnIModelsRetrieved(bool bSuccess, FIModelInfos const& Infos) override;
	virtual void OnRealityDataRetrieved(bool bSuccess, FITwinRealityDataInfos const& Infos) override;
	virtual void OnRealityData3DInfoRetrieved(bool bSuccess, FITwinRealityData3DInfo const& Info) override;

	/// overridden from FITwinDefaultWebServicesObserver
	virtual const TCHAR* GetObserverName() const override;

	const TMap<FString, FIModelInfo>& GetIModelsMap() const { return IModelsMap; }
	const TMap<FString, FITwinRealityData3DInfo>& GetRealityDataMap() const { return RealityDataMap; }

	const FString& GetITwinName() const { return DisplayName; }
	const FString& GetITwinId() const { return ITwinId; }
	FString GetComponentName(FString const& StringId) const;
	bool IsComponentLoaded(FString const& StringId) const;
	bool IsComponentBeingLoaded(FString const& StringId) const;
	EITwinLoadContext GetComponentLoadContext(FString const& StringId) const;
	bool AreComponentSavedViewsLoaded(FString const& StringId) const;

	// Get loaded IModel or Reality Data objects
	AITwinRealityData* GetRealityData(FString const& StringId) const;
	AITwinIModel* GetIModel(FString const& StringId) const;
	TUniquePtr<FITwinTilesetAccess> GetTilesetAccess(EITwinModelType ModelType, FString const& StringId) const;
	TUniquePtr<FITwinTilesetAccess> GetTilesetAccessFromId(FString const& StringId) const;

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	UDirectionalLightComponent* GetSkyLight() const;

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void SetSkyLight(UDirectionalLightComponent* InSkyLight);

	// Get/set active (primary) IModel
	AITwinIModel* GetActiveIModel() const { return GetIModel(ActiveModelId); }
	void SetActiveIModel(FString const& StringId) { ActiveModelId = StringId; }

	TMap<FString/*iModelID*/, UITwinSynchro4DSchedules*> const& GetSynchro4DSchedules() const {
		return Synchro4DSchedules;
	}

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void RemoveComponent(FString const& StringId);

	UFUNCTION()
	void OnSceneLoaded(bool bSuccess);

	void SetIsLoadingScene(bool bIsLoading);
	bool IsLoadingScene() const { return bIsLoadingScene; }
	
	bool IsIModel(FString const& StringId) const;
	bool IsRealityData(FString const& StringId) const;

	bool HasLoadingPending(bool bLogState = false) const;

	int32 CountProcessedIModelAttachmentRequests() const { return NumProcessedIModelAttachmentRequests; }

	//! Returns whether the request retrieving iTWin information is completed.
	bool HasRetrievedITwinInfo() const;

	//! Return the IDs of all layers of the given type currently loaded in the scene.
	//! \param bOnlyCountFullyLoadedModels if true, layers added to the scene but not yet fully loaded will
	//! be ignored.
	TSet<FString> GetLoadedLayers(EITwinModelType LayerType, bool bOnlyCountFullyLoadedLayers) const;

	TSet<FString> GetLoadedIModels(bool bOnlyCountFullyLoadedModels) const {
		return GetLoadedLayers(EITwinModelType::IModel, bOnlyCountFullyLoadedModels);
	}

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void SetAutoLoadAllComponents(bool bInAutoLoadAllComponents);

	//! Start loading the decoration attached to this iTwin, if any.
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void LoadDecoration();

	//! Posts a request to start saving the decoration attached to this iTwin, if any.
	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void SaveDecoration();

	//! Returns true if the geo-location of the scene has been set (either from a loaded layer or from the
	//! scene itself).
	bool IsGeoLocationSet() const;


protected:
	virtual void ConnectLoadedIModelToUI(AITwinIModel* IModel) const;

private:
	void RequestData();

	// Load specified iModel into the scene.
	void LoadIModel(FIModelInfo const& Info, EITwinLoadContext LoadContext);

	// Load specified Reality Data into the scene.
	void LoadRealityData(FITwinRealityData3DInfo const& Info, EITwinLoadContext LoadContext);

	void SetLoadStatus(EITwinModelType ModelType, FString const& StringId, EITwinLayerLoadStatus NewStatus);

	void OnComponentInfoRetrieved();

	void OnComponentLoaded(AActor* LoadedObject, EITwinModelType ModelType, const FString& LayerId);

	void SetupSpawnedLayer(AITwinServiceActor* NewLayer);

private:
	// iTwin info
	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	FString ITwinId;

	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	FString DisplayName;

	/// Map of all the iModels present in the iTwin (loaded or not in the scene).
	UPROPERTY(Category = "iTwin",
		BlueprintReadOnly,
		Meta = (AllowPrivateAccess))
	TMap<FString, FIModelInfo> IModelsMap;

	/// Map of all the Reality-Data layers present in the iTwin (loaded or not in the scene).
	UPROPERTY(Category = "iTwin",
		BlueprintReadOnly,
		Meta = (AllowPrivateAccess))
	TMap<FString, FITwinRealityData3DInfo> RealityDataMap;

	/// Map of all the iModels present in the iTwin, with their load status.
	/// The key is the display name of the iModel, for easier understanding in Editor.
	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	TMap<FString, FITwinLoadableLayerHelper> IModelLoadStatusMap;

	/// Map of all the Reality-Data layers present in the iTwin, with their load status.
	/// The key is the display name of the Reality-Data, for easier understanding in Editor.
	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	TMap<FString, FITwinLoadableLayerHelper> RealityDataLoadStatusMap;

	FString ActiveModelId;
	/// Map of already loaded objects (keep in mind that iModels added to this list may not be fully loaded yet)
	TMap<FString/*iModelID*/, AActor*> LoadedObjects;
	TMap<FString/*iModelID*/, UITwinSynchro4DSchedules*> Synchro4DSchedules;

	/// Components to load once this manager is fully initialized.
	TMap<FString, EITwinLoadContext> PendingLoadIds;

	/// If true, all iModels and Reality Data contained in the iTwin will be automatically loaded once
	/// discovered.
	UPROPERTY(Category = "iTwin",
		EditAnywhere,
		BlueprintSetter = SetAutoLoadAllComponents)
	bool bAutoLoadAllComponents = false;

	// components that have been fully loaded
	TSet<FString> CompletedLoadIds;

	int32 NumProcessedIModelAttachmentRequests = 0;

	UDirectionalLightComponent* SkyLight = nullptr;
	int32 NumLoadingScopes = 0;
	bool bIsLoadingScene = false;

	class FImpl;
	TPimplPtr<FImpl> Impl;
};
