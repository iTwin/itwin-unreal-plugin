/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwin2DCalloutWidgetImpl.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Callouts/ITwin2DCalloutWidgetImpl.h>
#include <Callouts/ITwinLineWidget.h>

#include <Blueprint/WidgetBlueprintLibrary.h>
#include <Components/Border.h>
#include <Components/Button.h>
#include <Components/CanvasPanel.h>
#include "Components/CanvasPanelSlot.h"
#include <Components/Image.h>
#include <Components/Textblock.h>
#include <Components/Widget.h>
#include <Kismet/KismetMathLibrary.h>



void UITwin2DCalloutWidgetImpl::NativeConstruct()
{
	Super::NativeConstruct();

	if (PinButton && !PinButton->OnPressed.IsBound())
	{
		PinButton->OnPressed.AddDynamic(this, &UITwin2DCalloutWidgetImpl::OnPinButtonPressed);
	}
}

void UITwin2DCalloutWidgetImpl::SetPinPosition(FVector2D pos)
{
	pinPosition = pos;
	line->SetPinPosition(pos);
	auto slot = Cast<UCanvasPanelSlot>(Pin->Slot);
	slot->SetPosition(pos);
}

void UITwin2DCalloutWidgetImpl::SetLabelPosition(FVector2D pos)
{
	labelPosition = pos;
	line->SetLabelPosition(pos);
	auto slot = Cast<UCanvasPanelSlot>(Label->Slot);
	slot->SetPosition(pos);
}

void UITwin2DCalloutWidgetImpl::SetText(FText inText)
{
	content->SetText(inText);
	auto slot = Cast<UCanvasPanelSlot>(Label->Slot);
	ForceLayoutPrepass();
}

void UITwin2DCalloutWidgetImpl::ToggleShowLabel(bool shown)
{
	bLabelShown = shown;
	UpdateComponentsVisibility();
}

void UITwin2DCalloutWidgetImpl::SetLabelOnly(bool on)
{
	bLabelOnly = on;
	UpdateComponentsVisibility();
}

bool UITwin2DCalloutWidgetImpl::IsLabelShown() const
{
	return bLabelShown;
}

FText UITwin2DCalloutWidgetImpl::GetText() const
{
	return content->GetText();
}

void UITwin2DCalloutWidgetImpl::SetBackgroundColor(const FLinearColor& inColor)
{
	Label->SetBrushColor(inColor);
	Pin->SetBrushColor(inColor);
}

FLinearColor UITwin2DCalloutWidgetImpl::GetBackgroundColor() const
{
	return Label->GetBrushColor();
}

void UITwin2DCalloutWidgetImpl::SetTextColor(const FLinearColor& InColor)
{
	content->SetColorAndOpacity(InColor);
	Image->SetColorAndOpacity(InColor);
}

FLinearColor UITwin2DCalloutWidgetImpl::GetTextColor() const
{
	return Image->GetColorAndOpacity();
}

void UITwin2DCalloutWidgetImpl::SetFontSize(int32 InSize)
{
	content->SetFont(FSlateFontInfo(content->GetFont().FontObject, InSize));
}

void UITwin2DCalloutWidgetImpl::SetFontObject(const UObject* InFontObject)
{
	content->SetFont(FSlateFontInfo(InFontObject, content->GetFont().Size));
}

void UITwin2DCalloutWidgetImpl::UpdateComponentsVisibility()
{
	if (bLabelOnly)
	{
		Label->SetVisibility(bLabelShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		line->SetVisibility(ESlateVisibility::Hidden);
		Pin->SetVisibility(bLabelShown ? ESlateVisibility::Hidden : ESlateVisibility::SelfHitTestInvisible);
	}
	else
	{
		Label->SetVisibility(bLabelShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		line->SetVisibility(bLabelShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		Pin->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
}

bool UITwin2DCalloutWidgetImpl::SetPinTexture(UTexture2D* InTexture)
{
	if (!Image)
		return false;

	if (!InTexture)
		return false;

	IconTexture = InTexture; // keeps it alive against GC
	Image->SetBrushFromTexture(InTexture, /*bMatchSize*/ true);
	return true;
}

void UITwin2DCalloutWidgetImpl::EnableButtonInteractions(bool bEnable)
{
	if (PinButton)
	{
		PinButton->SetIsEnabled(bEnable);
	}
}

void UITwin2DCalloutWidgetImpl::OnPinButtonPressed()
{
	if (ensure(PinButton))
	{
		OnPinButtonPressedEvent.Broadcast();
	}
}
