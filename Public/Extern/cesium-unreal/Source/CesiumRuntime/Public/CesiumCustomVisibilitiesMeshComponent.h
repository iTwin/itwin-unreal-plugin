/*--------------------------------------------------------------------------------------+
|
|     $Source: CesiumCustomVisibilitiesMeshComponent.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"

#include <optional>

#include "CesiumCustomVisibilitiesMeshComponent.generated.h"

/// A mesh that can be forced hidden to override the mesh's visibility. Its parents' (to which the
/// component is attached) visibility is still complied to. Forcing to hide the mesh also hides its
/// attached sub-components.
UCLASS()
class CESIUMRUNTIME_API UCesiumCustomVisibilitiesMeshComponent : public UStaticMeshComponent {
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
