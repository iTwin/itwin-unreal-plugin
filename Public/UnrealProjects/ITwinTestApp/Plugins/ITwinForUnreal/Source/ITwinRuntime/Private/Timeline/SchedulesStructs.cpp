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
	// No, when S+ schedule gets a binding update, ElemIDGroups only lacks the newly created group
	//ensure(ElemIDGroups.size() == FedGUIDGroups.size() || ElemIDGroups.empty() || FedGUIDGroups.empty());
	// Should test Generation, but keep this flexibility for test data
	return std::max(ElemIDGroups.size(), FedGUIDGroups.size());
	//switch (Generation)
	//{
	//case EITwinSchedulesGeneration::Legacy:
	//	return ElemIDGroups.size();
	//case EITwinSchedulesGeneration::NextGen:
	//	return FedGUIDGroups.size();
	//case EITwinSchedulesGeneration::Unknown:
	//	break;
	//}
	//return 0;
}

void FITwinSchedule::ResetElemIDGroups()
{
	ElemIDGroups.clear();
}

size_t FITwinSchedule::GetNextGroupID() const { return NumGroups(); }

void FITwinSchedule::CreateNextGroup(bool const bIsElemIDGroup)
{
	// Should test Generation, but keep this flexibility for test data
	if (bIsElemIDGroup)
		ElemIDGroups.emplace_back();
	else
		FedGUIDGroups.emplace_back();
}

void FITwinSchedule::CreateNextGroup(FElementsGroup&& Group)
{
	ElemIDGroups.emplace_back(std::move(Group));
}

bool FITwinSchedule::AddToGroup(size_t InVec, ITwinElementID const ElemID)
{
	// Skip the Generation test to allow pseudo-NextGen test data generated from a Legacy schedule's
	// requests cache to work
	if (ensure(/*EITwinSchedulesGeneration::Legacy == Generation &&*/ InVec < ElemIDGroups.size()))
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
