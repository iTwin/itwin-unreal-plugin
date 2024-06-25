// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#include "ITwinCesiumCameraManager.h"
#include "ITwinCesiumRuntime.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include <string>
#include <vector>

FName AITwinCesiumCameraManager::DEFAULT_CAMERAMANAGER_TAG =
    FName("DEFAULT_CAMERAMANAGER");

/*static*/ AITwinCesiumCameraManager* AITwinCesiumCameraManager::GetDefaultCameraManager(
    const UObject* WorldContextObject) {
  // A null world context means a null return value (no camera manager
  // available)
  if (WorldContextObject == nullptr)
    return nullptr;
  UWorld* world = WorldContextObject->GetWorld();
  // This method can be called by actors even when opening the content browser.
  if (!IsValid(world)) {
    return nullptr;
  }
  UE_LOG(
      LogITwinCesium,
      Verbose,
      TEXT("World name for GetDefaultCameraManager: %s"),
      *world->GetFullName());

  // Note: The actor iterator will be created with the
  // "EActorIteratorFlags::SkipPendingKill" flag,
  // meaning that we don't have to handle objects
  // that have been deleted. (This is the default,
  // but made explicit here)
  AITwinCesiumCameraManager* pCameraManager = nullptr;
  EActorIteratorFlags flags = EActorIteratorFlags::OnlyActiveLevels |
                              EActorIteratorFlags::SkipPendingKill;
  for (TActorIterator<AActor> actorIterator(
           world,
           AITwinCesiumCameraManager::StaticClass(),
           flags);
       actorIterator;
       ++actorIterator) {
    AActor* actor = *actorIterator;
    if (actor->GetLevel() == world->PersistentLevel &&
        actor->ActorHasTag(DEFAULT_CAMERAMANAGER_TAG)) {
      pCameraManager = Cast<AITwinCesiumCameraManager>(actor);
      break;
    }
  }

  if (!pCameraManager) {
    UE_LOG(
        LogITwinCesium,
        Verbose,
        TEXT("Creating default AITwinCesiumCameraManager for actor %s"),
        *WorldContextObject->GetName());
    // Spawn georeference in the persistent level
    FActorSpawnParameters spawnParameters;
    spawnParameters.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    spawnParameters.OverrideLevel = world->PersistentLevel;
    pCameraManager = world->SpawnActor<AITwinCesiumCameraManager>(spawnParameters);
    // Null check so the editor doesn't crash when it makes arbitrary calls to
    // this function without a valid world context object.
    if (pCameraManager) {
      pCameraManager->Tags.Add(DEFAULT_CAMERAMANAGER_TAG);
    }
  } else {
    UE_LOG(
        LogITwinCesium,
        Verbose,
        TEXT("Using existing AITwinCesiumCameraManager %s for actor %s"),
        *pCameraManager->GetName(),
        *WorldContextObject->GetName());
  }
  return pCameraManager;
}

AITwinCesiumCameraManager::AITwinCesiumCameraManager() : AActor() {
#if WITH_EDITOR
  this->SetIsSpatiallyLoaded(false);
#endif
}

bool AITwinCesiumCameraManager::ShouldTickIfViewportsOnly() const { return true; }

void AITwinCesiumCameraManager::Tick(float DeltaTime) { Super::Tick(DeltaTime); }

int32 AITwinCesiumCameraManager::AddCamera(UPARAM(ref) const FITwinCesiumCamera& camera) {
  int32 cameraId = this->_currentCameraId++;
  this->_cameras.Emplace(cameraId, camera);
  return cameraId;
}

bool AITwinCesiumCameraManager::RemoveCamera(int32 cameraId) {
  int32 numRemovedPairs = this->_cameras.Remove(cameraId);
  bool success = numRemovedPairs > 0;
  return success;
}

bool AITwinCesiumCameraManager::UpdateCamera(
    int32 cameraId,
    UPARAM(ref) const FITwinCesiumCamera& camera) {
  FITwinCesiumCamera* pCurrentCamera = this->_cameras.Find(cameraId);
  if (pCurrentCamera) {
    *pCurrentCamera = camera;
    return true;
  }

  return false;
}

const TMap<int32, FITwinCesiumCamera>& AITwinCesiumCameraManager::GetCameras() const {
  return this->_cameras;
}
