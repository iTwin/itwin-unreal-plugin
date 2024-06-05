// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#pragma once

#include "ITwinCesiumCamera.h"
#include "Containers/Map.h"
#include "GameFramework/Actor.h"

#include "ITwinCesiumCameraManager.generated.h"

/**
 * @brief Manages custom {@link FITwinCesiumCamera}s for all
 * {@link Cesium3DTileset}s in the world.
 */
UCLASS()
class ITWINCESIUMRUNTIME_API AITwinCesiumCameraManager : public AActor {
  GENERATED_BODY()

public:
  /**
   * @brief Get the camera manager for this world.
   */
  UFUNCTION(
      BlueprintCallable,
      Category = "CesiumCameraManager",
      meta = (WorldContext = "WorldContextObject"))
  static AITwinCesiumCameraManager*
  GetDefaultCameraManager(const UObject* WorldContextObject);

  AITwinCesiumCameraManager();

  /**
   * @brief Register a new camera with the camera manager.
   *
   * @param Camera The current state for the new camera.
   * @return The generated ID for this camera. Use this ID to refer to the
   * camera in the future when calling UpdateCamera.
   */
  UFUNCTION(BlueprintCallable, Category = "Cesium")
  int32 AddCamera(UPARAM(ref) const FITwinCesiumCamera& Camera);

  /**
   * @brief Unregister an existing camera with the camera manager.
   *
   * @param CameraId The ID of the camera, as returned by AddCamera during
   * registration.
   * @return Whether the updating was successful. If false, the CameraId was
   * invalid.
   */
  UFUNCTION(BlueprintCallable, Category = "Cesium")
  bool RemoveCamera(int32 CameraId);

  /**
   * @brief Update the state of the specified camera.
   *
   * @param CameraId The ID of the camera, as returned by AddCamera during
   * registration.
   * @param Camera The new, updated state of the camera.
   * @return Whether the updating was successful. If false, the CameraId was
   * invalid.
   */
  UFUNCTION(BlueprintCallable, Category = "Cesium")
  bool UpdateCamera(int32 CameraId, UPARAM(ref) const FITwinCesiumCamera& Camera);

  /**
   * @brief Get a read-only map of the current camera IDs to cameras.
   */
  UFUNCTION(BlueprintCallable, Category = "Cesium")
  const TMap<int32, FITwinCesiumCamera>& GetCameras() const;

  virtual bool ShouldTickIfViewportsOnly() const override;

  virtual void Tick(float DeltaTime) override;

private:
  int32 _currentCameraId = 0;
  TMap<int32, FITwinCesiumCamera> _cameras;

  static FName DEFAULT_CAMERAMANAGER_TAG;
};
