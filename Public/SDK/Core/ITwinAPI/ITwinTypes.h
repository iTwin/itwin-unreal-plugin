/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTypes.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#ifndef SDK_CPPMODULES
	#include <array>
	#include <map>
	#include <optional>
	#include <string>
	#include <unordered_map>
	#include <variant>
	#include <vector>
	#ifndef MODULE_EXPORT
		#define MODULE_EXPORT
	#endif // !MODULE_EXPORT
#endif

#include "ITwinRequestTypes.h"

MODULE_EXPORT namespace SDK::Core
{
	/// =====================================================================================================
	/// Important remark: those types respect the schema defined for iTwin APIs: do not rename any member
	/// unless you were notified a change in the corresponding iTwin service.
	/// =====================================================================================================

	struct ITwinErrorDetails
	{
		std::string code;
		std::string message;
		std::optional<std::string> target;
	};

	struct ITwinErrorData
	{
		std::string code;
		std::string message;
		std::optional< std::vector<ITwinErrorDetails> > details;
	};

	struct ITwinError
	{
		ITwinErrorData error;
	};

	struct ITwinGeolocationInfo
	{
		double latitude = 0.0;
		double longitude = 0.0;
	};

	struct ITwinExportInfo
	{
		std::string id;
		std::string displayName;
		std::string status;
		std::string iModelId;
		std::string iTwinId;
		std::string changesetId;
		std::string meshUrl;
		std::string lastModified;
	};

	struct ITwinExportInfos
	{
		std::vector<ITwinExportInfo> exports;
	};


	struct ITwinInfo
	{
		std::string id;
		std::string displayName;
		std::string status;
		std::string number;
	};

	struct ITwinInfos
	{
		std::vector<ITwinInfo> iTwins;
	};


	struct IModelInfo
	{
		std::string id;
		std::string displayName;
	};

	struct IModelInfos
	{
		std::vector<IModelInfo> iModels;
	};


	struct ChangesetInfo
	{
		std::string id;
		std::string displayName;
		std::optional<std::string> description;
		int index = 0;
	};

	struct ChangesetInfos
	{
		std::vector<ChangesetInfo> changesets;
	};

	struct SavedViewExtensionsInfo
	{
		std::string extensionName;
	};

	struct SavedViewInfo
	{
		std::string id;
		std::string displayName;
		bool shared = false;
		std::string creationTime;
		std::vector<SavedViewExtensionsInfo> extensions;
	};

	struct SavedViewInfos
	{
		std::vector<SavedViewInfo> savedViews;
		std::optional<std::string> groupId;
	};

	struct SavedViewGroupInfo
	{
		std::string id;
		std::string displayName;
		bool shared = false;
		bool readOnly = false;
	};

	struct SavedViewGroupInfos
	{
		std::vector<SavedViewGroupInfo> groups;
		std::optional<std::string> iModelId;
	};

	struct Rotator
	{
		std::optional<double> yaw;
		std::optional<double> pitch;
		std::optional<double> roll;
	};

	struct DisplayStyle
	{
		std::optional<std::string> renderTimeline;
		std::optional<double> timePoint = 0;
	};

	struct SavedView
	{
		std::array<double, 3> origin = { 0, 0, 0 };
		std::array<double, 3> extents = { 0, 0, 0 };
		Rotator angles;
		std::optional<std::vector<std::string>> hiddenCategories;
		std::optional<std::vector<std::string>> hiddenModels;
		std::optional<std::vector<std::string>> hiddenElements;
		std::optional<DisplayStyle> displayStyle;
		std::array<double, 3> frustumOrigin = { 0, 0, 0 };
		double focusDist = 0.;
	};


	struct ITwinRealityDataInfo
	{
		std::string id;
		std::string displayName;
	};

	struct ITwinRealityDataInfos
	{
		std::vector<ITwinRealityDataInfo> realityData;
	};


	struct ITwinRealityData3DInfo : public ITwinRealityDataInfo
	{
		bool bGeolocated = false;
		ITwinGeolocationInfo extentSouthWest = {};
		ITwinGeolocationInfo extentNorthEast = {};

		std::string meshUrl;
	};

	struct ITwinElementAttribute
	{
		std::string name;
		std::string value;
	};

	struct ITwinElementProperty
	{
		std::string name;
		std::vector<ITwinElementAttribute> attributes;
	};

	struct ITwinElementProperties
	{
		std::vector<ITwinElementProperty> properties;
	};


	// For now, consider a material as an array of { key, value } attributes.
	using ITwinMaterialAttributeValue = std::variant<bool,
													 double,
													 std::array<double, 2>,
													 std::array<double, 3>,
													 std::string
													>;
	using AttributeMap = std::unordered_map<std::string, ITwinMaterialAttributeValue>;

	struct ITwinMaterialProperties
	{
		std::string id;
		std::string name;
		AttributeMap attributes;
		std::map<std::string, AttributeMap> maps; // texture maps
	};

	struct ITwinMaterialPropertiesMap
	{
		std::map<std::string, ITwinMaterialProperties> data_;
	};

	// from https://www.itwinjs.org/v3/reference/core-common/entities/texturedata/
	enum class ImageSourceFormat : uint8_t
	{
		Jpeg = 0,
		Png = 2,
		Svg = 3
	};
	enum class TextureTransparency : uint8_t
	{
		Opaque = 0,
		Translucent = 1,
		Mixed = 2
	};
	struct ITwinTextureData
	{
		int width = 0;
		int height = 0;
		std::optional<ImageSourceFormat> format;
		std::optional<TextureTransparency> transparency;
		std::vector<uint8_t> bytes;
	};

	using OptionalVec3D = std::optional<std::array<double, 3>>;

	using Matrix3x4 = std::array< std::array<double, 4>, 3 >;

	struct ProjectExtents
	{
		std::array<double, 3> low = { 0., 0., 0. };
		std::array<double, 3> high = { 0., 0., 0. };
	};
	struct CartographicOrigin
	{
		double latitude = 0.;
		double longitude = 0.;
		double height = 0.;
	};
	struct EcefLocation
	{
		std::array<double, 3> origin = { 0, 0, 0 };
		Rotator orientation;
		std::optional<Matrix3x4> transform;
		std::optional<CartographicOrigin> cartographicOrigin;
		OptionalVec3D xVector;
		OptionalVec3D yVector;
	};
	struct IModelProperties
	{
		std::optional<ProjectExtents> projectExtents;
		std::optional<EcefLocation> ecefLocation;
		OptionalVec3D globalOrigin;
	};
}
