/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSpline2DWidget.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include <Templates/PimplPtr.h>

#include "ITwinSpline2DWidget.generated.h"

class USplineComponent;

/// Information about a 2D spline chunk, used for hit testing and drawing the spline in 2D.
USTRUCT(BlueprintType)
struct FITwinSplineChunk2DInfo
{
	GENERATED_BODY()

	/// Starting position of the spline in screen space.
	UPROPERTY(Category = "iTwin Spline",
		VisibleAnywhere)
	FVector2D Start = { 0., 0. };

	/// The direction of the spline from the start point. This is not necessarily a unit vector, its
	/// magnitude being used to control the "strength" of the spline tangent.
	UPROPERTY(Category = "iTwin Spline",
		VisibleAnywhere)
	FVector2D StartDir = { 1., 0. };

	/// Ending position of the spline in screen space.
	UPROPERTY(Category = "iTwin Spline",
		VisibleAnywhere)
	FVector2D End = { 1., 0. };

	/// The direction of the spline to the end point. Same remarks as StartDir apply.
	UPROPERTY(Category = "iTwin Spline",
		VisibleAnywhere)
	FVector2D EndDir = { 1., 0. };


	/// Whether the different points and tangents are valid. In some cases (e.g. when the start point is
	/// behind the camera), the projection can fail and produce invalid start/end points or tangents.
	bool bValidStart = true;
	bool bValidStartDir = true;
	bool bValidEnd = true;
	bool bValidEndDir = true;

	bool AllValid() const
	{
		return bValidStart && bValidStartDir && bValidEnd && bValidEndDir;
	}
};

namespace ITwin
{
	//! There can be two different purposes for sampling a spline: either to draw it on screen, or to perform
	//! hit-testing. The two purposes may require different sampling strategies (e.g. more subdivisions for
	//! hit-testing than for drawing, in case of linear tangent mode).
	enum class ESplineSamplingPurpose : uint8
	{
		Drawing = 0,
		HitTesting,
		ENUM_END
	};
}

/// This widget is used to display a 3D spline in 2D, on screen, as a child of the main viewport widget.
UCLASS()
class ITWINRUNTIME_API UITwinSpline2DWidget : public UUserWidget
{
    GENERATED_BODY()
public:
	UITwinSpline2DWidget(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	void SetStartAndEnd(const FITwinSplineChunk2DInfo& InChunk2DInfo);

	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	void SetTint(const FLinearColor& InTint);

	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	const FLinearColor& GetTint() const { return Tint; }

	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	void SetThickness(float InThickness);

	UFUNCTION(BlueprintCallable, Category = "iTwin Spline")
	float GetThickness() const { return Thickness; }

	bool IsScreenPositionOverSpline(const FGeometry& Geometry, const FVector2D& ScreenPosition,
		FVector::FReal& OutDistanceSquaredToSpline,
		FVector2D& OutClosestPoint2D,
		FVector::FReal ExtraTolerance = 0.) const;


	/// Cache the sampled positions of the spline, to avoid re-sampling it every frame for mouse hit testing.
	void CacheSplineSampling(const TArray<FVector2D>& InSampledPositions, ITwin::ESplineSamplingPurpose Purpose);

	/// Get the cached sampled positions of the spline, if any.
	const TArray<FVector2D>& GetCachedSplineSampling(ITwin::ESplineSamplingPurpose Purpose) const;

	/// Whether the sampled positions of the spline are cached and can be retrieved with
	/// GetCachedSplineSampling.
	bool HasCachedSplineSampling(ITwin::ESplineSamplingPurpose Purpose) const { return !GetCachedSplineSampling(Purpose).IsEmpty(); }

	/// Whether to draw the spline as a multi-line (instead of a single Bezier curve). This can be used to
	/// avoid numeric issues when the spline is very long on screen or has very high curvature, which can
	/// cause the single Bezier curve to have degenerated tangents.
	void SetDrawAsMultiLine(bool bInDrawAsMultiLine) { bDrawAsMultiLine = bInDrawAsMultiLine; }

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	static FVector::FReal DistancePointToSegmentSquared(
		const FVector2D& Point,
		const FVector2D& A,
		const FVector2D& B);

private:
	FITwinSplineChunk2DInfo	Chunk2DInfo;

	FLinearColor Tint = FLinearColor::White;
	float Thickness = 1.0f;

	bool bDrawAsMultiLine = false;

	struct FImpl;
	TPimplPtr<FImpl> Impl;
};
