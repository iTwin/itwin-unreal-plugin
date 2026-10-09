/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinCallout.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Callouts/ITwinCallout.h>
#include <Annotations/ITwin2DAnnotationWidgetImpl.h>

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Materials/Material.h"
#include "Components/WidgetComponent.h"

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <map>
#	include <BeHeaders/Util/CleanUpGuard.h>
#	include <Core/Visualization/AnnotationsManager.h>
#include <Compil/AfterNonUnrealIncludes.h>

namespace ITwin::Callout
{
	static TObjectPtr<const UObject> CustomFontObject;

	static const std::map<EITwinCalloutColor, std::string> ColorNames
	{
		{ EITwinCalloutColor::Dark, "Dark" },
		{ EITwinCalloutColor::Blue, "Blue" },
		{ EITwinCalloutColor::Green, "Green" },
		{ EITwinCalloutColor::Orange, "Orange" },
		{ EITwinCalloutColor::Red, "Red"},
		{ EITwinCalloutColor::White, "White"},
		{ EITwinCalloutColor::None, "None"}
	};

	static const std::map<EITwinCalloutColor, FLinearColor> BackgroundColors
	{
		{ EITwinCalloutColor::Dark, FLinearColor(0.067f, 0.071f, 0.075f, 1.0f) },
		{ EITwinCalloutColor::Blue, FLinearColor(0.002f, 0.162f, 0.724) },
		{ EITwinCalloutColor::Green, FLinearColor(0.016f, 0.231f, 0.001) },
		{ EITwinCalloutColor::Orange, FLinearColor(0.714f, 0.349f, 0.001) },
		{ EITwinCalloutColor::Red, FLinearColor(0.714f, 0.001f, 0.001) },
		{ EITwinCalloutColor::White, FLinearColor(1.0f, 1.0f, 1.0f) },
		{ EITwinCalloutColor::None, FLinearColor(1.0f, 1.0f, 1.0f, 0.0f) }
	};
}

/*static*/ bool AITwinCallout::bVRMode = false;

/*static*/ void AITwinCallout::EnableVR()
{
	bVRMode = true;
}

/*static*/ void AITwinCallout::SetCustomFontObject(const UObject* InFontObject)
{
	ITwin::Callout::CustomFontObject = InFontObject;
}

AITwinCallout::AITwinCallout()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root Position"));
	SetRootComponent(Root);

	// Create widget component for world-space rendering
	WidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("CalloutWidgetComponent"));
	WidgetComponent->SetupAttachment(Root);
	WidgetComponent->SetWidgetSpace(EWidgetSpace::Screen);
	WidgetComponent->SetDrawSize(FVector2D(400.0f, 200.0f));
	WidgetComponent->SetPivot(FVector2D(0.0f, 0.0f)); // Pivot at bottom center (pin position)
	WidgetComponent->SetVisibility(false); // Hidden by default until BeginPlay
}

void AITwinCallout::BeginPlay()
{
	Super::BeginPlay();
	SetTickGroup(ETickingGroup::TG_PostUpdateWork);

	BuildOnScreenWidget();
	// Init the appropriate widget type based on settings
	if (bUseWorldSpaceWidgets)
	{
		InitWorldSpaceWidget();
	}
	else
	{
		InitViewportWidget();
	}

	SetColorTheme(ColorTheme);
	SetMode(Mode);
	SetFontSize(FontSize);

	// Hiding callouts if in VR mode
	if (AITwinCallout::VRMode())
	{
		SetVisibility(false);
	}
}

void AITwinCallout::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveWidgetFromViewport();
	Super::EndPlay(EndPlayReason);
}

void AITwinCallout::Destroyed()
{
	RemoveWidgetFromViewport();
	Super::Destroyed();
}

void AITwinCallout::BuildOnScreenWidget()
{
	if (OnScreen || !GetWorld())
		return;
	DoBuildOnScreenWidget();
	if (OnScreen)
	{
		if (ITwin::Callout::CustomFontObject)
		{
			OnScreen->SetFontObject(ITwin::Callout::CustomFontObject);
		}
		OnScreen->SetText(Content);
	}
}

void AITwinCallout::InitWorldSpaceWidget()
{
	BE_ASSERT(bUseWorldSpaceWidgets);
	if (!WidgetComponent || !OnScreen || !bUseWorldSpaceWidgets)
		return;

	// Configure widget component for world-space rendering
	OnScreen->SetPinPosition(FVector2D(0.0f, 0.0f));
	WidgetComponent->SetWidget(OnScreen);
	WidgetComponent->SetVisibility(bVisible);

	// Set collision and rendering properties
	WidgetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WidgetComponent->SetCastShadow(false);
}

void AITwinCallout::InitViewportWidget()
{
	BE_ASSERT(!bUseWorldSpaceWidgets);
	if (!OnScreen || bUseWorldSpaceWidgets)
		return;

	// Do not let the component own the widget in this mode.
	if (WidgetComponent)
	{
		WidgetComponent->SetWidget(nullptr);
		WidgetComponent->SetVisibility(false);
	}

	if (!OnScreen->IsInViewport())
	{
		static constexpr int32 CalloutZOrder = 0;
		OnScreen->AddToViewport(CalloutZOrder);
	}
	// Root must not be HitTestInvisible, or PinButton stays unreachable.
	OnScreen->SetVisibility(bVisible ? ESlateVisibility::SelfHitTestInvisible
		: ESlateVisibility::Hidden);
}

void AITwinCallout::RemoveWidgetFromViewport()
{
	if (!bUseWorldSpaceWidgets && OnScreen)
	{
		OnScreen->RemoveFromParent();
		OnScreen = nullptr;
	}
}

const FText& AITwinCallout::GetText() const
{
	return Content;
}

void AITwinCallout::OnTextModified(const FText& /*InText*/)
{

}

void AITwinCallout::SetText(const FText& InText)
{
	Content = InText;
	OnTextModified(InText);
	UpdateDisplay();
	OnTextChanged.Broadcast(this, InText);
}

void AITwinCallout::OnVisibilityModified(bool /*bInVisible*/)
{

}

void AITwinCallout::SetVisibility(bool bInVisible)
{
	if (AITwinCallout::VRMode() && bInVisible)
		return;

	OnVisibilityModified(bInVisible);

	bVisible = bInVisible;
	UpdateDisplay();
}

bool AITwinCallout::Is2DMode() const
{
	return Mode == EITwinCalloutMode::BasicWidget
		|| Mode == EITwinCalloutMode::FixedWidget
		|| Mode == EITwinCalloutMode::LabelOnly;
}

void AITwinCallout::OnModeModified(EITwinCalloutMode /*InMode*/)
{

}

void AITwinCallout::SetMode(EITwinCalloutMode InMode)
{
	OnModeModified(InMode);
	Mode = InMode;
	UpdateDisplay();
}

void AITwinCallout::SetModeFromIndex(int InMode)
{
	if (InMode < 0 || InMode >= 2)
		return;
	if (InMode == 0)
		SetMode(EITwinCalloutMode::BasicWidget);
	else if (InMode == 1)
		SetMode(EITwinCalloutMode::LabelOnly);
}

EITwinCalloutMode AITwinCallout::GetDisplayMode() const
{
	return Mode;
}

int AITwinCallout::GetDisplayModeIndex() const
{
	return Mode == EITwinCalloutMode::LabelOnly ? 1 : 0;
}

void AITwinCallout::OnColorThemeModified(EITwinCalloutColor /*InColor*/)
{

}

void AITwinCallout::SetColorTheme(EITwinCalloutColor InColor)
{
	OnColorThemeModified(InColor);
	ColorTheme = InColor;
	UpdateDisplay();
}

void AITwinCallout::SetColorThemeFromIndex(int color)
{
	color += 1; // To skip undefined
	if (color <= 0 || color >= static_cast<int>(EITwinCalloutColor::Count))
		return;
	SetColorTheme(static_cast<EITwinCalloutColor>(color));
}

EITwinCalloutColor AITwinCallout::GetColorTheme() const
{
	return ColorTheme;
}

int AITwinCallout::GetColorThemeIndex() const
{
	return (int)ColorTheme - 1; // -1 to ignore undefined
}

void AITwinCallout::OnModeChanged()
{}

void AITwinCallout::SetBackgroundColor(const FLinearColor& color)
{
	if (Is2DMode() && OnScreen)
	{
		OnScreen->SetBackgroundColor(color);
	}
}

FLinearColor AITwinCallout::GetBackgroundColor() const
{
	if (OnScreen)
	{
		return OnScreen->GetBackgroundColor();
	}
	else
	{
		return ColorThemeToBackgroundColor(ColorTheme);
	}
}

void AITwinCallout::SetTextColor(const FLinearColor& color)
{
	if (Is2DMode() && OnScreen)
	{
		OnScreen->SetTextColor(color);
	}
}

FLinearColor AITwinCallout::GetTextColor() const
{
	if (OnScreen)
	{
		return OnScreen->GetTextColor();
	}
	else
	{
		return ColorTheme == EITwinCalloutColor::White
			? FLinearColor::Black
			: FLinearColor::White;
	}
}

void AITwinCallout::OnNameModified(const FString& /*InNewName*/)
{

}

void AITwinCallout::SetName(FString NewName)
{
	Name = NewName;
	OnNameModified(NewName);
}

void AITwinCallout::OnFontSizeModified(int32 /*InNewFontSize*/)
{

}

void AITwinCallout::SetFontSize(int32 InFontSize)
{
	FontSize = InFontSize;
	OnFontSizeModified(InFontSize);
	if (Is2DMode())
	{
		UpdateDisplay();
	}
}

int AITwinCallout::GetFontSize() const
{
	return FontSize;
}

bool AITwinCallout::GetVisibility() const
{
	return bVisible;
}

void AITwinCallout::ConfigureWidget(UITwin2DCalloutWidgetImpl* Widget) const
{
	if (!Widget)
		return;

	Widget->SetText(Content);
	Widget->SetLabelOnly(Mode == EITwinCalloutMode::LabelOnly);
	Widget->SetBackgroundColor(ColorThemeToBackgroundColor(ColorTheme));
	Widget->SetTextColor(ColorTheme == EITwinCalloutColor::White
		? FLinearColor(0.0f, 0.0f, 0.0f, 1.0f)
		: FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
	Widget->SetFontSize(FontSize);

	if (Mode == EITwinCalloutMode::LabelOnly)
	{
		Widget->SetPinPosition(FVector2D(0.0f, 0.0f));
		Widget->SetLabelPosition(FVector2D(0.0f, 0.0f));
	}
	else
	{
		Widget->SetPinPosition(FVector2D(0.0f, 0.0f));
		Widget->SetLabelPosition(FVector2D(0.0f, -100.0f));
	}
}

void AITwinCallout::UpdateDisplay()
{
	if (!OnScreen)
		return;

	ConfigureWidget(OnScreen);

	if (bUseWorldSpaceWidgets && WidgetComponent)
	{
		WidgetComponent->SetVisibility(bVisible);
	}
	else if (OnScreen)
	{
		OnScreen->SetVisibility(bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Hidden);
	}
}

void AITwinCallout::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bVisible)
		return;

	if (Is2DMode() && OnScreen)
	{
		APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();

		if (!bUseWorldSpaceWidgets)
		{
			// When we don't use world-space widgets, we need to update the position of the callout in screen
			// space every tick.
			FVector2D ScreenPos = FVector2D::ZeroVector;
			bool bOnScreen = PlayerController
				&& UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
					PlayerController, GetActorLocation(), ScreenPos, /*bPlayerViewportRelative*/ false);
			if (bOnScreen)
			{
				const FVector2D ViewportSize = UWidgetLayoutLibrary::GetViewportSize(this);
				bOnScreen =
					ScreenPos.X >= 0. && ScreenPos.X < ViewportSize.X &&
					ScreenPos.Y >= 0. && ScreenPos.Y < ViewportSize.Y;
			}
			if (bOnScreen)
			{
				OnScreen->SetPinPosition(ScreenPos);
				OnScreen->SetLabelPosition(ScreenPos + FVector2D(0.0f, -100.0f));
			}
			const ESlateVisibility Wanted = bOnScreen
				? ESlateVisibility::SelfHitTestInvisible
				: ESlateVisibility::Hidden;
			// Update visibility only if it changed, to avoid unnecessary Slate updates.
			if (OnScreen->GetVisibility() != Wanted)
			{
				OnScreen->SetVisibility(Wanted);
			}
		}

		// Distance based label collapsing to reduce clutter.
		if (PlayerController && PlayerController->PlayerCameraManager)
		{
			const FVector CameraLocation = PlayerController->PlayerCameraManager->GetCameraLocation();

			const double Dist = UKismetMathLibrary::Vector_Distance(GetActorLocation(), CameraLocation);
			if ((Dist >= LabelCollapseDistance) == OnScreen->IsLabelShown())
			{
				OnScreen->ToggleShowLabel(!OnScreen->IsLabelShown());
			}
		}
	}
}

std::string AITwinCallout::ColorThemeToString(EITwinCalloutColor Color)
{
	using namespace ITwin::Callout;
	if (ColorNames.find(Color) != ColorNames.end())
		return ColorNames.at(Color);
	else
		return "Dark";
}

std::string AITwinCallout::DisplayModeToString(EITwinCalloutMode Mode, bool bVisibility)
{
	if (Mode == EITwinCalloutMode::LabelOnly)
		return bVisibility ? "Label only" : "Label only;Hidden";
	else
		return bVisibility ? "Marker and label" : "Marker and label;Hidden";
}

EITwinCalloutColor AITwinCallout::ColorThemeToEnum(const std::string& StrColor)
{
	using namespace ITwin::Callout;
	for (auto [key, value] : ColorNames)
	{
		if (value == StrColor)
			return key;
	}
	return EITwinCalloutColor::Dark;
}

EITwinCalloutMode AITwinCallout::DisplayModeToEnum(const std::string& StrMode)
{
	if (StrMode.starts_with("Label only"))
		return EITwinCalloutMode::LabelOnly;
	else
		return EITwinCalloutMode::BasicWidget;
}

FLinearColor AITwinCallout::ColorThemeToBackgroundColor(EITwinCalloutColor Color)
{
	using namespace ITwin::Callout;
	if (BackgroundColors.find(Color) != BackgroundColors.end())
		return BackgroundColors.at(Color);
	else
		return FLinearColor(0.067f, 0.071f, 0.075f, 1.0f);
}

bool AITwinCallout::SetPinTexture(UTexture2D* InTexture)
{
	if (OnScreen)
	{
		return OnScreen->SetPinTexture(InTexture);
	}
	else
	{
		return false;
	}
}

void AITwinCallout::EnableButtonInteractions(bool bEnable)
{
	if (OnScreen)
	{
		OnScreen->EnableButtonInteractions(bEnable);
	}
}

bool AITwinCallout::IsVisibleInViewport() const
{
	BE_ASSERT(!bUseWorldSpaceWidgets);
	if (bVisible && OnScreen)
	{
		return OnScreen->GetVisibility() == ESlateVisibility::SelfHitTestInvisible;
	}
	else
	{
		return false;
	}
}
