/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMetadataPropertyAccess.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <CesiumMetadataPickingBlueprintLibrary.h>
#include <CesiumPropertyTableProperty.h>

class FITwinMetadataPropertyAccess
{
public:
	/**
	 * Retrieves a property by name.
	 * If the specified feature ID set does not exist or if the property table
	 * does not contain a property with that name, this function returns nullptr.
	 */
	static inline const FCesiumPropertyTableProperty* FindValidProperty(
		const FCesiumPrimitiveFeatures& Features,
		const FCesiumModelMetadata& Metadata,
		const FString& PropertyName,
		int64 FeatureIDSetIndex = 0)
	{
		auto const& Property = UCesiumMetadataPickingBlueprintLibrary::FindPropertyTableProperty(
			Features, Metadata, PropertyName, FeatureIDSetIndex);
		const ECesiumPropertyTablePropertyStatus PropertyStatus =
			UCesiumPropertyTablePropertyBlueprintLibrary::
			GetPropertyTablePropertyStatus(Property);
		if (PropertyStatus != ECesiumPropertyTablePropertyStatus::Valid) {
			return nullptr;
		}
		return &Property;
	}
};
