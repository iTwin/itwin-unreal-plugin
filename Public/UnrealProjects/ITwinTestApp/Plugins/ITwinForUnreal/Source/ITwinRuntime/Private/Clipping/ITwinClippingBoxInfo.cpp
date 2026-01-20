/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingBoxInfo.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Clipping/ITwinClippingBoxInfo.h>

#include <Clipping/ITwinBoxTileExcluder.h>
#include <array>

//---------------------------------------------------------------------------------------
// struct FITwinClippingBoxInfo
//---------------------------------------------------------------------------------------

bool FITwinClippingBoxInfo::GetInvertEffect() const
{
	return BoxProperties->bInvertEffect;
}

void FITwinClippingBoxInfo::DoSetInvertEffect(bool bInvert)
{
	BoxProperties->bInvertEffect = bInvert;
}

void FITwinClippingBoxInfo::CalcBoxBounds(glm::dmat3x3 const& BoxMatrix, glm::dvec3 const& BoxTranslation)
{
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
