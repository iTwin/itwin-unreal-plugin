/*--------------------------------------------------------------------------------------+
|
|     $Source: IModelSelectionMenuScript.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <IModelSelectionMenuScript.h>
#include <ITwinSelector.h>
#include <TopMenu.h>
#include <ITwinIModel.h>
#include <Helpers.h>

void AIModelSelectionMenuScript::PreInitializeComponents()
{
	Super::PreInitializeComponents();
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AIModelSelectionMenuScript::OnLeftMouseButtonPressed);
}

void AIModelSelectionMenuScript::BeginPlay()
{
	Super::BeginPlay();
	auto* const ITwinSelector = GetWorld()->SpawnActor<AITwinSelector>();
	ITwinSelector->LoadModel.AddDynamic(this, &AIModelSelectionMenuScript::OnLoadIModel);
}

void AIModelSelectionMenuScript::OnLoadIModel(FString InIModelId, FString InExportId, FString InChangesetId, FString InITwinId)
{
	IModelId = InIModelId;
	ExportId = InExportId;
	ITwinId = InITwinId;
	TopPanel = GetWorld()->SpawnActor<ATopMenu>();
	IModel = GetWorld()->SpawnActor<AITwinIModel>();
	IModel->OnIModelLoaded.AddDynamic(this, &AIModelSelectionMenuScript::IModelLoaded);
	IModel->LoadModel(ExportId);
}

void AIModelSelectionMenuScript::IModelLoaded(bool bSuccess)
{
	FITwinIModel3DInfo IModel3dInfo;
	IModel->GetModel3DInfo(IModel3dInfo);
	TopPanel->SetIModelInfo(ITwinId, IModelId, IModel3dInfo);
	TopPanel->GetAllSavedViews();
	TopPanel->ZoomOnIModel();
}

void AIModelSelectionMenuScript::OnLeftMouseButtonPressed()
{
	if (TopPanel)
	{
		bool bValid;
		FString ElementId;
		UHelpers::PickMouseElements(this, bValid, ElementId);
		TopPanel->UpdateElementId(bValid, ElementId);
	}
}
