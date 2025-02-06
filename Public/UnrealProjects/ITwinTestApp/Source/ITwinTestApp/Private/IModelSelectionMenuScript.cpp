/*--------------------------------------------------------------------------------------+
|
|     $Source: IModelSelectionMenuScript.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <IModelSelectionMenuScript.h>
#include <ITwinSelector.h>
#include <TopMenu.h>
#include <ITwinIModel.h>
#include <ITwinWebServices/ITwinWebServices_Info.h>
#include <Helpers.h>
#include <Decoration/ITwinDecorationHelper.h>
#include <Decoration/ITwinDecorationServiceSettings.h>

#include <Components/InputComponent.h>
#include <Engine/World.h>


void AIModelSelectionMenuScript::PreInitializeComponents()
{
	Super::PreInitializeComponents();
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this,
							&AIModelSelectionMenuScript::OnLeftMouseButtonPressed);
}

void AIModelSelectionMenuScript::BeginPlay()
{
	Super::BeginPlay();
	ITwinSelector = GetWorld()->SpawnActor<AITwinSelector>();
	ITwinSelector->LoadModel.AddDynamic(this, &AIModelSelectionMenuScript::OnLoadIModel);
}

void AIModelSelectionMenuScript::OnLoadIModel(FString InIModelId, FString InExportId, FString InChangesetId,
	FString InITwinId, FString DisplayName, FString MeshUrl)
{
	IModelId = InIModelId;
	ExportId = InExportId;
	ITwinId = InITwinId;

	FITwinLoadInfo Info;
	Info.ITwinId = InITwinId;
	Info.IModelId = InIModelId;
	Info.ChangesetId = InChangesetId;
	Info.ExportId = InExportId;
#if WITH_EDITOR
	// use the display name of the iModel preferably
	if (ensure(ITwinSelector))
	{
		Info.IModelDisplayName = ITwinSelector->GetIModelDisplayName(IModelId);
		ensureMsgf(!Info.IModelDisplayName.IsEmpty(), TEXT("Display Name should be retrievable from UI"));
	}
#endif
	TopPanel = GetWorld()->SpawnActor<ATopMenu>();
	IModel = GetWorld()->SpawnActor<AITwinIModel>();
	IModel->SetModelLoadInfo(Info);

	UITwinDecorationServiceSettings const* DecoSettings = GetDefault<UITwinDecorationServiceSettings>();
	if (ensure(DecoSettings) && DecoSettings->bLoadDecorationsInPlugin)
	{
		DecoHelper = GetWorld()->SpawnActor<AITwinDecorationHelper>();
		DecoHelper->SetLoadedITwinInfo(Info);
		DecoHelper->LoadDecoration();
	}

	IModel->OnIModelLoaded.AddDynamic(this, &AIModelSelectionMenuScript::IModelLoaded);
	IModel->LoadModelFromInfos(FITwinExportInfo{ InExportId, DisplayName, TEXT("Complete"), IModelId,
												 ITwinId, InChangesetId, MeshUrl });
}

void AIModelSelectionMenuScript::IModelLoaded(bool bSuccess, FString InIModelId)
{
	ensure(IModelId == InIModelId);

	FITwinIModel3DInfo Tmp;
	// for compatibility with former 3DFT plugin, we hold the 2 versions
	IModel->GetModel3DInfoInCoordSystem(Tmp, EITwinCoordSystem::ITwin);
	TopPanel->SetIModelInfo(ITwinId, IModelId, Tmp);

	IModel->GetModel3DInfoInCoordSystem(Tmp, EITwinCoordSystem::UE);
	TopPanel->SetIModel3DInfoInCoordSystem(Tmp, EITwinCoordSystem::UE);

	TopPanel->GetAllSavedViews();
	IModel->ZoomOnIModel();
	IModel->AdjustPawnSpeedToExtents();
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
