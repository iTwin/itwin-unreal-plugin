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
struct FITwinClippingPlaneInfo final : public FITwinClippingInfoBase
{
	GENERATED_USTRUCT_BODY()

	virtual bool GetInvertEffect() const override { return bInvertEffect; }

	struct FPlaneEquation
	{
		FVector PlaneOrientation = FVector::ZAxisVector;
		double PlaneW = 0.;
	};

	FPlaneEquation const& GetPlaneEquation() const { return PlaneEquation; }

	// returns the equation with inversion already folded in
	FPlaneEquation GetEffectivePlaneEquation() const
	{
		auto Eq = GetPlaneEquation();
		return GetInvertEffect() ? FPlaneEquation{ -Eq.PlaneOrientation, -Eq.PlaneW } : Eq;
	}

	void SetPlaneEquation(FVector const& PlaneOrientation, double PlaneW, bool bPropagateToTileExcluders = true);

protected:
	virtual void DoSetInvertEffect(bool bInvert) override;

	virtual int32 CountRequiredEdgeSplines() const override;
	virtual void DoCreateEdgeSplines(TArray<TObjectPtr<AITwinSplineHelper>>& OutEdgeSplines, AITwinSplineTool& SplineTool) override;

private:
	/**
	 * Whether to invert the effect of the clipping plane.
	 */
	bool bInvertEffect = false;
	FPlaneEquation PlaneEquation;
};
