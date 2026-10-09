/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPickingEdMode.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#if WITH_EDITOR

#include <EdMode.h>
#include <EditorModes.h>

/// Editor-only mode used to reuse the gameplay-side picking logic (AITwinPickingActor) when
/// left-clicking in the level editor viewport, outside of Play-In-Editor.
class FITwinPickingEdMode : public FEdMode
{
public:
	static const FEditorModeID EM_ITwinPicking;

	FITwinPickingEdMode() = default;
	virtual ~FITwinPickingEdMode() override = default;

	// FEdMode interface
	virtual bool UsesToolkits() const override { return false; }
	virtual bool InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport,
		FKey InKey, EInputEvent InEvent) override;
	// End of FEdMode interface
};

#endif // WITH_EDITOR
