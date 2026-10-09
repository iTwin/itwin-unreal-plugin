/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwin2DCalloutWidgetImpl.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Misc/Attribute.h"

#include "ITwin2DCalloutWidgetImpl.generated.h"

class UBorder;
class UButton;
class UImage;
class UTextBlock;
class UITwinLineWidget;
class UTexture2D;


//! This widget is used to display a 2D callout in the viewport, with a pin and a label.
//! It is used by the UITwin2DAnnotationWidgetImpl to display annotations in 2D.
//! It will be reused to display forms markers in 2D, and other 2D callouts in the future.
UCLASS()
class ITWINRUNTIME_API UITwin2DCalloutWidgetImpl : public UUserWidget
{
    GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category = "Interface")
	void ToggleShowLabel(bool shown);

	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetLabelOnly(bool bOn);

	UFUNCTION(BlueprintCallable, Category = "Interface")
	bool IsLabelShown() const;
	UFUNCTION(BlueprintCallable, Category = "Interface")
	FText GetText() const;
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetText(FText InText);
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetPinPosition(FVector2D InPos);
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetLabelPosition(FVector2D InPos);

	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetBackgroundColor(const FLinearColor& InColor);
	UFUNCTION(BlueprintCallable, Category = "Interface")
	FLinearColor GetBackgroundColor() const;
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetTextColor(const FLinearColor& InColor);
	UFUNCTION(BlueprintCallable, Category = "Interface")
	FLinearColor GetTextColor() const;
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetFontSize(int32 InSize);
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetFontObject(const UObject* InFontObject);

	UFUNCTION(BlueprintCallable, Category = "Interface")
	bool SetPinTexture(UTexture2D* InTexture);

	UFUNCTION(BlueprintCallable, Category = "Interface")
	void OnPinButtonPressed();

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPinButtonPressedEvent);
	UPROPERTY(BlueprintAssignable)
	FOnPinButtonPressedEvent OnPinButtonPressedEvent;

	/// Enable or disable the possibility to interact with the pin button.
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void EnableButtonInteractions(bool bEnable);

protected:
	virtual void NativeConstruct() override;

protected:
	FVector2D pinPosition;
	FVector2D labelPosition;


private:
	void UpdateComponentsVisibility();

	UPROPERTY(Meta = (BindWidget))
	UBorder* Pin = nullptr;

	UPROPERTY(Meta = (BindWidget))
	UBorder* Label = nullptr;

	UPROPERTY(meta = (BindWidget))
	UTextBlock* content = nullptr;

	UPROPERTY(meta = (BindWidget))
	UButton* PinButton = nullptr;

	UPROPERTY(meta = (BindWidget))
	UImage* Image = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> IconTexture = nullptr;

	UPROPERTY(meta = (BindWidget))
	UITwinLineWidget* line = nullptr;

	bool bLabelShown = true;
	bool bLabelOnly = false;
};
