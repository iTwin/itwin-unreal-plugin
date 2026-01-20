/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwin2DAnnotationWidgetImpl.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Misc/Attribute.h"

#include "ITwin2DAnnotationWidgetImpl.generated.h"

class UBorder;
class UImage;
class UTextBlock;
class UITwinLineWidget;

UCLASS()
class ITWINRUNTIME_API UITwin2DAnnotationWidgetImpl : public UUserWidget
{
    GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category = "Interface")
	void ToggleShowLabel(bool shown);

	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetLabelOnly(bool on);

	UFUNCTION(BlueprintCallable, Category = "Interface")
	bool IsLabelShown();
	UFUNCTION(BlueprintCallable, Category = "Interface")
	FText GetText();
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetText(FText inText);
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetPinPosition(FVector2D pos);
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetLabelPosition(FVector2D pos);

	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetBackgroundColor(const FLinearColor& inColor);
	UFUNCTION(BlueprintCallable, Category = "Interface")
	FLinearColor GetBackgroundColor();
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetTextColor(const FLinearColor& inColor);
	UFUNCTION(BlueprintCallable, Category = "Interface")
	FLinearColor GetTextColor();

protected:
	FVector2D pinPosition;
	FVector2D labelPosition;
	
private:
	void UpdateComponentsVisibility();
	//UPROPERTY(Meta = (BindWidget))
	//UCanvasPanel* canvas;
	UPROPERTY(Meta = (BindWidget))
	UBorder* Pin;
	UPROPERTY(Meta = (BindWidget))
	UBorder* Label = nullptr;
	UPROPERTY(meta = (BindWidget))
	UTextBlock* content = nullptr;
	UPROPERTY(meta = (BindWidget))
	UImage* Image = nullptr;
	UPROPERTY(meta = (BindWidget))
	UITwinLineWidget* line = nullptr;
	bool bLabelShown = true;
	bool bLabelOnly = false;
};
