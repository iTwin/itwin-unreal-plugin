/*--------------------------------------------------------------------------------------+
|
|     $Source: TopMenuWidgetImpl.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "Blueprint/UserWidget.h"
#include "TopMenuWidgetImpl.generated.h"

class UImage;
class UButton;
class UComboBoxString;
class UTextBlock;
class UImage;

//! Used as "parent class" of widget "TopMenuWidget".
//! Contains all the logic for this widget.
UCLASS()
class UTopMenuWidgetImpl: public UUserWidget
{
	GENERATED_BODY()
public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSavedViewSelectedEvent, FString, DisplayName, FString, Value);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnZoomPressedEvent);
	UPROPERTY()
	FOnSavedViewSelectedEvent OnSavedViewSelected;
	FOnZoomPressedEvent OnZoomPressed;
	UFUNCTION(BlueprintCallable)
	void UpdateElementId(bool bVisible, const FString& InElementId);
	UFUNCTION(BlueprintCallable)
	void AddSavedView(const FString& DisplayName, const FString& Value);
	UFUNCTION(BlueprintCallable)
	void RemoveSavedView(const FString& SavedViewId);
protected:
	virtual void NativeConstruct() override;
private:
	UPROPERTY(meta = (BindWidget))
	UImage* ZoomiModel;
	UPROPERTY(meta = (BindWidget))
	UComboBoxString* ComboBox_SavedViews;
	UPROPERTY(meta = (BindWidget))
	UButton* Next;
	UPROPERTY(meta = (BindWidget))
	UButton* Prev;
	UPROPERTY(meta = (BindWidget))
	UTextBlock* ElementId;
	UPROPERTY(meta = (BindWidget))
	UImage* icon_element;
	UPROPERTY()
	TArray<FString> SavedViewsValues;
	UFUNCTION(BlueprintCallable)
	FEventReply OnZoomIModelMouseButtonDown(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	UFUNCTION(BlueprintCallable)
	void OnPrevClicked();
	UFUNCTION(BlueprintCallable)
	void OnNextClicked();
	UFUNCTION(BlueprintCallable)
	void ChangeSavedViews();
	UFUNCTION(BlueprintCallable)
	void SavedViewsChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION(BlueprintCallable)
	void GotoNextSavedView(int Increment);
};
