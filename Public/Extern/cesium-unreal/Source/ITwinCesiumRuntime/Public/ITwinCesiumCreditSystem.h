// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#pragma once

#include "Components/WidgetComponent.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "UObject/Class.h"
#include "UObject/ConstructorHelpers.h"
#include <memory>
#include <string>
#include <unordered_map>

#if WITH_EDITOR
#include "IAssetViewport.h"
#include "UnrealEdMisc.h"
#endif

#include "ITwinCesiumCreditSystem.generated.h"

namespace CesiumUtility {
class CreditSystem;
}

/**
 * Manages credits / atttribution for Cesium data sources. These credits
 * are displayed by the corresponding Blueprints class
 * /ITwinForUnreal/CesiumCreditSystemBP.CesiumCreditSystemBP_C.
 */
UCLASS(Abstract)
class ITWINCESIUMRUNTIME_API AITwinCesiumCreditSystem : public AActor {
  GENERATED_BODY()

public:
  static AITwinCesiumCreditSystem*
  GetDefaultCreditSystem(const UObject* WorldContextObject);

  AITwinCesiumCreditSystem();

  virtual void BeginPlay() override;
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  virtual void OnConstruction(const FTransform& Transform) override;
  virtual void BeginDestroy() override;

  UPROPERTY(EditDefaultsOnly, Category = "Cesium")
  TSubclassOf<class UITwinScreenCreditsWidget> CreditsWidgetClass;

  /**
   * Whether the credit string has changed since last frame.
   */
  UPROPERTY(BlueprintReadOnly, Category = "Cesium")
  bool CreditsUpdated = false;

  UPROPERTY(BlueprintReadOnly, Transient, Category = "Cesium")
  class UITwinScreenCreditsWidget* CreditsWidget;

  // Called every frame
  virtual bool ShouldTickIfViewportsOnly() const override;
  virtual void Tick(float DeltaTime) override;

  const std::shared_ptr<CesiumUtility::CreditSystem>&
  GetExternalCreditSystem() const {
    return _pCreditSystem;
  }

  void updateCreditsViewport(bool recreateWidget);
  void removeCreditsFromViewports();

#if WITH_EDITOR
  void OnRedrawLevelEditingViewports(bool);
  void OnPreBeginPIE(bool bIsSimulating);
  void OnEndPIE();
  void OnCleanseEditor();
#endif

private:
  static UObject* CesiumCreditSystemBP;

  /**
   * A tag that is assigned to Credit Systems when they are created
   * as the "default" Credit System for a certain world.
   */
  static FName DEFAULT_CREDITSYSTEM_TAG;

  // the underlying cesium-native credit system that is managed by this actor.
  std::shared_ptr<CesiumUtility::CreditSystem> _pCreditSystem;

  size_t _lastCreditsCount;

  FString ConvertHtmlToRtf(std::string html);
  std::unordered_map<std::string, FString> _htmlToRtf;

#if WITH_EDITOR
  TWeakPtr<IAssetViewport> _pLastEditorViewport;
#endif
};
