/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingMPCHolder.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Clipping/ITwinClippingMPCHolder.h>

#include <Materials/MaterialParameterCollection.h>
#include <UObject/ConstructorHelpers.h>

UITwinClippingMPCHolder::UITwinClippingMPCHolder()
{
	// We use a material parameter collection to store global information regarding clipping planes and
	// clipping box: so that they are accessible in all material instances.
	struct FConstructorStatics {
		ConstructorHelpers::FObjectFinder<UMaterialParameterCollection> MPCCuttingPlanes;
		FConstructorStatics()
			: MPCCuttingPlanes(TEXT("/ITwinForUnreal/ITwin/Materials/MPC_Clipping"))
		{}
	};
	static FConstructorStatics ConstructorStatics;
	this->MPC_Clipping = ConstructorStatics.MPCCuttingPlanes.Object;
}
