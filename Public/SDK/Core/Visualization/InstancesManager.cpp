/*--------------------------------------------------------------------------------------+
|
|     $Source: InstancesManager.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "InstancesManager.h"
#include "Core/Network/Network.h"
#include "Config.h"
#include "InstancesGroup.h"

namespace SDK::Core
{
	class InstancesManager::Impl
	{
	public:
		std::shared_ptr<Http> http_;
		SharedInstVect instances_;
		SharedInstGroupVect instancesGroups_;
		SharedInstGroupMap mapIdToInstGroups_;
		std::map<std::string, SharedInstVect> mapObjectRefToInstances_;
		std::map<std::string, SharedInstVect> mapObjectRefToDeletedInstances_;

		std::shared_ptr<Http>& GetHttp() { return http_; }
		void SetHttp(const std::shared_ptr<Http>& http) { http_ = http; }

		// Some structures used in I/O functions
		struct SJsonLink
		{
			std::optional<std::string> prev;
			std::optional<std::string> self;
			std::optional<std::string> next;
		};

#define INSTANCE_STRUCT_MEMBERS \
			std::string name;\
			double matrix[12];\
			std::optional<std::string> colorshift;\
			std::string groupid;\
			std::string objref;

		struct SJsonInst
		{
			INSTANCE_STRUCT_MEMBERS
		};

		struct SJsonInstWithId
		{
			INSTANCE_STRUCT_MEMBERS
			std::string id;
		};

		struct SJsonInEmpty	{};

		void LoadInstancesGroups(const std::string& decorationId)
		{
			SJsonInEmpty jIn;

			struct SJsonInstGroupWithId
			{
				std::string name;
				std::optional<std::string> userData;
				std::string id;
			};

			struct SJsonOut
			{ 
				int total_rows;
				std::vector<SJsonInstGroupWithId> rows;
				SJsonLink _links;
			};

			SJsonOut jOut;

			long status = GetHttp()->GetJsonJBody(
				jOut, std::string("instancesgroup?decorationid=") + decorationId, jIn);

			if (status == 200 || status == 201)
			{
				for (auto& row : jOut.rows)
				{
					InstancesGroup* group = new InstancesGroup();
					group->SetId(row.id);
					group->SetName(row.name);

					std::shared_ptr<InstancesGroup> sharedGroup(group);
					instancesGroups_.push_back(sharedGroup);
					mapIdToInstGroups_[row.id] = sharedGroup;
				}
			}
			else
			{
				throw std::string("Load instances groups failed. http status:" + std::to_string(status));
			}
		}

		void LoadInstances(const std::string& decorationId)
		{
			SJsonInEmpty jIn;
			struct SJsonOut { int total_rows; std::vector<SJsonInstWithId> rows; SJsonLink _links; };
			SJsonOut jOut;

			long status = GetHttp()->GetJsonJBody(
				jOut, std::string("instances?decorationid=") + decorationId, jIn);
			bool continueLoading = true;

			while (continueLoading)
			{
				if (status != 200 && status != 201)
				{
					continueLoading = false;
					throw std::string("Load instances failed. http status:" + std::to_string(status));
				}

				crtmath::dmat4x3 mat;
				for (auto& row : jOut.rows)
				{
					Instance* inst = new Instance();
					inst->SetId(row.id);
					inst->SetName(row.name);
					inst->SetObjectRef(row.objref);
					if (row.colorshift.has_value())
					{
						inst->SetColorShift(row.colorshift.value());
					}
					std::memcpy(&mat[0], row.matrix, 12*sizeof(double));
					inst->SetMatrix(mat);

					SharedInstGroupMap::const_iterator itGroup = mapIdToInstGroups_.find(row.groupid);
					if (itGroup != mapIdToInstGroups_.end())
					{
						inst->SetGroup(itGroup->second);
					}

					std::shared_ptr<Instance> sharedInst(inst);
					instances_.push_back(sharedInst);
					mapObjectRefToInstances_[row.objref].push_back(sharedInst);
				}

				jOut.rows.clear();

				if (jOut._links.next.has_value() && !jOut._links.next.value().empty())
				{
					status = GetHttp()->GetJsonJBody(jOut, jOut._links.next.value(), jIn, {}, true);
				}
				else
				{
					continueLoading = false;
				}
			}
		}

		void LoadDataFromServer(const std::string& decorationId)
		{
			LoadInstancesGroups(decorationId);
			LoadInstances(decorationId);
		}

		void SaveInstancesGroup(const std::string& decorationId, std::shared_ptr<IInstancesGroup>& instGroup)
		{
			struct SJsonInstGroup {	std::string name; std::optional<std::string> userData; };
			SJsonInstGroup jIn{ .name = instGroup->GetName() };
			struct SJsonOut { std::string id; };
			SJsonOut jOut;

			long status = GetHttp()->PostJsonJBody(
				jOut, std::string("instancesgroup?decorationid=") + decorationId, jIn);

			if (status == 200 || status == 201)
			{
				instGroup->SetId(jOut.id);
			}
			else
			{
				throw std::string("Save instances group failed. http status:" + std::to_string(status));
			}
		}

		template<class T>
		void CopyInstance(T& dst, const SDK::Core::IInstance& src)
		{
			dst.name = src.GetName();
			std::memcpy(dst.matrix, &src.GetMatrix()[0], 12*sizeof(double));
			dst.colorshift = src.GetColorShift();
			dst.objref = src.GetObjectRef();
			if (src.GetGroup())
			{
				dst.groupid = src.GetGroup()->GetId();
			}
		}

		void SaveInstances(const std::string& decorationId, SharedInstVect& instances)
		{
			struct SJsonInstVect { std::vector<SJsonInst> instances; };
			struct SJsonInstWithIdVect { std::vector<SJsonInstWithId> instances; };
			SJsonInstVect jInPost;
			SJsonInstWithIdVect jInPut;

			int64_t instCount = 0;
			std::vector<int64_t> newInstIndices;
			std::vector<int64_t> updatedInstIndices;

			// Sort instances for requests (addition/update)
			for(auto& inst : instances)
			{
				if (inst->GetId().empty())
				{
					SJsonInst jInst;
					CopyInstance<SJsonInst>(jInst, *inst);
					jInPost.instances.push_back(jInst);
					newInstIndices.push_back(instCount);

				}
				else if (inst->IsMarkedForUpdate(Database))
				{
					SJsonInstWithId jInstwId;
					CopyInstance<SJsonInstWithId>(jInstwId, *inst);
					jInstwId.id = inst->GetId();
					jInPut.instances.push_back(jInstwId);
					updatedInstIndices.push_back(instCount);
				}
				++instCount;
			}

			// Post (new instances)
			if (!jInPost.instances.empty())
			{
				struct SJsonInstOut { std::string name; std::string id; };
				struct SJsonInstOutVect	{ std::vector<SJsonInstOut> instances; };
				SJsonInstOutVect jOutPost;
				long status = GetHttp()->PostJsonJBody(
					jOutPost, std::string("instances?decorationid=") + decorationId, jInPost);

				if (status == 200 || status == 201)
				{
					if (newInstIndices.size() == jOutPost.instances.size())
					{
						for(size_t i = 0; i < newInstIndices.size(); ++i)
						{
							SDK::Core::IInstance& inst = *instances[newInstIndices[i]];
							inst.SetId(jOutPost.instances[i].id);
							inst.MarkForUpdate(Database, false);
						}
					}
				}
				else
				{
					throw std::string("Saving new instances failed. Http status:" + std::to_string(status));
				}
			}

			// Put (updated instances)
			if (!jInPut.instances.empty())
			{
				struct SJsonInstOutUpd { int64_t numUpdated; };
				SJsonInstOutUpd jOutPut;
				long status = GetHttp()->PutJsonJBody(
					jOutPut, std::string("instances?decorationid=") + decorationId, jInPut);

				if (status == 200 || status == 201)
				{
					if (updatedInstIndices.size() == static_cast<size_t>(jOutPut.numUpdated))
					{
						for(size_t i = 0; i < updatedInstIndices.size(); ++i)
						{
							instances[updatedInstIndices[i]]->MarkForUpdate(Database, false);
						}
					}
				}
				else
				{
					throw std::string("Saving new instances failed. Http status:" + std::to_string(status));
				}
			}
		}

		void DeleteInstances(const std::string& decorationId, SharedInstVect& instances)
		{
			if (!instances.empty())
			{
				struct JSonIn { std::vector<std::string> ids; } jIn;
				struct JSonOut {} jOut;

				jIn.ids.resize(instances.size());
				for (size_t i = 0; i < instances.size(); ++i)
				{
					jIn.ids[i] = instances[i]->GetId();
				}

				long status = GetHttp()->DeleteJsonJBody(
					jOut, std::string("instances?decorationid=") + decorationId, jIn);

				if (status != 200 && status != 201)
				{
					throw std::string("Deleting instances failed. Http status:" + std::to_string(status));
				}

				instances.clear();
			}
		}

		void SaveDataOnServer(const std::string& decorationId)
		{
			// Save groups
			for (auto& instGroup : instancesGroups_)
			{
				SaveInstancesGroup(decorationId, instGroup);
			}

			// Save instances
			for (auto& instVec : mapObjectRefToInstances_)
			{
				SaveInstances(decorationId, instVec.second);
			}

			// Delete instances
			for (auto& instVec : mapObjectRefToDeletedInstances_)
			{
				DeleteInstances(decorationId, instVec.second);
			}
		}

		uint64_t GetInstanceCountByObjectRef(const std::string& objectRef) const
		{
			const auto it = mapObjectRefToInstances_.find(objectRef);
			if (it != mapObjectRefToInstances_.cend())
			{
				return static_cast<uint64_t>(it->second.size());
			}
			return 0;
		}
		
		void SetInstanceCountByObjectRef(const std::string& objectRef, uint64_t count)
		{
			SharedInstVect& currentInstances = mapObjectRefToInstances_[objectRef];
			uint64_t oldSize = currentInstances.size();
			currentInstances.resize(static_cast<size_t>(count));
			for (uint64_t i = oldSize; i < count; ++i)
			{
				currentInstances[i] = SDK::Core::IInstance::New();
			}
		}

		void GetInstancesByObjectRef(
			const std::string& objectRef, SharedInstVect& instances) const
		{
			const auto it = mapObjectRefToInstances_.find(objectRef);
			if (it != mapObjectRefToInstances_.cend())
			{
				instances = it->second;
			}
		}

		void RemoveInstancesByObjectRef(
			const std::string& objectRef, const std::vector<int32_t> indices)
		{
			SharedInstVect& currentInstances = mapObjectRefToInstances_[objectRef];
			SharedInstVect& deletedInstances =  mapObjectRefToDeletedInstances_[objectRef];

			int32_t lastIndex = -1;
			for (auto it = indices.rbegin(); it != indices.rend(); it++)
			{
				if (lastIndex == -1 || *it < lastIndex)
				{
					deletedInstances.push_back(currentInstances[*it]);
					currentInstances.erase(currentInstances.begin() + *it);
				}
				lastIndex = *it;
			}
		}

		const SharedInstVect& GetInstances() const
		{
			return instances_;
		}

		std::vector<std::string> GetObjectReferences() const
		{
			std::vector<std::string> refs;
			for (const auto& it : mapObjectRefToInstances_)
			{
				refs.push_back(it.first);
			}
			return refs;
		}

		void AddInstancesGroup(const std::shared_ptr<IInstancesGroup>& instancesGroup)
		{
			instancesGroups_.push_back(instancesGroup);
		}

		const SharedInstGroupVect& GetInstancesGroups() const
		{
			return instancesGroups_;
		}
	};

	void InstancesManager::LoadDataFromServer(const std::string& decorationId)
	{
		GetImpl().LoadDataFromServer(decorationId);
	}

	void InstancesManager::SaveDataOnServer(const std::string& decorationId)
	{
		GetImpl().SaveDataOnServer(decorationId);
	}

	uint64_t InstancesManager::GetInstanceCountByObjectRef(const std::string& objectRef) const
	{
		return GetImpl().GetInstanceCountByObjectRef(objectRef);
	}

	void InstancesManager::SetInstanceCountByObjectRef(const std::string& objectRef, uint64_t count)
	{
		GetImpl().SetInstanceCountByObjectRef(objectRef, count);
	}

	void InstancesManager::GetInstancesByObjectRef(const std::string& objectRef, SharedInstVect& instances) const
	{
		GetImpl().GetInstancesByObjectRef(objectRef, instances);
	}

	void InstancesManager::RemoveInstancesByObjectRef(const std::string& objectRef, const std::vector<int32_t> indices)
	{
		GetImpl().RemoveInstancesByObjectRef(objectRef, indices);
	}

	const SharedInstVect& InstancesManager::GetInstances() const
	{
		return GetImpl().GetInstances();
	}

	std::vector<std::string> InstancesManager::GetObjectReferences() const
	{
		return GetImpl().GetObjectReferences();
	}

	void InstancesManager::AddInstancesGroup(const std::shared_ptr<IInstancesGroup>& instancesGroup)
	{
		GetImpl().AddInstancesGroup(instancesGroup);
	}

	const SharedInstGroupVect& InstancesManager::GetInstancesGroups() const
	{
		return GetImpl().GetInstancesGroups();
	}

	void InstancesManager::SetHttp(std::shared_ptr<Http> http)
	{
		GetImpl().SetHttp(http);
	}

	InstancesManager::InstancesManager():impl_(new Impl())
	{
		GetImpl().SetHttp(GetDefaultHttp());
	}

	InstancesManager::~InstancesManager() 
	{}

	InstancesManager::Impl& InstancesManager::GetImpl()
	{
		return *impl_;
	}

	const InstancesManager::Impl& InstancesManager::GetImpl() const
	{
		return *impl_;
	}

	template<>
	std::function<std::shared_ptr<IInstancesManager>()> Tools::Factory<IInstancesManager>::newFct_ = []() {
		std::shared_ptr<IInstancesManager> p(static_cast<IInstancesManager*>(new InstancesManager()));
		return p;
		};
}