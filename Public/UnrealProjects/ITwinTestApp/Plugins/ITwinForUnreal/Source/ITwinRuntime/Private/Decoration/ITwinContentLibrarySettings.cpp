/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinContentLibrarySettings.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Decoration/ITwinContentLibrarySettings.h>

UITwinContentLibrarySettings::UITwinContentLibrarySettings(
    const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer) {
    CategoryName = FName(TEXT("Engine"));
}
