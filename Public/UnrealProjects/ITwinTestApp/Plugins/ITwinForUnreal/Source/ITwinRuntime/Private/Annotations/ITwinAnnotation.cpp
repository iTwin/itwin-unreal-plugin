/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAnnotation.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Annotations/ITwinAnnotation.h>
#include <Annotations/ITwin2DAnnotationWidgetImpl.h>

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Materials/Material.h"

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Util/CleanUpGuard.h>
#	include <Core/Visualization/AnnotationsManager.h>
#include <Compil/AfterNonUnrealIncludes.h>


template <typename ObjClass>
static ObjClass* LoadObjFromPath(const FName& Path)
{
	if (Path == NAME_None)
		return nullptr;
	return Cast<ObjClass>(StaticLoadObject(ObjClass::StaticClass(), nullptr, *Path.ToString()));
}

static UMaterial* LoadMaterialFromPath(const FName& Path)
{
	if (Path == NAME_None)
		return nullptr;
	return LoadObjFromPath<UMaterial>(Path);
}

/*static*/ bool AITwinAnnotation::bVRMode = false;

/*static*/ void AITwinAnnotation::EnableVR()
{
	bVRMode = true;
}

AITwinAnnotation::AITwinAnnotation()
{
	PrimaryActorTick.bCanEverTick = true;

	root = CreateDefaultSubobject<USceneComponent>(TEXT("Root Position"));

	SetRootComponent(root);

	BuildWidget();
}

void AITwinAnnotation::BeginPlay()
{
	Super::BeginPlay();
	SetTickGroup(ETickingGroup::TG_PostUpdateWork);
	SetColorTheme(colorTheme);
	SetMode(mode);
	//hiding annotations if in VR mode
	if (AITwinAnnotation::VRMode()) {
		SetVisibility(false);
	}
}

void AITwinAnnotation::BuildWidget()
{
	onScreen = CreateWidget<UITwin2DAnnotationWidgetImpl>(GetWorld(), LoadClass <UITwin2DAnnotationWidgetImpl> (nullptr,
		TEXT("/Script/UMGEditor.WidgetBlueprint'/ITwinForUnreal/ITwin/Annotations/ITwin2DAnnotationWidget.ITwin2DAnnotationWidget_C'")));
	if (onScreen)
	{
		onScreen->AddToViewport();
		onScreen->SetText(content);
	}
}

bool AITwinAnnotation::Destroy(bool bNetForce, bool bShouldModifyLevel)
{
	if (onScreen)
	{
		onScreen->RemoveFromParent();
		onScreen = nullptr;
	}
	return Super::Destroy(bNetForce, bShouldModifyLevel);
}

std::shared_ptr<AdvViz::SDK::Annotation> AITwinAnnotation::GetAVizAnnotation() const
{
	if (!aVizAnnotation)
	{
		auto position = GetActorLocation();
		AdvViz::SDK::Annotation annot = {
			{position.X, position.Y, position.Z},
			TCHAR_TO_UTF8(*GetText().ToString()),
			TCHAR_TO_UTF8(*GetName()),
			ColorThemeToString(GetColorTheme()),
			DisplayModeToString(GetDisplayMode())
		};
		aVizAnnotation = std::make_shared<AdvViz::SDK::Annotation>(annot);
	}
	return aVizAnnotation;
}

void AITwinAnnotation::LoadAVizAnnotation(const std::shared_ptr<AdvViz::SDK::Annotation>&annotation)
{
	SetText(FText::FromString(UTF8_TO_TCHAR(annotation->text.c_str())));
	SetName(UTF8_TO_TCHAR(annotation->name.value_or("").c_str()));
	SetColorTheme(ColorThemeToEnum(annotation->colorTheme.value_or("Dark")));
	SetMode(DisplayModeToEnum(annotation->displayMode.value_or("Marker and label")));
	SetAVizAnnotation(annotation);// set annotation at the end to avoid Should save from changing
}

void AITwinAnnotation::SetAVizAnnotation(const std::shared_ptr<AdvViz::SDK::Annotation>& annotation)
{
	aVizAnnotation = annotation;
}

const FText& AITwinAnnotation::GetText() const
{
	return content;
}

void AITwinAnnotation::SetText(const FText& text)
{
	if (content.ToString() != text.ToString() && aVizAnnotation)
	{
		aVizAnnotation->text = std::string(reinterpret_cast<const char*>(StringCast<UTF8CHAR>(*text.ToString()).Get()));
		aVizAnnotation->SetShouldSave(true);
	}
	content = text;
	onScreen->SetText(text);
	OnTextChanged.Broadcast(this, text);
}

void AITwinAnnotation::SetVisibility(bool InBVisible)
{
	if (AITwinAnnotation::VRMode() && InBVisible)
		return;
	bVisible = InBVisible;
	onScreen->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
}

bool AITwinAnnotation::Is2DMode() const
{
	return mode == EITwinAnnotationMode::BasicWidget
		|| mode == EITwinAnnotationMode::FixedWidget
		|| mode == EITwinAnnotationMode::LabelOnly;
}

void AITwinAnnotation::SetMode(EITwinAnnotationMode inMode)
{
	bool bWas2d = Is2DMode();
	if (aVizAnnotation && (aVizAnnotation->displayMode.value_or("Marker and label") != DisplayModeToString(inMode)))
	{
		aVizAnnotation->displayMode = DisplayModeToString(inMode);
		aVizAnnotation->SetShouldSave(true);
	}
	mode = inMode;
	if (Is2DMode())
	{
		//textRender->SetVisibility(false, true);
		onScreen->SetVisibility(ESlateVisibility::HitTestInvisible);
		onScreen->SetLabelOnly(mode == EITwinAnnotationMode::LabelOnly);
	}
	else if (!Is2DMode())
	{
		onScreen->SetVisibility(ESlateVisibility::Hidden);
	}
}

EITwinAnnotationMode AITwinAnnotation::GetDisplayMode() const
{
	return mode;
}

void AITwinAnnotation::SetColorTheme(EITwinAnnotationColor color)
{
	if (aVizAnnotation && (aVizAnnotation->colorTheme.value_or("Dark") != ColorThemeToString(color)))
	{
		aVizAnnotation->colorTheme = ColorThemeToString(color);
		aVizAnnotation->SetShouldSave(true);
	}
	colorTheme = color;
	if (colorTheme == EITwinAnnotationColor::Dark) {
		onScreen->SetBackgroundColor(FLinearColor(0.067f, 0.071f, 0.075f, 1.0f));
		onScreen->SetTextColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
	} else if (colorTheme == EITwinAnnotationColor::Blue) {
		onScreen->SetBackgroundColor(FLinearColor(0.002f, 0.162f, 0.724));
		onScreen->SetTextColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
	}
	else if (colorTheme == EITwinAnnotationColor::Green)
	{
		onScreen->SetBackgroundColor(FLinearColor(0.016f, 0.231f, 0.001));
		onScreen->SetTextColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
	}
	else if (colorTheme == EITwinAnnotationColor::Orange)
	{
		onScreen->SetBackgroundColor(FLinearColor(0.323143f, 0.138432f, 0.000911f));
		onScreen->SetTextColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
	}
	else if (colorTheme == EITwinAnnotationColor::Red)
	{
		onScreen->SetBackgroundColor(FLinearColor(0.737911f, 0.01096f, 0.052861f));
		onScreen->SetTextColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
	}
	else if (colorTheme == EITwinAnnotationColor::White)
	{
		onScreen->SetBackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f));
		onScreen->SetTextColor(FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
	}
	else if (colorTheme == EITwinAnnotationColor::None)
	{
		onScreen->SetBackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.0f));
		onScreen->SetTextColor(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
	}
}

EITwinAnnotationColor AITwinAnnotation::GetColorTheme() const
{
	return colorTheme;
}

void AITwinAnnotation::OnModeChanged()
{}

void AITwinAnnotation::Relocate(FVector position, FRotator rotation)
{
	SetActorLocationAndRotation(position, rotation);
	if (aVizAnnotation)
	{
		aVizAnnotation->position = {position.X, position.Y, position.Z};
		aVizAnnotation->SetShouldSave(true);
	}
}

void AITwinAnnotation::SetBackgroundColor(const FLinearColor& color)
{
	if (Is2DMode())
	{
		onScreen->SetBackgroundColor(color);
	}
}

FLinearColor AITwinAnnotation::GetBackgroundColor() const
{
	return _backgroundColor;
}

void AITwinAnnotation::SetTextColor(const FLinearColor& color)
{
	if (Is2DMode())
	{
		onScreen->SetTextColor(color);
	}
}

FLinearColor AITwinAnnotation::GetTextColor() const
{
	return _textColor;
}

void AITwinAnnotation::SetName(FString newName)
{
	name = newName;
	if (aVizAnnotation && (aVizAnnotation->name != TCHAR_TO_UTF8(*newName)))
	{
		aVizAnnotation->name = TCHAR_TO_UTF8(*newName);
		aVizAnnotation->SetShouldSave(true);
	}
}

FString AITwinAnnotation::GetName() const
{
	return name;
}

void AITwinAnnotation::SetShouldSave(bool shouldSave)
{
	if (aVizAnnotation)
		aVizAnnotation->SetShouldSave(shouldSave);
}

bool AITwinAnnotation::CalculatePinPosition(FVector2D& out)
{
	auto playerCtrl = GetWorld()->GetFirstPlayerController();
	bool bresult = UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(playerCtrl, GetActorLocation(), out, false);
	auto size = UWidgetLayoutLibrary::GetViewportSize(GetWorld());
	if (out.X < 0 || out.Y < 0 || out.X > size.X || out.Y > size.Y)
		return false;
	return bresult;
}

void AITwinAnnotation::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bVisible)
		return;

	if (Is2DMode())
	{
		FVector2D scrPos;
		if (CalculatePinPosition(scrPos))
		{
			onScreen->SetVisibility(ESlateVisibility::HitTestInvisible);
			if (mode == EITwinAnnotationMode::LabelOnly) {
				onScreen->SetPinPosition(scrPos);
				onScreen->SetLabelPosition(scrPos);
			} else {
				onScreen->SetPinPosition(scrPos);
				onScreen->SetLabelPosition(FVector2D(scrPos.X, scrPos.Y - 100));
			}
		}
		else
			onScreen->SetVisibility(ESlateVisibility::Hidden);

		APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
		if (!PlayerController || !PlayerController->PlayerCameraManager)
			return;
		FVector camLoc = PlayerController->PlayerCameraManager.Get()->GetCameraLocation();
		auto dist = UKismetMathLibrary::Vector_Distance(GetActorLocation(), camLoc);
		if (Is2DMode() && (dist >= labelCollapseDistance) == onScreen->IsLabelShown())
			onScreen->ToggleShowLabel(!onScreen->IsLabelShown());
	}
}

std::string AITwinAnnotation::ColorThemeToString(EITwinAnnotationColor color)
{
	if (colorNames.find(color) != colorNames.end())
		return colorNames.at(color);
	else
		return "Dark";
}

std::string AITwinAnnotation::DisplayModeToString(EITwinAnnotationMode mode)
{
	if (mode == EITwinAnnotationMode::LabelOnly)
		return "Label only";
	else
		return "Marker and label";
}

EITwinAnnotationColor AITwinAnnotation::ColorThemeToEnum(const std::string& color)
{
	for (auto [key, value] : colorNames)
	{
		if (value == color)
			return key;
	}
	return EITwinAnnotationColor::Dark;
}

EITwinAnnotationMode AITwinAnnotation::DisplayModeToEnum(const std::string& mode)
{
	if (mode == "Label only")
		return EITwinAnnotationMode::LabelOnly;
	else
		return EITwinAnnotationMode::BasicWidget;
}
