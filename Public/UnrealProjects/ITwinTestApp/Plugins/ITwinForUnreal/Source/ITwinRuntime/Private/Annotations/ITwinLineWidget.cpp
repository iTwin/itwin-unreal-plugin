/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinLineWidget.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Annotations/ITwinLineWidget.h>

#include <Blueprint/WidgetBlueprintLibrary.h>
#include <Components/CanvasPanelSlot.h>
#include <Components/Widget.h>
#include <Kismet/KismetMathLibrary.h>


void UITwinLineWidget::SetPinPosition(FVector2D pos)
{
	pinPosition = pos;
}

void UITwinLineWidget::SetLabelPosition(FVector2D pos)
{
	labelPosition = pos;
}

void UITwinLineWidget::SetLabelHeight(double height)
{
	labelHeight = height;
}

int32 UITwinLineWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	auto direction = (labelPosition - pinPosition);
	direction.Normalize();
	FPaintContext Context = FPaintContext(AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	UWidgetBlueprintLibrary::DrawLine(Context, pinPosition + (direction * 10.0f), labelPosition - (direction * 10.0f));
	auto newLayerId = FMath::Max(LayerId, Context.MaxLayer);
	return Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, newLayerId, InWidgetStyle, bParentEnabled);
}
