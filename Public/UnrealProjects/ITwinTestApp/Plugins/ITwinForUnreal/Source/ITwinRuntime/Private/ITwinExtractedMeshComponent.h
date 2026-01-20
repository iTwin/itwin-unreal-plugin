/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinExtractedMeshComponent.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"

#include <optional>

#include "ITwinExtractedMeshComponent.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
namespace ITwin
{
	UMaterialInstanceDynamic* ChangeBaseMaterialInUEMesh(UStaticMeshComponent& MeshComponent,
		UMaterialInterface* BaseMaterial,
		TWeakObjectPtr<UMaterialInstanceDynamic> const* SupposedPreviousMaterial = nullptr);
}

/// A mesh that can be forced hidden to override the mesh's visibility. Its parents' (to which the
/// component is attached) visibility is still complied to. Forcing to hide the mesh also hides its
/// attached sub-components.
UCLASS()
class UITwinExtractedMeshComponent : public UStaticMeshComponent {
	GENERATED_BODY()

public:
	void SetFullyHidden(bool bHidden);

	bool IsVisible() const override;
	bool IsVisibleInEditor() const override;

protected:
	virtual void OnVisibilityChanged() override;
private:
	std::optional<bool> FullyHidden;
};
