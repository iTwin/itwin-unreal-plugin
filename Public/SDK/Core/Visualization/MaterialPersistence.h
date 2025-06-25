/*--------------------------------------------------------------------------------------+
|
|     $Source: MaterialPersistence.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#ifndef SDK_CPPMODULES
#	include <filesystem>
#	include <map>
#	include <memory>
#	include <optional>
#	include <set>
#	include <string>
#	include <unordered_map>
#	include <vector>
#	ifndef MODULE_EXPORT
#		define MODULE_EXPORT
#	endif // !MODULE_EXPORT
#endif

#include "../AdvVizLinkType.h"
#include "TextureKey.h"
#include "TextureUsage.h"

MODULE_EXPORT namespace AdvViz::SDK 
{
	class Http;
	struct ITwinMaterial;

	using KeyValueStringMap = std::map<std::string, std::string>;

	/// Short-term solution to load/save material settings in the decoration service.
	/// In the future, materials will be part of the Scene API, this is just a temporary solution.
	class ADVVIZ_LINK MaterialPersistenceManager
	{
	public:
		MaterialPersistenceManager();
		~MaterialPersistenceManager();

		bool NeedUpdateDB() const;

		/// Load the data from the server
		void LoadDataFromServer(std::string const& decorationId,
			std::set<std::string> const& specificModels = {});

		/// Save the data on the server
		void SaveDataOnServer(const std::string& decorationId);

		/// Fills the list of iModel IDs for which some material customizations are known.
		size_t ListIModelsWithMaterialSettings(std::vector<std::string>& iModelIds) const;

		/// Get map iModelID -> TextureSet.
		PerIModelTextureSet const& GetDecorationTexturesByIModel() const;

		TextureUsageMap const& GetTextureUsageMap() const;
		TextureUsage GetTextureUsage(TextureKey const& textureKey) const;
		void AddTextureUsage(TextureKey const& textureKey, EChannelType channel);

		/// Returns whether the given iModel was totally loaded from the decoration service (including
		/// textures), and thus can be used to customize the model.
		bool HasLoadedModel(std::string const& iModelId) const;

		/// Mark the given iModel as fully loaded or not.
		void SetLoadedModel(std::string const& iModelId, bool bLoaded = true);

		/// Returns whether a material definition exists for given iModel and material ID.
		bool HasMaterialDefinition(std::string const& iModelId, uint64_t materialId) const;

		/// Get material settings for a given iModel and material.
		bool GetMaterialSettings(std::string const& iModelId, uint64_t materialId, ITwinMaterial& material) const;

		/// Store material settings for a given iModel and material.
		void SetMaterialSettings(std::string const& iModelId, uint64_t materialId, ITwinMaterial const& material);

		/// Set Http server to use (if none provided, the default created by Config is used.)
		void SetHttp(std::shared_ptr<Http> http);

		/// Set path to Material Library. Beware this may not be a physical file path in the context of a
		/// packaged application: it should be used withing UE's file system API only.
		void SetMaterialLibraryDirectory(std::string const& materialLibraryDirectory);

		/// Return the base url to access a texture stored in the decoration service or in the material
		/// library.
		std::string GetBaseURL(ETextureSource texSource) const;

		/// Return the relative url to access a texture stored in the decoration service or in the material
		/// library.
		std::string GetRelativeURL(TextureKey const& textureKey) const;

		/// Return the url to access a texture stored in the decoration service or in the material library.
		std::string GetTextureURL(TextureKey const& textureKey) const;
		std::string GetTextureURL(std::string const& textureId, ETextureSource texSource) const;

		/// Request deleting all custom materials in DB upon next saving operation.
		/// Beware this will apply to *all* imodels in current iTwin, except if specificIModelID is provided.
		void RequestDeleteITwinMaterialsInDB(std::optional<std::string> const& specificIModelID = std::nullopt);
		/// Request deleting all custom materials for a given iModel.
		void RequestDeleteIModelMaterialsInDB(std::string const& iModelID);

		/// Activates a special mode where we try to load/save material definitions from a local Json file.
		/// Mostly used in development, typically so save some material collections.
		/// \param materialDirectory the destination directory of the materials.json file. An empty path will
		/// deactivate this special mode.
		/// \remark You need to save the decoration when the application asks (upon exit or Ctrl+U).
		void SetLocalMaterialDirectory(std::filesystem::path const& materialDirectory);

		/// Load a collection of materials, and assign it the given iModel ID.
		/// \return The number of loaded materials.
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

		/// Export the given material definition to json format.
		std::string ExportAsJson(ITwinMaterial const& material, std::string const& iModelID, uint64_t materialId) const;

		bool ConvertJsonFileToKeyValueMap(std::filesystem::path const& jsonPath,
			std::filesystem::path const& textureDir, KeyValueStringMap& outMap) const;

	private:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl();
		Impl const& GetImpl() const;
	};

}
