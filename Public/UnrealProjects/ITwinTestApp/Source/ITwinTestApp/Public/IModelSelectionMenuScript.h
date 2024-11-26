/*--------------------------------------------------------------------------------------+
|
|     $Source: IModelSelectionMenuScript.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Engine/LevelScriptActor.h>
#include <IModelSelectionMenuScript.generated.h>

class AITwinDecorationHelper;
class AITwinIModel;
class AITwinSelector;
class ATopMenu;

//! Used as "parent class" of level "IModelSelectionMenu".
//! Contains all the logic for this level.
UCLASS()
class ITWINTESTAPP_API AIModelSelectionMenuScript: public ALevelScriptActor
{
	GENERATED_BODY()
public:
	virtual void PreInitializeComponents() override;
protected:
	void BeginPlay() override;
private:
	UPROPERTY()
	TObjectPtr<AITwinSelector> ITwinSelector;
	UPROPERTY()
	ATopMenu* TopPanel = nullptr;
	UPROPERTY()
	FString IModelId;
	UPROPERTY()
	FString ITwinId;
	UPROPERTY()
	FString ExportId;
	UPROPERTY()
	AITwinIModel* IModel = nullptr;
	UPROPERTY()
	TObjectPtr<AITwinDecorationHelper> DecoHelper;
	UFUNCTION(BlueprintCallable)
	void OnLoadIModel(FString InIModelId, FString InExportId, FString InChangesetId, FString InITwinId,
					  FString DisplayName, FString MeshUrl);
	UFUNCTION(BlueprintCallable)
	void IModelLoaded(bool bSuccess);
	void OnLeftMouseButtonPressed();
};
