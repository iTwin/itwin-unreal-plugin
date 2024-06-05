#include "ITwinCesiumTileExcluder.h"
#include "ITwinCesium3DTileset.h"
#include "ITwinCesiumLifetime.h"
#include "ITwinCesiumTileExcluderAdapter.h"

using namespace Cesium3DTilesSelection;

namespace {
auto findExistingExcluder(
    const std::vector<std::shared_ptr<ITileExcluder>>& excluders,
    const CesiumTileExcluderAdapter& excluder) {
  return std::find_if(
      excluders.begin(),
      excluders.end(),
      [&excluder](const std::shared_ptr<ITileExcluder>& pCandidate) {
        return pCandidate.get() == &excluder;
      });
}
} // namespace

UITwinCesiumTileExcluder::UITwinCesiumTileExcluder(
    const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer) {
  PrimaryComponentTick.bCanEverTick = false;
  bAutoActivate = true;
}

void UITwinCesiumTileExcluder::AddToTileset() {
  AITwinCesium3DTileset* CesiumTileset = this->GetOwner<AITwinCesium3DTileset>();
  if (!CesiumTileset)
    return;
  Tileset* pTileset = CesiumTileset->GetTileset();
  if (!pTileset)
    return;

  std::vector<std::shared_ptr<ITileExcluder>>& excluders =
      pTileset->getOptions().excluders;

  auto it = findExistingExcluder(excluders, *this->pExcluderAdapter);
  if (it != excluders.end())
    return;

  CesiumTile = NewObject<UITwinCesiumTile>(this);
  CesiumTile->SetVisibility(false);
  CesiumTile->SetMobility(EComponentMobility::Movable);
  CesiumTile->SetFlags(
      RF_Transient | RF_DuplicateTransient | RF_TextExportTransient);
  CesiumTile->SetupAttachment(CesiumTileset->GetRootComponent());
  CesiumTile->RegisterComponent();

  auto pAdapter = std::make_shared<CesiumTileExcluderAdapter>(
      TWeakObjectPtr<UITwinCesiumTileExcluder>(this),
      CesiumTileset->ResolveGeoreference(),
      CesiumTile);
  pExcluderAdapter = pAdapter.get();
  excluders.push_back(std::move(pAdapter));
}

void UITwinCesiumTileExcluder::RemoveFromTileset() {
  AITwinCesium3DTileset* CesiumTileset = this->GetOwner<AITwinCesium3DTileset>();
  if (!CesiumTileset)
    return;
  Tileset* pTileset = CesiumTileset->GetTileset();
  if (!pTileset)
    return;

  std::vector<std::shared_ptr<ITileExcluder>>& excluders =
      pTileset->getOptions().excluders;

  auto it = findExistingExcluder(excluders, *pExcluderAdapter);
  if (it != excluders.end()) {
    excluders.erase(it);
  }

  CesiumLifetime::destroyComponentRecursively(CesiumTile);
}

void UITwinCesiumTileExcluder::Refresh() {
  this->RemoveFromTileset();
  this->AddToTileset();
}

bool UITwinCesiumTileExcluder::ShouldExclude_Implementation(
    const UITwinCesiumTile* TileObject) {
  return false;
}

void UITwinCesiumTileExcluder::Activate(bool bReset) {
  Super::Activate(bReset);
  this->AddToTileset();
}

void UITwinCesiumTileExcluder::Deactivate() {
  Super::Deactivate();
  this->RemoveFromTileset();
}

void UITwinCesiumTileExcluder::OnComponentDestroyed(bool bDestroyingHierarchy) {
  this->RemoveFromTileset();
  Super::OnComponentDestroyed(bDestroyingHierarchy);
}

#if WITH_EDITOR
// Called when properties are changed in the editor
void UITwinCesiumTileExcluder::PostEditChangeProperty(
    FPropertyChangedEvent& PropertyChangedEvent) {
  Super::PostEditChangeProperty(PropertyChangedEvent);

  this->RemoveFromTileset();
  this->AddToTileset();
}
#endif
