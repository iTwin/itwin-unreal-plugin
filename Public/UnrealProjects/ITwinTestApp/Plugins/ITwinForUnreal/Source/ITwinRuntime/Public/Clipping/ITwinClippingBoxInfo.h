/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingBoxInfo.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Clipping/ITwinClippingInfoBase.h>

#include <glm/ext/matrix_double3x3.hpp>
#include <glm/ext/vector_double3.hpp>
#include <memory>

#include <ITwinClippingBoxInfo.generated.h>


USTRUCT()
struct FITwinClippingBoxInfo : public FITwinClippingInfoBase
{
	GENERATED_USTRUCT_BODY()

	virtual bool GetInvertEffect() const override;
	virtual void DeactivatePrimitiveInExcluder(UITwinTileExcluderBase& Excluder) const override;

	void CalcBoxBounds(glm::dmat3x3 const& BoxMatrix, glm::dvec3 const& BoxTranslation);


	struct FBoxProperties
	{
		FBoxSphereBounds BoxBounds;
		bool bInvertEffect = false;
	};

protected:
	virtual void DoSetInvertEffect(bool bInvert);


	// Will be shared by all tile excluders including this box.
	std::shared_ptr<FBoxProperties> BoxProperties = std::make_shared<FBoxProperties>();

	friend class AITwinClippingTool;
};
