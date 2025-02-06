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
#include <unordered_set>

namespace BeUtils
{


GltfMaterialHelper::PerMaterialData::PerMaterialData(SDK::Core::ITwinMaterialProperties const& props)
	: iTwinProps_(props)
{

}

GltfMaterialHelper::GltfMaterialHelper()
{
}

void GltfMaterialHelper::SetITwinMaterialProperties(uint64_t matID, SDK::Core::ITwinMaterialProperties const& props)
{
	Lock lock(mutex_);
	SetITwinMaterialProperties(matID, props, lock);
}

void GltfMaterialHelper::SetITwinMaterialProperties(uint64_t matID, SDK::Core::ITwinMaterialProperties const& props, Lock const&)
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

	static const std::unordered_set<std::string> supportedTypes =
	{ "Pattern" };

	// Gather the different texture IDs referenced by this material
	for (auto const& [strMapType, mapData] : props.maps)
	{
		// Filter supported map types
		if (supportedTypes.find(strMapType) == supportedTypes.end())
			continue;
		std::string const* pTextureId = TryGetMaterialAttribute<std::string>(mapData, "TextureId");
		if (pTextureId)
		{
			// Those textures are identified with a @ITWIN_ prefix, to avoid confusing them with textures
			// selected by user or loaded from the decoration service.
			textureDataMap_.try_emplace(
				TextureKey{ *pTextureId, SDK::Core::ETextureSource::ITwin },
				TextureData{});
		}
	}
}

GltfMaterialHelper::MaterialInfo GltfMaterialHelper::CreateITwinMaterialSlot(uint64_t matID, Lock const&)
{
	auto ret = materialMap_.try_emplace(matID, SDK::Core::ITwinMaterialProperties{});

	// See if this material contains a customization in current decoration, if any.
	if (persistenceMngr_)
	{
		persistenceMngr_->GetMaterialSettings(iModelID_, matID, ret.first->second.iTwinMaterialDefinition_);
	}
	return std::make_pair(&ret.first->second.iTwinProps_, &ret.first->second.iTwinMaterialDefinition_);;
}

GltfMaterialHelper::MaterialInfo GltfMaterialHelper::GetITwinMaterialInfo(uint64_t matID, Lock const&) const
{
	auto itMat = materialMap_.find(matID);
	if (itMat != materialMap_.end())
	{
		return std::make_pair(&itMat->second.iTwinProps_, &itMat->second.iTwinMaterialDefinition_);;
	}
	return { nullptr, nullptr };
}

bool GltfMaterialHelper::HasCustomDefinition(uint64_t matID, Lock const& lock) const
{
	auto const materialInfo = GetITwinMaterialInfo(matID, lock);

	// If the material uses custom settings, activate advanced conversion so that the tuning
	// can handle it.
	auto const* itwinMat = materialInfo.second;
	return itwinMat && SDK::Core::HasCustomSettings(*itwinMat);
}

bool GltfMaterialHelper::StoreInitialAlphaMode(uint64_t matID, std::string const& alphaMode, Lock const&)
{
	// GLTF tuning is incremental, so we should only store the initial alpha mode once!
	auto itMat = materialMap_.find(matID);
	if (itMat != materialMap_.end()
		&& itMat->second.initialAlphaMode_.empty())
	{
		itMat->second.initialAlphaMode_ = alphaMode;
		return true;
	}
	return false;
}

bool GltfMaterialHelper::GetInitialAlphaMode(uint64_t matID, std::string& alphaMode, Lock const&) const
{
	auto itMat = materialMap_.find(matID);
	if (itMat != materialMap_.end()
		&& !itMat->second.initialAlphaMode_.empty())
	{
		alphaMode = itMat->second.initialAlphaMode_;
		return true;
	}
	return false;
}

bool GltfMaterialHelper::SetCurrentAlphaMode(uint64_t matID, std::string const& alphaMode, Lock const&)
{
	auto itMat = materialMap_.find(matID);
	if (itMat != materialMap_.end())
	{
		itMat->second.currentAlphaMode_ = alphaMode;
		return true;
	}
	return false;
}

bool GltfMaterialHelper::GetCurrentAlphaMode(uint64_t matID, std::string& alphaMode, Lock const&) const
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

/*static*/
double GltfMaterialHelper::GetChannelDefaultIntensity(SDK::Core::EChannelType channel,
	SDK::Core::ITwinMaterialProperties const& itwinProps)
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
	case SDK::Core::EChannelType::Metallic:
	{
		// Use same formula as in Mesh Export Service
		// Since https://github.com/iTwin/imodel-native-internal/pull/698
		// (see ConvertMaterialToMetallicRoughness in <imodel-native-internal>/iModelCore/Visualization/TilesetPublisher/tiler/CesiumTileWriter.cpp)
		double const* pSpecularValue = TryGetMaterialProperty<double>(itwinProps, "specular");
		if (pSpecularValue
			&& *pSpecularValue > 0.25
			&& GetChannelDefaultColorMap(SDK::Core::EChannelType::Color, itwinProps).IsEmpty())
		{
			return 1.;
		}
		else
		{
			return 0.;
		}
	}
	case SDK::Core::EChannelType::Roughness:
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

	case SDK::Core::EChannelType::Normal:
	case SDK::Core::EChannelType::AmbientOcclusion:
	{
		return 1.;
	}

	case SDK::Core::EChannelType::Alpha:
	case SDK::Core::EChannelType::Transparency:
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
		return channel == SDK::Core::EChannelType::Transparency
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
	inline bool ApproxEquals(SDK::Core::ITwinColor const& color1, SDK::Core::ITwinColor const& color2)
	{
		return (std::fabs(color1[0] - color2[0]) < 1e-4)
			&& (std::fabs(color1[1] - color2[1]) < 1e-4)
			&& (std::fabs(color1[2] - color2[2]) < 1e-4)
			&& (std::fabs(color1[3] - color2[3]) < 1e-4);
	}
	inline bool ApproxEquals(SDK::Core::ITwinChannelMap const& map1, SDK::Core::ITwinChannelMap const& map2)
	{
		return map1.texture == map2.texture;
	}
	inline bool ApproxEquals(SDK::Core::EMaterialKind const& val1, SDK::Core::EMaterialKind const& val2)
	{
		return val1 == val2;
	}
	inline bool ApproxEquals(SDK::Core::ITwinUVTransform const& tsf1, SDK::Core::ITwinUVTransform const& tsf2)
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
		SDK::Core::EChannelType const channel_;
		ParamType const newValue_;

		ParamHelperBase(BeUtils::GltfMaterialHelper& GltfHelper,
			SDK::Core::EChannelType InChannel,
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
						SDK::Core::EChannelType channel,
						double newIntensity)
			: Super(GltfHelper, channel, newIntensity)
		{}

		ParamType GetCurrentValue(uint64_t matID, GltfMaterialHelper::Lock const& lock) const
		{
			return this->gltfMatHelper_.GetChannelIntensity(matID, this->channel_, lock);
		}

		void SetNewValue(uint64_t /*matID*/, SDK::Core::ITwinMaterial& matDefinition,
			GltfMaterialHelper::Lock const&) const
		{
			matDefinition.SetChannelIntensity(this->channel_, this->newValue_);
		}
	};

	class IntensityMapHelper : public ParamHelperBase<SDK::Core::ITwinChannelMap>
	{
		using Super = ParamHelperBase<SDK::Core::ITwinChannelMap>;

	public:
		using ParamType = SDK::Core::ITwinChannelMap;

		IntensityMapHelper(GltfMaterialHelper& GltfHelper,
			SDK::Core::EChannelType channel,
			SDK::Core::ITwinChannelMap const& newIntensityMap)
			: Super(GltfHelper, channel, newIntensityMap)
		{}

		ParamType GetCurrentValue(uint64_t matID, GltfMaterialHelper::Lock const& lock) const
		{
			return this->gltfMatHelper_.GetChannelIntensityMap(matID, this->channel_, lock);
		}

		void SetNewValue(uint64_t matID, SDK::Core::ITwinMaterial& matDefinition,
			GltfMaterialHelper::Lock const& lock) const
		{
			if (!matDefinition.DefinesChannel(this->channel_))
			{
				// Make sure we bake the default intensity for this channel before adding the texture
				matDefinition.SetChannelIntensity(this->channel_,
					this->gltfMatHelper_.GetChannelIntensity(matID, this->channel_, lock));
			}
			matDefinition.SetChannelIntensityMap(this->channel_, this->newValue_);
		}
	};

	class ColorHelper : public ParamHelperBase<SDK::Core::ITwinColor>
	{
		using Super = ParamHelperBase<SDK::Core::ITwinColor>;

	public:
		using ParamType = SDK::Core::ITwinColor;

		ColorHelper(GltfMaterialHelper& gltfHelper,
			SDK::Core::EChannelType channel,
			SDK::Core::ITwinColor const& newColor)
			: Super(gltfHelper, channel, newColor)
		{}

		ParamType GetCurrentValue(uint64_t matID, GltfMaterialHelper::Lock const& lock) const
		{
			return this->gltfMatHelper_.GetChannelColor(matID, this->channel_, lock);
		}

		void SetNewValue(uint64_t /*matID*/, SDK::Core::ITwinMaterial& matDefinition,
			GltfMaterialHelper::Lock const&) const
		{
			matDefinition.SetChannelColor(this->channel_, this->newValue_);
		}
	};

	class ColorMapHelper : public ParamHelperBase<SDK::Core::ITwinChannelMap>
	{
		using Super = ParamHelperBase<SDK::Core::ITwinChannelMap>;

	public:
		using ParamType = SDK::Core::ITwinChannelMap;

		ColorMapHelper(GltfMaterialHelper& GltfHelper,
			SDK::Core::EChannelType channel,
			SDK::Core::ITwinChannelMap const& newColorMap)
			: Super(GltfHelper, channel, newColorMap)
		{}

		ParamType GetCurrentValue(uint64_t matID, GltfMaterialHelper::Lock const& lock) const
		{
			return this->gltfMatHelper_.GetChannelColorMap(matID, this->channel_, lock);
		}

		void SetNewValue(uint64_t matID, SDK::Core::ITwinMaterial& matDefinition,
			GltfMaterialHelper::Lock const& lock) const
		{
			if (!matDefinition.DefinesChannel(this->channel_))
			{
				// Make sure we store the current base color in the custom definition, in order not to alter
				// the material too much!
				if (this->channel_ == SDK::Core::EChannelType::Color)
				{
					matDefinition.SetChannelColor(this->channel_,
						this->gltfMatHelper_.GetChannelColor(matID, this->channel_, lock));
				}
				else
				{
					// idem for Normal, except we bake an intensity and not a color
					matDefinition.SetChannelIntensity(this->channel_,
						this->gltfMatHelper_.GetChannelIntensity(matID, this->channel_, lock));
				}
			}
			matDefinition.SetChannelColorMap(this->channel_, this->newValue_);
		}
	};

	bool FindColorMapTexture(SDK::Core::ITwinMaterialProperties const& itwinProps,
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

	class UVTransformHelper : public ParamHelperBase<SDK::Core::ITwinUVTransform>
	{
		using Super = ParamHelperBase<SDK::Core::ITwinUVTransform>;

	public:
		using ParamType = SDK::Core::ITwinUVTransform;

		UVTransformHelper(GltfMaterialHelper& GltfHelper,
			SDK::Core::ITwinUVTransform const& newUVTransform)
			: Super(GltfHelper, SDK::Core::EChannelType::ENUM_END, newUVTransform)
		{}

		ParamType GetCurrentValue(uint64_t matID, GltfMaterialHelper::Lock const& lock) const
		{
			return this->gltfMatHelper_.GetUVTransform(matID, lock);
		}

		void SetNewValue(uint64_t /*matID*/, SDK::Core::ITwinMaterial& matDefinition,
			GltfMaterialHelper::Lock const&) const
		{
			matDefinition.uvTransform = this->newValue_;
		}
	};

	class MaterialKindHelper : public ParamHelperBase<SDK::Core::EMaterialKind>
	{
		using Super = ParamHelperBase<SDK::Core::EMaterialKind>;

	public:
		using ParamType = SDK::Core::EMaterialKind;

		MaterialKindHelper(GltfMaterialHelper& GltfHelper,
			SDK::Core::EMaterialKind const& newKind)
			: Super(GltfHelper, SDK::Core::EChannelType::ENUM_END, newKind)
		{}

		ParamType GetCurrentValue(uint64_t matID, GltfMaterialHelper::Lock const& lock) const
		{
			return this->gltfMatHelper_.GetMaterialKind(matID, lock);
		}

		void SetNewValue(uint64_t /*matID*/, SDK::Core::ITwinMaterial& matDefinition,
			GltfMaterialHelper::Lock const&) const
		{
			matDefinition.kind = this->newValue_;
		}
	};

	class MaterialNameHelper : public ParamHelperBase<std::string>
	{
		using Super = ParamHelperBase<std::string>;

	public:
		using ParamType = std::string;

		MaterialNameHelper(GltfMaterialHelper& GltfHelper,
			std::string const& newName)
			: Super(GltfHelper, SDK::Core::EChannelType::ENUM_END, newName)
		{}

		ParamType GetCurrentValue(uint64_t matID, GltfMaterialHelper::Lock const& lock) const
		{
			return this->gltfMatHelper_.GetMaterialName(matID, lock);
		}

		void SetNewValue(uint64_t /*matID*/, SDK::Core::ITwinMaterial& matDefinition,
			GltfMaterialHelper::Lock const&) const
		{
			matDefinition.displayName = this->newValue_;
		}
	};

}

double GltfMaterialHelper::GetChannelIntensity(uint64_t matID, SDK::Core::EChannelType channel, Lock const&) const
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

double GltfMaterialHelper::GetChannelIntensity(uint64_t matID, SDK::Core::EChannelType channel) const
{
	Lock lock(mutex_);
	return GetChannelIntensity(matID, channel, lock);
}

void GltfMaterialHelper::CompleteDefinitionWithDefaultValues(SDK::Core::ITwinMaterial& matDefinition,
	uint64_t matID, Lock const& lock) const
{
	for (auto eChan : {
		SDK::Core::EChannelType::Metallic,
		SDK::Core::EChannelType::Roughness,
		SDK::Core::EChannelType::Opacity })
	{
		matDefinition.SetChannelIntensity(eChan, GetChannelIntensity(matID, eChan, lock));
	}
	for (auto eChan : { SDK::Core::EChannelType::Color })
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

	Lock lock(mutex_);
	auto itMat = materialMap_.find(matID);
	if (itMat != materialMap_.end())
	{
		SDK::Core::ITwinMaterial& matDefinition = itMat->second.iTwinMaterialDefinition_;

		ParameterType const oldValue = helper.GetCurrentValue(matID, lock);
		bValueModified = helper.DoesNewValueDifferFrom(oldValue);
		helper.SetNewValue(matID, matDefinition, lock);

		if (bValueModified && persistenceMngr_)
		{
			// Notify the persistence manager for future DB update.
			// Note that we now pass the *full* definition of the material, because there is no guarantee
			// that default values are the same in our plugin and in the decoration service.
			SDK::Core::ITwinMaterial matDefToStore(matDefinition);
			CompleteDefinitionWithDefaultValues(matDefToStore, matID, lock);
			persistenceMngr_->SetMaterialSettings(iModelID_, matID, matDefToStore);
		}
	}
}


void GltfMaterialHelper::SetChannelIntensity(uint64_t matID, SDK::Core::EChannelType channel,
	double intensity, bool& bValueModified)
{
	IntensityHelper intensitySetter(*this, channel, intensity);
	TSetChannelParam(intensitySetter, matID, bValueModified);
}


/*static*/
GltfMaterialHelper::ITwinColor GltfMaterialHelper::GetChannelDefaultColor(SDK::Core::EChannelType channel,
	SDK::Core::ITwinMaterialProperties const& itwinProps)
{
	// If you add the handling of another channel here, please update #CompleteDefinitionWithDefaultValues to
	// retrieve the correct color when saving to DB.

	if (channel == SDK::Core::EChannelType::Color)
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

GltfMaterialHelper::ITwinColor GltfMaterialHelper::GetChannelColor(uint64_t matID, SDK::Core::EChannelType channel, Lock const&) const
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

GltfMaterialHelper::ITwinColor GltfMaterialHelper::GetChannelColor(uint64_t matID, SDK::Core::EChannelType channel) const
{
	Lock lock(mutex_);
	return GetChannelColor(matID, channel, lock);
}


void GltfMaterialHelper::SetChannelColor(uint64_t matID, SDK::Core::EChannelType channel, ITwinColor const& color, bool& bValueModified)
{
	ColorHelper colorSetter(*this, channel, color);
	TSetChannelParam(colorSetter, matID, bValueModified);
}


/*static*/
SDK::Core::ITwinChannelMap GltfMaterialHelper::GetChannelDefaultIntensityMap(SDK::Core::EChannelType /*channel*/,
	SDK::Core::ITwinMaterialProperties const& /*itwinProps*/)
{
	// If you add the handling of another channel here, please update #CompleteDefinitionWithDefaultValues to
	// retrieve the correct intensity map when saving to DB.
	// Also update #MaterialUsingTextures for the detection of the first texture map.

	// I only found bump maps in Dgn material properties (and we do not expose them...)
	return {};
}

SDK::Core::ITwinChannelMap GltfMaterialHelper::GetChannelIntensityMap(uint64_t matID, SDK::Core::EChannelType channel, Lock const&) const
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

SDK::Core::ITwinChannelMap GltfMaterialHelper::GetChannelIntensityMap(uint64_t matID, SDK::Core::EChannelType channel) const
{
	Lock lock(mutex_);
	return GetChannelIntensityMap(matID, channel, lock);
}

void GltfMaterialHelper::SetChannelIntensityMap(uint64_t matID, SDK::Core::EChannelType channel, ITwinChannelMap const& intensityMap, bool& bValueModified)
{
	IntensityMapHelper mapSetter(*this, channel, intensityMap);
	TSetChannelParam(mapSetter, matID, bValueModified);
}


/*static*/
SDK::Core::ITwinChannelMap GltfMaterialHelper::GetChannelDefaultColorMap(SDK::Core::EChannelType channel,
	SDK::Core::ITwinMaterialProperties const& itwinProps)
{
	// If you add the handling of another channel here, please update #CompleteDefinitionWithDefaultValues to
	// retrieve the correct color map when saving to DB.
	// Also update #MaterialUsingTextures for the detection of the first texture map.

	if (channel == SDK::Core::EChannelType::Color)
	{
		std::string itwinColorTexId;
		if (FindColorMapTexture(itwinProps, itwinColorTexId))
		{
			return ITwinChannelMap{
				.texture = itwinColorTexId,
				.eSource = SDK::Core::ETextureSource::ITwin
			};
		}
	}
	return {};
}

SDK::Core::ITwinChannelMap GltfMaterialHelper::GetChannelColorMap(uint64_t matID, SDK::Core::EChannelType channel, Lock const&) const
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

SDK::Core::ITwinChannelMap GltfMaterialHelper::GetChannelColorMap(uint64_t matID, SDK::Core::EChannelType channel) const
{
	Lock lock(mutex_);
	return GetChannelColorMap(matID, channel, lock);
}

void GltfMaterialHelper::SetChannelColorMap(uint64_t matID, SDK::Core::EChannelType channel, ITwinChannelMap const& colorMap, bool& bValueModified)
{
	ColorMapHelper colorMapSetter(*this, channel, colorMap);
	TSetChannelParam(colorMapSetter, matID, bValueModified);
}

SDK::Core::ITwinChannelMap GltfMaterialHelper::GetChannelMap(uint64_t matID, SDK::Core::EChannelType channel, Lock const& lock) const
{
	// Distinguish color from intensity textures.
	if (channel == SDK::Core::EChannelType::Color
		|| channel == SDK::Core::EChannelType::Normal)
	{
		return GetChannelColorMap(matID, channel, lock);
	}
	else
	{
		// For other channels, the map defines an intensity
		return GetChannelIntensityMap(matID, channel, lock);
	}
}

SDK::Core::ITwinChannelMap GltfMaterialHelper::GetChannelMap(uint64_t matID, SDK::Core::EChannelType channel) const
{
	Lock lock(mutex_);
	return GetChannelMap(matID, channel, lock);
}

bool GltfMaterialHelper::HasChannelMap(uint64_t matID, SDK::Core::EChannelType channel, Lock const& lock) const
{
	return !GetChannelMap(matID, channel, lock).IsEmpty();
}

bool GltfMaterialHelper::HasChannelMap(uint64_t matID, SDK::Core::EChannelType channel) const
{
	Lock lock(mutex_);
	return HasChannelMap(matID, channel, lock);
}

bool GltfMaterialHelper::MaterialUsingTextures(uint64_t matID, Lock const&) const
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
		if (!GetChannelDefaultColorMap(SDK::Core::EChannelType::Color, itMat->second.iTwinProps_).IsEmpty())
			return true;
	}
	return false;
}

bool GltfMaterialHelper::GetMaterialFullDefinition(uint64_t matID, SDK::Core::ITwinMaterial& matDefinition) const
{
	Lock lock(mutex_);
	auto itMat = materialMap_.find(matID);
	if (itMat == materialMap_.end())
	{
		return false;
	}
	matDefinition = itMat->second.iTwinMaterialDefinition_;

	// Use default values for all properties which were not customized:
	CompleteDefinitionWithDefaultValues(matDefinition, matID, lock);

	return true;;
}

void GltfMaterialHelper::SetMaterialFullDefinition(uint64_t matID, SDK::Core::ITwinMaterial const& matDefinition)
{
	Lock lock(mutex_);
	auto itMat = materialMap_.find(matID);
	if (itMat != materialMap_.end())
	{
		itMat->second.iTwinMaterialDefinition_ = matDefinition;
		if (persistenceMngr_)
		{
			persistenceMngr_->SetMaterialSettings(iModelID_, matID, matDefinition);
		}
	}
}

void GltfMaterialHelper::SetTextureDirectory(std::filesystem::path const& textureDir, Lock const&)
{
	textureDir_ = textureDir;
	hasValidTextureDir_.reset(); // Enforce check next time we initiate a download
}

bool GltfMaterialHelper::CheckTextureDir(std::string& strError, Lock const&)
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
	Lock lock(mutex_);
	std::string strErr;
	if (CheckTextureDir(strErr, lock))
	{
		std::error_code ec;
		std::filesystem::remove_all(textureDir_, ec);
		hasValidTextureDir_.reset(); // Enforce recreate texture directory next time we initiate a download
	}
}

bool GltfMaterialHelper::SetITwinTextureData(std::string const& itwinTextureID, SDK::Core::ITwinTextureData const& textureData)
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
	Lock lock(mutex_);

	std::string dirError;
	if (!CheckTextureDir(dirError, lock))
	{
		BE_ISSUE("texture directory error: ", dirError);
		return false;
	}
	TextureKey const textureKey = { itwinTextureID, SDK::Core::ETextureSource::ITwin };
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
	case SDK::Core::ImageSourceFormat::Jpeg: outputPath += ".jpg"; break;
	case SDK::Core::ImageSourceFormat::Png: outputPath += ".png"; break;
	case SDK::Core::ImageSourceFormat::Svg:
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
		BE_ISSUE("exception while writing texture", e);
		return false;
	}
	itTex->second.isAvailableOpt_ = writeOK;
	return writeOK;
}

std::filesystem::path const& GltfMaterialHelper::GetTextureLocalPath(std::string const& textureID,
	SDK::Core::ETextureSource texSource, Lock const&) const
{
	auto itTex = textureDataMap_.find(TextureKey{ textureID, texSource });
	if (itTex != textureDataMap_.end()
		&& itTex->second.IsAvailable())
	{
		return itTex->second.path_;
	}
	static const std::filesystem::path emptyPath;
	return emptyPath;
}

std::filesystem::path const& GltfMaterialHelper::GetTextureLocalPath(std::string const& textureID,
	SDK::Core::ETextureSource texSource) const
{
	Lock lock(mutex_);
	return GetTextureLocalPath(textureID, texSource, lock);
}

GltfMaterialHelper::TextureAccess GltfMaterialHelper::GetTextureAccess(std::string const& textureID,
	SDK::Core::ETextureSource texSource, Lock const&) const
{
	auto itTex = textureDataMap_.find(TextureKey{ textureID, texSource });
	if (itTex != textureDataMap_.end()
		&& itTex->second.IsAvailable())
	{
		return {
			itTex->second.path_,
			itTex->second.cesiumImage_.has_value() ? &(itTex->second.cesiumImage_.value()) : nullptr
		};
	}
	static const std::filesystem::path emptyPath;
	return { emptyPath, nullptr };
}

std::string GltfMaterialHelper::FindOrCreateTextureID(std::filesystem::path const& texturePath)
{
	std::error_code ec;
	TextureData newEntry;
	newEntry.path_ = texturePath;
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
		Lock lock(mutex_);
		textureDataMap_.try_emplace(
			TextureKey{ pathAsId, SDK::Core::ETextureSource::LocalDisk },
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
													 Lock const& lock)
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
			texData.path_ = FindTextureInCache(texId.id);
			texData.isAvailableOpt_ = !texData.path_.empty();
		}

		// Only consider iTwin textures here (those downloaded from iModelRpc interface)
		if (!texData.IsAvailable()
			&& texId.eSource == SDK::Core::ETextureSource::ITwin)
		{
			missingTextureIds.emplace_back(texId.id);
		}
	}
}


SDK::Core::ITwinUVTransform GltfMaterialHelper::GetUVTransform(uint64_t matID, Lock const&) const
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

SDK::Core::ITwinUVTransform GltfMaterialHelper::GetUVTransform(uint64_t matID) const
{
	Lock lock(mutex_);
	return GetUVTransform(matID, lock);
}

void GltfMaterialHelper::SetUVTransform(uint64_t matID, ITwinUVTransform const& uvTransform, bool& bValueModified)
{
	UVTransformHelper uvTsfHelper(*this, uvTransform);
	TSetChannelParam(uvTsfHelper, matID, bValueModified);
}

SDK::Core::EMaterialKind GltfMaterialHelper::GetMaterialKind(uint64_t matID, Lock const&) const
{
	auto itMat = materialMap_.find(matID);
	if (itMat != materialMap_.end())
	{
		auto const& matDefinition = itMat->second.iTwinMaterialDefinition_;
		return matDefinition.kind;
	}
	return SDK::Core::EMaterialKind::PBR;
}

SDK::Core::EMaterialKind GltfMaterialHelper::GetMaterialKind(uint64_t matID) const
{
	Lock lock(mutex_);
	return GetMaterialKind(matID, lock);
}

void GltfMaterialHelper::SetMaterialKind(uint64_t matID, SDK::Core::EMaterialKind newKind, bool& bValueModified)
{
	MaterialKindHelper nameHelper(*this, newKind);
	TSetChannelParam(nameHelper, matID, bValueModified);
}

std::string GltfMaterialHelper::GetMaterialName(uint64_t matID, Lock const&) const
{
	auto itMat = materialMap_.find(matID);
	if (itMat != materialMap_.end())
	{
		auto const& matDefinition = itMat->second.iTwinMaterialDefinition_;
		return matDefinition.displayName;
	}
	else
	{
		return {};
	}
}

std::string GltfMaterialHelper::GetMaterialName(uint64_t matID) const
{
	Lock lock(mutex_);
	return GetMaterialName(matID, lock);
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

size_t GltfMaterialHelper::LoadMaterialCustomizations(Lock const& lock, bool resetToDefaultIfNone /*= false*/)
{
	if (!persistenceMngr_)
		return 0;
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
			data.iTwinMaterialDefinition_ = SDK::Core::ITwinMaterial();
		}
	}
	return nbLoadedMatDefinitions;
}

std::string GltfMaterialHelper::GetTextureURL(std::string const& textureId, SDK::Core::ETextureSource texSource) const
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
	Lock const&)
{
	auto ret = textureDataMap_.try_emplace(textureKey, TextureData{});
	auto& newEntry = ret.first->second;
	newEntry.cesiumImage_ = std::move(cesiumImage);

	return {
		newEntry.path_,
		&(newEntry.cesiumImage_.value())
	};
}

} // namespace BeUtils
