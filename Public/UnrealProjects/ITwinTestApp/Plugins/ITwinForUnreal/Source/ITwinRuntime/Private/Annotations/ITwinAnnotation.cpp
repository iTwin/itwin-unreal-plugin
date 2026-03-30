/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAnnotation.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
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

namespace
{
	static TObjectPtr<const UObject> CustomFontObject;
}

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

/*static*/ void AITwinAnnotation::SetCustomFontObject(const UObject* InFontObject)
{
	CustomFontObject = InFontObject;
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
	SetFontSize(_fontSize);
		
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
		if (CustomFontObject)
		{
			onScreen->SetFontObject(CustomFontObject);
		}
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

AdvViz::SDK::AnnotationPtr AITwinAnnotation::GetAVizAnnotation() const
{
	if (!aVizAnnotationPtr)
	{
		auto position = GetActorLocation();

		AdvViz::SDK::Annotation* annot = new AdvViz::SDK::Annotation();
		annot->position = {position.X, position.Y, position.Z};
		annot->text = TCHAR_TO_UTF8(*GetText().ToString());
		annot->fontSize = GetFontSize() == 14 ? std::nullopt : std::optional<int>(GetFontSize());
		annot->name = TCHAR_TO_UTF8(*GetName());
		annot->colorTheme = ColorThemeToString(GetColorTheme());
		annot->displayMode = DisplayModeToString(GetDisplayMode(), bVisible);

		aVizAnnotationPtr = AdvViz::SDK::MakeSharedLockableDataPtr<AdvViz::SDK::Annotation>(annot);
	}
	return aVizAnnotationPtr;
}

void AITwinAnnotation::LoadAVizAnnotation(const AdvViz::SDK::AnnotationPtr&annotationPtr)
{
	auto annotation = annotationPtr->GetAutoLock();
	SetText(FText::FromString(UTF8_TO_TCHAR(annotation->text.c_str())));
	SetFontSize(annotation->fontSize.value_or(14));
	SetName(UTF8_TO_TCHAR(annotation->name.value_or("").c_str()));
	SetColorTheme(ColorThemeToEnum(annotation->colorTheme.value_or("Dark")));
	SetMode(DisplayModeToEnum(annotation->displayMode.value_or("Marker and label")));
	SetVisibility(!(annotation->displayMode.value_or("Marker and label").ends_with(";Hidden")));
	SetAVizAnnotation(annotationPtr);// set annotation at the end to avoid Should save from changing
}

void AITwinAnnotation::SetAVizAnnotation(const AdvViz::SDK::AnnotationPtr& annotation)
{
	aVizAnnotationPtr = annotation;
}

const FText& AITwinAnnotation::GetText() const
{
	return content;
}

void AITwinAnnotation::SetText(const FText& text)
{
	if (content.ToString() != text.ToString() && aVizAnnotationPtr)
	{
		auto aVizAnnotation = aVizAnnotationPtr->GetAutoLock();
		aVizAnnotation->text = std::string(reinterpret_cast<const char*>(StringCast<UTF8CHAR>(*text.ToString()).Get()));
		aVizAnnotation->SetShouldSave(true);
	}
	content = text;
	UpdateDisplay();
	OnTextChanged.Broadcast(this, text);
}

void AITwinAnnotation::SetVisibility(bool InBVisible)
{
	if (AITwinAnnotation::VRMode() && InBVisible || !onScreen)
		return;
	if (aVizAnnotationPtr)
	{
		auto aVizAnnotation = aVizAnnotationPtr->GetAutoLock();
		std::string displayMode = DisplayModeToString(mode, InBVisible);
		if (aVizAnnotation->displayMode.value_or("Marker and label") != displayMode)
		{
			aVizAnnotation->displayMode = displayMode;
			aVizAnnotation->SetShouldSave(true);
		}
	}
	bVisible = InBVisible;
	UpdateDisplay();
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
	if (aVizAnnotationPtr)
	{
		auto aVizAnnotation = aVizAnnotationPtr->GetAutoLock();
		std::string displayMode = DisplayModeToString(inMode, bVisible);
		if (aVizAnnotation->displayMode.value_or("Marker and label") != displayMode)
		{
			aVizAnnotation->displayMode = displayMode;
			aVizAnnotation->SetShouldSave(true);
		}
	}
	mode = inMode;
	UpdateDisplay();
}

void AITwinAnnotation::SetModeFromIndex(int inMode)
{
	if (inMode < 0 || inMode >= 2)
		return;
	if (inMode == 0)
		SetMode(EITwinAnnotationMode::BasicWidget);
	else if (inMode == 1)
		SetMode(EITwinAnnotationMode::LabelOnly);
}

EITwinAnnotationMode AITwinAnnotation::GetDisplayMode() const
{
	return mode;
}

int AITwinAnnotation::GetDisplayModeIndex() const
{
	return mode == EITwinAnnotationMode::LabelOnly ? 1 : 0;
}

void AITwinAnnotation::SetColorTheme(EITwinAnnotationColor color)
{
	if (aVizAnnotationPtr)
	{
		auto aVizAnnotation = aVizAnnotationPtr->GetAutoLock();
		if (aVizAnnotation->colorTheme.value_or("Dark") != ColorThemeToString(color))
		{
			aVizAnnotation->colorTheme = ColorThemeToString(color);
			aVizAnnotation->SetShouldSave(true);
		}
	}
	colorTheme = color;
	UpdateDisplay();
}

void AITwinAnnotation::SetColorThemeFromIndex(int color)
{
	color += 1; // to skip undefined
	if (color <= 0 || color >= static_cast<int>(EITwinAnnotationColor::Count))
		return;
	SetColorTheme(static_cast<EITwinAnnotationColor>(color));
}

EITwinAnnotationColor AITwinAnnotation::GetColorTheme() const
{
	return colorTheme;
}

int AITwinAnnotation::GetColorThemeIndex() const
{
	return (int) colorTheme - 1; // -1 to ignore undefined
}

void AITwinAnnotation::OnModeChanged()
{}

void AITwinAnnotation::Relocate(FVector position, FRotator rotation)
{
	SetActorLocationAndRotation(position, rotation);
	if (aVizAnnotationPtr)
	{
		auto aVizAnnotation = aVizAnnotationPtr->GetAutoLock();
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
	if (aVizAnnotationPtr)
	{
		auto aVizAnnotation = aVizAnnotationPtr->GetAutoLock();
		if (aVizAnnotation->name != TCHAR_TO_UTF8(*newName))
		{
			aVizAnnotation->name = TCHAR_TO_UTF8(*newName);
			aVizAnnotation->SetShouldSave(true);
		}
	}
}

FString AITwinAnnotation::GetName() const
{
	return name;
}

void AITwinAnnotation::SetFontSize(int size)
{
	_fontSize = size;
	if (Is2DMode())
	{
		if (aVizAnnotationPtr)
		{
			auto aVizAnnotation = aVizAnnotationPtr->GetAutoLock();
			if (aVizAnnotation->fontSize.value_or(14) != size)
			{
				if (size == 14)
					aVizAnnotation->fontSize.reset();
				else
					aVizAnnotation->fontSize = size;
				aVizAnnotation->SetShouldSave(true);
			}
		}
		UpdateDisplay();
	}
}

int AITwinAnnotation::GetFontSize() const
{
	return _fontSize;
}

bool AITwinAnnotation::GetVisibility() const
{
	return bVisible;
}

void AITwinAnnotation::SetShouldSave(bool shouldSave)
{
	if (aVizAnnotationPtr)
	{
		auto aVizAnnotation = aVizAnnotationPtr->GetAutoLock();
		aVizAnnotation->SetShouldSave(shouldSave);
	}
}

void AITwinAnnotation::SetId(int inId)
{
	BE_ASSERT(inId >= 0, "keep negative values for actions over 'all'");
	id = inId;
}

void AITwinAnnotation::UpdateDisplay()
{
	if (!onScreen)
		return;
	
	onScreen->SetText(content);
	onScreen->SetLabelOnly(mode == EITwinAnnotationMode::LabelOnly);
	onScreen->SetBackgroundColor(ColorThemeToBackgroundColor(colorTheme));
	onScreen->SetTextColor(colorTheme == EITwinAnnotationColor::White ? FLinearColor(0.0f, 0.0f, 0.0f, 1.0f) : FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
	onScreen->SetFontSize(_fontSize);
	onScreen->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
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

std::string AITwinAnnotation::DisplayModeToString(EITwinAnnotationMode mode, bool visibility)
{
	if (mode == EITwinAnnotationMode::LabelOnly)
		return visibility ? "Label only" : "Label only;Hidden";
	else
		return visibility ? "Marker and label" : "Marker and label;Hidden";
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
	if (mode.starts_with("Label only"))
		return EITwinAnnotationMode::LabelOnly;
	else
		return EITwinAnnotationMode::BasicWidget;
}

FLinearColor AITwinAnnotation::ColorThemeToBackgroundColor(EITwinAnnotationColor color)
{
	if (backgroundColors.find(color) != backgroundColors.end())
		return backgroundColors.at(color);
	else
		return FLinearColor(0.067f, 0.071f, 0.075f, 1.0f);
}
