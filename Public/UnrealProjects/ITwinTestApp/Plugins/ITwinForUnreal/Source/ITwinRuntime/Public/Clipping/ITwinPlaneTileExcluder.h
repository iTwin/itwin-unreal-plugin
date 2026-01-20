/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPlaneTileExcluder.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <Clipping/ITwinTileExcluderBase.h>

#include <ITwinPlaneTileExcluder.generated.h>

class ACesium3DTileset;
class AITwinClippingTool;

UCLASS()
class ITWINRUNTIME_API UITwinPlaneTileExcluder : public UITwinTileExcluderBase
{
	GENERATED_BODY()
public:
	UFUNCTION(Category = "iTwin|Clipping",
		BlueprintCallable)
	bool ShouldInvertEffect() const { return bInvertEffect; }

	UFUNCTION(Category = "iTwin|Clipping",
		BlueprintCallable)
	void SetInvertEffect(bool bInvert);


	virtual bool ShouldExclude_Implementation(const UCesiumTile* TileObject) override;


private:
	inline bool ShouldExcludePoint(FVector3f const& WorldPosition) const;

private:
	//! Whether to invert the effect specified by the clipping plane.
	UPROPERTY(Category = "iTwin|Clipping",
		EditAnywhere,
		BlueprintSetter = SetInvertEffect)
	bool bInvertEffect = false;

	// Same as in Synchro4D timelines, but without 'deferred' status.
	struct FPlaneEquation
	{
		FVector3f PlaneOrientation = FVector3f::ZAxisVector;
		float PlaneW = 0.f;
	};
	FPlaneEquation PlaneEquation;
	int32 PlaneIndex = -1;

	friend class AITwinClippingTool;
};
