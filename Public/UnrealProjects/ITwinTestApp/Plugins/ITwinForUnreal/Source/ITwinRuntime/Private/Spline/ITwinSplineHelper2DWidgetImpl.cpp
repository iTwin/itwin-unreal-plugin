/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSplineHelper2DWidgetImpl.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Spline/ITwinSplineHelper2DWidgetImpl.h>

#include <Spline/ITwinSpline2DWidget.h>
#include <Spline/ITwinSplineWithPin2DWidgetImpl.h>
#include <Spline/ITwinSplineHelper.h>
#include <Spline/ITwinSplineTool.h>
#include <Spline/ITwinUESplineCurve.h>
#include <Helpers/ITwinViewProjectionState.h>

#include <Blueprint/WidgetLayoutLibrary.h>
#include <Components/Border.h>
#include <Components/CanvasPanel.h>
#include <Components/CanvasPanelSlot.h>
#include <Components/Image.h>
#include <Components/Widget.h>
#include <Components/SplineComponent.h>
#include <GameFramework/PlayerController.h>
#include <Engine/Engine.h>
#include <Engine/GameViewportClient.h>

#include <optional>


#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeUtils/SplineSampling/SplineSampling.h>
#include <Compil/AfterNonUnrealIncludes.h>


namespace ITwin
{
	void TriggerCursorUpdate();
}

struct UITwinSplineHelper2DWidgetImpl::FImpl
{
	bool bIsMouseOverSpline = false;
	TWeakObjectPtr<UITwinSplineHelper2DWidgetImpl> SlaveBeingHovered;
	std::optional<FITwinViewProjectionState> LastViewProjectionState;


	bool DetectViewProjectionChange(const UWorld* World)
	{
		// Check if the view/projection matrix has changed since the last update, in which case we also need
		// to update the 2D spline widgets.
		FITwinViewProjectionState CurrentViewProjectionState;
		const bool bViewProjectionStateValid = ITwin::GetViewProjectionState(World, CurrentViewProjectionState);

		const bool bViewpointHasChanged = !LastViewProjectionState.has_value()
			|| !LastViewProjectionState->NearlyEquals(CurrentViewProjectionState);

		if (bViewProjectionStateValid && bViewpointHasChanged)
		{
			if (!LastViewProjectionState.has_value())
				LastViewProjectionState = CurrentViewProjectionState;
			else
				*LastViewProjectionState = CurrentViewProjectionState;
		}
		return bViewpointHasChanged;
	}
};

namespace
{
	static TSet<UITwinSplineHelper2DWidgetImpl*> sSlaveWidgets;
}

/*static*/
UITwinSplineHelper2DWidgetImpl* UITwinSplineHelper2DWidgetImpl::sMasterInstance = nullptr;

/*static*/
void UITwinSplineHelper2DWidgetImpl::SetMasterInstance(UITwinSplineHelper2DWidgetImpl* InMasterInstance)
{
	sMasterInstance = InMasterInstance;
}

/*static*/
void UITwinSplineHelper2DWidgetImpl::SetMasterInstanceVisibility(ESlateVisibility InVisibility)
{
	if (sMasterInstance)
	{
		sMasterInstance->SetVisibility(InVisibility);
	}
}

/*static*/
void UITwinSplineHelper2DWidgetImpl::RegisterSlaveWidget(UITwinSplineHelper2DWidgetImpl* InSlaveWidget)
{
	sSlaveWidgets.FindOrAdd(InSlaveWidget);
}

/*static*/
void UITwinSplineHelper2DWidgetImpl::UnregisterSlaveWidget(UITwinSplineHelper2DWidgetImpl* InSlaveWidget)
{
	if (sSlaveWidgets.Contains(InSlaveWidget))
	{
		// Before removing the slave widget from the manager, make sure we remove its spline-chunks
		// sub-widgets.
		if (IsValid(InSlaveWidget))
		{
			InSlaveWidget->EnsureSplineChunkWidgetCount(0);
		}

		sSlaveWidgets.Remove(InSlaveWidget);
	}
}

UITwinSplineHelper2DWidgetImpl::UITwinSplineHelper2DWidgetImpl(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Impl(MakePimpl<FImpl>())
{

}

void UITwinSplineHelper2DWidgetImpl::BeginDestroy()
{
	Super::BeginDestroy();

	if (this == sMasterInstance)
	{
		sMasterInstance = nullptr;
	}
	else
	{
		UnregisterSlaveWidget(this);
	}
}

void UITwinSplineHelper2DWidgetImpl::OnVisibilityUpdated()
{
	if (this != sMasterInstance)
	{
		// If this is a slave widget, we need to update the visibility of the spline chunk widgets in the master.
		auto const ThisVisibility = GetVisibility();
		for (UITwinSplineWithPin2DWidgetImpl* SplineChunkWidget : SplineChunkWidgets)
		{
			if (SplineChunkWidget)
			{
				SplineChunkWidget->SetVisibility(ThisVisibility);
			}
		}
		// If all slaves are hidden, we can hide the master as well.
		bool bHasVisibleSlaves = false;
		for (UITwinSplineHelper2DWidgetImpl* SlaveWidget : sSlaveWidgets)
		{
			// We cannot test 'IsVisible' here, as the result would depend on the master widget's visibility.
			if (SlaveWidget->GetVisibility() != ESlateVisibility::Collapsed
				&& SlaveWidget->GetVisibility() != ESlateVisibility::Hidden)
			{
				bHasVisibleSlaves = true;
				break;
			}
		}
		SetMasterInstanceVisibility(bHasVisibleSlaves ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UITwinSplineHelper2DWidgetImpl::NativeConstruct()
{
	Super::NativeConstruct();

	UpdateComponentsVisibility();
}

void UITwinSplineHelper2DWidgetImpl::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// The master instance is the only one that is actually ticking, and it will update all slave widgets as
	// needed.
	if (this == sMasterInstance)
	{
		for (UITwinSplineHelper2DWidgetImpl* SlaveWidget : sSlaveWidgets)
		{
			SlaveWidget->UpdateSplineWidgets();
		}
	}
	else
	{
		ensure(false); // Only the master instance should be ticking.
		UpdateSplineWidgets();
	}
}

void UITwinSplineHelper2DWidgetImpl::SetSplineHelper(AITwinSplineHelper* InSplineHelper)
{
	if (SplineHelper.Get() == InSplineHelper)
		return;

	SplineHelper = InSplineHelper;
	UpdateSplineWidgets();
}

void UITwinSplineHelper2DWidgetImpl::SetShowPins(bool bInShowPins)
{
	if (bShowPins == bInShowPins)
		return;

	bShowPins = bInShowPins;

	for (UITwinSplineWithPin2DWidgetImpl* SplineChunkWidget : SplineChunkWidgets)
	{
		if (SplineChunkWidget)
		{
			SplineChunkWidget->SetShowPins(bShowPins);
		}
	}
}

namespace
{
	UITwinSplineWithPin2DWidgetImpl::FUIColors MakeUIColorsFromTint(const FLinearColor& InTint)
	{
		UITwinSplineWithPin2DWidgetImpl::FUIColors Colors;
		Colors.LineColor = InTint;
		// For the interior, we want some contrast (lighter color if possible, else darker...)
		// Also adjust the hover/pressed color of the buttons for better contrast.
		FLinearColor ColorHSV_Interior = InTint.LinearRGBToHSV();
		FLinearColor ColorHSV_Hover = ColorHSV_Interior;
		FLinearColor ColorHSV_Pressed = ColorHSV_Interior;
		if (ColorHSV_Interior.B < 0.7f)
		{
			ColorHSV_Interior.B *= 2.f;

			ColorHSV_Hover.B *= 1.4f;
			ColorHSV_Pressed.B *= 1.2f;
		}
		else if (InTint.Equals(FLinearColor::White, 0.1f))
		{
			// If the color is very bright, we darken it.
			ColorHSV_Interior.B *= 0.5f;
			ColorHSV_Hover.B *= 0.2f;
			ColorHSV_Pressed.B *= 0.4f;
		}
		else
		{
			// If the color is bright enough, but not white, we set it to white.
			ColorHSV_Interior.G = 0.f;
			ColorHSV_Interior.B = 1.f;

			ColorHSV_Hover.B *= 0.2f;
			ColorHSV_Pressed.B *= 0.4f;
		}
		ColorHSV_Interior.B = FMath::Clamp(ColorHSV_Interior.B, 0.f, 1.f);
		ColorHSV_Hover.B = FMath::Clamp(ColorHSV_Hover.B, 0.f, 1.f);
		ColorHSV_Pressed.B = FMath::Clamp(ColorHSV_Pressed.B, 0.f, 1.f);

		Colors.PointInteriorColor = ColorHSV_Interior.HSVToLinearRGB();
		Colors.ButtonHoverColor = ColorHSV_Hover.HSVToLinearRGB();
		Colors.ButtonPressedColor = ColorHSV_Pressed.HSVToLinearRGB();

		return Colors;
	}
}

void UITwinSplineHelper2DWidgetImpl::SetTint(const FLinearColor& InTint)
{
	Tint = InTint;

	const auto UIColors = MakeUIColorsFromTint(InTint);

	for (UITwinSplineWithPin2DWidgetImpl* SplineChunkWidget : SplineChunkWidgets)
	{
		if (SplineChunkWidget)
		{
			SplineChunkWidget->SetUIColors(UIColors);
		}
	}
}

void UITwinSplineHelper2DWidgetImpl::SetThickness(float InThickness)
{
	Thickness = InThickness;

	for (UITwinSplineWithPin2DWidgetImpl* SplineChunkWidget : SplineChunkWidgets)
	{
		if (SplineChunkWidget)
		{
			SplineChunkWidget->SetSplineThickness(InThickness);
		}
	}
}

void UITwinSplineHelper2DWidgetImpl::UpdateComponentsVisibility()
{

}

namespace
{
	enum class EInputKeyDirection
	{
		Forward,
		Backward
	};
}

struct UITwinSplineHelper2DWidgetImpl::FScreenSpaceProjector
{
	const APlayerController* PC = nullptr;
	float ViewportScale = 1.0f;

	FScreenSpaceProjector(const UWorld* InWorld)
	{
		PC = InWorld ? InWorld->GetFirstPlayerController() : nullptr;
		ViewportScale = InWorld ? UWidgetLayoutLibrary::GetViewportScale(InWorld) : 1.0f;
		ViewportScale = FMath::Max(ViewportScale, 1e-2f);
	}

	inline bool ProjectWorldToScreen(FVector const& WorldPosition, FVector2D& OutScreenPosition) const
	{
		if (!PC)
			return false;

		FVector2D ViewportPosition;
		if (!PC->ProjectWorldLocationToScreen(WorldPosition, ViewportPosition))
			return false;

		// Convert viewport pixel coordinates to Slate / UMG coordinates.
		OutScreenPosition = ViewportPosition / ViewportScale;
		return true;
	}

	static void SetViewportSize(FVector2D const& InViewportSize)
	{
		ViewportSize = InViewportSize;
	}

	static bool IsInsideViewport(FVector2D const& ScreenPosition, double Tolerance = 200.)
	{
		BE_ASSERT(ViewportSize.X > 0 && ViewportSize.Y > 0);
		return ScreenPosition.X >= -Tolerance && ScreenPosition.X < ViewportSize.X + Tolerance &&
			ScreenPosition.Y >= -Tolerance && ScreenPosition.Y < ViewportSize.Y + Tolerance;
	}

	/// Returns whether the given screen tangent is long enough (compared to viewport size) to be considered
	/// as potentially instable.
	static bool IsDubious2DTangent(FVector2D const& ScreenTangent)
	{
		BE_ASSERT(ViewportSize.X > 0 && ViewportSize.Y > 0);

		// Check for NaN or infinite values
		if (!FMath::IsFinite(ScreenTangent.X) || !FMath::IsFinite(ScreenTangent.Y))
			return true;

		static constexpr double LongTangentRatio = 0.333;
		return FMath::Abs(ScreenTangent.X) >= ViewportSize.X * LongTangentRatio
			|| FMath::Abs(ScreenTangent.Y) >= ViewportSize.Y * LongTangentRatio;
	}

private:
	static FVector2D ViewportSize;
};

/*static*/ FVector2D UITwinSplineHelper2DWidgetImpl::FScreenSpaceProjector::ViewportSize = { -1., -1. };

struct UITwinSplineHelper2DWidgetImpl::FScreenSpaceTangentComputer
{
	const USplineComponent& SplineComponent;
	const FScreenSpaceProjector& Projector;
	const float StartInputKey, EndInputKey;

	FScreenSpaceTangentComputer(const USplineComponent& InSplineComponent, const FScreenSpaceProjector& InProjector,
		const float InStartInputKey, const float InEndInputKey)
		: SplineComponent(InSplineComponent)
		, Projector(InProjector)
		, StartInputKey(InStartInputKey)
		, EndInputKey(InEndInputKey)
	{
	}

	bool ComputeScreenSpaceTangent(FVector2D& Out2DTangent,
		const float InputKey, const FVector& In3DPoint, const FVector2D& In2DPoint,
		const EInputKeyDirection IKDirection) const
	{
		// Sample the spline itself near the segment endpoints in order to estimate the 2D Bezier handles.
		// This is more stable than projecting 3D tangents with an arbitrary scale factor.
		constexpr float Dt = 0.1f;

		float NeighborInputKey = InputKey;
		const bool bIncreasingDt = (IKDirection == EInputKeyDirection::Forward);
		if (bIncreasingDt)
		{
			NeighborInputKey = FMath::Min(InputKey + Dt, EndInputKey);
		}
		else
		{
			NeighborInputKey = FMath::Max(InputKey - Dt, StartInputKey);
		}

		const FVector NeighborWorld =
			SplineComponent.GetLocationAtSplineInputKey(NeighborInputKey, ESplineCoordinateSpace::World);

		FVector2D NeighborScreen;
		const bool bNeighborOk = Projector.ProjectWorldToScreen(NeighborWorld, NeighborScreen);
		if (!bNeighborOk)
			return false;
		const float EffectiveDt = FMath::Max(FMath::Abs(NeighborInputKey - InputKey), KINDA_SMALL_NUMBER)
			* (bIncreasingDt ? 1.f : -1.f);
		Out2DTangent = (NeighborScreen - In2DPoint) / EffectiveDt;
		return true;
	}

};

bool UITwinSplineHelper2DWidgetImpl::BuildScreenSpaceSplineChunk(const USplineComponent& SplineComponent,
	const int32 StartIndex, const int32 EndIndex,
	const FScreenSpaceProjector& Projector,
	FITwinSplineChunk2DInfo& OutChunk2DInfo,
	const bool bLinearTangents) const
{
	const int32 NumPoints = SplineComponent.GetNumberOfSplinePoints();
	BE_ASSERT(StartIndex >= 0 && StartIndex < NumPoints && EndIndex >= 0 && EndIndex < NumPoints);

	const float StartInputKey = static_cast<float>(StartIndex);
	float EndInputKey = static_cast<float>(EndIndex);

	// For a closed loop, the last chunk goes from N-1 to N, not N-1 to 0 in spline input-key space.
	if (SplineComponent.IsClosedLoop() && EndIndex <= StartIndex)
	{
		EndInputKey += static_cast<float>(NumPoints);
	}

	const FVector StartWorld =
		SplineComponent.GetLocationAtSplineInputKey(StartInputKey, ESplineCoordinateSpace::World);
	const FVector EndWorld =
		SplineComponent.GetLocationAtSplineInputKey(EndInputKey, ESplineCoordinateSpace::World);

	OutChunk2DInfo.bValidStart = Projector.ProjectWorldToScreen(StartWorld, OutChunk2DInfo.Start);
	OutChunk2DInfo.bValidEnd = Projector.ProjectWorldToScreen(EndWorld, OutChunk2DInfo.End);
	if (!OutChunk2DInfo.bValidStart && !OutChunk2DInfo.bValidEnd)
	{
		// If both points fail to project on screen, we will skip the whole chunk.
		return false;
	}

	if (bLinearTangents)
	{
		// Exact cubic Bezier handles for a straight segment:
		// P1 = P0 + (P3 - P0) / 3
		// P2 = P3 - (P3 - P0) / 3
		// DrawSpline expects handle vectors relative to endpoints.
		const FVector2D Delta = OutChunk2DInfo.End - OutChunk2DInfo.Start;
		OutChunk2DInfo.StartDir = Delta / 3.0f;
		OutChunk2DInfo.EndDir = -Delta / 3.0f;
		OutChunk2DInfo.bValidStartDir = OutChunk2DInfo.bValidEndDir = true;
		return true;
	};

	// Compute 2D tangents. Note that if they are very long compared to the viewport size, we may switch to
	// multi-line drawing.
	const FScreenSpaceTangentComputer TangentComputer(SplineComponent, Projector, StartInputKey, EndInputKey);
	OutChunk2DInfo.bValidStartDir = TangentComputer.ComputeScreenSpaceTangent(OutChunk2DInfo.StartDir, StartInputKey,
		StartWorld, OutChunk2DInfo.Start, EInputKeyDirection::Forward);
	OutChunk2DInfo.bValidEndDir = TangentComputer.ComputeScreenSpaceTangent(OutChunk2DInfo.EndDir, EndInputKey,
		EndWorld, OutChunk2DInfo.End, EInputKeyDirection::Backward);

	return true;
}

void UITwinSplineHelper2DWidgetImpl::EnsureSplineChunkWidgetCount(int32 DesiredCount)
{
	if (!RootCanvas)
		return;

	if (!SplineChunkClass && DesiredCount > 0)
	{
		SplineChunkClass = LoadClass<UITwinSplineWithPin2DWidgetImpl>(nullptr,
			TEXT("/Script/UMGEditor.WidgetBlueprint'/ITwinForUnreal/ITwin/Splines/ITwinSplineWithPin2DWidget.ITwinSplineWithPin2DWidget_C'"));
	}
	if (!SplineChunkClass)
		return;

	if (SplineChunkWidgets.Num() == DesiredCount)
		return;

	const auto UIColors = MakeUIColorsFromTint(GetTint());

	UITwinSplineHelper2DWidgetImpl* ContainerWidget = sMasterInstance ? sMasterInstance : this;

	while (SplineChunkWidgets.Num() < DesiredCount)
	{
		UITwinSplineWithPin2DWidgetImpl* NewWidget = CreateWidget<UITwinSplineWithPin2DWidgetImpl>(ContainerWidget, SplineChunkClass);
		if (!ensure(NewWidget))
			return;

		NewWidget->SetSplineChunkIndex(SplineChunkWidgets.Num());
		NewWidget->OnSplinePointPickedEvent.AddDynamic(this, &UITwinSplineHelper2DWidgetImpl::OnSplinePointPicked);
		NewWidget->SetShowPins(bShowPins);
		NewWidget->SetUIColors(UIColors);
		NewWidget->SetSplineThickness(GetThickness());
		NewWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

		UCanvasPanelSlot* CanvasSlot = ContainerWidget->RootCanvas->AddChildToCanvas(NewWidget);
		if (CanvasSlot)
		{
			CanvasSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			CanvasSlot->SetOffsets(FMargin(0.f, 0.f, 0.f, 0.f));
			CanvasSlot->SetAutoSize(false);
			CanvasSlot->SetZOrder(0);
		}

		SplineChunkWidgets.Add(NewWidget);
	}

	while (SplineChunkWidgets.Num() > DesiredCount)
	{
		if (UITwinSplineWithPin2DWidgetImpl* Widget = SplineChunkWidgets.Pop())
		{
			Widget->RemoveFromParent();
		}
	}
}

void UITwinSplineHelper2DWidgetImpl::ClearSplineChunkWidgets()
{
	for (UITwinSplineWithPin2DWidgetImpl* Widget : SplineChunkWidgets)
	{
		if (Widget)
		{
			Widget->RemoveFromParent();
		}
	}
	SplineChunkWidgets.Reset();
}

void UITwinSplineHelper2DWidgetImpl::UpdateSplineWidgets()
{
	if (GetVisibility() == ESlateVisibility::Collapsed
		|| GetVisibility() == ESlateVisibility::Hidden)
	{
		// With the new centralized system, UpdateSplineWidgets is now called from the master, so me must
		// test the visibility of this slave manually.
		return;
	}

	AITwinSplineHelper* SplineActor = SplineHelper.Get();
	if (!SplineActor || !SplineActor->GetSplineComponent())
	{
		ClearSplineChunkWidgets();
		return;
	}

	// Check if the spline data has changed since the last update, in which case we need to update the
	// screen-space spline chunks.
	const bool bNeedsUpdate2D = SplineActor->NeedsUpdate2DElements();

	// Check if the view/projection matrix has changed since the last update, in which case we also need
	// to update them.
	const bool bViewpointHasChanged = Impl->DetectViewProjectionChange(GetWorld());

	if (!bNeedsUpdate2D && !bViewpointHasChanged)
		return;

	const USplineComponent& SplineComponent = *SplineActor->GetSplineComponent();
	const int32 NumPoints = SplineComponent.GetNumberOfSplinePoints();
	const bool bClosedLoop = SplineComponent.IsClosedLoop();
	const int32 NumChunks = bClosedLoop ? NumPoints : FMath::Max(0, NumPoints - 1);
	const bool bInteractiveCreation = SplineActor->IsInteractiveCreationInProgress();
	const bool bLinearTangents = (SplineActor->GetTangentMode() == EITwinTangentMode::Linear);

	EnsureSplineChunkWidgetCount(NumChunks);

	FScreenSpaceProjector::SetViewportSize(UWidgetLayoutLibrary::GetViewportSize(this));
	FScreenSpaceProjector Projector(GetWorld());

	for (int32 ChunkIndex = 0; ChunkIndex < SplineChunkWidgets.Num(); ++ChunkIndex)
	{
		UITwinSplineWithPin2DWidgetImpl* Widget = SplineChunkWidgets[ChunkIndex];
		if (!Widget)
			continue;

		const int32 StartIndex = ChunkIndex;
		const int32 EndIndex = (ChunkIndex + 1) % NumPoints;

		FITwinSplineChunk2DInfo Chunk2DInfo;
		if (BuildScreenSpaceSplineChunk(SplineComponent, StartIndex, EndIndex, Projector,
										Chunk2DInfo, bLinearTangents))
		{
			// The end pin is only needed if the spline is open and this is the last chunk.
			Widget->SetShowEndPin(bShowPins && !bClosedLoop && EndIndex == NumChunks);

			Widget->SetStartAndEnd(Chunk2DInfo);

			bool bDrawAsMultiLine = false;
			if (Chunk2DInfo.AllValid())
			{
				// Even though both points could be projected, the tangents can be instable when the camera
				// is near a segment.
				// Note that it can even lead to crashes in DrawSpline if the tangents are too long, so we
				// must be careful here (see crash AzDev#2080574).
				bDrawAsMultiLine = Projector.IsDubious2DTangent(Chunk2DInfo.StartDir)
					|| Projector.IsDubious2DTangent(Chunk2DInfo.EndDir);
			}
			else
			{
				// If any of the projected points/tangents is invalid, we draw as multi-line.
				bDrawAsMultiLine = true;
			}

			// Fix artefacts for cutout splines with linear tangents, by enforcing multi-line drawing in
			// this case (see ADO#2104159).
			bool bEnforceMultiLineForLinearCase = false;
			if (bLinearTangents && !bDrawAsMultiLine)
			{
				bEnforceMultiLineForLinearCase = true;
				bDrawAsMultiLine = true;
			}

			if (bDrawAsMultiLine)
			{
				// Draw as multi-line to avoid absurd tangents (or even crashes!). In this case, we sample
				// the spline chunk at a higher resolution. If the sampling fails, the widget will simply
				// not draw anything, which is better than drawing a single segment with absurd tangents.
				SampleSplineChunkWidget(*Widget,
					bEnforceMultiLineForLinearCase ? 2 : 128 /*NumSubdivisions*/,
					ITwin::ESplineSamplingPurpose::Drawing);
			}
			UITwinSpline2DWidget* SplineWidget = Widget->GetSpline2DWidget();
			if (ensure(SplineWidget))
			{
				SplineWidget->SetDrawAsMultiLine(bDrawAsMultiLine);
			}

			Widget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

			bool bEnableStartButton = true;
			bool bEnableEndButton = true;
			// In interactive creation mode, only the first and last point can be interacted with, and
			// clicking on them should terminate the interactive creation.
			if (bInteractiveCreation)
			{
				// NB: we test N-2 below, as this is the last point actually validated by the user ; the last
				// point, during interactive drawing, is always following the mouse...
				bEnableStartButton = (ChunkIndex == 0 || ChunkIndex == NumPoints - 2);
				// The End button should never be enabled during interactive creation (of animation paths,
				// typically), or else the creation will always be aborted upon the 2nd click!
				bEnableEndButton = false;
			}
			Widget->EnableStartEndButtonInteractions(bEnableStartButton, bEnableEndButton);

			auto ChunkSlot = Cast<UCanvasPanelSlot>(Widget->Slot);
			ChunkSlot->SetZOrder(bEnableStartButton ? 1 : 0);
		}
		else
		{
			Widget->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	SplineActor->SetNeedsUpdate2DElements(false);
}

void UITwinSplineHelper2DWidgetImpl::OnSplinePointPicked(int32 PickedPointIndex)
{
	AITwinSplineHelper* SplineActor = SplineHelper.Get();
	if (!SplineActor)
		return;
	if (!ensure(PickedPointIndex >= 0 && PickedPointIndex < SplineActor->GetNumberOfSplinePoints()))
		return;
	if (SplineActor->IsInteractiveCreationInProgress())
	{
		// Let's terminate the interactive creation.
		AITwinSplineTool* SplineTool = ITwin::GetSplineTool(GetWorld());
		if (ensure(SplineTool && SplineTool->IsInteractiveCreationMode()))
		{
			SplineTool->ValidateInteractiveCreation(false /*bTriggeredFromITS*/);
		}
	}
	else
	{
		ITwin::SelectSpline(SplineActor, PickedPointIndex, GetWorld());
	}
}

bool UITwinSplineHelper2DWidgetImpl::SampleSplineChunkWidget(const UITwinSplineWithPin2DWidgetImpl& ChunkWidget,
	int32 NumSubdivisions,
	ITwin::ESplineSamplingPurpose Purpose) const
{
	// Only slave widgets should call this function, as the master has no spline helper to sample from.
	BE_ASSERT(this != sMasterInstance);

	UITwinSpline2DWidget* SplineWidget = ChunkWidget.GetSpline2DWidget();
	if (!SplineWidget)
		return false;

	TArray<FVector2D> SampledPositions;
	if (SampleSplineChunk(SampledPositions, ChunkWidget.GetSplineChunkIndex(), NumSubdivisions))
	{
		SplineWidget->CacheSplineSampling(SampledPositions, Purpose);
		return true;
	}
	else
	{
		return false;
	}
}

const UITwinSplineWithPin2DWidgetImpl* UITwinSplineHelper2DWidgetImpl::FindClosestSplineChunk(
	const FVector2D& ScreenPosition,
	FClosestImpactInfo& OutImpactInfo,
	FVector::FReal ExtraTolerance) const
{
	const UITwinSplineWithPin2DWidgetImpl* BestCandidate = nullptr;
	for (const UITwinSplineWithPin2DWidgetImpl* ChunkWidget : SplineChunkWidgets)
	{
		if (!ChunkWidget || ChunkWidget->GetVisibility() == ESlateVisibility::Collapsed)
			continue;

		UITwinSpline2DWidget* SplineWidget = ChunkWidget->GetSpline2DWidget();
		if (!SplineWidget || SplineWidget->GetVisibility() == ESlateVisibility::Collapsed)
			continue;

		const FGeometry& SplineGeometry = SplineWidget->GetCachedGeometry();
		FVector::FReal DistanceSq = 1e5;
		FVector2D ClosestPoint2D = { -1., -1. };

		// First sample the corresponding spline chunk if needed.
		if (!SplineWidget->HasCachedSplineSampling(ITwin::ESplineSamplingPurpose::HitTesting)
			&& !SampleSplineChunkWidget(*ChunkWidget,
										96 /*NumSubdivisions*/,
										ITwin::ESplineSamplingPurpose::HitTesting))
		{
			continue;
		}

		// Then test the distance to the mouse position.
		if (SplineWidget->IsScreenPositionOverSpline(SplineGeometry, ScreenPosition,
			DistanceSq, ClosestPoint2D,
			ExtraTolerance))
		{
			if (OutImpactInfo.HasNewClosestImpact(ClosestPoint2D, DistanceSq))
			{
				BestCandidate = ChunkWidget;
			}
		}
	}

	return BestCandidate;
}


const UITwinSplineWithPin2DWidgetImpl* UITwinSplineHelper2DWidgetImpl::FindSplineChunkUnderMouse(
	const FPointerEvent& InMouseEvent,
	FVector2D& OutClosestPoint2D,
	FVector::FReal ExtraTolerance /*= 0.*/) const
{
	OutClosestPoint2D = { -1., -1. };

	const FVector2D ScreenPosition = InMouseEvent.GetScreenSpacePosition();
	FClosestImpactInfo ClosestImpactInfo;
	const UITwinSplineWithPin2DWidgetImpl* ChunkWidget =
		FindClosestSplineChunk(ScreenPosition, ClosestImpactInfo, ExtraTolerance);
	if (ChunkWidget && ensure(ClosestImpactInfo.ClosestPoint2D))
	{
		OutClosestPoint2D = *ClosestImpactInfo.ClosestPoint2D;
	}
	return ChunkWidget;
}

inline bool UITwinSplineHelper2DWidgetImpl::IsPointInsertionAllowed() const
{
	// The spline should meet the requirements for point edition, but also be fully constructed (not in
	// interactive creation mode).
	AITwinSplineHelper const* SplineActor = SplineHelper.Get();
	return SplineActor
		&& SplineActor->IsPointEditionAllowed()
		&& !SplineActor->IsInteractiveCreationInProgress();
}

FReply UITwinSplineHelper2DWidgetImpl::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const bool bOverSplineOld(Impl->bIsMouseOverSpline);

	if (ensure(this == sMasterInstance))
	{
		bool bIsMouseOverSpline = false;
		// Test if mouse if over a spline chunk, and if so, change the cursor to indicate that the user can
		// click to select the chunk (and insert a point in the spline).
		FVector2D ClosestPoint2D = { -1., -1. };

		const FVector2D ScreenPosition = InMouseEvent.GetScreenSpacePosition();
		FClosestImpactInfo ClosestImpactInfo;

		UITwinSplineHelper2DWidgetImpl* SlaveBeingHovered = nullptr;
		for (UITwinSplineHelper2DWidgetImpl* SlaveWidget : sSlaveWidgets)
		{
			if (SlaveWidget->IsPointInsertionAllowed()
				&& SlaveWidget->FindClosestSplineChunk(ScreenPosition, ClosestImpactInfo, 2.0f))
			{
				bIsMouseOverSpline = true;
				SlaveBeingHovered = SlaveWidget;
			}
		}
		Impl->bIsMouseOverSpline = bIsMouseOverSpline;
		Impl->SlaveBeingHovered = bIsMouseOverSpline ? SlaveBeingHovered : nullptr;
	}

	const FReply Reply = Super::NativeOnMouseMove(InGeometry, InMouseEvent);

	if (bOverSplineOld != Impl->bIsMouseOverSpline)
	{
		// Make sure the new cursor will actually change.
		// (in packaged builds, the cursor may not update automatically when hovering a widget, so we need
		// to trigger an update manually).
		ITwin::TriggerCursorUpdate();
	}

	return Reply;
}

FCursorReply UITwinSplineHelper2DWidgetImpl::NativeOnCursorQuery(const FGeometry& InGeometry, const FPointerEvent& InCursorEvent)
{
	return (Impl->bIsMouseOverSpline)
		? FCursorReply::Cursor(EMouseCursor::Crosshairs)
		: FCursorReply::Unhandled();
}

FVector UITwinSplineHelper2DWidgetImpl::GetClosestPointOnSplineMatching2D(
	const USplineComponent& SplineComponent,
	const FVector2D& SplinePoint2D,
	int32 ChunkIndex) const
{
	FScreenSpaceProjector Projector(GetWorld());

	FVector ClosestWorldPosition = FVector::ZeroVector;
	FVector2D::FReal ClosestDistSquared = std::numeric_limits<FVector2D::FReal>::max();
	bool bFoundValidProjection = false;

	constexpr int32 NumSubdiv = 128;
	const float fInvSubdiv = 1.0f / static_cast<float>(NumSubdiv);

	float CurInputKey = static_cast<float>(ChunkIndex);
	for (int32 i = 0; i < NumSubdiv; ++i, CurInputKey += fInvSubdiv)
	{
		FVector WorldPosition = SplineComponent.GetLocationAtSplineInputKey(CurInputKey, ESplineCoordinateSpace::World);
		FVector2D PointScreen;
		if (Projector.ProjectWorldToScreen(WorldPosition, PointScreen))
		{
			auto const DistSq2D = FVector2D::DistSquared(PointScreen, SplinePoint2D);
			if (DistSq2D < ClosestDistSquared)
			{
				ClosestWorldPosition = WorldPosition;
				ClosestDistSquared = DistSq2D;
				bFoundValidProjection = true;
			}
		}
	}
	if (!ensure(bFoundValidProjection))
	{
		// By default, take the middle of the spline chunk.
		ClosestWorldPosition = SplineComponent.GetLocationAtSplineInputKey(
			static_cast<float>(ChunkIndex) + 0.5f,
			ESplineCoordinateSpace::World);
	}
	return ClosestWorldPosition;
}

FReply UITwinSplineHelper2DWidgetImpl::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	BE_ASSERT(this == sMasterInstance);

	bool bHasInsertedPoint = false;
	if (Impl->bIsMouseOverSpline
		&& Impl->SlaveBeingHovered.IsValid()
		&& Impl->SlaveBeingHovered->IsPointInsertionAllowed())
	{
		UITwinSplineHelper2DWidgetImpl* TargetWidget = Impl->SlaveBeingHovered.Get();
		FVector2D ClosestPoint2D = { -1., -1. };
		const UITwinSplineWithPin2DWidgetImpl* PickedChunk =
			TargetWidget->FindSplineChunkUnderMouse(InMouseEvent, ClosestPoint2D, 2.0f);

		if (PickedChunk && ensure(PickedChunk->GetSplineChunkIndex() >= 0))
		{
			AITwinSplineHelper* SplineActor = TargetWidget->SplineHelper.Get();
			const USplineComponent* SplineComp = SplineActor ? SplineActor->GetSplineComponent() : nullptr;
			AITwinSplineTool* SplineTool = ITwin::GetSplineTool(GetWorld());
			if (ensure(SplineTool && !SplineTool->IsInteractiveCreationMode() && SplineComp))
			{
				// Get the 3D point on curve corresponding to the closest 2D point, and insert a new point
				// in the spline at this position.
				const FVector WorldPosition = TargetWidget->GetClosestPointOnSplineMatching2D(*SplineComp, ClosestPoint2D,
					PickedChunk->GetSplineChunkIndex());
				bHasInsertedPoint = SplineTool->InsertPointAt(SplineActor,
					PickedChunk->GetSplineChunkIndex() + 1,
					WorldPosition);
			}
		}
	}
	if (bHasInsertedPoint)
		return FReply::Handled();
	else
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

bool UITwinSplineHelper2DWidgetImpl::SampleSplineChunk(TArray<FVector2D>& OutSampledPositions,
	int32 ChunkIndex,
	int32 NumSubdivisions) const
{
	// Only slave widgets should call this function, as the master has no spline helper to sample from.
	BE_ASSERT(this != sMasterInstance);

	if (!SplineHelper.IsValid())
		return false;
	const USplineComponent* SplineComponent = SplineHelper->GetSplineComponent();
	if (!SplineComponent)
		return false;

	FITwinUESplineChunkCurve const Curve(*SplineComponent, ChunkIndex);

	BeUtils::SplineSamplingParameters SamplingParams;
	SamplingParams.samplingMode = BeUtils::ESplineSamplingMode::AlongPath;
	SamplingParams.fixedNbInstances = NumSubdivisions;

	// The transformation to world is already baked in FITwinUESplineChunkCurve.
	BeUtils::TransformHolder const IdentityTsf;

	static constexpr double InvalidProjectionValue = -100000.;

	/// Adapts a 3D world-to-screen projector (like FScreenSpaceProjector) to the Spline2DProjector interface
	/// expected by BeUtils::SampleSplinePath.
	class OnScreen2DProjector final : public BeUtils::Spline2DProjector
	{
	public:
		OnScreen2DProjector(const UWorld* InWorld)
			: Projector(InWorld)
		{}

		virtual bool MayFail() const final { return true; }

		virtual std::optional<vec2_type> Project2DOpt(vec3_type const& pos) const final
		{
			FVector2D ScreenPos;
			if (Projector.ProjectWorldToScreen(FVector(pos.x, pos.y, pos.z), ScreenPos)
				&& Projector.IsInsideViewport(ScreenPos))
			{
				return vec2_type(ScreenPos.X, ScreenPos.Y);
			}
			else
			{
				return std::nullopt;
			}
		}

		virtual std::optional<vec3_type> Project3DOpt(vec3_type const& pos) const final
		{
			std::optional<vec2_type> Proj2D = Project2DOpt(pos);
			if (Proj2D)
			{
				return vec3_type(*Proj2D, 0.);
			}
			else
			{
				return std::nullopt;
			}
		}

		virtual vec2_type Project2D(vec3_type const& pos) const final
		{
			return Project2DOpt(pos).value_or(vec2_type(InvalidProjectionValue, InvalidProjectionValue));
		}

		virtual vec3_type Project3D(vec3_type const& pos) const final
		{
			return vec3_type(Project2D(pos), 0.);
		}

		virtual BeUtils::E2DProjection GetProjection() const final
		{
			return BeUtils::E2DProjection::Z_Axis;
		}

		const FScreenSpaceProjector Projector;
	};
	SamplingParams.customProjector = std::make_unique<OnScreen2DProjector>(GetWorld());

	std::vector<glm::dvec3> Positions;
	BeUtils::SampleSplinePath(Curve, IdentityTsf, SamplingParams, Positions);
	if (Positions.empty())
	{
		return false;
	}

	OutSampledPositions.Reserve(Positions.size());
	for (const auto& Pos : Positions)
	{
		// Invalid values should be filtered out by the custom projector.
		BE_ASSERT(Pos.x > InvalidProjectionValue);
		OutSampledPositions.Emplace(FVector2D{ Pos.x, Pos.y });
	}
	return !OutSampledPositions.IsEmpty();
}

/*static*/
AITwinSplineHelper* UITwinSplineHelper2DWidgetImpl::FindClosestSplineToScreenPosition(const FVector2D& ScreenPosition,
	FVector::FReal& OutClosestDistance,
	const TFunction<bool(const AITwinSplineHelper&)>& IgnoreSpline)
{
	FClosestImpactInfo ClosestImpactInfo;
	AITwinSplineHelper* BestCandidate = nullptr;
	for (UITwinSplineHelper2DWidgetImpl* SlaveWidget : sSlaveWidgets)
	{
		if (IgnoreSpline && SlaveWidget->SplineHelper.IsValid()
			&& IgnoreSpline(*SlaveWidget->SplineHelper))
		{
			continue;
		}
		// We cannot test 'IsVisible' here, as the result would depend on the master widget's visibility.
		if (SlaveWidget->FindClosestSplineChunk(ScreenPosition, ClosestImpactInfo, 200.0 /*ExtraTolerance*/))
		{
			BestCandidate = SlaveWidget->SplineHelper.Get();
		}
	}
	if (ClosestImpactInfo.ClosestDistanceSquared)
	{
		OutClosestDistance = FMath::Sqrt(*ClosestImpactInfo.ClosestDistanceSquared);
	}
	else
	{
		OutClosestDistance = -1.;
	}
	return BestCandidate;
}
