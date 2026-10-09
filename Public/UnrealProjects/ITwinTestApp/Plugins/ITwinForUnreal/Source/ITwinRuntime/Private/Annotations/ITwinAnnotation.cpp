/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAnnotation.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Annotations/ITwinAnnotation.h>
#include <Annotations/ITwin2DAnnotationWidgetImpl.h>

AITwinAnnotation::AITwinAnnotation()
{
}

void AITwinAnnotation::DoBuildOnScreenWidget()
{
	CreateOnScreenWidgetFromClass<UITwin2DAnnotationWidgetImpl>(
		TEXT("/Script/UMGEditor.WidgetBlueprint'/ITwinForUnreal/ITwin/Annotations/ITwin2DAnnotationWidget.ITwin2DAnnotationWidget_C'"));
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
	SetAVizAnnotation(annotationPtr); // Set annotation at the end to avoid should save from changing
}

void AITwinAnnotation::SetAVizAnnotation(const AdvViz::SDK::AnnotationPtr& annotation)
{
	aVizAnnotationPtr = annotation;
}

void AITwinAnnotation::OnTextModified(const FText& InText)
{
	if (aVizAnnotationPtr)
	{
		auto aVizAnnotation = aVizAnnotationPtr->GetAutoLock();
		const FString TextString = InText.ToString();
		const std::string NewText = TCHAR_TO_UTF8(*TextString);
		if (aVizAnnotation->text != NewText)
		{
			aVizAnnotation->text = NewText;
			aVizAnnotation->SetShouldSave(true);
		}
	}
}

void AITwinAnnotation::OnVisibilityModified(bool bInVisible)
{
	if (aVizAnnotationPtr)
	{
		auto aVizAnnotation = aVizAnnotationPtr->GetAutoLock();
		std::string displayMode = DisplayModeToString(Mode, bInVisible);
		if (aVizAnnotation->displayMode.value_or("Marker and label") != displayMode)
		{
			aVizAnnotation->displayMode = displayMode;
			aVizAnnotation->SetShouldSave(true);
		}
	}
}

void AITwinAnnotation::OnModeModified(EITwinCalloutMode InMode)
{
	if (aVizAnnotationPtr)
	{
		auto aVizAnnotation = aVizAnnotationPtr->GetAutoLock();
		std::string displayMode = DisplayModeToString(InMode, bVisible);
		if (aVizAnnotation->displayMode.value_or("Marker and label") != displayMode)
		{
			aVizAnnotation->displayMode = displayMode;
			aVizAnnotation->SetShouldSave(true);
		}
	}
}

void AITwinAnnotation::OnColorThemeModified(EITwinCalloutColor InColor)
{
	if (aVizAnnotationPtr)
	{
		auto aVizAnnotation = aVizAnnotationPtr->GetAutoLock();
		if (aVizAnnotation->colorTheme.value_or("Dark") != ColorThemeToString(InColor))
		{
			aVizAnnotation->colorTheme = ColorThemeToString(InColor);
			aVizAnnotation->SetShouldSave(true);
		}
	}
}

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

void AITwinAnnotation::OnNameModified(const FString& NewName)
{
	if (aVizAnnotationPtr)
	{
		auto aVizAnnotation = aVizAnnotationPtr->GetAutoLock();
		if (aVizAnnotation->name != TCHAR_TO_UTF8(*NewName))
		{
			aVizAnnotation->name = TCHAR_TO_UTF8(*NewName);
			aVizAnnotation->SetShouldSave(true);
		}
	}
}

void AITwinAnnotation::OnFontSizeModified(int32 InFontSize)
{
	if (Is2DMode())
	{
		if (aVizAnnotationPtr)
		{
			auto aVizAnnotation = aVizAnnotationPtr->GetAutoLock();
			if (aVizAnnotation->fontSize.value_or(14) != InFontSize)
			{
				if (InFontSize == 14)
					aVizAnnotation->fontSize.reset();
				else
					aVizAnnotation->fontSize = InFontSize;
				aVizAnnotation->SetShouldSave(true);
			}
		}
	}
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
