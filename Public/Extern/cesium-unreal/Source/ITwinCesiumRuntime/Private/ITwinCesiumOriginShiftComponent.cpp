// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#include "ITwinCesiumOriginShiftComponent.h"
#include "ITwinCesiumGeoreference.h"
#include "ITwinCesiumGlobeAnchorComponent.h"
#include "ITwinCesiumSubLevelComponent.h"
#include "ITwinCesiumSubLevelSwitcherComponent.h"
#include "ITwinCesiumWgs84Ellipsoid.h"
#include "LevelInstance/LevelInstanceActor.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

EITwinCesiumOriginShiftMode UITwinCesiumOriginShiftComponent::GetMode() const {
  return this->Mode;
}

void UITwinCesiumOriginShiftComponent::SetMode(EITwinCesiumOriginShiftMode NewMode) {
  this->Mode = NewMode;
}

double UITwinCesiumOriginShiftComponent::GetDistance() const {
  return this->Distance;
}

void UITwinCesiumOriginShiftComponent::SetDistance(double NewDistance) {
  this->Distance = NewDistance;
}

UITwinCesiumOriginShiftComponent::UITwinCesiumOriginShiftComponent() {
  this->PrimaryComponentTick.bCanEverTick = true;
  this->PrimaryComponentTick.TickGroup = ETickingGroup::TG_PrePhysics;
  this->bAutoActivate = true;
}

namespace {
/**
 * @brief Clamping addition.
 *
 * Returns the sum of the given values, clamping the result to
 * the minimum/maximum value that can be represented as a 32 bit
 * signed integer.
 *
 * @param f The floating point value
 * @param i The integer value
 * @return The clamped result
 */
int32 clampedAdd(double f, int32 i) {
  int64 sum = static_cast<int64>(f) + static_cast<int64>(i);
  int64 min = static_cast<int64>(TNumericLimits<int32>::Min());
  int64 max = static_cast<int64>(TNumericLimits<int32>::Max());
  int64 clamped = FMath::Max(min, FMath::Min(max, sum));
  return static_cast<int32>(clamped);
}
} // namespace

void UITwinCesiumOriginShiftComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction) {
  Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

  if (TickType != ELevelTick::LEVELTICK_All)
    return;

  if (!this->IsActive() || this->Mode == EITwinCesiumOriginShiftMode::Disabled)
    return;

  UITwinCesiumGlobeAnchorComponent* GlobeAnchor = this->GetGlobeAnchor();
  if (!IsValid(GlobeAnchor))
    return;

  AITwinCesiumGeoreference* Georeference = GlobeAnchor->ResolveGeoreference();

  if (!IsValid(Georeference))
    return;

  UITwinCesiumSubLevelSwitcherComponent* Switcher =
      Georeference->GetSubLevelSwitcher();
  if (!Switcher)
    return;

  const TArray<TWeakObjectPtr<ALevelInstance>>& Sublevels =
      Switcher->GetRegisteredSubLevelsWeak();

  // If we don't have any known sub-levels, and aren't origin shifting outside
  // of sub-levels, then bail quickly to save ourselves a little work.
  if (Sublevels.IsEmpty() &&
      this->Mode == EITwinCesiumOriginShiftMode::SwitchSubLevelsOnly) {
    return;
  }

  FVector ActorEcef = GlobeAnchor->GetEarthCenteredEarthFixedPosition();

  ALevelInstance* ClosestActiveLevel = nullptr;
  double ClosestLevelDistance = std::numeric_limits<double>::max();

  for (int32 i = 0; i < Sublevels.Num(); ++i) {
    ALevelInstance* Current = Sublevels[i].Get();
    if (!IsValid(Current))
      continue;

    UITwinCesiumSubLevelComponent* SubLevelComponent =
        Current->FindComponentByClass<UITwinCesiumSubLevelComponent>();
    if (!IsValid(SubLevelComponent))
      continue;

    if (!SubLevelComponent->GetEnabled())
      continue;

    FVector LevelEcef =
        UITwinCesiumWgs84Ellipsoid::LongitudeLatitudeHeightToEarthCenteredEarthFixed(
            FVector(
                SubLevelComponent->GetOriginLongitude(),
                SubLevelComponent->GetOriginLatitude(),
                SubLevelComponent->GetOriginHeight()));

    double LevelDistance = FVector::Distance(LevelEcef, ActorEcef);
    if (LevelDistance < SubLevelComponent->GetLoadRadius() &&
        LevelDistance < ClosestLevelDistance) {
      ClosestActiveLevel = Current;
      ClosestLevelDistance = LevelDistance;
    }
  }

  Switcher->SetTargetSubLevel(ClosestActiveLevel);

  // Only shift the origin when we're outside of all sub-levels.
  bool doOriginShift =
      Switcher->GetTargetSubLevel() == nullptr &&
      Switcher->GetCurrentSubLevel() == nullptr &&
      this->Mode != EITwinCesiumOriginShiftMode::SwitchSubLevelsOnly;

  if (doOriginShift) {
    // We're between sub-levels, but we also only want to shift the origin when
    // the Actor has traveled more than Distance from the old origin.
    AActor* Actor = this->GetOwner();
    doOriginShift =
        IsValid(Actor) && Actor->GetActorLocation().SquaredLength() >
                              this->Distance * this->Distance;
  }

  if (doOriginShift) {
    if (this->Mode == EITwinCesiumOriginShiftMode::ChangeCesiumGeoreference) {
      Georeference->SetOriginEarthCenteredEarthFixed(ActorEcef);
    } else {
      check(false && "Missing EITwinCesiumOriginShiftMode implementation.")
    }
  }
}
