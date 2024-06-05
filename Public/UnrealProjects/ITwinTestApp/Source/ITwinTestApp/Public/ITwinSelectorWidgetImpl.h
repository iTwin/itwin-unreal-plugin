/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSelectorWidgetImpl.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "Blueprint/UserWidget.h"
#include "ITwinSelectorWidgetImpl.generated.h"

class UComboBoxString;
class UTextBlock;
class UCanvasPanel;
class UButton;

//! Used as "parent class" of widget "ITwinSelectorWidget".
//! Contains all the logic for this widget.
UCLASS()
class UITwinSelectorWidgetImpl: public UUserWidget
{
	GENERATED_BODY()
public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnITwinSelectedEvent, FString, DisplayName, FString, Value);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnIModelSelectedEvent, FString, DisplayName, FString, Value);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnChangesetSelectedEvent, FString, DisplayName, FString, Value);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnOpenPressedEvent);
	UPROPERTY(meta = (BindWidget))
	UTextBlock* TextError;
	UPROPERTY()
	FOnITwinSelectedEvent OnITwinSelected;
	UPROPERTY()
	FOnIModelSelectedEvent OnIModelSelected;
	UPROPERTY()
	FOnChangesetSelectedEvent OnChangesetSelected;
	UPROPERTY()
	FOnOpenPressedEvent OnOpenPressed;
	UFUNCTION(BlueprintCallable)
	void ShowPanel(int Index);
	UFUNCTION(BlueprintCallable)
	void AddITwin(const FString& DisplayName, const FString& Value);
	UFUNCTION(BlueprintCallable)
	void AddIModel(const FString& DisplayName, const FString& Value);
	UFUNCTION(BlueprintCallable)
	void AddChangeset(const FString& DisplayName, const FString& Value);
	UFUNCTION(BlueprintCallable)
	void DisableITwinPanel();
protected:
	virtual void NativeConstruct() override;
private:
	UPROPERTY(meta = (BindWidget))
	UComboBoxString* ComboBox_iTwin;
	UPROPERTY(meta = (BindWidget))
	UComboBoxString* ComboBox_iModel;
	UPROPERTY(meta = (BindWidget))
	UComboBoxString* ComboBox_Changeset;
	UPROPERTY(meta = (BindWidget))
	UButton* Open;
	UPROPERTY(meta = (BindWidget))
	UCanvasPanel* Panel_Converting;
	UPROPERTY(meta = (BindWidget))
	UCanvasPanel* Panel_Error;
	UPROPERTY(meta = (BindWidget))
	UCanvasPanel* Panel_SelectItwin;
	UPROPERTY()
	TArray<FString> ITwinValues;
	UPROPERTY()
	TArray<FString> IModelValues;
	UPROPERTY()
	TArray<FString> ChangesetValues;
	UFUNCTION(BlueprintCallable)
	void ChangeITwinSelection();
	UFUNCTION(BlueprintCallable)
	void ChangeIModelSelection();
	UFUNCTION(BlueprintCallable)
	void ChangeChangesetSelection();
	UFUNCTION(BlueprintCallable)
	void ITwinChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION(BlueprintCallable)
	void IModelChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION(BlueprintCallable)
	void ChangesetChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION(BlueprintCallable)
	void OnOpenClicked();
};
