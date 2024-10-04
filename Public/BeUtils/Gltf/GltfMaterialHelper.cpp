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

#include <fmt/format.h>
#include <fstream>
#include <iostream>
#include <unordered_set>

namespace BeUtils
{


GltfMaterialHelper::PerMaterialData::PerMaterialData(SDK::Core::ITwinMaterialProperties const& props)
	: iTwinProps_(props)
{

}

GltfMaterialHelper::PerMaterialData::~PerMaterialData()
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

	static const std::unordered_set<std::string> unsupportedTypes =
	{ "AnisotropicDirection", "Finish", "Geometry" };

	// Gather the different texture IDs referenced by this material
	for (auto const& [strMapType, mapData] : props.maps)
	{
		// Ignore unsupported map types
		if (unsupportedTypes.find(strMapType) != unsupportedTypes.end())
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

bool GltfMaterialHelper::StoreMaterialInitialAlphaMode(uint64_t matID, std::string const& alphaMode, Lock const&)
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

bool GltfMaterialHelper::GetMaterialInitialAlphaMode(uint64_t matID, std::string& alphaMode, Lock const&) const
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


/*static*/
double GltfMaterialHelper::GetChannelDefaultIntensity(SDK::Core::EChannelType channel,
	SDK::Core::ITwinMaterialProperties const& itwinProps)
{
	// Currently, the materials exported by the Mesh Export Service are not really PBR, but do initialize
	// some of the gltf (PBR) material properties with non-zero constants.
	// For now, unless iTwin material become themselves PBR, use those constants here to match
	// the default settings used when cesium tiles are loaded (so that the percentage displayed in the UI
	// matches the actual PBR value of the Cesium material.
	//
	// The corresponding code can be found in tileset-publisher, GltfModelMaker::AddMaterial.

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

double GltfMaterialHelper::GetChannelIntensity(uint64_t matID, SDK::Core::EChannelType channel) const
{
	Lock lock(mutex_);
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

void GltfMaterialHelper::SetChannelIntensity(uint64_t matID, SDK::Core::EChannelType channel,
	double intensity, bool& bValueModified)
{
	bValueModified = false;

	Lock lock(mutex_);
	auto itMat = materialMap_.find(matID);
	if (itMat != materialMap_.end())
	{
		auto& matDefinition = itMat->second.iTwinMaterialDefinition_;
		std::optional<double> curIntensityOpt = matDefinition.GetChannelIntensityOpt(channel);
		if (!curIntensityOpt)
		{
			bValueModified = std::fabs(GetChannelDefaultIntensity(channel, itMat->second.iTwinProps_) - intensity) > 1e-5;
		}
		else
		{
			bValueModified = std::fabs(*curIntensityOpt - intensity) > 1e-5;
		}
		matDefinition.SetChannelIntensity(channel, intensity);

		if (bValueModified && persistenceMngr_)
		{
			// Notify the persistence manager for future DB update.
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

std::filesystem::path const& GltfMaterialHelper::GetITwinTextureLocalPath(std::string const& strTextureID) const
{
	Lock lock(mutex_);
	auto itTex = textureDataMap_.find(strTextureID);
	if (itTex != textureDataMap_.end()
		&& itTex->second.IsAvailable())
	{
		return itTex->second.path_;
	}
	static const std::filesystem::path emptyPath;
	return emptyPath;
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

} // namespace BeUtils
