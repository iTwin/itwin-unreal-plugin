/*--------------------------------------------------------------------------------------+
|
|     $Source: GltfMaterialHelper.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once


#include <SDK/Core/ITwinAPI/ITwinMaterial.h>
#include <SDK/Core/ITwinAPI/ITwinTypes.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace CesiumGltf
{
	struct Material;
}

namespace SDK::Core
{
	class MaterialPersistenceManager;
}

namespace BeUtils
{

//! Helper to manage the customization of GLTF materials based on the original iTwin materials.
class GltfMaterialHelper
{
public:
	GltfMaterialHelper();

	using Lock = std::scoped_lock<std::mutex>;

	std::mutex& GetMutex() { return mutex_; }


	//===================================================================================
	// Materials
	//===================================================================================

	using MaterialInfo = std::pair<SDK::Core::ITwinMaterialProperties const*, SDK::Core::ITwinMaterial const*>;

	GltfMaterialHelper::MaterialInfo CreateITwinMaterialSlot(uint64_t matID, Lock const&);

	//! Store the iTwin material properties for the given ID.
	void SetITwinMaterialProperties(uint64_t matID, SDK::Core::ITwinMaterialProperties const& props);
	void SetITwinMaterialProperties(uint64_t matID, SDK::Core::ITwinMaterialProperties const& props, Lock const&);

	MaterialInfo GetITwinMaterialInfo(uint64_t matID, Lock const&) const;

	//! Returns whether the given material should use a custom definition.
	bool HasCustomDefinition(uint64_t matID, Lock const&) const;

	bool StoreInitialAlphaMode(uint64_t matID, std::string const& alphaMode, Lock const&);
	bool GetInitialAlphaMode(uint64_t matID, std::string& alphaMode, Lock const&) const;

	bool SetCurrentAlphaMode(uint64_t matID, std::string const& alphaMode, Lock const&);
	bool GetCurrentAlphaMode(uint64_t matID, std::string& alphaMode, Lock const&) const;

	static double GetChannelDefaultIntensity(SDK::Core::EChannelType channel,
		SDK::Core::ITwinMaterialProperties const& itwinProps);
	double GetChannelIntensity(uint64_t matID, SDK::Core::EChannelType channel, Lock const&) const;
	double GetChannelIntensity(uint64_t matID, SDK::Core::EChannelType channel) const;
	void SetChannelIntensity(uint64_t matID, SDK::Core::EChannelType channel, double intensity, bool& bValueModified);

	using ITwinColor = SDK::Core::ITwinColor;
	static ITwinColor GetChannelDefaultColor(SDK::Core::EChannelType channel,
		SDK::Core::ITwinMaterialProperties const& itwinProps);
	ITwinColor GetChannelColor(uint64_t matID, SDK::Core::EChannelType channel, Lock const&) const;
	ITwinColor GetChannelColor(uint64_t matID, SDK::Core::EChannelType channel) const;
	void SetChannelColor(uint64_t matID, SDK::Core::EChannelType channel, ITwinColor const& color, bool& bValueModified);

	using ITwinChannelMap = SDK::Core::ITwinChannelMap;
	static ITwinChannelMap GetChannelDefaultIntensityMap(SDK::Core::EChannelType channel,
		SDK::Core::ITwinMaterialProperties const& itwinProps);
	ITwinChannelMap GetChannelIntensityMap(uint64_t matID, SDK::Core::EChannelType channel, Lock const&) const;
	ITwinChannelMap GetChannelIntensityMap(uint64_t matID, SDK::Core::EChannelType channel) const;
	void SetChannelIntensityMap(uint64_t matID, SDK::Core::EChannelType channel, ITwinChannelMap const& intensityMap, bool& bValueModified);

	static ITwinChannelMap GetChannelDefaultColorMap(SDK::Core::EChannelType channel,
		SDK::Core::ITwinMaterialProperties const& itwinProps);
	ITwinChannelMap GetChannelColorMap(uint64_t matID, SDK::Core::EChannelType channel, Lock const&) const;
	ITwinChannelMap GetChannelColorMap(uint64_t matID, SDK::Core::EChannelType channel) const;
	void SetChannelColorMap(uint64_t matID, SDK::Core::EChannelType channel, ITwinChannelMap const& colorMap, bool& bValueModified);

	bool HasChannelMap(uint64_t matID, SDK::Core::EChannelType channel, Lock const&) const;
	bool HasChannelMap(uint64_t matID, SDK::Core::EChannelType channel) const;

	//===================================================================================
	// Textures
	//===================================================================================

	void SetTextureDirectory(std::filesystem::path const& textureDir, Lock const&);
	std::filesystem::path const& GetTextureDirectory(Lock const&) const { return textureDir_; }

	void ListITwinTexturesToDownload(std::vector<std::string>& missingTextureIds, Lock const&);

	//! Called when a texture is retrieved from iTwin services.
	bool SetITwinTextureData(std::string const& strTextureID, SDK::Core::ITwinTextureData const& textureData);

	//! Return the path to the file downloaded (or cached) for the given iTwin texture ID.
	std::filesystem::path const& GetITwinTextureLocalPath(std::string const& strTextureID, Lock const&) const;
	std::filesystem::path const& GetITwinTextureLocalPath(std::string const& strTextureID) const;

	//! Creates a new texture from the given path, if it has not yet been registered ; else return the
	//! existing texture ID.
	std::string FindOrCreateTextureID(std::filesystem::path const& texturePath);

	//! Enforce downloading all textures, by flushing the cache.
	void FlushTextureDirectory();


	//===================================================================================
	// Persistence
	//===================================================================================

	using MaterialPersistencePtr = std::shared_ptr<SDK::Core::MaterialPersistenceManager>;
	void SetPersistenceInfo(std::string const& iModelID, MaterialPersistencePtr const& mngr);
	bool HasPersistenceInfo() const;
	size_t LoadMaterialCustomizations(Lock const& lock, bool resetToDefaultIfNone = false);

private:
	//! Check texture directory status if it's not yet done - in such case, try to create the directory.
	bool CheckTextureDir(std::string& strError, Lock const&);

	std::filesystem::path FindTextureInCache(std::string const& strTextureID) const;

	template <typename ParamHelper>
	void TSetChannelParam(ParamHelper const& helper, uint64_t matID, bool& bValueModified);

	struct PerMaterialData
	{
		SDK::Core::ITwinMaterialProperties iTwinProps_;
		SDK::Core::ITwinMaterial iTwinMaterialDefinition_;
		std::optional<bool> needCustomMatOpt_;
		std::string initialAlphaMode_; // used when switching back from translucent
		std::string currentAlphaMode_;

		PerMaterialData() = default;
		PerMaterialData(SDK::Core::ITwinMaterialProperties const& props);
	};
	std::unordered_map<uint64_t, PerMaterialData> materialMap_;

	struct TextureData
	{
		std::filesystem::path path_;
		std::optional<bool> isAvailableOpt_; // whether the texture was downloaded

		bool IsAvailable() const { return isAvailableOpt_.value_or(false); }
	};
	std::unordered_map<std::string, TextureData> textureDataMap_;
	std::filesystem::path textureDir_; // directory where we download textures
	mutable std::optional<bool> hasValidTextureDir_;

	mutable std::mutex mutex_;

	/*** persistence handling ***/
	std::string iModelID_;
	MaterialPersistencePtr persistenceMngr_;
};


//! Helper template to fetch a given property with an expected type
template <typename T>
inline T const* TryGetMaterialAttribute(SDK::Core::AttributeMap const& attributes, std::string const& prop_name)
{
	auto itProp = attributes.find(prop_name);
	if (itProp == attributes.cend())
		return nullptr;
	return std::get_if<T>(&itProp->second);
}

template <typename T>
inline T const* TryGetMaterialProperty(SDK::Core::ITwinMaterialProperties const& props, std::string const& prop_name)
{
	return TryGetMaterialAttribute<T>(props.attributes, prop_name);
}

inline bool GetMaterialBoolProperty(SDK::Core::ITwinMaterialProperties const& props, std::string const& prop_name)
{
	bool const* pBool = TryGetMaterialAttribute<bool>(props.attributes, prop_name);
	return pBool && *pBool;
}

inline std::optional<uint64_t> TryGetId64(SDK::Core::AttributeMap const& attributes, std::string const& id_name)
{
	std::string const* pStrId = TryGetMaterialAttribute<std::string>(attributes, id_name);
	if (pStrId)
	{
		char* pLastUsed;
		uint64_t const itwinId = std::strtoull(pStrId->c_str(), &pLastUsed, 16);
		return itwinId;
	}
	return std::nullopt;
}

} // namespace BeUtils
