/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSelectorWidgetImpl.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinSelectorWidgetImpl.h>
#include <Components/ComboBoxString.h>
#include <Components/CanvasPanel.h>
#include <Components/Button.h>
#include <Components/TextBlock.h>

void UITwinSelectorWidgetImpl::NativeConstruct()
{
	Super::NativeConstruct();
	ComboBox_iTwin->OnSelectionChanged.AddDynamic(this, &UITwinSelectorWidgetImpl::ITwinChanged);
	ComboBox_iModel->OnSelectionChanged.AddDynamic(this, &UITwinSelectorWidgetImpl::IModelChanged);
	ComboBox_Changeset->OnSelectionChanged.AddDynamic(this, &UITwinSelectorWidgetImpl::ChangesetChanged);
	Open->OnPressed.AddDynamic(this, &UITwinSelectorWidgetImpl::OnOpenClicked);
}

void UITwinSelectorWidgetImpl::ChangeITwinSelection()
{
	if (ComboBox_iTwin->GetSelectedIndex() >= 0)
	{
		ComboBox_iModel->ClearOptions();
		ComboBox_Changeset->ClearOptions();
		IModelValues.Empty();
		OnITwinSelected.Broadcast(ComboBox_iTwin->GetSelectedOption(), ITwinValues[ComboBox_iTwin->GetSelectedIndex()]);
	}
}

void UITwinSelectorWidgetImpl::ChangeIModelSelection()
{
	if (ComboBox_iModel->GetSelectedIndex() >= 0)
	{
		ComboBox_Changeset->ClearOptions();
		ChangesetValues.Empty();
		OnIModelSelected.Broadcast(ComboBox_iModel->GetSelectedOption(), IModelValues[ComboBox_iModel->GetSelectedIndex()]);
	}
}

void UITwinSelectorWidgetImpl::ChangeChangesetSelection()
{
	Open->SetIsEnabled(ComboBox_Changeset->GetSelectedIndex() >= 0);
	if (ComboBox_Changeset->GetSelectedIndex() >= 0)
		OnChangesetSelected.Broadcast(ComboBox_Changeset->GetSelectedOption(), ChangesetValues[ComboBox_Changeset->GetSelectedIndex()]);
}

void UITwinSelectorWidgetImpl::ITwinChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	ChangeITwinSelection();
}

void UITwinSelectorWidgetImpl::IModelChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	ChangeIModelSelection();
}

void UITwinSelectorWidgetImpl::ChangesetChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	ChangeChangesetSelection();
}

void UITwinSelectorWidgetImpl::ShowPanel(int Index)
{
	UWidget* const Panels[] = {Panel_SelectItwin, Panel_Converting, Panel_Error};
	for (auto Index2 = 0; Index2 < std::extent_v<decltype(Panels)>; ++Index2)
		Panels[Index2]->SetVisibility(Index == Index2 ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
}

void UITwinSelectorWidgetImpl::ShowErrorPanel(const FString& Error)
{
	ShowPanel(2);
	// Store the default (generic) error message defined in resources.
	static const FText DefaultError = TextError->GetText();
	if (!Error.IsEmpty())
	{
		TextError->SetText(FText::FromString(Error));
	}
	else
	{
		TextError->SetText(DefaultError);
	}
}

void UITwinSelectorWidgetImpl::AddITwin(const FString& DisplayName, const FString& Value)
{
	ITwinValues.Add(Value);
	ComboBox_iTwin->AddOption(DisplayName);
	if (ComboBox_iTwin->GetOptionCount() == 1)
		ComboBox_iTwin->SetSelectedIndex(0);
}

void UITwinSelectorWidgetImpl::AddIModel(const FString& DisplayName, const FString& Value)
{
	IModelValues.Add(Value);
	ComboBox_iModel->AddOption(DisplayName);
	if (ComboBox_iModel->GetOptionCount() == 1)
		ComboBox_iModel->SetSelectedIndex(0);
}

void UITwinSelectorWidgetImpl::AddChangeset(const FString& DisplayName, const FString& Value)
{
	ChangesetValues.Add(Value);
	ComboBox_Changeset->AddOption(DisplayName);
	if (ComboBox_Changeset->GetOptionCount() == 1)
		ComboBox_Changeset->SetSelectedIndex(0);
}

void UITwinSelectorWidgetImpl::DisableITwinPanel()
{
	Panel_SelectItwin->SetIsEnabled(false);
	Open->SetIsEnabled(false);
}

void UITwinSelectorWidgetImpl::OnOpenClicked()
{
	OnOpenPressed.Broadcast();
}

FString UITwinSelectorWidgetImpl::GetIModelDisplayName(const FString& iModelId) const
{
	ensureMsgf(IModelValues.Num() == ComboBox_iModel->GetOptionCount(), TEXT("IModel ComboBox Invariant"));
	TArray<FString>::SizeType iModelIndex;
	if (IModelValues.Find(iModelId, iModelIndex)
		&&
		ensure(iModelIndex < ComboBox_iModel->GetOptionCount()))
	{
		return ComboBox_iModel->GetOptionAtIndex(iModelIndex);
	}
	return {};
}
