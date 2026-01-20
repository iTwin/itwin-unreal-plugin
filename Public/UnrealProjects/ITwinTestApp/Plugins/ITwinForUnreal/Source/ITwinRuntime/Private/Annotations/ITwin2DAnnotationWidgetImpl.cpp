/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwin2DAnnotationWidgetImpl.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Annotations/ITwin2DAnnotationWidgetImpl.h>
#include <Annotations/ITwinLineWidget.h>

#include <Blueprint/WidgetBlueprintLibrary.h>
#include <Components/Border.h>
#include <Components/CanvasPanel.h>
#include "Components/CanvasPanelSlot.h"
#include <Components/Image.h>
#include <Components/Textblock.h>
#include <Components/Widget.h>
#include <Kismet/KismetMathLibrary.h>


void UITwin2DAnnotationWidgetImpl::SetPinPosition(FVector2D pos)
{
	pinPosition = pos;
	line->SetPinPosition(pos);
	auto slot = Cast<UCanvasPanelSlot>(Pin->Slot);
	slot->SetPosition(pos);
}

void UITwin2DAnnotationWidgetImpl::SetLabelPosition(FVector2D pos)
{
	labelPosition = pos;
	line->SetLabelPosition(pos);
	auto slot = Cast<UCanvasPanelSlot>(Label->Slot);
	slot->SetPosition(pos);
}

void UITwin2DAnnotationWidgetImpl::SetText(FText inText)
{
	content->SetText(inText);
	auto slot = Cast<UCanvasPanelSlot>(Label->Slot);
	ForceLayoutPrepass();
}

void UITwin2DAnnotationWidgetImpl::ToggleShowLabel(bool shown)
{
	bLabelShown = shown;
	UpdateComponentsVisibility();
}

void UITwin2DAnnotationWidgetImpl::SetLabelOnly(bool on)
{
	bLabelOnly = on;
	UpdateComponentsVisibility();
}

bool UITwin2DAnnotationWidgetImpl::IsLabelShown()
{
	return bLabelShown;
}

FText UITwin2DAnnotationWidgetImpl::GetText()
{
	return content->GetText();
}

void UITwin2DAnnotationWidgetImpl::SetBackgroundColor(const FLinearColor& inColor)
{
	Label->SetBrushColor(inColor);
	Pin->SetBrushColor(inColor);
}

FLinearColor UITwin2DAnnotationWidgetImpl::GetBackgroundColor()
{
	return Label->GetBrushColor();
}

void UITwin2DAnnotationWidgetImpl::SetTextColor(const FLinearColor& inColor)
{
	content->SetColorAndOpacity(inColor);
	Image->SetColorAndOpacity(inColor);
}

FLinearColor UITwin2DAnnotationWidgetImpl::GetTextColor()
{
	return Image->GetColorAndOpacity();
}

void UITwin2DAnnotationWidgetImpl::UpdateComponentsVisibility()
{
	if (bLabelOnly)
	{
		Label->SetVisibility(bLabelShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		line->SetVisibility(ESlateVisibility::Hidden);
		Pin->SetVisibility(bLabelShown ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
	}
	else
	{
		Label->SetVisibility(bLabelShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		line->SetVisibility(bLabelShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		Pin->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}
