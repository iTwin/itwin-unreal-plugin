/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinIModelSettings.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "ITwinIModelSettings.h"

UITwinIModelSettings::UITwinIModelSettings(const FObjectInitializer& ObjectIniter) : Super(ObjectIniter)
{
    CategoryName = FName(TEXT("Engine"));
}
