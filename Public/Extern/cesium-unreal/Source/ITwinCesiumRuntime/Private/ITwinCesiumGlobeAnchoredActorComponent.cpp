// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#include "ITwinCesiumGlobeAnchoredActorComponent.h"
#include "ITwinCesiumGlobeAnchorComponent.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

UITwinCesiumGlobeAnchorComponent*
UITwinCesiumGlobeAnchoredActorComponent::GetGlobeAnchor() {
  return this->GlobeAnchor;
}

void UITwinCesiumGlobeAnchoredActorComponent::OnRegister() {
  Super::OnRegister();
  this->ResolveGlobeAnchor();
}

void UITwinCesiumGlobeAnchoredActorComponent::BeginPlay() {
  Super::BeginPlay();
  this->ResolveGlobeAnchor();
}

void UITwinCesiumGlobeAnchoredActorComponent::ResolveGlobeAnchor() {
  this->GlobeAnchor = nullptr;

  AActor* Owner = this->GetOwner();
  if (!IsValid(Owner))
    return;

  this->GlobeAnchor =
      Owner->FindComponentByClass<UITwinCesiumGlobeAnchorComponent>();
  if (!IsValid(this->GlobeAnchor)) {
    // A globe anchor is missing and required, so add one.
    this->GlobeAnchor =
        Cast<UITwinCesiumGlobeAnchorComponent>(Owner->AddComponentByClass(
            UITwinCesiumGlobeAnchorComponent::StaticClass(),
            false,
            FTransform::Identity,
            false));
    Owner->AddInstanceComponent(this->GlobeAnchor);

    // Force the Editor to refresh to show the newly-added component
#if WITH_EDITOR
    Owner->Modify();
    if (Owner->IsSelectedInEditor()) {
      GEditor->SelectActor(Owner, true, true, true, true);
    }
#endif
  }
}
