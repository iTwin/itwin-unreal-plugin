/*--------------------------------------------------------------------------------------+
|
|     $Source: SchedulesStructs.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "SchedulesStructs.h"

size_t FITwinSchedule::NumGroups() const
{
	switch (Generation)
	{
	case EITwinSchedulesGeneration::Legacy:
		return ElemIDGroups.size();
	case EITwinSchedulesGeneration::NextGen:
		return FedGUIDGroups.size();
	case EITwinSchedulesGeneration::Unknown:
		break;
	}
	return 0;
}

size_t FITwinSchedule::GetNextGroupID() const { return NumGroups(); }

void FITwinSchedule::CreateNextGroup()
{
	switch (Generation)
	{
	case EITwinSchedulesGeneration::Legacy:
		ElemIDGroups.emplace_back();
		break;
	case EITwinSchedulesGeneration::NextGen:
		FedGUIDGroups.emplace_back();
		break;
	case EITwinSchedulesGeneration::Unknown:
		break;
	}
}

void FITwinSchedule::CreateNextGroup(FElementsGroup&& Group)
{
	ElemIDGroups.emplace_back(std::move(Group));
}

bool FITwinSchedule::AddToGroup(size_t InVec, ITwinElementID const ElemID)
{
	if (ensure(EITwinSchedulesGeneration::Legacy == Generation && InVec < ElemIDGroups.size()))
	{
		return ElemIDGroups[InVec].insert(ElemID).second; // was inserted
	}
	return false;
}

bool FITwinSchedule::AddToGroup(size_t InVec, FGuid const FedGUID)
{
	if (ensure(EITwinSchedulesGeneration::NextGen == Generation && InVec < FedGUIDGroups.size()))
	{
		return FedGUIDGroups[InVec].insert(FedGUID).second; // was inserted
	}
	return false;
}
