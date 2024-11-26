/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSelector.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <GameFramework/Actor.h>
#include <ITwinSelector.generated.h>

class UITwinSelectorWidgetImpl;
class UITwinWebServices;

UCLASS()
class ITWINTESTAPP_API AITwinSelector: public AActor
{
	GENERATED_BODY()
public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_SixParams(FLoadModelEvent, FString, IModelId, FString, ExportId, FString, ChangesetId, FString, ITwinId, FString, DisplayName, FString, MeshUrl);
	UPROPERTY()
	FLoadModelEvent LoadModel;

	UFUNCTION(BlueprintCallable)
	FString GetIModelDisplayName(const FString& iModelId) const;

protected:
	void BeginPlay() override;
private:
	UPROPERTY()
	UITwinSelectorWidgetImpl* UI = nullptr;
	UPROPERTY()
	UITwinWebServices* ITwinWebService = nullptr;
	UPROPERTY()
	FString SelectedExportId;
	UPROPERTY()
	FString SelectedIModelId;
	UPROPERTY()
	FString SelectedChangesetId;
	UPROPERTY()
	FString SelectedITwinId;
	UPROPERTY()
	FString SelectedDisplayName;
	UPROPERTY()
	FString SelectedMeshUrl;
	UFUNCTION(BlueprintCallable)
	void OnAuthorizationDone(bool bSuccess, FString AuthError);
	UFUNCTION(BlueprintCallable)
	void GetITwinsCompleted(bool bSuccess, FITwinInfos ITwins);
	UFUNCTION(BlueprintCallable)
	void ITwinSelected(FString DisplayName, FString Value);
	UFUNCTION(BlueprintCallable)
	void OnIModelsComplete(bool bSuccess, FIModelInfos IModels);
	UFUNCTION(BlueprintCallable)
	void IModelSelected(FString DisplayName, FString Value);
	UFUNCTION(BlueprintCallable)
	void OnChangesetsComplete(bool bSuccess, FChangesetInfos Changesets);
	UFUNCTION(BlueprintCallable)
	void ChangesetSelected(FString DisplayName, FString Value);
	UFUNCTION(BlueprintCallable)
	void OnOpenClicked();
	UFUNCTION(BlueprintCallable)
	void OnExportsCompleted(bool bSuccess, FITwinExportInfos Exports);
	UFUNCTION(BlueprintCallable)
	void OnStartExportComplete(bool bSuccess, FString ExportId);
	UFUNCTION(BlueprintCallable)
	void GetExportInfoComplete(bool bSuccess, FITwinExportInfo Export);
	UFUNCTION(BlueprintCallable)
	void FindExport(FString& Status, const FITwinExportInfos& Exports);
	UFUNCTION(BlueprintCallable)
	void GetExportState(FString& State, const FITwinExportInfo& Export);
	void LoadIModel();
};
