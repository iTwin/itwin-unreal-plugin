/*--------------------------------------------------------------------------------------+
|
|     $Source: MaterialPersistence.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#include "MaterialPersistence.h"

#include "AsyncHttp.inl"
#include "Core/ITwinAPI/ITwinMaterial.h"
#include <Core/Json/Json.h>
#include "Core/Network/HttpGetWithLink.h"
#include "Config.h"
#include <Core/Visualization/AsyncHelpers.h>
#include <Core/Visualization/SavableItem.h>

#include <fmt/format.h>

#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace AdvViz::SDK
{
	class MaterialPersistenceManager::Impl : public std::enable_shared_from_this<Impl>, public SavableItemWithoutID
	{
	public:
		Impl()
		{
			isThisValid_ = std::make_shared<std::atomic_bool>(true);

			SetHttp(GetDefaultHttp());
		}

		~Impl()
		{
			*isThisValid_ = false;
		}

		std::shared_ptr<Http> const& GetHttp() const { return http_; }
		void SetHttp(std::shared_ptr<Http> const& http) { http_ = http; }

		void LoadDataFromServer(std::string const& decorationId,
			TextureUsageMap& outTextureUsageMap,
			std::set<std::string> const& specificModels = {});
		void AsyncLoadDataFromServer(std::string const& decorationId,
			std::function<void(expected<void, std::string> const&, TextureUsageMap const&)> const& callback,
			std::set<std::string> const& specificModels = {});

		bool HasLoadedModel(std::string const& iModelId) const;
		void SetLoadedModel(std::string const& iModelId, bool bLoaded);

		void AsyncSaveDataOnServer(const std::string& decorationId, std::function<void(bool)>&& onDataSavedFunc = {});

		size_t ListIModelsWithMaterialSettings(std::vector<std::string>& iModelIds) const;
		bool HasMaterialDefinition(std::string const& iModelId, uint64_t materialId) const;
		bool GetMaterialSettings(std::string const& iModelId, uint64_t materialId, ITwinMaterial& material) const;
		void SetMaterialSettings(std::string const& iModelId, uint64_t materialId, ITwinMaterial const& material);

		void SetMaterialLibraryDirectory(std::string const& materialLibraryDirectory);

		void RequestDeleteITwinMaterialsInDB(std::optional<std::string> const& specificIModelID = std::nullopt);

		/// Start the (asynchronous) upload of given texture if needed.
		/// Returns true if an upload request was actually started.
		bool AsyncUploadChannelTextureIfNeeded(
			std::shared_ptr<AsyncRequestGroupCallback> const& callbackPtr,
			ITwinChannelMap& texMap,
			std::string const& decorationId);

		/// Start the (asynchronous) upload of all textures needing it.
		/// Returns the number of upload requests started.
		size_t AsyncUploadTexuresIfNeeded(
			std::shared_ptr<AsyncRequestGroupCallback> const& callbackPtr,
			ITwinMaterial& material,
			std::string const& decorationId);

		void BuildDecorationFilesURL(std::string const& decorationId);

		std::string GetBaseURL(ETextureSource texSource) const;
		std::string GetRelativeURL(TextureKey const& textureKey) const;
		std::string GetTextureURL(TextureKey const& textureKey) const;

		void CopyPathsAndURLsFrom(Impl const& other);

		PerIModelTextureSet const& GetDecorationTexturesByIModel() const { 
			//TODO: check thread safety
			auto thdata = thdata_.GetAutoLock();
			return thdata->perIModelTextures_;
		}

		void SetLocalMaterialDirectory(std::filesystem::path const& materialDirectory);
		size_t LoadMaterialCollection(std::filesystem::path const& materialJsonPath,
			std::string const& iModelID,
			TextureUsageMap& outTextureUsageMap,
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
			KeyValueStringMap& outMap,
			bool bForceUseTexturesFromLocalDir = false) const;

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
		struct MaterialInfo : public SavableItemWithoutID
		{
			ITwinMaterial settings;
			bool existsInDB = false; // to distinguish create vs update...
			bool needDeleteFromDB = false;
		};
		using IModelMaterialInfo = std::unordered_map<uint64_t, MaterialInfo>;
		using IModelMaterialMap = std::unordered_map<std::string, IModelMaterialInfo>;


		inline void MaterialToJson(ITwinMaterial const& material,
			std::string const& iModelID, uint64_t matID,
			SJsonMaterialWithId& jsonMat) const;

		void ParseJSONMaterials(std::vector<SJsonMaterialWithId> const& rows,
			IModelMaterialMap& dataIO,
			PerIModelTextureSet& perIModelTextures,
			TextureUsageMap& textureUsageMap,
			std::optional<ETextureSource> const& customTexSource = std::nullopt) const;
		bool ConvertJsonToKeyValueMap(SJsonMaterialWithId const& jsonMat, KeyValueStringMap& outMap) const;

		void AsyncSaveMaterials(const std::string& decorationId,
			std::shared_ptr<IModelMaterialMap> dataIO,
			std::function<void(bool)>&& onDataSavedFunc = {});
		struct SThreadSafeData
		{
			IModelMaterialMap data_;
			std::unordered_map<std::string, std::string> localToDecoTexId_;
			PerIModelTextureSet perIModelTextures_;
			std::unordered_set<std::string> loadedIModelIds_; // fully loaded model IDs.
			std::set<std::string> iModelsForMaterialCollection_;
			std::unordered_map<uint64_t, std::string> matIDToDisplayName_;
		};

		Tools::RWLockableObject<SThreadSafeData> thdata_;
	private:

		std::shared_ptr<Http> http_;
		std::string decorationBaseURL_;
		std::string decorationFilesRelativeURL_; // depends on loaded decoration ID
		std::string materialLibraryURL_;

		std::filesystem::path materialDirectory_;


		std::shared_ptr< std::atomic_bool > isThisValid_;
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

			materialInfo.SetShouldSave(false);
			materialInfo.existsInDB = true;
		}
	}

	void MaterialPersistenceManager::Impl::LoadDataFromServer(std::string const& decorationId,
		TextureUsageMap& outTextureUsageMap,
		std::set<std::string> const& specificModels /*= {}*/)
	{
		if (decorationId.empty())
		{
			BE_ISSUE("decoration ID missing to load material definitions");
			return;
		}

		if (specificModels.empty())
		{
			auto thdata = thdata_.GetAutoLock();
			thdata->data_.clear();
		}

		if (!GetHttp())
		{
			BE_ISSUE("No http support!");
			return;
		}

		// Use a local map for loading, and replace it at the end.
		IModelMaterialMap dataIO;
		TextureUsageMap textureUsageMap;

		auto ret = HttpGetWithLink_ByBatch<SJsonMaterialWithId>(GetHttp(),
			"decorations/" + decorationId + "/materials",
			{} /* extra headers*/,
			[this, &dataIO, &textureUsageMap](std::vector<SJsonMaterialWithId> const& rows) -> expected<void, std::string>
		{
			auto thdata = thdata_.GetAutoLock();
			ParseJSONMaterials(rows, dataIO, thdata->perIModelTextures_, textureUsageMap);
			return {};
		});

		outTextureUsageMap.swap(textureUsageMap);

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
			auto thdata = thdata_.GetAutoLock();
			auto& data_ = thdata->data_;
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
			}
		}
	}

	void MaterialPersistenceManager::Impl::AsyncLoadDataFromServer(std::string const& decorationId,
		std::function<void(expected<void, std::string> const&, TextureUsageMap const&)> const& onFinishCallback,
		std::set<std::string> const& specificModels /*= {}*/)
	{
		if (decorationId.empty())
		{
			BE_ISSUE("decoration ID missing to load material definitions");
			onFinishCallback(make_unexpected("decoration ID missing to load material definitions"), {});
			return;
		}

		if (specificModels.empty())
		{
			auto thdata = thdata_.GetAutoLock();
			thdata->data_.clear();
		}

		if (!GetHttp())
		{
			BE_ISSUE("No http support!");
			onFinishCallback(make_unexpected("No http support!"), {});
			return;
		}

		// Use a local map for loading, and replace it at the end.
		auto dataIOPtr = MakeSharedLockableData<IModelMaterialMap>();
		std::shared_ptr< MaterialPersistenceManager::Impl> SThis = shared_from_this();
		std::shared_ptr<std::set<std::string>> SSpecificModels = std::make_shared<std::set<std::string>>(specificModels);

		std::shared_ptr<TextureUsageMap> textureUsageMapPtr = std::make_shared<TextureUsageMap>();
		AsyncHttpGetWithLink_ByBatch<SJsonMaterialWithId>(GetHttp(),
			"decorations/" + decorationId + "/materials",
			{} /* extra headers*/,
			[SThis, dataIOPtr, decorationId, textureUsageMapPtr](std::vector<SJsonMaterialWithId> const& rows) -> expected<void, std::string>
			{
				auto thdata = SThis->thdata_.GetAutoLock();
				auto dataIO = dataIOPtr->GetAutoLock();
				SThis->ParseJSONMaterials(rows, dataIO, thdata->perIModelTextures_, *textureUsageMapPtr);
				return {};
			},
			[SThis, dataIOPtr, SSpecificModels, decorationId, onFinishCallback, textureUsageMapPtr](expected<void, std::string> const& exp)
			{
				if (!exp)
				{
					BE_LOGW("ITwinDecoration", "Load material definitions failed. " << exp.error());
					onFinishCallback(exp, {});
					return;
				}
				auto dataIO = dataIOPtr->GetAutoLock();
				for (auto const& [iModelID, materialMap] : *dataIO)
				{
					BE_LOGI("ITwinDecoration", "Loaded " << materialMap.size() << " material definition(s) for imodel " << iModelID);
				}

				// Update the URL used to access textures.
				SThis->BuildDecorationFilesURL(decorationId);

				// Transfer loaded material definitions to internal map.
				{
					auto thdata = SThis->thdata_.GetAutoLock();
					auto& data_ = thdata->data_;
					if (!SSpecificModels->empty())
					{
						// Only replace materials for the selected iModel(s).
						for (std::string const& iModelId : *SSpecificModels)
						{
							data_.erase(iModelId);
							auto const itLoadedMats = dataIO->find(iModelId);
							if (itLoadedMats != dataIO->end())
							{
								data_[iModelId] = itLoadedMats->second;
							}
						}
					}
					else
					{
						data_.swap(dataIO);
						SThis->SetShouldSave(false);
					}
				}

				onFinishCallback(exp, *textureUsageMapPtr);
			}
			);

	}

	bool MaterialPersistenceManager::Impl::HasLoadedModel(std::string const& iModelId) const
	{
		auto thdata = thdata_.GetRAutoLock();
		return thdata->loadedIModelIds_.contains(iModelId);
	}

	void MaterialPersistenceManager::Impl::SetLoadedModel(std::string const& iModelId, bool bLoaded)
	{
		auto thdata = thdata_.GetAutoLock();
		if (bLoaded)
			thdata->loadedIModelIds_.insert(iModelId);
		else
			thdata->loadedIModelIds_.erase(iModelId);
	}

	void MaterialPersistenceManager::Impl::RequestDeleteITwinMaterialsInDB(
		std::optional<std::string> const& specificIModelID /*= std::nullopt*/)
	{
		auto thdata = thdata_.GetAutoLock();
		auto& data_ = thdata->data_;	
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
					matInfo.InvalidateDB();
					needDeletion = true;
				}
			}
		}
		if (needDeletion)
		{
			InvalidateDB();
		}
	}

	bool MaterialPersistenceManager::Impl::AsyncUploadChannelTextureIfNeeded(
		std::shared_ptr<AsyncRequestGroupCallback> const& callbackPtr,
		ITwinChannelMap& texMap,
		std::string const& decorationId)
	{
		if (texMap.eSource != ETextureSource::LocalDisk)
			return false;
		if (!texMap.HasTexture())
			return false;

		std::string const filePath = texMap.texture;

		{
			// See if this texture has been uploaded before (typically if a same texture is used in multiple
			// materials).
			auto thdata = thdata_.GetRAutoLock();
			auto itDecoId = thdata->localToDecoTexId_.find(filePath);
			if (itDecoId != thdata->localToDecoTexId_.end())
			{
				texMap.texture = itDecoId->second;
				texMap.eSource = ETextureSource::Decoration;
				return false;
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

		AsyncPostFile(GetHttp(), callbackPtr,
			[this, &texMap, filePath, basename](const Http::Response& r)
		{
			// Note that texMap is still valid because it's owned by the shared dataIO wrapped in
			// callbackPtr...
			bool const uploaded = (r.first == 200 || r.first == 201 || r.first == 409);  //409 is Conflict, meaning the file is already uploaded.
			if (uploaded)
			{
				// Replace the identifier in the channel.
				texMap.texture = basename;
				texMap.eSource = ETextureSource::Decoration;

				auto thdata = thdata_.GetAutoLock();
				thdata->localToDecoTexId_.emplace(filePath, basename);

				BE_LOGI("ITwinDecoration", "Uploaded texture " << filePath);
			}
			else
			{
				BE_LOGE("ITwinDecoration", "Failed upload of texture " << filePath << " - error code: " << r.first);
			}
			return uploaded;
		},
			"decorations/" + decorationId + "/files",
			"file",
			filePath,
			{ { "filename", basename } });
		return true;
	}

	size_t MaterialPersistenceManager::Impl::AsyncUploadTexuresIfNeeded(
		std::shared_ptr<AsyncRequestGroupCallback> const& callbackPtr,
		ITwinMaterial& material,
		std::string const& decorationId)
	{
		size_t nUploadStarted = 0;
		for (uint8_t chanIndex(0) ; chanIndex < (uint8_t)EChannelType::ENUM_END; ++chanIndex)
		{
			EChannelType const chan = static_cast<EChannelType>(chanIndex);
			auto const chanMap = material.GetChannelMapOpt(chan);
			if (chanMap && chanMap->HasTexture())
			{
				// We need a mutable ITwinChannelMap here, as we may change the map's ID or source.
				ITwinChannelMap& chanMapRef = material.GetMutableChannelMap(chan);
				if (AsyncUploadChannelTextureIfNeeded(
					callbackPtr, chanMapRef, decorationId))
				{
					nUploadStarted++;
				}
			}
		}
		return nUploadStarted;
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
		relativeURL += http_->EncodeForUrl(textureKey.id);
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

		auto& thdata = thdata_.GetRAutoLock();
		auto const& matIDToDisplayName_ = thdata->matIDToDisplayName_;
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

	void MaterialPersistenceManager::Impl::AsyncSaveDataOnServer(const std::string& decorationId,
		std::function<void(bool)>&& onDataSavedFunc /*= {}*/)
	{
		std::function<void(bool)> onMaterialSavedFunc =
			[isValidLambda = isThisValid_, this,
			 callback = std::move(onDataSavedFunc)](bool bSuccess)
		{
			if (*isValidLambda && bSuccess)
			{
				// reset the global save status.
				OnSaved();
			}
			if (callback)
				callback(bSuccess);
		};

		bool const saveMaterialCollection = !materialDirectory_.empty();

		// Make a copy of the data to save at current time.
		std::shared_ptr<IModelMaterialMap> dataIO = std::make_shared<IModelMaterialMap>();
		{	
			auto thdata = thdata_.GetAutoLock();

			for (auto& [iModelID, materialMap] : thdata->data_)
			{
				IModelMaterialInfo matsToSaveForIModel;
				bool const saveIModelMaterials =
					saveMaterialCollection && thdata->iModelsForMaterialCollection_.contains(iModelID);
				for (auto& [matID, matInfo] : materialMap)
				{
					if (!matInfo.ShouldSave() && !saveIModelMaterials)
						continue;
					if (matInfo.ShouldSave())
						matInfo.OnStartSave();
					matsToSaveForIModel.emplace(matID, matInfo);
				}
				if (!matsToSaveForIModel.empty())
				{
					dataIO->emplace(iModelID, matsToSaveForIModel);
				}
			}
		}


		std::shared_ptr<AsyncRequestGroupCallback> onUploadFinished =
			std::make_shared<AsyncRequestGroupCallback>(
				[isValidLambda = isThisValid_, this, decorationId, dataIO,
				 callback = std::move(onMaterialSavedFunc)](bool bSuccess) mutable
		{
			if (*isValidLambda)
			{
				if (bSuccess)
					AsyncSaveMaterials(decorationId, dataIO, std::move(callback));
				else if (callback)
					callback(false);
			}
		}, isThisValid_);

		OnStartSave();

		// First upload textures if needed, and update texture identifiers accordingly.
		for (auto& [iModelID, materialMap] : *dataIO)
		{
			for (auto& [matID, matInfo] : materialMap)
			{
				AsyncUploadTexuresIfNeeded(onUploadFinished, matInfo.settings, decorationId);
			}
		}

		onUploadFinished->OnFirstLevelRequestsRegistered();
	}

	void MaterialPersistenceManager::Impl::AsyncSaveMaterials(const std::string& decorationId,
		std::shared_ptr<IModelMaterialMap> dataIO,
		std::function<void(bool)>&& onDataSavedFunc /*= {}*/)
	{
		struct SJsonMaterialIdVect { std::vector<std::string> ids; };
		SJsonMaterialWithIdVec jInPost; // for creation
		SJsonMaterialWithIdVec jInPut; // for update
		SJsonMaterialIdVect jInDelete; // for deletion

		using MaterialIdSet = std::unordered_set<uint64_t>;
		using IModelMaterialIdMap = std::unordered_map<std::string, MaterialIdSet>;
		IModelMaterialIdMap allMatIdsToCreate, allMatIdsToUpdate, allMatIdsToDelete;

		SJsonMaterialWithIdVec jMaterialCollection;
		bool const saveMaterialCollection = !materialDirectory_.empty();

		std::shared_ptr<AsyncRequestGroupCallback> onMaterialsSaved =
			std::make_shared<AsyncRequestGroupCallback>(
				std::move(onDataSavedFunc), isThisValid_);

		// Now that needed uploads have occurred, we can update the URL used to access those textures.
		BuildDecorationFilesURL(decorationId);

		auto thdata = thdata_.GetRAutoLock();
		auto& iModelsForMaterialCollection_ = thdata->iModelsForMaterialCollection_;

		// Sort materials for requests (addition/update)
		SJsonMaterialWithId jsonMat;
		for (auto const& [iModelID, materialMap] : *dataIO)
		{
			MaterialIdSet matIdsToCreate, matIdsToUpdate, matIdsToDelete;
			bool const saveIModelMaterials =
				saveMaterialCollection && iModelsForMaterialCollection_.contains(iModelID);
			for (auto const& [matID, matInfo] : materialMap)
			{
				MaterialToJson(matInfo.settings, iModelID, matID, jsonMat);

				if (saveIModelMaterials)
				{
					jMaterialCollection.materials.push_back(jsonMat);
					if (!matInfo.ShouldSave())
						continue;
				}

				if (matInfo.needDeleteFromDB)
				{
					if (matInfo.existsInDB)
					{
						jInDelete.ids.push_back(jsonMat.id);
						matIdsToDelete.insert(matID);
					}
				}
				else if (matInfo.existsInDB)
				{
					jInPut.materials.push_back(jsonMat);
					matIdsToUpdate.insert(matID);
				}
				else
				{
					jInPost.materials.push_back(jsonMat);
					matIdsToCreate.insert(matID);
				}
			}
			if (!matIdsToCreate.empty())
			{
				allMatIdsToCreate.emplace(iModelID, std::move(matIdsToCreate));
			}
			if (!matIdsToUpdate.empty())
			{
				allMatIdsToUpdate.emplace(iModelID, std::move(matIdsToUpdate));
			}
			if (!matIdsToDelete.empty())
			{
				allMatIdsToDelete.emplace(iModelID, std::move(matIdsToDelete));
			}
		}

		auto const UpdateDBFlagsOnSuccess = [this](bool forExistingInDB, IModelMaterialIdMap const& modifiedMats)
		{
			auto thdata = thdata_.GetAutoLock();
			for (auto const& [iModelID, modifiedMatIds] : modifiedMats)
			{
				auto& realMaterials = thdata->data_[iModelID];
				for (auto matId : modifiedMatIds)
				{
					auto itMatInfo = realMaterials.find(matId);
					if (itMatInfo != realMaterials.end())
					{
						itMatInfo->second.OnSaved();

						if (!forExistingInDB)
						{
							// The material was successfully created in DB now
							itMatInfo->second.existsInDB = true;
						}
					}
				}
			}
		};

		// Delete material definitions if requested
		if (!jInDelete.ids.empty())
		{
			AsyncDeleteJsonJBody<std::string>(GetHttp(), onMaterialsSaved,
				[this,
				 jInDelete,
				 allMatIdsToDelete](long httpCode, const Tools::TSharedLockableData<std::string>& /*joutPtr*/)
			{
				const bool bSuccess = (httpCode == 200 || httpCode == 201 || httpCode == 204 /* No-Content*/);
				if (bSuccess)
				{
					BE_LOGI("ITwinDecoration", "Deleted " << jInDelete.ids.size()
						<< " material definition(s). Http status: " << httpCode);
					// Now remove all deleted entries in the manager map
					auto thdata = thdata_.GetAutoLock();
					for (auto const& [iModelID, deletedIds] : allMatIdsToDelete)
					{
						auto& realMaterials = thdata->data_[iModelID];
						for (auto delId : deletedIds)
						{
							realMaterials.erase(delId);
						}
					}
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Deleting material definitions failed. Http status: " << httpCode);
				}
				return bSuccess;
			},
				"decorations/"+ decorationId + "/materials",
				jInDelete);
		}

		// Post (new materials)
		if (!jInPost.materials.empty())
		{
			AsyncPostJsonJBody<SJsonMaterialWithIdVec>(GetHttp(), onMaterialsSaved,
				[this, UpdateDBFlagsOnSuccess,
				jInPost, allMatIdsToCreate](long httpCode, const Tools::TSharedLockableData<SJsonMaterialWithIdVec>& joutPtr)
			{
				const bool bSuccess = (httpCode == 200 || httpCode == 201);
				if (bSuccess)
				{
					auto unlockedJout = joutPtr->GetAutoLock();
					SJsonMaterialWithIdVec const& jOutPost = unlockedJout.Get();
					if (jInPost.materials.size() == jOutPost.materials.size())
					{
						BE_LOGI("ITwinDecoration", "Saved " << jInPost.materials.size() << " new material definition(s). Http status: " << httpCode);
						UpdateDBFlagsOnSuccess(false, allMatIdsToCreate);
					}
					else
					{
						BE_ISSUE("mismatch count while saving new material definitions",
							jInPost.materials.size(), jOutPost.materials.size());
					}
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Saving new material definitions failed. Http status: " << httpCode);
				}
				return bSuccess;
			},
				"decorations/" + decorationId + "/materials",
				jInPost);
		}

		// Put (updated materials)
		if (!jInPut.materials.empty())
		{
			struct SJsonMaterialOutUpd
			{
				int64_t numUpdated = 0;
			};
			AsyncPutJsonJBody<SJsonMaterialOutUpd>(GetHttp(), onMaterialsSaved,
				[this, UpdateDBFlagsOnSuccess,
				jInPut, allMatIdsToUpdate](long httpCode, const Tools::TSharedLockableData<SJsonMaterialOutUpd>& joutPtr)
			{
				const bool bSuccess = (httpCode == 200 || httpCode == 201);
				if (bSuccess)
				{
					auto unlockedJout = joutPtr->GetAutoLock();
					SJsonMaterialOutUpd const& jOutPut = unlockedJout.Get();
					if (jInPut.materials.size() == static_cast<size_t>(jOutPut.numUpdated))
					{
						BE_LOGI("ITwinDecoration", "Updated " << jInPut.materials.size() << " material definition(s). Http status: " << httpCode);
						UpdateDBFlagsOnSuccess(true, allMatIdsToUpdate);
					}
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Updating material definitions failed. Http status: " << httpCode);
				}
				return bSuccess;
			},
				"decorations/" + decorationId + "/materials",
				jInPut);
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

		onMaterialsSaved->OnFirstLevelRequestsRegistered();
	}

	size_t MaterialPersistenceManager::Impl::ListIModelsWithMaterialSettings(std::vector<std::string>& iModelIds) const
	{
		iModelIds.clear();
		auto thdata = thdata_.GetRAutoLock();
		auto const& data_ = thdata->data_;
		iModelIds.reserve(data_.size());
		for (auto const& [iModelID, materialMap] : data_)
		{
			iModelIds.push_back(iModelID);
		}
		return iModelIds.size();
	}

	bool MaterialPersistenceManager::Impl::HasMaterialDefinition(std::string const& iModelId, uint64_t materialId) const
	{
		auto thdata = thdata_.GetRAutoLock();
		auto const& data_ = thdata->data_;
		auto itIModel = data_.find(iModelId);
		if (itIModel == data_.end())
			return false;
		return itIModel->second.contains(materialId);
	}

	bool MaterialPersistenceManager::Impl::GetMaterialSettings(std::string const& iModelId, uint64_t materialId, ITwinMaterial& material) const
	{
		auto thdata = thdata_.GetRAutoLock();
		auto const& data_ = thdata->data_;
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
		auto thdata = thdata_.GetAutoLock();
		IModelMaterialInfo& materialMap = thdata->data_[iModelId];
		MaterialInfo& materialInfo = materialMap[materialId];
		ITwinMaterial& storedSettings = materialInfo.settings;
		if (material != storedSettings)
		{
			storedSettings = material;
			materialInfo.InvalidateDB();
			InvalidateDB();
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
		std::filesystem::path const& materialJsonPath,
		std::string const& iModelID,
		TextureUsageMap& outTextureUsageMap,
		std::unordered_map<uint64_t, std::string>& outMatIDToDisplayName)
	{
		auto thdata = thdata_.GetAutoLock();
		auto& iModelsForMaterialCollection_ = thdata->iModelsForMaterialCollection_;
		auto& matIDToDisplayName_ = thdata->matIDToDisplayName_;
		auto& perIModelTextures_ = thdata->perIModelTextures_;
		auto& data_ = thdata->data_;

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
			ParseJSONMaterials(jsonMaterials.materials, dataIO, perIModelTextures, outTextureUsageMap);

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
		auto thdata = thdata_.GetAutoLock();
		auto& matIDToDisplayName_ = thdata->matIDToDisplayName_;
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
		void operator()(const int64_t& nValue) const
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
		auto thdata = thdata_.GetAutoLock();
		IModelMaterialInfo& iModelMats = thdata->data_[iModelId];
		auto& matEntry = iModelMats[materialId];
		matEntry.settings = material;
		matEntry.SetShouldSave(false);
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
		KeyValueStringMap& outMap,
		bool bForceUseTexturesFromLocalDir /*= false*/) const
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

			[[maybe_unused]] std::error_code ec;

			for (auto& [key, value] : outMap)
			{
				// Note about symbolic paths such as <MatLibrary>/Folder/name.ext: to simplify the
				// implementation of the Component Center management, we enforce changing those paths to
				// absolute paths using the texture directory where the json file was loaded (with the CC,
				// both files (json and PNG) are downloaded in individual temporary directories)
				// The drawback of doing this is that we will upload those textures in the decoration service
				// while ideally we could prefer to avoid it, and instead download the corresponding
				// material component from the Component Center if needed, when we load a scene referencing
				// such a texture (it would necessitate more work to associate a material name to the CC
				// identifier).
				if (key.ends_with("Map")
					&& !value.empty()
					&& value != "\"\""
					&& value != "\"0\""
					&& (bForceUseTexturesFromLocalDir || !value.starts_with("\"<")) /* avoid losing symbolic prefix such as <MatLibrary> */
					&& !value.ends_with("/0\""))
				{
					std::filesystem::path const texPathInFile = trimPath(value);
					std::filesystem::path const baseName = texPathInFile.filename();
					std::filesystem::path texPathToWrite = textureDir / baseName;
					if (bForceUseTexturesFromLocalDir)
					{
						BE_ASSERT(std::filesystem::exists(texPathToWrite, ec));
					}
					else if (texPathInFile.is_relative()
						&& std::filesystem::exists(textureDir / texPathInFile, ec))
					{
						texPathToWrite = textureDir / texPathInFile;
					}
					value = quote + texPathToWrite.generic_string() + quote;
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
		return GetImpl().ShouldSave();
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
		TextureUsageMap& outTextureUsageMap,
		std::set<std::string> const& specificModels /*= {}*/)
	{
		GetImpl().LoadDataFromServer(decorationId, outTextureUsageMap, specificModels);
	}

	void MaterialPersistenceManager::AsyncLoadDataFromServer(
		std::string const& decorationId,
		std::function<void(expected<void, std::string> const&, TextureUsageMap const&)> callback,
		std::set<std::string> const& specificModels)
	{
		GetImpl().AsyncLoadDataFromServer(decorationId, callback, specificModels);
	}

	void MaterialPersistenceManager::AsyncSaveDataOnServer(const std::string& decorationId, std::function<void(bool)>&& onDataSavedFunc /*= {}*/)
	{
		GetImpl().AsyncSaveDataOnServer(decorationId, std::move(onDataSavedFunc));
	}

	void MaterialPersistenceManager::SetLocalMaterialDirectory(std::filesystem::path const& materialDirectory)
	{
		GetImpl().SetLocalMaterialDirectory(materialDirectory);
	}

	size_t MaterialPersistenceManager::LoadMaterialCollection(std::filesystem::path const& materialJsonPath,
		std::string const& iModelID,
		TextureUsageMap& outTextureUsageMap,
		std::unordered_map<uint64_t, std::string>& matIDToDisplayName)
	{
		return GetImpl().LoadMaterialCollection(materialJsonPath, iModelID, outTextureUsageMap, matIDToDisplayName);
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
		std::filesystem::path const& textureDir, KeyValueStringMap& outMap,
		bool bForceUseTexturesFromLocalDir /*= false*/) const
	{
		return GetImpl().ConvertJsonFileToKeyValueMap(jsonPath, textureDir, outMap, bForceUseTexturesFromLocalDir);
	}

	bool MaterialPersistenceManager::RenameMaterialInJsonFile(std::filesystem::path const& jsonPath,
		std::string const& newMaterialName, std::string& outError) const
	{
		return GetImpl().RenameMaterialInJsonFile(jsonPath, newMaterialName, outError);
	}
}
