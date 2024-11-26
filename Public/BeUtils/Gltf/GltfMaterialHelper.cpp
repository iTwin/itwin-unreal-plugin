/*--------------------------------------------------------------------------------------+
|
|     $Source: GltfMaterialHelper.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#include "GltfMaterialHelper.h"

#include <CesiumGltf/Material.h>

#include <SDK/Core/Tools/Assert.h>
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
			textureDataMap_.try_emplace(*pTextureId, TextureData{});
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
	return (itwinMat
		&& (
			itwinMat->DefinesChannel(SDK::Core::EChannelType::Roughness)
			|| itwinMat->DefinesChannel(SDK::Core::EChannelType::Metallic)
			|| itwinMat->DefinesChannel(SDK::Core::EChannelType::Transparency)
			|| itwinMat->DefinesChannel(SDK::Core::EChannelType::Alpha)
			));
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
	// some of the gltf (PBR) material properties with non-zero constants.
	// For now, unless iTwin material become themselves PBR, use those constants here to match
	// the default settings used when cesium tiles are loaded (so that the percentage displayed in the UI
	// matches the actual PBR value of the Cesium material).
	//
	// The corresponding code can be found in tileset-publisher, GltfModelMaker::AddMaterial.
	//
	// If you add the handling of another channel here, please update #TSetChannelParam to retrieve the
	// correct value when saving to DB (see #matDefToStore).

	if (channel == SDK::Core::EChannelType::Roughness)
	{
		return 1.;
	}

	if (channel == SDK::Core::EChannelType::Alpha
		|| channel == SDK::Core::EChannelType::Transparency)
	{
		// for opacity/alpha, test the 'transmit' setting of the original material
		// see https://www.itwinjs.org/reference/core-backend/elements/rendermaterialelement/rendermaterialelement.params/transmit/
		double const* pTransmitValue = TryGetMaterialProperty<double>(itwinProps, "transmit");
		if (pTransmitValue)
		{
			BE_ASSERT(*pTransmitValue >= 0. && *pTransmitValue <= 1.);
			const double dTransp = std::clamp(*pTransmitValue, 0., 1.);
			return channel == SDK::Core::EChannelType::Transparency
				? dTransp
				: (1. - dTransp);
		}
	}

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

		void SetNewValue(uint64_t /*matID*/, SDK::Core::ITwinMaterial& matDefinition,
			GltfMaterialHelper::Lock const&) const
		{
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
			if (this->channel_ == SDK::Core::EChannelType::Color
				&& !matDefinition.DefinesChannel(this->channel_))
			{
				// Make sure we store the current base color in the custom definition, in order not to alter
				// the material too much!
				matDefinition.SetChannelColor(this->channel_,
					this->gltfMatHelper_.GetChannelColor(matID, this->channel_, lock));
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
			for (auto eChan : { SDK::Core::EChannelType::Roughness, SDK::Core::EChannelType::Transparency })
			{
				matDefToStore.SetChannelIntensity(eChan, GetChannelIntensity(matID, eChan, lock));
			}
			for (auto eChan : { SDK::Core::EChannelType::Color })
			{
				matDefToStore.SetChannelColor(eChan, GetChannelColor(matID, eChan, lock));
				matDefToStore.SetChannelColorMap(eChan, GetChannelColorMap(matID, eChan, lock));
			}
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
	// If you add the handling of another channel here, please update #TSetChannelParam to retrieve the
	// correct color when saving to DB (see #matDefToStore).

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
	// If you add the handling of another channel here, please update #TSetChannelParam to retrieve the
	// correct intensity map when saving to DB (see #matDefToStore).

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
	// If you add the handling of another channel here, please update #TSetChannelParam to retrieve the
	// correct color map when saving to DB (see #matDefToStore).

	if (channel == SDK::Core::EChannelType::Color)
	{
		std::string itwinColorTexId;
		if (FindColorMapTexture(itwinProps, itwinColorTexId))
		{
			return ITwinChannelMap{ itwinColorTexId };
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

bool GltfMaterialHelper::HasChannelMap(uint64_t matID, SDK::Core::EChannelType channel, Lock const& lock) const
{
	// Distinguish color from intensity textures.
	if (channel == SDK::Core::EChannelType::Color
		|| channel == SDK::Core::EChannelType::Normal)
	{
		return !GetChannelColorMap(matID, channel, lock).IsEmpty();
	}
	else
	{
		// For other channels, the map defines an intensity
		return !GetChannelIntensityMap(matID, channel, lock).IsEmpty();
	}
}

bool GltfMaterialHelper::HasChannelMap(uint64_t matID, SDK::Core::EChannelType channel) const
{
	Lock lock(mutex_);
	return HasChannelMap(matID, channel, lock);
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

bool GltfMaterialHelper::SetITwinTextureData(std::string const& strTextureID, SDK::Core::ITwinTextureData const& textureData)
{
	if (textureData.bytes.empty() || textureData.width == 0 || textureData.height == 0)
	{
		BE_ISSUE("empty texture", strTextureID);
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
	auto itTex = textureDataMap_.find(strTextureID);
	if (itTex == textureDataMap_.end())
	{
		BE_ISSUE("unknown texture", strTextureID);
		return false;
	}
	std::filesystem::path& outputPath(itTex->second.path_);
	outputPath = textureDir_ / strTextureID;
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

std::filesystem::path const& GltfMaterialHelper::GetITwinTextureLocalPath(std::string const& strTextureID, Lock const&) const
{
	auto itTex = textureDataMap_.find(strTextureID);
	if (itTex != textureDataMap_.end()
		&& itTex->second.IsAvailable())
	{
		return itTex->second.path_;
	}
	static const std::filesystem::path emptyPath;
	return emptyPath;
}

std::filesystem::path const& GltfMaterialHelper::GetITwinTextureLocalPath(std::string const& strTextureID) const
{
	Lock lock(mutex_);
	return GetITwinTextureLocalPath(strTextureID, lock);
}

std::string GltfMaterialHelper::FindOrCreateTextureID(std::filesystem::path const& texturePath)
{
	std::error_code ec;
	TextureData newEntry;
	newEntry.path_ = texturePath;
	newEntry.isAvailableOpt_ = std::filesystem::exists(texturePath, ec);

	std::filesystem::path const canonicalPath = std::filesystem::canonical(texturePath, ec);
	std::string const pathAsId = canonicalPath.generic_string();

	{
		Lock lock(mutex_);
		textureDataMap_.try_emplace(pathAsId, newEntry);
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
			texData.path_ = FindTextureInCache(texId);
			texData.isAvailableOpt_ = !texData.path_.empty();
		}

		if (!texData.IsAvailable())
		{
			missingTextureIds.push_back(texId);
		}
	}
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

} // namespace BeUtils
