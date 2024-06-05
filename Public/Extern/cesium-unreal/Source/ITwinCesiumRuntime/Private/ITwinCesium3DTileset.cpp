// Copyright 2020-2023 CesiumGS, Inc. and Contributors

#include "ITwinCesium3DTileset.h"
#include "Async/Async.h"
#include "Camera/CameraTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Cesium3DTilesSelection/IPrepareRendererResources.h"
#include "Cesium3DTilesSelection/Tile.h"
#include "Cesium3DTilesSelection/TilesetLoadFailureDetails.h"
#include "Cesium3DTilesSelection/TilesetOptions.h"
#include "ITwinCesium3DTilesetLoadFailureDetails.h"
#include "ITwinCesium3DTilesetRoot.h"
#include "ITwinCesiumActors.h"
#include "ITwinCesiumBoundingVolumeComponent.h"
#include "ITwinCesiumCamera.h"
#include "ITwinCesiumCameraManager.h"
#include "ITwinCesiumCommon.h"
#include "ITwinCesiumCustomVersion.h"
#include "CesiumGeospatial/GlobeTransforms.h"
#include "CesiumGltf/ImageCesium.h"
#include "CesiumGltf/Ktx2TranscodeTargets.h"
#include "ITwinCesiumGltfComponent.h"
#include "ITwinCesiumGltfPointsSceneProxyUpdater.h"
#include "ITwinCesiumGltfPrimitiveComponent.h"
#include "CesiumIonClient/Connection.h"
#include "ITwinCesiumLifetime.h"
#include "ITwinCesiumMeshBuildCallbacks.h"
#include "ITwinCesiumRasterOverlay.h"
#include "ITwinCesiumRuntime.h"
#include "ITwinCesiumRuntimeSettings.h"
#include "ITwinCesiumTextureUtility.h"
#include "ITwinCesiumTileExcluder.h"
#include "ITwinCesiumViewExtension.h"
#include "Components/SceneCaptureComponent2D.h"
#include "ITwinCreateGltfOptions.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "Math/UnrealMathUtility.h"
#include "PixelFormat.h"
#include "StereoRendering.h"
#include "ITwinVecMath.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <memory>
#include <spdlog/spdlog.h>

FITwinCesium3DTilesetLoadFailure OnCesium3DTilesetLoadFailure{};

#if WITH_EDITOR
#include "Editor.h"
#include "EditorViewportClient.h"
#include "FileHelpers.h"
#include "LevelEditorViewport.h"
#endif

// Avoid complaining about the deprecated metadata struct
PRAGMA_DISABLE_DEPRECATION_WARNINGS

// Sets default values
AITwinCesium3DTileset::AITwinCesium3DTileset()
    : AActor(),
      Georeference(nullptr),
      ResolvedGeoreference(nullptr),
      CreditSystem(nullptr),

      _pTileset(nullptr),

      _lastTilesRendered(0),
      _lastWorkerThreadTileLoadQueueLength(0),
      _lastMainThreadTileLoadQueueLength(0),

      _lastTilesVisited(0),
      _lastTilesCulled(0),
      _lastTilesOccluded(0),
      _lastTilesWaitingForOcclusionResults(0),
      _lastMaxDepthVisited(0),

      _captureMovieMode{false},
      _beforeMoviePreloadAncestors{PreloadAncestors},
      _beforeMoviePreloadSiblings{PreloadSiblings},
      _beforeMovieLoadingDescendantLimit{LoadingDescendantLimit},
      _beforeMovieUseLodTransitions{true},

      _tilesetsBeingDestroyed(0) {

  PrimaryActorTick.bCanEverTick = true;
  PrimaryActorTick.TickGroup = ETickingGroup::TG_PostUpdateWork;

#if WITH_EDITOR
  this->SetIsSpatiallyLoaded(false);
#endif

  this->SetActorEnableCollision(true);

  this->RootComponent =
      CreateDefaultSubobject<UITwinCesium3DTilesetRoot>(TEXT("Tileset"));
  this->Root = this->RootComponent;

  PlatformName = UGameplayStatics::GetPlatformName();

#if WITH_EDITOR
  bIsMac = PlatformName == TEXT("Mac");
#endif
}

AITwinCesium3DTileset::~AITwinCesium3DTileset() { this->DestroyTileset(); }
PRAGMA_ENABLE_DEPRECATION_WARNINGS

TSoftObjectPtr<AITwinCesiumGeoreference> AITwinCesium3DTileset::GetGeoreference() const {
  return this->Georeference;
}

void AITwinCesium3DTileset::SetMobility(EComponentMobility::Type NewMobility) {
  if (NewMobility != this->RootComponent->Mobility) {
    this->RootComponent->SetMobility(NewMobility);
    DestroyTileset();
  }
}

void AITwinCesium3DTileset::SetGeoreference(
    TSoftObjectPtr<AITwinCesiumGeoreference> NewGeoreference) {
  this->Georeference = NewGeoreference;
  this->InvalidateResolvedGeoreference();
  this->ResolveGeoreference();
}

AITwinCesiumGeoreference* AITwinCesium3DTileset::ResolveGeoreference() {
  if (IsValid(this->ResolvedGeoreference)) {
    return this->ResolvedGeoreference;
  }

  if (IsValid(this->Georeference.Get())) {
    this->ResolvedGeoreference = this->Georeference.Get();
  } else {
    this->ResolvedGeoreference =
        AITwinCesiumGeoreference::GetDefaultGeoreferenceForActor(this);
  }

  UITwinCesium3DTilesetRoot* pRoot = Cast<UITwinCesium3DTilesetRoot>(this->RootComponent);
  if (pRoot) {
    this->ResolvedGeoreference->OnGeoreferenceUpdated.AddUniqueDynamic(
        pRoot,
        &UITwinCesium3DTilesetRoot::HandleGeoreferenceUpdated);

    // Update existing tile positions, if any.
    pRoot->HandleGeoreferenceUpdated();
  }

  return this->ResolvedGeoreference;
}

void AITwinCesium3DTileset::InvalidateResolvedGeoreference() {
  if (IsValid(this->ResolvedGeoreference)) {
    this->ResolvedGeoreference->OnGeoreferenceUpdated.RemoveAll(
        this->RootComponent);
  }
  this->ResolvedGeoreference = nullptr;
}

TSoftObjectPtr<AITwinCesiumCreditSystem> AITwinCesium3DTileset::GetCreditSystem() const {
  return this->CreditSystem;
}

void AITwinCesium3DTileset::SetCreditSystem(
    TSoftObjectPtr<AITwinCesiumCreditSystem> NewCreditSystem) {
  this->CreditSystem = NewCreditSystem;
  this->InvalidateResolvedCreditSystem();
  this->ResolveCreditSystem();
}

AITwinCesiumCreditSystem* AITwinCesium3DTileset::ResolveCreditSystem() {
  if (IsValid(this->ResolvedCreditSystem)) {
    return this->ResolvedCreditSystem;
  }

  if (IsValid(this->CreditSystem.Get())) {
    this->ResolvedCreditSystem = this->CreditSystem.Get();
  } else {
    this->ResolvedCreditSystem =
        AITwinCesiumCreditSystem::GetDefaultCreditSystem(this);
  }

  // Refresh the tileset so it uses the new credit system.
  this->RefreshTileset();

  return this->ResolvedCreditSystem;
}

void AITwinCesium3DTileset::InvalidateResolvedCreditSystem() {
  this->ResolvedCreditSystem = nullptr;
  this->RefreshTileset();
}

TSoftObjectPtr<AITwinCesiumCameraManager>
AITwinCesium3DTileset::GetCameraManager() const {
  return this->CameraManager;
}

void AITwinCesium3DTileset::SetCameraManager(
    TSoftObjectPtr<AITwinCesiumCameraManager> NewCameraManager) {
  this->CameraManager = NewCameraManager;
  this->InvalidateResolvedCameraManager();
  this->ResolveCameraManager();
}

AITwinCesiumCameraManager* AITwinCesium3DTileset::ResolveCameraManager() {
  if (IsValid(this->ResolvedCameraManager)) {
    return this->ResolvedCameraManager;
  }

  if (IsValid(this->CameraManager.Get())) {
    this->ResolvedCameraManager = this->CameraManager.Get();
  } else {
    this->ResolvedCameraManager =
        AITwinCesiumCameraManager::GetDefaultCameraManager(this);
  }

  return this->ResolvedCameraManager;
}

void AITwinCesium3DTileset::InvalidateResolvedCameraManager() {
  this->ResolvedCameraManager = nullptr;
  this->RefreshTileset();
}

void AITwinCesium3DTileset::RefreshTileset() { this->DestroyTileset(); }

void AITwinCesium3DTileset::TroubleshootToken() {
  OnCesium3DTilesetIonTroubleshooting.Broadcast(this);
}

void AITwinCesium3DTileset::AddFocusViewportDelegate() {
#if WITH_EDITOR
  FEditorDelegates::OnFocusViewportOnActors.AddLambda(
      [this](const TArray<AActor*>& actors) {
        if (actors.Num() == 1 && actors[0] == this) {
          this->OnFocusEditorViewportOnThis();
        }
      });
#endif // WITH_EDITOR
}

void AITwinCesium3DTileset::PostInitProperties() {
  UE_LOG(
      LogCesium,
      Verbose,
      TEXT("Called PostInitProperties on actor %s"),
      *this->GetName());

  Super::PostInitProperties();

  AddFocusViewportDelegate();

  UITwinCesiumRuntimeSettings* pSettings =
      GetMutableDefault<UITwinCesiumRuntimeSettings>();
  if (pSettings) {
    CanEnableOcclusionCulling =
        pSettings->EnableExperimentalOcclusionCullingFeature;
#if WITH_EDITOR
    pSettings->OnSettingChanged().AddUObject(
        this,
        &AITwinCesium3DTileset::RuntimeSettingsChanged);
#endif
  }
}

void AITwinCesium3DTileset::SetUseLodTransitions(bool InUseLodTransitions) {
  if (InUseLodTransitions != this->UseLodTransitions) {
    this->UseLodTransitions = InUseLodTransitions;
    this->DestroyTileset();
  }
}

void AITwinCesium3DTileset::SetTilesetSource(ETilesetSource InSource) {
  if (InSource != this->TilesetSource) {
    this->DestroyTileset();
    this->TilesetSource = InSource;
  }
}

void AITwinCesium3DTileset::SetUrl(const FString& InUrl) {
  if (InUrl != this->Url) {
    if (this->TilesetSource == ETilesetSource::FromUrl) {
      this->DestroyTileset();
    }
    this->Url = InUrl;
  }
}

void AITwinCesium3DTileset::SetIonAssetID(int64 InAssetID) {
  if (InAssetID >= 0 && InAssetID != this->IonAssetID) {
    if (this->TilesetSource == ETilesetSource::FromCesiumIon) {
      this->DestroyTileset();
    }
    this->IonAssetID = InAssetID;
  }
}

void AITwinCesium3DTileset::SetIonAccessToken(const FString& InAccessToken) {
  if (this->IonAccessToken != InAccessToken) {
    if (this->TilesetSource == ETilesetSource::FromCesiumIon) {
      this->DestroyTileset();
    }
    this->IonAccessToken = InAccessToken;
  }
}

void AITwinCesium3DTileset::SetCesiumIonServer(UITwinCesiumIonServer* Server) {
  if (this->CesiumIonServer != Server) {
    if (this->TilesetSource == ETilesetSource::FromCesiumIon) {
      this->DestroyTileset();
    }
    this->CesiumIonServer = Server;
  }
}

void AITwinCesium3DTileset::SetMaximumScreenSpaceError(
    double InMaximumScreenSpaceError) {
  if (MaximumScreenSpaceError != InMaximumScreenSpaceError) {
    MaximumScreenSpaceError = InMaximumScreenSpaceError;
    FITwinCesiumGltfPointsSceneProxyUpdater::UpdateSettingsInProxies(this);
  }
}

bool AITwinCesium3DTileset::GetEnableOcclusionCulling() const {
  return GetDefault<UITwinCesiumRuntimeSettings>()
             ->EnableExperimentalOcclusionCullingFeature &&
         EnableOcclusionCulling;
}

void AITwinCesium3DTileset::SetEnableOcclusionCulling(bool bEnableOcclusionCulling) {
  if (this->EnableOcclusionCulling != bEnableOcclusionCulling) {
    this->EnableOcclusionCulling = bEnableOcclusionCulling;
    this->DestroyTileset();
  }
}

void AITwinCesium3DTileset::SetOcclusionPoolSize(int32 newOcclusionPoolSize) {
  if (this->OcclusionPoolSize != newOcclusionPoolSize) {
    this->OcclusionPoolSize = newOcclusionPoolSize;
    this->DestroyTileset();
  }
}

void AITwinCesium3DTileset::SetDelayRefinementForOcclusion(
    bool bDelayRefinementForOcclusion) {
  if (this->DelayRefinementForOcclusion != bDelayRefinementForOcclusion) {
    this->DelayRefinementForOcclusion = bDelayRefinementForOcclusion;
    this->DestroyTileset();
  }
}

void AITwinCesium3DTileset::SetCreatePhysicsMeshes(bool bCreatePhysicsMeshes) {
  if (this->CreatePhysicsMeshes != bCreatePhysicsMeshes) {
    this->CreatePhysicsMeshes = bCreatePhysicsMeshes;
    this->DestroyTileset();
  }
}

void AITwinCesium3DTileset::SetCreateNavCollision(bool bCreateNavCollision) {
  if (this->CreateNavCollision != bCreateNavCollision) {
    this->CreateNavCollision = bCreateNavCollision;
    this->DestroyTileset();
  }
}

void AITwinCesium3DTileset::SetAlwaysIncludeTangents(bool bAlwaysIncludeTangents) {
  if (this->AlwaysIncludeTangents != bAlwaysIncludeTangents) {
    this->AlwaysIncludeTangents = bAlwaysIncludeTangents;
    this->DestroyTileset();
  }
}

void AITwinCesium3DTileset::SetGenerateSmoothNormals(bool bGenerateSmoothNormals) {
  if (this->GenerateSmoothNormals != bGenerateSmoothNormals) {
    this->GenerateSmoothNormals = bGenerateSmoothNormals;
    this->DestroyTileset();
  }
}

void AITwinCesium3DTileset::SetEnableWaterMask(bool bEnableMask) {
  if (this->EnableWaterMask != bEnableMask) {
    this->EnableWaterMask = bEnableMask;
    this->DestroyTileset();
  }
}

void AITwinCesium3DTileset::SetIgnoreKhrMaterialsUnlit(
    bool bIgnoreKhrMaterialsUnlit) {
  if (this->IgnoreKhrMaterialsUnlit != bIgnoreKhrMaterialsUnlit) {
    this->IgnoreKhrMaterialsUnlit = bIgnoreKhrMaterialsUnlit;
    this->DestroyTileset();
  }
}

void AITwinCesium3DTileset::SetMaterial(UMaterialInterface* InMaterial) {
  if (this->Material != InMaterial) {
    this->Material = InMaterial;
    this->DestroyTileset();
  }
}

void AITwinCesium3DTileset::SetTranslucentMaterial(UMaterialInterface* InMaterial) {
  if (this->TranslucentMaterial != InMaterial) {
    this->TranslucentMaterial = InMaterial;
    this->DestroyTileset();
  }
}

void AITwinCesium3DTileset::SetWaterMaterial(UMaterialInterface* InMaterial) {
  if (this->WaterMaterial != InMaterial) {
    this->WaterMaterial = InMaterial;
    this->DestroyTileset();
  }
}

void AITwinCesium3DTileset::SetCustomDepthParameters(
    FITwinCustomDepthParameters InCustomDepthParameters) {
  if (this->CustomDepthParameters != InCustomDepthParameters) {
    this->CustomDepthParameters = InCustomDepthParameters;
    this->DestroyTileset();
  }
}

void AITwinCesium3DTileset::SetPointCloudShading(
    FITwinCesiumPointCloudShading InPointCloudShading) {
  if (PointCloudShading != InPointCloudShading) {
    PointCloudShading = InPointCloudShading;
    FITwinCesiumGltfPointsSceneProxyUpdater::UpdateSettingsInProxies(this);
  }
}

void AITwinCesium3DTileset::PlayMovieSequencer() {
  this->_beforeMoviePreloadAncestors = this->PreloadAncestors;
  this->_beforeMoviePreloadSiblings = this->PreloadSiblings;
  this->_beforeMovieLoadingDescendantLimit = this->LoadingDescendantLimit;
  this->_beforeMovieUseLodTransitions = this->UseLodTransitions;

  this->_captureMovieMode = true;
  this->PreloadAncestors = false;
  this->PreloadSiblings = false;
  this->LoadingDescendantLimit = 10000;
  this->UseLodTransitions = false;
}

void AITwinCesium3DTileset::StopMovieSequencer() {
  this->_captureMovieMode = false;
  this->PreloadAncestors = this->_beforeMoviePreloadAncestors;
  this->PreloadSiblings = this->_beforeMoviePreloadSiblings;
  this->LoadingDescendantLimit = this->_beforeMovieLoadingDescendantLimit;
  this->UseLodTransitions = this->_beforeMovieUseLodTransitions;
}

void AITwinCesium3DTileset::PauseMovieSequencer() { this->StopMovieSequencer(); }

#if WITH_EDITOR
void AITwinCesium3DTileset::OnFocusEditorViewportOnThis() {

  UE_LOG(
      LogCesium,
      Verbose,
      TEXT("Called OnFocusEditorViewportOnThis on actor %s"),
      *this->GetName());

  struct CalculateECEFCameraPosition {
    glm::dvec3 operator()(const CesiumGeometry::BoundingSphere& sphere) {
      const glm::dvec3& center = sphere.getCenter();
      glm::dmat4 ENU =
          CesiumGeospatial::GlobeTransforms::eastNorthUpToFixedFrame(center);
      glm::dvec3 offset =
          sphere.getRadius() *
          glm::normalize(
              glm::dvec3(ENU[0]) + glm::dvec3(ENU[1]) + glm::dvec3(ENU[2]));
      glm::dvec3 position = center + offset;
      return position;
    }

    glm::dvec3
    operator()(const CesiumGeometry::OrientedBoundingBox& orientedBoundingBox) {
      const glm::dvec3& center = orientedBoundingBox.getCenter();
      glm::dmat4 ENU =
          CesiumGeospatial::GlobeTransforms::eastNorthUpToFixedFrame(center);
      const glm::dmat3& halfAxes = orientedBoundingBox.getHalfAxes();
      glm::dvec3 offset =
          glm::length(halfAxes[0] + halfAxes[1] + halfAxes[2]) *
          glm::normalize(
              glm::dvec3(ENU[0]) + glm::dvec3(ENU[1]) + glm::dvec3(ENU[2]));
      glm::dvec3 position = center + offset;
      return position;
    }

    glm::dvec3
    operator()(const CesiumGeospatial::BoundingRegion& boundingRegion) {
      return (*this)(boundingRegion.getBoundingBox());
    }

    glm::dvec3
    operator()(const CesiumGeospatial::BoundingRegionWithLooseFittingHeights&
                   boundingRegionWithLooseFittingHeights) {
      return (*this)(boundingRegionWithLooseFittingHeights.getBoundingRegion()
                         .getBoundingBox());
    }

    glm::dvec3 operator()(const CesiumGeospatial::S2CellBoundingVolume& s2) {
      return (*this)(s2.computeBoundingRegion());
    }
  };

  const Cesium3DTilesSelection::Tile* pRootTile =
      this->_pTileset->getRootTile();
  if (!pRootTile) {
    return;
  }

  const Cesium3DTilesSelection::BoundingVolume& boundingVolume =
      pRootTile->getBoundingVolume();

  AITwinCesiumGeoreference* pGeoreference = this->ResolveGeoreference();

  // calculate unreal camera position
  glm::dvec3 ecefCameraPosition =
      std::visit(CalculateECEFCameraPosition{}, boundingVolume);
  FVector unrealCameraPosition =
      pGeoreference->TransformEarthCenteredEarthFixedPositionToUnreal(
          VecMath::createVector(ecefCameraPosition));

  // calculate unreal camera orientation
  glm::dvec3 ecefCenter =
      Cesium3DTilesSelection::getBoundingVolumeCenter(boundingVolume);
  FVector unrealCenter =
      pGeoreference->TransformEarthCenteredEarthFixedPositionToUnreal(
          VecMath::createVector(ecefCenter));
  FVector unrealCameraFront =
      (unrealCenter - unrealCameraPosition).GetSafeNormal();
  FVector unrealCameraRight =
      FVector::CrossProduct(FVector::ZAxisVector, unrealCameraFront)
          .GetSafeNormal();
  FVector unrealCameraUp =
      FVector::CrossProduct(unrealCameraFront, unrealCameraRight)
          .GetSafeNormal();
  FRotator cameraRotator = FMatrix(
                               unrealCameraFront,
                               unrealCameraRight,
                               unrealCameraUp,
                               FVector::ZeroVector)
                               .Rotator();

  // Update all viewports.
  for (FLevelEditorViewportClient* LinkedViewportClient :
       GEditor->GetLevelViewportClients()) {
    // Dont move camera attach to an actor
    if (!LinkedViewportClient->IsAnyActorLocked()) {
      FViewportCameraTransform& ViewTransform =
          LinkedViewportClient->GetViewTransform();
      LinkedViewportClient->SetViewRotation(cameraRotator);
      LinkedViewportClient->SetViewLocation(unrealCameraPosition);
      LinkedViewportClient->Invalidate();
    }
  }
}
#endif

const glm::dmat4&
AITwinCesium3DTileset::GetCesiumTilesetToUnrealRelativeWorldTransform() const {
  return Cast<UITwinCesium3DTilesetRoot>(this->RootComponent)
      ->GetCesiumTilesetToUnrealRelativeWorldTransform();
}

void AITwinCesium3DTileset::UpdateTransformFromCesium() {

  const glm::dmat4& CesiumToUnreal =
      this->GetCesiumTilesetToUnrealRelativeWorldTransform();
  TArray<UITwinCesiumGltfComponent*> gltfComponents;
  this->GetComponents<UITwinCesiumGltfComponent>(gltfComponents);

  for (UITwinCesiumGltfComponent* pGltf : gltfComponents) {
    pGltf->UpdateTransformFromCesium(CesiumToUnreal);
  }

  if (this->BoundingVolumePoolComponent) {
    this->BoundingVolumePoolComponent->UpdateTransformFromCesium(
        CesiumToUnreal);
  }
}

// Called when the game starts or when spawned
void AITwinCesium3DTileset::BeginPlay() {
  Super::BeginPlay();

  this->ResolveGeoreference();
  this->ResolveCameraManager();
  this->ResolveCreditSystem();

  this->LoadTileset();

  // Search for level sequence.
  for (auto sequenceActorIt = TActorIterator<ALevelSequenceActor>(GetWorld());
       sequenceActorIt;
       ++sequenceActorIt) {
    ALevelSequenceActor* sequenceActor = *sequenceActorIt;

    if (!IsValid(sequenceActor->GetSequencePlayer())) {
      continue;
    }

    FScriptDelegate playMovieSequencerDelegate;
    playMovieSequencerDelegate.BindUFunction(this, FName("PlayMovieSequencer"));
    sequenceActor->GetSequencePlayer()->OnPlay.Add(playMovieSequencerDelegate);

    FScriptDelegate stopMovieSequencerDelegate;
    stopMovieSequencerDelegate.BindUFunction(this, FName("StopMovieSequencer"));
    sequenceActor->GetSequencePlayer()->OnStop.Add(stopMovieSequencerDelegate);

    FScriptDelegate pauseMovieSequencerDelegate;
    pauseMovieSequencerDelegate.BindUFunction(
        this,
        FName("PauseMovieSequencer"));
    sequenceActor->GetSequencePlayer()->OnPause.Add(
        pauseMovieSequencerDelegate);
  }
}

void AITwinCesium3DTileset::OnConstruction(const FTransform& Transform) {
  this->ResolveGeoreference();
  this->ResolveCameraManager();
  this->ResolveCreditSystem();

  this->LoadTileset();

  // Hide all existing tiles. The still-visible ones will be shown next time we
  // tick. But if update is suspended, leave the components in their current
  // state.
  if (!this->SuspendUpdate) {
    TArray<UITwinCesiumGltfComponent*> gltfComponents;
    this->GetComponents<UITwinCesiumGltfComponent>(gltfComponents);

    for (UITwinCesiumGltfComponent* pGltf : gltfComponents) {
      if (pGltf && IsValid(pGltf) && pGltf->IsVisible()) {
        pGltf->SetVisibility(false, true);
        pGltf->SetCollisionEnabled(ECollisionEnabled::NoCollision);
      }
    }
  }
}

void AITwinCesium3DTileset::NotifyHit(
    UPrimitiveComponent* MyComp,
    AActor* Other,
    UPrimitiveComponent* OtherComp,
    bool bSelfMoved,
    FVector HitLocation,
    FVector HitNormal,
    FVector NormalImpulse,
    const FHitResult& Hit) {
  // std::cout << "Hit face index: " << Hit.FaceIndex << std::endl;

  // FHitResult detailedHit;
  // FCollisionQueryParams params;
  // params.bReturnFaceIndex = true;
  // params.bTraceComplex = true;
  // MyComp->LineTraceComponent(detailedHit, Hit.TraceStart, Hit.TraceEnd,
  // params);

  // std::cout << "Hit face index 2: " << detailedHit.FaceIndex << std::endl;
}

class UnrealResourcePreparer
    : public Cesium3DTilesSelection::IPrepareRendererResources {
public:
  UnrealResourcePreparer(AITwinCesium3DTileset* pActor) : _pActor(pActor) {}

  virtual CesiumAsync::Future<
      Cesium3DTilesSelection::TileLoadResultAndRenderResources>
  prepareInLoadThread(
      const CesiumAsync::AsyncSystem& asyncSystem,
      Cesium3DTilesSelection::TileLoadResult&& tileLoadResult,
      const glm::dmat4& transform,
      const std::any& rendererOptions) override {
    CesiumGltf::Model* pModel =
        std::get_if<CesiumGltf::Model>(&tileLoadResult.contentKind);
    if (!pModel)
      return asyncSystem.createResolvedFuture(
          Cesium3DTilesSelection::TileLoadResultAndRenderResources{
              std::move(tileLoadResult),
              nullptr});

    CreateGltfOptions::CreateModelOptions options;
    options.pModel = pModel;
    options.alwaysIncludeTangents = this->_pActor->GetAlwaysIncludeTangents();
    options.createPhysicsMeshes = this->_pActor->GetCreatePhysicsMeshes();

    options.ignoreKhrMaterialsUnlit =
        this->_pActor->GetIgnoreKhrMaterialsUnlit();

    if (this->_pActor->_featuresMetadataDescription) {
      options.pFeaturesMetadataDescription =
          &(*this->_pActor->_featuresMetadataDescription);
    } else if (this->_pActor->_metadataDescription_DEPRECATED) {
      options.pEncodedMetadataDescription_DEPRECATED =
          &(*this->_pActor->_metadataDescription_DEPRECATED);
    }

    // propagate mesh construction callback, if any
    options.MeshBuildCallbacks = this->_pActor->GetMeshBuildCallbacks();

    TUniquePtr<UITwinCesiumGltfComponent::HalfConstructed> pHalf =
        UITwinCesiumGltfComponent::CreateOffGameThread(transform, options);
    return asyncSystem.createResolvedFuture(
        Cesium3DTilesSelection::TileLoadResultAndRenderResources{
            std::move(tileLoadResult),
            pHalf.Release()});
  }

  virtual void* prepareInMainThread(
      Cesium3DTilesSelection::Tile& tile,
      void* pLoadThreadResult) override {
    const Cesium3DTilesSelection::TileContent& content = tile.getContent();
    if (content.isRenderContent()) {
      TUniquePtr<UITwinCesiumGltfComponent::HalfConstructed> pHalf(
          reinterpret_cast<UITwinCesiumGltfComponent::HalfConstructed*>(
              pLoadThreadResult));
      const Cesium3DTilesSelection::TileRenderContent& renderContent =
          *content.getRenderContent();
      return UITwinCesiumGltfComponent::CreateOnGameThread(
          renderContent.getModel(),
          this->_pActor,
          std::move(pHalf),
          _pActor->GetCesiumTilesetToUnrealRelativeWorldTransform(),
          this->_pActor->GetMaterial(),
          this->_pActor->GetTranslucentMaterial(),
          this->_pActor->GetWaterMaterial(),
          this->_pActor->GetCustomDepthParameters(),
          tile,
          this->_pActor->GetCreateNavCollision());
    }
    // UE_LOG(LogCesium, VeryVerbose, TEXT("No content for tile"));
    return nullptr;
  }

  virtual void free(
      Cesium3DTilesSelection::Tile& tile,
      void* pLoadThreadResult,
      void* pMainThreadResult) noexcept override {
    if (pLoadThreadResult) {
      UITwinCesiumGltfComponent::HalfConstructed* pHalf =
          reinterpret_cast<UITwinCesiumGltfComponent::HalfConstructed*>(
              pLoadThreadResult);
      delete pHalf;
    } else if (pMainThreadResult) {
      UITwinCesiumGltfComponent* pGltf =
          reinterpret_cast<UITwinCesiumGltfComponent*>(pMainThreadResult);
      CesiumLifetime::destroyComponentRecursively(pGltf);
    }
  }

  virtual void* prepareRasterInLoadThread(
      CesiumGltf::ImageCesium& image,
      const std::any& rendererOptions) override {
    auto ppOptions =
        std::any_cast<FRasterOverlayRendererOptions*>(&rendererOptions);
    check(ppOptions != nullptr && *ppOptions != nullptr);
    if (ppOptions == nullptr || *ppOptions == nullptr) {
      return nullptr;
    }

    auto pOptions = *ppOptions;

    auto texture = CesiumTextureUtility::loadTextureAnyThreadPart(
        CesiumTextureUtility::GltfImagePtr{&image},
        TextureAddress::TA_Clamp,
        TextureAddress::TA_Clamp,
        pOptions->filter,
        pOptions->group,
        pOptions->useMipmaps,
        true); // TODO: sRGB should probably be configurable on the raster
               // overlay
    return texture.Release();
  }

  virtual void* prepareRasterInMainThread(
      CesiumRasterOverlays::RasterOverlayTile& rasterTile,
      void* pLoadThreadResult) override {

    TUniquePtr<CesiumTextureUtility::LoadedTextureResult> pLoadedTexture{
        static_cast<CesiumTextureUtility::LoadedTextureResult*>(
            pLoadThreadResult)};

    if (!pLoadedTexture) {
      return nullptr;
    }

    // The image source pointer during loading may have been invalidated,
    // so replace it.
    CesiumTextureUtility::GltfImagePtr* pImageSource =
        std::get_if<CesiumTextureUtility::GltfImagePtr>(
            &pLoadedTexture->textureSource);
    if (pImageSource) {
      pImageSource->pImage = &rasterTile.getImage();
    }

    UTexture2D* pTexture =
        CesiumTextureUtility::loadTextureGameThreadPart(pLoadedTexture.Get());
    if (!pTexture) {
      return nullptr;
    }

    pTexture->AddToRoot();
    return pTexture;
  }

  virtual void freeRaster(
      const CesiumRasterOverlays::RasterOverlayTile& rasterTile,
      void* pLoadThreadResult,
      void* pMainThreadResult) noexcept override {
    if (pLoadThreadResult) {
      CesiumTextureUtility::LoadedTextureResult* pLoadedTexture =
          static_cast<CesiumTextureUtility::LoadedTextureResult*>(
              pLoadThreadResult);
      CesiumTextureUtility::destroyHalfLoadedTexture(*pLoadedTexture);
      delete pLoadedTexture;
    }

    if (pMainThreadResult) {
      UTexture* pTexture = static_cast<UTexture*>(pMainThreadResult);
      pTexture->RemoveFromRoot();
      CesiumTextureUtility::destroyTexture(pTexture);
    }
  }

  virtual void attachRasterInMainThread(
      const Cesium3DTilesSelection::Tile& tile,
      int32_t overlayTextureCoordinateID,
      const CesiumRasterOverlays::RasterOverlayTile& rasterTile,
      void* pMainThreadRendererResources,
      const glm::dvec2& translation,
      const glm::dvec2& scale) override {
    const Cesium3DTilesSelection::TileContent& content = tile.getContent();
    const Cesium3DTilesSelection::TileRenderContent* pRenderContent =
        content.getRenderContent();
    if (pRenderContent) {
      UITwinCesiumGltfComponent* pGltfContent =
          reinterpret_cast<UITwinCesiumGltfComponent*>(
              pRenderContent->getRenderResources());
      if (pGltfContent) {
        pGltfContent->AttachRasterTile(
            tile,
            rasterTile,
            static_cast<UTexture2D*>(pMainThreadRendererResources),
            translation,
            scale,
            overlayTextureCoordinateID);
      }
    }
  }

  virtual void detachRasterInMainThread(
      const Cesium3DTilesSelection::Tile& tile,
      int32_t overlayTextureCoordinateID,
      const CesiumRasterOverlays::RasterOverlayTile& rasterTile,
      void* pMainThreadRendererResources) noexcept override {
    const Cesium3DTilesSelection::TileContent& content = tile.getContent();
    const Cesium3DTilesSelection::TileRenderContent* pRenderContent =
        content.getRenderContent();
    if (pRenderContent) {
      UITwinCesiumGltfComponent* pGltfContent =
          reinterpret_cast<UITwinCesiumGltfComponent*>(
              pRenderContent->getRenderResources());
      if (pGltfContent) {
        pGltfContent->DetachRasterTile(
            tile,
            rasterTile,
            static_cast<UTexture2D*>(pMainThreadRendererResources));
      }
    }
  }

private:
  AITwinCesium3DTileset* _pActor;
};

void AITwinCesium3DTileset::UpdateLoadStatus() {
  TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::UpdateLoadStatus)

  float nativeLoadProgress = this->_pTileset->computeLoadProgress();

  // If native tileset still loading, just copy its progress
  if (nativeLoadProgress < 100) {
    this->LoadProgress = nativeLoadProgress;
    return;
  }

  // Native tileset is 100% loaded, but there might be a few frames where
  // nothing needs to be loaded as we are waiting for occlusion results to come
  // back, which means we are not done with loading all the tiles in the tileset
  // yet. Interpret this as 99% (almost) done
  if (this->_lastTilesWaitingForOcclusionResults > 0) {
    this->LoadProgress = 99;
    return;
  }

  // If we have tiles to hide next frame, we haven't completely finished loading
  // yet. We need to tick once more. We're really close to done.
  if (!this->_tilesToHideNextFrame.empty()) {
    this->LoadProgress = glm::min(this->LoadProgress, 99.9999f);
    return;
  }

  // We can now report 100 percent loaded
  float lastLoadProgress = this->LoadProgress;
  this->LoadProgress = 100;

  // Only broadcast the update when we first hit 100%, not everytime
  if (lastLoadProgress != LoadProgress) {
    TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::BroadcastOnTilesetLoaded)

    // Tileset just finished loading, we broadcast the update
    UE_LOG(LogCesium, Verbose, TEXT("Broadcasting OnTileLoaded"));
    OnTilesetLoaded.Broadcast();
  }
}

namespace {

const TSharedRef<CesiumViewExtension, ESPMode::ThreadSafe>&
getCesiumViewExtension() {
  static TSharedRef<CesiumViewExtension, ESPMode::ThreadSafe>
      cesiumViewExtension =
          GEngine->ViewExtensions->NewExtension<CesiumViewExtension>();
  return cesiumViewExtension;
}

} // namespace

void AITwinCesium3DTileset::LoadTileset() {
  TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::LoadTileset)

  if (this->_pTileset) {
    // Tileset already loaded, do nothing.
    return;
  }

  UWorld* pWorld = this->GetWorld();
  if (!pWorld) {
    return;
  }

  AWorldSettings* pWorldSettings = pWorld->GetWorldSettings();
  if (pWorldSettings && pWorldSettings->bEnableWorldBoundsChecks) {
    UE_LOG(
        LogCesium,
        Warning,
        TEXT(
            "\"Enable World Bounds Checks\" in the world settings is currently enabled. Please consider disabling it to avoid potential issues."),
        *this->Url);
  }

  // Make sure we have a valid Cesium ion server if we need one.
  if (this->TilesetSource == ETilesetSource::FromCesiumIon &&
      !IsValid(this->CesiumIonServer)) {
    this->Modify();
    this->CesiumIonServer = UITwinCesiumIonServer::GetServerForNewObjects();
  }

  const TSharedRef<CesiumViewExtension, ESPMode::ThreadSafe>&
      cesiumViewExtension = getCesiumViewExtension();
  const std::shared_ptr<CesiumAsync::IAssetAccessor>& pAssetAccessor =
      getAssetAccessor();
  const CesiumAsync::AsyncSystem& asyncSystem = getAsyncSystem();

  // Both the feature flag and the CesiumViewExtension are global, not owned by
  // the Tileset. We're just applying one to the other here out of convenience.
  cesiumViewExtension->SetEnabled(
      GetDefault<UITwinCesiumRuntimeSettings>()
          ->EnableExperimentalOcclusionCullingFeature);

  TArray<UITwinCesiumRasterOverlay*> rasterOverlays;
  this->GetComponents<UITwinCesiumRasterOverlay>(rasterOverlays);

  TArray<UITwinCesiumTileExcluder*> tileExcluders;
  this->GetComponents<UITwinCesiumTileExcluder>(tileExcluders);

  const UITwinCesiumFeaturesMetadataComponent* pFeaturesMetadataComponent =
      this->FindComponentByClass<UITwinCesiumFeaturesMetadataComponent>();

  // Check if this component exists for backwards compatibility.
  PRAGMA_DISABLE_DEPRECATION_WARNINGS

  const UDEPRECATED_CesiumEncodedMetadataComponent* pEncodedMetadataComponent =
      this->FindComponentByClass<UDEPRECATED_CesiumEncodedMetadataComponent>();

  this->_featuresMetadataDescription = std::nullopt;
  this->_metadataDescription_DEPRECATED = std::nullopt;

  if (pFeaturesMetadataComponent) {
    FITwinCesiumFeaturesMetadataDescription& description =
        this->_featuresMetadataDescription.emplace();
    description.Features = {pFeaturesMetadataComponent->FeatureIdSets};
    description.PrimitiveMetadata = {
        pFeaturesMetadataComponent->PropertyTextureNames};
    description.ModelMetadata = {
        pFeaturesMetadataComponent->PropertyTables,
        pFeaturesMetadataComponent->PropertyTextures};
  } else if (pEncodedMetadataComponent) {
    UE_LOG(
        LogCesium,
        Warning,
        TEXT(
            "CesiumEncodedMetadataComponent is deprecated. Use CesiumFeaturesMetadataComponent instead."));
    this->_metadataDescription_DEPRECATED = {
        pEncodedMetadataComponent->FeatureTables,
        pEncodedMetadataComponent->FeatureTextures};
  }

  PRAGMA_ENABLE_DEPRECATION_WARNINGS

  this->_cesiumViewExtension = cesiumViewExtension;

  if (GetDefault<UITwinCesiumRuntimeSettings>()
          ->EnableExperimentalOcclusionCullingFeature &&
      this->EnableOcclusionCulling && !this->BoundingVolumePoolComponent) {
    const glm::dmat4& cesiumToUnreal =
        GetCesiumTilesetToUnrealRelativeWorldTransform();
    this->BoundingVolumePoolComponent =
        NewObject<UITwinCesiumBoundingVolumePoolComponent>(this);
    this->BoundingVolumePoolComponent->SetFlags(
        RF_Transient | RF_DuplicateTransient | RF_TextExportTransient);
    this->BoundingVolumePoolComponent->RegisterComponent();
    this->BoundingVolumePoolComponent->UpdateTransformFromCesium(
        cesiumToUnreal);
  }

  if (this->BoundingVolumePoolComponent) {
    this->BoundingVolumePoolComponent->initPool(this->OcclusionPoolSize);
  }

  AITwinCesiumCreditSystem* pCreditSystem = this->ResolvedCreditSystem;

  Cesium3DTilesSelection::TilesetExternals externals{
      pAssetAccessor,
      std::make_shared<UnrealResourcePreparer>(this),
      asyncSystem,
      pCreditSystem ? pCreditSystem->GetExternalCreditSystem() : nullptr,
      spdlog::default_logger(),
      (GetDefault<UITwinCesiumRuntimeSettings>()
           ->EnableExperimentalOcclusionCullingFeature &&
       this->EnableOcclusionCulling && this->BoundingVolumePoolComponent)
          ? this->BoundingVolumePoolComponent->getPool()
          : nullptr,
      _gltfTuner};

  this->_startTime = std::chrono::high_resolution_clock::now();

  this->LoadProgress = 0;

  Cesium3DTilesSelection::TilesetOptions options;

  options.enableOcclusionCulling =
      GetDefault<UITwinCesiumRuntimeSettings>()
          ->EnableExperimentalOcclusionCullingFeature &&
      this->EnableOcclusionCulling;
  options.delayRefinementForOcclusion = this->DelayRefinementForOcclusion;

  options.showCreditsOnScreen = ShowCreditsOnScreen;

  options.loadErrorCallback =
      [this](const Cesium3DTilesSelection::TilesetLoadFailureDetails& details) {
        static_assert(
            uint8_t(ECesium3DTilesetLoadType::CesiumIon) ==
            uint8_t(Cesium3DTilesSelection::TilesetLoadType::CesiumIon));
        static_assert(
            uint8_t(ECesium3DTilesetLoadType::TilesetJson) ==
            uint8_t(Cesium3DTilesSelection::TilesetLoadType::TilesetJson));
        static_assert(
            uint8_t(ECesium3DTilesetLoadType::Unknown) ==
            uint8_t(Cesium3DTilesSelection::TilesetLoadType::Unknown));

        uint8_t typeValue = uint8_t(details.type);
        assert(
            uint8_t(details.type) <=
            uint8_t(Cesium3DTilesSelection::TilesetLoadType::TilesetJson));
        assert(this->_pTileset == details.pTileset);

        FITwinCesium3DTilesetLoadFailureDetails ueDetails{};
        ueDetails.Tileset = this;
        ueDetails.Type = ECesium3DTilesetLoadType(typeValue);
        ueDetails.HttpStatusCode = details.statusCode;
        ueDetails.Message = UTF8_TO_TCHAR(details.message.c_str());

        // Broadcast the event from the game thread.
        // Even if we're already in the game thread, let the stack unwind.
        // Otherwise actions that destroy the Tileset will cause a deadlock.
        AsyncTask(
            ENamedThreads::GameThread,
            [ueDetails = std::move(ueDetails)]() {
              OnCesium3DTilesetLoadFailure.Broadcast(ueDetails);
            });
      };

  // Generous per-frame time limits for loading / unloading on main thread.
  options.mainThreadLoadingTimeLimit = 5.0;
  options.tileCacheUnloadTimeLimit = 5.0;

  options.contentOptions.generateMissingNormalsSmooth =
      this->GenerateSmoothNormals;

  // TODO: figure out why water material crashes mac
#if PLATFORM_MAC
#else
  options.contentOptions.enableWaterMask = this->EnableWaterMask;
#endif

  CesiumGltf::SupportedGpuCompressedPixelFormats supportedFormats;
  supportedFormats.ETC1_RGB = GPixelFormats[EPixelFormat::PF_ETC1].Supported;
  supportedFormats.ETC2_RGBA =
      GPixelFormats[EPixelFormat::PF_ETC2_RGBA].Supported;
  supportedFormats.BC1_RGB = GPixelFormats[EPixelFormat::PF_DXT1].Supported;
  supportedFormats.BC3_RGBA = GPixelFormats[EPixelFormat::PF_DXT5].Supported;
  supportedFormats.BC4_R = GPixelFormats[EPixelFormat::PF_BC4].Supported;
  supportedFormats.BC5_RG = GPixelFormats[EPixelFormat::PF_BC5].Supported;
  supportedFormats.BC7_RGBA = GPixelFormats[EPixelFormat::PF_BC7].Supported;
  supportedFormats.ASTC_4x4_RGBA =
      GPixelFormats[EPixelFormat::PF_ASTC_4x4].Supported;
  supportedFormats.PVRTC2_4_RGBA =
      GPixelFormats[EPixelFormat::PF_PVRTC2].Supported;
  supportedFormats.ETC2_EAC_R11 =
      GPixelFormats[EPixelFormat::PF_ETC2_R11_EAC].Supported;
  supportedFormats.ETC2_EAC_RG11 =
      GPixelFormats[EPixelFormat::PF_ETC2_RG11_EAC].Supported;

  options.contentOptions.ktx2TranscodeTargets =
      CesiumGltf::Ktx2TranscodeTargets(supportedFormats, false);

  options.contentOptions.applyTextureTransform = false;

  switch (this->TilesetSource) {
  case ETilesetSource::FromUrl:
    UE_LOG(LogCesium, Log, TEXT("Loading tileset from URL %s"), *this->Url);
    this->_pTileset = MakeUnique<Cesium3DTilesSelection::Tileset>(
        externals,
        TCHAR_TO_UTF8(*this->Url),
        options);
    break;
  case ETilesetSource::FromCesiumIon:
    UE_LOG(
        LogCesium,
        Log,
        TEXT("Loading tileset for asset ID %d"),
        this->IonAssetID);
    FString token = this->IonAccessToken.IsEmpty()
                        ? this->CesiumIonServer->DefaultIonAccessToken
                        : this->IonAccessToken;

#if WITH_EDITOR
    this->CesiumIonServer->ResolveApiUrl();
#endif

    std::string ionAssetEndpointUrl =
        TCHAR_TO_UTF8(*this->CesiumIonServer->ApiUrl);

    if (!ionAssetEndpointUrl.empty()) {
      // Make sure the URL ends with a slash
      if (!ionAssetEndpointUrl.empty() && *ionAssetEndpointUrl.rbegin() != '/')
        ionAssetEndpointUrl += '/';

      this->_pTileset = MakeUnique<Cesium3DTilesSelection::Tileset>(
          externals,
          static_cast<uint32_t>(this->IonAssetID),
          TCHAR_TO_UTF8(*token),
          options,
          ionAssetEndpointUrl);
    }
    break;
  }

  for (UITwinCesiumRasterOverlay* pOverlay : rasterOverlays) {
    if (pOverlay->IsActive()) {
      pOverlay->AddToTileset();
    }
  }

  for (UITwinCesiumTileExcluder* pTileExcluder : tileExcluders) {
    if (pTileExcluder->IsActive()) {
      pTileExcluder->AddToTileset();
    }
  }

  switch (this->TilesetSource) {
  case ETilesetSource::FromUrl:
    UE_LOG(
        LogCesium,
        Log,
        TEXT("Loading tileset from URL %s done"),
        *this->Url);
    break;
  case ETilesetSource::FromCesiumIon:
    UE_LOG(
        LogCesium,
        Log,
        TEXT("Loading tileset for asset ID %d done"),
        this->IonAssetID);
    break;
  }

  switch (ApplyDpiScaling) {
  case (EApplyDpiScaling::UseProjectDefault):
    _scaleUsingDPI =
        GetDefault<UITwinCesiumRuntimeSettings>()->ScaleLevelOfDetailByDPI;
    break;
  case (EApplyDpiScaling::Yes):
    _scaleUsingDPI = true;
    break;
  case (EApplyDpiScaling::No):
    _scaleUsingDPI = false;
    break;
  default:
    _scaleUsingDPI = true;
  }
}

void AITwinCesium3DTileset::DestroyTileset() {
  if (this->_cesiumViewExtension) {
    this->_cesiumViewExtension = nullptr;
  }

  switch (this->TilesetSource) {
  case ETilesetSource::FromUrl:
    UE_LOG(
        LogCesium,
        Verbose,
        TEXT("Destroying tileset from URL %s"),
        *this->Url);
    break;
  case ETilesetSource::FromCesiumIon:
    UE_LOG(
        LogCesium,
        Verbose,
        TEXT("Destroying tileset for asset ID %d"),
        this->IonAssetID);
    break;
  }

  // The way CesiumRasterOverlay::add is currently implemented, destroying the
  // tileset without removing overlays will make it impossible to add it again
  // once a new tileset is created (e.g. when switching between terrain
  // assets)
  TArray<UITwinCesiumRasterOverlay*> rasterOverlays;
  this->GetComponents<UITwinCesiumRasterOverlay>(rasterOverlays);
  for (UITwinCesiumRasterOverlay* pOverlay : rasterOverlays) {
    if (pOverlay->IsActive()) {
      pOverlay->RemoveFromTileset();
    }
  }

  TArray<UITwinCesiumTileExcluder*> tileExcluders;
  this->GetComponents<UITwinCesiumTileExcluder>(tileExcluders);
  for (UITwinCesiumTileExcluder* pTileExcluder : tileExcluders) {
    if (pTileExcluder->IsActive()) {
      pTileExcluder->RemoveFromTileset();
    }
  }

  if (!this->_pTileset) {
    return;
  }

  // Don't allow this Cesium3DTileset to be fully destroyed until
  // any cesium-native Tilesets it created have wrapped up any async
  // operations in progress and have been fully destroyed.
  // See IsReadyForFinishDestroy.
  ++this->_tilesetsBeingDestroyed;
  this->_pTileset->getAsyncDestructionCompleteEvent().thenInMainThread(
      [this]() { --this->_tilesetsBeingDestroyed; });
  this->_pTileset.Reset();

  switch (this->TilesetSource) {
  case ETilesetSource::FromUrl:
    UE_LOG(
        LogCesium,
        Verbose,
        TEXT("Destroying tileset from URL %s done"),
        *this->Url);
    break;
  case ETilesetSource::FromCesiumIon:
    UE_LOG(
        LogCesium,
        Verbose,
        TEXT("Destroying tileset for asset ID %d done"),
        this->IonAssetID);
    break;
  }
}

std::vector<FITwinCesiumCamera> AITwinCesium3DTileset::GetCameras() const {
  TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::CollectCameras)
  std::vector<FITwinCesiumCamera> cameras = this->GetPlayerCameras();

  std::vector<FITwinCesiumCamera> sceneCaptures = this->GetSceneCaptures();
  cameras.insert(
      cameras.end(),
      std::make_move_iterator(sceneCaptures.begin()),
      std::make_move_iterator(sceneCaptures.end()));

#if WITH_EDITOR
  std::vector<FITwinCesiumCamera> editorCameras = this->GetEditorCameras();
  cameras.insert(
      cameras.end(),
      std::make_move_iterator(editorCameras.begin()),
      std::make_move_iterator(editorCameras.end()));
#endif

  AITwinCesiumCameraManager* pCameraManager = this->ResolvedCameraManager;
  if (pCameraManager) {
    const TMap<int32, FITwinCesiumCamera>& extraCameras =
        pCameraManager->GetCameras();
    cameras.reserve(cameras.size() + extraCameras.Num());
    for (auto cameraIt : extraCameras) {
      cameras.push_back(cameraIt.Value);
    }
  }

  return cameras;
}

std::vector<FITwinCesiumCamera> AITwinCesium3DTileset::GetPlayerCameras() const {
  UWorld* pWorld = this->GetWorld();
  if (!pWorld) {
    return {};
  }

  double worldToMeters = 100.0;
  AWorldSettings* pWorldSettings = pWorld->GetWorldSettings();
  if (pWorldSettings) {
    worldToMeters = pWorldSettings->WorldToMeters;
  }

  TSharedPtr<IStereoRendering, ESPMode::ThreadSafe> pStereoRendering = nullptr;
  if (GEngine) {
    pStereoRendering = GEngine->StereoRenderingDevice;
  }

  bool useStereoRendering = false;
  if (pStereoRendering && pStereoRendering->IsStereoEnabled()) {
    useStereoRendering = true;
  }

  std::vector<FITwinCesiumCamera> cameras;
  cameras.reserve(pWorld->GetNumPlayerControllers());

  for (auto playerControllerIt = pWorld->GetPlayerControllerIterator();
       playerControllerIt;
       playerControllerIt++) {

    const TWeakObjectPtr<APlayerController> pPlayerController =
        *playerControllerIt;
    if (pPlayerController == nullptr) {
      continue;
    }

    const APlayerCameraManager* pPlayerCameraManager =
        pPlayerController->PlayerCameraManager;

    if (!pPlayerCameraManager) {
      continue;
    }

    double fov = pPlayerCameraManager->GetFOVAngle();

    FVector location;
    FRotator rotation;
    pPlayerController->GetPlayerViewPoint(location, rotation);

    int32 sizeX, sizeY;
    pPlayerController->GetViewportSize(sizeX, sizeY);
    if (sizeX < 1 || sizeY < 1) {
      continue;
    }

    float dpiScalingFactor = 1.0f;
    if (this->_scaleUsingDPI) {
      ULocalPlayer* LocPlayer = Cast<ULocalPlayer>(pPlayerController->Player);
      if (LocPlayer && LocPlayer->ViewportClient) {
        dpiScalingFactor = LocPlayer->ViewportClient->GetDPIScale();
      }
    }

    if (useStereoRendering) {
      const auto leftEye = EStereoscopicEye::eSSE_LEFT_EYE;
      const auto rightEye = EStereoscopicEye::eSSE_RIGHT_EYE;

      uint32 stereoLeftSizeX = static_cast<uint32>(sizeX);
      uint32 stereoLeftSizeY = static_cast<uint32>(sizeY);
      uint32 stereoRightSizeX = static_cast<uint32>(sizeX);
      uint32 stereoRightSizeY = static_cast<uint32>(sizeY);
      if (useStereoRendering) {
        int32 _x;
        int32 _y;

        pStereoRendering
            ->AdjustViewRect(leftEye, _x, _y, stereoLeftSizeX, stereoLeftSizeY);

        pStereoRendering->AdjustViewRect(
            rightEye,
            _x,
            _y,
            stereoRightSizeX,
            stereoRightSizeY);
      }

      FVector2D stereoLeftSize(stereoLeftSizeX, stereoLeftSizeY);
      FVector2D stereoRightSize(stereoRightSizeX, stereoRightSizeY);

      if (stereoLeftSize.X >= 1.0 && stereoLeftSize.Y >= 1.0) {
        FVector leftEyeLocation = location;
        FRotator leftEyeRotation = rotation;
        pStereoRendering->CalculateStereoViewOffset(
            leftEye,
            leftEyeRotation,
            worldToMeters,
            leftEyeLocation);

        FMatrix projection =
            pStereoRendering->GetStereoProjectionMatrix(leftEye);

        // TODO: consider assymetric frustums using 4 fovs
        double one_over_tan_half_hfov = projection.M[0][0];

        double hfov =
            glm::degrees(2.0 * glm::atan(1.0 / one_over_tan_half_hfov));

        cameras.emplace_back(
            stereoLeftSize,
            leftEyeLocation,
            leftEyeRotation,
            hfov);
      }

      if (stereoRightSize.X >= 1.0 && stereoRightSize.Y >= 1.0) {
        FVector rightEyeLocation = location;
        FRotator rightEyeRotation = rotation;
        pStereoRendering->CalculateStereoViewOffset(
            rightEye,
            rightEyeRotation,
            worldToMeters,
            rightEyeLocation);

        FMatrix projection =
            pStereoRendering->GetStereoProjectionMatrix(rightEye);

        double one_over_tan_half_hfov = projection.M[0][0];

        double hfov =
            glm::degrees(2.0f * glm::atan(1.0f / one_over_tan_half_hfov));

        cameras.emplace_back(
            stereoRightSize,
            rightEyeLocation,
            rightEyeRotation,
            hfov);
      }
    } else {
      cameras.emplace_back(
          FVector2D(sizeX / dpiScalingFactor, sizeY / dpiScalingFactor),
          location,
          rotation,
          fov);
    }
  }

  return cameras;
}

std::vector<FITwinCesiumCamera> AITwinCesium3DTileset::GetSceneCaptures() const {
  // TODO: really USceneCaptureComponent2D can be attached to any actor, is it
  // worth searching every actor? Might it be better to provide an interface
  // where users can volunteer cameras to be used with the tile selection as
  // needed?
  TArray<AActor*> sceneCaptures;
  static TSubclassOf<ASceneCapture2D> SceneCapture2D =
      ASceneCapture2D::StaticClass();
  UGameplayStatics::GetAllActorsOfClass(this, SceneCapture2D, sceneCaptures);

  std::vector<FITwinCesiumCamera> cameras;
  cameras.reserve(sceneCaptures.Num());

  for (AActor* pActor : sceneCaptures) {
    ASceneCapture2D* pSceneCapture = static_cast<ASceneCapture2D*>(pActor);
    if (!pSceneCapture) {
      continue;
    }

    USceneCaptureComponent2D* pSceneCaptureComponent =
        pSceneCapture->GetCaptureComponent2D();
    if (!pSceneCaptureComponent) {
      continue;
    }

    if (pSceneCaptureComponent->ProjectionType !=
        ECameraProjectionMode::Type::Perspective) {
      continue;
    }

    UTextureRenderTarget2D* pRenderTarget =
        pSceneCaptureComponent->TextureTarget;
    if (!pRenderTarget) {
      continue;
    }

    FVector2D renderTargetSize(pRenderTarget->SizeX, pRenderTarget->SizeY);
    if (renderTargetSize.X < 1.0 || renderTargetSize.Y < 1.0) {
      continue;
    }

    FVector captureLocation = pSceneCaptureComponent->GetComponentLocation();
    FRotator captureRotation = pSceneCaptureComponent->GetComponentRotation();
    double captureFov = pSceneCaptureComponent->FOVAngle;

    cameras.emplace_back(
        renderTargetSize,
        captureLocation,
        captureRotation,
        captureFov);
  }

  return cameras;
}

/*static*/ Cesium3DTilesSelection::ViewState
AITwinCesium3DTileset::CreateViewStateFromViewParameters(
    const FITwinCesiumCamera& camera,
    const glm::dmat4& unrealWorldToTileset) {

  double horizontalFieldOfView =
      FMath::DegreesToRadians(camera.FieldOfViewDegrees);

  double actualAspectRatio;
  glm::dvec2 size(camera.ViewportSize.X, camera.ViewportSize.Y);

  if (camera.OverrideAspectRatio != 0.0f) {
    // Use aspect ratio and recompute effective viewport size after black bars
    // are added.
    actualAspectRatio = camera.OverrideAspectRatio;
    double computedX = actualAspectRatio * camera.ViewportSize.Y;
    double computedY = camera.ViewportSize.Y / actualAspectRatio;

    double barWidth = camera.ViewportSize.X - computedX;
    double barHeight = camera.ViewportSize.Y - computedY;

    if (barWidth > 0.0 && barWidth > barHeight) {
      // Black bars on the sides
      size.x = computedX;
    } else if (barHeight > 0.0 && barHeight > barWidth) {
      // Black bars on the top and bottom
      size.y = computedY;
    }
  } else {
    actualAspectRatio = camera.ViewportSize.X / camera.ViewportSize.Y;
  }

  double verticalFieldOfView =
      atan(tan(horizontalFieldOfView * 0.5) / actualAspectRatio) * 2.0;

  FVector direction = camera.Rotation.RotateVector(FVector(1.0f, 0.0f, 0.0f));
  FVector up = camera.Rotation.RotateVector(FVector(0.0f, 0.0f, 1.0f));

  glm::dvec3 tilesetCameraLocation = glm::dvec3(
      unrealWorldToTileset *
      glm::dvec4(camera.Location.X, camera.Location.Y, camera.Location.Z, 1.0));
  glm::dvec3 tilesetCameraFront = glm::normalize(glm::dvec3(
      unrealWorldToTileset *
      glm::dvec4(direction.X, direction.Y, direction.Z, 0.0)));
  glm::dvec3 tilesetCameraUp = glm::normalize(
      glm::dvec3(unrealWorldToTileset * glm::dvec4(up.X, up.Y, up.Z, 0.0)));

  return Cesium3DTilesSelection::ViewState::create(
      tilesetCameraLocation,
      tilesetCameraFront,
      tilesetCameraUp,
      size,
      horizontalFieldOfView,
      verticalFieldOfView);
}

#if WITH_EDITOR
std::vector<FITwinCesiumCamera> AITwinCesium3DTileset::GetEditorCameras() const {
  if (!GEditor) {
    return {};
  }

  UWorld* pWorld = this->GetWorld();
  if (!IsValid(pWorld)) {
    return {};
  }

  // Do not include editor cameras when running in a game world (which includes
  // Play-in-Editor)
  if (pWorld->IsGameWorld()) {
    return {};
  }

  const TArray<FEditorViewportClient*>& viewportClients =
      GEditor->GetAllViewportClients();

  std::vector<FITwinCesiumCamera> cameras;
  cameras.reserve(viewportClients.Num());

  for (FEditorViewportClient* pEditorViewportClient : viewportClients) {
    if (!pEditorViewportClient) {
      continue;
    }

    if (!pEditorViewportClient->IsVisible() ||
        !pEditorViewportClient->IsRealtime() ||
        !pEditorViewportClient->IsPerspective()) {
      continue;
    }

    FRotator rotation;
    if (pEditorViewportClient->bUsingOrbitCamera) {
      rotation = (pEditorViewportClient->GetLookAtLocation() -
                  pEditorViewportClient->GetViewLocation())
                     .Rotation();
    } else {
      rotation = pEditorViewportClient->GetViewRotation();
    }

    const FVector& location = pEditorViewportClient->GetViewLocation();
    double fov = pEditorViewportClient->ViewFOV;
    FIntPoint offset;
    FIntPoint size;
    pEditorViewportClient->GetViewportDimensions(offset, size);

    if (size.X < 1 || size.Y < 1) {
      continue;
    }

    if (this->_scaleUsingDPI) {
      float dpiScalingFactor = pEditorViewportClient->GetDPIScale();
      size.X = static_cast<float>(size.X) / dpiScalingFactor;
      size.Y = static_cast<float>(size.Y) / dpiScalingFactor;
    }

    if (pEditorViewportClient->IsAspectRatioConstrained()) {
      cameras.emplace_back(
          size,
          location,
          rotation,
          fov,
          pEditorViewportClient->AspectRatio);
    } else {
      cameras.emplace_back(size, location, rotation, fov);
    }
  }

  return cameras;
}
#endif

bool AITwinCesium3DTileset::ShouldTickIfViewportsOnly() const {
  return this->UpdateInEditor;
}

namespace {

void removeVisibleTilesFromList(
    std::vector<Cesium3DTilesSelection::Tile*>& list,
    const std::vector<Cesium3DTilesSelection::Tile*>& visibleTiles) {
  if (list.empty()) {
    return;
  }

  for (Cesium3DTilesSelection::Tile* pTile : visibleTiles) {
    auto it = std::find(list.begin(), list.end(), pTile);
    if (it != list.end()) {
      list.erase(it);
    }
  }
}

/**
 * @brief Hides the visual representations of the given tiles.
 *
 * The visual representations (i.e. the `getRendererResources` of the
 * tiles) are assumed to be `UITwinCesiumGltfComponent` instances that
 * are made invisible by this call.
 *
 * @param tiles The tiles to hide
 */
void hideTiles(const std::vector<Cesium3DTilesSelection::Tile*>& tiles) {
  TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::HideTiles)
  for (Cesium3DTilesSelection::Tile* pTile : tiles) {
    if (pTile->getState() != Cesium3DTilesSelection::TileLoadState::Done) {
      continue;
    }

    const Cesium3DTilesSelection::TileContent& content = pTile->getContent();
    const Cesium3DTilesSelection::TileRenderContent* pRenderContent =
        content.getRenderContent();
    if (!pRenderContent) {
      continue;
    }

    UITwinCesiumGltfComponent* Gltf = static_cast<UITwinCesiumGltfComponent*>(
        pRenderContent->getRenderResources());
    if (Gltf && Gltf->IsVisible()) {
      TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::SetVisibilityFalse)
      Gltf->SetVisibility(false, true);
    } else {
      // TODO: why is this happening?
      UE_LOG(
          LogCesium,
          Verbose,
          TEXT("Tile to no longer render does not have a visible Gltf"));
    }
  }
}

/**
 * @brief Removes collision for tiles that have been removed from the render
 * list. This includes tiles that are fading out.
 */
void removeCollisionForTiles(
    const std::unordered_set<Cesium3DTilesSelection::Tile*>& tiles) {
  TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::RemoveCollisionForTiles)
  for (Cesium3DTilesSelection::Tile* pTile : tiles) {
    if (pTile->getState() != Cesium3DTilesSelection::TileLoadState::Done) {
      continue;
    }

    const Cesium3DTilesSelection::TileContent& content = pTile->getContent();
    const Cesium3DTilesSelection::TileRenderContent* pRenderContent =
        content.getRenderContent();
    if (!pRenderContent) {
      continue;
    }

    UITwinCesiumGltfComponent* Gltf = static_cast<UITwinCesiumGltfComponent*>(
        pRenderContent->getRenderResources());
    if (Gltf) {
      TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::SetCollisionDisabled)
      Gltf->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
  }
}

/**
 * @brief Applies the actor collision settings for a newly created glTF
 * component
 *
 * TODO Add details here what that means
 * @param BodyInstance ...
 * @param Gltf ...
 */
void applyActorCollisionSettings(
    const FBodyInstance& BodyInstance,
    UITwinCesiumGltfComponent* Gltf) {
  TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::ApplyActorCollisionSettings)

  const TArray<USceneComponent*>& ChildrenComponents =
      Gltf->GetAttachChildren();

  for (USceneComponent* ChildComponent : ChildrenComponents) {
    UITwinCesiumGltfPrimitiveComponent* PrimitiveComponent =
        Cast<UITwinCesiumGltfPrimitiveComponent>(ChildComponent);
    if (PrimitiveComponent != nullptr) {
      if (PrimitiveComponent->GetCollisionObjectType() !=
          BodyInstance.GetObjectType()) {
        PrimitiveComponent->SetCollisionObjectType(
            BodyInstance.GetObjectType());
      }
      const UEnum* ChannelEnum = StaticEnum<ECollisionChannel>();
      if (ChannelEnum) {
        FCollisionResponseContainer responseContainer =
            BodyInstance.GetResponseToChannels();
        PrimitiveComponent->SetCollisionResponseToChannels(responseContainer);
      }
    }
  }
}
} // namespace

void AITwinCesium3DTileset::updateTilesetOptionsFromProperties() {
  Cesium3DTilesSelection::TilesetOptions& options =
      this->_pTileset->getOptions();
  options.maximumScreenSpaceError =
      static_cast<double>(this->MaximumScreenSpaceError);
  options.maximumCachedBytes = this->MaximumCachedBytes;
  options.preloadAncestors = this->PreloadAncestors;
  options.preloadSiblings = this->PreloadSiblings;
  options.forbidHoles = this->ForbidHoles;
  options.maximumSimultaneousTileLoads = this->MaximumSimultaneousTileLoads;
  options.loadingDescendantLimit = this->LoadingDescendantLimit;
  options.enableFrustumCulling = this->EnableFrustumCulling;
  options.enableOcclusionCulling =
      GetDefault<UITwinCesiumRuntimeSettings>()
          ->EnableExperimentalOcclusionCullingFeature &&
      this->EnableOcclusionCulling;
  options.showCreditsOnScreen = this->ShowCreditsOnScreen;

  options.delayRefinementForOcclusion = this->DelayRefinementForOcclusion;
  options.enableFogCulling = this->EnableFogCulling;
  options.enforceCulledScreenSpaceError = this->EnforceCulledScreenSpaceError;
  options.culledScreenSpaceError =
      static_cast<double>(this->CulledScreenSpaceError);
  options.enableLodTransitionPeriod = this->UseLodTransitions;
  options.lodTransitionLength = this->LodTransitionLength;
  // options.kickDescendantsWhileFadingIn = false;
}

void AITwinCesium3DTileset::updateLastViewUpdateResultState(
    const Cesium3DTilesSelection::ViewUpdateResult& result) {
  TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::updateLastViewUpdateResultState)

  if (!this->LogSelectionStats) {
    return;
  }

  if (result.tilesToRenderThisFrame.size() != this->_lastTilesRendered ||
      result.workerThreadTileLoadQueueLength !=
          this->_lastWorkerThreadTileLoadQueueLength ||
      result.mainThreadTileLoadQueueLength !=
          this->_lastMainThreadTileLoadQueueLength ||
      result.tilesVisited != this->_lastTilesVisited ||
      result.culledTilesVisited != this->_lastCulledTilesVisited ||
      result.tilesCulled != this->_lastTilesCulled ||
      result.tilesOccluded != this->_lastTilesOccluded ||
      result.tilesWaitingForOcclusionResults !=
          this->_lastTilesWaitingForOcclusionResults ||
      result.maxDepthVisited != this->_lastMaxDepthVisited) {

    this->_lastTilesRendered = result.tilesToRenderThisFrame.size();
    this->_lastWorkerThreadTileLoadQueueLength =
        result.workerThreadTileLoadQueueLength;
    this->_lastMainThreadTileLoadQueueLength =
        result.mainThreadTileLoadQueueLength;

    this->_lastTilesVisited = result.tilesVisited;
    this->_lastCulledTilesVisited = result.culledTilesVisited;
    this->_lastTilesCulled = result.tilesCulled;
    this->_lastTilesOccluded = result.tilesOccluded;
    this->_lastTilesWaitingForOcclusionResults =
        result.tilesWaitingForOcclusionResults;
    this->_lastMaxDepthVisited = result.maxDepthVisited;

    UE_LOG(
        LogCesium,
        Display,
        TEXT(
            "%s: %d ms, Visited %d, Culled Visited %d, Rendered %d, Culled %d, Occluded %d, Waiting For Occlusion Results %d, Max Depth Visited: %d, Loading-Worker %d, Loading-Main %d, Loaded tiles %g%%"),
        *this->GetName(),
        (std::chrono::high_resolution_clock::now() - this->_startTime).count() /
            1000000,
        result.tilesVisited,
        result.culledTilesVisited,
        result.tilesToRenderThisFrame.size(),
        result.tilesCulled,
        result.tilesOccluded,
        result.tilesWaitingForOcclusionResults,
        result.maxDepthVisited,
        result.workerThreadTileLoadQueueLength,
        result.mainThreadTileLoadQueueLength,
        this->LoadProgress);
  }
}

void AITwinCesium3DTileset::showTilesToRender(
    const std::vector<Cesium3DTilesSelection::Tile*>& tiles) {
  TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::ShowTilesToRender)

  for (Cesium3DTilesSelection::Tile* pTile : tiles) {
    if (pTile->getState() != Cesium3DTilesSelection::TileLoadState::Done) {
      continue;
    }

    // That looks like some reeeally entertaining debug session...:
    // const Cesium3DTilesSelection::TileID& id = pTile->getTileID();
    // const CesiumGeometry::QuadtreeTileID* pQuadtreeID =
    // std::get_if<CesiumGeometry::QuadtreeTileID>(&id); if (!pQuadtreeID ||
    // pQuadtreeID->level != 14 || pQuadtreeID->x != 5503 || pQuadtreeID->y !=
    // 11626) { 	continue;
    //}

    const Cesium3DTilesSelection::TileContent& content = pTile->getContent();
    const Cesium3DTilesSelection::TileRenderContent* pRenderContent =
        content.getRenderContent();
    if (!pRenderContent) {
      continue;
    }

    UITwinCesiumGltfComponent* Gltf = static_cast<UITwinCesiumGltfComponent*>(
        pRenderContent->getRenderResources());
    if (!Gltf) {
      // When a tile does not have render resources (i.e. a glTF), then
      // the resources either have not yet been loaded or prepared,
      // or the tile is from an external tileset and does not directly
      // own renderable content. In both cases, the tile is ignored here.
      continue;
    }

    applyActorCollisionSettings(BodyInstance, Gltf);

    if (Gltf->GetAttachParent() == nullptr) {

      // The AttachToComponent method is ridiculously complex,
      // so print a warning if attaching fails for some reason
      bool attached = Gltf->AttachToComponent(
          this->RootComponent,
          FAttachmentTransformRules::KeepRelativeTransform);
      if (!attached) {
        FString tileIdString(
            Cesium3DTilesSelection::TileIdUtilities::createTileIdString(
                pTile->getTileID())
                .c_str());
        UE_LOG(
            LogCesium,
            Warning,
            TEXT("Tile %s could not be attached to root"),
            *tileIdString);
      }
    }

    if (!Gltf->IsVisible()) {
      TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::SetVisibilityTrue)
      Gltf->SetVisibility(true, true);
    }

    {
      TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::SetCollisionEnabled)
      Gltf->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    }
  }
}

static void updateTileFade(Cesium3DTilesSelection::Tile* pTile, bool fadingIn) {
  if (!pTile || !pTile->getContent().isRenderContent()) {
    return;
  }

  if (pTile->getState() != Cesium3DTilesSelection::TileLoadState::Done) {
    return;
  }

  const Cesium3DTilesSelection::TileContent& content = pTile->getContent();
  const Cesium3DTilesSelection::TileRenderContent* pRenderContent =
      content.getRenderContent();
  if (!pRenderContent) {
    return;
  }

  UITwinCesiumGltfComponent* pGltf = reinterpret_cast<UITwinCesiumGltfComponent*>(
      pRenderContent->getRenderResources());
  if (!pGltf) {
    return;
  }

  float percentage =
      pTile->getContent().getRenderContent()->getLodTransitionFadePercentage();

  pGltf->UpdateFade(percentage, fadingIn);
}

// Called every frame
void AITwinCesium3DTileset::Tick(float DeltaTime) {
  TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::TilesetTick)

  Super::Tick(DeltaTime);

  this->ResolveGeoreference();
  this->ResolveCameraManager();
  this->ResolveCreditSystem();

  UITwinCesium3DTilesetRoot* pRoot = Cast<UITwinCesium3DTilesetRoot>(this->RootComponent);
  if (!pRoot) {
    return;
  }

  if (this->SuspendUpdate) {
    return;
  }

  if (!this->_pTileset) {
    LoadTileset();

    // In the unlikely event that we _still_ don't have a tileset, stop here so
    // we don't crash below. This shouldn't happen.
    if (!this->_pTileset) {
      assert(false);
      return;
    }
  }

  if (this->BoundingVolumePoolComponent && this->_cesiumViewExtension) {
    TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::UpdateOcclusion)
    const TArray<USceneComponent*>& children =
        this->BoundingVolumePoolComponent->GetAttachChildren();
    for (USceneComponent* pChild : children) {
      UITwinCesiumBoundingVolumeComponent* pBoundingVolume =
          Cast<UITwinCesiumBoundingVolumeComponent>(pChild);

      if (!pBoundingVolume) {
        continue;
      }

      pBoundingVolume->UpdateOcclusion(*this->_cesiumViewExtension.Get());
    }
  }

  updateTilesetOptionsFromProperties();

  std::vector<FITwinCesiumCamera> cameras = this->GetCameras();
  if (cameras.empty()) {
    return;
  }

  glm::dmat4 ueTilesetToUeWorld =
      VecMath::createMatrix4D(this->GetActorTransform().ToMatrixWithScale());

  const glm::dmat4& cesiumTilesetToUeTileset =
      this->GetCesiumTilesetToUnrealRelativeWorldTransform();
  glm::dmat4 unrealWorldToCesiumTileset =
      glm::affineInverse(ueTilesetToUeWorld * cesiumTilesetToUeTileset);

  if (glm::isnan(unrealWorldToCesiumTileset[3].x) ||
      glm::isnan(unrealWorldToCesiumTileset[3].y) ||
      glm::isnan(unrealWorldToCesiumTileset[3].z)) {
    // Probably caused by a zero scale.
    return;
  }

  std::vector<Cesium3DTilesSelection::ViewState> frustums;
  for (const FITwinCesiumCamera& camera : cameras) {
    frustums.push_back(
        CreateViewStateFromViewParameters(camera, unrealWorldToCesiumTileset));
  }

  const Cesium3DTilesSelection::ViewUpdateResult* pResult;
  if (this->_captureMovieMode) {
    TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::updateViewOffline)
    pResult = &this->_pTileset->updateViewOffline(frustums);
  } else {
    TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::updateView)
    pResult = &this->_pTileset->updateView(frustums, DeltaTime);
  }
  updateLastViewUpdateResultState(*pResult);

  removeCollisionForTiles(pResult->tilesFadingOut);

  removeVisibleTilesFromList(
      _tilesToHideNextFrame,
      pResult->tilesToRenderThisFrame);
  hideTiles(_tilesToHideNextFrame);

  _tilesToHideNextFrame.clear();
  for (Cesium3DTilesSelection::Tile* pTile : pResult->tilesFadingOut) {
    Cesium3DTilesSelection::TileRenderContent* pRenderContent =
        pTile->getContent().getRenderContent();
    if (!this->UseLodTransitions ||
        (pRenderContent &&
         pRenderContent->getLodTransitionFadePercentage() >= 1.0f)) {
      _tilesToHideNextFrame.push_back(pTile);
    }
  }

  showTilesToRender(pResult->tilesToRenderThisFrame);

  if (this->UseLodTransitions) {
    TRACE_CPUPROFILER_EVENT_SCOPE(Cesium::UpdateTileFades)

    for (Cesium3DTilesSelection::Tile* pTile :
         pResult->tilesToRenderThisFrame) {
      updateTileFade(pTile, true);
    }

    for (Cesium3DTilesSelection::Tile* pTile : pResult->tilesFadingOut) {
      updateTileFade(pTile, false);
    }
  }

  this->UpdateLoadStatus();
}

void AITwinCesium3DTileset::EndPlay(const EEndPlayReason::Type EndPlayReason) {
  this->DestroyTileset();
  AActor::EndPlay(EndPlayReason);
}

void AITwinCesium3DTileset::PostLoad() {
  BodyInstance.FixupData(this); // We need to call this one after Loading the
                                // actor to have correct BodyInstance values.

  Super::PostLoad();

  if (CesiumActors::shouldValidateFlags(this))
    CesiumActors::validateActorFlags(this);

#if WITH_EDITOR
  const int32 CesiumVersion =
      this->GetLinkerCustomVersion(FITwinCesiumCustomVersion::GUID);

  PRAGMA_DISABLE_DEPRECATION_WARNINGS
  if (CesiumVersion < FITwinCesiumCustomVersion::CesiumIonServer) {
    this->CesiumIonServer = UITwinCesiumIonServer::GetBackwardCompatibleServer(
        this->IonAssetEndpointUrl_DEPRECATED);
  }
  PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif
}

void AITwinCesium3DTileset::Serialize(FArchive& Ar) {
  Super::Serialize(Ar);

  Ar.UsingCustomVersion(FITwinCesiumCustomVersion::GUID);

  const int32 CesiumVersion = Ar.CustomVer(FITwinCesiumCustomVersion::GUID);

  if (CesiumVersion < FITwinCesiumCustomVersion::TilesetExplicitSource) {
    // In previous versions, the tileset source was inferred from the presence
    // of a non-empty URL property, rather than being explicitly specified.
    if (this->Url.Len() > 0) {
      this->TilesetSource = ETilesetSource::FromUrl;
    } else {
      this->TilesetSource = ETilesetSource::FromCesiumIon;
    }
  }

  if (CesiumVersion < FITwinCesiumCustomVersion::TilesetMobilityRemoved) {
    this->RootComponent->SetMobility(this->Mobility_DEPRECATED);
  }
}

#if WITH_EDITOR
void AITwinCesium3DTileset::PostEditChangeProperty(
    FPropertyChangedEvent& PropertyChangedEvent) {
  Super::PostEditChangeProperty(PropertyChangedEvent);

  if (!PropertyChangedEvent.Property) {
    return;
  }

  FName PropName = PropertyChangedEvent.Property->GetFName();
  FString PropNameAsString = PropertyChangedEvent.Property->GetName();

  if (PropName == GET_MEMBER_NAME_CHECKED(AITwinCesium3DTileset, TilesetSource) ||
      PropName == GET_MEMBER_NAME_CHECKED(AITwinCesium3DTileset, Url) ||
      PropName == GET_MEMBER_NAME_CHECKED(AITwinCesium3DTileset, IonAssetID) ||
      PropName == GET_MEMBER_NAME_CHECKED(AITwinCesium3DTileset, IonAccessToken) ||
      PropName ==
          GET_MEMBER_NAME_CHECKED(AITwinCesium3DTileset, CreatePhysicsMeshes) ||
      PropName ==
          GET_MEMBER_NAME_CHECKED(AITwinCesium3DTileset, CreateNavCollision) ||
      PropName ==
          GET_MEMBER_NAME_CHECKED(AITwinCesium3DTileset, AlwaysIncludeTangents) ||
      PropName ==
          GET_MEMBER_NAME_CHECKED(AITwinCesium3DTileset, GenerateSmoothNormals) ||
      PropName == GET_MEMBER_NAME_CHECKED(AITwinCesium3DTileset, EnableWaterMask) ||
      PropName ==
          GET_MEMBER_NAME_CHECKED(AITwinCesium3DTileset, IgnoreKhrMaterialsUnlit) ||
      PropName == GET_MEMBER_NAME_CHECKED(AITwinCesium3DTileset, Material) ||
      PropName ==
          GET_MEMBER_NAME_CHECKED(AITwinCesium3DTileset, TranslucentMaterial) ||
      PropName == GET_MEMBER_NAME_CHECKED(AITwinCesium3DTileset, WaterMaterial) ||
      PropName == GET_MEMBER_NAME_CHECKED(AITwinCesium3DTileset, ApplyDpiScaling) ||
      PropName ==
          GET_MEMBER_NAME_CHECKED(AITwinCesium3DTileset, EnableOcclusionCulling) ||
      PropName ==
          GET_MEMBER_NAME_CHECKED(AITwinCesium3DTileset, UseLodTransitions) ||
      PropName ==
          GET_MEMBER_NAME_CHECKED(AITwinCesium3DTileset, ShowCreditsOnScreen) ||
      PropName == GET_MEMBER_NAME_CHECKED(AITwinCesium3DTileset, Root) ||
      PropName == GET_MEMBER_NAME_CHECKED(AITwinCesium3DTileset, CesiumIonServer) ||
      // For properties nested in structs, GET_MEMBER_NAME_CHECKED will prefix
      // with the struct name, so just do a manual string comparison.
      PropNameAsString == TEXT("RenderCustomDepth") ||
      PropNameAsString == TEXT("CustomDepthStencilValue") ||
      PropNameAsString == TEXT("CustomDepthStencilWriteMask")) {
    this->DestroyTileset();
  } else if (
      PropName == GET_MEMBER_NAME_CHECKED(AITwinCesium3DTileset, Georeference)) {
    this->InvalidateResolvedGeoreference();
  } else if (
      PropName == GET_MEMBER_NAME_CHECKED(AITwinCesium3DTileset, CreditSystem)) {
    this->InvalidateResolvedCreditSystem();
  } else if (
      PropName ==
      GET_MEMBER_NAME_CHECKED(AITwinCesium3DTileset, MaximumScreenSpaceError)) {
    TArray<UITwinCesiumRasterOverlay*> rasterOverlays;
    this->GetComponents<UITwinCesiumRasterOverlay>(rasterOverlays);

    for (UITwinCesiumRasterOverlay* pOverlay : rasterOverlays) {
      pOverlay->Refresh();
    }
    TArray<UITwinCesiumTileExcluder*> tileExcluders;
    this->GetComponents<UITwinCesiumTileExcluder>(tileExcluders);

    for (UITwinCesiumTileExcluder* pTileExcluder : tileExcluders) {
      pTileExcluder->Refresh();
    }

    // Maximum Screen Space Error can affect how attenuated points are rendered,
    // so propagate the new value to the render proxies for this tileset.
    FITwinCesiumGltfPointsSceneProxyUpdater::UpdateSettingsInProxies(this);
  }
}

void AITwinCesium3DTileset::PostEditChangeChainProperty(
    FPropertyChangedChainEvent& PropertyChangedChainEvent) {
  Super::PostEditChangeChainProperty(PropertyChangedChainEvent);

  if (!PropertyChangedChainEvent.Property ||
      PropertyChangedChainEvent.PropertyChain.IsEmpty()) {
    return;
  }

  FName PropName =
      PropertyChangedChainEvent.PropertyChain.GetHead()->GetValue()->GetFName();
  if (PropName ==
      GET_MEMBER_NAME_CHECKED(AITwinCesium3DTileset, PointCloudShading)) {
    FITwinCesiumGltfPointsSceneProxyUpdater::UpdateSettingsInProxies(this);
  }
}

void AITwinCesium3DTileset::PostEditUndo() {
  Super::PostEditUndo();

  // It doesn't appear to be possible to get detailed information about what
  // changed in the undo/redo operation, so we have to assume the worst and
  // recreate the tileset.
  this->DestroyTileset();
}

void AITwinCesium3DTileset::PostEditImport() {
  Super::PostEditImport();

  // Recreate the tileset on Paste.
  this->DestroyTileset();
}
#endif

void AITwinCesium3DTileset::BeginDestroy() {
  this->InvalidateResolvedGeoreference();
  this->DestroyTileset();

  AActor::BeginDestroy();
}

bool AITwinCesium3DTileset::IsReadyForFinishDestroy() {
  bool ready = AActor::IsReadyForFinishDestroy();
  ready &= this->_tilesetsBeingDestroyed == 0;

  if (!ready) {
    getAssetAccessor()->tick();
    getAsyncSystem().dispatchMainThreadTasks();
  }

  return ready;
}

void AITwinCesium3DTileset::Destroyed() {
  this->DestroyTileset();

  AActor::Destroyed();
}

#if WITH_EDITOR
void AITwinCesium3DTileset::RuntimeSettingsChanged(
    UObject* pObject,
    struct FPropertyChangedEvent& changed) {
  bool occlusionCullingAvailable =
      GetDefault<UITwinCesiumRuntimeSettings>()
          ->EnableExperimentalOcclusionCullingFeature;
  if (occlusionCullingAvailable != this->CanEnableOcclusionCulling) {
    this->CanEnableOcclusionCulling = occlusionCullingAvailable;
    this->RefreshTileset();
  }
}
#endif

void AITwinCesium3DTileset::SetMeshBuildCallbacks(const TWeakPtr<ICesiumMeshBuildCallbacks>& Callbacks) {
    this->_meshBuildCallbacks = Callbacks;
}

void AITwinCesium3DTileset::SetGltfTuner(const std::shared_ptr<Cesium3DTilesSelection::GltfTuner>& tuner)
{
	_gltfTuner = tuner;
}
