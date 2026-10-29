/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSelectionOutlineViewExtension.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinSelectionOutlineViewExtension.h"

#include <ITwinSelectionOutlineRenderer.h>

#include <FXRenderingUtils.h>
#include <GlobalShader.h>
#include <PostProcess/PostProcessMaterialInputs.h>
#include <RenderGraphBuilder.h>
#include <RenderGraphUtils.h>
#include <RenderingThread.h>
#include <RHIStaticStates.h>
#include <SceneRenderTargetParameters.h>
#include <SceneView.h>
#include <ScreenPass.h>
#include <ShaderParameterStruct.h>

#include <HAL/ThreadSafeCounter.h>

#if WITH_EDITOR
#include <Misc/AutomationTest.h>
#endif

namespace
{
	FThreadSafeCounter ActiveSources;

	/// Pass 1 (rendering resolution): selected mask + vertical distance to the silhouette, per pixel
	class FITwinSelectionDistancePS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FITwinSelectionDistancePS);
		SHADER_USE_PARAMETER_STRUCT(FITwinSelectionDistancePS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Scene)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<uint2>, CustomStencilTexture)
			SHADER_PARAMETER(uint32, StencilValue)
			RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	/// Pass 2 (output resolution): horizontal half of the distance transform + outline compositing
	class FITwinSelectionOutlinePS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FITwinSelectionOutlinePS);
		SHADER_USE_PARAMETER_STRUCT(FITwinSelectionOutlinePS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Input)
			SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Scene)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, InputTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MaskAndDistTexture)
			RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FITwinSelectionDistancePS,
		"/Plugin/ITwinForUnreal/ITwin/ITwinSelectionOutline.usf", "VerticalDistancePS", SF_Pixel);
	IMPLEMENT_GLOBAL_SHADER(FITwinSelectionOutlinePS,
		"/Plugin/ITwinForUnreal/ITwin/ITwinSelectionOutline.usf", "MainPS", SF_Pixel);

}

namespace ITwin::SelectionOutline
{
	bool IsEnabled()
	{
		return true;
	}

	void AddActiveSource()
	{
		check(IsInGameThread());
		ActiveSources.Increment();
		FITwinSelectionOutlineViewExtension::EnsureCreated();
	}

	void RemoveActiveSource()
	{
		check(IsInGameThread());
		if (ensureMsgf(ActiveSources.GetValue() > 0, TEXT("RemoveActiveSource called without a matching AddActiveSource")))
			ActiveSources.Decrement();
	}

	int32 GetActiveSourceCount()
	{
		return ActiveSources.GetValue();
	}
}

TSharedPtr<FITwinSelectionOutlineViewExtension, ESPMode::ThreadSafe> FITwinSelectionOutlineViewExtension::Instance;

FITwinSelectionOutlineViewExtension::FITwinSelectionOutlineViewExtension(FAutoRegister const& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

/*static*/
void FITwinSelectionOutlineViewExtension::EnsureCreated()
{
	if (!Instance)
		Instance = FSceneViewExtensions::NewExtension<FITwinSelectionOutlineViewExtension>();
}

/*static*/
void FITwinSelectionOutlineViewExtension::Shutdown()
{
	Instance.Reset();
}

bool FITwinSelectionOutlineViewExtension::IsActiveThisFrame_Internal(FSceneViewExtensionContext const&) const
{
	return ActiveSources.GetValue() > 0;
}

void FITwinSelectionOutlineViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass Pass,
	FSceneView const& InView, FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	// Right after tone-mapping, so that the outline color is not altered by exposure/tone-mapping
	if (Pass == EPostProcessingPass::Tonemap && bIsPassEnabled && !InView.bIsSceneCapture
		&& ActiveSources.GetValue() > 0
		&& IsFeatureLevelSupported(InView.GetShaderPlatform(), ERHIFeatureLevel::SM5))
	{
		InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(this,
			&FITwinSelectionOutlineViewExtension::RenderOutline_RenderThread));
	}
}

FScreenPassTexture FITwinSelectionOutlineViewExtension::RenderOutline_RenderThread(FRDGBuilder& GraphBuilder,
	FSceneView const& View, FPostProcessMaterialInputs const& Inputs)
{
	FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder,
		Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
	if (!SceneColor.IsValid() || !Inputs.SceneTextures.SceneTextures)
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}
	FSceneTextureUniformParameters const& SceneTextures = *Inputs.SceneTextures.SceneTextures->GetContents();
	if (!SceneTextures.SceneDepthTexture || !SceneTextures.CustomStencilTexture)
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}
	// When this is the last pass of the chain, we must write to the provided output
	FScreenPassRenderTarget Output = Inputs.OverrideOutput;
	if (!Output.IsValid())
	{
		Output = FScreenPassRenderTarget::CreateFromInput(GraphBuilder, SceneColor, View.GetOverwriteLoadAction(),
														  TEXT("ITwinSelectionOutline"));
	}
	FScreenPassTextureViewport const InputViewport(SceneColor);
	FScreenPassTextureViewport const OutputViewport(Output);
	// Scene depth/stencil textures are at rendering resolution (screen percentage), whereas the scene color
	// here may already have been upscaled: map through normalized viewport coordinates in the shader.
	FScreenPassTextureViewport const SceneViewport(SceneTextures.SceneDepthTexture,
												   UE::FXRenderingUtils::GetRawViewRectUnsafe(View));

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);

	// Pass 1: separable distance transform, vertical half (at rendering resolution)
	FRDGTextureRef MaskAndDistTexture = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(SceneTextures.SceneDepthTexture->Desc.Extent, PF_G16R16F,
			FClearValueBinding::None, TexCreate_RenderTargetable | TexCreate_ShaderResource),
		TEXT("ITwinSelectionMaskAndDist"));
	{
		TShaderMapRef<FITwinSelectionDistancePS> PixelShader(ShaderMap);
		auto* Params = GraphBuilder.AllocParameters<FITwinSelectionDistancePS::FParameters>();
		Params->Scene = GetScreenPassTextureViewportParameters(SceneViewport);
		Params->CustomStencilTexture = SceneTextures.CustomStencilTexture;
		Params->StencilValue = static_cast<uint32>(ITwin::SelectionOutline::StencilValue);
		Params->RenderTargets[0] = FRenderTargetBinding(MaskAndDistTexture, ERenderTargetLoadAction::ENoAction);
		AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("ITwinSelectionDistance"), FScreenPassViewInfo(View),
			SceneViewport, SceneViewport, VertexShader, PixelShader, Params);
	}

	// Pass 2: horizontal half of the distance transform + compositing (at output resolution)
	{
		TShaderMapRef<FITwinSelectionOutlinePS> PixelShader(ShaderMap);
		auto* Params = GraphBuilder.AllocParameters<FITwinSelectionOutlinePS::FParameters>();
		Params->Input = GetScreenPassTextureViewportParameters(InputViewport);
		Params->Scene = GetScreenPassTextureViewportParameters(SceneViewport);
		Params->InputTexture = SceneColor.Texture;
		Params->InputSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Params->MaskAndDistTexture = MaskAndDistTexture;
		Params->RenderTargets[0] = Output.GetRenderTargetBinding();
		AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("ITwinSelectionOutline"), FScreenPassViewInfo(View),
			OutputViewport, InputViewport, VertexShader, PixelShader, Params);
	}
	return MoveTemp(Output);
}

#if WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FITwinSelectionOnlySchedulingTest,
	"Bentley.ITwinForUnreal.ITwinRendering.SelectionOnlyScheduling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinSelectionOnlySchedulingTest::RunTest(FString const&)
{
	auto Extension = FSceneViewExtensions::NewExtension<FITwinSelectionOutlineViewExtension>();
	FSceneViewFamilyContext MainFamily(FSceneViewFamily::ConstructionValues(
		nullptr, nullptr, FEngineShowFlags(ESFIM_Game)).SetTime(FGameTime()));
	FSceneViewFamilyContext CaptureFamily(FSceneViewFamily::ConstructionValues(
		nullptr, nullptr, FEngineShowFlags(ESFIM_Game)).SetTime(FGameTime()));
	FSceneViewInitOptions MainOptions;
	MainOptions.ViewFamily = &MainFamily;
	MainOptions.SetViewRectangle(FIntRect(0, 0, 32, 32));
	MainOptions.ProjectionMatrix = FReversedZPerspectiveMatrix(PI / 4., 32., 32., 1.);
	FSceneView MainView(MainOptions);
	MainFamily.Views.Add(&MainView);
	FSceneViewInitOptions CaptureOptions;
	CaptureOptions.ViewFamily = &CaptureFamily;
	CaptureOptions.SetViewRectangle(FIntRect(0, 0, 32, 32));
	CaptureOptions.ProjectionMatrix = MainOptions.ProjectionMatrix;
	FSceneView CaptureView(CaptureOptions);
	CaptureView.bIsSceneCapture = true;
	CaptureFamily.Views.Add(&CaptureView);
	ITwin::SelectionOutline::AddActiveSource();
	int32 MainCallbacks = 0;
	int32 CaptureCallbacks = 0;
	int32 DisabledPassCallbacks = 0;
	ENQUEUE_RENDER_COMMAND(ITwinHoverSchedulingTest)([&](FRHICommandListImmediate& RHICmdList)
	{
		FRDGBuilder GraphBuilder(RHICmdList);
		FPostProcessingPassDelegateArray Callbacks;
		Extension->SubscribeToPostProcessingPass(ISceneViewExtension::EPostProcessingPass::Tonemap,
			MainView, Callbacks, true);
		MainCallbacks = Callbacks.Num();
		Callbacks.Reset();
		Extension->SubscribeToPostProcessingPass(ISceneViewExtension::EPostProcessingPass::Tonemap,
			CaptureView, Callbacks, true);
		CaptureCallbacks = Callbacks.Num();
		Callbacks.Reset();
		Extension->SubscribeToPostProcessingPass(ISceneViewExtension::EPostProcessingPass::Tonemap,
			MainView, Callbacks, false);
		DisabledPassCallbacks = Callbacks.Num();
		GraphBuilder.Execute();
	});
	FlushRenderingCommands();
	ITwin::SelectionOutline::RemoveActiveSource();
	int32 NoSourceCallbacks = -1;
	ENQUEUE_RENDER_COMMAND(ITwinSelectionOutlineNoSourceSchedulingTest)([&](FRHICommandListImmediate&)
	{
		FPostProcessingPassDelegateArray Callbacks;
		Extension->SubscribeToPostProcessingPass(ISceneViewExtension::EPostProcessingPass::Tonemap,
			MainView, Callbacks, true);
		NoSourceCallbacks = Callbacks.Num();
	});
	FlushRenderingCommands();
	MainFamily.Views.Reset();
	CaptureFamily.Views.Reset();
	TestEqual(TEXT("Main view subscribes after tonemapping"), MainCallbacks, 1);
	TestEqual(TEXT("Capture does not subscribe"), CaptureCallbacks, 0);
	TestEqual(TEXT("Disabled tonemap pass does not subscribe"), DisabledPassCallbacks, 0);
	TestEqual(TEXT("No view subscribes when there is no selected source"), NoSourceCallbacks, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FITwinMaterialHoverControlsTest,
	"Bentley.ITwinForUnreal.ITwinRendering.MaterialHoverControls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinMaterialHoverControlsTest::RunTest(FString const&)
{
	for (auto const* Name : { TEXT("ITwin.Hover.MaterialShift"), TEXT("ITwin.Hover.MaterialDarkenNearWhite"),
		TEXT("ITwin.Hover.Mode"), TEXT("ITwin.Hover.LightnessShift"),
		TEXT("ITwin.Hover.TraceFrames"), TEXT("ITwin.Hover.Diagnose") })
		TestNull(Name, IConsoleManager::Get().FindConsoleObject(Name));
	return true;
}

#endif
