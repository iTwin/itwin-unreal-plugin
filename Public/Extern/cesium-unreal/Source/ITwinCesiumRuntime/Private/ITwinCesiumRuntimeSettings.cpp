// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#include "ITwinCesiumRuntimeSettings.h"

UITwinCesiumRuntimeSettings::UITwinCesiumRuntimeSettings(
    const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer) {
  CategoryName = FName(TEXT("Plugins"));
}
