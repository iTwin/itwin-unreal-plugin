/*--------------------------------------------------------------------------------------+
|
|     $Source: GrowthStatus.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Containers/UnrealString.h>

namespace ITwin::Timeline {

namespace Detail::GrowthStatus
{
	namespace Bit { enum EBit { Removed, Grown, Deferred }; }
	namespace Mask {
		enum EMask {
			Removed = (1 << Bit::Removed), Grown = (1 << Bit::Grown),
			Deferred = (1 << Bit::Deferred)
		};
	}
	constexpr int IgnoreDeferred = ~Mask::Deferred; ///< to be ANDed with
}

enum class EGrowthStatus : uint8_t
{
	/// Neither of the other states, ie the growth is probably somewhere in the middle of the Element(s) BBox
	Partial = 0,
	FullyRemoved = Detail::GrowthStatus::Mask::Removed,
	/// The growth animation has reached a point where the Element(s) are fully hidden (ie. construction has
	/// not started, or removal has finished). This is a deferred state, in that it will have to be converted
	/// to the first or last(*) cutting plane equation of the growth simulation ((*) depending on the task
	/// action = install/remove/etc.)
	DeferredFullyRemoved = (FullyRemoved | Detail::GrowthStatus::Mask::Deferred),
	/// The Element(s) are simply full visible ('static' state, as opposed to DeferredFullyRemoved).
	FullyGrown = Detail::GrowthStatus::Mask::Grown,
	/// The Element(s) are simply fully hidden ('static' state, as opposed to DeferredFullyRemoved).
	/// The growth animation has reached a point where the Element(s) are fully visible (ie. construction has
	/// finished, or removal has not started). This is a deferred state, in that it will have to be converted
	/// to the first or last(*) cutting plane equation of the growth simulation ((*) depending on the task
	/// action = install/remove/etc.)
	DeferredFullyGrown = (FullyGrown | Detail::GrowthStatus::Mask::Deferred),
	/// Partial growth whose ratio in [0;1] (0 = fully removed, 1 = fully grown) is stored in PlaneW until
	/// the plane equation is finalized (to Partial).
	DeferredPartial = (Partial | Detail::GrowthStatus::Mask::Deferred),
};

inline FString GetGrowthStatusString(EGrowthStatus const GrowthStatus)
{
	switch (GrowthStatus)
	{
	case EGrowthStatus::DeferredFullyRemoved:
		return TEXT("DeferredFullyRemoved");
	case EGrowthStatus::DeferredFullyGrown:
		return TEXT("DeferredFullyGrown");
	case EGrowthStatus::DeferredPartial:
		return TEXT("DeferredPartiallyGrown");
	case EGrowthStatus::FullyRemoved:
		return TEXT("FullyRemoved");
	case EGrowthStatus::FullyGrown:
		return TEXT("FullyGrown");
	case EGrowthStatus::Partial:
		return TEXT("PartiallyGrown");
	default:
		return TEXT("<InvalidGrowthStatus>");
	}
}

inline std::optional<EGrowthStatus> ParseGrowthStatus(FString const& GrowthStatus)
{
	if (GrowthStatus == TEXT("DeferredFullyRemoved"))
		return EGrowthStatus::DeferredFullyRemoved;
	else if (GrowthStatus == TEXT("DeferredFullyGrown"))
		return EGrowthStatus::DeferredFullyGrown;
	else if (GrowthStatus == TEXT("DeferredPartiallyGrown"))
		return EGrowthStatus::DeferredPartial;
	else if (GrowthStatus == TEXT("FullyRemoved"))
		return EGrowthStatus::FullyRemoved;
	else if (GrowthStatus == TEXT("FullyGrown"))
		return EGrowthStatus::FullyGrown;
	else if (GrowthStatus == TEXT("PartiallyGrown"))
		return EGrowthStatus::Partial;
	else
		return std::nullopt;
}

} // namespace ITwin::Timeline
