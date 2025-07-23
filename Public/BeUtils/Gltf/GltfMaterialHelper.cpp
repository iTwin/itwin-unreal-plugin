/*--------------------------------------------------------------------------------------+
|
|     $Source: GltfMaterialHelper.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#include "GltfMaterialHelper.h"

#include <CesiumGltf/Material.h>

#include <SDK/Core/ITwinAPI/ITwinMaterial.inl>
#include <SDK/Core/Tools/Assert.h>
#include <SDK/Core/Tools/Log.h>
#include <SDK/Core/Visualization/MaterialPersistence.h>

#include <fstream>
#include <iostream>
#include <unordered_map>

#include <boost/algorithm/string/predicate.hpp>

#include <spdlog/fmt/fmt.h>

namespace BeUtils
{

//=======================================================================================
//	GltfMaterialHelper::TextureData
//=======================================================================================

GltfMaterialHelper::PerMaterialData::PerMaterialData(AdvViz::SDK::ITwinMaterialProperties const& props)
	: iTwinProps_(props)
{

}

inline std::optional<AdvViz::SDK::ImageSourceFormat> GuessImageSourceFormat(std::string const& texture)
{
	if (boost::iends_with(texture, ".png"))
	{
		return AdvViz::SDK::ImageSourceFormat::Png;
	}
	else if (boost::iends_with(texture, ".jpeg") || boost::iends_with(texture, ".jpg"))
	{
		return AdvViz::SDK::ImageSourceFormat::Jpeg;
	}
	return std::nullopt;
}

void GltfMaterialHelper::TextureData::SetPath(std::filesystem::path const& inPath)
{
	path_ = inPath;

	if (inPath.has_extension())
	{
		// Deduce source format from extension.
		auto const ext = inPath.extension().generic_string();
		if (boost::iequals(ext, ".png"))
		{
			sourceFormatOpt_ = AdvViz::SDK::ImageSourceFormat::Png;
		}
		else if (boost::iequals(ext, ".jpeg") || boost::iequals(ext, ".jpg"))
		{
			sourceFormatOpt_ = AdvViz::SDK::ImageSourceFormat::Jpeg;
		}
		else
		{
			BE_ISSUE("unhandled texture extension", ext);
		}
	}
}


//=======================================================================================
//	GltfMaterialHelper::TextureAccess
//=======================================================================================
bool GltfMaterialHelper::TextureAccess::HasValidCesiumImage(bool bRequirePixelData) const
{
	return cesiumImage
		&& cesiumImage->pAsset
		&& (!bRequirePixelData || !cesiumImage->pAsset->pixelData.empty());
}



//=======================================================================================
//	GltfMaterialHelper
//=======================================================================================

GltfMaterialHelper::GltfMaterialHelper()
{
}

void GltfMaterialHelper::SetITwinMaterialProperties(uint64_t matID, AdvViz::SDK::ITwinMaterialProperties const& props,
	std::string const& nameInIModel, WLock const&)
{
	auto ret = materialMap_.try_emplace(matID, props);

	if (ret.second)
	{
		// A new entry was just created => See if this material contains a customization in current
		// decoration, if any.
		if (persistenceMngr_)
		{
			persistenceMngr_->GetMaterialSettings(iModelID_, matID, ret.first->second.iTwinMaterialDefinition_);
		}
	}
	else
	{
		// The slot already existed => just edit its iTwin properties.
		ret.first->second.iTwinProps_ = props;
	}
	ret.first->second.nameInIModel_ = nameInIModel;

	static const std::unordered_map<std::string, AdvViz::SDK::EChannelType> supportedTypes =
	{
		{ "Pattern", AdvViz::SDK::EChannelType::Color },
	};

	// Gather the different texture IDs referenced by this material
	for (auto const& [strMapType, mapData] : props.maps)
	{
		// Filter supported map types
		auto supportedTypeIt = supportedTypes.find(strMapType);
		if (supportedTypeIt == supportedTypes.end())
			continue;
		std::string const* pTextureId = TryGetMaterialAttribute<std::string>(mapData, "TextureId");
		if (pTextureId)
		{
			// Those textures are identified with a @ITWIN_ prefix, to avoid confusing them with textures
			// selected by user or loaded from the decoration service.
			TextureKey const texKey = { *pTextureId, AdvViz::SDK::ETextureSource::ITwin };

			textureDataMap_.try_emplace(texKey, TextureData{});

			if (persistenceMngr_) // register texture usage now (even though the texture is not loaded yet
				persistenceMngr_->AddTextureUsage(texKey, supportedTypeIt->second);
		}
	}
}

GltfMaterialHelper::MaterialInfo GltfMaterialHelper::CreateITwinMaterialSlot(uint64_t matID,
	std::string const& nameInIModel,
	WLock const&,
	bool bOnlyIfCustomDefinitionExists /*= false*/)
{
	if (bOnlyIfCustomDefinitionExists
		&& !(persistenceMngr_ && persistenceMngr_->HasMaterialDefinition(iModelID_, matID)))
	{
		return { nullptr, nullptr };
	}

	auto ret = materialMap_.try_emplace(matID, AdvViz::SDK::ITwinMaterialProperties{});

	// See if this material contains a customization in current decoration, if any.
	if (persistenceMngr_)
	{
		persistenceMngr_->GetMaterialSettings(iModelID_, matID, ret.first->second.iTwinMaterialDefinition_);
	}
	if (!nameInIModel.empty())
	{
		ret.first->second.nameInIModel_ = nameInIModel;
	}
	return std::make_pair(&ret.first->second.iTwinProps_, &ret.first->second.iTwinMaterialDefinition_);;
}

GltfMaterialHelper::MaterialInfo GltfMaterialHelper::GetITwinMaterialInfo(uint64_t matID, RWLockBase const&) const
{
	auto itMat = materialMap_.find(matID);
	if (itMat != materialMap_.end())
	{
		return std::make_pair(&itMat->second.iTwinProps_, &itMat->second.iTwinMaterialDefinition_);
	}
	return { nullptr, nullptr };
}

bool GltfMaterialHelper::HasCustomDefinition(uint64_t matID, RWLockBase const& lock) const
{
	auto const materialInfo = GetITwinMaterialInfo(matID, lock);

	// If the material uses custom settings, activate advanced conversion so that the tuning
	// can handle it.
	auto const* itwinMat = materialInfo.second;
	return itwinMat && AdvViz::SDK::HasCustomSettings(*itwinMat);
}

bool GltfMaterialHelper::SetCurrentAlphaMode(uint64_t matID, std::string const& alphaMode, WLock const&)
{
	auto itMat = materialMap_.find(matID);
	if (itMat != materialMap_.end())
	{
		itMat->second.currentAlphaMode_ = alphaMode;
		return true;
	}
	return false;
}

bool GltfMaterialHelper::GetCurrentAlphaMode(uint64_t matID, std::string& alphaMode, RLock const&) const
{
	auto itMat = materialMap_.find(matID);
	if (itMat != materialMap_.end()
		&& !itMat->second.currentAlphaMode_.empty())
	{
		alphaMode = itMat->second.currentAlphaMode_;
		return true;
	}
	return false;
}

void GltfMaterialHelper::StoreInitialAlphaModeIfNeeded(uint64_t matID, std::string& outCurrentAlphaMode, WLock const& lock)
{
	auto itMat = materialMap_.find(matID);
	if (itMat == materialMap_.end())
	{
		BE_ISSUE("unknown mat ID", matID);
		return;
	}
	if (itMat->second.currentAlphaMode_.empty())
	{
		UpdateCurrentAlphaMode(matID, std::nullopt, lock);
	}
	outCurrentAlphaMode = itMat->second.currentAlphaMode_;
}

/*static*/
double GltfMaterialHelper::GetChannelDefaultIntensity(AdvViz::SDK::EChannelType channel,
	AdvViz::SDK::ITwinMaterialProperties const& itwinProps)
{
	// Currently, the materials exported by the Mesh Export Service are not really PBR, but do initialize
	// some of the glTF (PBR) material properties with non-zero constants.
	// For now, unless iTwin material become themselves PBR, use those constants here to match
	// the default settings used when cesium tiles are loaded (so that the percentage displayed in the UI
	// matches the actual PBR value of the Cesium material).
	//
	// The corresponding code can be found in tileset-publisher, GltfModelMaker::AddMaterial.
	//
	// If you add the handling of another channel here, please update #CompleteDefinitionWithDefaultValues to
	// retrieve the correct value when saving to DB.
	switch (channel)
	{
	case AdvViz::SDK::EChannelType::Metallic:
	{
		// Use same formula as in Mesh Export Service
		// Since https://github.com/iTwin/imodel-native-internal/pull/698
		// (see ConvertMaterialToMetallicRoughness in <imodel-native-internal>/iModelCore/Visualization/TilesetPublisher/tiler/CesiumTileWriter.cpp)
		double const* pSpecularValue = TryGetMaterialProperty<double>(itwinProps, "specular");
		if (pSpecularValue
			&& *pSpecularValue > 0.25
			&& GetChannelDefaultColorMap(AdvViz::SDK::EChannelType::Color, itwinProps).IsEmpty())
		{
			return 1.;
		}
		else
		{
			return 0.;
		}
	}
	case AdvViz::SDK::EChannelType::Roughness:
	{
		// Idem: see ConvertMaterialToMetallicRoughness
		// Specular exponent is named "finish" in IModelReadRpcInterface.
		double specularExponent = 0.;
		if (GetMaterialBoolProperty(itwinProps, "HasFinish"))
		{
			double const* pFinishValue = TryGetMaterialProperty<double>(itwinProps, "finish");
			if (pFinishValue)
			{
				specularExponent = fabs(*pFinishValue);
			}
			else
			{
				// default value found in https://github.com/iTwin/itwinjs-core/blob/release/4.8.x/core/common/src/MaterialProps.ts#L191
				specularExponent = 13.5;
			}
		}
		return sqrt(2.0 / (specularExponent + 2.0));
	}

	case AdvViz::SDK::EChannelType::Color:
	case AdvViz::SDK::EChannelType::Normal:
	case AdvViz::SDK::EChannelType::AmbientOcclusion:
	{
		return 1.;
	}

	case AdvViz::SDK::EChannelType::Alpha:
	case AdvViz::SDK::EChannelType::Transparency:
	{
		double itwinTransparency = 0.;
		// for opacity/alpha, test the 'transmit' setting of the original material
		// see https://www.itwinjs.org/reference/core-backend/elements/rendermaterialelement/rendermaterialelement.params/transmit/
		double const* pTransmitValue = TryGetMaterialProperty<double>(itwinProps, "transmit");
		if (pTransmitValue)
		{
			BE_ASSERT(*pTransmitValue >= 0. && *pTransmitValue <= 1.);
			itwinTransparency = std::clamp(*pTransmitValue, 0., 1.);
		}
		return channel == AdvViz::SDK::EChannelType::Transparency
			? itwinTransparency
			: (1. - itwinTransparency);
	}
	default:
		break;
	}

	// All other channels take zero as default
	return 0.;
}

namespace
{
	inline bool ApproxEquals(double const& val1, double const& val2)
	{
		return std::fabs(val1 - val2) < 1e-5;
	}
	inline bool ApproxEquals(AdvViz::SDK::ITwinColor const& color1, AdvViz::SDK::ITwinColor const& color2)
	{
		return (std::fabs(color1[0] - color2[0]) < 1e-4)
			&& (std::fabs(color1[1] - color2[1]) < 1e-4)
			&& (std::fabs(color1[2] - color2[2]) < 1e-4)
			&& (std::fabs(color1[3] - color2[3]) < 1e-4);
	}
	inline bool ApproxEquals(AdvViz::SDK::ITwinChannelMap const& map1, AdvViz::SDK::ITwinChannelMap const& map2)
	{
		return map1.texture == map2.texture;
	}
	inline bool ApproxEquals(AdvViz::SDK::EMaterialKind const& val1, AdvViz::SDK::EMaterialKind const& val2)
	{
		return val1 == val2;
	}
	inline bool ApproxEquals(AdvViz::SDK::ITwinUVTransform const& tsf1, AdvViz::SDK::ITwinUVTransform const& tsf2)
	{
		if (std::fabs(tsf1.offset[0] - tsf2.offset[0]) > 1e-4) return false;
		if (std::fabs(tsf1.offset[1] - tsf2.offset[1]) > 1e-4) return false;
		if (std::fabs(tsf1.scale[0] - tsf2.scale[0]) > 1e-4) return false;
		if (std::fabs(tsf1.scale[1] - tsf2.scale[1]) > 1e-4) return false;
		if (std::fabs(tsf1.rotation - tsf2.rotation) > 1e-4) return false;
		return true;
	}
	inline bool ApproxEquals(std::string const& val1, std::string const& val2)
	{
		return val1 == val2;
	}

	template <typename ParamType>
	struct ParamHelperBase
	{
		using ParameterType = ParamType;

		GltfMaterialHelper& gltfMatHelper_;
		AdvViz::SDK::EChannelType const channel_;
		ParamType const newValue_;

		ParamHelperBase(BeUtils::GltfMaterialHelper& GltfHelper,
			AdvViz::SDK::EChannelType InChannel,
			ParamType const& NewParamValue)
			: gltfMatHelper_(GltfHelper)
			, channel_(InChannel)
			, newValue_(NewParamValue)
		{}

		bool DoesNewValueDifferFrom(ParameterType const& oldValue) const
		{
			return !ApproxEquals(this->newValue_, oldValue);
		}
	};

	class IntensityHelper : public ParamHelperBase<double>
	{
		using Super = ParamHelperBase<double>;

	public:
		using ParamType = double;

		IntensityHelper(GltfMaterialHelper& GltfHelper,
						AdvViz::SDK::EChannelType channel,
						double newIntensity)
			: Super(GltfHelper, channel, newIntensity)
		{}

		ParamType GetCurrentValue(uint64_t matID, WLock const& lock) const
		{
			return this->gltfMatHelper_.GetChannelIntensity(matID, this->channel_, lock);
		}

		void SetNewValue(uint64_t matID, AdvViz::SDK::ITwinMaterial& matDefinition, WLock const& lock) const
		{
			if (this->channel_ == AdvViz::SDK::EChannelType::Color
				&& !matDefinition.DefinesChannel(this->channel_))
			{
				// First time we edit the Color channel: bake some default values.
				matDefinition.SetChannelColor(this->channel_,
					this->gltfMatHelper_.GetChannelColor(matID, this->channel_, lock));
			}
			matDefinition.SetChannelIntensity(this->channel_, this->newValue_);
		}

		void OnModificationApplied(AdvViz::SDK::MaterialPersistenceManager& /*persistenceMngr*/) const
		{

		}
	};

	class IntensityMapHelper : public ParamHelperBase<AdvViz::SDK::ITwinChannelMap>
	{
		using Super = ParamHelperBase<AdvViz::SDK::ITwinChannelMap>;

	public:
		using ParamType = AdvViz::SDK::ITwinChannelMap;

		IntensityMapHelper(GltfMaterialHelper& GltfHelper,
			AdvViz::SDK::EChannelType channel,
			AdvViz::SDK::ITwinChannelMap const& newIntensityMap)
			: Super(GltfHelper, channel, newIntensityMap)
		{}

		ParamType GetCurrentValue(uint64_t matID, WLock const& lock) const
		{
			return this->gltfMatHelper_.GetChannelIntensityMap(matID, this->channel_, lock);
		}

		void SetNewValue(uint64_t matID, AdvViz::SDK::ITwinMaterial& matDefinition, WLock const& lock) const
		{
			if (!matDefinition.DefinesChannel(this->channel_))
			{
				// Make sure we bake the default intensity for this channel before adding the texture
				matDefinition.SetChannelIntensity(this->channel_,
					this->gltfMatHelper_.GetChannelIntensity(matID, this->channel_, lock));
			}
			matDefinition.SetChannelIntensityMap(this->channel_, this->newValue_);
		}

		void OnModificationApplied(AdvViz::SDK::MaterialPersistenceManager& persistenceMngr) const
		{
			// Register usage in the manager for the texture.
			persistenceMngr.AddTextureUsage({ this->newValue_.texture, this->newValue_.eSource }, this->channel_);
		}
	};

	class ColorHelper : public ParamHelperBase<AdvViz::SDK::ITwinColor>
	{
		using Super = ParamHelperBase<AdvViz::SDK::ITwinColor>;

	public:
		using ParamType = AdvViz::SDK::ITwinColor;

		ColorHelper(GltfMaterialHelper& gltfHelper,
			AdvViz::SDK::EChannelType channel,
			AdvViz::SDK::ITwinColor const& newColor)
			: Super(gltfHelper, channel, newColor)
		{}

		ParamType GetCurrentValue(uint64_t matID, WLock const& lock) const
		{
			return this->gltfMatHelper_.GetChannelColor(matID, this->channel_, lock);
		}

		void SetNewValue(uint64_t matID, AdvViz::SDK::ITwinMaterial& matDefinition, WLock const& lock) const
		{
			if (!matDefinition.DefinesChannel(this->channel_))
			{
				// First time we edit color channel: make sure we initialize the default intensity correctly.
				// (the default value for the color texture factor is 1.0).
				matDefinition.SetChannelIntensity(this->channel_,
					this->gltfMatHelper_.GetChannelIntensity(matID, this->channel_, lock));
			}
			matDefinition.SetChannelColor(this->channel_, this->newValue_);
		}

		void OnModificationApplied(AdvViz::SDK::MaterialPersistenceManager& /*persistenceMngr*/) const
		{

		}
	};

	class ColorMapHelper : public ParamHelperBase<AdvViz::SDK::ITwinChannelMap>
	{
		using Super = ParamHelperBase<AdvViz::SDK::ITwinChannelMap>;

	public:
		using ParamType = AdvViz::SDK::ITwinChannelMap;

		ColorMapHelper(GltfMaterialHelper& GltfHelper,
			AdvViz::SDK::EChannelType channel,
			AdvViz::SDK::ITwinChannelMap const& newColorMap)
			: Super(GltfHelper, channel, newColorMap)
		{}

		ParamType GetCurrentValue(uint64_t matID, WLock const& lock) const
		{
			return this->gltfMatHelper_.GetChannelColorMap(matID, this->channel_, lock);
		}

		void SetNewValue(uint64_t matID, AdvViz::SDK::ITwinMaterial& matDefinition, WLock const& lock) const
		{
			if (!matDefinition.DefinesChannel(this->channel_))
			{
				// First time we edit this channel: make sure we initialize the default value(s) correctly,
				// in order not to alter the material too much!
				const double defaultIntensity =
					this->gltfMatHelper_.GetChannelIntensity(matID, this->channel_, lock);

				if (this->channel_ == AdvViz::SDK::EChannelType::Color)
				{
					// Store the current base color as well as the color texture default intensity in
					// the custom definition.
					matDefinition.SetChannelColor(this->channel_,
						this->gltfMatHelper_.GetChannelColor(matID, this->channel_, lock));
				}
				matDefinition.SetChannelIntensity(this->channel_, defaultIntensity);
			}
			matDefinition.SetChannelColorMap(this->channel_, this->newValue_);
		}

		void OnModificationApplied(AdvViz::SDK::MaterialPersistenceManager& persistenceMngr) const
		{
			// Register usage in the manager for the texture.
			persistenceMngr.AddTextureUsage({ this->newValue_.texture, this->newValue_.eSource }, this->channel_);
		}
	};

	bool FindColorMapTexture(AdvViz::SDK::ITwinMaterialProperties const& itwinProps,
		std::string& outColorTexId)
	{
		// See if the Dgn material has a color texture.
		auto itColorMap = itwinProps.maps.find("Pattern");
		if (itColorMap != itwinProps.maps.end())
		{
			std::string const* itwinColorTexId = TryGetMaterialAttribute<std::string>(itColorMap->second, "TextureId");
			if (itwinColorTexId != nullptr)
			{
				outColorTexId = *itwinColorTexId;
				return true;
			}
		}
		return false;
	}

	class UVTransformHelper : public ParamHelperBase<AdvViz::SDK::ITwinUVTransform>
	{
		using Super = ParamHelperBase<AdvViz::SDK::ITwinUVTransform>;

	public:
		using ParamType = AdvViz::SDK::ITwinUVTransform;

		UVTransformHelper(GltfMaterialHelper& GltfHelper,
			AdvViz::SDK::ITwinUVTransform const& newUVTransform)
			: Super(GltfHelper, AdvViz::SDK::EChannelType::ENUM_END, newUVTransform)
		{}

		ParamType GetCurrentValue(uint64_t matID, WLock const& lock) const
		{
			return this->gltfMatHelper_.GetUVTransform(matID, lock);
		}

		void SetNewValue(uint64_t /*matID*/, AdvViz::SDK::ITwinMaterial& matDefinition, WLock const&) const
		{
			matDefinition.uvTransform = this->newValue_;
		}

		void OnModificationApplied(AdvViz::SDK::MaterialPersistenceManager& /*persistenceMngr*/) const
		{

		}
	};

	class MaterialKindHelper : public ParamHelperBase<AdvViz::SDK::EMaterialKind>
	{
		using Super = ParamHelperBase<AdvViz::SDK::EMaterialKind>;

	public:
		using ParamType = AdvViz::SDK::EMaterialKind;

		MaterialKindHelper(GltfMaterialHelper& GltfHelper,
			AdvViz::SDK::EMaterialKind const& newKind)
			: Super(GltfHelper, AdvViz::SDK::EChannelType::ENUM_END, newKind)
		{}

		ParamType GetCurrentValue(uint64_t matID, WLock const& lock) const
		{
			return this->gltfMatHelper_.GetMaterialKind(matID, lock);
		}

		void SetNewValue(uint64_t /*matID*/, AdvViz::SDK::ITwinMaterial& matDefinition, WLock const&) const
		{
			matDefinition.kind = this->newValue_;
		}

		void OnModificationApplied(AdvViz::SDK::MaterialPersistenceManager& /*persistenceMngr*/) const
		{

		}
	};

	class MaterialNameHelper : public ParamHelperBase<std::string>
	{
		using Super = ParamHelperBase<std::string>;

	public:
		using ParamType = std::string;

		MaterialNameHelper(GltfMaterialHelper& GltfHelper,
			std::string const& newName)
			: Super(GltfHelper, AdvViz::SDK::EChannelType::ENUM_END, newName)
		{}

		ParamType GetCurrentValue(uint64_t matID, WLock const& lock) const
		{
			return this->gltfMatHelper_.GetMaterialName(matID, lock);
		}

		void SetNewValue(uint64_t /*matID*/, AdvViz::SDK::ITwinMaterial& matDefinition, WLock const&) const
		{
			matDefinition.displayName = this->newValue_;
		}

		void OnModificationApplied(AdvViz::SDK::MaterialPersistenceManager& /*persistenceMngr*/) const
		{

		}
	};

}

double GltfMaterialHelper::GetChannelIntensity(uint64_t matID, AdvViz::SDK::EChannelType channel, RWLockBase const&) const
{
	auto itMat = materialMap_.find(matID);
	if (itMat != materialMap_.end())
	{
		auto const& matDefinition = itMat->second.iTwinMaterialDefinition_;
		std::optional<double> intensityOpt = matDefinition.GetChannelIntensityOpt(channel);
		if (intensityOpt)
			return *intensityOpt;
		return GetChannelDefaultIntensity(channel, itMat->second.iTwinProps_);
	}
	else
	{
		return GetChannelDefaultIntensity(channel, {});
	}
}

double GltfMaterialHelper::GetChannelIntensity(uint64_t matID, AdvViz::SDK::EChannelType channel) const
{
	RLock lock(mutex_);
	return GetChannelIntensity(matID, channel, lock);
}

void GltfMaterialHelper::CompleteDefinitionWithDefaultValues(AdvViz::SDK::ITwinMaterial& matDefinition,
	uint64_t matID, RWLockBase const& lock) const
{
	for (auto eChan : {
		AdvViz::SDK::EChannelType::Color,
		AdvViz::SDK::EChannelType::Metallic,
		AdvViz::SDK::EChannelType::Roughness,
		AdvViz::SDK::EChannelType::Opacity })
	{
		matDefinition.SetChannelIntensity(eChan, GetChannelIntensity(matID, eChan, lock));
	}
	for (auto eChan : { AdvViz::SDK::EChannelType::Color })
	{
		matDefinition.SetChannelColor(eChan, GetChannelColor(matID, eChan, lock));
		matDefinition.SetChannelColorMap(eChan, GetChannelColorMap(matID, eChan, lock));
	}
}

template <typename ParamHelper>
void GltfMaterialHelper::TSetChannelParam(ParamHelper const& helper, uint64_t matID, bool& bValueModified)
{
	using ParameterType = typename ParamHelper::ParamType;

	bValueModified = false;

	WLock lock(mutex_);
	auto itMat = materialMap_.find(matID);
	if (itMat != materialMap_.end())
	{
		AdvViz::SDK::ITwinMaterial& matDefinition = itMat->second.iTwinMaterialDefinition_;

		ParameterType const oldValue = helper.GetCurrentValue(matID, lock);
		bValueModified = helper.DoesNewValueDifferFrom(oldValue);
		helper.SetNewValue(matID, matDefinition, lock);

		if (bValueModified && persistenceMngr_)
		{
			// Notify the persistence manager for future DB update.
			// Note that we now pass the *full* definition of the material, because there is no guarantee
			// that default values are the same in our plugin and in the decoration service.
			AdvViz::SDK::ITwinMaterial matDefToStore(matDefinition);
			CompleteDefinitionWithDefaultValues(matDefToStore, matID, lock);
			persistenceMngr_->SetMaterialSettings(iModelID_, matID, matDefToStore);

			helper.OnModificationApplied(*persistenceMngr_);
		}
	}
}


void GltfMaterialHelper::SetChannelIntensity(uint64_t matID, AdvViz::SDK::EChannelType channel,
	double intensity, bool& bValueModified)
{
	IntensityHelper intensitySetter(*this, channel, intensity);
	TSetChannelParam(intensitySetter, matID, bValueModified);
}


/*static*/
GltfMaterialHelper::ITwinColor GltfMaterialHelper::GetChannelDefaultColor(AdvViz::SDK::EChannelType channel,
	AdvViz::SDK::ITwinMaterialProperties const& itwinProps)
{
	// If you add the handling of another channel here, please update #CompleteDefinitionWithDefaultValues to
	// retrieve the correct color when saving to DB.

	if (channel == AdvViz::SDK::EChannelType::Color)
	{
		// See if the material has a base color.
		using RgbColor = std::array<double, 3>;
		RgbColor const* pBaseColor = nullptr;

		// Note that if the material also has a color texture, we ignore the base color, as done in the
		// Mesh Export Service (see boolean textureShouldOverrideColor in ConvertMaterialsToCesium).
		std::string itwinColorTexId;
		bool const hasColorTexture = FindColorMapTexture(itwinProps, itwinColorTexId);
		if (!hasColorTexture
			&& GetMaterialBoolProperty(itwinProps, "HasBaseColor"))
		{
			pBaseColor = TryGetMaterialProperty<RgbColor>(itwinProps, "color");
		}
		if (pBaseColor)
		{
			return {
				(*pBaseColor)[0],
				(*pBaseColor)[1],
				(*pBaseColor)[2],
				1.
			};
		}
		else
		{
			// Default value filled by the Mesh Export Service is white
			// (again, see corresponding code in TilesetPublisher/tiler/GltfModelMaker.cpp
			// -> GltfModelMaker::AddMaterial)
			return { 1., 1., 1., 1. };
		}
	}
	return { 0., 0., 0., 1. };
}

GltfMaterialHelper::ITwinColor GltfMaterialHelper::GetChannelColor(uint64_t matID, AdvViz::SDK::EChannelType channel, RWLockBase const&) const
{
	auto itMat = materialMap_.find(matID);
	if (itMat != materialMap_.end())
	{
		auto const& matDefinition = itMat->second.iTwinMaterialDefinition_;
		std::optional<ITwinColor> colorOpt = matDefinition.GetChannelColorOpt(channel);
		if (colorOpt)
			return *colorOpt;
		return GetChannelDefaultColor(channel, itMat->second.iTwinProps_);
	}
	else
	{
		return GetChannelDefaultColor(channel, {});
	}
}

GltfMaterialHelper::ITwinColor GltfMaterialHelper::GetChannelColor(uint64_t matID, AdvViz::SDK::EChannelType channel) const
{
	RLock lock(mutex_);
	return GetChannelColor(matID, channel, lock);
}

void GltfMaterialHelper::SetChannelColor(uint64_t matID, AdvViz::SDK::EChannelType channel, ITwinColor const& color, bool& bValueModified)
{
	ColorHelper colorSetter(*this, channel, color);
	TSetChannelParam(colorSetter, matID, bValueModified);
}


/*static*/
AdvViz::SDK::ITwinChannelMap GltfMaterialHelper::GetChannelDefaultIntensityMap(AdvViz::SDK::EChannelType /*channel*/,
	AdvViz::SDK::ITwinMaterialProperties const& /*itwinProps*/)
{
	// If you add the handling of another channel here, please update #CompleteDefinitionWithDefaultValues to
	// retrieve the correct intensity map when saving to DB.
	// Also update #MaterialUsingTextures for the detection of the first texture map, and complete
	// #ListITwinTexturesToResolve.

	// I only found bump maps in Dgn material properties (and we do not expose them...)
	return {};
}

AdvViz::SDK::ITwinChannelMap GltfMaterialHelper::GetChannelIntensityMap(uint64_t matID, AdvViz::SDK::EChannelType channel, RWLockBase const&) const
{
	auto itMat = materialMap_.find(matID);
	if (itMat != materialMap_.end())
	{
		auto const& matDefinition = itMat->second.iTwinMaterialDefinition_;
		auto intensityMapOpt = matDefinition.GetChannelIntensityMapOpt(channel);
		if (intensityMapOpt)
			return *intensityMapOpt;
		return GetChannelDefaultIntensityMap(channel, itMat->second.iTwinProps_);
	}
	else
	{
		return GetChannelDefaultIntensityMap(channel, {});
	}
}

AdvViz::SDK::ITwinChannelMap GltfMaterialHelper::GetChannelIntensityMap(uint64_t matID, AdvViz::SDK::EChannelType channel) const
{
	RLock lock(mutex_);
	return GetChannelIntensityMap(matID, channel, lock);
}

namespace Detail
{
	extern bool RequiresCesiumBlendMode(GltfMaterialHelper::TextureAccess const& tex,
		GltfMaterialHelper const& matHelper, AdvViz::SDK::EChannelType channel,
		WLock const& lock, std::optional<uint64_t> const& matIdForLogs);
}


bool GltfMaterialHelper::TextureRequiringTranslucencyImpl(TextureKey const& textureKey,
	AdvViz::SDK::EChannelType channel,
	std::optional<uint64_t> const& matIdForLogs,
	WLock const& lock,
	bool bCacheResult /*= true*/)
{
	auto itTex = textureDataMap_.find(textureKey);
	if (itTex == textureDataMap_.end())
	{
		BE_ISSUE("unknown texture", textureKey.id);
		return false;
	}
	std::optional<bool>& needTranslucencyOpt = itTex->second.needTranslucencyOpt_;
	if (needTranslucencyOpt)
	{
		// Already computed before
		return *needTranslucencyOpt;
	}

	auto const& ImgSourceFormat = itTex->second.sourceFormatOpt_;
	// Optimization for color map coming from jpeg: we know it won't produce relevant alpha
	if (channel == AdvViz::SDK::EChannelType::Color
		&& ImgSourceFormat
		&& *ImgSourceFormat == AdvViz::SDK::ImageSourceFormat::Jpeg)
	{
		if (bCacheResult)
		{
			needTranslucencyOpt = false;
		}
		return false;
	}

	// First time we request the translucency mode for this texture: compute it now by inspecting pixels
	bool bNeedTranslucency = Detail::RequiresCesiumBlendMode(TextureAccess{
			itTex->second.path_,
			itTex->second.GetCesiumImage(),
			textureKey
		}, *this, channel, lock, matIdForLogs);
	if (bCacheResult)
	{
		needTranslucencyOpt = bNeedTranslucency;
	}
	return bNeedTranslucency;
}

bool GltfMaterialHelper::TextureRequiringTranslucency(AdvViz::SDK::ITwinChannelMap const& texMap,
	AdvViz::SDK::EChannelType channel,
	uint64_t matId,
	WLock const& lock)
{
	TextureKey const textureKey = { texMap.texture, texMap.eSource };
	return TextureRequiringTranslucencyImpl(textureKey, channel, matId, lock);
}

bool GltfMaterialHelper::TestTranslucencyRequirement(AdvViz::SDK::TextureKey const& textureKey,
	AdvViz::SDK::TextureUsage const& textureUsage,
	WLock const& lock,
	std::optional<uint64_t> const& matIdForLogs /*= std::nullopt*/)
{
	auto itTex = textureDataMap_.find(textureKey);
	if (itTex == textureDataMap_.end())
	{
		BE_ISSUE("unknown texture", textureKey.id);
		return false;
	}
	std::optional<AdvViz::SDK::ImageSourceFormat> ImgSourceFormat =
		GuessImageSourceFormat(textureKey.id);
	if (ImgSourceFormat && !itTex->second.sourceFormatOpt_)
	{
		itTex->second.sourceFormatOpt_ = ImgSourceFormat;
	}
	bool bNeedTranslucency(false);
	if (textureUsage.HasChannel(AdvViz::SDK::EChannelType::Color))
		bNeedTranslucency |= TextureRequiringTranslucencyImpl(textureKey,
			AdvViz::SDK::EChannelType::Color, matIdForLogs, lock, false);
	if (textureUsage.HasChannel(AdvViz::SDK::EChannelType::Alpha))
		bNeedTranslucency |= TextureRequiringTranslucencyImpl(textureKey,
			AdvViz::SDK::EChannelType::Alpha, matIdForLogs, lock, false);
	std::optional<bool>& needTranslucencyOpt = itTex->second.needTranslucencyOpt_;
	needTranslucencyOpt = bNeedTranslucency;
	return bNeedTranslucency;
}

void GltfMaterialHelper::UpdateCurrentAlphaMode(uint64_t matID,
	std::optional<bool> const& bHasTextureRequiringTranslucency,
	WLock const& lock)
{
	std::string alphaMode = CesiumGltf::Material::AlphaMode::MASK;

	double const alphaIntens = GetChannelIntensity(matID, AdvViz::SDK::EChannelType::Alpha, lock);
	if (alphaIntens > 1e-5 && (alphaIntens < (1 - 1e-5)))
	{
		alphaMode = CesiumGltf::Material::AlphaMode::BLEND;
	}
	else if (bHasTextureRequiringTranslucency)
	{
		if (*bHasTextureRequiringTranslucency)
		{
			alphaMode = CesiumGltf::Material::AlphaMode::BLEND;
		}
	}
	else
	{
		// Look in alpha map and then color map
		for (AdvViz::SDK::EChannelType chan : { AdvViz::SDK::EChannelType::Alpha, AdvViz::SDK::EChannelType::Color })
		{
			auto const texMap = GetChannelMap(matID, chan, lock);
			if (texMap.HasTexture()
				&& TextureRequiringTranslucency(texMap, chan, matID, lock))
			{
				alphaMode = CesiumGltf::Material::AlphaMode::BLEND;
				break;
			}
		}
	}
	SetCurrentAlphaMode(matID, alphaMode, lock);
}

void GltfMaterialHelper::UpdateCurrentAlphaMode(uint64_t matID,
	std::optional<bool> const& bHasTextureRequiringTranslucency /*= std::nullopt*/)
{
	WLock lock(mutex_);
	UpdateCurrentAlphaMode(matID, bHasTextureRequiringTranslucency, lock);
}

void GltfMaterialHelper::SetChannelIntensityMap(uint64_t matID, AdvViz::SDK::EChannelType channel, ITwinChannelMap const& intensityMap, bool& bValueModified)
{
	IntensityMapHelper mapSetter(*this, channel, intensityMap);
	TSetChannelParam(mapSetter, matID, bValueModified);
}


/*static*/
AdvViz::SDK::ITwinChannelMap GltfMaterialHelper::GetChannelDefaultColorMap(AdvViz::SDK::EChannelType channel,
	AdvViz::SDK::ITwinMaterialProperties const& itwinProps)
{
	// If you add the handling of another channel here, please update #CompleteDefinitionWithDefaultValues to
	// retrieve the correct color map when saving to DB.
	// Also update #MaterialUsingTextures for the detection of the first texture map, and complete
	// #ListITwinTexturesToResolve.

	if (channel == AdvViz::SDK::EChannelType::Color)
	{
		std::string itwinColorTexId;
		if (FindColorMapTexture(itwinProps, itwinColorTexId))
		{
			return ITwinChannelMap{
				.texture = itwinColorTexId,
				.eSource = AdvViz::SDK::ETextureSource::ITwin
			};
		}
	}
	return {};
}

AdvViz::SDK::ITwinChannelMap GltfMaterialHelper::GetChannelColorMap(uint64_t matID, AdvViz::SDK::EChannelType channel, RWLockBase const&) const
{
	auto itMat = materialMap_.find(matID);
	if (itMat != materialMap_.end())
	{
		auto const& matDefinition = itMat->second.iTwinMaterialDefinition_;
		auto colorMapOpt = matDefinition.GetChannelColorMapOpt(channel);
		if (colorMapOpt)
			return *colorMapOpt;
		return GetChannelDefaultColorMap(channel, itMat->second.iTwinProps_);
	}
	else
	{
		return GetChannelDefaultColorMap(channel, {});
	}
}

AdvViz::SDK::ITwinChannelMap GltfMaterialHelper::GetChannelColorMap(uint64_t matID, AdvViz::SDK::EChannelType channel) const
{
	RLock lock(mutex_);
	return GetChannelColorMap(matID, channel, lock);
}

void GltfMaterialHelper::SetChannelColorMap(uint64_t matID, AdvViz::SDK::EChannelType channel, ITwinChannelMap const& colorMap, bool& bValueModified)
{
	ColorMapHelper colorMapSetter(*this, channel, colorMap);
	TSetChannelParam(colorMapSetter, matID, bValueModified);
}

AdvViz::SDK::ITwinChannelMap GltfMaterialHelper::GetChannelMap(uint64_t matID, AdvViz::SDK::EChannelType channel, RWLockBase const& lock) const
{
	// Distinguish color from intensity textures.
	if (channel == AdvViz::SDK::EChannelType::Color
		|| channel == AdvViz::SDK::EChannelType::Normal)
	{
		return GetChannelColorMap(matID, channel, lock);
	}
	else
	{
		// For other channels, the map defines an intensity
		return GetChannelIntensityMap(matID, channel, lock);
	}
}

AdvViz::SDK::ITwinChannelMap GltfMaterialHelper::GetChannelMap(uint64_t matID, AdvViz::SDK::EChannelType channel) const
{
	RLock lock(mutex_);
	return GetChannelMap(matID, channel, lock);
}

bool GltfMaterialHelper::HasChannelMap(uint64_t matID, AdvViz::SDK::EChannelType channel, RWLockBase const& lock) const
{
	return GetChannelMap(matID, channel, lock).HasTexture();
}

bool GltfMaterialHelper::HasChannelMap(uint64_t matID, AdvViz::SDK::EChannelType channel) const
{
	RLock lock(mutex_);
	return HasChannelMap(matID, channel, lock);
}

bool GltfMaterialHelper::MaterialUsingTextures(uint64_t matID, RLock const&) const
{
	auto itMat = materialMap_.find(matID);
	if (itMat != materialMap_.end())
	{
		auto const& matDefinition = itMat->second.iTwinMaterialDefinition_;
		if (matDefinition.HasTextureMap())
			return true;
		// Test default settings (those exported by the MES)
		// For now, only Color is available, but this code should be synchronized with
		// #GetChannelDefaultColorMap and #GetChannelDefaultIntensityMap...
		if (!GetChannelDefaultColorMap(AdvViz::SDK::EChannelType::Color, itMat->second.iTwinProps_).IsEmpty())
			return true;
	}
	return false;
}

bool GltfMaterialHelper::GetMaterialFullDefinition(uint64_t matID, AdvViz::SDK::ITwinMaterial& matDefinition, RWLockBase const& lock) const
{
	auto itMat = materialMap_.find(matID);
	if (itMat == materialMap_.end())
	{
		return false;
	}
	matDefinition = itMat->second.iTwinMaterialDefinition_;

	// Use default values for all properties which were not customized:
	CompleteDefinitionWithDefaultValues(matDefinition, matID, lock);

	return true;
}

bool GltfMaterialHelper::GetMaterialFullDefinition(uint64_t matID, AdvViz::SDK::ITwinMaterial& matDefinition) const
{
	RLock lock(mutex_);
	return GetMaterialFullDefinition(matID, matDefinition, lock);
}

void GltfMaterialHelper::SetMaterialFullDefinition(uint64_t matID,
												   AdvViz::SDK::ITwinMaterial const& matDefinition,
												   WLock const&)
{
	auto itMat = materialMap_.find(matID);
	if (itMat != materialMap_.end())
	{
		itMat->second.iTwinMaterialDefinition_ = matDefinition;
	}
	if (persistenceMngr_)
	{
		persistenceMngr_->SetMaterialSettings(iModelID_, matID, matDefinition);
	}
}

void GltfMaterialHelper::SetMaterialFullDefinition(uint64_t matID, AdvViz::SDK::ITwinMaterial const& matDefinition)
{
	WLock lock(mutex_);
	SetMaterialFullDefinition(matID, matDefinition, lock);
}

void GltfMaterialHelper::SetTextureDirectory(std::filesystem::path const& textureDir, WLock const&)
{
	textureDir_ = textureDir;
	hasValidTextureDir_.reset(); // Enforce check next time we initiate a download
}

bool GltfMaterialHelper::CheckTextureDir(std::string& strError, WLock const&)
{
	if (hasValidTextureDir_)
	{
		return *hasValidTextureDir_;
	}
	hasValidTextureDir_ = [&](std::string& strErr) -> bool
	{
		if (textureDir_.empty())
		{
			strErr = "texture directory not set - cannot download textures";
			return false;
		}
		std::error_code ec;
		if (!std::filesystem::is_directory(textureDir_, ec))
		{
			ec.clear();
			if (!std::filesystem::create_directories(textureDir_, ec))
			{
				strErr = ec.message();
				return false;
			}
		}
		return true;
	}(strError);
	return *hasValidTextureDir_;
}

void GltfMaterialHelper::FlushTextureDirectory()
{
	WLock lock(mutex_);
	std::string strErr;
	if (CheckTextureDir(strErr, lock))
	{
		std::error_code ec;
		std::filesystem::remove_all(textureDir_, ec);
		hasValidTextureDir_.reset(); // Enforce recreate texture directory next time we initiate a download
	}
}

bool GltfMaterialHelper::SetITwinTextureData(std::string const& itwinTextureID, AdvViz::SDK::ITwinTextureData const& textureData,
	std::filesystem::path& outTexturePath)
{
	if (textureData.bytes.empty() || textureData.width == 0 || textureData.height == 0)
	{
		BE_ISSUE("empty texture", itwinTextureID);
		return false;
	}
	if (!textureData.format)
	{
		BE_ISSUE("unknown texture format");
		return false;
	}
	WLock lock(mutex_);

	std::string dirError;
	if (!CheckTextureDir(dirError, lock))
	{
		BE_ISSUE("texture directory error: ", dirError);
		return false;
	}
	TextureKey const textureKey = { itwinTextureID, AdvViz::SDK::ETextureSource::ITwin };
	auto itTex = textureDataMap_.find(textureKey);
	if (itTex == textureDataMap_.end())
	{
		BE_ISSUE("unknown iTwin texture", itwinTextureID);
		return false;
	}
	std::filesystem::path& outputPath(itTex->second.path_);
	outputPath = textureDir_ / itwinTextureID;
	// Save the texture - build a path depending on its format.
	switch (*textureData.format)
	{
	case AdvViz::SDK::ImageSourceFormat::Jpeg: outputPath += ".jpg"; break;
	case AdvViz::SDK::ImageSourceFormat::Png: outputPath += ".png"; break;
	case AdvViz::SDK::ImageSourceFormat::Svg:
		BE_ISSUE("unsupported format for Cesium texture!", *textureData.format);
		return false;
	}
	bool writeOK = false;
	try
	{
		std::ofstream output(outputPath, std::ios::out | std::ios::binary);
		writeOK = output.is_open()
			&& output.write(
				reinterpret_cast<const char*>(textureData.bytes.data()),
				textureData.bytes.size());
		output.close();
	}
	catch (std::exception& e) {
		BE_ISSUE("exception while writing texture", e); UNUSED(e);
		return false;
	}
	itTex->second.isAvailableOpt_ = writeOK;
	itTex->second.sourceFormatOpt_ = *textureData.format;
	if (writeOK)
	{
		outTexturePath = outputPath;
	}
	return writeOK;
}

std::filesystem::path const& GltfMaterialHelper::GetTextureLocalPath(TextureKey const& texKey, RWLockBase const&) const
{
	auto itTex = textureDataMap_.find(texKey);
	if (itTex != textureDataMap_.end()
		&& itTex->second.IsAvailable())
	{
		return itTex->second.path_;
	}
	static const std::filesystem::path emptyPath;
	return emptyPath;
}

std::filesystem::path const& GltfMaterialHelper::GetTextureLocalPath(TextureKey const& texKey) const
{
	RLock lock(mutex_);
	return GetTextureLocalPath(texKey, lock);
}


GltfMaterialHelper::TextureAccess GltfMaterialHelper::GetTextureAccess(std::string const& textureID,
	AdvViz::SDK::ETextureSource texSource,
	RWLockBase const&,
	bool* outNeedTranslucency /*= nullptr*/) const
{
	const TextureKey texKey = { textureID, texSource };
	auto itTex = textureDataMap_.find(texKey);
	if (itTex != textureDataMap_.end()
		&& itTex->second.IsAvailable())
	{
		if (outNeedTranslucency)
		{
			*outNeedTranslucency = itTex->second.needTranslucencyOpt_.value_or(false);
		}
		return {
			itTex->second.path_,
			itTex->second.GetCesiumImage(),
			texKey
		};
	}
	static const std::filesystem::path emptyPath;
	return { emptyPath, nullptr, texKey };
}

std::string GltfMaterialHelper::FindOrCreateTextureID(std::filesystem::path const& texturePath)
{
	std::error_code ec;
	TextureData newEntry;
	newEntry.SetPath(texturePath);
	newEntry.isAvailableOpt_ = std::filesystem::exists(texturePath, ec);

	std::filesystem::path const canonicalPath = std::filesystem::canonical(texturePath, ec);
	std::string pathAsId = canonicalPath.generic_string();
	if (pathAsId.empty())
	{
		BE_LOGE("ITwinMaterial",
			"Error making path '" << texturePath.generic_string() << "' canonical: " << ec);
		pathAsId = texturePath.generic_string();
	}

	{
		WLock lock(mutex_);
		textureDataMap_.try_emplace(
			TextureKey{ pathAsId, AdvViz::SDK::ETextureSource::LocalDisk },
			newEntry);
	}

	return pathAsId;
}

std::filesystem::path GltfMaterialHelper::FindTextureInCache(std::string const& strTextureID) const
{
	BE_ASSERT(hasValidTextureDir_.value_or(false) == true);
	std::filesystem::path const cachedPath_noExt = textureDir_ / strTextureID;
	for (auto const& ext : { ".jpg", ".png" })
	{
		std::filesystem::path cachedPath = cachedPath_noExt;
		cachedPath += ext;
		std::error_code ec;
		if (std::filesystem::exists(cachedPath, ec))
		{
			return cachedPath;
		}
	}
	return {};
}

void GltfMaterialHelper::ListITwinTexturesToDownload(std::vector<std::string>& missingTextureIds,
													 WLock const& lock)
{
	std::string dirError;
	if (!CheckTextureDir(dirError, lock))
	{
		BE_ISSUE("texture directory error", dirError);
		return;
	}
	missingTextureIds.reserve(textureDataMap_.size());
	for (auto& [texId, texData] : textureDataMap_)
	{
		if (!texData.isAvailableOpt_)
		{
			// Look for the texture in the cache
			texData.SetPath(FindTextureInCache(texId.id));
			texData.isAvailableOpt_ = !texData.path_.empty();
		}

		// Only consider iTwin textures here (those downloaded from iModelRpc interface)
		if (!texData.IsAvailable()
			&& texId.eSource == AdvViz::SDK::ETextureSource::ITwin)
		{
			missingTextureIds.emplace_back(texId.id);
		}
	}
}

void GltfMaterialHelper::AppendITwinTexturesToResolveFromMaterial(
	std::unordered_map<AdvViz::SDK::TextureKey, std::string>& itwinTextures,
	AdvViz::SDK::TextureUsageMap& usageMap,
	uint64_t matID,
	RWLockBase const& lock) const
{
	using EITwinChannelType = AdvViz::SDK::EChannelType;
	using ETextureSource = AdvViz::SDK::ETextureSource;

	// For now, only color textures are retrieved from the Mesh Export Service
	for (auto eChan : { AdvViz::SDK::EChannelType::Color })
	{
		// Texture value.
		auto const texMap = GetChannelMap(matID, eChan, lock);
		if (texMap.HasTexture() && texMap.eSource == ETextureSource::ITwin)
		{
			AdvViz::SDK::TextureKey const key = { texMap.texture, texMap.eSource };
			usageMap[key].AddChannel(eChan);
			auto itTex = textureDataMap_.find(key);
			if (itTex != textureDataMap_.end()
				&& itTex->second.IsAvailable()
				&& !itTex->second.HasCesiumImage())
			{
				itwinTextures.emplace(key, itTex->second.path_.filename().generic_string());
			}
		}
	}
}

void GltfMaterialHelper::ListITwinTexturesToResolve(std::unordered_map<AdvViz::SDK::TextureKey, std::string>& itwinTextures,
													AdvViz::SDK::TextureUsageMap& usageMap,
													RWLockBase const& lock) const
{
	// iTwin textures already present in cache should be resolved before any tuning can occur.
	// We only need to resolve those belonging to materials which have been customized by the user.
	for (auto const& [matID, data] : materialMap_)
	{
		auto const& itwinMat(data.iTwinMaterialDefinition_);
		if (AdvViz::SDK::HasCustomSettings(itwinMat))
		{
			AppendITwinTexturesToResolveFromMaterial(itwinTextures, usageMap, matID, lock);
		}
	}
}



AdvViz::SDK::ITwinUVTransform GltfMaterialHelper::GetUVTransform(uint64_t matID, RWLockBase const&) const
{
	auto itMat = materialMap_.find(matID);
	if (itMat != materialMap_.end())
	{
		auto const& matDefinition = itMat->second.iTwinMaterialDefinition_;
		return matDefinition.uvTransform;
	}
	else
	{
		return ITwinUVTransform::NullTransform();
	}
}

AdvViz::SDK::ITwinUVTransform GltfMaterialHelper::GetUVTransform(uint64_t matID) const
{
	RLock lock(mutex_);
	return GetUVTransform(matID, lock);
}

void GltfMaterialHelper::SetUVTransform(uint64_t matID, ITwinUVTransform const& uvTransform, bool& bValueModified)
{
	UVTransformHelper uvTsfHelper(*this, uvTransform);
	TSetChannelParam(uvTsfHelper, matID, bValueModified);
}

AdvViz::SDK::EMaterialKind GltfMaterialHelper::GetMaterialKind(uint64_t matID, RWLockBase const&) const
{
	auto itMat = materialMap_.find(matID);
	if (itMat != materialMap_.end())
	{
		auto const& matDefinition = itMat->second.iTwinMaterialDefinition_;
		return matDefinition.kind;
	}
	return AdvViz::SDK::EMaterialKind::PBR;
}

AdvViz::SDK::EMaterialKind GltfMaterialHelper::GetMaterialKind(uint64_t matID) const
{
	RLock lock(mutex_);
	return GetMaterialKind(matID, lock);
}

void GltfMaterialHelper::SetMaterialKind(uint64_t matID, AdvViz::SDK::EMaterialKind newKind, bool& bValueModified)
{
	MaterialKindHelper nameHelper(*this, newKind);
	TSetChannelParam(nameHelper, matID, bValueModified);
}

bool GltfMaterialHelper::GetCustomRequirements(uint64_t matID, AdvViz::SDK::EMaterialKind& outKind, bool& bOutRequiresTranslucency) const
{
	RLock lock(mutex_);
	auto itMat = materialMap_.find(matID);
	if (itMat != materialMap_.end())
	{
		auto const& matDefinition = itMat->second.iTwinMaterialDefinition_;
		outKind = matDefinition.kind;

		std::string alphaMode;
		if (GetCurrentAlphaMode(matID, alphaMode, lock))
		{
			bOutRequiresTranslucency = (alphaMode == CesiumGltf::Material::AlphaMode::BLEND);
		}
		return true;
	}
	return false;
}

std::string GltfMaterialHelper::GetMaterialName(uint64_t matID, RWLockBase const&,
	bool bAppendLogInfo /*= false*/) const
{
	auto itMat = materialMap_.find(matID);
	if (itMat != materialMap_.end())
	{
		auto const& matDefinition = itMat->second.iTwinMaterialDefinition_;
		std::string matName = matDefinition.displayName;
		if (bAppendLogInfo)
		{
			matName += fmt::format(" (#{} | {})", matID, itMat->second.nameInIModel_);
		}
		return matName;
	}
	else
	{
		return {};
	}
}

std::string GltfMaterialHelper::GetMaterialName(uint64_t matID, bool bAppendLogInfo /*= false*/) const
{
	RLock lock(mutex_);
	return GetMaterialName(matID, lock, bAppendLogInfo);
}

bool GltfMaterialHelper::SetMaterialName(uint64_t matID, std::string const& newName)
{
	MaterialNameHelper nameHelper(*this, newName);
	bool bValueModified = false;
	TSetChannelParam(nameHelper, matID, bValueModified);
	return bValueModified;
}

void GltfMaterialHelper::SetPersistenceInfo(std::string const& iModelID, MaterialPersistencePtr const& mngr)
{
	BE_ASSERT(!iModelID.empty(), "iModel ID is required to identify material in Decoration Service");
	iModelID_ = iModelID;
	persistenceMngr_ = mngr;
}

bool GltfMaterialHelper::HasPersistenceInfo() const
{
	return persistenceMngr_ && !iModelID_.empty();
}

size_t GltfMaterialHelper::LoadMaterialCustomizations(WLock const& lock, bool resetToDefaultIfNone /*= false*/)
{
	// Ensure the texture directory for current iModel is created if needed.
	std::string dirError;
	if (!CheckTextureDir(dirError, lock))
	{
		BE_LOGE("ITwinMaterial", "Texture directory error: " << dirError);
	}

	if (!persistenceMngr_)
	{
		return 0;
	}
	// Postpone the customization if the loading of this iModel is not finished (asynchronous process...)
	if (!persistenceMngr_->HasLoadedModel(iModelID_))
	{
		return 0;
	}

	size_t nbLoadedMatDefinitions = 0;
	for (auto& [matID, data] : materialMap_)
	{
		if (persistenceMngr_->GetMaterialSettings(iModelID_, matID, data.iTwinMaterialDefinition_))
		{
			nbLoadedMatDefinitions++;
		}
		else if (resetToDefaultIfNone)
		{
			// Called when enforcing the deletion of all material custom definitions, which is more a dev
			// tool for now...
			data.iTwinMaterialDefinition_ = AdvViz::SDK::ITwinMaterial();
		}
	}

	return nbLoadedMatDefinitions;
}

std::string GltfMaterialHelper::GetTextureURL(std::string const& textureId, AdvViz::SDK::ETextureSource texSource) const
{
	if (persistenceMngr_)
	{
		return persistenceMngr_->GetTextureURL(textureId, texSource);
	}
	else
	{
		BE_ISSUE("no persistence manager to retrieve the decoration files URL from");
		return {};
	}
}

GltfMaterialHelper::TextureAccess GltfMaterialHelper::StoreCesiumImage(TextureKey const& textureKey,
	CesiumGltf::Image&& cesiumImage,
	AdvViz::SDK::TextureUsageMap const& textureUsageMap,
	WLock const& lock,
	std::optional<bool> const& needTranslucencyOpt /*= std::nullopt*/,
	std::optional<std::filesystem::path> const& pathOnDisk /*= std::nullopt*/)
{
	auto ret = textureDataMap_.try_emplace(textureKey, TextureData{});
	auto& newEntry = ret.first->second;
	newEntry.cesiumImage_ = std::move(cesiumImage);
	if (needTranslucencyOpt)
	{
		newEntry.needTranslucencyOpt_ = needTranslucencyOpt;
	}
	if (pathOnDisk)
	{
		newEntry.path_ = *pathOnDisk;
	}

	auto const texUsage = AdvViz::SDK::FindTextureUsage(textureUsageMap, textureKey);
	// If the texture is used by color or alpha, we meed to detect the need for blend mode now,
	// because the cesium image can be freed from CPU later (once transferred by Cesium to GPU).
	if (texUsage.HasChannel(AdvViz::SDK::EChannelType::Color)
		|| texUsage.HasChannel(AdvViz::SDK::EChannelType::Alpha))
	{
		TestTranslucencyRequirement(textureKey, texUsage, lock);
	}
	return {
		newEntry.path_,
		newEntry.GetCesiumImage(),
		textureKey
	};
}

} // namespace BeUtils
