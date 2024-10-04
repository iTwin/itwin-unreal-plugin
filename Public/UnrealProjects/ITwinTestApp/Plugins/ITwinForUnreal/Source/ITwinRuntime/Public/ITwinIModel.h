/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinIModel.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinCoordSystem.h>
#include <ITwinServiceActor.h>
#include <Templates/PimplPtr.h>
#include <ITwinFwd.h>
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
	struct ITwinMaterial;
	class MaterialPersistenceManager;
}

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnIModelLoaded, bool, bSuccess);

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
struct ITWINRUNTIME_API FITwinCustomMaterial
{
	GENERATED_BODY()

	UPROPERTY(Category = "iTwin",
		VisibleAnywhere)
	FString Name;

	//! TEMPORARY MODE - REPLACES THE WHOLE BASE MATERIAL
	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	TObjectPtr<UMaterialInterface> Material;

	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	bool bAdvancedConversion = false;
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

	//! WORK IN PROGRESS - UNRELEASED - Synchro4D schedules found on this iModel.
	UPROPERTY(Category = "iTwin",
		VisibleAnywhere)
	UITwinSynchro4DSchedules* Synchro4DSchedules = nullptr;

public:
	AITwinIModel();
	virtual void Destroyed() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void PostLoad() override;

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
	void GetModel3DInfoInCoordSystem(FITwinIModel3DInfo& OutInfo, EITwinCoordSystem CoordSystem,
		bool bGetLegacy3DFTValue = false) const;

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

	using MaterialPersistencePtr = std::shared_ptr<SDK::Core::MaterialPersistenceManager>;
	static void SetMaterialPersistenceManager(MaterialPersistencePtr const& Mngr);


	UFUNCTION()
	void OnSavedViewsRetrieved(bool bSuccess, FSavedViewInfos SavedViews);
	UFUNCTION()
	void OnSavedViewInfoAdded(bool bSuccess, FSavedViewInfo SavedViewInfo);

	//! Returns null if the iModel does not have extents, or if it is not known yet.
	const FProjectExtents* GetProjectExtents() const;
	//! Returns null if the iModel is not geolocated, or if it is not known yet.
	const FEcefLocation* GetEcefLocation() const;
	//! Returns null if the tileset has not been constructed yet.
	const AITwinCesium3DTileset* GetTileset() const;

	void HideTileset(bool bHide);

	FString GetExportID() const { return ExportId; }

private:
	void AutoExportAndLoad();
	void TestExportCompletionAfterDelay(FString const& InExportId, float DelayInSeconds);

	void SetResolvedChangesetId(FString const& InChangesetId);

	void OnLoadingUIEvent();
	void UpdateAfterLoadingUIEvent();
	void DestroyTileset();

	//! Fills the map of known iTwin materials, if it was read from the tileset.
	void FillMaterialInfoFromTuner();
	//! Retune the tileset if needed, to ensure that all materials customized by the user (or about to be...)
	//! can be applied to individual meshes.
	void SplitGltfModelForCustomMaterials(bool bForceRetune = false);

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
	virtual void OnElementPropertiesRetrieved(bool bSuccess, FElementProperties const& ElementProps) override;
	virtual void OnMaterialPropertiesRetrieved(bool bSuccess, SDK::Core::ITwinMaterialPropertiesMap const& props) override;
	virtual void OnTextureDataRetrieved(bool bSuccess, std::string const& textureId, SDK::Core::ITwinTextureData const& textureData) override;
	virtual void OnIModelQueried(bool bSuccess, FString const& QueryResult) override;

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

	//! WORK IN PROGRESS - UNRELEASED - material customization.
	UPROPERTY(Category = "iTwin",
		EditAnywhere,
		Meta = (EditCondition = "bCanReplaceMaterials", EditConditionHides))
	TMap<uint64, FITwinCustomMaterial> CustomMaterials;

	UPROPERTY()
	bool bCanReplaceMaterials = false;

	//! Persistence manager for material settings (temporary mode).
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
