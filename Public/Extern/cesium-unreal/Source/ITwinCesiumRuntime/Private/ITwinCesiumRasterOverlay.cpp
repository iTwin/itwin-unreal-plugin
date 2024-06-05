// Copyright 2020-2021 CesiumGS, Inc. and Contributors

#include "ITwinCesiumRasterOverlay.h"
#include "Async/Async.h"
#include "Cesium3DTilesSelection/Tileset.h"
#include "ITwinCesium3DTileset.h"
#include "CesiumAsync/IAssetResponse.h"
#include "CesiumRasterOverlays/RasterOverlayLoadFailureDetails.h"
#include "ITwinCesiumRuntime.h"

FITwinCesiumRasterOverlayLoadFailure OnCesiumRasterOverlayLoadFailure{};

// Sets default values for this component's properties
UITwinCesiumRasterOverlay::UITwinCesiumRasterOverlay()
    : _pOverlay(nullptr), _overlaysBeingDestroyed(0) {
  this->bAutoActivate = true;

  // Set this component to be initialized when the game starts, and to be ticked
  // every frame.  You can turn these features off to improve performance if you
  // don't need them.
  PrimaryComponentTick.bCanEverTick = false;

  // ...
}

#if WITH_EDITOR
// Called when properties are changed in the editor
void UITwinCesiumRasterOverlay::PostEditChangeProperty(
    FPropertyChangedEvent& PropertyChangedEvent) {
  Super::PostEditChangeProperty(PropertyChangedEvent);

  this->Refresh();
}
#endif

void UITwinCesiumRasterOverlay::AddToTileset() {
  if (this->_pOverlay) {
    return;
  }

  Cesium3DTilesSelection::Tileset* pTileset = FindTileset();
  if (!pTileset) {
    return;
  }

  CesiumRasterOverlays::RasterOverlayOptions options{};
  options.maximumScreenSpaceError = this->MaximumScreenSpaceError;
  options.maximumSimultaneousTileLoads = this->MaximumSimultaneousTileLoads;
  options.maximumTextureSize = this->MaximumTextureSize;
  options.subTileCacheBytes = this->SubTileCacheBytes;
  options.showCreditsOnScreen = this->ShowCreditsOnScreen;
  options.rendererOptions = &this->rendererOptions;
  options.loadErrorCallback =
      [this](const CesiumRasterOverlays::RasterOverlayLoadFailureDetails&
                 details) {
        static_assert(
            uint8_t(ECesiumRasterOverlayLoadType::CesiumIon) ==
            uint8_t(CesiumRasterOverlays::RasterOverlayLoadType::CesiumIon));
        static_assert(
            uint8_t(ECesiumRasterOverlayLoadType::TileProvider) ==
            uint8_t(CesiumRasterOverlays::RasterOverlayLoadType::TileProvider));
        static_assert(
            uint8_t(ECesiumRasterOverlayLoadType::Unknown) ==
            uint8_t(CesiumRasterOverlays::RasterOverlayLoadType::Unknown));

        uint8_t typeValue = uint8_t(details.type);
        assert(
            uint8_t(details.type) <=
            uint8_t(
                Cesium3DTilesSelection::RasterOverlayLoadType::TilesetJson));
        assert(this->_pTileset == details.pTileset);

        FITwinCesiumRasterOverlayLoadFailureDetails ueDetails{};
        ueDetails.Overlay = this;
        ueDetails.Type = ECesiumRasterOverlayLoadType(typeValue);
        ueDetails.HttpStatusCode =
            details.pRequest && details.pRequest->response()
                ? details.pRequest->response()->statusCode()
                : 0;
        ueDetails.Message = UTF8_TO_TCHAR(details.message.c_str());

        // Broadcast the event from the game thread.
        // Even if we're already in the game thread, let the stack unwind.
        // Otherwise actions that destroy the Tileset will cause a deadlock.
        AsyncTask(
            ENamedThreads::GameThread,
            [ueDetails = std::move(ueDetails)]() {
              OnCesiumRasterOverlayLoadFailure.Broadcast(ueDetails);
            });
      };

  std::unique_ptr<CesiumRasterOverlays::RasterOverlay> pOverlay =
      this->CreateOverlay(options);

  if (pOverlay) {
    this->_pOverlay = pOverlay.release();

    pTileset->getOverlays().add(this->_pOverlay);

    this->OnAdd(pTileset, this->_pOverlay);
  }
}

void UITwinCesiumRasterOverlay::RemoveFromTileset() {
  if (!this->_pOverlay) {
    return;
  }

  Cesium3DTilesSelection::Tileset* pTileset = FindTileset();
  if (!pTileset) {
    return;
  }

  // Don't allow this RasterOverlay to be fully destroyed until
  // any cesium-native RasterOverlays it created have wrapped up any async
  // operations in progress and have been fully destroyed.
  // See IsReadyForFinishDestroy.
  ++this->_overlaysBeingDestroyed;
  this->_pOverlay->getAsyncDestructionCompleteEvent(getAsyncSystem())
      .thenInMainThread([this]() { --this->_overlaysBeingDestroyed; });

  this->OnRemove(pTileset, this->_pOverlay);
  pTileset->getOverlays().remove(this->_pOverlay);
  this->_pOverlay = nullptr;
}

void UITwinCesiumRasterOverlay::Refresh() {
  this->RemoveFromTileset();
  this->AddToTileset();
}

double UITwinCesiumRasterOverlay::GetMaximumScreenSpaceError() const {
  return this->MaximumScreenSpaceError;
}

void UITwinCesiumRasterOverlay::SetMaximumScreenSpaceError(double Value) {
  this->MaximumScreenSpaceError = Value;
  this->Refresh();
}

int32 UITwinCesiumRasterOverlay::GetMaximumTextureSize() const {
  return this->MaximumTextureSize;
}

void UITwinCesiumRasterOverlay::SetMaximumTextureSize(int32 Value) {
  this->MaximumTextureSize = Value;
  this->Refresh();
}

int32 UITwinCesiumRasterOverlay::GetMaximumSimultaneousTileLoads() const {
  return this->MaximumSimultaneousTileLoads;
}

void UITwinCesiumRasterOverlay::SetMaximumSimultaneousTileLoads(int32 Value) {
  this->MaximumSimultaneousTileLoads = Value;

  if (this->_pOverlay) {
    this->_pOverlay->getOptions().maximumSimultaneousTileLoads = Value;
  }
}

int64 UITwinCesiumRasterOverlay::GetSubTileCacheBytes() const {
  return this->SubTileCacheBytes;
}

void UITwinCesiumRasterOverlay::SetSubTileCacheBytes(int64 Value) {
  this->SubTileCacheBytes = Value;

  if (this->_pOverlay) {
    this->_pOverlay->getOptions().subTileCacheBytes = Value;
  }
}

void UITwinCesiumRasterOverlay::Activate(bool bReset) {
  Super::Activate(bReset);
  this->AddToTileset();
}

void UITwinCesiumRasterOverlay::Deactivate() {
  Super::Deactivate();
  this->RemoveFromTileset();
}

void UITwinCesiumRasterOverlay::OnComponentDestroyed(bool bDestroyingHierarchy) {
  this->RemoveFromTileset();
  Super::OnComponentDestroyed(bDestroyingHierarchy);
}

bool UITwinCesiumRasterOverlay::IsReadyForFinishDestroy() {
  bool ready = Super::IsReadyForFinishDestroy();
  ready &= this->_overlaysBeingDestroyed == 0;

  if (!ready) {
    getAssetAccessor()->tick();
    getAsyncSystem().dispatchMainThreadTasks();
  }

  return ready;
}

Cesium3DTilesSelection::Tileset* UITwinCesiumRasterOverlay::FindTileset() const {
  AITwinCesium3DTileset* pActor = this->GetOwner<AITwinCesium3DTileset>();
  if (!pActor) {
    return nullptr;
  }

  return pActor->GetTileset();
}
