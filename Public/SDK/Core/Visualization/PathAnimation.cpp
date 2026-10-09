/*--------------------------------------------------------------------------------------+
|
|     $Source: PathAnimation.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "PathAnimation.h"

#include "AsyncHelpers.h"
#include "AsyncHttp.inl"
#include "InstancesManager.h"
#include "SplinesManager.h"
#include "Core/Network/HttpGetWithLink.h"
#include "Core/Singleton/singleton.h"
#include "Config.h"
#include "Core/Tools/FactoryClassInternalHelper.h"

//#define GLM_FORCE_ALIGNED_GENTYPES
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
//#include <glm/gtc/type_aligned.hpp>


namespace AdvViz::SDK
{
	class AnimationPathInfo::Impl : public std::enable_shared_from_this<AnimationPathInfo::Impl>
		, public SavableItemWithID
	{
	public:
		SPathAnimationInfo serverSideData_;
		RefID splineId_ = RefID::Invalid(); // identifies the associated spline (and may hold id defined by the server)
		//RefID instGroupId_; // identifies the associated population group (and may hold id defined by the server)

		void SetId(const RefID& id) override
		{
			SavableItemWithID::SetId(id);
			if (id.HasDBIdentifier())
				serverSideData_.id = id.GetDBIdentifier();
		}
	};

	AnimationPathInfo::AnimationPathInfo() :impl_(new Impl())
	{}

	AnimationPathInfo::~AnimationPathInfo()
	{}

	DEFINEFACTORYGLOBALS(AnimationPathInfo);

	const RefID& AnimationPathInfo::GetId() const
	{
		return GetImpl().GetId();
	}

	void AnimationPathInfo::SetId(const RefID& id)
	{
		GetImpl().SetId(id);
	}

	const RefID& AnimationPathInfo::GetSplineId() const
	{
		return GetImpl().splineId_;
	}

	void AnimationPathInfo::SetSplineId(const RefID& id)
	{
		GetImpl().splineId_ = id;
		if (id.HasDBIdentifier())
		{
			GetImpl().serverSideData_.splineId = id.GetDBIdentifier();
			GetImpl().InvalidateDB();
		}
	}

	//const RefID& AnimationPathInfo::GetInstGroupId() const
	//{
	//	return GetImpl().instGroupId_;
	//}

	//void AnimationPathInfo::SetInstGroupId(const RefID& id)
	//{
	//	GetImpl().instGroupId_ = id;
	//	if (id.HasDBIdentifier())
	//	{
	//		GetImpl().serverSideData_.instGroupId = id.GetDBIdentifier();
	//		GetImpl().InvalidateDB();
	//	}
	//}

	void AnimationPathInfo::SetSpeed(double v)
	{
		GetImpl().serverSideData_.speed = v;
		GetImpl().InvalidateDB();
	}

	double AnimationPathInfo::GetSpeed() const
	{
		return GetImpl().serverSideData_.speed.value_or(750.); // in m/s
	}

	void AnimationPathInfo::SetOffsetX(double v)
	{
		GetImpl().serverSideData_.offsetX = v;
		GetImpl().InvalidateDB();
	}

	double AnimationPathInfo::GetOffsetX() const
	{
		return GetImpl().serverSideData_.offsetX.value_or(0.0);
	}

	void AnimationPathInfo::SetOffsetY(double v)
	{
		GetImpl().serverSideData_.offsetY = v;
		GetImpl().InvalidateDB();
	}

	double AnimationPathInfo::GetOffsetY() const
	{
		return GetImpl().serverSideData_.offsetY.value_or(0.0);
	}

	void AnimationPathInfo::SetStartTime(double v)
	{
		GetImpl().serverSideData_.startTime = v;
		GetImpl().InvalidateDB();
	}

	double AnimationPathInfo::GetStartTime() const
	{
		return GetImpl().serverSideData_.startTime.value_or(0.0);
	}

	void AnimationPathInfo::SetIsLooping(bool b)
	{
		GetImpl().serverSideData_.hasLoop = b;
		GetImpl().InvalidateDB();
	}

	bool AnimationPathInfo::IsLooping() const
	{
		return GetImpl().serverSideData_.hasLoop.value_or(false);
	}

	void AnimationPathInfo::SetIsEnabled(bool b)
	{
		GetImpl().serverSideData_.isEnabled = b;
		GetImpl().InvalidateDB();
	}

	bool AnimationPathInfo::IsEnabled() const
	{
		return GetImpl().serverSideData_.isEnabled.value_or(true);
	}

	void AnimationPathInfo::SetInvDir(bool b)
	{
		GetImpl().serverSideData_.invDir = b;
		GetImpl().InvalidateDB();
	}

	bool AnimationPathInfo::HasInvDir() const
	{
		return GetImpl().serverSideData_.invDir.value_or(false);
	}

	void AnimationPathInfo::SetRepeatMode(int b)
	{
		GetImpl().serverSideData_.repeatMode = b;
		GetImpl().InvalidateDB();
	}

	int AnimationPathInfo::GetRepeatMode() const
	{
		return GetImpl().serverSideData_.repeatMode.value_or(1);
	}

	void AnimationPathInfo::SetOneWay(bool b)
	{
		GetImpl().serverSideData_.oneWay = b;
		GetImpl().InvalidateDB();
	}

	bool AnimationPathInfo::IsOneWay() const
	{
		return GetImpl().serverSideData_.oneWay.value_or(true);
	}

	void AnimationPathInfo::SetLaneCount(int b)
	{
		GetImpl().serverSideData_.laneCount = b;
		GetImpl().InvalidateDB();
	}

	int AnimationPathInfo::GetLaneCount() const
	{
		return GetImpl().serverSideData_.laneCount.value_or(1);
	}

	void AnimationPathInfo::SetLaneWidth(double v)
	{
		GetImpl().serverSideData_.laneWidth = v;
		GetImpl().InvalidateDB();
	}

	double AnimationPathInfo::GetLaneWidth() const
	{
		return GetImpl().serverSideData_.laneWidth.value_or(320.0);
	}

	void AnimationPathInfo::SetDensity(double v)
	{
		GetImpl().serverSideData_.density = v;
		GetImpl().InvalidateDB();
	}

	double AnimationPathInfo::GetDensity() const
	{
		return GetImpl().serverSideData_.density.value_or(0.3);
	}

	void AnimationPathInfo::SetSepWidth(double v)
	{
		GetImpl().serverSideData_.sepWidth = v;
		GetImpl().InvalidateDB();
	}

	double AnimationPathInfo::GetSepWidth() const
	{
		return GetImpl().serverSideData_.sepWidth.value_or(200.0);
	}

	void AnimationPathInfo::SetMinSpeed(double v)
	{
		GetImpl().serverSideData_.minSpeed = v;
		GetImpl().InvalidateDB();
	}

	double AnimationPathInfo::GetMinSpeed() const
	{
		return GetImpl().serverSideData_.minSpeed.value_or(1389.0);
	}

	void AnimationPathInfo::SetMaxSpeed(double v)
	{
		GetImpl().serverSideData_.maxSpeed = v;
		GetImpl().InvalidateDB();
	}

	double AnimationPathInfo::GetMaxSpeed() const
	{
		return GetImpl().serverSideData_.maxSpeed.value_or(1389.0);
	}

	void AnimationPathInfo::SetObjects(const std::vector<std::string> &objs)
	{
		GetImpl().serverSideData_.objects = std::move(objs);
		GetImpl().InvalidateDB();
	}

	void AnimationPathInfo::GetObjects(std::vector<std::string>& objs) const
	{
		objs.clear();
		if (GetImpl().serverSideData_.objects.has_value())
			objs = std::move(GetImpl().serverSideData_.objects.value());
	}

	ESaveStatus AnimationPathInfo::GetSaveStatus() const
	{
		return GetImpl().GetSaveStatus();
	}
	void AnimationPathInfo::SetSaveStatus(ESaveStatus status)
	{
		GetImpl().SetSaveStatus(status);
	}

	void AnimationPathInfo::SetServerSideData(const IAnimationPathInfo::SPathAnimationInfo& data)
	{
		GetImpl().serverSideData_ = data;
	}

	const IAnimationPathInfo::SPathAnimationInfo& AnimationPathInfo::GetServerSideData() const
	{
		return GetImpl().serverSideData_;
	}

	AnimationPathInfo::Impl& AnimationPathInfo::GetImpl()
	{
		return *impl_;
	}

	const AnimationPathInfo::Impl& AnimationPathInfo::GetImpl() const
	{
		return *impl_;
	}


	class PathAnimManager::Impl : public std::enable_shared_from_this<PathAnimManager::Impl>
	{
	public:
		std::shared_ptr<Http> http_;
		std::weak_ptr<IInstancesManager> instanceManager_;
		std::weak_ptr<ISplinesManager> splinesManager_;

		struct SThreadSafeData {
			std::unordered_map<RefID, IAnimationPathInfoPtr> infosMap_; // contains RefIDs without database ids
			std::unordered_map<RefID, IAnimationPathInfoPtr> removedInfosMap_;
		};

		Tools::RWLockableObject<SThreadSafeData> thdata_;

		std::shared_ptr< std::atomic_bool > isThisValid_;


		struct SJsonIds { std::vector<std::string> ids; };


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

		IAnimationPathInfoPtr FindAnimationPathInfoByDBId(const std::string& id) const
		{
			auto thdata = thdata_.GetRAutoLock();
			auto infosMap_ = thdata->infosMap_;
			for (const auto& [_, animpathinfoPtr] : infosMap_)
			{
				auto animpathinfo = animpathinfoPtr->GetRAutoLock();
				if (animpathinfo->HasDBIdentifier() && animpathinfo->GetDBIdentifier() == id)
					return animpathinfoPtr;
			}
			return IAnimationPathInfoPtr();
		}
	
		IAnimationPathInfoPtr FindAnimationPathInfoBySplineRefId(const RefID& id) const
		{
			auto thdata = thdata_.GetRAutoLock();
			auto infosMap_ = thdata->infosMap_;
			for (const auto& [_, animpathinfoPtr] : infosMap_)
			{
				auto animpathinfo = animpathinfoPtr->GetRAutoLock();
				if (animpathinfo->GetSplineId() == id)
					return animpathinfoPtr;
			}
			return IAnimationPathInfoPtr();
		}

		IAnimationPathInfoPtr AddAnimationPathInfo()
		{
			IAnimationPathInfo* AnimPath(IAnimationPathInfo::New());
			IAnimationPathInfoPtr animPtr = MakeSharedLockableDataPtr<IAnimationPathInfo>(AnimPath);
			auto thdata = thdata_.GetAutoLock();
			thdata->infosMap_[AnimPath->GetId()] = animPtr;
			return animPtr;
		}

		void RemoveAnimationPathInfo(const RefID& id)
		{
			auto thdata = thdata_.GetAutoLock();
			thdata->removedInfosMap_[id] = thdata->infosMap_[id];
			thdata->infosMap_.erase(id);
		}

		IAnimationPathInfoPtr GetAnimationPathInfo(const RefID& id) const
		{
			auto thdata = thdata_.GetRAutoLock();
			auto it = thdata->infosMap_.find(id);
			return (it != thdata->infosMap_.end()) ? it->second : IAnimationPathInfoPtr();
		}

		void GetAnimationPathIds(std::set<AdvViz::SDK::RefID>& ids) const
		{
			ids.clear();
			auto thdata = thdata_.GetRAutoLock();
			for (const auto& [key, _] : thdata->infosMap_)
			{
				ids.insert(key);
			}
		}

		bool HasAnimPathsToSave() const
		{
			auto thdata = thdata_.GetRAutoLock();
			for (const auto& [_, itPtr] : thdata->infosMap_)
			{
				auto it = itPtr->GetRAutoLock();
				if (it->ShouldSave())
				{
					return true;
				}
			}
			for (const auto& [_, itPtr] : thdata->removedInfosMap_)
			{
				auto it = itPtr->GetRAutoLock();
				if (it->HasDBIdentifier())
				{
					return true;
				}
			}
			return false;
		}

		void LoadDataFromServer(const std::string& decorationId);
		void AsyncLoadDataFromServer(const std::string& decorationId,
			const std::function<void(IAnimationPathInfoPtr&)>& onPathLoaded,
			const std::function<void(expected<void, std::string> const&)>& onComplete);

		void AsyncSaveDataOnServer(const std::string& decorationId, std::function<void(bool)>&& onDataSavedFunc);
	};

	void PathAnimManager::Impl::LoadDataFromServer(const std::string& decorationId)
	{
		auto ret = HttpGetWithLink<IAnimationPathInfo::SPathAnimationInfo>(GetHttp(),
			"decorations/" + decorationId + "/animationpaths",
			{} /* extra headers*/,
			[this](IAnimationPathInfo::SPathAnimationInfo const& row) -> expected<void, std::string>
		{
			if (!row.id)
				return make_unexpected("Server returned no id for animation path.");
			auto pathInfoPtr = AddAnimationPathInfo();
			auto pathInfo = pathInfoPtr->GetAutoLock();
			pathInfo->SetServerSideData(row);
			//if (row.speed)
			//	pathInfo->SetSpeed(row.speed.value());
			//if (row.offsetX)
			//	pathInfo->SetOffsetX(row.offsetX.value());
			//if (row.offsetY)
			//	pathInfo->SetOffsetY(row.offsetY.value());
			//if (row.startTime)
			//	pathInfo->SetStartTime(row.startTime.value());
			//if (row.is_looping)
			//	pathInfo->SetIsLooping(row.is_looping.value());
			//if (row.is_enabled)
			//	pathInfo->SetIsEnabled(row.is_enabled.value());
			pathInfo->SetDBIdentifier(row.id.value());
			// init spline RefId
			auto splinesManager(splinesManager_.lock());
			if (auto splinePtr = splinesManager->GetSplineByDBId(row.splineId.value()))
			{
				auto spline = splinePtr->GetRAutoLock();
				pathInfo->SetSplineId(spline->GetId());
			}
			// init population group RefId
			//auto instanceManager(instanceManager_.lock());
			//if (auto instGroupPtr = instanceManager->GetInstancesGroupBySplineID(pathInfo->GetSplineId()))
			//{
			//	auto instGroup = instGroupPtr->GetRAutoLock();
			//	pathInfo->SetInstGroupId(instGroup->GetId());
			//}
			pathInfo->SetShouldSave(false);
			return {};
		});

		if (!ret)
		{
			BE_LOGW("ITwinDecoration", "Loading of animation paths failed. " << ret.error());
		}
	}

	void PathAnimManager::Impl::AsyncLoadDataFromServer(const std::string& decorationId,
		const std::function<void(IAnimationPathInfoPtr&)>& onPathLoaded,
		const std::function<void(expected<void, std::string> const&)>& onComplete)
	{
		auto SThis = this->shared_from_this();
		AsyncHttpGetWithLink<IAnimationPathInfo::SPathAnimationInfo>(GetHttp(),
			"decorations/" + decorationId + "/animationpaths",
			{} /* extra headers*/,
			[SThis, onPathLoaded](IAnimationPathInfo::SPathAnimationInfo const& row) -> expected<void, std::string>
		{
			if (!row.id)
				return make_unexpected("Server returned no id for animation path.");
			auto pathInfoPtr = SThis->AddAnimationPathInfo();
			auto pathInfo = pathInfoPtr->GetAutoLock();
			pathInfo->SetServerSideData(row);
			//if (row.speed)
			//	pathInfo->SetSpeed(row.speed.value());
			//if (row.offsetX)
			//	pathInfo->SetOffsetX(row.offsetX.value());
			//if (row.offsetY)
			//	pathInfo->SetOffsetY(row.offsetY.value());
			//if (row.startTime)
			//	pathInfo->SetStartTime(row.startTime.value());
			//if (row.is_looping)
			//	pathInfo->SetIsLooping(row.is_looping.value());
			//if (row.is_enabled)
			//	pathInfo->SetIsEnabled(row.is_enabled.value());
			auto refID = pathInfo->GetId();
			refID.SetDBIdentifier(row.id.value());
			pathInfo->SetId(refID);
			// init spline RefId
			auto splinesManager(SThis->splinesManager_.lock());
			if (!splinesManager)
				return make_unexpected("Splines manager is not set.");
			auto splinePtr = splinesManager->GetSplineByDBId(row.splineId.value());
			if(splinePtr)
			{ 
				auto spline = splinePtr->GetRAutoLock();
				pathInfo->SetSplineId(spline->GetId());
				pathInfo->SetShouldSave(false);
			}
			else
			{
				BE_LOGW("ITwinDecoration", "Could not find spline with DB id " << row.splineId.value() << " for animation path with id " << row.id.value());
				// We don't return an error here since the animation path itself is valid and can be loaded, even if the associated spline is missing (maybe it will be loaded later, or maybe the path can be used without the spline, etc.)
			}
			if (onPathLoaded)
				onPathLoaded(pathInfoPtr);
			return {};
		},
			onComplete
		);
	}

	void PathAnimManager::Impl::AsyncSaveDataOnServer(const std::string& decorationId, std::function<void(bool)>&& onDataSavedFunc)
	{
		std::shared_ptr<AsyncRequestGroupCallback> callbackPtr =
			std::make_shared<AsyncRequestGroupCallback>(
				std::move(onDataSavedFunc), isThisValid_);

		struct SJsonAnimPathVect
		{
			std::vector<IAnimationPathInfo::SPathAnimationInfo> AnimationPaths;
		};
		SJsonAnimPathVect jInPost, jInPut;

		std::vector<RefID> newIndices;
		std::vector<RefID> updatedIndices;

		auto thdata = thdata_.GetRAutoLock();
		auto infosMap_ = thdata->infosMap_;
		// Sort splines for requests (addition/update)
		for (auto const& elem : infosMap_)
		{
			auto infoPtr = elem.second->GetAutoLock();
			// init spline database id for new splines (splines should always be saved before the animation paths!)
			auto splinesManager(splinesManager_.lock());
			auto splinePtr = splinesManager->GetSplineById(infoPtr->GetSplineId());
			if (splinePtr)
			{
				auto spline = splinePtr->GetRAutoLock();
				infoPtr->SetSplineId(spline->GetId());

				if (!infoPtr->HasDBIdentifier())
				{
					jInPost.AnimationPaths.emplace_back(infoPtr->GetServerSideData());
					newIndices.push_back(elem.first);
					infoPtr->OnStartSave();
				}
				else if (infoPtr->ShouldSave())
				{
					jInPut.AnimationPaths.emplace_back(infoPtr->GetServerSideData());
					updatedIndices.push_back(elem.first);
					infoPtr->OnStartSave();
				}
			}
		}

		// Post (new paths)
		if (!jInPost.AnimationPaths.empty())
		{
			AsyncPostJsonJBody<SJsonIds>(GetHttp(), callbackPtr,
				[this, newIndices](
					long httpCode,
					const Tools::TSharedLockableData<SJsonIds>& joutPtr)
			{
				const bool bSuccess = (httpCode == 200 || httpCode == 201);
				if (bSuccess)
				{
					auto unlockedJout = joutPtr->GetAutoLock();
					SJsonIds& jOutPost = unlockedJout.Get();
					if (newIndices.size() == jOutPost.ids.size())
					{
						for (size_t i = 0; i < newIndices.size(); ++i)
						{
							if (auto pathInfoPtr = GetAnimationPathInfo(newIndices[i]))
							{
								// Update the DB identifier only.
								auto pathInfo = pathInfoPtr->GetAutoLock();
								pathInfo->SetDBIdentifier(jOutPost.ids[i]);
								pathInfo->OnSaved();
							}
						}
					}
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Saving new animation paths failed. Http status: " << httpCode);
				}
				return bSuccess;
			},
				"decorations/" + decorationId + "/animationpaths",
				jInPost);
		}

		// Put (updated paths)
		if (!jInPut.AnimationPaths.empty())
		{
			struct SJsonAnimPathOutUpd
			{
				int64_t numUpdated = 0;
			};
			AsyncPutJsonJBody<SJsonAnimPathOutUpd>(GetHttp(), callbackPtr,
				[this, updatedIndices](
					long httpCode,
					const Tools::TSharedLockableData<SJsonAnimPathOutUpd>& joutPtr)
			{
				const bool bSuccess = (httpCode == 200 || httpCode == 201);
				if (bSuccess)
				{
					auto unlockedJout = joutPtr->GetAutoLock();
					SJsonAnimPathOutUpd& jOutPut = unlockedJout.Get();
					if (updatedIndices.size() == static_cast<size_t>(jOutPut.numUpdated))
					{
						for (RefID const& pathId : updatedIndices)
						{
							if (auto pathInfoPtr = GetAnimationPathInfo(pathId))
							{
								auto pathInfo = pathInfoPtr->GetAutoLock();
								pathInfo->OnSaved();
							}
						}
					}
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Updating animation paths failed. Http status: " << httpCode);
				}
				return bSuccess;
			},
				"decorations/" + decorationId + "/animationpaths",
				jInPut);
		}

		// delete obsolete animation paths
		SJsonIds jIn;
		std::vector<RefID> deletedPathIds;
		auto removedInfosMap_ = thdata->removedInfosMap_;
		jIn.ids.reserve(removedInfosMap_.size());
		deletedPathIds.reserve(removedInfosMap_.size());
		for (auto const& elem : removedInfosMap_)
		{
			auto infoPtr = elem.second->GetAutoLock();
			auto const& refId = infoPtr->GetId();
			deletedPathIds.push_back(refId);
			if (refId.HasDBIdentifier())
				jIn.ids.push_back(refId.GetDBIdentifier());
		}

		if (!jIn.ids.empty())
		{
			AsyncDeleteJsonNoOutput(GetHttp(), callbackPtr,
				[this, deletedPathIds](long httpCode)
			{
				const bool bSuccess = (httpCode == 200 || httpCode == 201 || httpCode == 204 /* No-Content*/);
				if (bSuccess)
				{
					auto thdata = thdata_.GetAutoLock();
					auto removedInfosMap_ = thdata->removedInfosMap_;
					for (RefID const& deletedId : deletedPathIds)
						removedInfosMap_.erase(deletedId);
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Deleting animation paths failed. Http status: " << httpCode);
				}
				return bSuccess;
			},
				"decorations/" + decorationId + "/animationpaths",
				jIn);
		}

		callbackPtr->OnFirstLevelRequestsRegistered();
	}


	PathAnimManager::PathAnimManager() : impl_(new Impl)
	{
	}

	void PathAnimManager::SetInstanceManager(const std::shared_ptr<IInstancesManager>& instanceManager)
	{
		GetImpl().instanceManager_ = instanceManager;
	}

	void PathAnimManager::SetSplinesManager(const std::shared_ptr<ISplinesManager>& splinesManager)
	{
		GetImpl().splinesManager_ = splinesManager;
	}

	size_t PathAnimManager::GetNumberOfPaths() const
	{
		auto thdata = GetImpl().thdata_.GetRAutoLock();
		return thdata->infosMap_.size();
	}

	IAnimationPathInfoPtr PathAnimManager::FindAnimationPathInfoByDBId(const std::string& id) const
	{
		return GetImpl().FindAnimationPathInfoByDBId(id);
	}

	IAnimationPathInfoPtr PathAnimManager::FindAnimationPathInfoBySplineRefId(const RefID& id) const
	{
		return GetImpl().FindAnimationPathInfoBySplineRefId(id);
	}

	IAnimationPathInfoPtr PathAnimManager::AddAnimationPathInfo()
	{
		return GetImpl().AddAnimationPathInfo();
	}

	void PathAnimManager::RemoveAnimationPathInfo(const RefID& id)
	{
		GetImpl().RemoveAnimationPathInfo(id);
	}

	IAnimationPathInfoPtr PathAnimManager::GetAnimationPathInfo(const RefID& id) const
	{
		return GetImpl().GetAnimationPathInfo(id);
	}

	void PathAnimManager::GetAnimationPathIds(std::set<AdvViz::SDK::RefID>& ids) const
	{
		GetImpl().GetAnimationPathIds(ids);
	}

	void PathAnimManager::LoadDataFromServer(const std::string& decorationId)
	{
		GetImpl().LoadDataFromServer(decorationId);
	}

	void PathAnimManager::AsyncLoadDataFromServer(const std::string& decorationId,
		const std::function<void(IAnimationPathInfoPtr&)>& onPathLoaded,
		const std::function<void(expected<void, std::string> const&)>& onComplete)
	{
		GetImpl().AsyncLoadDataFromServer(decorationId, onPathLoaded, onComplete);
	}

	void PathAnimManager::AsyncSaveDataOnServer(const std::string& decorationId, std::function<void(bool)>&& onDataSavedFunc)
	{
		GetImpl().AsyncSaveDataOnServer(decorationId, std::move(onDataSavedFunc));
	}

	bool PathAnimManager::HasAnimPathsToSave() const
	{
		return GetImpl().HasAnimPathsToSave();
	}

	PathAnimManager::Impl& PathAnimManager::GetImpl()
	{
		return *impl_;
	}

	const PathAnimManager::Impl& PathAnimManager::GetImpl() const
	{
		return *impl_;
	}

	DEFINEFACTORYGLOBALS(PathAnimManager);
 }