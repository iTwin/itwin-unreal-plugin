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
	// for compatibility with former 3DFT plugin, we hold the 2 versions
	FITwinIModel3DInfo IModel3dInfo_iTwin, IModel3dInfo_UE;

	IModel->GetModel3DInfoInCoordSystem(IModel3dInfo_iTwin, EITwinCoordSystem::ITwin);
	TopPanel->SetIModelInfo(ITwinId, IModelId, IModel3dInfo_iTwin);

	IModel->GetModel3DInfoInCoordSystem(IModel3dInfo_UE, EITwinCoordSystem::UE);
	TopPanel->SetIModel3DInfoInCoordSystem(IModel3dInfo_UE, EITwinCoordSystem::UE);

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
