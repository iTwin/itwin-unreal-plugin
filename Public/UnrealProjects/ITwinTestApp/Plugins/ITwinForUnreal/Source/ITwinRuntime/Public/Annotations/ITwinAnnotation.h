/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAnnotation.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Callouts/ITwinCallout.h>
#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <Core/Visualization/AnnotationsManager.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include "ITwinAnnotation.generated.h"

UCLASS()
class ITWINRUNTIME_API AITwinAnnotation : public AITwinCallout
{
	GENERATED_BODY()

public:
	AITwinAnnotation();

	AdvViz::SDK::AnnotationPtr GetAVizAnnotation() const;
	void LoadAVizAnnotation(const AdvViz::SDK::AnnotationPtr& annotation);
	void SetAVizAnnotation(const AdvViz::SDK::AnnotationPtr& annotation);

	UFUNCTION(BlueprintCallable, Category = "Interface")
	void Relocate(FVector position, FRotator rotation);

	void SetShouldSave(bool shouldSave);

	void SetId(int inId);
	int GetId() const { return id; }

protected:
	virtual void DoBuildOnScreenWidget() override;

	virtual void OnModeModified(EITwinCalloutMode InMode) override;
	virtual void OnColorThemeModified(EITwinCalloutColor InColor) override;
	virtual void OnVisibilityModified(bool bInVisible) override;
	virtual void OnNameModified(const FString& InName) override;
	virtual void OnTextModified(const FText& InText) override;
	virtual void OnFontSizeModified(int32 InFontSize) override;

private:
	int id = 0;

	mutable AdvViz::SDK::AnnotationPtr aVizAnnotationPtr = nullptr;
};
