/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAnnotation.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
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
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include "ITwinAnnotation.generated.h"

namespace AdvViz::SDK
{
	struct Annotation;
}
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


	AITwinAnnotation();
	
	bool Destroy(bool bNetForce = false, bool bShouldModifyLevel = true );

	std::shared_ptr<AdvViz::SDK::Annotation> GetAVizAnnotation() const;
	void LoadAVizAnnotation(const std::shared_ptr<AdvViz::SDK::Annotation>& annotation);
	void SetAVizAnnotation(const std::shared_ptr<AdvViz::SDK::Annotation>& annotation);

	UFUNCTION(BlueprintCallable, Category = "Interface")
	const FText& GetText() const;
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetText(const FText& text);
	
	UFUNCTION(BlueprintCallable, Category = "Interface")
	void SetVisibility(bool visible);

	UFUNCTION(BluePrintCallable, Category = "Interface")
	bool Is2DMode() const;
	
	UFUNCTION(BluePrintCallable, Category = "Interface")
	void SetMode(EITwinAnnotationMode mode);
	UFUNCTION(BlueprintCallable, Category = "Interface")
	EITwinAnnotationMode GetDisplayMode() const;
	UFUNCTION(BluePrintCallable, Category = "Interface")
	void SetColorTheme(EITwinAnnotationColor color);
	UFUNCTION(BluePrintCallable, Category = "Interface")
	EITwinAnnotationColor GetColorTheme() const;

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

	void SetShouldSave(bool shouldSave);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	void BuildWidget();

	bool CalculatePinPosition(FVector2D& out);
	void OnModeChanged();

	UPROPERTY()
	FString name = "";
	UPROPERTY()
	FText content = FText::FromString("");
	UPROPERTY()
	EITwinAnnotationMode mode = EITwinAnnotationMode::BasicWidget;
	UPROPERTY()
	EITwinAnnotationColor colorTheme = EITwinAnnotationColor::Dark;

	

	float _height;

	FLinearColor _textColor;
	FLinearColor _backgroundColor;

	bool bVisible = true;
	float labelCollapseDistance = 10000.0f;

	mutable std::shared_ptr<AdvViz::SDK::Annotation> aVizAnnotation = nullptr;

	static std::string ColorThemeToString(EITwinAnnotationColor	color);
	static std::string DisplayModeToString(EITwinAnnotationMode mode);
	static EITwinAnnotationColor ColorThemeToEnum(const std::string& color);
	static EITwinAnnotationMode DisplayModeToEnum(const std::string& mode);

	const static inline std::map<EITwinAnnotationColor, std::string> colorNames {
		{ EITwinAnnotationColor::Dark, "Dark" },
		{ EITwinAnnotationColor::Blue, "Blue" },
		{ EITwinAnnotationColor::Green, "Green" },
		{ EITwinAnnotationColor::Orange, "Orange" },
		{ EITwinAnnotationColor::Red, "Red"},
		{ EITwinAnnotationColor::White, "White"},
		{ EITwinAnnotationColor::None, "None"}
	};
};
