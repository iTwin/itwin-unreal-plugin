/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPopulationWithPathExt.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "Population/ITwinPopulationWithPathExt.h"

FITwinPopulationWithPathExt::FITwinPopulationWithPathExt()
{
}

FITwinPopulationWithPathExt::~FITwinPopulationWithPathExt()
{
}

void FITwinPopulationWithPathExt::InstanceToUpdateColor(size_t instIndex, FVector color)
{
	auto locked = instancesToUpdateColor_.GetAutoLock();
	locked.Get()[instIndex] = color;
}

void FITwinPopulationWithPathExt::InstanceToUpdateTransForm(size_t instIndex, const FTransform& Trans)
{
	auto locked = instancesToUpdateTr_.GetAutoLock();
	locked.Get()[instIndex] = Trans;
}

void FITwinPopulationWithPathExt::UpdatePopulationInstances()
{
	if (population_ == nullptr || !population_->meshComp)
		return;
	{
		if ( (instancesToUpdateTr_.GetRAutoLock().Get().size() == 0)
			&& (instancesToUpdateColor_.GetRAutoLock().Get().size() == 0))
			return;

		auto meshComp = population_->meshComp;
		meshComp->bAutoRebuildTreeOnInstanceChanges = false;

		// update transformation
		{
			auto locked = instancesToUpdateTr_.GetAutoLock();
			for (auto const& [instIndex, prop] : locked.Get())
				meshComp->UpdateInstanceTransform(instIndex, prop, true);
			locked.Get().clear();
		}
		// update color
		{
			auto locked = instancesToUpdateColor_.GetAutoLock();
			if (meshComp->NumCustomDataFloats != 3)
				meshComp->SetNumCustomDataFloats(3);
			for (auto const& [instIndex, prop] : locked.Get())
			{
				meshComp->SetCustomDataValue(instIndex, 0, prop.X, true);
				meshComp->SetCustomDataValue(instIndex, 1, prop.Y, true);
				meshComp->SetCustomDataValue(instIndex, 2, prop.Z, true);
			}
			locked.Get().clear();
		}

		meshComp->bAutoRebuildTreeOnInstanceChanges = true;
		meshComp->BuildTreeIfOutdated(true, false);
		
	}
}

