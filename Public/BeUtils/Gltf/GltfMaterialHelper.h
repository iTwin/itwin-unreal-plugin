/*--------------------------------------------------------------------------------------+
|
|     $Source: GltfMaterialHelper.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <SDK/Core/ITwinAPI/ITwinMaterial.h>
#include <SDK/Core/ITwinAPI/ITwinTypes.h>
#include <SDK/Core/Visualization/TextureKey.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

#include <boost/container_hash/hash.hpp>
#include <boost/functional/hash.hpp>

#include <CesiumGltf/Image.h>

namespace CesiumGltf
{
	struct Material;
}

namespace SDK::Core
{
	class MaterialPersistenceManager;
}


namespace std
{
	template <>
	struct hash<SDK::Core::TextureKey>
	{
	public:
		std::size_t operator()(SDK::Core::TextureKey const& key) const
		{
			std::size_t res = std::hash<std::string>()(key.id);
			boost::hash_combine(res, uint64_t(key.eSource));
			return res;
		}
	};
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

	bool MaterialUsingTextures(uint64_t matID, Lock const&) const;

	// Simplified API (hiding color-map vs intensity-map distinction).
	ITwinChannelMap GetChannelMap(uint64_t matID, SDK::Core::EChannelType channel, Lock const&) const;
	ITwinChannelMap GetChannelMap(uint64_t matID, SDK::Core::EChannelType channel) const;
	bool HasChannelMap(uint64_t matID, SDK::Core::EChannelType channel, Lock const&) const;
	bool HasChannelMap(uint64_t matID, SDK::Core::EChannelType channel) const;


	using ITwinUVTransform = SDK::Core::ITwinUVTransform;
	ITwinUVTransform GetUVTransform(uint64_t matID, Lock const&) const;
	ITwinUVTransform GetUVTransform(uint64_t matID) const;
	void SetUVTransform(uint64_t matID, ITwinUVTransform const& uvTransform, bool& bValueModified);


	SDK::Core::EMaterialKind GetMaterialKind(uint64_t matID, Lock const&) const;
	SDK::Core::EMaterialKind GetMaterialKind(uint64_t matID) const;
	void SetMaterialKind(uint64_t matID, SDK::Core::EMaterialKind newKind, bool& bValueModified);


	std::string GetMaterialName(uint64_t matID, Lock const&) const;
	std::string GetMaterialName(uint64_t matID) const;
	bool SetMaterialName(uint64_t matID, std::string const& newName);


	//! Retrieve the full material definition, taking both customizations (if any) and settings deduced from
	//! the iModel properties.
	bool GetMaterialFullDefinition(uint64_t matID, SDK::Core::ITwinMaterial& matDefinition) const;
	//! Replace the full material definition.
	void SetMaterialFullDefinition(uint64_t matID, SDK::Core::ITwinMaterial const& matDefinition);


	//===================================================================================
	// Textures
	//===================================================================================

	void SetTextureDirectory(std::filesystem::path const& textureDir, Lock const&);
	std::filesystem::path const& GetTextureDirectory(Lock const&) const { return textureDir_; }

	void ListITwinTexturesToDownload(std::vector<std::string>& missingTextureIds, Lock const&);

	//! Called when a texture is retrieved from iTwin services.
	bool SetITwinTextureData(std::string const& itwinTextureID, SDK::Core::ITwinTextureData const& textureData);

	std::string GetTextureURL(std::string const& textureId, SDK::Core::ETextureSource texSource) const;


	struct TextureAccess
	{
		std::filesystem::path filePath = {};
		CesiumGltf::Image const* cesiumImage = nullptr;

		bool IsValid() const { return cesiumImage || !filePath.empty(); }
	};

	TextureAccess StoreCesiumImage(SDK::Core::TextureKey const& textureKey,
		CesiumGltf::Image&& cesiumImage, Lock const&);

	TextureAccess GetTextureAccess(std::string const& strTextureID,
		SDK::Core::ETextureSource texSource, Lock const&) const;

	inline TextureAccess GetTextureAccess(SDK::Core::ITwinChannelMap const& texMap, Lock const& lock) const {
		return GetTextureAccess(texMap.texture, texMap.eSource, lock);
	}

	std::filesystem::path const& GetTextureLocalPath(std::string const& strTextureID,
		SDK::Core::ETextureSource texSource, Lock const&) const;
	std::filesystem::path const& GetTextureLocalPath(std::string const& strTextureID,
		SDK::Core::ETextureSource texSource) const;

	inline std::filesystem::path const& GetTextureLocalPath(SDK::Core::ITwinChannelMap const& texMap,
		Lock const& lock) const {
		return GetTextureLocalPath(texMap.texture, texMap.eSource, lock);
	}

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

	void CompleteDefinitionWithDefaultValues(SDK::Core::ITwinMaterial& matDefinition,
		uint64_t matID, Lock const& lock) const;

	struct PerMaterialData
	{
		SDK::Core::ITwinMaterialProperties iTwinProps_;
		SDK::Core::ITwinMaterial iTwinMaterialDefinition_;
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
		std::optional<CesiumGltf::Image> cesiumImage_;

		bool HasCesiumImage() const { return cesiumImage_.has_value(); }
		bool IsAvailable() const { return HasCesiumImage() || isAvailableOpt_.value_or(false); }
	};
	using TextureKey = SDK::Core::TextureKey;
	std::unordered_map<TextureKey, TextureData> textureDataMap_;
	std::filesystem::path textureDir_; // directory where we download textures
	mutable std::optional<bool> hasValidTextureDir_;

	mutable std::mutex mutex_; // TODO_JDE we could implement RW-lock with shared_mutex if needed...

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
