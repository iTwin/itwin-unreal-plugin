/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinStyle.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "ITwinStyle.h"

#include <Brushes/SlateBoxBrush.h>
#include <Interfaces/IPluginManager.h>
#include <Brushes/SlateImageBrush.h>
#include <Styling/SlateStyleMacros.h>
#include <Brushes/SlateNoResource.h>
#include <Styling/SlateStyleRegistry.h>
#include <Brushes/SlateRoundedBoxBrush.h>
#include <Styling/StyleColors.h>
#include <Misc/Paths.h>
#include <Styling/AppStyle.h>
#include <Styling/CoreStyle.h>
#include <Styling/SlateStyle.h>
#include <Styling/SlateTypes.h>

TSharedPtr<FSlateStyleSet> FITwinStyle::StyleInstance = nullptr;

void FITwinStyle::Initialize(FITwinStyleArgs const& CtorArgs /*= {}*/)
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create(CtorArgs);
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

bool FITwinStyle::ApplyToApplication()
{
	if (FITwinStyle::IsCreated())
	{
		FAppStyle::SetAppStyleSetName(FITwinStyle::GetStyleSetName());
		return true;
	}
	return false;
}

void FITwinStyle::Shutdown()
{
	if (StyleInstance)
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
		ensure(StyleInstance.IsUnique());
		StyleInstance.Reset();
	}
}

bool FITwinStyle::IsCreated()
{
	return (bool)StyleInstance;
}

FName FITwinStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("ITwinStyle"));
	return StyleSetName;
}

namespace
{
	inline FString InContent(const FString& ContentDir, const FString& RelativePath, const TCHAR* Extension)
	{
		return (ContentDir / RelativePath) + Extension;
	}
}

#define IMAGE_PLUGIN_BRUSH( RelativePath, ... ) FSlateImageBrush( InContent(ContentDir, RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define IMAGE_PLUGIN_BOX_BRUSH( RelativePath, ... ) FSlateBoxBrush( InContent(ContentDir, RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define IMAGE_PLUGIN_BRUSH_SVG( RelativePath, ... ) FSlateVectorImageBrush( InContent(ContentDir, RelativePath, TEXT(".svg") ), __VA_ARGS__)


TSharedRef<FSlateStyleSet> FITwinStyle::Create(FITwinStyleArgs const& CtorArgs /*= {}*/)
{
	TSharedRef<FSlateStyleSet> StyleSet = MakeShared<FSlateStyleSet>(GetStyleSetName());
	auto const ParentStyleName = FAppStyle::GetAppStyleSetName();
	if (ParentStyleName.IsValid() && ParentStyleName.GetStringLength() > 0)
	{
		StyleSet->SetParentStyleName(ParentStyleName);
	}

	FString ContentDir;
	if (CtorArgs.CustomContentDir.IsSet())
	{
		ContentDir = CtorArgs.CustomContentDir.GetValue();
	}
	else
	{
		ContentDir = IPluginManager::Get().FindPlugin(TEXT("ITwinForUnreal"))->GetContentDir() / TEXT("ITwin/Icons");
	}

	const FVector2D Icon10x10(10.0f, 10.0f);
	//const FVector2D Icon16x16(16.0f, 16.0f);
	const FVector2D Icon24x24(24.0f, 24.0f);
	//const FVector2D Icon32x32(32.0f, 32.0f);
	const FVector2D Icon48x48(48.0f, 48.0f);


	// Customize Application icons
	if (CtorArgs.bShowAppIcon)
	{
		StyleSet->Set("AppIcon", new IMAGE_PLUGIN_BRUSH("AppIcon-48", Icon48x48));
		StyleSet->Set("AppIcon.Small", new IMAGE_PLUGIN_BRUSH("AppIcon-24", Icon24x24));
	}
	else
	{
		StyleSet->Set("AppIcon", new IMAGE_PLUGIN_BRUSH("EmptyIcon-48", Icon48x48));
		StyleSet->Set("AppIcon.Small", new IMAGE_PLUGIN_BRUSH("EmptyIcon-48", Icon24x24));
	}
	StyleSet->Set("AppIconPadding", FMargin(5.0f, 5.0f, 5.0f, 5.0f));
	StyleSet->Set("AppIconPadding.Small", FMargin(4.0f, 4.0f, 0.0f, 0.0f));

	// For (Slate) Color Picker
	const FTextBlockStyle& NormalText = FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
	StyleSet->Set("NormalText", FTextBlockStyle(NormalText)
		.SetColorAndOpacity(FLinearColor::White)
	);
	const FExpandableAreaStyle& ExpandableAreaStyle = FAppStyle::Get().GetWidgetStyle<FExpandableAreaStyle>("ExpandableArea");
	StyleSet->Set("ExpandableArea", FExpandableAreaStyle(ExpandableAreaStyle)
		.SetCollapsedImage(IMAGE_PLUGIN_BRUSH("TreeArrow_Collapsed-10", Icon10x10, FLinearColor::White))
		.SetExpandedImage(IMAGE_PLUGIN_BRUSH("TreeArrow_Expanded-10", Icon10x10, FLinearColor::White))
	);
	return StyleSet;
}

#undef IMAGE_PLUGIN_BRUSH
#undef IMAGE_PLUGIN_BOX_BRUSH
#undef IMAGE_PLUGIN_BRUSH_SVG
