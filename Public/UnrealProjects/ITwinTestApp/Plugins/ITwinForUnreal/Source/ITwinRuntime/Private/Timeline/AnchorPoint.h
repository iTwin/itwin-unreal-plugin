/*--------------------------------------------------------------------------------------+
|
|     $Source: AnchorPoint.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Containers/UnrealString.h>

namespace ITwin::Timeline {

enum class EAnchorPoint : uint8_t
{
	/// Anchor point related to the Element (group)'s axis-aligned bounding box (volume or face center)
	/// (NOT in any local box - tested in SynchroPro). Thus we don't need the Element's local base.
	Center, MinX, MaxX, MinY, MaxY, MinZ, MaxZ,
	/// 'Original Position' in SynchroPro's UI: the Element starts the task at its original position and then
	/// follows the path' relative translations from each keyframe to the next. Thus we don't need the
	/// Element's local base for translations, but we need its origin for rotations. For pure rotations, we can
	/// maybe assume the keyframe position matches this origin?
	Original,
	/// Value is entered by the user as an offset along the world axes relative to the 'Center' anchor as
	/// defined above
	Custom,
	/// Not an anchor mode, used to tell Static transform assignments, which happen to have no anchor setting,
	/// from 3D path keyframes, ie 'tis a hack, yeah.
	Static
};

inline FString GetAnchorPointString(EAnchorPoint const AnchorPoint)
{
	switch (AnchorPoint)
	{
	case EAnchorPoint::Center:  return TEXT("Center");
	case EAnchorPoint::MinX:	return TEXT("MinX");
	case EAnchorPoint::MaxX:	return TEXT("MaxX");
	case EAnchorPoint::MinY:	return TEXT("MinY");
	case EAnchorPoint::MaxY:	return TEXT("MaxY");
	case EAnchorPoint::MinZ:	return TEXT("MinZ");
	case EAnchorPoint::MaxZ:	return TEXT("MaxZ");
	case EAnchorPoint::Original:return TEXT("Original");
	case EAnchorPoint::Custom:	return TEXT("Custom");
	case EAnchorPoint::Static:	return TEXT("Static");
	default:					return TEXT("<InvalidAnchorPoint>");
	}
}

} // namespace ITwin::Timeline
