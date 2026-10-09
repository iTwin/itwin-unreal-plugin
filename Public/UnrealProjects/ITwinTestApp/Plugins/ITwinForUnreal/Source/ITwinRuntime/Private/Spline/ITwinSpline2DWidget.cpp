/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSpline2DWidget.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Spline/ITwinSpline2DWidget.h>

#include <Blueprint/WidgetBlueprintLibrary.h>
#include <Components/CanvasPanelSlot.h>
#include <Components/Widget.h>
#include <Kismet/KismetMathLibrary.h>
#include <Input/Reply.h>
#include <array>


struct UITwinSpline2DWidget::FImpl
{
	//! Use a distinct slot for each purpose - important when drawing uses just 2 samples, for cutouts, while
	//! hit testing requires more samples to be more accurate.
	std::array<TArray<FVector2D>, static_cast<size_t>(ITwin::ESplineSamplingPurpose::ENUM_END)> SampledOnScreenPositions;
};


UITwinSpline2DWidget::UITwinSpline2DWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Impl(MakePimpl<FImpl>())
{

}

void UITwinSpline2DWidget::SetStartAndEnd(const FITwinSplineChunk2DInfo& InChunk2DInfo)
{
	Chunk2DInfo = InChunk2DInfo;

	for (auto& SampledPositions : Impl->SampledOnScreenPositions)
	{
		SampledPositions.Reset();
	}
}

void UITwinSpline2DWidget::SetTint(const FLinearColor& InTint)
{
	Tint = InTint;
}

void UITwinSpline2DWidget::SetThickness(float InThickness)
{
	Thickness = InThickness;
}

FVector::FReal UITwinSpline2DWidget::DistancePointToSegmentSquared(
	const FVector2D& Point,
	const FVector2D& A,
	const FVector2D& B)
{
	const FVector2D AB = B - A;
	const FVector::FReal LenSq = AB.SizeSquared();
	if (LenSq <= KINDA_SMALL_NUMBER)
	{
		return FVector2D::DistSquared(Point, A);
	}

	const FVector::FReal T = FMath::Clamp(FVector2D::DotProduct(Point - A, AB) / LenSq, 0., 1.);
	const FVector2D Projection = A + T * AB;
	return FVector2D::DistSquared(Point, Projection);
}

bool UITwinSpline2DWidget::IsScreenPositionOverSpline(const FGeometry& Geometry, const FVector2D& ScreenPosition,
	FVector::FReal& OutDistanceSquaredToSpline,
	FVector2D& OutClosestPoint2D,
	const FVector::FReal ExtraTolerance /*= 0.*/) const
{
	const FVector2D LocalMouse = Geometry.AbsoluteToLocal(ScreenPosition);

	if (!ensure(HasCachedSplineSampling(ITwin::ESplineSamplingPurpose::HitTesting)))
	{
		return false;
	}

	const FVector::FReal HitRadius = FMath::Max(Thickness * 0.5 + ExtraTolerance, 4.0);
	const FVector::FReal HitRadiusSq = HitRadius * HitRadius;

	// Make sure we are not too close to the points (avoid conflict with selection...)
	// Do it by enforcing a minimum distance to the curve near the endpoints.
	const FVector::FReal MinDistanceFromPoints = 24.0;
	const FVector::FReal MinDistanceFromPointsSq = MinDistanceFromPoints * MinDistanceFromPoints;

	FVector::FReal MinDistanceSq = 10. * HitRadiusSq;
	FVector2D ClosestPoint2D = { -1., -1. };
	bool bIsOver = false;

	const TArray<FVector2D>& SampledPositions = GetCachedSplineSampling(ITwin::ESplineSamplingPurpose::HitTesting);
	FVector2D Prev = SampledPositions[0];
	for (int32 Step = 1; Step < SampledPositions.Num(); ++Step)
	{
		FVector2D Cur = SampledPositions[Step];
		auto const DistSq = DistancePointToSegmentSquared(LocalMouse, Prev, Cur);
		if (DistSq <= HitRadiusSq
			&& FVector2D::DistSquared(Chunk2DInfo.Start, Cur) > MinDistanceFromPointsSq
			&& FVector2D::DistSquared(Chunk2DInfo.End, Cur) > MinDistanceFromPointsSq)
		{
			if (DistSq < MinDistanceSq)
			{
				ClosestPoint2D = Cur;
				MinDistanceSq = DistSq;
			}
			bIsOver = true;
		}

		Prev = Cur;
	}

	if (bIsOver)
	{
		OutDistanceSquaredToSpline = MinDistanceSq;
		OutClosestPoint2D = ClosestPoint2D;
	}
	return bIsOver;
}

int32 UITwinSpline2DWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	FPaintContext Context = FPaintContext(AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (bDrawAsMultiLine)
	{
		if (HasCachedSplineSampling(ITwin::ESplineSamplingPurpose::Drawing))
		{
			UWidgetBlueprintLibrary::DrawLines(Context,
				GetCachedSplineSampling(ITwin::ESplineSamplingPurpose::Drawing),
				Tint,
				true /*bAntiAlias*/,
				Thickness);
		}
	}
	else
	{
		UWidgetBlueprintLibrary::DrawSpline(Context,
			Chunk2DInfo.Start, Chunk2DInfo.StartDir,
			Chunk2DInfo.End, Chunk2DInfo.EndDir,
			Tint, Thickness);
	}
	auto NewLayerId = FMath::Max(LayerId, Context.MaxLayer);
	return Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, NewLayerId, InWidgetStyle, bParentEnabled);
}


void UITwinSpline2DWidget::CacheSplineSampling(const TArray<FVector2D>& InSampledPositions, ITwin::ESplineSamplingPurpose Purpose)
{
	Impl->SampledOnScreenPositions[static_cast<size_t>(Purpose)] = InSampledPositions;
}

const TArray<FVector2D>& UITwinSpline2DWidget::GetCachedSplineSampling(ITwin::ESplineSamplingPurpose Purpose) const
{
	return Impl->SampledOnScreenPositions[static_cast<size_t>(Purpose)];
}
