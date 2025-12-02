/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingCartographicPolygonInfo.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Clipping/ITwinClippingInfoBase.h>

#include <ITwinClippingCartographicPolygonInfo.generated.h>

class AITwinSplineHelper;


USTRUCT()
struct FITwinClippingCartographicPolygonInfo : public FITwinClippingInfoBase
{
	GENERATED_USTRUCT_BODY()

	virtual bool GetInvertEffect() const override;

protected:
	virtual void DoSetEnabled(bool bInEnabled) override;
	virtual void DoSetInvertEffect(bool bInvert) override;

private:
	struct FProperties
	{
		bool bInvertEffect = false;
	};
	FProperties Properties;

	/// Spline Helper associated to this primitive.
	UPROPERTY()
	TWeakObjectPtr<AITwinSplineHelper> SplineHelper;


	friend class AITwinClippingTool;
};
