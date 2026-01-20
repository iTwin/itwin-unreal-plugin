/*--------------------------------------------------------------------------------------+
|
|     $Source: GltfMaterialHelper.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <BeUtils/Misc/RWLock.h>

#include <SDK/Core/ITwinAPI/ITwinMaterial.h>
#include <SDK/Core/ITwinAPI/ITwinTypes.h>
#include <SDK/Core/Visualization/TextureKey.h>
#include <SDK/Core/Visualization/TextureUsage.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_map>

#include <boost/container_hash/hash.hpp>
#include <boost/functional/hash.hpp>

#include <CesiumGltf/Image.h>

namespace CesiumGltf
{
	struct Material;
}

namespace AdvViz::SDK
{
	class MaterialPersistenceManager;
}


namespace std
{
	template <>
	struct hash<AdvViz::SDK::TextureKey>
	{
	public:
		std::size_t operator()(AdvViz::SDK::TextureKey const& key) const
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

	std::shared_mutex& GetMutex() { return mutex_; }


	//===================================================================================
	// Materials
	//===================================================================================

	using MaterialInfo = std::pair<AdvViz::SDK::ITwinMaterialProperties const*, AdvViz::SDK::ITwinMaterial const*>;

	GltfMaterialHelper::MaterialInfo CreateITwinMaterialSlot(uint64_t matID, std::string const& nameInIModel,
		WLock const&, bool bOnlyIfCustomDefinitionExists = false);

	//! Store the iTwin material properties for the given ID.
	void SetITwinMaterialProperties(uint64_t matID, AdvViz::SDK::ITwinMaterialProperties const& props,
		std::string const& nameInIModel, WLock const&);

	MaterialInfo GetITwinMaterialInfo(uint64_t matID, RWLockBase const&) const;

	//! Returns whether the given material should use a custom definition.
	bool HasCustomDefinition(uint64_t matID, RWLockBase const&) const;

	void StoreInitialAlphaModeIfNeeded(uint64_t matID, std::string& outCurrentAlphaMode, WLock const&);
	bool SetCurrentAlphaMode(uint64_t matID, std::string const& alphaMode, WLock const&);
	bool GetCurrentAlphaMode(uint64_t matID, std::string& alphaMode, RLock const&) const;

	static double GetChannelDefaultIntensity(AdvViz::SDK::EChannelType channel,
		AdvViz::SDK::ITwinMaterialProperties const& itwinProps);
	double GetChannelIntensity(uint64_t matID, AdvViz::SDK::EChannelType channel, RWLockBase const&) const;
	double GetChannelIntensity(uint64_t matID, AdvViz::SDK::EChannelType channel) const;
	void SetChannelIntensity(uint64_t matID, AdvViz::SDK::EChannelType channel, double intensity, bool& bValueModified);


	using ITwinColor = AdvViz::SDK::ITwinColor;
	static ITwinColor GetChannelDefaultColor(AdvViz::SDK::EChannelType channel,
		AdvViz::SDK::ITwinMaterialProperties const& itwinProps);
	ITwinColor GetChannelColor(uint64_t matID, AdvViz::SDK::EChannelType channel, RWLockBase const&) const;
	ITwinColor GetChannelColor(uint64_t matID, AdvViz::SDK::EChannelType channel) const;
	void SetChannelColor(uint64_t matID, AdvViz::SDK::EChannelType channel, ITwinColor const& color, bool& bValueModified);


	using ITwinChannelMap = AdvViz::SDK::ITwinChannelMap;
	static ITwinChannelMap GetChannelDefaultIntensityMap(AdvViz::SDK::EChannelType channel,
		AdvViz::SDK::ITwinMaterialProperties const& itwinProps);
	ITwinChannelMap GetChannelIntensityMap(uint64_t matID, AdvViz::SDK::EChannelType channel, RWLockBase const&) const;
	ITwinChannelMap GetChannelIntensityMap(uint64_t matID, AdvViz::SDK::EChannelType channel) const;
	void SetChannelIntensityMap(uint64_t matID, AdvViz::SDK::EChannelType channel, ITwinChannelMap const& intensityMap, bool& bValueModified);

	static ITwinChannelMap GetChannelDefaultColorMap(AdvViz::SDK::EChannelType channel,
		AdvViz::SDK::ITwinMaterialProperties const& itwinProps);
	ITwinChannelMap GetChannelColorMap(uint64_t matID, AdvViz::SDK::EChannelType channel, RWLockBase const&) const;
	ITwinChannelMap GetChannelColorMap(uint64_t matID, AdvViz::SDK::EChannelType channel) const;
	void SetChannelColorMap(uint64_t matID, AdvViz::SDK::EChannelType channel, ITwinChannelMap const& colorMap, bool& bValueModified);

	bool MaterialUsingTextures(uint64_t matID, RLock const&) const;

	// Simplified API (hiding color-map vs intensity-map distinction).
	ITwinChannelMap GetChannelMap(uint64_t matID, AdvViz::SDK::EChannelType channel, RWLockBase const&) const;
	ITwinChannelMap GetChannelMap(uint64_t matID, AdvViz::SDK::EChannelType channel) const;
	bool HasChannelMap(uint64_t matID, AdvViz::SDK::EChannelType channel, RWLockBase const&) const;
	bool HasChannelMap(uint64_t matID, AdvViz::SDK::EChannelType channel) const;


	using ITwinUVTransform = AdvViz::SDK::ITwinUVTransform;
	ITwinUVTransform GetUVTransform(uint64_t matID, RWLockBase const&) const;
	ITwinUVTransform GetUVTransform(uint64_t matID) const;
	void SetUVTransform(uint64_t matID, ITwinUVTransform const& uvTransform, bool& bValueModified);


	AdvViz::SDK::EMaterialKind GetMaterialKind(uint64_t matID, RWLockBase const&) const;
	AdvViz::SDK::EMaterialKind GetMaterialKind(uint64_t matID) const;
	void SetMaterialKind(uint64_t matID, AdvViz::SDK::EMaterialKind newKind, bool& bValueModified);

	//! Returns whether the given material has a custom definition. In such case, outKind and
	//! bOutRequiresTranslucencya are filled accordingly.
	bool GetCustomRequirements(uint64_t matID, AdvViz::SDK::EMaterialKind& outKind, bool& bOutRequiresTranslucency) const;

	std::string GetMaterialName(uint64_t matID, RWLockBase const&, bool bAppendLogInfo = false) const;
	std::string GetMaterialName(uint64_t matID, bool bAppendLogInfo = false) const;
	bool SetMaterialName(uint64_t matID, std::string const& newName);


	//! Retrieve the full material definition, taking both customizations (if any) and settings deduced from
	//! the iModel properties.
	bool GetMaterialFullDefinition(uint64_t matID, AdvViz::SDK::ITwinMaterial& matDefinition, RWLockBase const&) const;
	bool GetMaterialFullDefinition(uint64_t matID, AdvViz::SDK::ITwinMaterial& matDefinition) const;
	//! Replace the full material definition.
	void SetMaterialFullDefinition(uint64_t matID, AdvViz::SDK::ITwinMaterial const& matDefinition, WLock const&);
	void SetMaterialFullDefinition(uint64_t matID, AdvViz::SDK::ITwinMaterial const& matDefinition);


	//===================================================================================
	// Textures
	//===================================================================================

	void CopyTextureDataFrom(GltfMaterialHelper const& other, WLock const&);

	void SetTextureDirectory(std::filesystem::path const& textureDir, WLock const&, bool bCreateAtOnce = false);
	std::filesystem::path const& GetTextureDirectory(RWLockBase const&) const;

	void ListITwinTexturesToDownload(std::vector<std::string>& missingTextureIds, WLock const&);
	void ListITwinTexturesToResolve(std::unordered_map<AdvViz::SDK::TextureKey, std::string>& itwinTextures,
		AdvViz::SDK::TextureUsageMap& usageMap,
		RWLockBase const&) const;
	void AppendITwinTexturesToResolveFromMaterial(
		std::unordered_map<AdvViz::SDK::TextureKey, std::string>& itwinTextures,
		AdvViz::SDK::TextureUsageMap& usageMap,
		uint64_t matID,
		RWLockBase const&) const;

	//! Called when a texture is retrieved from iTwin services.
	bool SetITwinTextureData(std::string const& itwinTextureID, AdvViz::SDK::ITwinTextureData const& textureData,
		std::filesystem::path& outTexturePath);

	std::string GetTextureURL(std::string const& textureId, AdvViz::SDK::ETextureSource texSource) const;

	using TextureKey = AdvViz::SDK::TextureKey;

	struct TextureAccess
	{
		std::filesystem::path filePath = {};
		CesiumGltf::Image const* cesiumImage = nullptr;
		TextureKey texKey = { {}, AdvViz::SDK::ETextureSource::LocalDisk };

		bool IsValid() const { return cesiumImage || !filePath.empty(); }
		bool HasValidCesiumImage(bool bRequirePixelData) const;
	};

	TextureAccess StoreCesiumImage(TextureKey const& textureKey,
		CesiumGltf::Image&& cesiumImage,
		AdvViz::SDK::TextureUsageMap const& textureUsageMap,
		WLock const&,
		std::optional<bool> const& needTranslucency = std::nullopt,
		std::optional<std::filesystem::path> const& pathOnDisk = std::nullopt);

	TextureAccess GetTextureAccess(std::string const& strTextureID,
		AdvViz::SDK::ETextureSource texSource,
		RWLockBase const&,
		bool* outNeedTranslucency = nullptr) const;
	inline TextureAccess GetTextureAccess(AdvViz::SDK::ITwinChannelMap const& texMap,
		RWLockBase const& lock) const {
		return GetTextureAccess(texMap.texture, texMap.eSource, lock);
	}

	std::filesystem::path const& GetTextureLocalPath(TextureKey const& textureKey, RWLockBase const&) const;
	std::filesystem::path const& GetTextureLocalPath(TextureKey const& textureKey) const;

	inline std::filesystem::path const& GetTextureLocalPath(AdvViz::SDK::ITwinChannelMap const& texMap,
		RLock const& lock) const {
		return GetTextureLocalPath(TextureKey{ texMap.texture, texMap.eSource }, lock);
	}

	void UpdateCurrentAlphaMode(uint64_t matID,
		std::optional<bool> const& bHasTextureRequiringTranslucency,
		WLock const& lock);
	void UpdateCurrentAlphaMode(uint64_t matID,
		std::optional<bool> const& bHasTextureRequiringTranslucency = std::nullopt);

	bool TestTranslucencyRequirement(AdvViz::SDK::TextureKey const& textureKey,
		AdvViz::SDK::TextureUsage const& textureUsage,
		WLock const& lock,
		std::optional<uint64_t> const& matIdForLogs = std::nullopt);

	//! Creates a new texture from the given path, if it has not yet been registered ; else return the
	//! existing texture ID.
	std::string FindOrCreateTextureID(std::filesystem::path const& texturePath);

	//! Enforce downloading all textures, by flushing the cache.
	void FlushTextureDirectory();


	//===================================================================================
	// Persistence
	//===================================================================================

	using MaterialPersistencePtr = std::shared_ptr<AdvViz::SDK::MaterialPersistenceManager>;
	void SetPersistenceInfo(std::string const& iModelID, MaterialPersistencePtr const& mngr);
	bool HasPersistenceInfo() const;
	size_t LoadMaterialCustomizations(WLock const& lock, bool resetToDefaultIfNone = false);

private:
	//! Check texture directory status if it's not yet done - in such case, try to create the directory.
	bool CheckTextureDir(std::string& strError, WLock const&);

	bool TextureRequiringTranslucency(AdvViz::SDK::ITwinChannelMap const& texMap,
		AdvViz::SDK::EChannelType channel,
		uint64_t matId,
		WLock const& lock);
	bool TextureRequiringTranslucencyImpl(AdvViz::SDK::TextureKey const& textureKey,
		AdvViz::SDK::EChannelType channel,
		std::optional<uint64_t> const& matIdForLogs,
		WLock const& lock,
		bool bCacheResult = true);

	std::filesystem::path FindTextureInCache(std::string const& strTextureID) const;

	template <typename ParamHelper>
	void TSetChannelParam(ParamHelper const& helper, uint64_t matID, bool& bValueModified);


	void CompleteDefinitionWithDefaultValues(AdvViz::SDK::ITwinMaterial& matDefinition,
		uint64_t matID, RWLockBase const& lock) const;



	struct PerMaterialData
	{
		AdvViz::SDK::ITwinMaterialProperties iTwinProps_;
		AdvViz::SDK::ITwinMaterial iTwinMaterialDefinition_;
		std::string currentAlphaMode_;
		std::string nameInIModel_; // used mostly for debugging/logging

		PerMaterialData() = default;
		PerMaterialData(AdvViz::SDK::ITwinMaterialProperties const& props);
	};
	std::unordered_map<uint64_t, PerMaterialData> materialMap_;

	struct TextureData
	{
		std::filesystem::path path_;
		std::optional<bool> isAvailableOpt_; // whether the texture was downloaded
		std::optional<CesiumGltf::Image> cesiumImage_;
		std::optional<AdvViz::SDK::ImageSourceFormat> sourceFormatOpt_;
		std::optional<bool> needTranslucencyOpt_; // whether the texture would produce mid-range alpha values (not 0 or 1)

		void SetPath(std::filesystem::path const& inPath);
		bool HasCesiumImage() const { return cesiumImage_.has_value(); }
		bool IsAvailable() const { return HasCesiumImage() || isAvailableOpt_.value_or(false); }

		CesiumGltf::Image const* GetCesiumImage() const {
			return cesiumImage_.has_value() ? &(cesiumImage_.value()) : nullptr;
		}
	};
	std::unordered_map<TextureKey, TextureData> textureDataMap_;
	std::filesystem::path textureDir_; // directory where we download textures
	mutable std::optional<bool> hasValidTextureDir_;

	mutable std::shared_mutex mutex_;

	/*** persistence handling ***/
	std::string iModelID_;
	MaterialPersistencePtr persistenceMngr_;
};


//! Helper template to fetch a given property with an expected type
template <typename T>
inline T const* TryGetMaterialAttribute(AdvViz::SDK::AttributeMap const& attributes, std::string const& prop_name)
{
	auto itProp = attributes.find(prop_name);
	if (itProp == attributes.cend())
		return nullptr;
	return std::get_if<T>(&itProp->second);
}

template <typename T>
inline T const* TryGetMaterialProperty(AdvViz::SDK::ITwinMaterialProperties const& props, std::string const& prop_name)
{
	return TryGetMaterialAttribute<T>(props.attributes, prop_name);
}

inline bool GetMaterialBoolProperty(AdvViz::SDK::ITwinMaterialProperties const& props, std::string const& prop_name)
{
	bool const* pBool = TryGetMaterialAttribute<bool>(props.attributes, prop_name);
	return pBool && *pBool;
}

inline std::optional<uint64_t> TryGetId64(AdvViz::SDK::AttributeMap const& attributes, std::string const& id_name)
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
