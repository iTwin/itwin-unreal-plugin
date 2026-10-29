/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSelectionOutlineViewExtension.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <SceneViewExtension.h>

/// Draws the selection outline (see ITwinSelectionOutlineRenderer.h) right after tone-mapping, from the
/// custom stencil buffer.
class FITwinSelectionOutlineViewExtension : public FSceneViewExtensionBase
{
public:
	FITwinSelectionOutlineViewExtension(FAutoRegister const& AutoRegister);

	/// Create (once) and register the extension with the engine. Game thread only.
	static void EnsureCreated();
	/// Release the engine registration (called at module shutdown).
	static void Shutdown();

	//! ISceneViewExtension
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, FSceneView const& InView,
		FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;
	virtual bool IsActiveThisFrame_Internal(FSceneViewExtensionContext const& Context) const override;

private:
	FScreenPassTexture RenderOutline_RenderThread(FRDGBuilder& GraphBuilder, FSceneView const& View,
												  FPostProcessMaterialInputs const& Inputs);

	static TSharedPtr<FITwinSelectionOutlineViewExtension, ESPMode::ThreadSafe> Instance;
};
