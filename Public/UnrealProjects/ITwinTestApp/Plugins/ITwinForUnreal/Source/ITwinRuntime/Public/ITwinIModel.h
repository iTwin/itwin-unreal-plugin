/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinIModel.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinCoordSystem.h>
#include <ITwinFwd.h>
#include <ITwinLoadInfo.h>
#include <ITwinServiceActor.h>
#include <Misc/Optional.h>
#include <Templates/PimplPtr.h>
#include <memory>
#include <ITwinIModel.generated.h>

struct FITwinIModel3DInfo;
struct FChangesetInfos;
struct FITwinExportInfos;
struct FCesium3DTilesetLoadFailureDetails;
class FITwinTilesetAccess;
struct FSavedViewInfos;
struct FSavedViewGroupInfos;
class UITwinClipping3DTilesetHelper;
class UITwinMaterialDefaultTexturesHolder;
class ULightComponent;
namespace AdvViz::SDK
{
	enum class EChannelType : uint8_t;
	enum class ETextureSource : uint8_t;
	enum class EMaterialKind : uint8_t;
	struct ITwinMaterial;
	struct ITwinUVTransform;
	class MaterialPersistenceManager;
}
namespace BeUtils
{
	class GltfMaterialHelper;
	class GltfTuner;
}

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnIModelLoaded, bool, bSuccess, FString, IModelId);

UENUM()
enum class EITwinExportStatus : uint8
{
	Unknown,
	NoneFound,
	InProgress,
	Complete,
};


UENUM(BlueprintType)
enum class ELoadingMethod : uint8
{
	LM_Automatic UMETA(DisplayName = "Automatic"),
	LM_Manual UMETA(DisplayName = "Manual")
};

USTRUCT()
struct FGetAllSavedViewsProgress
{
	GENERATED_USTRUCT_BODY()
	int GroupsProcessed = 0;
	int GroupsCount = 0;
};

UCLASS()
class ITWINRUNTIME_API AITwinIModel : public AITwinServiceActor
{
	GENERATED_BODY()
public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFinishedLoadingSavedViewsEvent, const FString&, ID);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSavedViewsRetrievedEvent, bool, bSuccess, FSavedViewInfos, SavedViews);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSavedViewGroupsRetrievedEvent, bool, bSuccess, FSavedViewGroupInfos, SavedViewGroups);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSavedViewGroupAddedEvent, bool, bSuccess, const FSavedViewGroupInfo&, SavedViewGroup);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSavedViewAddedEvent, bool, bSuccess, const FSavedViewInfo&, SavedView);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnConfirmLoadNewChangeset, FString, IModelId, FString, IModelName, FString, NewChangesetId, FString, NewChangesetName);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnElementPropertiesRetrieved, bool, bSuccess, const FElementProperties&, ElementProps, const FString&, ElementId);
	UPROPERTY()
	FOnFinishedLoadingSavedViewsEvent FinishedLoadingSavedViews;
	UPROPERTY()
	FOnSavedViewsRetrievedEvent SavedViewsRetrieved;
	UPROPERTY()
	FOnSavedViewGroupsRetrievedEvent SavedViewGroupsRetrieved;
	UPROPERTY()
	FOnSavedViewGroupAddedEvent SavedViewGroupAdded;
	UPROPERTY()
	FOnSavedViewAddedEvent SavedViewAdded;
	UPROPERTY()
	FOnConfirmLoadNewChangeset ConfirmLoadNewChangeset;
	UPROPERTY()
	FOnElementPropertiesRetrieved ElementPropertiesRetrieved;


	UPROPERTY(Category = "iTwin|Loading",
		EditAnywhere)
	ELoadingMethod LoadingMethod = ELoadingMethod::LM_Manual;

	UPROPERTY(Category = "iTwin|Loading",
		meta = (EditCondition = "LoadingMethod == ELoadingMethod::LM_Automatic", DisplayName = "iModel Id"),
		EditAnywhere)
	FString IModelId;

	UPROPERTY(Category = "iTwin",
		VisibleAnywhere)
	FString ITwinId;

	//! Editable changeset ID. Use of the latest changeset can be asked explicitly by setting the special value
	//! "LATEST" here (case insensitive). If LoadingMethod is ELoadingMethod::LM_Manual, the latest changeset will
	//! also be used automatically when the changesetId is empty.
	//! See ResolvedChangesetId.
	UPROPERTY(Category = "iTwin|Loading",
		meta = (EditCondition = "LoadingMethod == ELoadingMethod::LM_Automatic"),
		EditAnywhere)
	FString ChangesetId;

	//! The resolved changeset ID, computed as follows:
	//! - If ChangesetId is not empty and not "LATEST" (case insensitive), then ResolvedChangesetId is same
	//!	  as ChangesetId.
	//! - Otherwise, ResolvedChangesetId is the latest changeset given by the iModel Hub.
	//!   Note that in this case, if the iModel does not have any changeset (only a baseline file)
	//!   then ResolvedChangesetId will be empty.
	UPROPERTY(Category = "iTwin|Loading",
		VisibleAnywhere)
	FString ResolvedChangesetId;

	//! Indicates whether ResolvedChangesetId has been computed/updated and is valid.
	UPROPERTY(Category = "iTwin|Loading",
		VisibleAnywhere)
	bool bResolvedChangesetIdValid = false;

	//! Current export status of the iModel.
	//! Call Export() to update this status.
	UPROPERTY(Category = "iTwin|Loading",
		VisibleAnywhere)
	EITwinExportStatus ExportStatus = EITwinExportStatus::Unknown;

	//! Synchro4D schedules found on this iModel.
	UPROPERTY(Category = "iTwin",
		VisibleAnywhere,
		BlueprintGetter = GetSynchro4DSchedules)
	UITwinSynchro4DSchedules* Synchro4DSchedules = nullptr;
	UFUNCTION(BlueprintGetter)
	UITwinSynchro4DSchedules* GetSynchro4DSchedules();

	/// Clear the persistence cache for this iModel Elements metadata, used for the interpretation of the 4D schedule.
	/// This is a helper tool that will not cancel (nor automatically restart) the loading of the iModel or its 4D
	/// data, it will only do anything if the metadata *and* 4D loading processes are complete (no in-flight HTTP
	/// requests left).
	/// See also the equivalent function on UITwinSynchro4DSchedules, for the 4D data cache.
	/// @return True when the iModel was able to delete its cache
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool ClearMetadataCacheWithConfirmation();

	/// Same as ClearCacheWithConfirmation, but no return value in order to have a button in the Editor
	UFUNCTION(Category = "iTwin", CallInEditor)
	void ClearMetadataCache();

	/// Clear both metadata and 4D schedule caches
	/// @see ClearCacheOnlyThis, UITwinSynchro4DSchedules::ClearCacheOnlyThis
	/// @return True when both the iModel and its schedule (if any) were able to delete their cache
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool ClearMetadataAnd4DCachesWithConfirmation();

	/// Same as ClearMetadataAnd4DCachesWithConfirmation, but no return value in order to have a button in the Editor
	UFUNCTION(Category = "iTwin", CallInEditor)
	void ClearMetadataAnd4DCaches();

	/// Sets whether the Synchro4D schedule should be automatically loaded. In test context, if the automatic
	/// loading was disabled through Disable4DAutoLoadSchedule, this function will have no effect and the
	/// automatic loading will remain disabled.
	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void SetSynchro4DAutoLoadSchedule(bool bInSynchro4DAutoLoadSchedule);

	//! When false, Synchro4D schedule queries and loading will not happen. If some queries have been already
	//! started, setting to false will not prevent their replies from being handled, but no new query will be
	//! emitted: they will be stacked and should restart correctly when the flag is set to true again
	//! (UNTESTED though). It is recommended to set to false before the actor starts ticking, or at least
	//! before the iModel Elements metadata have finished querying/loading.
	UPROPERTY(Category = "iTwin", meta = (DisplayName = "Auto-Load Synchro4D Schedule"),
		EditAnywhere,
		BlueprintSetter = SetSynchro4DAutoLoadSchedule)
	bool bSynchro4DAutoLoadSchedule = true;

	//! When true, the movie sequencer will wait for the 4D schedule to be fully loaded before starting to render
	//! a sequence. When false it will not, which assumes the 4D animation is not to be applied to the movie sequence,
	//! or that the user will take care of waiting for the schedule to be loaded before starting the sequence.
	UPROPERTY(Category = "iTwin", EditAnywhere)
	bool bMovieSequencerWaitsForSchedule = true;

	UFUNCTION(BlueprintGetter)
	double GetScheduleDownloadPercentComplete() const;


private:
	/// Percentage of the data needed to replay a 4D schedule (if any) that is estimated to be available.
	/// Includes internal iModel data not strictly part of the schedule but required for it to replay:
	/// this data needs to be downloaded even when there is no schedule. When the schedule is fully available,
	/// or when it had been determined that there is no schedule, the variable is set to 100.
	UPROPERTY(Category = "iTwin",
		VisibleAnywhere,
		BlueprintGetter = GetScheduleDownloadPercentComplete)
	double ScheduleDownloadPercentComplete = 0.;

public:
	AITwinIModel();
	~AITwinIModel();
	/// Called when placed in editor or spawned: override to force spawning by default at (0,0,0), otherwise
	/// you get a geo offset that you probably didn't want in the first place. I had tried
	/// OnConstruction(FTransform) first but, bad idea, it gets called everytime the construction script is
	/// re-run, for example after editing property fields!! (witnessed when changing schedule component's 
	/// time...)
	virtual void PostActorCreated() override;
	virtual void Destroyed() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void PostLoad() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }
	// also from UObject "interface":
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	//! To be called at least once after ServerConnection, IModelId, ChangesetId have been set.
	//! This will query the mesh export service for a corresponding export, and if complete one is found,
	//! It will spawn the corresponding Cesium tileset.
	//! In any case, this will also update ExportStatus.
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void UpdateIModel();

	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void ZoomOnIModel();

	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void AdjustPawnSpeedToExtents();

	UFUNCTION(Category = "iTwin|Info",
		BlueprintCallable)
	void GetModel3DInfo(FITwinIModel3DInfo& Info) const;

	UFUNCTION(Category = "iTwin|Info",
		BlueprintCallable)
	void GetModel3DInfoInCoordSystem(FITwinIModel3DInfo& OutInfo, EITwinCoordSystem CoordSystem) const;

	UFUNCTION(Category = "iTwin|Load",
		BlueprintCallable)
	void SetModelLoadInfo(FITwinLoadInfo InLoadInfo);

	UFUNCTION(Category = "iTwin|Load",
		BlueprintCallable)
	FITwinLoadInfo GetModelLoadInfo() const;

	UFUNCTION(Category = "iTwin|Load",
		BlueprintCallable)
	void LoadModel(FString ExportId);

	UPROPERTY(Category = "iTwin|Load",
		BlueprintAssignable)
	FOnIModelLoaded OnIModelLoaded;

	//! Returns the changeset currently selected for loading (whether manually by the user or automatically).
	UFUNCTION(Category = "iTwin|Load",
		BlueprintCallable)
	FString GetSelectedChangeset() const;

	//! Start a new export of the iModel by the mesh export service.
	//! If the export is successfully started, the actor will regularly check for its completion and the
	//! tileset will be loaded automatically as soon as the export is complete.
	UFUNCTION(Category = "iTwin|Load",
		CallInEditor,
		BlueprintCallable)
	void StartExport();


	//! Globally enable or disable saved view updates.
	static void EnableSavedViewsUpdates(bool bEnableSV);
	//! Returns whether saved view updates are globally enabled.
	static bool AreSavedViewsUpdatesEnabled();

#if WITH_TESTS
	//! Used in some automated tests, to permanently disable auto-loading of 4D schedules.
	static void Disable4DAutoLoadSchedule(bool bDisable);
#endif


	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	AITwinSavedView* GetITwinSavedViewActor(const FString& SavedViewId);

	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void UpdateSavedViews();

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void ShowConstructionData(bool bShow);

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void UpdateConstructionData();

	void HideCategories(std::vector<std::string> const& InCategoryIDs, bool forceUpdate);

	void HideModels(std::vector<std::string> const& InModelIDs, bool forceUpdate);

	void HideElements(std::vector<std::string> const& InElementIDs, bool forceUpdate);

	void ShowElements(std::vector<std::string> const& InElementIDs, bool forceUpdate);

	void HideCategoriesPerModel(std::vector<std::string> const& InModelIDs, std::vector<std::string> const& InCategoryIDs, bool forceUpdate);

	void ShowCategoriesPerModel(std::vector<std::string> const& InModelIDs, std::vector<std::string> const& InCategoryIDs, bool forceUpdate);

	UPROPERTY(Category = "iTwin", EditAnywhere)
	bool bShowConstructionData = false;

	//! Deselect any element previously selected. This will disable the selection highlight, if any.
	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void DeSelectElements();

	//! Returns true if the given element is currently selected (highlighted).
	bool IsElementSelected(const FString& ElementId) const;

	//! Returns true if the iModel contains an element with the given ID.
	bool HasElementWithID(const FString& ElementId) const;

	//! Deselect any material previously selected. This will disable the selection highlight, if any.
	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void DeSelectMaterials();

	//! Deselect any element or material previously selected. This will disable the selection highlight, if
	//! any.
	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void DeSelectAll();

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void AddSavedView(const FString& displayName, const FString& groupId = "");

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void AddSavedViewGroup(const FString& groupName);

	UFUNCTION(Category = "iTwin|Tree",
		BlueprintCallable)
	void GetPagedNodes(const FString& KeyString = "", int Offset = 0, int Count = 1000);

	UFUNCTION(Category = "iTwin|Tree",
		BlueprintCallable)
	void GetModelFilteredNodes(const FString& Filter);

	UFUNCTION(Category = "iTwin|Tree",
		BlueprintCallable)
	void GetCategoryFilteredNodes(const FString& Filter);

	UFUNCTION(Category = "iTwin|Tree",
		BlueprintCallable)
	void GetCategoryNodes(const FString& KeyString = "");

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void GetElementProperties(const FString& ElementId);

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	bool SelectElement(const FString& ElementId);

	/// Select multiple elements at once, with additive highlight.
	void SelectElements(const TArray<FString>& ElementIds);

	/// Add elements to the current selection (additive, does not clear existing selection).
	void AddElementsToSelection(const TArray<FString>& ElementIds);

	/// Remove specific elements from the current selection.
	void RemoveElementsFromSelection(const TArray<FString>& ElementIds);

	UFUNCTION(Category = "iTwin|Load",
		BlueprintCallable)
	void Reset();

	UFUNCTION(Category = "iTwin|Load",
		BlueprintCallable)
	void RefreshTileset();

	UFUNCTION(Category = "iTwin|Load",
		BlueprintCallable)
	void OnLoadNewChangesetConfirmation(const FString& NewChangesetId, bool bLoadNewChangeset);

#if WITH_EDITOR
	//! TEMPORARY (for tests). Triggers a re-tune of the glTF model.
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void Retune();
#endif // WITH_EDITOR

	//! Globally enable or disable material tuning features.
	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	static void EnableMaterialTuning(bool bEnable);

	//! Returns whether material tuning features are globally enabled.
	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	static bool IsMaterialTuningEnabled();

	void InitializeMaterialTuning();

	//! Highlight the parts of the model using the given iTwin Material ID.
	void HighlightMaterial(uint64 MaterialID);

	//! Returns the map of ITwin material info - the key being the iTwin Material ID, and the value, the
	//! display name of the material.
	TMap<uint64, FString> GetITwinMaterialMap() const;
	FString GetMaterialName(uint64_t MaterialId, bool bForMaterialEditor = false) const;

	//! Simple API for material tuning based on the original material definitions of the iModel.
	double GetMaterialChannelIntensity(uint64_t MaterialId, AdvViz::SDK::EChannelType Channel) const;
	void SetMaterialChannelIntensity(uint64_t MaterialId, AdvViz::SDK::EChannelType Channel, double Intensity);

	//! Return the color defined for this channel. Beware the 'A' component of this color has no meaning for
	//! EChannelType::Color, as the opacity is to be retrieved from EChannelType::Opacity.
	FLinearColor GetMaterialChannelColor(uint64_t MaterialId, AdvViz::SDK::EChannelType Channel) const;
	//! Sets the color for the given channel. Beware the 'A' component of this color will be ignored for
	//! EChannelType::Color, as the opacity is controlled by the EChannelType::Opacity channel.
	void SetMaterialChannelColor(uint64_t MaterialId, AdvViz::SDK::EChannelType Channel, FLinearColor const& Color);

	UITwinMaterialDefaultTexturesHolder const& GetDefaultTexturesHolder();
	FString GetMaterialChannelTextureID(uint64_t MaterialId, AdvViz::SDK::EChannelType Channel, AdvViz::SDK::ETextureSource& OutSource) const;
	void SetMaterialChannelTextureID(uint64_t MaterialId, AdvViz::SDK::EChannelType Channel,
		FString const& TextureId, AdvViz::SDK::ETextureSource eSource);

	AdvViz::SDK::ITwinUVTransform GetMaterialUVTransform(uint64_t MaterialId) const;
	void SetMaterialUVTransform(uint64_t MaterialId, AdvViz::SDK::ITwinUVTransform const& UVTransform);

	AdvViz::SDK::EMaterialKind GetMaterialKind(uint64_t MaterialId) const;
	void SetMaterialKind(uint64_t MaterialId, AdvViz::SDK::EMaterialKind NewKind);

	//! Retrieves some properties (base material kind, translucency requirement) required by material
	//! customizations.
	//! Returns whether the given material has a custom definition.
	bool GetMaterialCustomRequirements(uint64_t MaterialId, AdvViz::SDK::EMaterialKind& OutMaterialKind,
		bool& bOutRequiresTranslucency) const;

	//! Rename a material.
	bool SetMaterialName(uint64_t MaterialId, FString const& NewName);

	//! Load a material from an asset file (expecting an asset of class #UITwinMaterialDataAsset).
	bool LoadMaterialFromAssetFile(uint64_t MaterialId, FString const& AssetFilePath,
		TOptional<FString> const& CustomTextureDir = {});

	using GltfMaterialHelperPtr = std::shared_ptr<BeUtils::GltfMaterialHelper>;
	std::shared_ptr<BeUtils::GltfMaterialHelper> const& GetGltfMaterialHelper() const;

	using MaterialPersistencePtr = std::shared_ptr<AdvViz::SDK::MaterialPersistenceManager>;
	static void SetMaterialPersistenceManager(MaterialPersistencePtr const& Mngr);
	static MaterialPersistencePtr const& GetMaterialPersistenceManager();


	//! Detect material customized by user, and trigger a re-tuning if needed (called when data is loaded
	//! from the persistence manager).
	void DetectCustomizedMaterials();

	//! Enforce reloading material definitions as read from the material persistence manager.
	void ReloadCustomizedMaterials();


	//! Creates a helper to perform some requests/modifications on the tileset.
	TUniquePtr<FITwinTilesetAccess> MakeTilesetAccess();

	ITwin::ModelLink GetModelLink() const {
		return std::make_pair(EITwinModelType::IModel, IModelId);
	}

	void OnIModelOffsetChanged();

	//! Start loading the decoration attached to this model, if any.
	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void LoadDecoration();

	//! Posts a request to start saving the decoration attached to this model, if any.
	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void SaveDecoration();


	UFUNCTION()
	void OnSavedViewsRetrieved(bool bSuccess, FSavedViewInfos SavedViews);
	UFUNCTION()
	void OnSavedViewInfoAdded(bool bSuccess, FSavedViewInfo SavedViewInfo);
	UFUNCTION()
	bool AreSavedViewsLoaded() const { return bAreSavedViewsLoaded; }
	UFUNCTION()
	bool IsUpdatingSavedViews() const { return bIsUpdatingSavedViews; }


	//! Returns null if the iModel does not have extents, or if it is not known yet.
	const FProjectExtents* GetProjectExtents() const;
	//! Returns null if the iModel is not geolocated, or if it is not known yet.
	const FEcefLocation* GetEcefLocation() const;
	//! Returns a bounding box for the loaded tileset, if any.
	enum class EBBoxMethod
	{
		/* Uses the project extents defined in the model. */
		ProjectExtents,
		/* Uses the glTF meshes already created. Can be more precise than project extents, but with the
		 * disadvantage of depending on which tiles have been loaded.
		 */
		UnrealMeshes,
	};
	bool GetBoundingBox(FBox& OutBox, bool bClampOutlandishValues, EBBoxMethod Method = EBBoxMethod::ProjectExtents) const;
	//! Returns null if the tileset has not been constructed yet.
	const ACesium3DTileset* GetTileset() const;
	ACesium3DTileset* GetTileset();

	FString GetExportID() const { return ExportId; }
	void LoadModelFromInfos(FITwinExportInfo const& ExportInfo);
	//! Returns the list of IDs of the supported (ie. having Cesium format) reality data attached to the iModel.
	TFuture<TArray<FString>> GetAttachedRealityDataIds();
	void SetLightForForcedShadowUpdate(ULightComponent* SkyLight);
	TFuture<TArray<FString>> GetChildrenModelIds(const FString& ParentModelId);
	TFuture<TArray<FString>> GetSubCategoryIds(const FString& ParentCategoryId);

	UITwinClipping3DTilesetHelper* GetClippingHelper() const;
	bool MakeClippingHelper();
	std::shared_ptr<BeUtils::GltfTuner> GetGltfTuner();
	void SetNeedForcedShadowUpdate();

	bool AutoRefreshChangeset() const;
	void DisableAutoRefresh();

	UFUNCTION()
	bool HasLoadedTileset() const;

	UFUNCTION()
	bool HasTilesetLoadFailure() const;

	UFUNCTION()
	void PlayMovieSequencer();
	UFUNCTION()
	void StopOrPauseMovieSequencer();

private:
	void ResetResolvedChangesetId();
	void SetResolvedChangesetId(FString const& InChangesetId, bool bValidId = true);
	bool IsFetchingExportForAutoRefresh() const;

	/// overridden from AITwinServiceActor:
	virtual void UpdateOnSuccessfulAuthorization() override;

	/// overridden from IITwinWebServicesObserver:
	virtual void OnChangesetsRetrieved(bool bSuccess, FChangesetInfos const& ChangesetInfos) override;
	virtual void OnExportInfosRetrieved(bool bSuccess, FITwinExportInfos const& ExportInfos) override;
	virtual void OnExportInfoRetrieved(bool bSuccess, FITwinExportInfo const& ExportInfo) override;
	virtual void OnExportStarted(bool bSuccess, FString const& InExportId) override;
	virtual void OnIModelPropertiesRetrieved(bool bSuccess, bool bHasExtents, FProjectExtents const& Extents,
		bool bHasEcefLocation, FEcefLocation const& EcefLocation) override;
	virtual void OnConvertedIModelCoordsToGeoCoords(bool bSuccess,
		AdvViz::SDK::GeoCoordsReply const& GeoCoords, HttpRequestID const& RequestID) override;
	virtual void OnSavedViewGroupInfosRetrieved(bool bSuccess, FSavedViewGroupInfos const& SVGroups) override;
	virtual void OnSavedViewGroupAdded(bool bSuccess, FSavedViewGroupInfo const& GroupInfo) override;
	virtual void OnSavedViewInfosRetrieved(bool bSuccess, FSavedViewInfos const& Infos) override;
	virtual void OnSavedViewRetrieved(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo) override;
	virtual void OnSavedViewAdded(bool bSuccess, FSavedViewInfo const& SavedViewInfo) override;
	virtual void OnSavedViewDeleted(bool bSuccess, FString const& SavedViewId, FString const& Response) override;
	virtual void OnSavedViewEdited(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo) override;
	virtual void OnElementPropertiesRetrieved(bool bSuccess, FElementProperties const& ElementProps, FString const& ElementId) override;
	virtual void OnIModelPagedNodesRetrieved(bool bSuccess, FIModelPagedNodesRes const& IModelNodes) override;
	virtual void OnIModelCategoryNodesRetrieved(bool bSuccess, FIModelPagedNodesRes const& IModelNodes) override;
	virtual void OnModelFilteredNodesRetrieved(bool bSuccess, FFilteredNodesRes const& FilteredNodes, FString const& Filter) override;
	virtual void OnCategoryFilteredNodesRetrieved(bool bSuccess, FFilteredNodesRes const& IModelNodes, FString const& Filter) override;
	virtual void OnMaterialPropertiesRetrieved(bool bSuccess, AdvViz::SDK::ITwinRenderMaterialPropertiesMap const& props) override;
	virtual void OnTextureDataRetrieved(bool bSuccess, std::string const& textureId, AdvViz::SDK::ITwinTextureData const& textureData) override;
	virtual void OnIModelQueried(bool bSuccess, FString const& QueryResult, HttpRequestID const&) override;

	/// overridden from FITwinDefaultWebServicesObserver:
	virtual const TCHAR* GetObserverName() const override;

	UFUNCTION()
	void OnTilesetLoaded();

	UFUNCTION()
	void OnTilesetLoadFailure(FCesium3DTilesetLoadFailureDetails const& Details);

	void CreateDefaultTexturesComponent();

	void RunUninit();

public:
	class FImpl;
private:
	TPimplPtr<FImpl> Impl;

	UPROPERTY(Category = "iTwin|Loading",
		meta = (EditCondition = "LoadingMethod == ELoadingMethod::LM_Manual"),
		EditAnywhere)
	FString ExportId;

	//! Default textures to nullify some glTF material effects.
	UPROPERTY(Category = "iTwin",
		VisibleAnywhere)
	UITwinMaterialDefaultTexturesHolder* DefaultTexturesHolder = nullptr;

	UPROPERTY()
	FGetAllSavedViewsProgress groupsProgress;

	UPROPERTY()
	bool bAreSavedViewsLoaded = false;
	UPROPERTY()
	bool bIsUpdatingSavedViews = false;


	//! FITwinIModelImplAccess is defined in ITwinImodel.cpp, so it is only usable here.
	//! It is needed for some free functions (console commands) to access the impl.
	friend class FITwinIModelImplAccess;
	//! Allows the entire plugin to access the FITwinIModelInternals.
	//! Actually, code outside the plugin (ie. "client" code) can also call this function,
	//! but since FITwinIModelInternals is defined in the Private folder,
	//! client code cannot do anything with it (because it cannot even include its declaration header).
	friend FITwinIModelInternals& GetInternals(AITwinIModel& IModel);


	//! When true, check regularly if a new changeset is available
	UPROPERTY(Category = "iTwin|Loading",
		EditAnywhere)
	bool bAutoRefreshChangeset = true;

	class FTilesetAccess;
};
