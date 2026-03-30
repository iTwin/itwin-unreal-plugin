/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAnnotation.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/TextRenderComponent.h"
#include "Engine/EngineTypes.h"

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <map>
#	include <memory>
#	include <string>
#	include <BeHeaders/Util/CleanUpGuard.h>
#	include <Core/Visualization/AnnotationsManager.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>
#include "ITwinAnnotation.generated.h"

class UITwin2DAnnotationWidgetImpl;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAnnotationChangeText, AITwinAnnotation*, annotation, FText, text);

UENUM(BlueprintType)
enum class EITwinAnnotationMode : uint8
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
enum class EITwinAnnotationColor : uint8
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

UCLASS()
class ITWINRUNTIME_API AITwinAnnotation : public AActor
{
	GENERATED_BODY()

	static bool bVRMode;

	UPROPERTY(VisibleDefaultsOnly, Category=Interface)
	USceneComponent* root = nullptr;
	UITwin2DAnnotationWidgetImpl* onScreen = nullptr;

public:
	UPROPERTY(BlueprintAssignable)
	FOnAnnotationChangeText OnTextChanged;

	static void EnableVR();

	static bool VRMode() { return bVRMode; }

	/// Set a custom font to use for the on-screen representation of the annotations.
	static void SetCustomFontObject(const UObject* InFontObject);


	AITwinAnnotation();
	
	bool Destroy(bool bNetForce = false, bool bShouldModifyLevel = true );

	AdvViz::SDK::AnnotationPtr GetAVizAnnotation() const;
	void LoadAVizAnnotation(const AdvViz::SDK::AnnotationPtr& annotation);
	void SetAVizAnnotation(const AdvViz::SDK::AnnotationPtr& annotation);

	UFUNCTION(BlueprintCallable, Category = "Interface")
	const FText& GetText() const;
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetText(const FText& text);
	
	UFUNCTION(BlueprintCallable, Category = "Interface")
	bool GetVisibility() const;
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetVisibility(bool visible);

	UFUNCTION(BluePrintCallable, Category = "Interface")
	bool Is2DMode() const;
	
	UFUNCTION(BluePrintCallable, Category = "Interface")
	void SetMode(EITwinAnnotationMode mode);
	UFUNCTION(BluePrintCallable, Category = "Interface")
	void SetModeFromIndex(int mode);
	UFUNCTION(BlueprintCallable, Category = "Interface")
	EITwinAnnotationMode GetDisplayMode() const;
	UFUNCTION(BlueprintCallable, Category = "Interface")
	int GetDisplayModeIndex() const;
	UFUNCTION(BluePrintCallable, Category = "Interface")
	void SetColorTheme(EITwinAnnotationColor color);
	UFUNCTION(BluePrintCallable, Category = "Interface")
	void SetColorThemeFromIndex(int color);
	UFUNCTION(BluePrintCallable, Category = "Interface")
	EITwinAnnotationColor GetColorTheme() const;
	UFUNCTION(BluePrintCallable, Category = "Interface")
	int GetColorThemeIndex() const;


	UFUNCTION(BlueprintCallable, Category = "Interface")
	void Relocate(FVector position, FRotator rotation);
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetBackgroundColor(const FLinearColor& color);
	UFUNCTION(BlueprintCallable, Category = "Interface")
	FLinearColor GetBackgroundColor() const;
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetTextColor(const FLinearColor& color);
	UFUNCTION(BlueprintCallable, Category = "Interface")
	FLinearColor GetTextColor() const;
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetName(FString newName);
	UFUNCTION(BlueprintCallable, Category = "Interface")
	FString GetName() const;
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetFontSize(int size);
	UFUNCTION(BlueprintCallable, Category = "Interface")
	int GetFontSize() const;

	void SetShouldSave(bool shouldSave);

	void SetId(int inId);
	int GetId() const { return id; }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	void BuildWidget();

	bool CalculatePinPosition(FVector2D& out);
	void UpdateDisplay();
	void OnModeChanged();

	UPROPERTY()
	FString name = "";
	UPROPERTY()
	FText content = FText::FromString("");
	UPROPERTY()
	EITwinAnnotationMode mode = EITwinAnnotationMode::BasicWidget;
	UPROPERTY()
	EITwinAnnotationColor colorTheme = EITwinAnnotationColor::Dark;

	int id = 0;
	float _height;
	int _fontSize = 14;

	FLinearColor _textColor;
	FLinearColor _backgroundColor;

	bool bVisible = true;
	float labelCollapseDistance = 10000.0f;

	mutable AdvViz::SDK::AnnotationPtr aVizAnnotationPtr = nullptr;

	static std::string ColorThemeToString(EITwinAnnotationColor	color);
	static std::string DisplayModeToString(EITwinAnnotationMode mode, bool visibility);
	static EITwinAnnotationColor ColorThemeToEnum(const std::string& color);
	static EITwinAnnotationMode DisplayModeToEnum(const std::string& mode);
	static FLinearColor ColorThemeToBackgroundColor(EITwinAnnotationColor color);

	const static inline std::map<EITwinAnnotationColor, std::string> colorNames {
		{ EITwinAnnotationColor::Dark, "Dark" },
		{ EITwinAnnotationColor::Blue, "Blue" },
		{ EITwinAnnotationColor::Green, "Green" },
		{ EITwinAnnotationColor::Orange, "Orange" },
		{ EITwinAnnotationColor::Red, "Red"},
		{ EITwinAnnotationColor::White, "White"},
		{ EITwinAnnotationColor::None, "None"}
	};

	const static inline std::map<EITwinAnnotationColor, FLinearColor> backgroundColors {
		{ EITwinAnnotationColor::Dark, FLinearColor(0.067f, 0.071f, 0.075f, 1.0f) },
		{ EITwinAnnotationColor::Blue, FLinearColor(0.002f, 0.162f, 0.724) },
		{ EITwinAnnotationColor::Green, FLinearColor(0.016f, 0.231f, 0.001) },
		{ EITwinAnnotationColor::Orange, FLinearColor(0.714f, 0.349f, 0.001) },
		{ EITwinAnnotationColor::Red, FLinearColor(0.714f, 0.001f, 0.001) },
		{ EITwinAnnotationColor::White, FLinearColor(1.0f, 1.0f, 1.0f) },
		{ EITwinAnnotationColor::None, FLinearColor(1.0f, 1.0f, 1.0f, 0.0f) }
	};
};
