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
#	include <string>
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

		/// Get material settings for a given iModel and material.
		void GetMaterialSettings(std::string const& iModelId, uint64_t materialId, ITwinMaterial& material) const;

		/// Store material settings for a given iModel and material.
		void SetMaterialSettings(std::string const& iModelId, uint64_t materialId, ITwinMaterial const& material);

		/// Set Http server to use (if none provided, the default created by Config is used.)
		void SetHttp(std::shared_ptr<Http> http);

		/// Allow deleting all custom materials in DB upon exit
		void SetDeleteAllMaterialsInDB(bool bDelete);

	private:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl();
		Impl const& GetImpl() const;
	};
}
