/*--------------------------------------------------------------------------------------+
|
|     $Source: InstancesManager.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "InstancesManager.h"

#include "AsyncHelpers.h"
#include "AsyncHttp.inl"
#include "Core/Network/HttpGetWithLink.h"
#include "Config.h"
#include "InstancesGroup.h"
#include "SplinesManager.h"
#include "PathAnimation.h"
#include "../Singleton/singleton.h"
#include <optional>

namespace AdvViz::SDK
{
	class InstancesManager::Impl : public std::enable_shared_from_this<Impl>
	{

	public:
		std::shared_ptr<Http> http_;
		std::shared_ptr<ISplinesManager> splineManager_;
		std::shared_ptr<IPathAnimator> animPathManager_;

		std::shared_ptr< std::atomic_bool > isThisValid_;
		using ObjRefAndGPId = std::pair<std::string /*objRef*/, RefID/*group*/>;

		struct SThreadSafeData
		{
			SharedInstGroupNameMap instanceGroupsByName_;
			SharedInstGroupMap mapIdToInstGroups_;
			RefID::DBToIDMap groupIDMap_;
			RefID::DBToIDMap instanceIDMap_;
			std::map<ObjRefAndGPId, SharedInstVect> mapObjectRefToInstances_;
			std::map<ObjRefAndGPId, SharedInstVect> mapObjectRefToDeletedInstances_;
			std::vector<IInstancesGroupPtr> instancesGroupsToDelete_;
		};
		Tools::RWLockableObject<SThreadSafeData> thdata_;


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
			auto thdata = thdata_.GetAutoLock();
			thdata->instanceGroupsByName_.clear();
			thdata->mapIdToInstGroups_.clear();
			thdata->mapObjectRefToInstances_.clear();
			thdata->mapObjectRefToDeletedInstances_.clear();
			thdata->groupIDMap_.clear();
			thdata->instanceIDMap_.clear();
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


		IInstancesGroupPtr FindDefaultGroup() const
		{
			auto thdata = thdata_.GetAutoLock();
			if (thdata->mapIdToInstGroups_.empty())
			{
				return {};
			}
			// First try to find it by name.
			for (auto const& [id, groupPtr] : thdata->mapIdToInstGroups_)
			{
				auto gp = groupPtr->GetAutoLock();
				if (gp->GetName() == DEFAULT_GROUP_NAME)
					return groupPtr;
			}
			// For compatibility with older version, if not found by name, try to find it by the default DB
			// id (empty string).
			for (auto const& [id, groupPtr] : thdata->mapIdToInstGroups_)
			{
				if (!id.HasDBIdentifier())
					return groupPtr;
			}
			// Last resort: a group with no user data nor type...

			for (auto const& [id, groupPtr] : thdata->mapIdToInstGroups_)
			{
				auto gp = groupPtr->GetAutoLock();
				if (gp->GetType().empty()
					&& gp->GetName().empty()
					&& !gp->GetLinkedSplineId())
				{
					return groupPtr;
				}
			}
			return {};
		}

		void InitDefaultGroupFromLoadedGroups(const std::function<void(IInstancesGroupPtr const&)>& setDefaultGroupfct = {})
		{
			// Determine the default group, and set it through the provided callback (if any).
			IInstancesGroupPtr defaultGroupPtr = FindDefaultGroup();

			if (setDefaultGroupfct)
			{
				setDefaultGroupfct(defaultGroupPtr);

				// Note that the callback may have created a new default group if it was not found, so we
				// need to retrieve it again to be sure to have the right pointer (with the right ID).
				if (!defaultGroupPtr)
				{
					defaultGroupPtr = FindDefaultGroup();
				}
			}

			if (defaultGroupPtr)
			{
				// For compatibility with older version, always associate the empty DB id with the default
				// group (in case some instances were saved without any group ID).
				auto gp = defaultGroupPtr->GetAutoLock();
				auto thdata = thdata_.GetAutoLock();
				thdata->groupIDMap_[""] = gp->GetId().ID();
			}
		}

		void LoadInstancesGroups(const std::string& decorationId,
			const std::function<void(IInstancesGroupPtr const&)>& setDefaultGroupfct = {})
		{
			struct SJsonInstGroupWithId
			{
				std::string name;
				std::optional<std::string> userData;
				std::optional<std::string> type;
				std::string id;
			};

			auto ret = HttpGetWithLink<SJsonInstGroupWithId>(GetHttp(),
				"decorations/" + decorationId + "/instancesgroups",
				{} /* extra headers*/,
				[this](SJsonInstGroupWithId const& row) -> expected<void, std::string>
			{
				IInstancesGroup* group;
				{
					auto thdata = thdata_.GetAutoLock();
					group = IInstancesGroup::New();
					group->SetId(RefID::FromDBIdentifier(row.id, thdata->groupIDMap_));
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

				auto groupPtr = Tools::MakeSharedLockableDataPtr<IInstancesGroup>(group);
				AddInstancesGroup(groupPtr);
				return {};
			});

			if (!ret)
			{
				BE_LOGW("ITwinDecoration", "Load instances groups failed. " << ret.error());
			}

			// Determine the default group, and set it through the provided callback (if any).
			InitDefaultGroupFromLoadedGroups(setDefaultGroupfct);
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
					IInstancePtr sharedInst = MakeSharedLockableDataPtr(inst);

					auto thdata = thdata_.GetAutoLock();

					inst->SetId(RefID::FromDBIdentifier(row.id, thdata->instanceIDMap_));
					inst->SetName(row.name);
					if (row.animationid.has_value())
					{
						inst->SetAnimId(row.animationid.value());
						if (auto animPathInfoPtr = animPathManager_->FindAnimationPathInfoByDBId(row.animationid.value()))
						{
							auto animPathInfo = animPathInfoPtr->GetRAutoLock();
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
					RefID const gpId = RefID::FromDBIdentifier(groupid, thdata->groupIDMap_);
					auto group = GetInstancesGroup(gpId);
					if (group)
					{
						inst->SetGroup(group);
						auto gp = group->GetAutoLock();
						gp->AddInstance(sharedInst);
					}

					thdata->mapObjectRefToInstances_[std::make_pair(row.objref, gpId)].push_back(sharedInst);
					return {};
				}
			);
			if (!ret)
			{
				BE_LOGW("ITwinDecoration", "Load instances failed. " << ret.error());
			}
		}

		void AsyncLoadInstances(const std::string& decorationId,
			std::function<void(IInstancePtr&)> onInstanceCreatedCallback,
			std::function<void(expected<void, std::string> const&)> onFinishCallback)
		{
			std::weak_ptr<InstancesManager::Impl> WThis(shared_from_this());
			AsyncHttpGetWithLink<SJsonInstWithId>(GetHttp(),
				"decorations/" + decorationId + "/instances",
				{} /* extra headers*/,
				[WThis, onInstanceCreatedCallback](SJsonInstWithId const& row) -> expected<void, std::string>
				{
					auto SThis = WThis.lock();
					if (!SThis)
						return make_unexpected("InstancesManager::Impl no longer exists");

					auto thdata = SThis->thdata_.GetAutoLock();

					IInstance* inst = IInstance::New();
					inst->SetId(RefID::FromDBIdentifier(row.id, thdata->instanceIDMap_));
					inst->SetName(row.name);
					if (row.animationid.has_value())
					{
						inst->SetAnimId(row.animationid.value());
						if (auto animPathInfoPtr = SThis->animPathManager_->FindAnimationPathInfoByDBId(row.animationid.value()))
						{
							auto animPathInfo = animPathInfoPtr->GetRAutoLock();
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
					dmat3x4 mat;
					std::memcpy(&mat[0], row.matrix.data(), 12 * sizeof(double));
					inst->SetTransform(mat);
					
					IInstancePtr sharedInst = MakeSharedLockableDataPtr(inst);

					std::string const groupid = row.groupid.value_or("");
					RefID const gpId = RefID::FromDBIdentifier(groupid, thdata->groupIDMap_);
					auto itGroup = thdata->mapIdToInstGroups_.find(gpId);
					if (itGroup != thdata->mapIdToInstGroups_.end())
					{
						inst->SetGroup(itGroup->second);
						auto gp = itGroup->second->GetAutoLock();
						gp->AddInstance(sharedInst);
					}

					thdata->mapObjectRefToInstances_[std::make_pair(row.objref, gpId)].push_back(sharedInst);

					onInstanceCreatedCallback(sharedInst);
					return {};
				},
				onFinishCallback
			);
		}

		void AsyncLoadInstancesGroups(const std::string& decorationId,
			std::function<void(IInstancesGroupPtr&)> onCreatedGroupCallback,
			std::function<void(expected<void, std::string> const& result)> onFinishCallback,
			std::function<void(IInstancesGroupPtr const&)> const& setDefaultGroupfct = {})
		{
			struct SJsonInstGroupWithId
			{
				std::string name;
				std::optional<std::string> userData;
				std::optional<std::string> type;
				std::string id;
			};

			std::shared_ptr<InstancesManager::Impl> SThis(shared_from_this());

			AsyncHttpGetWithLink<SJsonInstGroupWithId>(GetHttp(),
				"decorations/" + decorationId + "/instancesgroups",
				{} /* extra headers*/,
				[SThis, onCreatedGroupCallback](SJsonInstGroupWithId const& row) -> expected<void, std::string>
				{
					IInstancesGroup* group;
					std::optional<Tools::AutoLockObject<RWLockablePtrObject<IInstancesGroup>>> dgpLocked;

					{
						auto thdata = SThis->thdata_.GetAutoLock();
						group = IInstancesGroup::New();
						group->SetId(RefID::FromDBIdentifier(row.id, thdata->groupIDMap_));
					}
					group->SetName(row.name);
					if (row.type.has_value())
						group->SetType(row.type.value());

					// We may have saved linked spline as userData. Spline should be loaded *before* populations
					// to make it work.
					if (row.userData
						&& group->GetType() == "spline"
						&& SThis->splineManager_)
					{
						RefID const splineId = SThis->splineManager_->GetLoadedSplineId(*row.userData);
						if (splineId.IsValid())
						{
							group->SetLinkedSplineId(splineId);
						}
					}
					auto groupPtr = Tools::MakeSharedLockableDataPtr<IInstancesGroup>(group);
					SThis->AddInstancesGroup(groupPtr);

					// Invoke callback for each created group
					if (onCreatedGroupCallback)
					{
						onCreatedGroupCallback(groupPtr);
					}

					return {};
				},
				[SThis, setDefaultGroupfct, onFinishCallback](expected<void, std::string> const& result)
				{
					// Determine the default group, and set it through the provided callback (if any).
					SThis->InitDefaultGroupFromLoadedGroups(setDefaultGroupfct);

					if (!result)
					{
						// Invoke completion callback
						if (onFinishCallback)
						{
							onFinishCallback(result);
						}
						return;
					}

					// Invoke completion callback
					if (onFinishCallback)
					{
						expected<void, std::string> result2;
						onFinishCallback(result2);
					}

					return;
				}
			);
		}
		void LoadDataFromServer(const std::string& decorationId,
			const std::function<void(IInstancesGroupPtr const&)>& setDefaultGroupfct = {})
		{
			Clear();
			LoadInstancesGroups(decorationId, setDefaultGroupfct);
			LoadInstances(decorationId);
		}

		void AsyncLoadDataFromServer(const std::string& decorationId,
			const std::function<void(IInstancePtr&)>& OnCreatedInstancefct,
			const std::function<void(IInstancesGroupPtr&)>& OnCreatedGroupfct,
			const std::function<void(expected<void, std::string> const&)>& OnLoadFinishedfct,
			const std::function<void(IInstancesGroupPtr const&)>& setDefaultGroupfct = {}
			)
		{
			Clear();
			std::weak_ptr<InstancesManager::Impl> WThis(shared_from_this());
			AsyncLoadInstancesGroups(decorationId,
				OnCreatedGroupfct,
				[WThis, decorationId, OnCreatedInstancefct, OnLoadFinishedfct](expected<void, std::string> const& result)
				{
					if (result)
					{
						// Once groups are loaded, load instances
						if (auto SThis = WThis.lock())
						{
							SThis->AsyncLoadInstances(decorationId, OnCreatedInstancefct, OnLoadFinishedfct);
						}
						else
						{
							OnLoadFinishedfct(make_unexpected("Object destroyed"));
						}
					}
					else
					{
						OnLoadFinishedfct(result);
					}
				},
				setDefaultGroupfct
			);
		}

		void AsyncSaveInstancesGroup(
			std::string const& decorationId,
			IInstancesGroupPtr const& instGroupPtr,
			std::shared_ptr<AsyncRequestGroupCallback> const& callbackPtr)
		{
			auto instGroup = instGroupPtr->GetRAutoLock();
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
				auto const linkedSplinePtr = splineManager_->GetSplineById(*instGroup->GetLinkedSplineId());
				if (linkedSplinePtr)
				{
					auto linkedSpline = linkedSplinePtr->GetAutoLock();
					if (linkedSpline->HasDBIdentifier())
						jIn.userData = linkedSpline->GetId().GetDBIdentifier();
				}
			}

			struct SJsonOut
			{
				std::string id;
			};
			AsyncPostJsonJBody<SJsonOut>(GetHttp(), callbackPtr,
				[this, instGroupPtr](long httpCode, const Tools::TSharedLockableData<SJsonOut>& joutPtr)
			{
				const bool bSuccess = (httpCode == 200 || httpCode == 201);
				if (bSuccess)
				{
					auto instGroup = instGroupPtr->GetAutoLock();
					auto unlockedJout = joutPtr->GetAutoLock();
					SJsonOut const& jOut = unlockedJout.Get();
					// A new group has been created on the server. Update the identifier internally (but keep
					// the 'session' ID unchanged)
					RefID groupId = instGroup->GetId();
					groupId.SetDBIdentifier(jOut.id);
					instGroup->SetId(groupId);

					auto thdata = thdata_.GetAutoLock();
					thdata->mapIdToInstGroups_.insert(std::make_pair(groupId, instGroupPtr));
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Save instances group failed. Http status: " << httpCode);
				}
				return bSuccess;
			},
				"decorations/" + decorationId + "/instancesgroups",
				jIn);
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
				auto gp = src.GetGroup()->GetAutoLock();
				BE_ASSERT(gp->GetId().HasDBIdentifier(), "groups should be saved before instances");
				dst.groupid = gp->GetId().GetDBIdentifier();
			}
			auto& animId = src.GetAnimId();
			if (animId != "")
			{
				dst.animationid = animId;
			}
		}

		IInstancePtr GetInstanceById(ObjRefAndGPId const& objRefAndGroup, RefID const& id) const
		{
			auto thdata = thdata_.GetRAutoLock();
			const auto itVec = thdata->mapObjectRefToInstances_.find(objRefAndGroup);
			if (itVec == thdata->mapObjectRefToInstances_.cend())
			{
				return {};
			}
			SharedInstVect const& instVec(itVec->second);
			auto it = std::find_if(instVec.begin(), instVec.end(),
				[&id](IInstancePtr const& instPtr) {
				auto inst = instPtr->GetRAutoLock();
				return inst->GetId() == id;
			});
			if (it != instVec.end())
				return *it;
			return {};
		}

		void AsyncSaveInstances(
			std::string const& decorationId,
			ObjRefAndGPId const& objRefAndGroup,
			SharedInstVect const& instances,
			std::shared_ptr<AsyncRequestGroupCallback> const& callbackPtr)
		{
			struct SJsonInstVect { std::vector<SJsonInst> instances; };
			struct SJsonInstWithIdVect { std::vector<SJsonInstWithId> instances; };
			SJsonInstVect jInPost;
			SJsonInstWithIdVect jInPut;

			using RefIDVec = std::vector<RefID>;
			std::shared_ptr<RefIDVec> pNewInstIds = std::make_shared<RefIDVec>();
			std::shared_ptr<RefIDVec> pUpdatedInstIds = std::make_shared<RefIDVec>();
			std::vector<RefID>& newInstIds = *pNewInstIds;
			std::vector<RefID>& updatedInstIds = *pUpdatedInstIds;

			// Sort instances for requests (addition/update)
			for (auto& instPtr : instances)
			{
				auto inst = instPtr->GetAutoLock();
				if (inst->GetAnimPathId())
				{
					// update animation path database id within the instance
					auto animPathInfoPtr = animPathManager_->GetAnimationPathInfo(*(inst->GetAnimPathId()));
					auto animPathInfo = animPathInfoPtr->GetRAutoLock();
					auto animPathRefId = animPathInfo->GetId();	
					BE_ASSERT(animPathRefId.HasDBIdentifier(), "animation paths should be saved before instances");
					inst->SetAnimId(animPathRefId.GetDBIdentifier());
					inst->SetAnimPathId(animPathRefId);
				}

				if (!inst->HasDBIdentifier())
				{
					SJsonInst jInst;
					CopyInstance<SJsonInst>(jInst, *inst);
					jInPost.instances.push_back(jInst);
					newInstIds.push_back(inst->GetId());
					inst->OnStartSave();
				}
				else if (inst->ShouldSave())
				{
					SJsonInstWithId jInstwId;
					CopyInstance<SJsonInstWithId>(jInstwId, *inst);
					jInstwId.id = inst->GetDBIdentifier();
					jInPut.instances.push_back(jInstwId);
					updatedInstIds.push_back(inst->GetId());
					inst->OnStartSave();
				}
			}


			// Post (new instances)
			if (!jInPost.instances.empty())
			{
				struct SJsonIds
				{
					std::vector<std::string> ids;
				};

				AsyncPostJsonJBody<SJsonIds>(GetHttp(), callbackPtr,
					[this, pNewInstIds, objRefAndGroup](long httpCode,
														const Tools::TSharedLockableData<SJsonIds>& joutPtr)
				{
					const bool bSuccess = (httpCode == 200 || httpCode == 201);
					if (bSuccess)
					{
						auto unlockedJout = joutPtr->GetAutoLock();
						SJsonIds const& jOutPost = unlockedJout.Get();
						std::vector<RefID> const& newInstIds = *pNewInstIds;
						if (newInstIds.size() == jOutPost.ids.size())
						{
							for (size_t i = 0; i < newInstIds.size(); ++i)
							{
								RefID instId = newInstIds[i];
								// Update the DB identifier only.
								instId.SetDBIdentifier(jOutPost.ids[i]);
								BE_ASSERT(instId.HasDBIdentifier());

								if (IInstancePtr instPtr = GetInstanceById(objRefAndGroup, instId))
								{
									auto instLock = instPtr->GetAutoLock();
									IInstance& inst = instLock.Get();
									inst.SetId(instId); // to store DB identifier in the instance.
									inst.OnSaved();
								}
							}
						}
					}
					else
					{
						BE_LOGW("ITwinDecoration", "Saving new instances failed. Http status: " << httpCode);
					}
					return bSuccess;
				},
					"decorations/" + decorationId + "/instances",
					jInPost);
			}

			// Put (updated instances)
			if (!jInPut.instances.empty())
			{
				struct SJsonInstOutUpd
				{
					int64_t numUpdated = 0;
				};
				AsyncPutJsonJBody<SJsonInstOutUpd>(GetHttp(), callbackPtr,
					[this, pUpdatedInstIds, objRefAndGroup](long httpCode, const Tools::TSharedLockableData<SJsonInstOutUpd>& joutPtr)
				{
					const bool bSuccess = (httpCode == 200 || httpCode == 201);
					if (bSuccess)
					{
						auto unlockedJout = joutPtr->GetAutoLock();
						SJsonInstOutUpd const& jOutPut = unlockedJout.Get();
						std::vector<RefID> const& updatedInstIds = *pUpdatedInstIds;
						if (updatedInstIds.size() == static_cast<size_t>(jOutPut.numUpdated))
						{
							for (RefID const& instId : updatedInstIds)
							{
								IInstancePtr instPtr = GetInstanceById(objRefAndGroup, instId);
								if (instPtr)
								{
									auto inst = instPtr->GetAutoLock();
									inst->OnSaved();
								}
							}
						}
						else
						{
							BE_ISSUE("mismatch in updated instance count", updatedInstIds.size(), jOutPut.numUpdated);
						}
					}
					else
					{
						BE_LOGW("ITwinDecoration", "Updating instances failed. Http status: " << httpCode);
					}
					return bSuccess;
				},
					"decorations/" + decorationId  + "/instances",
					jInPut);
			}
		}

		void OnInstanceDeletedOnServer(ObjRefAndGPId const& objRefAndGroup, RefID const& deletedId)
		{
			auto thdata = thdata_.GetAutoLock();
			const auto itVec = thdata->mapObjectRefToDeletedInstances_.find(objRefAndGroup);
			if (itVec == thdata->mapObjectRefToDeletedInstances_.cend())
			{
				return;
			}
			SharedInstVect& instVec(itVec->second);
			std::erase_if(instVec, [&deletedId](const auto& removedInstPtr)
			{
				auto removedInst = removedInstPtr->GetRAutoLock();
				return removedInst->GetId() == deletedId;
			});
		}

		void AsyncDeleteInstances(std::string const& decorationId,
								  ObjRefAndGPId const& objRefAndGroup,
								  SharedInstVect& instances,
								  std::shared_ptr<AsyncRequestGroupCallback> const& callbackPtr)
		{
			if (instances.empty())
				return;

			struct JSonIn
			{
				std::vector<std::string> ids;
			} jIn;

			std::vector<RefID> deletedInstIds;
			jIn.ids.reserve(instances.size());
			deletedInstIds.reserve(instances.size());
			for (auto const& instPtr : instances)
			{
				auto inst = instPtr->GetRAutoLock();
				RefID const& instId = inst->GetId();
				deletedInstIds.push_back(instId);
				if (instId.HasDBIdentifier())
					jIn.ids.push_back(instId.GetDBIdentifier());
			}
			if (jIn.ids.empty())
			{
				// If some instances were removed before being saved, make sure they are definitively
				// discarded.
				instances.clear();
				return;
			}

			AsyncDeleteJsonNoOutput(GetHttp(), callbackPtr,
				[this,
				deletedInstIds, objRefAndGroup](long httpCode)
			{
				const bool bSuccess = (httpCode == 200 || httpCode == 201 || httpCode == 204 /* No-Content*/);
				if (bSuccess)
				{
					for (RefID const& deletedId : deletedInstIds)
					{
						OnInstanceDeletedOnServer(objRefAndGroup, deletedId);
					}
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Deleting instances failed. Http status: " << httpCode);
				}
				return bSuccess;
			},
				"decorations/" + decorationId + "/instances",
				jIn);
		}

		void AsyncDeleteInstancesGroup(const std::string& decorationId, const IInstancesGroupPtr& instancesGroupPtr,
			std::shared_ptr<AsyncRequestGroupCallback> const& callbackPtr)
		{
			auto instancesGroup = instancesGroupPtr->GetAutoLock();
			if (instancesGroup->GetId().HasDBIdentifier())
			{
				struct JSonIn
				{
					std::array<std::string, 1> ids;
				} jIn;
				jIn.ids[0] = instancesGroup->GetId().GetDBIdentifier();
				AsyncDeleteJsonNoOutput(GetHttp(), callbackPtr,
					[groupName = instancesGroup->GetName()](long httpCode)
				{
					const bool bSuccess = (httpCode == 200 || httpCode == 201 || httpCode == 204 /* No-Content*/);
					if (!bSuccess)
					{
						BE_LOGW("ITwinDecoration", "Deleting instance group '" << groupName << "' failed. Http status : " << httpCode);
					}
					return bSuccess;
				},
					"decorations/" + decorationId + "/instancesgroups",
					jIn);
			}
		}

		void AsyncSaveDataOnServer(const std::string& decorationId, std::function<void(bool)>&& onDataSavedFunc = {})
		{
			// Called when all is saved.
			std::function<void(bool)> onPopulationsSavedFunc =
				[onSaveCallback = std::move(onDataSavedFunc)](bool bSuccess)
			{
				BE_LOGV("ITwinDecoration", "Save Populations end");
				if (onSaveCallback)
					onSaveCallback(bSuccess);
			};

			// Helper used to call onDataSavedFunc when *all* requests are processed.
			std::shared_ptr<AsyncRequestGroupCallback> saveAllCallback =
				std::make_shared<AsyncRequestGroupCallback>(
					std::move(onPopulationsSavedFunc), isThisValid_);


			// Save groups before instances.
			saveAllCallback->AddRequestToWait(); // one counting for the groups
			std::shared_ptr<AsyncRequestGroupCallback> onGroupsSavedCallback =
				std::make_shared<AsyncRequestGroupCallback>(
				[this, decorationId, saveAllCallback](bool bSuccess)
			{
				if (!saveAllCallback->IsValid())
					return;

				// Save instances.
				auto thdata = thdata_.GetRAutoLock();
				for (auto const& instVec : thdata->mapObjectRefToInstances_)
				{
					AsyncSaveInstances(decorationId, instVec.first, instVec.second, saveAllCallback);
				}
				saveAllCallback->OnRequestDone(bSuccess);
			}, isThisValid_);

			auto thdata = thdata_.GetAutoLock();

			for (auto const& instGroup : thdata->instanceGroupsByName_)
			{
				AsyncSaveInstancesGroup(decorationId, instGroup.second, onGroupsSavedCallback);
			}
			onGroupsSavedCallback->OnFirstLevelRequestsRegistered();

			// Delete obsolete instance groups.
			for (auto& instGroup : thdata->instancesGroupsToDelete_)
			{
				AsyncDeleteInstancesGroup(decorationId, instGroup, saveAllCallback);
			}

			// Delete obsolete instances.
			for (auto& instVec : thdata->mapObjectRefToDeletedInstances_)
			{
				AsyncDeleteInstances(decorationId, instVec.first, instVec.second, saveAllCallback);
			}

			saveAllCallback->OnFirstLevelRequestsRegistered();
		}

		uint64_t GetInstanceCountByObjectRef(const std::string& objectRef, const RefID& gpId) const
		{
			auto thdata = thdata_.GetRAutoLock();
			if (!gpId.IsValid())
			{
				// Count instances matching given object in *all* groups.
				uint64_t ret = 0;
				for (auto it : thdata->mapObjectRefToInstances_)
				{
					if (it.first.first == objectRef)
						ret += it.second.size();
				}
				return ret;
			}
			else
			{
				const auto it = thdata->mapObjectRefToInstances_.find(std::make_pair(objectRef, gpId));
				if (it != thdata->mapObjectRefToInstances_.cend())
				{
					return static_cast<uint64_t>(it->second.size());
				}
			}
			return 0;
		}
		
		void SetInstanceCountByObjectRef(const std::string& objectRef, const RefID& gpId, uint64_t count)
		{
			auto thdata = thdata_.GetAutoLock();
			SharedInstVect& currentInstances = thdata->mapObjectRefToInstances_[std::make_pair(objectRef, gpId)];
			uint64_t oldSize = currentInstances.size();
			currentInstances.resize(static_cast<size_t>(count));

			if (count > oldSize)
			{
				// Create new instances
				auto groupPtr = GetInstancesGroup(gpId);
				if (groupPtr)
				{
					auto group = groupPtr->GetAutoLock();
					for (uint64_t i = oldSize; i < count; ++i)
					{
						auto& sharedInst = currentInstances[i];
						IInstance* inst = IInstance::New();
						sharedInst = MakeSharedLockableDataPtr<IInstance>(inst);
						inst->SetObjectRef(objectRef);
						inst->OnIndexChanged((int32_t)i);
						inst->SetGroup(groupPtr);
						group->AddInstance(sharedInst);
					}
				}
				else
				{
					BE_ISSUE("invalid group ID");
					for (uint64_t i = oldSize; i < count; ++i)
					{
						auto& sharedInst = currentInstances[i];
						IInstance* inst = IInstance::New();
						sharedInst = MakeSharedLockableDataPtr<IInstance>(inst);
						inst->SetObjectRef(objectRef);
						inst->OnIndexChanged((int32_t)i);
					}
				}
			}
		}

		IInstancePtr AddInstance(const std::string& objectRef, const RefID& gpId)
		{
			auto thdata = thdata_.GetAutoLock();
			SharedInstVect& currentInstances = thdata->mapObjectRefToInstances_[std::make_pair(objectRef, gpId)];
			IInstance* inst = IInstance::New();
			auto sharedInst = MakeSharedLockableDataPtr<IInstance>(inst);
			inst->SetObjectRef(objectRef);
			inst->OnIndexChanged((int32_t)currentInstances.size());
			currentInstances.push_back(sharedInst);
			auto group = GetInstancesGroup(gpId);
			if (group)
			{
				auto instGroup = group->GetAutoLock();
				instGroup->AddInstance(sharedInst);
				inst->SetGroup(group);
			}
			return sharedInst;
		}

		const SharedInstVect& GetInstancesByObjectRef(const std::string& objectRef, const RefID& gpId) const
		{
			auto thdata = thdata_.GetRAutoLock();
			const auto it = thdata->mapObjectRefToInstances_.find(std::make_pair(objectRef, gpId));
			if (it != thdata->mapObjectRefToInstances_.cend())
			{
				return it->second;
			}
			static SharedInstVect empty;
			return empty;
		}

		void RemoveInstancesByObjectRef(
			const std::string& objectRef, const RefID& gpId,
			const std::vector<int32_t>& indicesInDescendingOrder, bool bUseRemoveAtSwap)
		{
			auto thdata = thdata_.GetAutoLock();
			auto pair = std::make_pair(objectRef, gpId);
			SharedInstVect& currentInstances = thdata->mapObjectRefToInstances_[pair];
			SharedInstVect& deletedInstances =  thdata->mapObjectRefToDeletedInstances_[pair];

			for (auto const& index : indicesInDescendingOrder)
			{
				if (index < currentInstances.size())
				{
					deletedInstances.push_back(currentInstances[index]);
					if (bUseRemoveAtSwap && currentInstances.size() > 1)
					{
						std::swap(currentInstances[index], currentInstances.back());
						currentInstances.pop_back();

						auto movedInst = currentInstances[index]->GetAutoLock();
						movedInst->OnIndexChanged(index);
					}
					else
					{
						currentInstances.erase(currentInstances.begin() + index);
					}
				}
			}
		}

		void RemoveGroupInstances(const RefID& gpId)
		{
			auto thdata = thdata_.GetAutoLock();
			for (auto it = thdata->mapObjectRefToInstances_.begin(); it != thdata->mapObjectRefToInstances_.end(); ++it)
			{
				if (it->first.second == gpId)
				{
					SharedInstVect& currentInstances = thdata->mapObjectRefToInstances_[it->first];
					SharedInstVect& deletedInstances = thdata->mapObjectRefToDeletedInstances_[it->first];
					for (auto& inst : currentInstances)
						deletedInstances.push_back(inst);
					currentInstances.clear();
					it = thdata->mapObjectRefToInstances_.erase(it);
				}
			}
		}

		void OnInstancesRestored(const std::string& objectRef, const RefID& gpId, const std::vector<RefID>& restoredInstances)
		{
			auto thdata = thdata_.GetAutoLock();
			auto pair = std::make_pair(objectRef, gpId);
			[[maybe_unused]] SharedInstVect const& currentInstances = thdata->mapObjectRefToInstances_[pair];
			SharedInstVect& deletedInstances = thdata->mapObjectRefToDeletedInstances_[pair];

			for (RefID const& refID : restoredInstances)
			{
				BE_ASSERT(std::find_if(currentInstances.begin(), currentInstances.end(),
					[&refID](const IInstancePtr& p) { auto i = p->GetAutoLock(); return i->GetRefId() == refID; })
						!= currentInstances.end(), "restored instance not found!");
				std::erase_if(deletedInstances,
					[&refID](const IInstancePtr& p) { auto i = p->GetAutoLock(); return i->GetRefId() == refID; });
			}
		}

		bool HasInstances() const
		{
			auto thdata = thdata_.GetRAutoLock();
			for (const auto& it : thdata->mapObjectRefToInstances_)
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
			auto thdata = thdata_.GetRAutoLock();
			for (const auto& it : thdata->mapObjectRefToInstances_)
			{
				for (const auto& inst : it.second)
				{
					auto lockedInst = inst->GetAutoLock();
					if (!lockedInst->HasDBIdentifier() ||
						lockedInst->ShouldSave())
					{
						return true;
					}
				}
			}
			for (const auto& it : thdata->mapObjectRefToDeletedInstances_)
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
			auto thdata = thdata_.GetRAutoLock();
			std::vector<std::pair<std::string, RefID>> refs;
			refs.reserve(thdata->mapObjectRefToInstances_.size());
			for (const auto& it : thdata->mapObjectRefToInstances_)
			{
				refs.push_back(it.first);
			}
			return refs;
		}

		void AddInstancesGroup(const IInstancesGroupPtr& group)
		{
			if (group)
			{
				auto thdata = thdata_.GetAutoLock();
				auto gp = group->GetAutoLock();
				thdata->instanceGroupsByName_[gp->GetName()] = group;
				thdata->mapIdToInstGroups_[gp->GetId()] = group;
			}
		}

		void RemoveInstancesGroup(const IInstancesGroupPtr& group)
		{
			if (group)
			{
				auto thdata = thdata_.GetAutoLock();
				thdata->instancesGroupsToDelete_.push_back(group);
				auto gp = group->GetAutoLock();
				thdata->instanceGroupsByName_.erase(gp->GetName());
				thdata->mapIdToInstGroups_.erase(gp->GetId());
			}
		}

		IInstancesGroupPtr GetInstancesGroup(const RefID& gpId) const
		{
			auto thdata = thdata_.GetRAutoLock();
			auto it = thdata->mapIdToInstGroups_.find(gpId);
			if (it != thdata->mapIdToInstGroups_.end())
				return it->second;
			return {};
		}

		IInstancesGroupPtr GetInstancesGroupByName(const std::string& name) const
		{
			auto thdata = thdata_.GetRAutoLock();
			auto it = thdata->instanceGroupsByName_.find(name);
			if (it != thdata->instanceGroupsByName_.end())
				return it->second;
			return {};
		}

		IInstancesGroupPtr GetInstancesGroupBySplineID(const RefID& splineId) const
		{
			auto thdata = thdata_.GetRAutoLock();
			RefID const nullRef = RefID::Invalid();
			auto it = std::find_if(thdata->instanceGroupsByName_.begin(), thdata->instanceGroupsByName_.end(),
				[splineId, &nullRef](const std::pair<const std::string, AdvViz::SDK::IInstancesGroupPtr> &pair) {
				auto gp = pair.second->GetRAutoLock();
				return gp->GetLinkedSplineId().value_or(nullRef) == splineId;
			});
			if (it != thdata->instanceGroupsByName_.end())
				return it->second;
			return {};
		}

		void IterateInstancesGroups(std::function<void(IInstancesGroup const&)> const& func) const
		{
			auto thdata = thdata_.GetRAutoLock();
			for (auto const& [_, groupPtr] : thdata->instanceGroupsByName_)
			{
				auto group = groupPtr->GetRAutoLock();
				func(*group);
			}
		}
	};

	void InstancesManager::LoadDataFromServer(const std::string& decorationId,
		const std::function<void(IInstancesGroupPtr const&)>& setDefaultGroupfct /*= {}*/)
	{
		GetImpl().LoadDataFromServer(decorationId, setDefaultGroupfct);
	}

	void InstancesManager::AsyncLoadDataFromServer(const std::string& decorationId, 
		const std::function<void(IInstancePtr&)>& OnCreatedInstancefct,
		const std::function<void(IInstancesGroupPtr&)>& OnCreatedGroupfct,
		const std::function<void(expected<void, std::string> const&)>& OnLoadFinishedfct,
		const std::function<void(IInstancesGroupPtr const&)>& setDefaultGroupfct /*= {}*/)
	{
		GetImpl().AsyncLoadDataFromServer(decorationId, OnCreatedInstancefct, OnCreatedGroupfct, OnLoadFinishedfct, setDefaultGroupfct);
	}

	void InstancesManager::AsyncSaveDataOnServer(const std::string& decorationId, std::function<void(bool)>&& onDataSavedFunc /*= {}*/)
	{
		GetImpl().AsyncSaveDataOnServer(decorationId, std::move(onDataSavedFunc));
	}

	uint64_t InstancesManager::GetInstanceCountByObjectRef(const std::string& objectRef, const RefID& gpId) const
	{
		return GetImpl().GetInstanceCountByObjectRef(objectRef, gpId);
	}

	void InstancesManager::SetInstanceCountByObjectRef(const std::string& objectRef, const RefID& gpId, uint64_t count)
	{
		GetImpl().SetInstanceCountByObjectRef(objectRef, gpId, count);
	}

	IInstancePtr InstancesManager::AddInstance(const std::string& objectRef, const RefID& gpId)
	{
		return GetImpl().AddInstance(objectRef, gpId);
	}

	const SharedInstVect& InstancesManager::GetInstancesByObjectRef(const std::string& objectRef, const RefID& gpId) const
	{
		return GetImpl().GetInstancesByObjectRef(objectRef, gpId);
	}

	void InstancesManager::RemoveInstancesByObjectRef(const std::string& objectRef, const RefID& gpId,
		const std::vector<int32_t>& indicesInDescendingOrder, bool bUseRemoveAtSwap)
	{
		GetImpl().RemoveInstancesByObjectRef(objectRef, gpId, indicesInDescendingOrder, bUseRemoveAtSwap);
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

	void InstancesManager::IterateInstancesGroups(std::function<void(IInstancesGroup const&)> const& func) const
	{
		GetImpl().IterateInstancesGroups(func);
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

	InstancesManager::InstancesManager() : impl_(std::make_shared<Impl>())
	{
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