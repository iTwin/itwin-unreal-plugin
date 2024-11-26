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

#include <mutex>
#include <unordered_map>

namespace SDK::Core
{
	class MaterialPersistenceManager::Impl
	{
	public:
		bool NeedUpdateDB() const;
		void LoadDataFromServer(const std::string& decorationId, const std::string& accessToken);
		void SaveDataOnServer(const std::string& decorationId, const std::string& accessToken);

		size_t ListIModelsWithMaterialSettings(std::vector<std::string>& iModelIds) const;
		bool GetMaterialSettings(std::string const& iModelId, uint64_t materialId, ITwinMaterial& material) const;
		void SetMaterialSettings(std::string const& iModelId, uint64_t materialId, ITwinMaterial const& material);

		std::shared_ptr<Http>& GetHttp() { return http_; }
		void SetHttp(const std::shared_ptr<Http>& http) { http_ = http; }

		void RequestDeleteITwinMaterialsInDB(std::optional<std::string> const& specificIModelID = std::nullopt);

		void EnableOffsetAndGeoLocation(bool bEnableOffsetAndGeoLoc);
		bool IsEnablingOffsetAndGeoLocation() const { return enableOffsetAndGeoLoc_; }

	private:
		void InvalidateDB() { needUpdateDB_ = true; }

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

		std::unordered_map<std::string, IModelMaterialInfo> data_;
		mutable Mutex dataMutex_;


		mutable bool needUpdateDB_ = false;
		bool enableOffsetAndGeoLoc_ = true; // quick & dirty, for the YII

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
			BE_ISSUE("decoration ID missing to load material definitions");
			return;
		}
		if (accessToken.empty())
		{
			BE_ISSUE("no access token to load material definitions");
			return;
		}
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
		std::unordered_map<std::string, IModelMaterialInfo> dataIO;

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
			jOut, "decorations/"+ decorationId + "/materials", jIn, headers);
		bool continueLoading = true;

		while (continueLoading)
		{
			if (status != 200 && status != 201)
			{
				continueLoading = false;
				BE_LOGW("ITwinDecoration", "Load material definitions failed. Http status: " << status);
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

				IModelMaterialInfo& materialMap = dataIO[iModelID];
				MaterialInfo& materialInfo = materialMap[matID];
				ITwinMaterial& material(materialInfo.settings);
				material.SetChannelIntensity(EChannelType::Roughness, row.roughness.value_or(0.));
				material.SetChannelIntensity(EChannelType::Metallic, row.metallic.value_or(0.));
				material.SetChannelIntensity(EChannelType::Transparency, 1.0 - row.opacity.value_or(0.));
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

		for (auto const& [iModelID, materialMap] : dataIO)
		{
			BE_LOGI("ITwinDecoration", "Loaded " << materialMap.size() << " material definitions for imodel " << iModelID);
		}

		{
			Lock lock(dataMutex_);
			data_.swap(dataIO);
		}

		needUpdateDB_ = false;
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

	void MaterialPersistenceManager::Impl::EnableOffsetAndGeoLocation(bool bEnableOffsetAndGeoLoc)
	{
		enableOffsetAndGeoLoc_ = bEnableOffsetAndGeoLoc;
	}

	void MaterialPersistenceManager::Impl::SaveDataOnServer(const std::string& decorationId, const std::string& accessToken)
	{
		struct SJsonMaterialWithIdVect { std::vector<SJsonMaterialWithId> materials; };
		struct SJsonMaterialIdVect { std::vector<std::string> ids; };
		SJsonMaterialWithIdVect jInPost; // for creation
		SJsonMaterialWithIdVect jInPut; // for update
		SJsonMaterialIdVect jInDelete; // for deletion

		// Make a copy of the data at current time.
		std::unordered_map<std::string, IModelMaterialInfo> dataIO;
		{
			Lock lock(dataMutex_);
			dataIO = data_;
		}

		// Sort materials for requests (addition/update)
		SJsonMaterialWithId jsonMat;
		for (auto const& [iModelID, materialMap] : dataIO)
		{
			for (auto const& [matID, matInfo] : materialMap)
			{
				if (!matInfo.needUpdateDB)
					continue;
				jsonMat.id = std::to_string(matID) + "_" + iModelID;
				ITwinMaterial const& material(matInfo.settings);
				jsonMat.metallic = material.GetChannelIntensityOpt(EChannelType::Metallic).value_or(0.);
				jsonMat.roughness = material.GetChannelIntensityOpt(EChannelType::Roughness).value_or(0.);
				jsonMat.opacity = 1. - material.GetChannelIntensityOpt(EChannelType::Transparency).value_or(0.);

				if (!matInfo.needDeleteFromDB
					&& !jsonMat.metallic && !jsonMat.roughness && !jsonMat.opacity)
				{
					BE_LOGW("ITwinDecoration", "Skipping material " << matID << " during saving process (empty)");
					continue;
				}

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

		Http::Headers headers;
		headers.emplace_back("Authorization", std::string("Bearer ") + accessToken);

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
				jOutDelete,"decorations/"+ decorationId + "/materials", jInDelete, headers);
			if (status == 200 || status == 201)
			{
				BE_LOGI("ITwinDecoration", "Deleted " << jInDelete.ids.size() << " material definitions. Http status: " << status);
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
			SJsonMaterialWithIdVect jOutPost;
			long status = GetHttp()->PostJsonJBody(
				jOutPost, "decorations/" + decorationId + "/materials", jInPost, headers);

			if (status == 200 || status == 201)
			{
				if (jInPost.materials.size() == jOutPost.materials.size())
				{
					BE_LOGI("ITwinDecoration", "Saved " << jInPost.materials.size() << " new material definitions. Http status: " << status);
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
				jOutPut, "decorations/" + decorationId + "/materials", jInPut, headers);

			if (status == 200 || status == 201)
			{
				if (jInPut.materials.size() == static_cast<size_t>(jOutPut.numUpdated))
				{
					BE_LOGI("ITwinDecoration", "Updated " << jInPut.materials.size() << " material definitions. Http status: " << status);
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

	size_t MaterialPersistenceManager::ListIModelsWithMaterialSettings(std::vector<std::string>& iModelIds) const
	{
		return GetImpl().ListIModelsWithMaterialSettings(iModelIds);
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


	/*** iModel offset and geo-location ***/

	constexpr uint64_t IMODEL_OFFSET_POS_MATID = static_cast<uint64_t>(-1981);
	constexpr uint64_t IMODEL_OFFSET_ROT_MATID = static_cast<uint64_t>(-1982);
	constexpr uint64_t ISCENE_GEOLOC_MATID = static_cast<uint64_t>(-1983);

	inline void EncodeDVec3InMaterial(ITwinMaterial& outMaterial, std::array<double, 3> const& vec3)
	{
		outMaterial.SetChannelIntensity(EChannelType::Roughness, vec3[0]);
		outMaterial.SetChannelIntensity(EChannelType::Metallic, vec3[1]);
		outMaterial.SetChannelIntensity(EChannelType::Transparency, vec3[2]);
	}

	inline bool DecodeDVec3FromMaterial(std::array<double, 3>& vec3, ITwinMaterial const& inMaterial)
	{
		auto const val0 = inMaterial.GetChannelIntensityOpt(EChannelType::Roughness);
		auto const val1 = inMaterial.GetChannelIntensityOpt(EChannelType::Metallic);
		auto const val2 = inMaterial.GetChannelIntensityOpt(EChannelType::Transparency);
		vec3 = {
			val0.value_or(0.),
			val1.value_or(0.),
			val2.value_or(1.) /* because default opacity is 0 on the server... */
		};
		return true;
	}

	void MaterialPersistenceManager::EnableOffsetAndGeoLocation(bool bEnableOffsetAndGeoLoc)
	{
		GetImpl().EnableOffsetAndGeoLocation(bEnableOffsetAndGeoLoc);
	}

	void MaterialPersistenceManager::SetModelOffset(std::string const& iModelId,
		std::array<double, 3> const& posOffset, std::array<double, 3> const& rotOffset)
	{
		if (!GetImpl().IsEnablingOffsetAndGeoLocation())
		{
			return; // currently disabled (Presentations...)
		}

		// Quick & dirty solution for the YII: use 2 materials to store those values
		{
			ITwinMaterial posOff_Material;
			EncodeDVec3InMaterial(posOff_Material, posOffset);
			SetMaterialSettings(iModelId, IMODEL_OFFSET_POS_MATID, posOff_Material);
		}
		{
			ITwinMaterial rotOff_Material;
			EncodeDVec3InMaterial(rotOff_Material, rotOffset);
			SetMaterialSettings(iModelId, IMODEL_OFFSET_ROT_MATID, rotOff_Material);
		}
	}

	bool MaterialPersistenceManager::GetModelOffset(std::string const& iModelId,
		std::array<double, 3>& posOffset, std::array<double, 3>& rotOffset) const
	{
		if (!GetImpl().IsEnablingOffsetAndGeoLocation())
		{
			return false; // currently disabled (Presentations...)
		}
		ITwinMaterial posOff_Material, rotOff_Material;
		if (	!GetMaterialSettings(iModelId, IMODEL_OFFSET_POS_MATID, posOff_Material)
			||	!GetMaterialSettings(iModelId, IMODEL_OFFSET_ROT_MATID, rotOff_Material))
		{
			return false;
		}
		return DecodeDVec3FromMaterial(posOffset, posOff_Material)
			&& DecodeDVec3FromMaterial(rotOffset, rotOff_Material);
	}

	void MaterialPersistenceManager::SetSceneGeoLocation(std::string const& iModelId,
		std::array<double, 3> const& latLongHeight)
	{
		if (!GetImpl().IsEnablingOffsetAndGeoLocation())
		{
			return; // currently disabled (Presentations...)
		}
		// Quick & dirty solution for the YII: use a special material to store those values
		{
			ITwinMaterial geoloc_Material;
			EncodeDVec3InMaterial(geoloc_Material, latLongHeight);
			SetMaterialSettings(iModelId, ISCENE_GEOLOC_MATID, geoloc_Material);
		}
	}

	bool MaterialPersistenceManager::GetSceneGeoLocation(std::string const& iModelId, std::array<double, 3>& latLongHeight) const
	{
		if (!GetImpl().IsEnablingOffsetAndGeoLocation())
		{
			return false; // currently disabled (Presentations...)
		}
		ITwinMaterial geoloc_Material;
		if (!GetMaterialSettings(iModelId, ISCENE_GEOLOC_MATID, geoloc_Material))
		{
			return false;
		}
		return DecodeDVec3FromMaterial(latLongHeight, geoloc_Material);
	}


}
