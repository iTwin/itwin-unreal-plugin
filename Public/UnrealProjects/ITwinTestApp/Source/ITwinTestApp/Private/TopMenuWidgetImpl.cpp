/*--------------------------------------------------------------------------------------+
|
|     $Source: TopMenuWidgetImpl.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <TopMenuWidgetImpl.h>
#include <Components/Image.h>
#include <Components/ComboBoxString.h>
#include <Components/Button.h>
#include <Components/TextBlock.h>

void UTopMenuWidgetImpl::NativeConstruct()
{
	Super::NativeConstruct();
	ZoomiModel->OnMouseButtonDownEvent.BindDynamic(this, &UTopMenuWidgetImpl::OnZoomIModelMouseButtonDown);
	ComboBox_SavedViews->OnSelectionChanged.AddDynamic(this, &UTopMenuWidgetImpl::SavedViewsChanged);
	Prev->OnPressed.AddDynamic(this, &UTopMenuWidgetImpl::OnPrevClicked);
	Next->OnPressed.AddDynamic(this, &UTopMenuWidgetImpl::OnNextClicked);
}

FEventReply UTopMenuWidgetImpl::OnZoomIModelMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent)
{
	OnZoomPressed.Broadcast();
	return {};
}

void UTopMenuWidgetImpl::OnPrevClicked()
{
	GotoNextSavedView(-1);
}

void UTopMenuWidgetImpl::OnNextClicked()
{
	GotoNextSavedView(1);
}

void UTopMenuWidgetImpl::SavedViewsChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	ChangeSavedViews();
}

void UTopMenuWidgetImpl::ChangeSavedViews()
{
	if (ComboBox_SavedViews->GetSelectedIndex() >= 0)
		OnSavedViewSelected.Broadcast(ComboBox_SavedViews->GetSelectedOption(), SavedViewsValues[ComboBox_SavedViews->GetSelectedIndex()]);
}

void UTopMenuWidgetImpl::GotoNextSavedView(int Increment)
{
	if (ComboBox_SavedViews->GetOptionCount() >= 1)
		ComboBox_SavedViews->SetSelectedIndex((Increment+ComboBox_SavedViews->GetSelectedIndex()+ComboBox_SavedViews->GetOptionCount())%
			ComboBox_SavedViews->GetOptionCount());
}

void UTopMenuWidgetImpl::UpdateElementId(bool bVisible, const FString& InElementId)
{
	if (bVisible)
	{
		ElementId->SetText(FText::FromString(InElementId));
		ElementId->SetVisibility(ESlateVisibility::Visible);
		icon_element->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		ElementId->SetVisibility(ESlateVisibility::Hidden);
		icon_element->SetVisibility(ESlateVisibility::Hidden);
	}
}

void UTopMenuWidgetImpl::AddSavedView(const FString& DisplayName, const FString& Value)
{
	SavedViewsValues.Add(Value);
	ComboBox_SavedViews->AddOption(DisplayName);
}

void UTopMenuWidgetImpl::RemoveSavedView(const FString& SavedViewId)
{
	ensureMsgf(SavedViewsValues.Num() == ComboBox_SavedViews->GetOptionCount(), TEXT("SavedView UI Invariant"));

	TArray<FString>::SizeType SavedViewIndex;
	if (SavedViewsValues.Find(SavedViewId, SavedViewIndex))
	{
		SavedViewsValues.RemoveAt(SavedViewIndex);
		if (ensure(SavedViewIndex < ComboBox_SavedViews->GetOptionCount()))
		{
			ComboBox_SavedViews->RemoveOption(ComboBox_SavedViews->GetOptionAtIndex(SavedViewIndex));
		}
	}
}
