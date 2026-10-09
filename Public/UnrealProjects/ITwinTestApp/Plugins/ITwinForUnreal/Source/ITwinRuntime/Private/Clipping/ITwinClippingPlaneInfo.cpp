/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingPlaneInfo.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Clipping/ITwinClippingPlaneInfo.h>

#include <Clipping/ITwinPlaneTileExcluder.h>
#include <Spline/ITwinSplineHelper.h>
#include <Spline/ITwinSplineTool.h>

//---------------------------------------------------------------------------------------
// struct FITwinClippingPlaneInfo
//---------------------------------------------------------------------------------------

void FITwinClippingPlaneInfo::SetPlaneEquation(FVector const& PlaneOrientation, double PlaneW,
	bool bPropagateToTileExcluders /*= true*/)
{
	PlaneEquation.PlaneOrientation = PlaneOrientation;
	PlaneEquation.PlaneW = PlaneW;

	if (bPropagateToTileExcluders)
	{
		// Update the plane equation in all tile excluders created from this plane.
		for (auto const& TileExcluder : TileExcluders)
		{
			if (TileExcluder.IsValid())
			{
				UITwinPlaneTileExcluder* PlaneExcluder = Cast<UITwinPlaneTileExcluder>(TileExcluder.Get());
				PlaneExcluder->PlaneEquation.PlaneOrientation = PlaneOrientation;
				PlaneExcluder->PlaneEquation.PlaneW = PlaneW;
			}
		}
	}
}

void FITwinClippingPlaneInfo::DoSetInvertEffect(bool bInvert)
{
	Super::DoSetInvertEffect(bInvert);

	this->bInvertEffect = bInvert;

	// Update tile excluders accordingly.
	for (auto const& TileExcluder : TileExcluders)
	{
		if (TileExcluder.IsValid()
			&& ensure(TileExcluder->IsA(UITwinPlaneTileExcluder::StaticClass())))
		{
			Cast<UITwinPlaneTileExcluder>(TileExcluder.Get())->SetInvertEffect(bInvert);
		}
	}
}

int32 FITwinClippingPlaneInfo::CountRequiredEdgeSplines() const
{
	// We use 1 spline of 4 points to represent the edges of the plane.
	return 1;
}

void FITwinClippingPlaneInfo::DoCreateEdgeSplines(TArray<TObjectPtr<AITwinSplineHelper>>& OutEdgeSplines, AITwinSplineTool& SplineTool)
{
	static const TArray<FVector> Face =
	{
		{ -0.5, -0.5, 0. },
		{ -0.5,  0.5, 0. },
		{  0.5,  0.5, 0. },
		{  0.5, -0.5, 0. }
	};
	auto EdgeSpline = SplineTool.AddSpline(FVector::ZeroVector, Face);
	if (!ensure(EdgeSpline))
	{
		return;
	}
	OutEdgeSplines.Add(EdgeSpline);
	EdgeSpline->SetActorHiddenInGame(true);
#if WITH_EDITOR
	static int32 PlaneEdgeSplineCounter = 0;
	EdgeSpline->SetCustomActorLabel(FString::Printf(TEXT("PlaneEdgeSpline_%d"), PlaneEdgeSplineCounter++));
#endif
}
