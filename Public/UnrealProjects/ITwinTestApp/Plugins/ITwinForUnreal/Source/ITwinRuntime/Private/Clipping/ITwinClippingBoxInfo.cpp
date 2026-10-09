/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingBoxInfo.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Clipping/ITwinClippingBoxInfo.h>

#include <Clipping/ITwinBoxTileExcluder.h>
#include <Spline/ITwinSplineHelper.h>
#include <Spline/ITwinSplineTool.h>

#include <array>

//---------------------------------------------------------------------------------------
// struct FITwinClippingBoxInfo
//---------------------------------------------------------------------------------------

bool FITwinClippingBoxInfo::GetInvertEffect() const
{
	// The default behavior was changed for LA-7: the box is now subtractive by default, meaning that it
	// creates a hole in the layer. This is more intuitive when creating a box from scratch, and is more
	// consistent with the way the cutout polygon works.
	// See AzDev#2068178
	return !BoxProperties->bIsSubtractive;
}

void FITwinClippingBoxInfo::DoSetInvertEffect(bool bInvert)
{
	BoxProperties->bIsSubtractive = !bInvert;
}

void FITwinClippingBoxInfo::UpdateBoxProperties(glm::dmat3x3 const& BoxMatrix, glm::dvec3 const& BoxTranslation)
{
	// Store inverse matrix and translation
	BoxProperties->BoxInvMatrix = glm::inverse(BoxMatrix);
	BoxProperties->BoxTranslation = BoxTranslation;

	// Calculate bounds
	std::array<glm::dvec3, 8> BoxVertices;
	BoxVertices[0] = BoxTranslation + (BoxMatrix * glm::dvec3(-0.5, -0.5, -0.5));
	BoxVertices[1] = BoxTranslation + (BoxMatrix * glm::dvec3(-0.5, -0.5, 0.5));
	BoxVertices[2] = BoxTranslation + (BoxMatrix * glm::dvec3(-0.5, 0.5, -0.5));
	BoxVertices[3] = BoxTranslation + (BoxMatrix * glm::dvec3(-0.5, 0.5, 0.5));
	BoxVertices[4] = BoxTranslation + (BoxMatrix * glm::dvec3(0.5, -0.5, -0.5));
	BoxVertices[5] = BoxTranslation + (BoxMatrix * glm::dvec3(0.5, -0.5, 0.5));
	BoxVertices[6] = BoxTranslation + (BoxMatrix * glm::dvec3(0.5, 0.5, -0.5));
	BoxVertices[7] = BoxTranslation + (BoxMatrix * glm::dvec3(0.5, 0.5, 0.5));

	FBox3d Box;
	for (auto const& v : BoxVertices)
	{
		Box += FVector3d(v.x, v.y, v.z);
	}
	BoxProperties->BoxBounds = FBoxSphereBounds(Box);
}

void FITwinClippingBoxInfo::DeactivatePrimitiveInExcluder(UITwinTileExcluderBase& Excluder) const
{
	if (ensure(Excluder.IsA(UITwinBoxTileExcluder::StaticClass())))
	{
		Cast<UITwinBoxTileExcluder>(&Excluder)->RemoveBox(BoxProperties);
	}
}

int32 FITwinClippingBoxInfo::CountRequiredEdgeSplines() const
{
	// We use 6 splines to represent the edges of the box, one spline per face. Each spline is a closed loop
	// of 4 points.
	return 6;
}

void FITwinClippingBoxInfo::DoCreateEdgeSplines(TArray<TObjectPtr<AITwinSplineHelper>>& OutEdgeSplines, AITwinSplineTool& SplineTool)
{
	static const TArray<FVector> CubePositions =
	{
		{ -0.5, -0.5, -0.5 },
		{ -0.5, -0.5,  0.5 },
		{ -0.5,  0.5, -0.5 },
		{ -0.5,  0.5,  0.5 },
		{  0.5, -0.5, -0.5 },
		{  0.5, -0.5,  0.5 },
		{  0.5,  0.5, -0.5 },
		{  0.5,  0.5,  0.5 }
	};
	static const TArray<TArray<FVector>> CubeFaces =
	{
		{ CubePositions[2], CubePositions[3], CubePositions[1], CubePositions[0] },
		{ CubePositions[6], CubePositions[7], CubePositions[3], CubePositions[2] },
		{ CubePositions[4], CubePositions[5], CubePositions[7], CubePositions[6] },
		{ CubePositions[0], CubePositions[1], CubePositions[5], CubePositions[4] },
		{ CubePositions[1], CubePositions[3], CubePositions[7], CubePositions[5] },
		{ CubePositions[0], CubePositions[4], CubePositions[6], CubePositions[2] }
	};

	static int32 BoxEdgeSplineCounter = 0;

	for (int i(0); i < 6; ++i)
	{
		auto EdgeSpline = SplineTool.AddSpline(FVector::ZeroVector, CubeFaces[i]);
		if (EdgeSpline)
		{
			OutEdgeSplines.Add(EdgeSpline);
			EdgeSpline->SetActorHiddenInGame(true);
#if WITH_EDITOR
			EdgeSpline->SetCustomActorLabel(FString::Printf(TEXT("BoxEdgeSpline_%d_%d"), BoxEdgeSplineCounter, i));
#endif
		}
	}
	++BoxEdgeSplineCounter;
}
