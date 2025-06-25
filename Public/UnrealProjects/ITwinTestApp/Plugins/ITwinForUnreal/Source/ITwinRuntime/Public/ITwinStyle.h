/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinStyle.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#pragma once

#include <CoreMinimal.h>
#include <Templates/SharedPointer.h>

class FSlateStyleSet;
class ISlateStyle;

struct ITWINRUNTIME_API FITwinStyleArgs
{
	TOptional<FString> CustomContentDir = {};
	bool bShowAppIcon = true;
};

/** Style data for iTwin plugin */
class ITWINRUNTIME_API FITwinStyle
{
public:
	static void Initialize(FITwinStyleArgs const& CtorArgs = {});
	static bool ApplyToApplication();
	static void Shutdown();

	static bool IsCreated();
	static FName GetStyleSetName();

private:
	static TSharedPtr<FSlateStyleSet> StyleInstance;

	static TSharedRef<FSlateStyleSet> Create(FITwinStyleArgs const& CtorArgs = {});
};
