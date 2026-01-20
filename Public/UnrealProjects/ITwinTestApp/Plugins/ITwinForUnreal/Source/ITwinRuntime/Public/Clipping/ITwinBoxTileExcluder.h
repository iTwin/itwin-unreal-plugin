/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinBoxTileExcluder.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <Clipping/ITwinClippingBoxInfo.h>
#include <Clipping/ITwinTileExcluderBase.h>

#include <vector>

#include <ITwinBoxTileExcluder.generated.h>

UCLASS()
class ITWINRUNTIME_API UITwinBoxTileExcluder : public UITwinTileExcluderBase
{
	GENERATED_BODY()
public:
	using SharedProperties = std::shared_ptr<FITwinClippingBoxInfo::FBoxProperties>;

	bool ContainsBox(SharedProperties const& BoxProperties) const;
	void RemoveBox(SharedProperties const& BoxProperties);

	virtual bool ShouldExclude_Implementation(const UCesiumTile* TileObject) override;

	inline bool ShouldExcludeTileForBox(const UCesiumTile* TileObject,
		const SharedProperties& BoxProperties) const;

private:
	using FBoxPropertiesArray = std::vector<SharedProperties>;

	// A Box tile excluder can reference several boxes.
	FBoxPropertiesArray BoxPropertiesArray;

	friend class AITwinClippingTool;
};
