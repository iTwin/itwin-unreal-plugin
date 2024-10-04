/*--------------------------------------------------------------------------------------+
|
|     $Source: MaterialPersistence.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#include "MaterialPersistence.h"

#include "Core/ITwinAPI/ITwinMaterial.h"
#include "Core/Network/Network.h"
#include "Config.h"

#include <unordered_map>

namespace SDK::Core
{
	class MaterialPersistenceManager::Impl
	{
	public:
		bool NeedUpdateDB() const;
		void LoadDataFromServer(const std::string& decorationId, const std::string& accessToken);
		void SaveDataOnServer(const std::string& decorationId, const std::string& accessToken);

		bool GetMaterialSettings(std::string const& iModelId, uint64_t materialId, ITwinMaterial& material) const;
		void SetMaterialSettings(std::string const& iModelId, uint64_t materialId, ITwinMaterial const& material);

		std::shared_ptr<Http>& GetHttp() { return http_; }
		void SetHttp(const std::shared_ptr<Http>& http) { http_ = http; }

		void SetDeleteAllMaterialsInDB(bool bDelete);

	private:
		void InvalidateDB() { needUpdateDB_ = true; }

		// Stores the only parameters we can edit with Carrot MVP...
		struct BasicMaterialSettings
		{
			std::optional<double> roughness;
			std::optional<double> metallic;
			std::optional<double> opacity;
		};
		struct MaterialInfo
		{
			BasicMaterialSettings settings;
			bool existsInDB = false; // to distinguish create vs update...
			bool needUpdateDB = false;
			bool needDeleteFromDB = false;
		};
		using IModelMaterialInfo = std::unordered_map<uint64_t, MaterialInfo>;
		std::unordered_map<std::string, IModelMaterialInfo> data_;
		mutable bool needUpdateDB_ = false;

		// For transfer to/from DB
		struct SJsonMaterialWithId
		{
			std::string id;
			std::optional<double> roughness;
			std::optional<double> metallic;
			std::optional<double> opacity;
		};

		std::shared_ptr<Http> http_;
	};

	void MaterialPersistenceManager::Impl::LoadDataFromServer(const std::string& decorationId, const std::string& accessToken)
	{
		if (decorationId.empty())
		{
			BE_ISSUE("decoration ID missing to load materials");
			return;
		}
		if (accessToken.empty())
		{
			BE_ISSUE("no access token to load materials");
			return;
		}
		data_.clear();

		if (!GetHttp())
		{
			BE_ISSUE("No http support!");
			return;
		}

		struct SJsonInEmpty {};

		struct SJsonLink
		{
			std::optional<std::string> prev;
			std::optional<std::string> self;
			std::optional<std::string> next;
		};

		SJsonInEmpty jIn;
		struct SJsonOut { int total_rows; std::vector<SJsonMaterialWithId> rows; SJsonLink _links; };
		SJsonOut jOut;

		Http::Headers headers;
		headers.emplace_back("Authorization", std::string("Bearer ") + accessToken);

		long status = GetHttp()->GetJsonJBody(
			jOut, decorationId + "/materials", jIn, headers);
		bool continueLoading = true;

		while (continueLoading)
		{
			if (status != 200 && status != 201)
			{
				continueLoading = false;
				BE_LOGW("ITwinDecoration", "Load materials failed. Http status: " << status);
			}
			for (auto const& row : jOut.rows)
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

				IModelMaterialInfo& materialMap = data_[iModelID];
				MaterialInfo& materialInfo = materialMap[matID];
				materialInfo.settings.roughness = row.roughness;
				materialInfo.settings.metallic = row.metallic;
				materialInfo.settings.opacity = row.opacity;
				materialInfo.needUpdateDB = false;
				materialInfo.existsInDB = true;
			}

			jOut.rows.clear();

			if (jOut._links.next.has_value() && !jOut._links.next.value().empty())
			{
				status = GetHttp()->GetJsonJBody(jOut, jOut._links.next.value(), jIn, headers, true);
			}
			else
			{
				continueLoading = false;
			}
		}

		needUpdateDB_ = false;
	}

	void MaterialPersistenceManager::Impl::SetDeleteAllMaterialsInDB(bool bDelete)
	{
		for (auto& [iModelID, materialMap] : data_)
		{
			for (auto& [matID, matInfo] : materialMap)
			{
				if (matInfo.existsInDB)
				{
					matInfo.needDeleteFromDB = bDelete;
					if (bDelete)
						matInfo.needUpdateDB = true;
				}
			}
		}
		InvalidateDB();
	}

	void MaterialPersistenceManager::Impl::SaveDataOnServer(const std::string& decorationId, const std::string& accessToken)
	{
		struct SJsonMaterialWithIdVect { std::vector<SJsonMaterialWithId> materials; };
		struct SJsonMaterialIdVect { std::vector<std::string> ids; };
		SJsonMaterialWithIdVect jInPost; // for creation
		SJsonMaterialWithIdVect jInPut; // for update
		SJsonMaterialIdVect jInDelete; // for deletion

		// Sort materials for requests (addition/update)
		SJsonMaterialWithId jsonMat;
		for (auto const& [iModelID, materialMap] : data_)
		{
			for (auto const& [matID, matInfo] : materialMap)
			{
				if (!matInfo.needUpdateDB)
					continue;
				jsonMat.id = std::to_string(matID) + "_" + iModelID;
				jsonMat.metallic = matInfo.settings.metallic;
				jsonMat.roughness = matInfo.settings.roughness;
				jsonMat.opacity = matInfo.settings.opacity;
				if (matInfo.needDeleteFromDB)
				{
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

		Http::Headers headers;
		headers.emplace_back("Authorization", std::string("Bearer ") + accessToken);


		auto const UpdateDBFlagsOnSuccess = [this](bool forExistingInDB)
		{
			for (auto& [iModelID, materialMap] : data_)
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


		// Delete (applied to all materials for now)
		if (!jInDelete.ids.empty())
		{
			bool deletionOK = false;
			std::string jOutDelete;
			long status = GetHttp()->DeleteJsonJBody(
				jOutDelete, decorationId + "/materials", jInDelete, headers);
			if (status == 200 || status == 201)
			{
				deletionOK = true;
			}
			else
			{
				BE_LOGW("ITwinDecoration", "Deleting materials failed. Http status: " << status);
			}
			saveOK &= deletionOK;
		}

		// Post (new materials)
		if (!jInPost.materials.empty())
		{
			bool creationOK = false;
			SJsonMaterialWithIdVect jOutPost;
			long status = GetHttp()->PostJsonJBody(
				jOutPost, decorationId + "/materials", jInPost, headers);

			if (status == 200 || status == 201)
			{
				if (jInPost.materials.size() == jOutPost.materials.size())
				{
					UpdateDBFlagsOnSuccess(false);
					creationOK = true;
				}
			}
			else
			{
				BE_LOGW("ITwinDecoration", "Saving new materials failed. Http status: " << status);
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
				jOutPut, decorationId + "/materials", jInPut, headers);

			if (status == 200 || status == 201)
			{
				if (jInPut.materials.size() == static_cast<size_t>(jOutPut.numUpdated))
				{
					UpdateDBFlagsOnSuccess(true);
					updateOK = true;
				}
			}
			else
			{
				BE_LOGW("ITwinDecoration", "Updating materials failed. Http status: " << status);
			}
			saveOK &= updateOK;
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

	bool MaterialPersistenceManager::Impl::GetMaterialSettings(std::string const& iModelId, uint64_t materialId, ITwinMaterial& material) const
	{
		auto itIModel = data_.find(iModelId);
		if (itIModel == data_.end())
			return false;
		IModelMaterialInfo const& materialMap = itIModel->second;
		auto itMaterial = materialMap.find(materialId);
		if (itMaterial == materialMap.end())
			return false;
		// We do have some data for this material
		BasicMaterialSettings const& basicSettings = itMaterial->second.settings;
		if (basicSettings.roughness)
			material.SetChannelIntensity(EChannelType::Roughness, *basicSettings.roughness);
		if (basicSettings.metallic)
			material.SetChannelIntensity(EChannelType::Metallic, *basicSettings.metallic);
		if (basicSettings.opacity)
			material.SetChannelIntensity(EChannelType::Transparency, 1. - *basicSettings.opacity);
		return true;
	}

	void MaterialPersistenceManager::Impl::SetMaterialSettings(std::string const& iModelId, uint64_t materialId, ITwinMaterial const& material)
	{
		IModelMaterialInfo& materialMap = data_[iModelId];
		MaterialInfo& materialInfo = materialMap[materialId];
		BasicMaterialSettings& basicSettings = materialInfo.settings;
		std::optional<double> intensityOpt;
		intensityOpt = material.GetChannelIntensityOpt(EChannelType::Roughness);
		if (intensityOpt)
		{
			basicSettings.roughness = static_cast<float>(*intensityOpt);
			materialInfo.needUpdateDB = true;
			InvalidateDB();
		}
		intensityOpt = material.GetChannelIntensityOpt(EChannelType::Metallic);
		if (intensityOpt)
		{
			basicSettings.metallic = static_cast<float>(*intensityOpt);
			materialInfo.needUpdateDB = true;
			InvalidateDB();
		}
		intensityOpt = material.GetChannelIntensityOpt(EChannelType::Transparency);
		if (intensityOpt)
		{
			basicSettings.opacity = static_cast<float>(1.0 - *intensityOpt);
			materialInfo.needUpdateDB = true;
			InvalidateDB();
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

	void MaterialPersistenceManager::SetHttp(std::shared_ptr<Http> http)
	{
		GetImpl().SetHttp(http);
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

	void MaterialPersistenceManager::LoadDataFromServer(const std::string& decorationId, const std::string& accessToken)
	{
		GetImpl().LoadDataFromServer(decorationId, accessToken);
	}

	void MaterialPersistenceManager::SaveDataOnServer(const std::string& decorationId, const std::string& accessToken)
	{
		GetImpl().SaveDataOnServer(decorationId, accessToken);
	}

	void MaterialPersistenceManager::GetMaterialSettings(std::string const& iModelId, uint64_t materialId, ITwinMaterial& material) const
	{
		GetImpl().GetMaterialSettings(iModelId, materialId, material);
	}

	void MaterialPersistenceManager::SetMaterialSettings(std::string const& iModelId, uint64_t materialId, ITwinMaterial const& material)
	{
		GetImpl().SetMaterialSettings(iModelId, materialId, material);
	}

	void MaterialPersistenceManager::SetDeleteAllMaterialsInDB(bool bDelete)
	{
		GetImpl().SetDeleteAllMaterialsInDB(bDelete);
	}

}
