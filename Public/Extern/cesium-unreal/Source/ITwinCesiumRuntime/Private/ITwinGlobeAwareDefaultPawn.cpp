// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#include "ITwinGlobeAwareDefaultPawn.h"
#include "Camera/CameraComponent.h"
#include "ITwinCesiumActors.h"
#include "ITwinCesiumCustomVersion.h"
#include "ITwinCesiumFlyToComponent.h"
#include "ITwinCesiumGeoreference.h"
#include "ITwinCesiumGlobeAnchorComponent.h"
#include "ITwinCesiumRuntime.h"
#include "ITwinCesiumTransforms.h"
#include "CesiumUtility/Math.h"
#include "ITwinCesiumWgs84Ellipsoid.h"
#include "Curves/CurveFloat.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"
#include "ITwinVecMath.h"
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_double3.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/rotate_vector.hpp>

#if WITH_EDITOR
#include "Editor.h"
#endif

AITwinGlobeAwareDefaultPawn::AITwinGlobeAwareDefaultPawn() : ADefaultPawn() {
  // Structure to hold one-time initialization
  struct FConstructorStatics {
    ConstructorHelpers::FObjectFinder<UCurveFloat> ProgressCurve;
    ConstructorHelpers::FObjectFinder<UCurveFloat> HeightPercentageCurve;
    ConstructorHelpers::FObjectFinder<UCurveFloat> MaximumHeightByDistanceCurve;
    FConstructorStatics()
        : ProgressCurve(TEXT(
              "/ITwinForUnreal/Curves/FlyTo/Curve_CesiumFlyToDefaultProgress_Float.Curve_CesiumFlyToDefaultProgress_Float")),
          HeightPercentageCurve(TEXT(
              "/ITwinForUnreal/Curves/FlyTo/Curve_CesiumFlyToDefaultHeightPercentage_Float.Curve_CesiumFlyToDefaultHeightPercentage_Float")),
          MaximumHeightByDistanceCurve(TEXT(
              "/ITwinForUnreal/Curves/FlyTo/Curve_CesiumFlyToDefaultMaximumHeightByDistance_Float.Curve_CesiumFlyToDefaultMaximumHeightByDistance_Float")) {
    }
  };
  static FConstructorStatics ConstructorStatics;

  this->FlyToProgressCurve_DEPRECATED = ConstructorStatics.ProgressCurve.Object;
  this->FlyToAltitudeProfileCurve_DEPRECATED =
      ConstructorStatics.HeightPercentageCurve.Object;
  this->FlyToMaximumAltitudeCurve_DEPRECATED =
      ConstructorStatics.MaximumHeightByDistanceCurve.Object;

#if WITH_EDITOR
  this->SetIsSpatiallyLoaded(false);
#endif
  this->GlobeAnchor =
      CreateDefaultSubobject<UITwinCesiumGlobeAnchorComponent>(TEXT("GlobeAnchor"));
}

void AITwinGlobeAwareDefaultPawn::MoveRight(float Val) {
  this->_moveAlongViewAxis(EAxis::Y, Val);
}

void AITwinGlobeAwareDefaultPawn::MoveForward(float Val) {
  this->_moveAlongViewAxis(EAxis::X, Val);
}

void AITwinGlobeAwareDefaultPawn::MoveUp_World(float Val) {
  if (Val == 0.0f) {
    return;
  }

  AITwinCesiumGeoreference* pGeoreference = this->GetGeoreference();
  if (!IsValid(pGeoreference)) {
    return;
  }

  FVector upEcef = UITwinCesiumWgs84Ellipsoid::GeodeticSurfaceNormal(
      this->GlobeAnchor->GetEarthCenteredEarthFixedPosition());
  FVector up =
      pGeoreference->TransformEarthCenteredEarthFixedDirectionToUnreal(upEcef);

  FTransform transform{};
  USceneComponent* pRootComponent = this->GetRootComponent();
  if (IsValid(pRootComponent)) {
    USceneComponent* pParent = pRootComponent->GetAttachParent();
    if (IsValid(pParent)) {
      transform = pParent->GetComponentToWorld();
    }
  }

  this->_moveAlongVector(transform.TransformVector(up), Val);
}

FRotator AITwinGlobeAwareDefaultPawn::GetViewRotation() const {
  if (!Controller) {
    return this->GetActorRotation();
  }

  AITwinCesiumGeoreference* pGeoreference = this->GetGeoreference();
  if (!pGeoreference) {
    return this->GetActorRotation();
  }

  // The control rotation is expressed in a left-handed East-South-Up (ESU)
  // coordinate system:
  // * Yaw: Clockwise from East: 0 is East, 90 degrees is
  // South, 180 degrees is West, 270 degrees is North.
  // * Pitch: Angle above level, Positive is looking up, negative is looking
  // down
  // * Roll: Rotation around the look direction. Positive is a barrel roll to
  // the right (clockwise).
  FRotator localRotation = Controller->GetControlRotation();

  FTransform transform{};
  USceneComponent* pRootComponent = this->GetRootComponent();
  if (IsValid(pRootComponent)) {
    USceneComponent* pParent = pRootComponent->GetAttachParent();
    if (IsValid(pParent)) {
      transform = pParent->GetComponentToWorld();
    }
  }

  // Transform the rotation in the ESU frame to the Unreal world frame.
  FVector globePosition =
      transform.InverseTransformPosition(this->GetPawnViewLocation());
  FMatrix esuAdjustmentMatrix =
      pGeoreference->ComputeEastSouthUpToUnrealTransformation(globePosition) *
      transform.ToMatrixNoScale();

  return FRotator(esuAdjustmentMatrix.ToQuat() * localRotation.Quaternion());
}

FRotator AITwinGlobeAwareDefaultPawn::GetBaseAimRotation() const {
  return this->GetViewRotation();
}

const FTransform&
AITwinGlobeAwareDefaultPawn::GetGlobeToUnrealWorldTransform() const {
  AActor* pParent = this->GetAttachParentActor();
  if (IsValid(pParent)) {
    return pParent->GetActorTransform();
  }
  return FTransform::Identity;
}

void AITwinGlobeAwareDefaultPawn::FlyToLocationECEF(
    const FVector& ECEFDestination,
    double YawAtDestination,
    double PitchAtDestination,
    bool CanInterruptByMoving) {
  UITwinCesiumFlyToComponent* FlyTo =
      this->FindComponentByClass<UITwinCesiumFlyToComponent>();
  if (!IsValid(FlyTo)) {
    UE_LOG(
        LogCesium,
        Warning,
        TEXT(
            "Cannot call deprecated FlyToLocationLongitudeLatitudeHeight because the GlobeAwareDefaultPawn does not have a CesiumFlyToComponent."))
    return;
  }

  // Make sure functions attached to the deprecated delegates will be called.
  FlyTo->OnFlightComplete.AddUniqueDynamic(
      this,
      &AITwinGlobeAwareDefaultPawn::_onFlightComplete);
  FlyTo->OnFlightInterrupted.AddUniqueDynamic(
      this,
      &AITwinGlobeAwareDefaultPawn::_onFlightInterrupted);

  FlyTo->FlyToLocationEarthCenteredEarthFixed(
      ECEFDestination,
      YawAtDestination,
      PitchAtDestination,
      CanInterruptByMoving);
}

void AITwinGlobeAwareDefaultPawn::FlyToLocationLongitudeLatitudeHeight(
    const FVector& LongitudeLatitudeHeightDestination,
    double YawAtDestination,
    double PitchAtDestination,
    bool CanInterruptByMoving) {
  UITwinCesiumFlyToComponent* FlyTo =
      this->FindComponentByClass<UITwinCesiumFlyToComponent>();
  if (!IsValid(FlyTo)) {
    UE_LOG(
        LogCesium,
        Warning,
        TEXT(
            "Cannot call deprecated FlyToLocationLongitudeLatitudeHeight because the GlobeAwareDefaultPawn does not have a CesiumFlyToComponent."))
    return;
  }

  // Make sure functions attached to the deprecated delegates will be called.
  FlyTo->OnFlightComplete.AddUniqueDynamic(
      this,
      &AITwinGlobeAwareDefaultPawn::_onFlightComplete);
  FlyTo->OnFlightInterrupted.AddUniqueDynamic(
      this,
      &AITwinGlobeAwareDefaultPawn::_onFlightInterrupted);

  FlyTo->FlyToLocationLongitudeLatitudeHeight(
      LongitudeLatitudeHeightDestination,
      YawAtDestination,
      PitchAtDestination,
      CanInterruptByMoving);
}

void AITwinGlobeAwareDefaultPawn::Serialize(FArchive& Ar) {
  Super::Serialize(Ar);

  Ar.UsingCustomVersion(FITwinCesiumCustomVersion::GUID);
}

void AITwinGlobeAwareDefaultPawn::PostLoad() {
  Super::PostLoad();

  // For backward compatibility, copy the value of the deprecated Georeference
  // property to its new home in the GlobeAnchor. It doesn't appear to be
  // possible to do this in Serialize:
  // https://udn.unrealengine.com/s/question/0D54z00007CAbHFCA1/backward-compatibile-serialization-for-uobject-pointers
  const int32 CesiumVersion =
      this->GetLinkerCustomVersion(FITwinCesiumCustomVersion::GUID);
  if (CesiumVersion < FITwinCesiumCustomVersion::GeoreferenceRefactoring) {
    if (this->Georeference_DEPRECATED != nullptr && this->GlobeAnchor &&
        this->GlobeAnchor->GetGeoreference() == nullptr) {
      this->GlobeAnchor->SetGeoreference(this->Georeference_DEPRECATED);
    }
  }

#if WITH_EDITOR
  if (CesiumVersion < FITwinCesiumCustomVersion::FlyToComponent &&
      !HasAnyFlags(RF_ClassDefaultObject)) {
    // If this is a Blueprint object, like DynamicPawn, its construction
    // scripts may not have been run yet at this point. Doing so might cause
    // a Fly To component to be added. So we force it to happen here so
    // that we don't end up adding a duplicate CesiumFlyToComponent.
    this->RerunConstructionScripts();

    UITwinCesiumFlyToComponent* FlyTo =
        this->FindComponentByClass<UITwinCesiumFlyToComponent>();
    if (!FlyTo) {
      FlyTo = Cast<UITwinCesiumFlyToComponent>(this->AddComponentByClass(
          UITwinCesiumFlyToComponent::StaticClass(),
          false,
          FTransform::Identity,
          false));
      FlyTo->SetFlags(RF_Transactional);
      this->AddInstanceComponent(FlyTo);

      UE_LOG(
          LogCesium,
          Warning,
          TEXT(
              "Added CesiumFlyToComponent to %s in order to preserve backward compatibility."),
          *this->GetName());
    }

    FlyTo->RotationToUse = ECesiumFlyToRotation::ControlRotationInEastSouthUp;
    FlyTo->ProgressCurve = this->FlyToProgressCurve_DEPRECATED;
    FlyTo->HeightPercentageCurve = this->FlyToAltitudeProfileCurve_DEPRECATED;
    FlyTo->MaximumHeightByDistanceCurve =
        this->FlyToMaximumAltitudeCurve_DEPRECATED;
    FlyTo->Duration = this->FlyToDuration_DEPRECATED;
  }
#endif
}

AITwinCesiumGeoreference* AITwinGlobeAwareDefaultPawn::GetGeoreference() const {
  if (!IsValid(this->GlobeAnchor)) {
    UE_LOG(
        LogCesium,
        Error,
        TEXT(
            "GlobeAwareDefaultPawn %s does not have a valid GlobeAnchorComponent."),
        *this->GetName());
    return nullptr;
  }

  AITwinCesiumGeoreference* pGeoreference = this->GlobeAnchor->ResolveGeoreference();
  if (!IsValid(pGeoreference)) {
    UE_LOG(
        LogCesium,
        Error,
        TEXT(
            "GlobeAwareDefaultPawn %s does not have a valid CesiumGeoreference."),
        *this->GetName());
    pGeoreference = nullptr;
  }

  return pGeoreference;
}

UCurveFloat* AITwinGlobeAwareDefaultPawn::GetFlyToProgressCurve_DEPRECATED() const {
  UITwinCesiumFlyToComponent* FlyTo =
      this->FindComponentByClass<UITwinCesiumFlyToComponent>();
  if (!IsValid(FlyTo))
    return nullptr;
  return FlyTo->ProgressCurve;
}

void AITwinGlobeAwareDefaultPawn::SetFlyToProgressCurve_DEPRECATED(
    UCurveFloat* NewValue) {
  UITwinCesiumFlyToComponent* FlyTo =
      this->FindComponentByClass<UITwinCesiumFlyToComponent>();
  if (!IsValid(FlyTo))
    return;
  FlyTo->ProgressCurve = NewValue;
}

UCurveFloat*
AITwinGlobeAwareDefaultPawn::GetFlyToAltitudeProfileCurve_DEPRECATED() const {
  UITwinCesiumFlyToComponent* FlyTo =
      this->FindComponentByClass<UITwinCesiumFlyToComponent>();
  if (!IsValid(FlyTo))
    return nullptr;
  return FlyTo->HeightPercentageCurve;
}

void AITwinGlobeAwareDefaultPawn::SetFlyToAltitudeProfileCurve_DEPRECATED(
    UCurveFloat* NewValue) {
  UITwinCesiumFlyToComponent* FlyTo =
      this->FindComponentByClass<UITwinCesiumFlyToComponent>();
  if (!IsValid(FlyTo))
    return;
  FlyTo->HeightPercentageCurve = NewValue;
}

UCurveFloat*
AITwinGlobeAwareDefaultPawn::GetFlyToMaximumAltitudeCurve_DEPRECATED() const {
  UITwinCesiumFlyToComponent* FlyTo =
      this->FindComponentByClass<UITwinCesiumFlyToComponent>();
  if (!IsValid(FlyTo))
    return nullptr;
  return FlyTo->MaximumHeightByDistanceCurve;
}

void AITwinGlobeAwareDefaultPawn::SetFlyToMaximumAltitudeCurve_DEPRECATED(
    UCurveFloat* NewValue) {
  UITwinCesiumFlyToComponent* FlyTo =
      this->FindComponentByClass<UITwinCesiumFlyToComponent>();
  if (!IsValid(FlyTo))
    return;
  FlyTo->MaximumHeightByDistanceCurve = NewValue;
}

float AITwinGlobeAwareDefaultPawn::GetFlyToDuration_DEPRECATED() const {
  UITwinCesiumFlyToComponent* FlyTo =
      this->FindComponentByClass<UITwinCesiumFlyToComponent>();
  if (!IsValid(FlyTo))
    return 0.0f;
  return FlyTo->Duration;
}

void AITwinGlobeAwareDefaultPawn::SetFlyToDuration_DEPRECATED(float NewValue) {
  UITwinCesiumFlyToComponent* FlyTo =
      this->FindComponentByClass<UITwinCesiumFlyToComponent>();
  if (!IsValid(FlyTo))
    return;
  FlyTo->Duration = NewValue;
}

void AITwinGlobeAwareDefaultPawn::_moveAlongViewAxis(EAxis::Type axis, double Val) {
  if (Val == 0.0) {
    return;
  }

  FRotator worldRotation = this->GetViewRotation();
  this->_moveAlongVector(
      FRotationMatrix(worldRotation).GetScaledAxis(axis),
      Val);
}

void AITwinGlobeAwareDefaultPawn::_moveAlongVector(
    const FVector& vector,
    double Val) {
  if (Val == 0.0) {
    return;
  }

  FRotator worldRotation = this->GetViewRotation();
  AddMovementInput(vector, Val);
}

void AITwinGlobeAwareDefaultPawn::_onFlightComplete() {
  this->OnFlightComplete_DEPRECATED.Broadcast();
}

void AITwinGlobeAwareDefaultPawn::_onFlightInterrupted() {
  this->OnFlightInterrupt_DEPRECATED.Broadcast();
}
