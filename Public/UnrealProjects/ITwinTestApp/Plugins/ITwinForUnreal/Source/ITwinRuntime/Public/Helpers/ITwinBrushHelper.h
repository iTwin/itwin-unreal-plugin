/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinBrushHelper.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"

#include <Templates/PimplPtr.h>

class ITWINRUNTIME_API FITwinBrushHelper
{
public:
	FITwinBrushHelper();

	void InitBrushSphere(UWorld* World);

	void ShowBrushSphere();
	void HideBrushSphere();

	float GetBrushFlow() const { return BrushFlow.UserFactor; }
	void SetBrushFlow(float InFlow);
	void SetBrushComputedValue(float InComputedValue);

	float GetBrushRadius() const { return BrushRadius; }
	void SetBrushRadius(float InRadius);

	FVector GetBrushPosition() const;
	void SetBrushPosition(const FVector& Position);

	bool StartBrushing(float GameTimeSinceCreation);
	bool IsBrushing() const { return bIsBrushing; }
	void EndBrushing();

protected:
	struct FBrushFlow
	{
		float ComputedValue = 1.f;
		float UserFactor = 1.f;
		float GetFlow() const { return ComputedValue * UserFactor; }
	};

	float BrushRadius = 1000.f; // radius in centimeters
	FBrushFlow BrushFlow; // number of added instances per m^2 per second.

	bool bIsBrushing = false;
	float BrushLastTime = 0.f;
	FVector BrushLastPos = FVector(0);

private:
	class FImpl;
	TPimplPtr<FImpl> Impl;
};
