/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSavedView.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#include <GameFramework/Actor.h>
#include <ITwinFwd.h>
#include <ITwinWebServices/ITwinWebServicesObserver.h>
#include <Templates/PimplPtr.h>

#include <ITwinSavedView.generated.h>

struct FSavedView;
struct FSavedViewInfo;
class UITwinWebServices;

UCLASS()
class ITWINRUNTIME_API AITwinSavedView : public AActor, public IITwinWebServicesObserver
{
	GENERATED_BODY()
public:
	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	TObjectPtr<AITwinServerConnection> ServerConnection;

	UPROPERTY(Category = "iTwin",
		EditAnywhere,
		BlueprintReadOnly)
	FString SavedViewId;

	AITwinSavedView();
	virtual void Destroyed() override;

	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void UpdateSavedView();

	//! Only saved views created with the plugin can be deleted,
	//! legacy saved views are not deletable.
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void DeleteSavedView();

	//! UpdateSavedView() needs to be called at least once before calling RenameSavedView()
	//! to set the savedView transform.
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void MoveToSavedView();

	//! UpdateSavedView() needs to be called at least once before calling RenameSavedView()
	//! to set the savedView transform.
	//! Note: Only saved views created with the plugin can be renamed, legacy saved views are not editable.
	UFUNCTION(Category = "iTwin",
		BlueprintCallable)
	void RenameSavedView();

	//! UpdateSavedView() needs to be called at least once before renaming the saved view
	//! to set its transform.
	//! Note: Only saved views created with the plugin can be renamed, legacy saved views are not editable.
	UPROPERTY(Category = "iTwin",
		EditAnywhere)
	FString DisplayName;

	#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	#endif

	//! Only saved views created with the plugin can be renamed, legacy saved views are not editable.
	UFUNCTION(Category = "iTwin",
		CallInEditor,
		BlueprintCallable)
	void RetakeSavedView();

private:
	class FImpl;
	TPimplPtr<FImpl> Impl;

	UPROPERTY()
	TObjectPtr<UITwinWebServices> WebServices;


	void UpdateWebServices();

	UFUNCTION()
	void OnTimelineTick(const float& output);

	/// overridden from IITwinWebServicesObserver:
	virtual void OnAuthorizationDone(bool bSuccess, FString const& Error) override;
	virtual void OnITwinsRetrieved(bool bSuccess, FITwinInfos const& Infos) override;
	virtual void OnIModelsRetrieved(bool bSuccess, FIModelInfos const& Infos) override;
	virtual void OnRealityDataRetrieved(bool bSuccess, FITwinRealityDataInfos const& Infos) override;
	virtual void OnChangesetsRetrieved(bool bSuccess, FChangesetInfos const& ChangesetInfos) override;
	virtual void OnExportInfosRetrieved(bool bSuccess, FITwinExportInfos const& ExportInfos) override;
	virtual void OnExportInfoRetrieved(bool bSuccess, FITwinExportInfo const& ExportInfo) override;
	virtual void OnExportStarted(bool bSuccess, FString const& ExportId) override;
	virtual void OnSavedViewInfosRetrieved(bool bSuccess, FSavedViewInfos const& Infos) override;
	virtual void OnSavedViewRetrieved(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo) override;
	virtual void OnSavedViewAdded(bool bSuccess, FSavedViewInfo const& SavedViewInfo) override;
	virtual void OnSavedViewDeleted(bool bSuccess, FString const& Response) override;
	virtual void OnSavedViewEdited(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo) override;
};
