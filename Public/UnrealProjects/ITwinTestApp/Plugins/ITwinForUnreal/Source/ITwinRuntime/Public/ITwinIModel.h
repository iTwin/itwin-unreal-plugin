/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinIModel.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinCoordSystem.h>
#include <ITwinFwd.h>
#include <ITwinLoadInfo.h>
#include <ITwinServiceActor.h>
#include <MaterialPrediction/ITwinMaterialPredictionStatus.h>
#include <Templates/PimplPtr.h>
#include <memory>
#include <ITwinIModel.generated.h>

struct FITwinIModel3DInfo;
struct FChangesetInfos;
struct FITwinExportInfos;
struct FITwinCesium3DTilesetLoadFailureDetails;
struct FSavedViewInfos;
namespace SDK::Core
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
}
namespace ITwin
{
	//! Get a max-screenspace-error value from a percentage (value in range [0;1])
	ITWINRUNTIME_API double ToScreenSpaceError(float QualityValue);

	//! Adjust the tileset quality, given a percentage (value in range [0;1])
	ITWINRUNTIME_API void SetTilesetQuality(AITwinCesium3DTileset& Tileset, float Value);

	//! Returns the tileset quality as a percentage (value in range [0;1])
	ITWINRUNTIME_API float GetTilesetQuality(AITwinCesium3DTileset const& Tileset);

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


UCLASS()
class ITWINRUNTIME_API AITwinIModel : public AITwinServiceActor
{
	GENERATED_BODY()
public:
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
		VisibleAnywhere)
	UITwinSynchro4DSchedules* Synchro4DSchedules = nullptr;

public:
	AITwinIModel();
	~AITwinIModel();
	/// Called when placed in editor or spawned: override to force spawning by default at (0,0,0), otherwise
	/// you get a geo offset that you probably didn't want in the first place. I had tried
	/// OnConstruction(FTransform) first but, bad idea, it gets called everytime the construction script is
	/// re-run, for example after editing property fields!! (witnessed when changing schedule component's 
	/// time...)
	virtual void PostActorCreated() override;
	virtual void BeginDestroy() override;
	virtual void Destroyed() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void PostLoad() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

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

	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void UpdateSavedViews();

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void ShowConstructionData(bool bShow, ULightComponent* SunLight = nullptr);

	UPROPERTY(Category = "iTwin", EditAnywhere)
	bool bShowConstructionData = true;

	//! Deselect any element previously selected. This will disable the selection highlight, if any.
	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void DeSelectElements();

	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void AddSavedView(const FString& displayName);

	UFUNCTION(Category = "iTwin|Load",
		BlueprintCallable)
	void Reset();

	UFUNCTION(Category = "iTwin|Load",
		BlueprintCallable)
	void RefreshTileset();

	//! TEMPORARY (for tests). Triggers a re-tune of the glTF model.
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void Retune();


	//! Returns the map of ITwin material info - the key being the iTwin Material ID, and the value, the
	//! display name of the material.
	TMap<uint64, FString> GetITwinMaterialMap() const;
	FString GetMaterialName(uint64_t MaterialId) const;

	//! Minimal API for material tuning in Carrot MVP
	double GetMaterialChannelIntensity(uint64_t MaterialId, SDK::Core::EChannelType Channel) const;
	void SetMaterialChannelIntensity(uint64_t MaterialId, SDK::Core::EChannelType Channel, double Intensity);

	FLinearColor GetMaterialChannelColor(uint64_t MaterialId, SDK::Core::EChannelType Channel) const;
	void SetMaterialChannelColor(uint64_t MaterialId, SDK::Core::EChannelType Channel, FLinearColor const& Color);

	FString GetMaterialChannelTextureID(uint64_t MaterialId, SDK::Core::EChannelType Channel, SDK::Core::ETextureSource& OutSource) const;
	void SetMaterialChannelTextureID(uint64_t MaterialId, SDK::Core::EChannelType Channel,
		FString const& TextureId, SDK::Core::ETextureSource eSource);

	SDK::Core::ITwinUVTransform GetMaterialUVTransform(uint64_t MaterialId) const;
	void SetMaterialUVTransform(uint64_t MaterialId, SDK::Core::ITwinUVTransform const& UVTransform);

	SDK::Core::EMaterialKind GetMaterialKind(uint64_t MaterialId) const;
	void SetMaterialKind(uint64_t MaterialId, SDK::Core::EMaterialKind NewKind);

	//! Rename a material.
	bool SetMaterialName(uint64_t MaterialId, FString const& NewName);

	//! Load a material from an asset file (expecting an asset of class #UITwinMaterialDataAsset).
	bool LoadMaterialFromAssetFile(uint64_t MaterialId, FString const& AssetFilePath);

	//! Return a texture that nullifies the given channel (and thus can be used as default texture for it).
	UTexture2D* GetDefaultTextureForChannel(SDK::Core::EChannelType Channel) const;

	using GltfMaterialHelperPtr = std::shared_ptr<BeUtils::GltfMaterialHelper>;
	std::shared_ptr<BeUtils::GltfMaterialHelper> const& GetGltfMaterialHelper() const;

	using MaterialPersistencePtr = std::shared_ptr<SDK::Core::MaterialPersistenceManager>;
	static void SetMaterialPersistenceManager(MaterialPersistencePtr const& Mngr);
	static MaterialPersistencePtr const& GetMaterialPersistenceManager();


	//! Detect material customized by user, and trigger a re-tuning if needed (called when data is loaded
	//! from the persistence manager).
	void DetectCustomizedMaterials();

	//! Enforce reloading material definitions as read from the material persistence manager.
	void ReloadCustomizedMaterials();

	//! Initiate the Machine Learning service for material predictions.
	UFUNCTION(Category = "iTwin|Materials",
		BlueprintCallable)
	void LoadMaterialMLPrediction();

	//! Toggle the ML-based material prediction mode on or off.
	UFUNCTION(Category = "iTwin|Materials",
		BlueprintCallable)
	void ToggleMLMaterialPrediction(bool bActivate);

	UFUNCTION(Category = "iTwin|Materials",
		BlueprintCallable)
	bool IsMaterialMLPredictionActivated() const {
		return bActivateMLMaterialPrediction;
	}

	UFUNCTION(Category = "iTwin|Materials",
		BlueprintCallable)
	EITwinMaterialPredictionStatus GetMaterialMLPredictionStatus() const {
		return MLMaterialPredictionStatus;
	}

	UFUNCTION(Category = "iTwin|Materials",
		BlueprintCallable)
	bool VisualizeMaterialMLPrediction() const {
		return bActivateMLMaterialPrediction && MLMaterialPredictionStatus == EITwinMaterialPredictionStatus::Complete;
	}
	void SetMaterialMLPredictionObserver(IITwinWebServicesObserver* observer);
	IITwinWebServicesObserver* GetMaterialMLPredictionObserver() const;

	/// Reset iModel manual "offset" (= scene placement customization), and persist it to Cloud
	void SetModelOffset(FVector Pos, FVector Rot);
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
	void OnSceneLoaded(bool success);


	//! Returns null if the iModel does not have extents, or if it is not known yet.
	const FProjectExtents* GetProjectExtents() const;
	//! Returns null if the iModel is not geolocated, or if it is not known yet.
	const FEcefLocation* GetEcefLocation() const;
	//! Returns null if the tileset has not been constructed yet.
	const AITwinCesium3DTileset* GetTileset() const;

	void HideTileset(bool bHide);
	bool IsTilesetHidden();
	void SetMaximumScreenSpaceError(double InMaximumScreenSpaceError);
	//Helper of SetMaximumScreenSpaceError :  Adjust the tileset quality, given a percentage (value in range [0;1])
	void SetTilesetQuality(float Value);
	float GetTilesetQuality() const;
	FString GetExportID() const { return ExportId; }
	void LoadModelFromInfos(FITwinExportInfo const& ExportInfo);
	//! Returns the list of IDs of the supported (ie. having Cesium format) reality data attached to the iModel.
	TFuture<TArray<FString>> GetAttachedRealityDataIds();

private:
	void SetResolvedChangesetId(FString const& InChangesetId);

	/// overridden from AITwinServiceActor:
	virtual void UpdateOnSuccessfulAuthorization() override;

	/// overridden from IITwinWebServicesObserver:
	virtual void OnChangesetsRetrieved(bool bSuccess, FChangesetInfos const& ChangesetInfos) override;
	virtual void OnExportInfosRetrieved(bool bSuccess, FITwinExportInfos const& ExportInfos) override;
	virtual void OnExportInfoRetrieved(bool bSuccess, FITwinExportInfo const& ExportInfo) override;
	virtual void OnExportStarted(bool bSuccess, FString const& InExportId) override;
	virtual void OnIModelPropertiesRetrieved(bool bSuccess, bool bHasExtents, FProjectExtents const& Extents,
		bool bHasEcefLocation, FEcefLocation const& EcefLocation) override;
	virtual void OnSavedViewInfosRetrieved(bool bSuccess, FSavedViewInfos const& Infos) override;
	virtual void OnSavedViewRetrieved(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo) override;
	virtual void OnSavedViewAdded(bool bSuccess, FSavedViewInfo const& SavedViewInfo) override;
	virtual void OnSavedViewDeleted(bool bSuccess, FString const& SavedViewId, FString const& Response) override;
	virtual void OnSavedViewEdited(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo) override;
	virtual void OnElementPropertiesRetrieved(bool bSuccess, FElementProperties const& ElementProps, FString const& ElementId) override;
	virtual void OnMaterialPropertiesRetrieved(bool bSuccess, SDK::Core::ITwinMaterialPropertiesMap const& props) override;
	virtual void OnTextureDataRetrieved(bool bSuccess, std::string const& textureId, SDK::Core::ITwinTextureData const& textureData) override;
	virtual void OnIModelQueried(bool bSuccess, FString const& QueryResult, HttpRequestID const&) override;
	virtual void OnMatMLPredictionRetrieved(bool bSuccess, SDK::Core::ITwinMaterialPrediction const& prediction) override;
	virtual void OnMatMLPredictionProgress(float fProgressRatio) override;

	/// overridden from FITwinDefaultWebServicesObserver:
	virtual const TCHAR* GetObserverName() const override;

	UFUNCTION()
	void OnTilesetLoaded();

	UFUNCTION()
	void OnTilesetLoadFailure(FITwinCesium3DTilesetLoadFailureDetails const& Details);


public:
	class FImpl;
private:
	TPimplPtr<FImpl> Impl;

	UPROPERTY(Category = "iTwin|Loading",
		meta = (EditCondition = "LoadingMethod == ELoadingMethod::LM_Manual"),
		EditAnywhere)
	FString ExportId;


	//! Default textures to nullify some material effects
	UPROPERTY()
	UTexture2D* NoColorTexture = nullptr;
	UPROPERTY()
	UTexture2D* NoNormalTexture = nullptr;
	UPROPERTY()
	UTexture2D* NoMetallicRoughnessTexture = nullptr;

	//! ML - Material Prediction
	UPROPERTY()
	TObjectPtr<UITwinWebServices> MaterialMLPredictionWS;

	UPROPERTY()
	bool bEnableMLMaterialPrediction = false;

	//! Activate material prediction based on machine learning API.
	UPROPERTY(Category = "iTwin|Materials",
		EditAnywhere,
		Meta = (EditCondition = "bEnableMLMaterialPrediction", EditConditionHides))
	bool bActivateMLMaterialPrediction = false;

	//! Current status of ML-based material prediction for the iModel.
	UPROPERTY(Category = "iTwin|Materials",
		VisibleAnywhere,
		Meta = (EditCondition = "bEnableMLMaterialPrediction", EditConditionHides))
	EITwinMaterialPredictionStatus MLMaterialPredictionStatus = EITwinMaterialPredictionStatus::Unknown;

	//! Persistence manager for material settings.
	static MaterialPersistencePtr MaterialPersistenceMngr;

	//! FITwinIModelImplAccess is defined in ITwinImodel.cpp, so it is only usable here.
	//! It is needed for some free functions (console commands) to access the impl.
	friend class FITwinIModelImplAccess;
	//! Allows the entire plugin to access the FITwinIModelInternals.
	//! Actually, code outside the plugin (ie. "client" code) can also call this function,
	//! but since FITwinIModelInternals is defined in the Private folder,
	//! client code cannot do anything with it (because it cannot even include its declaration header).
	friend FITwinIModelInternals& GetInternals(AITwinIModel& IModel);
};
