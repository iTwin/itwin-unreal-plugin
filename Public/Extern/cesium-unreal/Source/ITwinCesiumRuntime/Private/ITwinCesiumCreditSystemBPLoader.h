// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#pragma once

#include "ITwinCesiumCreditSystemBPLoader.generated.h"

UCLASS()
class UITwinCesiumCreditSystemBPLoader : public UObject {
  GENERATED_BODY()

public:
  UITwinCesiumCreditSystemBPLoader();

  UPROPERTY()
  TSoftObjectPtr<UObject> CesiumCreditSystemBP = TSoftObjectPtr<
      UObject>(FSoftObjectPath(TEXT(
      "Class'/ITwinForUnreal/ITwinCesiumCreditSystemBP.ITwinCesiumCreditSystemBP_C'")));
};
