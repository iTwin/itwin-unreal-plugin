// Copyright 2020-2024 CesiumGS, Inc. and Contributors

#pragma once

#include <Subsystems/EngineSubsystem.h>

#include "CesiumCreditSystemBPLoader.generated.h"

UCLASS()
class UCesiumCreditSystemBPLoader : public UEngineSubsystem {
  GENERATED_BODY()

public:
  UCesiumCreditSystemBPLoader();

  UPROPERTY()
  TSoftObjectPtr<UObject> CesiumCreditSystemBP = TSoftObjectPtr<
      UObject>(FSoftObjectPath(TEXT(
      "Class'/ITwinForUnreal/CesiumCreditSystemBP.CesiumCreditSystemBP_C'")));
};
