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

#include <ITwinIModel.generated.h>

struct FITwinIModel3DInfo;
struct FChangesetInfos;
struct FITwinExportInfos;
struct FITwinCesium3DTilesetLoadFailureDetails;
struct FSavedViewInfos;


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


UCLASS()
class ITWINRUNTIME_API AITwinIModel : public AITwinServiceActor
{
	GENERATED_BODY()
public:
	TSharedPtr<FITwinGeolocation> Geolocation;

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

	//! Editable changeset ID.
	//! If left empty, the latest changeset will be used.
	//! See ResolvedChangesetId.
	UPROPERTY(Category = "iTwin|Loading",
		meta = (EditCondition = "LoadingMethod == ELoadingMethod::LM_Automatic"),
		EditAnywhere)
	FString ChangesetId;

	//! The resolved changeset ID, computed as follows:
	//! - If ChangesetId is not empty, then ResolvedChangesetId is same as ChangesetId.
	//! - Otherwise, ResolvedChangesetId is the latest changeset given by the iModel Hub.
	//!   Note that in this case, if the iModel does not have any changeset (only a baseline file)
	//!   then ResolvedChangesetId will be empty.
	UPROPERTY(Category = "iTwin|Loading",
		VisibleAnywhere)
	FString ResolvedChangesetId;

	//! Indicates whether ResolvedChangesetId has been computed/updated and is valid.
	UPROPERTY(Category = "iTwin|Loading",
		VisibleAnywhere)
	bool bResolvedChangesetIdValid;

	//! Current export status of the iModel.
	//! Call Export() to update this status.
	UPROPERTY(Category = "iTwin|Loading",
		VisibleAnywhere)
	EITwinExportStatus ExportStatus;

	//! WORK IN PROGRESS - UNRELEASED - Synchro4D schedules found on this iModel.
	UPROPERTY()
	UITwinSynchro4DSchedules* Synchro4DSchedules = nullptr;

	AITwinIModel();
	virtual void Destroyed() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	virtual void Tick(float DeltaTime) override;
	virtual bool ShouldTickIfViewportsOnly() const override;

	//! To be called at least once after ServerConnection, IModelId, ChangesetId have been set.
	//! This will query the mesh export service for a corresponding export, and if complete one is found,
	//! It will spawn the corresponding Cesium tileset.
	//! In any case, this will also update ExportStatus.
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void UpdateIModel();

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

	//! TEMPORARY (for tests). Triggers a re-tune of the glTF model.
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void Retune();

	UFUNCTION()
	void OnSavedViewsRetrieved(bool bSuccess, FSavedViewInfos SavedViews);

private:
	void AutoExportAndLoad();
	void TestExportCompletionAfterDelay(FString const& InExportId, float DelayInSeconds);

	void SetResolvedChangesetId(FString const& InChangesetId);

	void OnLoadingUIEvent();
	void UpdateAfterLoadingUIEvent();
	void DestroyTileset();

	/// overridden from AITwinServiceActor:
	virtual void UpdateOnSuccessfulAuthorization() override;

	/// overridden from IITwinWebServicesObserver:
	virtual void OnChangesetsRetrieved(bool bSuccess, FChangesetInfos const& ChangesetInfos) override;
	virtual void OnExportInfosRetrieved(bool bSuccess, FITwinExportInfos const& ExportInfos) override;
	virtual void OnExportInfoRetrieved(bool bSuccess, FITwinExportInfo const& ExportInfo) override;
	virtual void OnExportStarted(bool bSuccess, FString const& InExportId) override;
	virtual void OnSavedViewInfosRetrieved(bool bSuccess, FSavedViewInfos const& Infos) override;
	virtual void OnSavedViewRetrieved(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo) override;
	virtual void OnSavedViewAdded(bool bSuccess, FSavedViewInfo const& SavedViewInfo) override;
	virtual void OnSavedViewDeleted(bool bSuccess, FString const& SavedViewId, FString const& Response) override;
	virtual void OnSavedViewEdited(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo) override;

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

	//! FITwinIModelImplAccess is defined in ITwinImodel.cpp, so it is only usable here.
	//! It is needed for some free functions (console commands) to access the impl.
	friend class FITwinIModelImplAccess;
	//! Allows the entire plugin to access the FITwinIModelInternals.
	//! Actually, code outside the plugin (ie. "client" code) can also call this function,
	//! but since FITwinIModelInternals is defined in the Private folder,
	//! client code cannot do anything with it (because it cannot even include its declaration header).
	friend FITwinIModelInternals& GetInternals(AITwinIModel& IModel);
};
