/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMaterialParameters.inl $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <Materials/MaterialInstanceDynamic.h>

namespace ITwin
{
	template <typename UVTransform>
	inline void SetUVTransformInMaterialInstance(UVTransform const& uvTransform,
		UMaterialInstanceDynamic& materialInstance,
		EMaterialParameterAssociation association, int32 index)
	{
		materialInstance.SetVectorParameterValueByInfo(
			FMaterialParameterInfo("uvScaleOffset", association, index),
			FLinearColor(
				uvTransform.scale[0],
				uvTransform.scale[1],
				uvTransform.offset[0],
				uvTransform.offset[1]));
		FLinearColor const rotationValues(
			float(FMath::Sin(uvTransform.rotation)),
			float(FMath::Cos(uvTransform.rotation)),
			0.0f,
			1.0f);
		materialInstance.SetVectorParameterValueByInfo(
			FMaterialParameterInfo(
				"uvRotation",
				association,
				index),
			rotationValues);
	}
}
