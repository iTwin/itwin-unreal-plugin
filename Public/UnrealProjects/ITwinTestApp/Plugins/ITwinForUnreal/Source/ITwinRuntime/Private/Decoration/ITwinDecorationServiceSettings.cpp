/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDecorationServiceSettings.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Decoration/ITwinDecorationServiceSettings.h>

UITwinDecorationServiceSettings::UITwinDecorationServiceSettings(
    const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer) {
    CategoryName = FName(TEXT("Engine"));
}
