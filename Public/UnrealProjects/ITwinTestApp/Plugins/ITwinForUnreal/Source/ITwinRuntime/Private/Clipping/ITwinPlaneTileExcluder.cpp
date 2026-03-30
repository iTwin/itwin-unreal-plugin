/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPlaneTileExcluder.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Clipping/ITwinPlaneTileExcluder.h>

void UITwinPlaneTileExcluder::SetInvertEffect(bool bInvert)
{
	bInvertEffect = bInvert;
}

inline bool UITwinPlaneTileExcluder::ShouldExcludePoint(FVector3d const& WorldPosition) const
{
	const double Value = glm::step(PlaneEquation.PlaneOrientation.Dot(WorldPosition) - PlaneEquation.PlaneW, 0.);
	return ShouldInvertEffect() ? (Value > 0.5) : (Value < 0.5);
}

bool UITwinPlaneTileExcluder::ShouldExclude_Implementation(const UCesiumTile* TileObject)
{
	FVector3d BoxVertices[8];
	TileObject->Bounds.GetBox().GetVertices(BoxVertices);
	for (int i=0; i<8; i++)
	{
		if (!ShouldExcludePoint(BoxVertices[i]))
		{
			return false;
		}
	}
	return true;
}
