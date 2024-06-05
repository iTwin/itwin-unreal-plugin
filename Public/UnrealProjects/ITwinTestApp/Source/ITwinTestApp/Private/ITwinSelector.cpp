/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSelector.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinSelector.h>
#include <ITwinSelectorWidgetImpl.h>
#include <iTwinWebServices/iTwinWebServices.h>
#include <Components/TextBlock.h>

void AITwinSelector::BeginPlay()
{
	Super::BeginPlay();
	// Create UI
	UI = CreateWidget<UITwinSelectorWidgetImpl>(GetWorld()->GetFirstPlayerController(), LoadClass<UITwinSelectorWidgetImpl>(nullptr,
		L"/Script/UMGEditor.WidgetBlueprint'/Game/UX/ITwinSelectorWidget.ITwinSelectorWidget_C'"));
	UI->AddToViewport();
	ITwinWebService = NewObject<UITwinWebServices>(this);
	// Check authorization
	ITwinWebService->OnAuthorizationChecked.AddDynamic(this, &AITwinSelector::AuthError);
	ITwinWebService->CheckAuthorization();
}

void AITwinSelector::AuthError(bool bSuccess, FString Error)
{
	if (!bSuccess)
	{
		UI->ShowPanel(2);
		UI->TextError->SetText(FText::FromString(Error));
		return;
	}
	// iTwin combobox
	ITwinWebService->OnGetiTwinsComplete.AddDynamic(this, &AITwinSelector::GetITwinsCompleted);
	UI->OnITwinSelected.AddDynamic(this, &AITwinSelector::ITwinSelected);
	// iModel combobox
	ITwinWebService->OnGetiTwiniModelsComplete.AddDynamic(this, &AITwinSelector::OnIModelsComplete);
	UI->OnIModelSelected.AddDynamic(this, &AITwinSelector::IModelSelected);
	// Changeset combobox
	ITwinWebService->OnGetiModelChangesetsComplete.AddDynamic(this, &AITwinSelector::OnChangesetsComplete);
	UI->OnChangesetSelected.AddDynamic(this, &AITwinSelector::ChangesetSelected);
	// Open button
	UI->OnOpenPressed.AddDynamic(this, &AITwinSelector::OnOpenClicked);
	// Check exports
	ITwinWebService->OnGetExportsComplete.AddDynamic(this, &AITwinSelector::OnExportsCompleted);
	// Check export
	ITwinWebService->OnStartExportComplete.AddDynamic(this, &AITwinSelector::OnStartExportComplete);
	ITwinWebService->OnGetExportInfoComplete.AddDynamic(this, &AITwinSelector::GetExportInfoComplete);
	// GetiTwins
	ITwinWebService->GetiTwins();
}

void AITwinSelector::GetITwinsCompleted(bool bSuccess, FITwinInfos ITwins)
{
	for (const auto& ITwin: ITwins.iTwins)
		UI->AddITwin(ITwin.DisplayName, ITwin.Id);
}

void AITwinSelector::ITwinSelected(FString DisplayName, FString Value)
{
	SelectedITwinId = Value;
	ITwinWebService->GetiTwiniModels(Value);
}

void AITwinSelector::OnIModelsComplete(bool bSuccess, FIModelInfos IModels)
{
	for (const auto& IModel: IModels.iModels)
		UI->AddIModel(IModel.DisplayName, IModel.Id);
}

void AITwinSelector::IModelSelected(FString DisplayName, FString Value)
{
	SelectedIModelId = Value;
	ITwinWebService->GetiModelChangesets(Value);
}

void AITwinSelector::OnChangesetsComplete(bool bSuccess, FChangesetInfos Changesets)
{
	for (const auto& Changeset: Changesets.Changesets)
		UI->AddChangeset("#"+Changeset.DisplayName+" "+Changeset.Description, Changeset.Id);

	if (bSuccess && Changesets.Changesets.IsEmpty())
	{
		// case of an iModel with no changeset at all: enable the Open button as well
		UI->AddChangeset(TEXT("#0 Initial"), TEXT(""));
	}
}

void AITwinSelector::ChangesetSelected(FString DisplayName, FString Value)
{
	SelectedChangesetId = Value;
}

void AITwinSelector::OnOpenClicked()
{
	UI->DisableITwinPanel();
	ITwinWebService->GetExports(SelectedIModelId, SelectedChangesetId);
}

void AITwinSelector::OnExportsCompleted(bool bSuccess, FITwinExportInfos Exports)
{
	FString Status;
	FindExport(Status, Exports);
	if (Status == "Complete")
	{
		LoadIModel();
		return;
	}
	if (Status == "Processing")
	{
		UI->ShowPanel(1);
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(TimerHandle, FTimerDelegate::CreateLambda([this, _ = TStrongObjectPtr<AITwinSelector>(this)]
			{
				ITwinWebService->GetExports(SelectedIModelId, SelectedChangesetId);
			}), 5, false);
		return;
	}
	UI->ShowPanel(1);
	ITwinWebService->StartExport(SelectedIModelId, SelectedChangesetId);
}

void AITwinSelector::OnStartExportComplete(bool bSuccess, FString ExportId)
{
	SelectedExportId = ExportId;
	ITwinWebService->GetExportInfo(ExportId);
}

void AITwinSelector::GetExportInfoComplete(bool bSuccess, FITwinExportInfo Export)
{
	FString State;
	GetExportState(State, Export);
	if (State == "Complete")
	{
		LoadIModel();
		return;
	}
	if (State == "Invalid")
	{
		UI->ShowPanel(2);
		return;
	}
	FTimerHandle TimerHandle;
	GetWorldTimerManager().SetTimer(TimerHandle, FTimerDelegate::CreateLambda([this, _ = TStrongObjectPtr<AITwinSelector>(this)]
		{
			ITwinWebService->GetExportInfo(SelectedExportId);
		}), 5, false);
}

void AITwinSelector::FindExport(FString& Status, const FITwinExportInfos& Exports)
{
	FString LastStatus;
	for (const auto& ExportInfo: Exports.ExportInfos)
	{
		if (ExportInfo.Status != "Invalid")
		{
			SelectedExportId = ExportInfo.Id;
			LastStatus = ExportInfo.Status;
			break;
		}
	}
	if (LastStatus == "Complete")
	{
		Status = "Complete";
		return;
	}
	if (!LastStatus.IsEmpty())
	{
		Status = "Processing";
		return;
	}
	Status = "Invalid";
}

void AITwinSelector::LoadIModel()
{
	LoadModel.Broadcast(SelectedIModelId, SelectedExportId, SelectedChangesetId, SelectedITwinId/*, ServerConnection*/);
	UI->SetVisibility(ESlateVisibility::Hidden);
}

void AITwinSelector::GetExportState(FString& State, const FITwinExportInfo& Export)
{
	if (Export.Status == "Complete")
	{
		SelectedExportId = Export.Id;
		State = "Complete";
		return;
	}
	if (Export.Status == "Invalid")
	{
		State = "Invalid";
		return;
	}
	State = "Processing";
}
