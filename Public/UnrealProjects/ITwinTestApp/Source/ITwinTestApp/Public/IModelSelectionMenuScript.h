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

class AITwinIModel;
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
	ATopMenu* TopPanel;
	UPROPERTY()
	FString IModelId;
	UPROPERTY()
	FString ITwinId;
	UPROPERTY()
	FString ExportId;
	UPROPERTY()
	AITwinIModel* IModel;
	UFUNCTION(BlueprintCallable)
	void OnLoadIModel(FString InIModelId, FString InExportId, FString InChangesetId, FString InITwinId);
	UFUNCTION(BlueprintCallable)
	void IModelLoaded(bool bSuccess);
	void OnLeftMouseButtonPressed();
};
