/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMaterialPreviewHolder.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Material/ITwinMaterialPreviewHolder.h>

#include <ITwinSynchro4DSchedules.h>

#include <Materials/MaterialInstance.h>
#include <Materials/MaterialInterface.h>
#include <UObject/ConstructorHelpers.h>

UITwinMaterialPreviewHolder::UITwinMaterialPreviewHolder()
{
	// For now, we only override the 'masked' base material (MI_ITwinMasked_Preview is just a copy of
	// MI_ITwinInstance, in which we removed some layers that made the material totally transparent when
	// applied to a non-Cesium mesh actor).
	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinder<UMaterialInstance> BaseMaterialMasked;
		// ConstructorHelpers::FObjectFinder<UMaterialInstance> BaseMaterialTranslucent;
		// ConstructorHelpers::FObjectFinder<UMaterialInstance> BaseMaterialGlass;

		FConstructorStatics()
			: BaseMaterialMasked(TEXT("/ITwinForUnreal/ITwin/Materials/MI_ITwinMasked_Preview"))
			//	, BaseMaterialTranslucent(TEXT("/ITwinForUnreal/ITwin/Materials/MI_ITwinTranslucent_Preview"))
			//	, BaseMaterialGlass(TEXT("/ITwinForUnreal/ITwin/Materials/MI_ITwinGlass_Preview"))
		{}
	};

	static FConstructorStatics ConstructorStatics;
	this->BaseMaterialMasked = ConstructorStatics.BaseMaterialMasked.Object;
	//	this->BaseMaterialTranslucent = ConstructorStatics.BaseMaterialTranslucent.Object;
	//	this->BaseMaterialGlass = ConstructorStatics.BaseMaterialGlass.Object;

	// The other base materials have no reason to be duplicated: pick them from UITwinSynchro4DSchedules:
	UITwinSynchro4DSchedules const* SchedulesComp = Cast<UITwinSynchro4DSchedules>(
		UITwinSynchro4DSchedules::StaticClass()->GetDefaultObject());
	if (ensure(SchedulesComp))
	{
		this->BaseMaterialTranslucent = SchedulesComp->BaseMaterialTranslucent_TwoSided;
		this->BaseMaterialGlass = SchedulesComp->BaseMaterialGlass;
	}
}
