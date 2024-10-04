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

	bool StoreMaterialInitialAlphaMode(uint64_t matID, std::string const& alphaMode, Lock const&);
	bool GetMaterialInitialAlphaMode(uint64_t matID, std::string& alphaMode, Lock const&) const;

	static double GetChannelDefaultIntensity(SDK::Core::EChannelType channel,
		SDK::Core::ITwinMaterialProperties const& itwinProps);
	double GetChannelIntensity(uint64_t matID, SDK::Core::EChannelType channel) const;
	void SetChannelIntensity(uint64_t matID, SDK::Core::EChannelType channel, double intensity, bool& bValueModified);


	//===================================================================================
	// Textures
	//===================================================================================

	void SetTextureDirectory(std::filesystem::path const& textureDir, Lock const&);
	void ListITwinTexturesToDownload(std::vector<std::string>& missingTextureIds, Lock const&);

	//! Called when a texture is retrieved from iTwin services.
	bool SetITwinTextureData(std::string const& strTextureID, SDK::Core::ITwinTextureData const& textureData);

	//! Return the path to the file downloaded (or cached) for the given iTwin texture ID.
	std::filesystem::path const& GetITwinTextureLocalPath(std::string const& strTextureID) const;

	//! Enforce downloading all textures, by flushing the cache.
	void FlushTextureDirectory();


	//===================================================================================
	// Persistence
	//===================================================================================

	using MaterialPersistencePtr = std::shared_ptr<SDK::Core::MaterialPersistenceManager>;
	void SetPersistenceInfo(std::string const& iModelID, MaterialPersistencePtr const& mngr);

private:
	//! Check texture directory status if it's not yet done - in such case, try to create the directory.
	bool CheckTextureDir(std::string& strError, Lock const&);

	std::filesystem::path FindTextureInCache(std::string const& strTextureID) const;

	struct PerMaterialData
	{
		SDK::Core::ITwinMaterialProperties iTwinProps_;
		SDK::Core::ITwinMaterial iTwinMaterialDefinition_;
		std::optional<bool> needCustomMatOpt_;
		std::unique_ptr<CesiumGltf::Material> customGltfMaterial_;
		std::string initialAlphaMode_; // used when switching back from translucent

		PerMaterialData() = default;
		PerMaterialData(SDK::Core::ITwinMaterialProperties const& props);
		~PerMaterialData(); // just to reduce dependencies (CesiumGltf::Material header...)
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
