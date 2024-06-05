// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#pragma once

#include "Components/ActorComponent.h"
#include "ITwinCesiumGlobeAnchoredActorComponent.generated.h"

class UITwinCesiumGlobeAnchorComponent;

UCLASS(ClassGroup = "Cesium", Abstract)
class ITWINCESIUMRUNTIME_API UITwinCesiumGlobeAnchoredActorComponent
    : public UActorComponent {
  GENERATED_BODY()

public:
  UFUNCTION(BlueprintGetter)
  UITwinCesiumGlobeAnchorComponent* GetGlobeAnchor();

protected:
  virtual void OnRegister() override;
  virtual void BeginPlay() override;

private:
  void ResolveGlobeAnchor();

  // The globe anchor attached to the same Actor as this component. Don't
  // save/load or copy this. It is set in BeginPlay and OnRegister.
  UPROPERTY(
      Category = "Cesium",
      BlueprintReadOnly,
      BlueprintGetter = GetGlobeAnchor,
      Transient,
      DuplicateTransient,
      TextExportTransient,
      Meta = (AllowPrivateAccess))
  UITwinCesiumGlobeAnchorComponent* GlobeAnchor;
};
