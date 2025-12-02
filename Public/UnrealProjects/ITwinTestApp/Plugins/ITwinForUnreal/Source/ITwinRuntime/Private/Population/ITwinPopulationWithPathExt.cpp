/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPopulationWithPathExt.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "Population/ITwinPopulationWithPathExt.h"

#include <Population/ITwinPopulation.inl>

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
	if (population_ == nullptr || population_->FoliageComponents.IsEmpty())
		return;
	{
		if ( (instancesToUpdateTr_.GetRAutoLock().Get().size() == 0)
			&& (instancesToUpdateColor_.GetRAutoLock().Get().size() == 0))
			return;

		// Disable automatic rebuild until all instances have been updated.
		AITwinPopulation::FAutoRebuildTreeDisabler RebuildTreeDisabler(*population_);

		// update transformation
		{
			auto locked = instancesToUpdateTr_.GetAutoLock();
			for (auto const& [instIndex, prop] : locked.Get())
				population_->SetInstanceTransformUEOnly(instIndex, prop, /*bMarkRenderStateDirty*/false);
			locked.Get().clear();
		}
		// update color
		{
			auto locked = instancesToUpdateColor_.GetAutoLock();
			for (auto const& [instIndex, prop] : locked.Get())
				population_->SetInstanceColorVariationUEOnly(instIndex, prop, /*bMarkRenderStateDirty*/false);
			locked.Get().clear();

			// mark render state dirty when all instances have been updated.
			population_->MarkFoliageRenderStateDirty();
		}
	}
}

