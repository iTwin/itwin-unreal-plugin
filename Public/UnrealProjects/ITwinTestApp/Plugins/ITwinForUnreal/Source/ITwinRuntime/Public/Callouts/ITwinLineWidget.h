/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinLineWidget.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ITwinLineWidget.generated.h"

UCLASS()
class ITWINRUNTIME_API UITwinLineWidget : public UUserWidget
{
    GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetPinPosition(FVector2D pos);
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetLabelPosition(FVector2D pos);
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetLabelHeight(double height);

protected:
	FVector2D pinPosition;
	FVector2D labelPosition;
	float labelHeight = 30.0f;

	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
};
