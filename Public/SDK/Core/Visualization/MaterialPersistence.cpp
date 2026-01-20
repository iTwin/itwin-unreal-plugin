/*--------------------------------------------------------------------------------------+
|
|     $Source: MaterialPersistence.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#include "MaterialPersistence.h"

#include "Core/ITwinAPI/ITwinMaterial.h"
#include <Core/Json/Json.h>
#include "Core/Network/HttpGetWithLink.h"
#include "Config.h"

#include <fmt/format.h>

#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace AdvViz::SDK
{
	class MaterialPersistenceManager::Impl
	{
	public:
		bool NeedUpdateDB() const;

		void LoadDataFromServer(std::string const& decorationId, std::set<std::string> const& specificModels = {});
		bool HasLoadedModel(std::string const& iModelId) const;
		void SetLoadedModel(std::string const& iModelId, bool bLoaded);

		void SaveDataOnServer(const std::string& decorationId);

		size_t ListIModelsWithMaterialSettings(std::vector<std::string>& iModelIds) const;
		bool HasMaterialDefinition(std::string const& iModelId, uint64_t materialId) const;
		bool GetMaterialSettings(std::string const& iModelId, uint64_t materialId, ITwinMaterial& material) const;
		void SetMaterialSettings(std::string const& iModelId, uint64_t materialId, ITwinMaterial const& material);

		std::shared_ptr<Http>& GetHttp() { return http_; }
		void SetHttp(const std::shared_ptr<Http>& http) { http_ = http; }

		void SetMaterialLibraryDirectory(std::string const& materialLibraryDirectory);

		void RequestDeleteITwinMaterialsInDB(std::optional<std::string> const& specificIModelID = std::nullopt);

		std::optional<bool> UploadChannelTextureIfNeeded(ITwinChannelMap& texMap,
			std::string const& decorationId);

		bool UploadTexuresIfNeeded(size_t& nUploaded, ITwinMaterial& material,
			const std::string& decorationId);

		void BuildDecorationFilesURL(std::string const& decorationId);

		std::string GetBaseURL(ETextureSource texSource) const;
		std::string GetRelativeURL(TextureKey const& textureKey) const;
		std::string GetTextureURL(TextureKey const& textureKey) const;

		void CopyPathsAndURLsFrom(Impl const& other);

		PerIModelTextureSet const& GetDecorationTexturesByIModel() const { return perIModelTextures_; }
		TextureUsageMap const& GetTextureUsageMap() const { return textureUsageMap_; }
		TextureUsage GetTextureUsage(TextureKey const& textureKey) const;
		void AddTextureUsage(TextureKey const& textureKey, EChannelType channel);

		void SetLocalMaterialDirectory(std::filesystem::path const& materialDirectory);
		size_t LoadMaterialCollection(std::filesystem::path const& materialJsonPath, std::string const& iModelID,
			std::unordered_map<uint64_t, std::string>& matIDToDisplayName);
		void AppendMaterialCollectionNames(std::unordered_map<uint64_t, std::string> const& matIDToDisplayName);

		bool GetMaterialAsKeyValueMap(std::string const& iModelId, uint64_t materialId, KeyValueStringMap& outMap) const;
		bool SetMaterialFromKeyValueMap(std::string const& iModelId, uint64_t materialId, KeyValueStringMap const& inMap);

		bool GetMaterialSettingsFromKeyValueMap(KeyValueStringMap const& inMap,
			ITwinMaterial& outMaterial,
			TextureKeySet& outTextures,
			TextureUsageMap& outTextureUsageMap,
			std::optional<ETextureSource> const& customTexSource = std::nullopt) const;

		std::string ExportAsJson(ITwinMaterial const& material, std::string const& iModelID, uint64_t materialId) const;
		bool ConvertJsonFileToKeyValueMap(std::filesystem::path const& jsonPath,
			std::filesystem::path const& textureDir,
			KeyValueStringMap& outMap) const;

		bool RenameMaterialInJsonFile(std::filesystem::path const& jsonPath,
			std::string const& newMaterialName, std::string& outError) const;

	private:
		// For transfer to/from DB
		struct SJsonMaterialWithId
		{
			std::string id;
			std::optional<std::string> displayName;

			std::optional<std::string> type;
			std::optional<std::string> color;
			std::optional<std::string> albedoMap;
			std::optional<double> albedoMapFactor;
			std::optional<double> roughness;
			std::optional<std::string> roughnessMap;
			std::optional<double> metallic;
			std::optional<std::string> metallicMap;
			std::optional<double> opacity;
			std::optional<std::string> opacityMap;
			std::optional<double> normal;
			std::optional<std::string> normalMap;
			std::optional<double> ao;
			std::optional<std::string> aoMap;
			std::optional<double> specular;
			//std::optional<std::string> specularMap;
			std::optional< std::array<double, 2> > uvScaling;
			std::optional< std::array<double, 2> > uvOffset;
			std::optional<double> uvRotationAngle;
		};
		struct SJsonMaterialWithIdVec
		{
			std::vector<SJsonMaterialWithId> materials;
		};
		struct MaterialInfo
		{
			ITwinMaterial settings;
			bool existsInDB = false; // to distinguish create vs update...
			bool needUpdateDB = false;
			bool needDeleteFromDB = false;
		};
		using IModelMaterialInfo = std::unordered_map<uint64_t, MaterialInfo>;
		using Mutex = std::recursive_mutex;
		using Lock = std::lock_guard<std::recursive_mutex>;

		using IModelMaterialMap = std::unordered_map<std::string, IModelMaterialInfo>;

		void InvalidateDB() { needUpdateDB_ = true; }

		inline void MaterialToJson(ITwinMaterial const& material,
			std::string const& iModelID, uint64_t matID,
			SJsonMaterialWithId& jsonMat) const;

		void ParseJSONMaterials(std::vector<SJsonMaterialWithId> const& rows,
			IModelMaterialMap& dataIO,
			PerIModelTextureSet& perIModelTextures,
			TextureUsageMap& textureUsageMap,
			std::optional<ETextureSource> const& customTexSource = std::nullopt) const;
		bool ConvertJsonToKeyValueMap(SJsonMaterialWithId const& jsonMat, KeyValueStringMap& outMap) const;


	private:
		IModelMaterialMap data_;

		std::unordered_map<std::string, std::string> localToDecoTexId_;

		PerIModelTextureSet perIModelTextures_;
		TextureUsageMap textureUsageMap_;

		mutable Mutex dataMutex_;

		mutable bool needUpdateDB_ = false;

		std::unordered_set<std::string> loadedIModelIds_; // fully loaded model IDs.

		std::shared_ptr<Http> http_;
		std::string decorationBaseURL_;
		std::string decorationFilesRelativeURL_; // depends on loaded decoration ID
		std::string materialLibraryURL_;

		std::filesystem::path materialDirectory_;
		std::set<std::string> iModelsForMaterialCollection_;
		std::unordered_map<uint64_t, std::string> matIDToDisplayName_;
	};


	// see https://developer.bentley.com/apis/imodels-v2/operations/create-imodel/#create-an-imodel-using-a-baseline-file
	// This function can allow to upload the Baseline File to blob storage using the response from step 1.
	bool UploadIModelFile(std::string const& uploadURL, std::filesystem::path const& imodelBaselineFilePath)
	{
		auto const sepPos = uploadURL.find('/', std::string_view("https://").length());
		if (sepPos == std::string::npos)
		{
			BE_ISSUE("invalid upload url", uploadURL);
			return false;
		}

		// If you use the "Try it out" button in the above page for step 1, the upload url may contain some
		// occurrences of \u0026 for & => let's replace them all
		std::string const urlSuffix = rfl::internal::strings::replace_all(
			uploadURL.substr(sepPos + 1), "\\u0026", "&");
		if (urlSuffix.find('&') == std::string::npos)
		{
			BE_ISSUE("invalid upload url: should contain several parameters...", urlSuffix);
			return false;
		}

		std::error_code ec;
		if (!std::filesystem::exists(imodelBaselineFilePath, ec))
		{
			BE_LOGE("ITwinAPI", "Invalid file path '" << imodelBaselineFilePath.generic_string() << "' -> " << ec);
			return false;
		}

		Http::Headers headers;
		headers.emplace_back("x-ms-blob-type", "BlockBlob"); // mandatory header!

		std::shared_ptr<Http> http;
		http.reset(Http::New());
		http->SetBaseUrl(uploadURL.substr(0, sepPos).c_str());
		Http::Response const r = http->PutBinaryFile(urlSuffix, imodelBaselineFilePath.generic_string(), headers);
		return Http::IsSuccessful(r);
	}

	void MaterialPersistenceManager::Impl::ParseJSONMaterials(
		std::vector<SJsonMaterialWithId> const& rows,
		IModelMaterialMap& dataIO,
		PerIModelTextureSet& perIModelTextures,
		TextureUsageMap& textureUsageMap,
		std::optional<ETextureSource> const& customTexSource /*= std::nullopt*/) const
	{

		static const auto stringToMaterialKind = [](std::string const& str)
			-> EMaterialKind
		{
			if (str.empty() || str == "PBR")
				return EMaterialKind::PBR;
			else if (str == "Glass")
				return EMaterialKind::Glass;
			BE_ISSUE("unknown material type:", str);
			return EMaterialKind::PBR;
		};

		static const auto stringToColor = [](std::string const& str)
			-> std::optional<ITwinColor>
		{
			// Expecting a format #RRGGBB
			if (str.length() != 7 || str.at(0) != '#')
			{
				BE_ISSUE("unexpected color format", str);
				return std::nullopt;
			}
			ITwinColor result;
			auto const* nextComponent = str.data() + 1; // skip '#'
			for (int i = 0; i < 3; ++i)
			{
				int nValue;
				auto [ptr, ec] = std::from_chars(nextComponent, nextComponent + 2, nValue, 16 /*Base*/);
				nextComponent += 2;
				if (ec != std::errc())
				{
					BE_ISSUE("error parsing color", str);
					return std::nullopt;
				}
				result[i] = 1. * nValue / 255.;
			}
			result[3] = 1.;
			return result;
		};

		// Store the identifiers of all decoration textures used by those materials in imodelTextures.

		static const auto setChannelMap = [&](ITwinMaterial& mat, EChannelType chan,
			std::optional<std::string> const& inTex,
			TextureKeySet& imodelTextures,
			TextureUsageMap& texUsageMap,
			std::optional<ETextureSource> const& customTexSource)
		{
			if (inTex)
			{
				// Distinguish 'Decoration' textures (which are to be downloaded from the decoration server)
				// from 'Library', ones, which should be loaded from the Advanced Visualization library
				// (packaged with the application).
				const std::string_view MAT_LIBRARY_PREFIX = ITWIN_MAT_LIBRARY_TAG "/";

				std::string texId = inTex.value();
				ETextureSource texSource = customTexSource.value_or(ETextureSource::Decoration);

				if (texId.starts_with(MAT_LIBRARY_PREFIX))
				{
					texSource = ETextureSource::Library;
					// remove the prefix to avoid having to filter it everywhere: its only purpose is to
					// make the distinction upon reading/writing, which can now be done with texSource...
					texId = texId.substr(MAT_LIBRARY_PREFIX.length());
				}
				// We should never encounter textures such as "Metals/0"...
				BE_ASSERT(texId == NONE_TEXTURE || !texId.ends_with(std::string("/") + NONE_TEXTURE));

				ITwinChannelMap const texMap = {
					.texture = texId,
					.eSource = texSource
				};
				if (texMap.HasTexture())
				{
					TextureKey const texKey = { texMap.texture, texSource };
					imodelTextures.insert(texKey);

					texUsageMap[texKey].AddChannel(chan);
				}
				mat.SetChannelMap(chan, texMap);
			}
		};

		for (auto const& row : rows)
		{
			// split the id: <Mat_ID>_<iModelID>
			auto sep_pos = row.id.find('_');
			if (sep_pos == std::string::npos)
			{
				BE_ISSUE("invalid material ID in DB", row.id);
				continue;
			}
			uint64_t const matID = static_cast<uint64_t>(std::stoull(row.id.substr(0, sep_pos)));
			std::string const iModelID = row.id.substr(sep_pos + 1);

			TextureKeySet& imodelTextures = perIModelTextures[iModelID];
			IModelMaterialInfo& materialMap = dataIO[iModelID];
			MaterialInfo& materialInfo = materialMap[matID];
			ITwinMaterial& material(materialInfo.settings);
			if (row.displayName)
			{
				material.displayName = *row.displayName;
			}
			if (row.type)
			{
				material.kind = stringToMaterialKind(*row.type);
			}
			if (row.roughness)
				material.SetChannelIntensity(EChannelType::Roughness, *row.roughness);
			if (row.metallic)
				material.SetChannelIntensity(EChannelType::Metallic, *row.metallic);
			if (row.opacity)
				material.SetChannelIntensity(EChannelType::Opacity, *row.opacity);
			if (row.normal)
				material.SetChannelIntensity(EChannelType::Normal, *row.normal);
			if (row.ao)
				material.SetChannelIntensity(EChannelType::AmbientOcclusion, *row.ao);
			if (row.specular)
				material.SetChannelIntensity(EChannelType::Specular, *row.specular);
			std::optional<ITwinColor> customColor;
			if (row.color)
				customColor = stringToColor(*row.color);
			if (customColor)
				material.SetChannelColor(EChannelType::Color, *customColor);

			setChannelMap(material, EChannelType::Color, row.albedoMap,			imodelTextures, textureUsageMap, customTexSource);
			setChannelMap(material, EChannelType::Normal, row.normalMap,		imodelTextures, textureUsageMap, customTexSource);
			setChannelMap(material, EChannelType::Roughness, row.roughnessMap,	imodelTextures, textureUsageMap, customTexSource);
			setChannelMap(material, EChannelType::Metallic, row.metallicMap,	imodelTextures, textureUsageMap, customTexSource);
			setChannelMap(material, EChannelType::Alpha, row.opacityMap,		imodelTextures, textureUsageMap, customTexSource);
			setChannelMap(material, EChannelType::AmbientOcclusion, row.aoMap,	imodelTextures, textureUsageMap, customTexSource);

			if (row.uvScaling)
				material.uvTransform.scale = *row.uvScaling;
			if (row.uvOffset)
				material.uvTransform.offset = *row.uvOffset;
			if (row.uvRotationAngle)
				material.uvTransform.rotation = *row.uvRotationAngle;

			// Color texture factor was added lately (AdvViz EAP2).
			// If now value was retrieved from DB, and the material defines a color channel, ensure we use
			// the default value.
			if (row.albedoMapFactor)
				material.SetChannelIntensity(EChannelType::Color, *row.albedoMapFactor);
			else if (material.DefinesChannel(EChannelType::Color))
				material.SetChannelIntensity(EChannelType::Color, 1.0);

			materialInfo.needUpdateDB = false;
			materialInfo.existsInDB = true;
		}
	}

	void MaterialPersistenceManager::Impl::LoadDataFromServer(std::string const& decorationId,
		std::set<std::string> const& specificModels /*= {}*/)
	{
		if (decorationId.empty())
		{
			BE_ISSUE("decoration ID missing to load material definitions");
			return;
		}

		if (specificModels.empty())
		{
			Lock lock(dataMutex_);
			data_.clear();
		}

		if (!GetHttp())
		{
			BE_ISSUE("No http support!");
			return;
		}

		// Use a local map for loading, and replace it at the end.
		IModelMaterialMap dataIO;


		auto ret = HttpGetWithLink_ByBatch<SJsonMaterialWithId>(GetHttp(),
			"decorations/" + decorationId + "/materials",
			{} /* extra headers*/,
			[this, &dataIO](std::vector<SJsonMaterialWithId> const& rows) -> expected<void, std::string>
		{
			ParseJSONMaterials(rows, dataIO, perIModelTextures_, textureUsageMap_);
			return {};
		});

		if (!ret)
			{
			BE_LOGW("ITwinDecoration", "Load material definitions failed. " << ret.error());
			}

		for (auto const& [iModelID, materialMap] : dataIO)
		{
			BE_LOGI("ITwinDecoration", "Loaded " << materialMap.size() << " material definition(s) for imodel " << iModelID);
		}

		// Update the URL used to access textures.
		BuildDecorationFilesURL(decorationId);

		// Transfer loaded material definitions to internal map.
		{
			Lock lock(dataMutex_);
			if (!specificModels.empty())
			{
				// Only replace materials for the selected iModel(s).
				for (std::string const& iModelId : specificModels)
				{
					data_.erase(iModelId);
					auto const itLoadedMats = dataIO.find(iModelId);
					if (itLoadedMats != dataIO.end())
					{
						data_[iModelId] = itLoadedMats->second;
					}
				}
			}
			else
			{
				data_.swap(dataIO);
				needUpdateDB_ = false;
			}
		}
	}

	bool MaterialPersistenceManager::Impl::HasLoadedModel(std::string const& iModelId) const
	{
		Lock lock(dataMutex_);
		return loadedIModelIds_.contains(iModelId);
	}

	void MaterialPersistenceManager::Impl::SetLoadedModel(std::string const& iModelId, bool bLoaded)
	{
		Lock lock(dataMutex_);
		if (bLoaded)
			loadedIModelIds_.insert(iModelId);
		else
			loadedIModelIds_.erase(iModelId);
	}

	void MaterialPersistenceManager::Impl::RequestDeleteITwinMaterialsInDB(
		std::optional<std::string> const& specificIModelID /*= std::nullopt*/)
	{
		Lock lock(dataMutex_);
		bool needDeletion = false;
		for (auto& [iModelID, materialMap] : data_)
		{
			if (specificIModelID && (*specificIModelID != iModelID))
				continue; // not this iModel...
			for (auto& [matID, matInfo] : materialMap)
			{
				matInfo.needDeleteFromDB = true;
				if (matInfo.existsInDB)
				{
					matInfo.needUpdateDB = true;
					needDeletion = true;
				}
			}
		}
		if (needDeletion)
		{
			InvalidateDB();
		}
	}

	std::optional<bool> MaterialPersistenceManager::Impl::UploadChannelTextureIfNeeded(ITwinChannelMap& texMap,
		std::string const& decorationId)
	{
		if (texMap.eSource != ETextureSource::LocalDisk)
			return std::nullopt;
		if (!texMap.HasTexture())
			return std::nullopt;

		std::string const filePath = texMap.texture;

		{
			// See if this texture has been uploaded before (typically if a same texture is used in multiple
			// materials).
			Lock lock(dataMutex_);
			auto itDecoId = localToDecoTexId_.find(filePath);
			if (itDecoId != localToDecoTexId_.end())
			{
				texMap.texture = itDecoId->second;
				texMap.eSource = ETextureSource::Decoration;
				return std::nullopt;
			}
		}

		// For the destination filename, we will encode the full path and append the basename, for easier
		// debugging.
		std::error_code ec;
		std::filesystem::path const texPath(texMap.texture);
		size_t const pathHash = std::hash<std::string>()(
			std::filesystem::canonical(texPath, ec).generic_string());
		std::string const basename = fmt::format("{0:#x}_{1}",
			pathHash, texPath.filename().generic_string());

		BE_LOGI("ITwinDecoration", "Uploading texture " << filePath << " as " << basename << "...");

		auto const r = GetHttp()->PostFile(
			"decorations/" + decorationId + "/files",
			"file", filePath, { { "filename", basename } });
		bool const uploaded = (r.first == 200 || r.first == 201);
		if (uploaded)
		{
			// Replace the identifier in the channel.
			texMap.texture = basename;
			texMap.eSource = ETextureSource::Decoration;

			Lock lock(dataMutex_);
			localToDecoTexId_.emplace(filePath, basename);

			BE_LOGI("ITwinDecoration", "Uploaded texture " << filePath);
		}
		else
		{
			BE_LOGE("ITwinDecoration", "Failed upload of texture " << filePath << " - error code: " << r.first);
		}
		return uploaded;
	}

	bool MaterialPersistenceManager::Impl::UploadTexuresIfNeeded(size_t& nUploaded, ITwinMaterial& material,
		const std::string& decorationId)
	{
		for (uint8_t chanIndex(0) ; chanIndex < (uint8_t)EChannelType::ENUM_END; ++chanIndex)
		{
			EChannelType const chan = static_cast<EChannelType>(chanIndex);
			auto const chanMap = material.GetChannelMapOpt(chan);
			if (chanMap && chanMap->HasTexture())
			{
				// We need a mutable ITwinChannelMap here, as we may change the map's ID or source.
				ITwinChannelMap& chanMapRef = material.GetMutableChannelMap(chan);
				auto uploadOpt = UploadChannelTextureIfNeeded(
					chanMapRef, decorationId);
				if (uploadOpt)
				{
					if (*uploadOpt)
						nUploaded++;
					else
						return false;
				}
			}
		}
		return true;
	}

	void MaterialPersistenceManager::Impl::BuildDecorationFilesURL(const std::string& decorationId)
	{
		if (GetHttp() && !decorationId.empty())
		{
			// Be careful with base URL: Uri::resolve behavior has changed in 2.14.1
			std::string const baseUrl = GetHttp()->GetBaseUrl();
			// In our case, this URL does contain sub-folders (corresponding to #urlapiprefix in
			// Core/Visualization/Config.h...), while Uri::resolve is expecting the "root" of it.
			std::string urlapiprefix;
			auto firstSep = baseUrl.find_first_of("/\\", std::string_view("https://").length());
			if (firstSep != std::string::npos)
			{
				decorationBaseURL_ = baseUrl.substr(0, firstSep);
				urlapiprefix = baseUrl.substr(firstSep);
			}
			else
			{
				decorationBaseURL_ = baseUrl;
			}
			decorationFilesRelativeURL_ = urlapiprefix + "/decorations/" + decorationId + "/files/";
		}
	}


	void MaterialPersistenceManager::Impl::SetMaterialLibraryDirectory(std::string const& materialLibraryDirectory)
	{
		BE_ASSERT(!materialLibraryDirectory.empty());

		// We will use local URIs to load those textures (which will work in packaged builds thanks to
		// CesiumAsync::IAssetAccessor, which uses UE's file systm API...
		materialLibraryURL_ = std::string("file:///") +
			rfl::internal::strings::replace_all(materialLibraryDirectory, "\\", "/");
		if (!materialLibraryURL_.ends_with("/"))
			materialLibraryURL_ += "/";
	}

	std::string MaterialPersistenceManager::Impl::GetBaseURL(ETextureSource texSource) const
	{
		if (texSource == ETextureSource::Library)
		{
			// Remark: following merge with Cesium 2.14.1, we should no longer use the file:/// protocol with
			// #resolveExternalData: it no longer works in packaged version...
			BE_ISSUE("file protocol no longer possible in packaged context!");
			// Use a local uri - note that thanks to CesiumAsync::IAssetAccessor, this is directly
			// compatible with packaged mode...
			BE_ASSERT(!materialLibraryURL_.empty());
			return materialLibraryURL_;
		}
		else
		{
			BE_ASSERT(!decorationBaseURL_.empty());
			BE_ASSERT(texSource == ETextureSource::Decoration);
			return decorationBaseURL_;
		}
	}

	std::string MaterialPersistenceManager::Impl::GetRelativeURL(TextureKey const& textureKey) const
	{
		BE_DEBUG_ASSERT(textureKey.id != NONE_TEXTURE);
		std::string relativeURL;
		if (textureKey.eSource == ETextureSource::Decoration)
		{
			BE_ASSERT(!decorationFilesRelativeURL_.empty());
			relativeURL = decorationFilesRelativeURL_ + "?filename=";
		}
		relativeURL += AdvViz::SDK::EncodeForUrl(textureKey.id);
		return relativeURL;
	}

	std::string MaterialPersistenceManager::Impl::GetTextureURL(TextureKey const& textureKey) const
	{
		return GetBaseURL(textureKey.eSource) + GetRelativeURL(textureKey);
	}

	void MaterialPersistenceManager::Impl::CopyPathsAndURLsFrom(Impl const& other)
	{
		decorationBaseURL_ = other.decorationBaseURL_;
		decorationFilesRelativeURL_ = other.decorationFilesRelativeURL_;
		materialLibraryURL_ = other.materialLibraryURL_;
		materialDirectory_ = other.materialDirectory_;
	}


	static const auto getMatTypeName = [](EMaterialKind kind)
		-> std::string
	{
		switch (kind)
		{
		case EMaterialKind::PBR: return "PBR";
		case EMaterialKind::Glass: return "Glass";
		default:
			BE_ISSUE("unhandled case:", static_cast<uint8_t>(kind));
			return "";
		}
	};

	static const auto colorToString = [](ITwinColor const& col)
		-> std::string
	{
		return fmt::format("#{0:02X}{1:02X}{2:02X}",
			static_cast<uint8_t>(std::clamp(255. * col[0], 0., 255.)),
			static_cast<uint8_t>(std::clamp(255. * col[1], 0., 255.)),
			static_cast<uint8_t>(std::clamp(255. * col[2], 0., 255.)));
	};

	// Only consider textures present on the decoration service.
	static const auto getChannelMap = [](ITwinMaterial const& mat, EChannelType chan)
		-> std::optional<std::string>
	{
		auto const mapOpt = mat.GetChannelMapOpt(chan);
		if (mapOpt && (mapOpt->eSource == ETextureSource::Decoration
					|| mapOpt->eSource == ETextureSource::Library))
		{
			std::string textureId(mapOpt->texture);
			// For MaterialLibrary textures, add a distinctive tag if needed.
			if (mapOpt->eSource == ETextureSource::Library
				&& textureId != NONE_TEXTURE
				&& !textureId.starts_with(ITWIN_MAT_LIBRARY_TAG))
			{
				textureId = std::string(ITWIN_MAT_LIBRARY_TAG "/") + mapOpt->texture;
			}
			return textureId;
		}
		else
			return std::nullopt;
	};

	inline void MaterialPersistenceManager::Impl::MaterialToJson(
		ITwinMaterial const& material,
		std::string const& iModelID,
		uint64_t matID,
		SJsonMaterialWithId& jsonMat) const
	{
		jsonMat.id = std::to_string(matID) + "_" + iModelID;
		jsonMat.type = getMatTypeName(material.kind);
		jsonMat.metallic = material.GetChannelIntensityOpt(EChannelType::Metallic);
		jsonMat.roughness = material.GetChannelIntensityOpt(EChannelType::Roughness);
		jsonMat.opacity = material.GetChannelIntensityOpt(EChannelType::Opacity);
		jsonMat.normal = material.GetChannelIntensityOpt(EChannelType::Normal);
		jsonMat.ao = material.GetChannelIntensityOpt(EChannelType::AmbientOcclusion);
		jsonMat.specular = material.GetChannelIntensityOpt(EChannelType::Specular);

		auto const colorOpt = material.GetChannelColorOpt(EChannelType::Color);
		if (colorOpt)
		{
			// Convert to string #RRGGBB
			jsonMat.color = colorToString(*colorOpt);
		}

		jsonMat.albedoMap =		getChannelMap(material, EChannelType::Color);
		jsonMat.normalMap =		getChannelMap(material, EChannelType::Normal);
		jsonMat.roughnessMap =	getChannelMap(material, EChannelType::Roughness);
		jsonMat.metallicMap =	getChannelMap(material, EChannelType::Metallic);
		jsonMat.opacityMap =	getChannelMap(material, EChannelType::Alpha);
		jsonMat.aoMap =			getChannelMap(material, EChannelType::AmbientOcclusion);

		jsonMat.albedoMapFactor = material.GetChannelIntensityOpt(EChannelType::Color);

		if (material.HasUVTransform())
		{
			jsonMat.uvScaling = material.uvTransform.scale;
			jsonMat.uvOffset = material.uvTransform.offset;
			jsonMat.uvRotationAngle = material.uvTransform.rotation;
		}

		jsonMat.displayName.reset();
		if (!material.displayName.empty())
		{
			jsonMat.displayName = material.displayName;
		}
		else if (!matIDToDisplayName_.empty())
		{
			// Fill the display name, to make it easier to edit the materials.json file...
			auto const itName = matIDToDisplayName_.find(matID);
			if (itName != matIDToDisplayName_.end())
				jsonMat.displayName = itName->second;
		}
	}

	void MaterialPersistenceManager::Impl::SaveDataOnServer(const std::string& decorationId)
	{
		struct SJsonMaterialIdVect { std::vector<std::string> ids; };
		SJsonMaterialWithIdVec jInPost; // for creation
		SJsonMaterialWithIdVec jInPut; // for update
		SJsonMaterialIdVect jInDelete; // for deletion

		SJsonMaterialWithIdVec jMaterialCollection;
		bool const saveMaterialCollection = !materialDirectory_.empty();

		// Make a copy of the data at current time.
		IModelMaterialMap dataIO;
		{
			Lock lock(dataMutex_);
			dataIO = data_;
		}

		// First upload textures if needed, and update texture identifiers accordingly.
		size_t totalUploadedTextures(0);
		bool failedUpload = false;
		for (auto& [iModelID, materialMap] : dataIO)
		{
			for (auto& [matID, matInfo] : materialMap)
			{
				if (!matInfo.needUpdateDB)
					continue;
				ITwinMaterial& material(matInfo.settings);
				size_t nUploaded(0);
				bool uploadOk = UploadTexuresIfNeeded(nUploaded, material, decorationId);
				totalUploadedTextures += nUploaded;
				if (!uploadOk)
				{
					failedUpload = true;
					break;
				}
			}
			if (failedUpload)
				break;
		}
		if (totalUploadedTextures > 0)
		{
			BE_LOGI("ITwinDecoration", "Uploaded " << totalUploadedTextures << " textures");
		}
		if (failedUpload)
		{
			BE_LOGE("ITwinDecoration", "Failed upload - abort material saving");
			return;
		}

		// Now that needed uploads have occurred, we can update the URL used to access those textures.
		BuildDecorationFilesURL(decorationId);

		// Sort materials for requests (addition/update)
		SJsonMaterialWithId jsonMat;
		for (auto const& [iModelID, materialMap] : dataIO)
		{
			bool const saveIModelMaterials =
				saveMaterialCollection && iModelsForMaterialCollection_.contains(iModelID);
			for (auto const& [matID, matInfo] : materialMap)
			{
				if (!matInfo.needUpdateDB && !saveIModelMaterials)
					continue;

				MaterialToJson(matInfo.settings, iModelID, matID, jsonMat);

				if (saveIModelMaterials)
				{
					jMaterialCollection.materials.push_back(jsonMat);
					if (!matInfo.needUpdateDB)
						continue;
				}

				//if (!matInfo.needDeleteFromDB
				//	&& !jsonMat.metallic && !jsonMat.roughness && !jsonMat.opacity)
				//{
				//	BE_LOGW("ITwinDecoration", "Skipping material " << matID << " during saving process (empty)");
				//	continue;
				//}

				if (matInfo.needDeleteFromDB)
				{
					if (matInfo.existsInDB)
						jInDelete.ids.push_back(jsonMat.id);
				}
				else if (matInfo.existsInDB)
				{
					jInPut.materials.push_back(jsonMat);
				}
				else
				{
					jInPost.materials.push_back(jsonMat);
				}
			}
		}
		bool hasModifedFlags = false;
		auto const UpdateDBFlagsOnSuccess = [&dataIO, &hasModifedFlags](bool forExistingInDB)
		{
			hasModifedFlags = true;
			for (auto& [iModelID, materialMap] : dataIO)
			{
				for (auto& [matID, matInfo] : materialMap)
				{
					if (matInfo.existsInDB == forExistingInDB)
					{
						matInfo.needUpdateDB = false;
					}
					if (!forExistingInDB)
					{
						// The material was successfully created in DB now
						matInfo.existsInDB = true;
					}
				}
			}
		};

		bool saveOK = true;

		// Delete material definitions if requested
		if (!jInDelete.ids.empty())
		{
			bool deletionOK = false;
			std::string jOutDelete;
			long status = GetHttp()->DeleteJsonJBody(
				jOutDelete,"decorations/"+ decorationId + "/materials", jInDelete);
			if (status == 200 || status == 201)
			{
				BE_LOGI("ITwinDecoration", "Deleted " << jInDelete.ids.size() << " material definition(s). Http status: " << status);
				deletionOK = true;

				// Now remove all deleted entries
				for (auto& [iModelID, materialMap] : dataIO)
				{
					std::erase_if(materialMap, [](const auto& item)
					{
						auto const& [matID, matInfo] = item;
						return matInfo.needDeleteFromDB;
					});
				}
				hasModifedFlags = true; // we did modify more than flags in fact...
			}
			else
			{
				BE_LOGW("ITwinDecoration", "Deleting material definitions failed. Http status: " << status);
			}
			saveOK &= deletionOK;
		}

		// Post (new materials)
		if (!jInPost.materials.empty())
		{
			bool creationOK = false;
			SJsonMaterialWithIdVec jOutPost;
			long status = GetHttp()->PostJsonJBody(
				jOutPost, "decorations/" + decorationId + "/materials", jInPost);

			if (status == 200 || status == 201)
			{
				if (jInPost.materials.size() == jOutPost.materials.size())
				{
					BE_LOGI("ITwinDecoration", "Saved " << jInPost.materials.size() << " new material definition(s). Http status: " << status);
					UpdateDBFlagsOnSuccess(false);
					creationOK = true;
				}
			}
			else
			{
				BE_LOGW("ITwinDecoration", "Saving new material definitions failed. Http status: " << status);
			}
			saveOK &= creationOK;
		}

		// Put (updated materials)
		if (!jInPut.materials.empty())
		{
			bool updateOK = false;
			struct SJsonMaterialOutUpd { int64_t numUpdated = 0; };
			SJsonMaterialOutUpd jOutPut;
			long status = GetHttp()->PutJsonJBody(
				jOutPut, "decorations/" + decorationId + "/materials", jInPut);

			if (status == 200 || status == 201)
			{
				if (jInPut.materials.size() == static_cast<size_t>(jOutPut.numUpdated))
				{
					BE_LOGI("ITwinDecoration", "Updated " << jInPut.materials.size() << " material definition(s). Http status: " << status);
					UpdateDBFlagsOnSuccess(true);
					updateOK = true;
				}
			}
			else
			{
				BE_LOGW("ITwinDecoration", "Updating material definitions failed. Http status: " << status);
			}
			saveOK &= updateOK;
		}

		if (!jMaterialCollection.materials.empty())
		{
			auto const materialsOutputPath = materialDirectory_ / "materials_NEW.json";
			auto& jsonMats(jMaterialCollection);
			// Sort by ID
			std::sort(jsonMats.materials.begin(), jsonMats.materials.end(),
				[](auto const& val1, auto const& val2) { return val1.id < val2.id; });
			std::ofstream(materialsOutputPath) << rfl::json::write(jsonMats, YYJSON_WRITE_PRETTY);
		}

		if (hasModifedFlags)
		{
			// Copy back dataIO to data_
			Lock lock(dataMutex_);
			data_.swap(dataIO);
		}

		if (saveOK)
		{
			needUpdateDB_ = false;
		}
	}

	bool MaterialPersistenceManager::Impl::NeedUpdateDB() const
	{
		return needUpdateDB_;
	}

	size_t MaterialPersistenceManager::Impl::ListIModelsWithMaterialSettings(std::vector<std::string>& iModelIds) const
	{
		iModelIds.clear();
		Lock lock(dataMutex_);
		iModelIds.reserve(data_.size());
		for (auto const& [iModelID, materialMap] : data_)
		{
			iModelIds.push_back(iModelID);
		}
		return iModelIds.size();
	}

	bool MaterialPersistenceManager::Impl::HasMaterialDefinition(std::string const& iModelId, uint64_t materialId) const
	{
		Lock lock(dataMutex_);
		auto itIModel = data_.find(iModelId);
		if (itIModel == data_.end())
			return false;
		return itIModel->second.contains(materialId);
	}

	bool MaterialPersistenceManager::Impl::GetMaterialSettings(std::string const& iModelId, uint64_t materialId, ITwinMaterial& material) const
	{
		Lock lock(dataMutex_);
		auto itIModel = data_.find(iModelId);
		if (itIModel == data_.end())
			return false;
		IModelMaterialInfo const& materialMap = itIModel->second;
		auto itMaterial = materialMap.find(materialId);
		if (itMaterial == materialMap.end())
			return false;
		// We do have some data for this material
		material = itMaterial->second.settings;
		return true;
	}

	void MaterialPersistenceManager::Impl::SetMaterialSettings(std::string const& iModelId, uint64_t materialId, ITwinMaterial const& material)
	{
		Lock lock(dataMutex_);
		IModelMaterialInfo& materialMap = data_[iModelId];
		MaterialInfo& materialInfo = materialMap[materialId];
		ITwinMaterial& storedSettings = materialInfo.settings;
		if (material != storedSettings)
		{
			storedSettings = material;
			materialInfo.needUpdateDB = true;
			InvalidateDB();
		}
	}

	TextureUsage MaterialPersistenceManager::Impl::GetTextureUsage(TextureKey const& textureKey) const
	{
		return FindTextureUsage(textureUsageMap_, textureKey);
	}

	void MaterialPersistenceManager::Impl::AddTextureUsage(TextureKey const& textureKey, EChannelType channel)
	{
		if (!textureKey.id.empty() && textureKey.id != NONE_TEXTURE)
		{
			textureUsageMap_[textureKey].AddChannel(channel);
		}
	}

	void MaterialPersistenceManager::Impl::SetLocalMaterialDirectory(std::filesystem::path const& materialDirectory)
	{
		materialDirectory_ = materialDirectory;

		if (!materialDirectory_.empty())
		{
			std::error_code ec;
			if (!std::filesystem::is_directory(materialDirectory_, ec))
			{
				materialDirectory_.clear();
			}
		}
	}

	size_t MaterialPersistenceManager::Impl::LoadMaterialCollection(
		std::filesystem::path const& materialJsonPath, std::string const& iModelID,
		std::unordered_map<uint64_t, std::string>& outMatIDToDisplayName)
	{
		iModelsForMaterialCollection_.insert(iModelID);

		std::error_code ec;
		if (!std::filesystem::is_regular_file(materialJsonPath, ec))
		{
			return 0;
		}

		size_t loadedMaterials = 0;
		SJsonMaterialWithIdVec jsonMaterials;
		std::ifstream ifs(materialJsonPath);
		std::string parseError;
		if (Json::FromStream(jsonMaterials, ifs, parseError))
		{
			IModelMaterialMap dataIO;

			PerIModelTextureSet perIModelTextures;
			ParseJSONMaterials(jsonMaterials.materials, dataIO, perIModelTextures, textureUsageMap_);

			// Append loaded definitions, enforcing their iModel ID from given one.
			if (!dataIO.empty())
			{
				BE_ASSERT(dataIO.size() == 1);

				// Fill map of display names with new entries.
				IModelMaterialInfo const& loadedMats = dataIO.begin()->second;
				for (auto const& [matId, matInfo] : loadedMats)
				{
					std::string const& displayName(matInfo.settings.displayName);
					if (!displayName.empty())
						matIDToDisplayName_.emplace(matId, displayName);
				}
				loadedMaterials = loadedMats.size();
				outMatIDToDisplayName = matIDToDisplayName_;

				// Append loaded textures to the iModel's slot.
				TextureKeySet& imodelTextures = perIModelTextures_[iModelID];
				for (auto const& [_, texSet] : perIModelTextures)
				{
					imodelTextures.insert(texSet.begin(), texSet.end());
				}

				Lock lock(dataMutex_);
				auto& imodelMats = data_[iModelID];
				for (auto const& [matId, matInfo] : loadedMats)
				{
					imodelMats[matId] = matInfo;
				}
			}
		}
		return loadedMaterials;
	}

	void MaterialPersistenceManager::Impl::AppendMaterialCollectionNames(
		std::unordered_map<uint64_t, std::string> const& matIDToDisplayName)
	{
		matIDToDisplayName_.insert(matIDToDisplayName.begin(), matIDToDisplayName.end());
	}


	struct MaterialPropertiesRflVisitor
	{
		KeyValueStringMap& outKeyValues_;
		mutable std::string currentKey_;
		mutable std::string currentValue_;
		mutable std::stringstream error_;

		MaterialPropertiesRflVisitor(KeyValueStringMap& keyValues)
			: outKeyValues_(keyValues)
		{

		}

		// implement the 6 possible cases of the variant
		// bool, int, double, std::string, Object, Array
		// some are not used at all, since the response only deals with string primitives

		void operator()(const bool& boolValue) const
		{
			const int nValue = boolValue ? 1 : 0;
			currentValue_ += std::to_string(nValue);
		}
		void operator()(const int& nValue) const
		{
			currentValue_ += std::to_string(nValue);
		}
		void operator()(const double& dValue) const
		{
			currentValue_ += std::to_string(dValue);
		}
		void operator()(const std::nullopt_t&) const
		{
			error_ << "unhandled std::nullopt_t" << std::endl;
		}

		void operator()(const std::string& strValue) const
		{
			currentValue_ += '\"';
			currentValue_ += strValue;
			currentValue_ += '\"';
		}

		void operator()(const rfl::Generic::Object& rflObject) const
		{
			for (auto const& data : rflObject)
			{
				currentKey_ = data.first;
				if (!currentKey_.empty())
				{
					currentValue_.clear();
					std::visit(*this, data.second.get());
					outKeyValues_[currentKey_] = currentValue_;
				}
			}
		}

		void operator()(const rfl::Generic::Array& rflArray) const
		{
			if (rflArray.empty())
				return;
			currentValue_ += '[';
			int elemIndex(0);
			for (rfl::Generic const& obj : rflArray)
			{
				if (elemIndex > 0)
					currentValue_ += ',';
				std::visit(*this, obj.get());
				elemIndex++;
			}
			currentValue_ += ']';
		}

		std::string GetError() const { return error_.str(); }
	};

	bool MaterialPersistenceManager::Impl::ConvertJsonToKeyValueMap(SJsonMaterialWithId const& jsonMat,
		KeyValueStringMap& outMap) const
	{
		KeyValueStringMap keyValues;

		// Use rfl::generic to recover the list of parameters as a map key -> value
		std::string const matStr = rfl::json::write(jsonMat, YYJSON_WRITE_PRETTY);
		rfl::Generic matGeneric;
		std::string matError;
		if (Json::FromString(matGeneric, matStr, matError))
		{
			MaterialPropertiesRflVisitor rflVisitor(keyValues);
			try
			{
				std::visit(rflVisitor, matGeneric.get());
			}
			catch (std::exception const& e)
			{
				matError = std::string("exception while parsing material properties: ") + e.what() + "\n";
			}
			matError += rflVisitor.GetError();
		}

		if (matError.empty())
		{
			outMap.swap(keyValues);
		}
		return matError.empty();
	}

	bool MaterialPersistenceManager::Impl::GetMaterialAsKeyValueMap(std::string const& iModelId, uint64_t materialId,
		KeyValueStringMap& outMap) const
	{
		ITwinMaterial matSettings;
		if (!GetMaterialSettings(iModelId, materialId, matSettings))
		{
			// Unknown material.
			return false;
		}
		SJsonMaterialWithId jsonMat;
		MaterialToJson(matSettings, iModelId, materialId, jsonMat);

		return ConvertJsonToKeyValueMap(jsonMat, outMap);
	}

	bool MaterialPersistenceManager::Impl::GetMaterialSettingsFromKeyValueMap(KeyValueStringMap const& inMap,
		ITwinMaterial& outMaterial,
		TextureKeySet& outTextures,
		TextureUsageMap& outTextureUsageMap,
		std::optional<ETextureSource> const& customTexSource /*= std::nullopt*/) const
	{
		// Rebuild json from map.
		std::string jsonStr = "{";
		int paramIndex(0);
		for (auto const& [key, value] : inMap)
		{
			if (paramIndex > 0)
				jsonStr += ',';
			jsonStr += fmt::format("\"{}\":{}", key, value);
			paramIndex++;
		}
		jsonStr += '}';

		SJsonMaterialWithId jsonMat;
		std::string matError;
		if (!Json::FromString(jsonMat, jsonStr, matError))
		{
			BE_ISSUE("unable to recover material settings from ", jsonStr);
			return false;
		}
		IModelMaterialMap dataIO;
		PerIModelTextureSet perIModelTextures;
		ParseJSONMaterials({ jsonMat }, dataIO,	perIModelTextures, outTextureUsageMap, customTexSource);
		if (dataIO.size() != 1)
		{
			BE_ISSUE("ParseJSONMaterials failed");
			return false;
		}
		auto const& loadedMats = dataIO.begin()->second;
		if (loadedMats.size() != 1)
		{
			BE_ISSUE("ParseJSONMaterials returned more than 1 material", loadedMats.size());
			return false;
		}
		auto const& matInfo = loadedMats.begin()->second;
		outMaterial = std::move(matInfo.settings);

		outTextures.clear();
		if (!perIModelTextures.empty())
		{
			outTextures = std::move(perIModelTextures.begin()->second);
		}

		return true;
	}

	bool MaterialPersistenceManager::Impl::SetMaterialFromKeyValueMap(std::string const& iModelId, uint64_t materialId,
		KeyValueStringMap const& inMap)
	{
		ITwinMaterial material;
		TextureKeySet textures;
		TextureUsageMap textureUsageMap;
		if (!GetMaterialSettingsFromKeyValueMap(inMap, material, textures, textureUsageMap))
		{
			return false;
		}
		Lock lock(dataMutex_);
		IModelMaterialInfo& iModelMats = data_[iModelId];
		auto& matEntry = iModelMats[materialId];
		matEntry.settings = material;
		matEntry.needUpdateDB = false;
		//matEntry.existsInDB = true;
		return true;
	}

	std::string MaterialPersistenceManager::Impl::ExportAsJson(ITwinMaterial const& matSettings,
		std::string const& iModelId, uint64_t materialId) const
	{
		SJsonMaterialWithId jsonMat;
		MaterialToJson(matSettings, iModelId, materialId, jsonMat);
		return rfl::json::write(jsonMat, YYJSON_WRITE_PRETTY);
	}

	bool MaterialPersistenceManager::Impl::ConvertJsonFileToKeyValueMap(std::filesystem::path const& jsonPath,
		std::filesystem::path const& textureDir,
		KeyValueStringMap& outMap) const
	{
		SJsonMaterialWithId jsonMat;
		std::ifstream ifs(jsonPath);
		std::string parseError;
		if (!Json::FromStream(jsonMat, ifs, parseError))
		{
			return false;
		}
		if (!ConvertJsonToKeyValueMap(jsonMat, outMap))
		{
			return false;
		}
		// Fix texture relative paths, in case the folder organization has been changed compared to (flat)
		// export time.
		if (!textureDir.empty())
		{
			const std::string quote("\"");

			static auto const trimPath = [](std::string const& source) -> std::string
			{
				std::string s(source);
				s.erase(0, s.find_first_not_of(" \n\r\t\""));
				s.erase(s.find_last_not_of(" \n\r\t\"") + 1);
				return s;
			};

			for (auto& [key, value] : outMap)
			{
				if (key.ends_with("Map")
					&& !value.empty()
					&& value != "\"\""
					&& value != "\"0\""
					&& !value.starts_with("\"<") /* avoid losing symbolic prefix such as <MatLibrary> */
					&& !value.ends_with("/0\""))
				{
					auto baseName = std::filesystem::path(trimPath(value)).filename();
					value = quote + (textureDir / baseName).generic_string() + quote;
				}
			}
		}

		return true;
	}

	bool MaterialPersistenceManager::Impl::RenameMaterialInJsonFile(std::filesystem::path const& jsonPath,
		std::string const& newMaterialName, std::string& outError) const
	{
		std::error_code ec;
		if (!std::filesystem::exists(jsonPath, ec))
		{
			outError = fmt::format("Cannot open file '{}' for reading. {}",
				jsonPath.generic_string(), ec.message());
			return false;
		}
		SJsonMaterialWithId jsonMat;
		{
			std::ifstream ifs(jsonPath);
			if (!Json::FromStream(jsonMat, ifs, outError))
			{
				return false;
			}
		}
		jsonMat.displayName = newMaterialName;

		std::ofstream ofs(jsonPath);
		if (ofs.is_open())
		{
			ofs << rfl::json::write(jsonMat, YYJSON_WRITE_PRETTY);
			return true;
		}
		else
		{
			outError = fmt::format("Cannot open file '{}' for writing.",
				jsonPath.generic_string());
			return false;
		}
	}


	//-----------------------------------------------------------------------------------
	// MaterialPersistenceManager
	//-----------------------------------------------------------------------------------

	MaterialPersistenceManager::MaterialPersistenceManager()
		: impl_(new Impl())
	{
		SetHttp(GetDefaultHttp());
	}

	MaterialPersistenceManager::~MaterialPersistenceManager()
	{

	}

	void MaterialPersistenceManager::CopyPathsAndURLsFrom(MaterialPersistenceManager const& other)
	{
		GetImpl().CopyPathsAndURLsFrom(other.GetImpl());
	}

	void MaterialPersistenceManager::SetHttp(std::shared_ptr<Http> http)
	{
		GetImpl().SetHttp(http);
	}

	void MaterialPersistenceManager::SetMaterialLibraryDirectory(std::string const& materialLibraryDirectory)
	{
		GetImpl().SetMaterialLibraryDirectory(materialLibraryDirectory);
	}

	std::string MaterialPersistenceManager::GetBaseURL(ETextureSource texSource) const
	{
		return GetImpl().GetBaseURL(texSource);
	}

	std::string MaterialPersistenceManager::GetRelativeURL(TextureKey const& textureKey) const
	{
		return GetImpl().GetRelativeURL(textureKey);
	}

	std::string MaterialPersistenceManager::GetTextureURL(std::string const& textureId, ETextureSource texSource) const
	{
		return GetImpl().GetTextureURL(TextureKey{ .id= textureId, .eSource = texSource});
	}

	std::string MaterialPersistenceManager::GetTextureURL(TextureKey const& textureKey) const
	{
		return GetImpl().GetTextureURL(textureKey);
	}

	MaterialPersistenceManager::Impl& MaterialPersistenceManager::GetImpl()
	{
		return *impl_;
	}

	MaterialPersistenceManager::Impl const& MaterialPersistenceManager::GetImpl() const
	{
		return *impl_;
	}

	bool MaterialPersistenceManager::NeedUpdateDB() const
	{
		return GetImpl().NeedUpdateDB();
	}

	bool MaterialPersistenceManager::HasLoadedModel(std::string const& iModelId) const
	{
		return GetImpl().HasLoadedModel(iModelId);
	}

	void MaterialPersistenceManager::SetLoadedModel(std::string const& iModelId, bool bLoaded /*= true*/)
	{
		GetImpl().SetLoadedModel(iModelId, bLoaded);
	}

	void MaterialPersistenceManager::LoadDataFromServer(std::string const& decorationId,
		std::set<std::string> const& specificModels /*= {}*/)
	{
		GetImpl().LoadDataFromServer(decorationId, specificModels);
	}

	void MaterialPersistenceManager::SaveDataOnServer(const std::string& decorationId)
	{
		GetImpl().SaveDataOnServer(decorationId);
	}

	void MaterialPersistenceManager::SetLocalMaterialDirectory(std::filesystem::path const& materialDirectory)
	{
		GetImpl().SetLocalMaterialDirectory(materialDirectory);
	}

	size_t MaterialPersistenceManager::LoadMaterialCollection(std::filesystem::path const& materialJsonPath, std::string const& iModelID,
		std::unordered_map<uint64_t, std::string>& matIDToDisplayName)
	{
		return GetImpl().LoadMaterialCollection(materialJsonPath, iModelID, matIDToDisplayName);
	}

	void MaterialPersistenceManager::AppendMaterialCollectionNames(
		std::unordered_map<uint64_t, std::string> const& matIDToDisplayName)
	{
		GetImpl().AppendMaterialCollectionNames(matIDToDisplayName);
	}

	size_t MaterialPersistenceManager::ListIModelsWithMaterialSettings(std::vector<std::string>& iModelIds) const
	{
		return GetImpl().ListIModelsWithMaterialSettings(iModelIds);
	}

	PerIModelTextureSet const& MaterialPersistenceManager::GetDecorationTexturesByIModel() const
	{
		return GetImpl().GetDecorationTexturesByIModel();
	}

	TextureUsageMap const& MaterialPersistenceManager::GetTextureUsageMap() const
	{
		return GetImpl().GetTextureUsageMap();
	}

	TextureUsage MaterialPersistenceManager::GetTextureUsage(TextureKey const& textureKey) const
	{
		return GetImpl().GetTextureUsage(textureKey);
	}

	void MaterialPersistenceManager::AddTextureUsage(TextureKey const& textureKey, EChannelType channel)
	{
		GetImpl().AddTextureUsage(textureKey, channel);
	}

	bool MaterialPersistenceManager::HasMaterialDefinition(std::string const& iModelId, uint64_t materialId) const
	{
		return GetImpl().HasMaterialDefinition(iModelId, materialId);
	}

	bool MaterialPersistenceManager::GetMaterialSettings(std::string const& iModelId, uint64_t materialId, ITwinMaterial& material) const
	{
		return GetImpl().GetMaterialSettings(iModelId, materialId, material);
	}

	void MaterialPersistenceManager::SetMaterialSettings(std::string const& iModelId, uint64_t materialId, ITwinMaterial const& material)
	{
		GetImpl().SetMaterialSettings(iModelId, materialId, material);
	}

	void MaterialPersistenceManager::RequestDeleteITwinMaterialsInDB(
		std::optional<std::string> const& specificIModelID /*= std::nullopt*/)
	{
		GetImpl().RequestDeleteITwinMaterialsInDB(specificIModelID);
	}

	void MaterialPersistenceManager::RequestDeleteIModelMaterialsInDB(std::string const& iModelID)
	{
		RequestDeleteITwinMaterialsInDB(iModelID);
	}

	bool MaterialPersistenceManager::GetMaterialAsKeyValueMap(std::string const& iModelId, uint64_t materialId,
		KeyValueStringMap& outMap) const
	{
		return GetImpl().GetMaterialAsKeyValueMap(iModelId, materialId, outMap);
	}

	bool MaterialPersistenceManager::SetMaterialFromKeyValueMap(std::string const& iModelId, uint64_t materialId, KeyValueStringMap const& inMap)
	{
		return GetImpl().SetMaterialFromKeyValueMap(iModelId, materialId, inMap);
	}

	bool MaterialPersistenceManager::GetMaterialSettingsFromKeyValueMap(KeyValueStringMap const& inMap,
		ITwinMaterial& outMaterial,
		TextureKeySet& outTextures,
		TextureUsageMap& outTextureUsageMap,
		std::optional<ETextureSource> const& customTexSource /*= std::nullopt*/) const
	{
		return GetImpl().GetMaterialSettingsFromKeyValueMap(inMap,
			outMaterial, outTextures, outTextureUsageMap,
			customTexSource);
	}

	std::string MaterialPersistenceManager::ExportAsJson(ITwinMaterial const& material, std::string const& iModelID, uint64_t materialId) const
	{
		return GetImpl().ExportAsJson(material, iModelID, materialId);
	}
	bool MaterialPersistenceManager::ConvertJsonFileToKeyValueMap(std::filesystem::path const& jsonPath,
		std::filesystem::path const& textureDir, KeyValueStringMap& outMap) const
	{
		return GetImpl().ConvertJsonFileToKeyValueMap(jsonPath, textureDir, outMap);
	}

	bool MaterialPersistenceManager::RenameMaterialInJsonFile(std::filesystem::path const& jsonPath,
		std::string const& newMaterialName, std::string& outError) const
	{
		return GetImpl().RenameMaterialInJsonFile(jsonPath, newMaterialName, outError);
	}
}
