/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinCallout.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/EngineTypes.h"
#include <string>

#include "ITwinCallout.generated.h"

class UITwin2DCalloutWidgetImpl;

class USceneComponent;
class UWidgetComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCalloutChangeText, AITwinCallout*, Callout, FText, Text);

UENUM(BlueprintType)
enum class EITwinCalloutMode : uint8
{
	Undefined UMETA(Hidden),
	BasicBillboard,
	FacingBillboard,
	AutoscaleBillboard,
	BasicWidget,
	FixedWidget,
	LabelOnly,
	Count UMETA(Hidden),
};

UENUM(BlueprintType)
enum class EITwinCalloutColor : uint8
{
	Undefined UMETA(Hidden),
	Dark,
	Blue,
	Green,
	Orange,
	Red,
	White,
	None,
	Count UMETA(Hidden),
};


//! AITwinCallout is a base class for callout actors, which hold 2D UI elements composed of a pin and a
//! label. It is inherited by AITwinAnnotation to display annotations in 2D, and will be reused to display
//! forms markers.
UCLASS()
class ITWINRUNTIME_API AITwinCallout : public AActor
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FOnCalloutChangeText OnTextChanged;

	static void EnableVR();

	static bool VRMode() { return bVRMode; }

	bool UseWorldSpaceWidgets() const { return bUseWorldSpaceWidgets; }
	void SetUseWorldSpaceWidgets(bool bInUseWorldSpaceWidgets) { bUseWorldSpaceWidgets = bInUseWorldSpaceWidgets; }

	/// Set a custom font to use for the on-screen representation of the callouts.
	static void SetCustomFontObject(const UObject* InFontObject);

	static std::string ColorThemeToString(EITwinCalloutColor Color);
	static std::string DisplayModeToString(EITwinCalloutMode Mode, bool bVisibility);
	static EITwinCalloutColor ColorThemeToEnum(const std::string& Color);
	static EITwinCalloutMode DisplayModeToEnum(const std::string& Mode);
	static FLinearColor ColorThemeToBackgroundColor(EITwinCalloutColor Color);


	AITwinCallout();

	UFUNCTION(BlueprintCallable, Category = "Interface")
	const FText& GetText() const;
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetText(const FText& text);

	UFUNCTION(BlueprintCallable, Category = "Interface")
	bool GetVisibility() const;
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetVisibility(bool bInVisible);

	UFUNCTION(BluePrintCallable, Category = "Interface")
	bool Is2DMode() const;

	UFUNCTION(BluePrintCallable, Category = "Interface")
	void SetMode(EITwinCalloutMode InMode);
	UFUNCTION(BluePrintCallable, Category = "Interface")
	void SetModeFromIndex(int InMode);
	UFUNCTION(BlueprintCallable, Category = "Interface")
	EITwinCalloutMode GetDisplayMode() const;
	UFUNCTION(BlueprintCallable, Category = "Interface")
	int GetDisplayModeIndex() const;

	UFUNCTION(BluePrintCallable, Category = "Interface")
	void SetColorTheme(EITwinCalloutColor InColor);
	UFUNCTION(BluePrintCallable, Category = "Interface")
	void SetColorThemeFromIndex(int InColor);
	UFUNCTION(BluePrintCallable, Category = "Interface")
	EITwinCalloutColor GetColorTheme() const;
	UFUNCTION(BlueprintCallable, Category = "Interface")
	int GetColorThemeIndex() const;


	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetBackgroundColor(const FLinearColor& InColor);
	UFUNCTION(BlueprintCallable, Category = "Interface")
	FLinearColor GetBackgroundColor() const;
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetTextColor(const FLinearColor& InColor);
	UFUNCTION(BlueprintCallable, Category = "Interface")
	FLinearColor GetTextColor() const;
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetName(FString InName);
	UFUNCTION(BlueprintCallable, Category = "Interface")
	const FString& GetName() const { return Name; }
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetFontSize(int32 InFontSize);
	UFUNCTION(BlueprintCallable, Category = "Interface")
	int32 GetFontSize() const;

	UFUNCTION(BlueprintCallable, Category = "Interface")
	bool SetPinTexture(UTexture2D* InTexture);

	/// Enable or disable the possibility to interact with the pin button.
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void EnableButtonInteractions(bool bEnable);

	/// Applies current display settings (text, colors, font, mode) to the given widget.
	void ConfigureWidget(UITwin2DCalloutWidgetImpl* Widget) const;

	/// Returns the distance at which the callout label collapses to just the pin marker.
	float GetLabelCollapseDistance() const { return LabelCollapseDistance; }

	//! Returns true if the callout is currently visible in the viewport.
	//! \warning Only valid for viewport widgets, not world-space widgets!
	UFUNCTION(BlueprintCallable, Category = "Interface")
	bool IsVisibleInViewport() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Destroyed() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

	virtual void OnModeModified(EITwinCalloutMode InMode);
	virtual void OnColorThemeModified(EITwinCalloutColor InColor);
	virtual void OnVisibilityModified(bool bInVisible);
	virtual void OnNameModified(const FString& InName);
	virtual void OnTextModified(const FText& InText);
	virtual void OnFontSizeModified(int32 InFontSize);

	UITwin2DCalloutWidgetImpl* GetOnScreenWidget() const { return OnScreen; }
	void BuildOnScreenWidget();
	virtual void DoBuildOnScreenWidget() {}

	template<typename Widget>
	void CreateOnScreenWidgetFromClass(TCHAR const* WidgetClassName)
	{
		TSubclassOf<Widget> WidgetClass = LoadClass<Widget>(nullptr, WidgetClassName);
		if (WidgetClass)
			OnScreen = CreateWidget<Widget>(GetWorld(), WidgetClass);
	}

private:
	void InitWorldSpaceWidget();
	void InitViewportWidget();
	void RemoveWidgetFromViewport();

	void UpdateDisplay();
	void OnModeChanged();


protected:
	UPROPERTY()
	FString Name = TEXT("");
	UPROPERTY()
	FText Content = FText::FromString(TEXT(""));
	UPROPERTY()
	EITwinCalloutMode Mode = EITwinCalloutMode::BasicWidget;
	UPROPERTY()
	EITwinCalloutColor ColorTheme = EITwinCalloutColor::Dark;

	int32 FontSize = 14;

	bool bVisible = true;
	double LabelCollapseDistance = 10000.0;


private:
	static bool bVRMode;
	bool bUseWorldSpaceWidgets = true;

	UPROPERTY(VisibleDefaultsOnly, Category = Interface)
	USceneComponent* Root = nullptr;

	UPROPERTY(VisibleDefaultsOnly, Category = Interface)
	UWidgetComponent* WidgetComponent = nullptr;

	UPROPERTY(VisibleDefaultsOnly, Category = Interface)
	UITwin2DCalloutWidgetImpl* OnScreen = nullptr;
};
