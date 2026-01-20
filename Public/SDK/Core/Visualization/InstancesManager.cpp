/*--------------------------------------------------------------------------------------+
|
|     $Source: InstancesManager.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "InstancesManager.h"

#include "Core/Network/HttpGetWithLink.h"
#include "Config.h"
#include "InstancesGroup.h"
#include "SplinesManager.h"
#include "PathAnimation.h"
#include "../Singleton/singleton.h"

namespace AdvViz::SDK
{
	class InstancesManager::Impl
	{
	public:
		std::shared_ptr<Http> http_;
		SharedInstGroupVect instancesGroups_;
		SharedInstGroupMap mapIdToInstGroups_;
		RefID::DBToIDMap groupIDMap_;
		std::map<std::pair<std::string /*objRef*/, RefID/*group*/>, SharedInstVect> mapObjectRefToInstances_;
		std::map<std::pair<std::string /*objRef*/, RefID/*group*/>, SharedInstVect> mapObjectRefToDeletedInstances_;
		std::vector<IInstancesGroupPtr> instancesGroupsToDelete_;
		std::shared_ptr<ISplinesManager> splineManager_;
		std::shared_ptr<IPathAnimator> animPathManager_;

		std::shared_ptr<Http>& GetHttp() { return http_; }
		void SetHttp(const std::shared_ptr<Http>& http) { http_ = http; }

		void SetSplineManager(std::shared_ptr<ISplinesManager> const& splineManager)
		{
			splineManager_ = splineManager;
		}

		void SetAnimPathManager(std::shared_ptr<IPathAnimator> const& animPathManager)
		{
			animPathManager_ = animPathManager;
		}

		void Clear()
		{
			instancesGroups_.clear();
			mapIdToInstGroups_.clear();
			mapObjectRefToInstances_.clear();
			mapObjectRefToDeletedInstances_.clear();
			groupIDMap_.clear();
		}

		// Some structures used in I/O functions

#define INSTANCE_STRUCT_MEMBERS \
			std::string name;\
			std::array<double, 12> matrix;\
			std::optional<std::string> colorshift;\
			std::optional<std::string> groupid;\
			std::optional<std::string> animationid;\
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

		void LoadInstancesGroups(const std::string& decorationId, const IInstancesGroupPtr& defaultGroup = {})
		{
			struct SJsonInstGroupWithId
			{
				std::string name;
				std::optional<std::string> userData;
				std::optional<std::string> type;
				std::string id;
			};

			if (defaultGroup)
			{
				// For compatibility with older version, always associate the empty DB id with the default
				// group.
				groupIDMap_[""] = defaultGroup->GetId().ID();
			}

			bool hasAddedDefaultGroup = false;

			auto ret = HttpGetWithLink<SJsonInstGroupWithId>(GetHttp(),
				"decorations/" + decorationId + "/instancesgroups",
				{} /* extra headers*/,
				[this, &hasAddedDefaultGroup, &defaultGroup](SJsonInstGroupWithId const& row) -> expected<void, std::string>
			{
				IInstancesGroupPtr group;
				bool const isDefaultGroup = defaultGroup && defaultGroup->GetName() == row.name;
				if (isDefaultGroup)
				{
					group = defaultGroup;
					// Do not change the internal ID of the default group, only update its DB identifier
					RefID groupId = group->GetId();
					groupId.SetDBIdentifier(row.id);
					group->SetId(groupId);
					groupIDMap_.emplace(row.id, groupId.ID());
				}
				else
				{
					group.reset(IInstancesGroup::New());
					group->SetId(RefID::FromDBIdentifier(row.id, groupIDMap_));
				}
				group->SetName(row.name);
				if (row.type.has_value())
					group->SetType(row.type.value());

				// We may have saved linked spline as userData. Spline should be loaded *before* populations
				// to make it work.
				if (row.userData
					&& group->GetType() == "spline"
					&& splineManager_)
				{
					RefID const splineId = splineManager_->GetLoadedSplineId(*row.userData);
					if (splineId.IsValid())
					{
						group->SetLinkedSplineId(splineId);
					}
				}

				AddInstancesGroup(group);
				if (isDefaultGroup)
					hasAddedDefaultGroup = true;
				return {};
			});

			// If we have provided a default group, and did not parse anything here, make sure the instances,
			// if any, will all be assigned this default group.
			if (defaultGroup && !hasAddedDefaultGroup)
			{
				AddInstancesGroup(defaultGroup);
			}

			if (!ret)
			{
				BE_LOGW("ITwinDecoration", "Load instances groups failed. " << ret.error());
			}
		}

		void LoadInstances(const std::string& decorationId)
		{
			dmat3x4 mat;
			auto ret = HttpGetWithLink<SJsonInstWithId>(GetHttp(),
				"decorations/" + decorationId + "/instances",
				{} /* extra headers*/,
				[this, &mat](SJsonInstWithId const& row) -> expected<void, std::string>
				{
					IInstance* inst = IInstance::New();
					std::shared_ptr<IInstance> sharedInst(inst);

					inst->SetId(row.id);
					inst->SetName(row.name);
					if (row.animationid.has_value())
					{
						inst->SetAnimId(row.animationid.value());
						if (auto animPathInfo = animPathManager_->FindAnimationPathInfoByDBId(row.animationid.value()))
						{
							inst->SetAnimPathId(animPathInfo->GetId());
						}
					}

					inst->SetObjectRef(row.objref);
					if (row.colorshift.has_value() && row.colorshift.value() != "")
					{
						int r, g, b;
						const char* str = row.colorshift.value().data();
						if (str[0] == '#') 
							str++;
						if (sscanf_s(str, "%02x%02x%02x", &r, &g, &b) == 3)
						{
							float3 color = { r / 255.f, g / 255.f, b / 255.f };
							inst->SetColorShift(color);
						}
					}
					std::memcpy(&mat[0], row.matrix.data(), 12*sizeof(double));
					inst->SetTransform(mat);

					std::string const groupid = row.groupid.value_or("");
					RefID const gpId = RefID::FromDBIdentifier(groupid, groupIDMap_);
					auto itGroup = mapIdToInstGroups_.find(gpId);
					if (itGroup != mapIdToInstGroups_.end())
					{
						inst->SetGroup(itGroup->second);
						itGroup->second->AddInstance(sharedInst);
					}

					mapObjectRefToInstances_[std::make_pair(row.objref, gpId)].push_back(sharedInst);
					return {};
				}
			);
			if (!ret)
			{
				BE_LOGW("ITwinDecoration", "Load instances failed. " << ret.error());
			}
		}

		void LoadDataFromServer(const std::string& decorationId, const IInstancesGroupPtr& defaultGroup = {})
		{
			Clear();
			LoadInstancesGroups(decorationId, defaultGroup);
			LoadInstances(decorationId);
		}

		void SaveInstancesGroup(
			const std::string& decorationId,
			IInstancesGroupPtr& instGroup)
		{
			if (instGroup->GetId().HasDBIdentifier())
			{
				return; // skip groups already present on the server
			}

			struct SJsonInstGroup {	
				std::string name;
				std::optional<std::string> type;
				std::optional<std::string> userData; };
			SJsonInstGroup jIn{ .name = instGroup->GetName() };
			if (instGroup->GetType() != "")
				jIn.type = instGroup->GetType();

			// Save linked spline as userData. Spline should be saved *before* populations to guarantee that
			// we have retrieved the spline identifier on the server.
			if (instGroup->GetLinkedSplineId() && splineManager_)
			{
				auto const linkedSpline = splineManager_->GetSplineById(*instGroup->GetLinkedSplineId());
				if (linkedSpline && linkedSpline->GetId().HasDBIdentifier())
				{
					jIn.userData = linkedSpline->GetId().GetDBIdentifier();
				}
			}

			struct SJsonOut { std::string id; };
			SJsonOut jOut;

			long status = GetHttp()->PostJsonJBody(
				jOut, "decorations/" + decorationId + "/instancesgroups", jIn);

			if (status == 200 || status == 201)
			{
				// A new group has been created on the server. Update the identifier internally (but keep
				// the 'session' ID unchanged)
				RefID groupId = instGroup->GetId();
				groupId.SetDBIdentifier(jOut.id);
				instGroup->SetId(groupId);
				mapIdToInstGroups_.insert(std::make_pair(groupId, instGroup));
			}
			else
			{
				BE_LOGW("ITwinDecoration", "Save instances group failed. Http status: " << status);
			}
		}

		template<class T>
		void CopyInstance(T& dst, const IInstance& src)
		{
			dst.name = src.GetName();
			std::memcpy(dst.matrix.data(), &src.GetTransform()[0], 12*sizeof(double));
			if (src.GetColorShift().has_value())
			{
				float3 color = src.GetColorShift().value();
				int r = int(color[0] * 255.f);
				int g = int(color[1] * 255.f);
				int b = int(color[2] * 255.f);
				char str[16];
				snprintf(str, sizeof(str), "#%02x%02x%02x", r, g, b);
				dst.colorshift = str;
			}
			dst.objref = src.GetObjectRef();
			if (src.GetGroup())
			{
				BE_ASSERT(src.GetGroup()->GetId().HasDBIdentifier(), "groups should be saved before instances");
				dst.groupid = src.GetGroup()->GetId().GetDBIdentifier();
			}
			auto& animId = src.GetAnimId();
			if (animId != "")
			{
				dst.animationid = animId;
			}
		}

		void SaveInstances(
			const std::string& decorationId,
			SharedInstVect& instances)
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
				if (inst->GetAnimPathId())
				{
					// update animation path database id within the instance
					auto animPathRefId = animPathManager_->GetAnimationPathInfo(*(inst->GetAnimPathId()))->GetId();
					BE_ASSERT(animPathRefId.HasDBIdentifier(), "animation paths should be saved before instances");
					inst->SetAnimId(animPathRefId.GetDBIdentifier());
					inst->SetAnimPathId(animPathRefId);
				}

				if (inst->GetId().empty())
				{
					SJsonInst jInst;
					CopyInstance<SJsonInst>(jInst, *inst);
					jInPost.instances.push_back(jInst);
					newInstIndices.push_back(instCount);

				}
				else if (inst->ShouldSave())
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
					jOutPost, "decorations/" + decorationId + "/instances", jInPost);

				if (status == 200 || status == 201)
				{
					if (newInstIndices.size() == jOutPost.instances.size())
					{
						for(size_t i = 0; i < newInstIndices.size(); ++i)
						{
							IInstance& inst = *instances[newInstIndices[i]];
							// update database id within the instance
							inst.SetId(jOutPost.instances[i].id);
							RefID refId = inst.GetRefId();
							refId.SetDBIdentifier(jOutPost.instances[i].id);
							inst.SetRefId(refId);

							inst.SetShouldSave(false);
						}
					}
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Saving new instances failed. Http status: " << status);
				}
			}

			// Put (updated instances)
			if (!jInPut.instances.empty())
			{
				struct SJsonInstOutUpd { int64_t numUpdated; };
				SJsonInstOutUpd jOutPut;
				long status = GetHttp()->PutJsonJBody(
					jOutPut, "decorations/" + decorationId  + "/instances", jInPut);

				if (status == 200 || status == 201)
				{
					if (updatedInstIndices.size() == static_cast<size_t>(jOutPut.numUpdated))
					{
						for(size_t i = 0; i < updatedInstIndices.size(); ++i)
						{
							instances[updatedInstIndices[i]]->SetShouldSave(false);
						}
					}
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Updating instances failed. Http status: " << status);
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
					jOut, "decorations/" + decorationId + "/instances", jIn);

				if (status != 200 && status != 201)
				{
					BE_LOGW("ITwinDecoration", "Deleting instances failed. Http status: " << status);
				}

				instances.clear();
			}
		}

		void DeleteInstancesGroup(const std::string& decorationId, const IInstancesGroupPtr& instancesGroup)
		{
			if (instancesGroup->GetId().HasDBIdentifier())
			{
				struct JSonIn { std::array<std::string, 1> ids; } jIn;
				struct JSonOut {} jOut;
				jIn.ids[0] = instancesGroup->GetId().GetDBIdentifier();
				long status = GetHttp()->DeleteJsonJBody(jOut, "decorations/" + decorationId + "/instancesgroups", jIn);
				if (status != 200 && status != 201)
					BE_LOGW("ITwinDecoration", "Deleting instancesgroups failed. Http status: " << status);
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

			for (auto& instGroup : instancesGroupsToDelete_)
			{
				DeleteInstancesGroup(decorationId, instGroup);
			}

			// Delete instances
			for (auto& instVec : mapObjectRefToDeletedInstances_)
			{
				DeleteInstances(decorationId, instVec.second);
			}
		}

		uint64_t GetInstanceCountByObjectRef(const std::string& objectRef, const RefID& gpId) const
		{
			if (!gpId.IsValid())
			{
				// Count instances matching given object in *all* groups.
				uint64_t ret = 0;
				for (auto it : mapObjectRefToInstances_)
				{
					if (it.first.first == objectRef)
						ret += it.second.size();
				}
				return ret;
			}
			else
			{
				const auto it = mapObjectRefToInstances_.find(std::make_pair(objectRef, gpId));
				if (it != mapObjectRefToInstances_.cend())
				{
					return static_cast<uint64_t>(it->second.size());
				}
			}
			return 0;
		}
		
		void SetInstanceCountByObjectRef(const std::string& objectRef, const RefID& gpId, uint64_t count)
		{
			SharedInstVect& currentInstances = mapObjectRefToInstances_[std::make_pair(objectRef, gpId)];
			uint64_t oldSize = currentInstances.size();
			currentInstances.resize(static_cast<size_t>(count));
			for (uint64_t i = oldSize; i < count; ++i)
			{
				currentInstances[i].reset(IInstance::New());
			}
		}

		std::shared_ptr<IInstance> AddInstance(const std::string& objectRef, const RefID& gpId)
		{
			SharedInstVect& currentInstances = mapObjectRefToInstances_[std::make_pair(objectRef, gpId)];
			auto inst = std::shared_ptr<IInstance>(IInstance::New());
			currentInstances.push_back(inst);
			SharedInstGroupMap::const_iterator itGroup = mapIdToInstGroups_.find(gpId);
			if (itGroup != mapIdToInstGroups_.end())
			{
				itGroup->second->AddInstance(inst);
				inst->SetGroup(itGroup->second);
			}
			return inst;
		}

		const SharedInstVect& GetInstancesByObjectRef(const std::string& objectRef, const RefID& gpId) const
		{
			const auto it = mapObjectRefToInstances_.find(std::make_pair(objectRef, gpId));
			if (it != mapObjectRefToInstances_.cend())
			{
				return it->second;
			}
			static SharedInstVect empty;
			return empty;
		}

		void RemoveInstancesByObjectRef(
			const std::string& objectRef, const RefID& gpId, const std::vector<int32_t> indicesInDescendingOrder)
		{
			auto pair = std::make_pair(objectRef, gpId);
			SharedInstVect& currentInstances = mapObjectRefToInstances_[pair];
			SharedInstVect& deletedInstances =  mapObjectRefToDeletedInstances_[pair];

			for (auto const& index : indicesInDescendingOrder)
			{
				if (index < currentInstances.size())
				{
					deletedInstances.push_back(currentInstances[index]);
					currentInstances.erase(currentInstances.begin() + index);
				}
			}
		}

		void RemoveGroupInstances(const RefID& gpId)
		{
			for (auto it = mapObjectRefToInstances_.begin(); it != mapObjectRefToInstances_.end(); ++it)
			{
				if (it->first.second == gpId)
				{
					SharedInstVect& currentInstances = mapObjectRefToInstances_[it->first];
					SharedInstVect& deletedInstances = mapObjectRefToDeletedInstances_[it->first];
					for (auto& inst : currentInstances)
						deletedInstances.push_back(inst);
					currentInstances.clear();
					it = mapObjectRefToInstances_.erase(it);
				}
			}
		}

		void OnInstancesRestored(const std::string& objectRef, const RefID& gpId, const std::vector<RefID>& restoredInstances)
		{
			auto pair = std::make_pair(objectRef, gpId);
			[[maybe_unused]] SharedInstVect const& currentInstances = mapObjectRefToInstances_[pair];
			SharedInstVect& deletedInstances = mapObjectRefToDeletedInstances_[pair];

			for (RefID const& refID : restoredInstances)
			{
				BE_ASSERT(std::find_if(currentInstances.begin(), currentInstances.end(),
						[&refID](const IInstancePtr& p) { return p->GetRefId() == refID; })
						!= currentInstances.end(), "restored instance not found!");
				std::erase_if(deletedInstances,
					[&refID](const IInstancePtr& p) { return p->GetRefId() == refID; });
			}
		}

		bool HasInstances() const
		{
			for (const auto& it : mapObjectRefToInstances_)
			{
				if (!it.second.empty())
				{
					return true;
				}
			}
			return false;
		}

		bool HasInstancesToSave() const
		{
			for (const auto& it : mapObjectRefToInstances_)
			{
				for (const auto& inst : it.second)
				{
					if (inst->GetId().empty() ||
						inst->ShouldSave())
					{
						return true;
					}
				}
			}
			for (const auto& it : mapObjectRefToDeletedInstances_)
			{
				if (!it.second.empty())
				{
					return true;
				}
			}
			return false;
		}

		std::vector<std::pair<std::string, RefID>> GetObjectReferences() const
		{
			std::vector<std::pair<std::string, RefID>> refs;
			refs.reserve(mapObjectRefToInstances_.size());
			for (const auto& it : mapObjectRefToInstances_)
			{
				refs.push_back(it.first);
			}
			return refs;
		}

		void AddInstancesGroup(const IInstancesGroupPtr& group)
		{
			if (group)
			{
				instancesGroups_.push_back(group);
				mapIdToInstGroups_[group->GetId()] = group;
			}
		}

		void RemoveInstancesGroup(const IInstancesGroupPtr& group)
		{
			if (group)
			{
				instancesGroupsToDelete_.push_back(group);
				std::erase(instancesGroups_, group);
				mapIdToInstGroups_.erase(group->GetId());
			}
		}

		const SharedInstGroupVect& GetInstancesGroups() const
		{
			return instancesGroups_;
		}

		IInstancesGroupPtr GetInstancesGroup(const RefID& gpId) const
		{
			auto it = mapIdToInstGroups_.find(gpId);
			if (it != mapIdToInstGroups_.end())
				return it->second;
			return {};
		}


		IInstancesGroupPtr GetInstancesGroupByName(const std::string& name) const
		{
			//TODO: optimize with map
			auto it = std::find_if(instancesGroups_.begin(), instancesGroups_.end(),
				[name](const IInstancesGroupPtr& p) {
				return p->GetName() == name;
			});
			if (it != instancesGroups_.end())
				return *it;
			return {};
		}

		IInstancesGroupPtr GetInstancesGroupBySplineID(const RefID& splineId) const
		{
			RefID const nullRef = RefID::Invalid();
			auto it = std::find_if(instancesGroups_.begin(), instancesGroups_.end(),
				[splineId, &nullRef](const IInstancesGroupPtr& p) {
				return p->GetLinkedSplineId().value_or(nullRef) == splineId;
			});
			if (it != instancesGroups_.end())
				return *it;
			return {};
		}
	};

	void InstancesManager::LoadDataFromServer(const std::string& decorationId, const IInstancesGroupPtr& defaultGroup /*= {}*/)
	{
		GetImpl().LoadDataFromServer(decorationId, defaultGroup);
	}

	void InstancesManager::SaveDataOnServer(const std::string& decorationId)
	{
		GetImpl().SaveDataOnServer(decorationId);
	}

	uint64_t InstancesManager::GetInstanceCountByObjectRef(const std::string& objectRef, const RefID& gpId) const
	{
		return GetImpl().GetInstanceCountByObjectRef(objectRef, gpId);
	}

	void InstancesManager::SetInstanceCountByObjectRef(const std::string& objectRef, const RefID& gpId, uint64_t count)
	{
		GetImpl().SetInstanceCountByObjectRef(objectRef, gpId, count);
	}

	std::shared_ptr<IInstance> InstancesManager::AddInstance(const std::string& objectRef, const RefID& gpId)
	{
		return GetImpl().AddInstance(objectRef, gpId);
	}

	const SharedInstVect& InstancesManager::GetInstancesByObjectRef(const std::string& objectRef, const RefID& gpId) const
	{
		return GetImpl().GetInstancesByObjectRef(objectRef, gpId);
	}

	void InstancesManager::RemoveInstancesByObjectRef(const std::string& objectRef, const RefID& gpId, const std::vector<int32_t> indicesInDescendingOrder)
	{
		GetImpl().RemoveInstancesByObjectRef(objectRef, gpId, indicesInDescendingOrder);
	}

	void InstancesManager::RemoveGroupInstances(const RefID& gpId)
	{
		GetImpl().RemoveGroupInstances(gpId);
	}

	void InstancesManager::OnInstancesRestored(const std::string& objectRef, const RefID& gpId, const std::vector<RefID>& restoredInstances)
	{
		GetImpl().OnInstancesRestored(objectRef, gpId, restoredInstances);
	}

	bool InstancesManager::HasInstances() const
	{
		return GetImpl().HasInstances();
	}

	bool InstancesManager::HasInstancesToSave() const
	{
		return GetImpl().HasInstancesToSave();
	}

	std::vector<std::pair<std::string/*objRef*/, RefID /*gpId*/>> InstancesManager::GetObjectReferences() const
	{
		return GetImpl().GetObjectReferences();
	}

	void InstancesManager::AddInstancesGroup(const IInstancesGroupPtr& group)
	{
		GetImpl().AddInstancesGroup(group);
	}

	void InstancesManager::RemoveInstancesGroup(const IInstancesGroupPtr& group)
	{
		GetImpl().RemoveInstancesGroup(group);
	}

	const SharedInstGroupVect& InstancesManager::GetInstancesGroups() const
	{
		return GetImpl().GetInstancesGroups();
	}

	IInstancesGroupPtr InstancesManager::GetInstancesGroup(const RefID& gpId) const
	{
		return GetImpl().GetInstancesGroup(gpId);
	}

	IInstancesGroupPtr InstancesManager::GetInstancesGroupByName(const std::string& name) const
	{
		return GetImpl().GetInstancesGroupByName(name);
	}

	IInstancesGroupPtr InstancesManager::GetInstancesGroupBySplineID(const RefID& splineId) const
	{
		return GetImpl().GetInstancesGroupBySplineID(splineId);
	}

	void InstancesManager::SetSplineManager(std::shared_ptr<ISplinesManager> const& splineManager)
	{
		GetImpl().SetSplineManager(splineManager);
	}

	void InstancesManager::SetAnimPathManager(std::shared_ptr<IPathAnimator> const& animPathManager)
	{
		GetImpl().SetAnimPathManager(animPathManager);
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
	Tools::Factory<IInstancesManager>::Globals::Globals()
	{
		newFct_ = []() {return static_cast<IInstancesManager*>(new InstancesManager());};
	}

	template<>
	Tools::Factory<IInstancesManager>::Globals& Tools::Factory<IInstancesManager>::GetGlobals()
	{
		return singleton<Tools::Factory<IInstancesManager>::Globals>();
	}

}