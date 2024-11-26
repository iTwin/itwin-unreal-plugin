/*--------------------------------------------------------------------------------------+
|
|     $Source: MaterialPersistence.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#ifndef SDK_CPPMODULES
#	include <memory>
#	include <optional>
#	include <string>
#	include <vector>
#	ifndef MODULE_EXPORT
#		define MODULE_EXPORT
#	endif // !MODULE_EXPORT
#endif

MODULE_EXPORT namespace SDK::Core 
{
	class Http;
	struct ITwinMaterial;

	/// Short-term solution to load/save material settings in the decoration service.
	/// In the future, materials will be part of the Scene API, this is just a temporary solution.
	class MaterialPersistenceManager
	{
	public:
		MaterialPersistenceManager();
		~MaterialPersistenceManager();

		bool NeedUpdateDB() const;

		/// Load the data from the server
		void LoadDataFromServer(const std::string& decorationId, const std::string& accessToken);
		/// Save the data on the server
		void SaveDataOnServer(const std::string& decorationId, const std::string& accessToken);

		/// Fills the list of iModel IDs for which some material customizations are known.
		size_t ListIModelsWithMaterialSettings(std::vector<std::string>& iModelIds) const;

		/// Get material settings for a given iModel and material.
		bool GetMaterialSettings(std::string const& iModelId, uint64_t materialId, ITwinMaterial& material) const;

		/// Store material settings for a given iModel and material.
		void SetMaterialSettings(std::string const& iModelId, uint64_t materialId, ITwinMaterial const& material);

		/// Set Http server to use (if none provided, the default created by Config is used.)
		void SetHttp(std::shared_ptr<Http> http);

		/// Request deleting all custom materials in DB upon next saving operation.
		/// Beware this will apply to *all* imodels in current iTwin, except if specificIModelID is provided.
		void RequestDeleteITwinMaterialsInDB(std::optional<std::string> const& specificIModelID = std::nullopt);
		/// Request deleting all custom materials for a given iModel.
		void RequestDeleteIModelMaterialsInDB(std::string const& iModelID);

		/////////////// [ ULTRA TEMPORARY, FOR CARROT MVP ///////////////
		void EnableOffsetAndGeoLocation(bool bEnableOffsetAndGeoLoc); // Can be disabled (for Presentations...)

		void SetModelOffset(std::string const& iModelId, std::array<double, 3> const& posOffset, std::array<double, 3> const& rotOffset);
		bool GetModelOffset(std::string const& iModelId, std::array<double, 3>& posOffset, std::array<double, 3>& rotOffset) const;

		void SetSceneGeoLocation(std::string const& iModelId, std::array<double, 3> const& latLongHeight);
		bool GetSceneGeoLocation(std::string const& iModelId, std::array<double, 3>& latLongHeight) const;

		/////////////// ULTRA TEMPORARY, FOR CARROT MVP ] ///////////////

	private:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl();
		Impl const& GetImpl() const;
	};
}
