/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPlaneTileExcluder.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Clipping/ITwinPlaneTileExcluder.h>

void UITwinPlaneTileExcluder::SetInvertEffect(bool bInvert)
{
	bInvertEffect = bInvert;
}

inline bool UITwinPlaneTileExcluder::ShouldExcludePoint(FVector3f const& WorldPosition) const
{
	const float Value = glm::step(PlaneEquation.PlaneOrientation.Dot(WorldPosition) - PlaneEquation.PlaneW, 0.f);
	return ShouldInvertEffect() ? (Value > 0.5f) : (Value < 0.5f);
}

bool UITwinPlaneTileExcluder::ShouldExclude_Implementation(const UCesiumTile* TileObject)
{
	FVector3d BoxVertices[8];
	TileObject->Bounds.GetBox().GetVertices(BoxVertices);
	for (int i=0; i<8; i++)
	{
		if (!ShouldExcludePoint(FVector3f(BoxVertices[i])))
		{
			return false;
		}
	}
	return true;
}
