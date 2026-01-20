/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingPlaneInfo.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Clipping/ITwinClippingInfoBase.h>

#include <ITwinClippingPlaneInfo.generated.h>


USTRUCT()
struct FITwinClippingPlaneInfo : public FITwinClippingInfoBase
{
	GENERATED_USTRUCT_BODY()

	virtual bool GetInvertEffect() const override { return bInvertEffect; }

protected:
	virtual void DoSetInvertEffect(bool bInvert) override;

private:
	/**
	 * Whether to invert the effect specified by the clipping primitive.
	 *
	 * Typically for a box, if this is true, only the areas outside of the box will be
	 * visible.
	 */
	bool bInvertEffect = false;

	friend class AITwinClippingTool;
};
