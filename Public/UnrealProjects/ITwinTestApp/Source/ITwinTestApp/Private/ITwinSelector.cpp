/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSelector.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinSelector.h>
#include <ITwinSelectorWidgetImpl.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <Components/TextBlock.h>
#include <TimerManager.h>
#include <UObject/StrongObjectPtr.h>
#include <Decoration/ITwinDecorationServiceSettings.h>

#define ITWIN_AUTH_ENFORCE_DISABLE_EXTERNAL_BROWSER() 0

void AITwinSelector::BeginPlay()
{
	Super::BeginPlay();
	// Create UI
	UI = CreateWidget<UITwinSelectorWidgetImpl>(GetWorld()->GetFirstPlayerController(), LoadClass<UITwinSelectorWidgetImpl>(nullptr,
		TEXT("/Script/UMGEditor.WidgetBlueprint'/Game/UX/ITwinSelectorWidget.ITwinSelectorWidget_C'")));
	UI->AddToViewport();
	ITwinWebService = NewObject<UITwinWebServices>(this);
	// Check authorization
	ITwinWebService->OnAuthorizationChecked.AddDynamic(this, &AITwinSelector::OnAuthorizationDone);

#if !ITWIN_AUTH_ENFORCE_DISABLE_EXTERNAL_BROWSER()
	// Depending on the plugin settings (boolean bUseExternalBrowserForAuthorization in
	// UITwinDecorationServiceSettings), the call below will either open an external browser or just fill
	// an URL which can then be processed by the application though another method (web widget...)
	ITwinWebService->CheckAuthorization();

	const FString AuthURL = ITwinWebService->GetAuthorizationURL();
	// If no external browser is used for the authorization, it's the responsibility of the application to
	// process the authorization URL:
	if (!ITwinWebService->UseExternalBrowser() && !AuthURL.IsEmpty())
	{
		// Here, we could create/fill a login widget in Unreal...
		// ...
		// ...
		// ...
	}

#else // ITWIN_AUTH_ENFORCE_DISABLE_EXTERNAL_BROWSER
	// Simplified code to enforce processing the authorization without any external browser:
	const FString AuthURL = ITwinWebService->InitiateAuthorizationURL();
	if (!AuthURL.IsEmpty())
	{
		// Here, we could create/fill a login widget in Unreal...
		// ...
		// ...
		// ...
	}
#endif // ITWIN_AUTH_ENFORCE_DISABLE_EXTERNAL_BROWSER
}

FString AITwinSelector::GetIModelDisplayName(const FString& iModelId) const
{
	// retrieve it from the UI
	FString DisplayName;
	if (ensure(UI))
	{
		DisplayName = UI->GetIModelDisplayName(iModelId);
	}
	return DisplayName;
}

void AITwinSelector::OnAuthorizationDone(bool bSuccess, FString AuthError)
{
	if (!bSuccess)
	{
		UI->ShowErrorPanel(AuthError);
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
	if (Status == TEXT("Complete"))
	{
		LoadIModel();
		return;
	}
	if (Status == TEXT("Processing"))
	{
		UI->ShowPanel(1);
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(TimerHandle, FTimerDelegate::CreateLambda(
			[this, _ = TStrongObjectPtr<AITwinSelector>(this)]
			{
				ITwinWebService->GetExports(SelectedIModelId, SelectedChangesetId);
			}), 5, false);
		return;
	}
	if (!bSuccess)
	{
		UI->ShowErrorPanel(FString::Printf(
			TEXT("Error listing available Exports for:\niTwin: %s\niModel: %s\nchangeset: %s"),
			*SelectedITwinId, *SelectedIModelId, *SelectedChangesetId));
	}
	else
	{
		UI->ShowPanel(1);
		ITwinWebService->StartExport(SelectedIModelId, SelectedChangesetId);
	}
}

void AITwinSelector::OnStartExportComplete(bool bSuccess, FString ExportId)
{
	if (bSuccess)
	{
		SelectedExportId = ExportId;
		ITwinWebService->GetExportInfo(ExportId);
	}
	else
	{
		UI->ShowErrorPanel(TEXT("Unable to process the tileset for first visualization.\nThe service may be temporarily unavailable. Please try again later."));
	}
}

void AITwinSelector::GetExportInfoComplete(bool /*bSuccess*/, FITwinExportInfo Export)
{
	FString State;
	GetExportState(State, Export);
	if (State == TEXT("Complete"))
	{
		LoadIModel();
		return;
	}
	if (State == TEXT("Invalid")) // bSuccess is probably false, or maybe we passed an outdated exportId
	{
		UI->ShowErrorPanel({});
		return;
	}
	FTimerHandle TimerHandle;
	GetWorldTimerManager().SetTimer(TimerHandle, FTimerDelegate::CreateLambda(
		[this, _ = TStrongObjectPtr<AITwinSelector>(this)]
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
			SelectedDisplayName = ExportInfo.DisplayName;
			SelectedMeshUrl = ExportInfo.MeshUrl;
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
	LoadModel.Broadcast(SelectedIModelId, SelectedExportId, SelectedChangesetId, SelectedITwinId,
						SelectedDisplayName, SelectedMeshUrl);
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
