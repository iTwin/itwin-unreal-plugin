/*--------------------------------------------------------------------------------------+
|
|     $Source: AnchorPoint.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Containers/UnrealString.h>

namespace ITwin::Timeline {

enum class EAnchorPoint : uint8_t
{
	Center, Bottom, Top, Left/*X min*/, Right/*X max*/, Front/*Y min*/, Back/*Y max*/,
	Original, // <== I don't know what this one means ATM :/
	/// Anchor is either entered by the user as a custom value, or set as one of the symbolic values above,
	/// but when actually applied it should already be computed from the Elements bounding boxes.
	Custom,
};

inline FString GetAnchorPointString(EAnchorPoint const AnchorPoint)
{
	switch (AnchorPoint)
	{
	case EAnchorPoint::Center:  return TEXT("Center");
	case EAnchorPoint::Bottom:	return TEXT("Bottom");
	case EAnchorPoint::Top:		return TEXT("Top");
	case EAnchorPoint::Left:	return TEXT("Left");
	case EAnchorPoint::Right:	return TEXT("Right");
	case EAnchorPoint::Front:	return TEXT("Front");
	case EAnchorPoint::Back:	return TEXT("Back");
	case EAnchorPoint::Original:return TEXT("Original");
	case EAnchorPoint::Custom:	return TEXT("Custom");
	default:					return TEXT("<InvalidAnchorPoint>");
	}
}

} // namespace ITwin::Timeline
