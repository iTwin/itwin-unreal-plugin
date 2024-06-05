// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#if WITH_EDITOR

#include "ITwinCesiumFlyToComponent.h"
#include "ITwinCesiumGlobeAnchorComponent.h"
#include "ITwinCesiumTestHelpers.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ITwinGlobeAwareDefaultPawn.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"

BEGIN_DEFINE_SPEC(
    FGlobeAwareDefaultPawnSpec,
    "Cesium.Unit.GlobeAwareDefaultPawn",
    EAutomationTestFlags::ApplicationContextMask |
        EAutomationTestFlags::ProductFilter)

FDelegateHandle subscriptionPostPIEStarted;

END_DEFINE_SPEC(FGlobeAwareDefaultPawnSpec)

void FGlobeAwareDefaultPawnSpec::Define() {
  Describe(
      "should not spike altitude when very close to final destination",
      [this]() {
        LatentBeforeEach(
            EAsyncExecution::TaskGraphMainThread,
            [this](const FDoneDelegate& done) {
              UWorld* pWorld = FAutomationEditorCommonUtils::CreateNewMap();

              AITwinGlobeAwareDefaultPawn* pPawn =
                  pWorld->SpawnActor<AITwinGlobeAwareDefaultPawn>();
              UITwinCesiumFlyToComponent* pFlyTo =
                  Cast<UITwinCesiumFlyToComponent>(pPawn->AddComponentByClass(
                      UITwinCesiumFlyToComponent::StaticClass(),
                      false,
                      FTransform::Identity,
                      false));
              pFlyTo->RotationToUse =
                  ECesiumFlyToRotation::ControlRotationInEastSouthUp;

              subscriptionPostPIEStarted =
                  FEditorDelegates::PostPIEStarted.AddLambda(
                      [done](bool isSimulating) { done.Execute(); });
              FRequestPlaySessionParams params{};
              GEditor->RequestPlaySession(params);
            });
        BeforeEach(EAsyncExecution::TaskGraphMainThread, [this]() {
          FEditorDelegates::PostPIEStarted.Remove(subscriptionPostPIEStarted);
        });
        AfterEach(EAsyncExecution::TaskGraphMainThread, [this]() {
          GEditor->RequestEndPlayMap();
        });
        It("", [this]() {
          UWorld* pWorld = GEditor->PlayWorld;

          TActorIterator<AITwinGlobeAwareDefaultPawn> it(pWorld);
          AITwinGlobeAwareDefaultPawn* pPawn = *it;
          UITwinCesiumFlyToComponent* pFlyTo =
              pPawn->FindComponentByClass<UITwinCesiumFlyToComponent>();
          TestNotNull("pFlyTo", pFlyTo);
          pFlyTo->Duration = 5.0f;

          UITwinCesiumGlobeAnchorComponent* pGlobeAnchor =
              pPawn->FindComponentByClass<UITwinCesiumGlobeAnchorComponent>();
          TestNotNull("pGlobeAnchor", pGlobeAnchor);

          // Start flying somewhere else
          pFlyTo->FlyToLocationLongitudeLatitudeHeight(
              FVector(25.0, 10.0, 100.0),
              0.0,
              0.0,
              false);

          // Tick almost to the end
          Cast<UActorComponent>(pFlyTo)->TickComponent(
              4.9999f,
              ELevelTick::LEVELTICK_All,
              nullptr);

          // The height should be close to the final height.
          FVector llh = pGlobeAnchor->GetLongitudeLatitudeHeight();
          TestTrue(
              "Height is close to final height",
              FMath::IsNearlyEqual(llh.Z, 100.0, 10.0));

          pPawn->Destroy();
        });
      });
}

#endif
