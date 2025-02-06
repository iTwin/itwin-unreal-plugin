/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinExtractedMeshComponent.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <Components/StaticMeshComponent.h>

#include <optional>

#include "ITwinExtractedMeshComponent.generated.h"

/// Mesh component created when extracting a specific iTwin element from a Cesium primitive.
/// The visibility of those meshes has to be overridden in some cases, to match the animation defined in
/// Synchro4D schedules.

UCLASS()
class UITwinExtractedMeshComponent : public UStaticMeshComponent {
	GENERATED_BODY()

public:
	UITwinExtractedMeshComponent();
	virtual ~UITwinExtractedMeshComponent();

	void SetFullyHidden(bool bHidden);

	bool IsVisible() const override;
	bool IsVisibleInEditor() const override;

protected:
	virtual void OnVisibilityChanged() override;
private:
	/// One may enforce hiding an extracted component, which should override the mesh visibility.
	std::optional<bool> FullyHidden;
};
