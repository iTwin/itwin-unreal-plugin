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
		RefID splineId_; // identifies the associated spline (and may hold id defined by the server)

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
			GetImpl().serverSideData_.splineId = id.GetDBIdentifier();
	}

	void AnimationPathInfo::SetSpeed(double v)
	{
		GetImpl().serverSideData_.speed = v;
		GetImpl().InvalidateDB();
	}

	double AnimationPathInfo::GetSpeed() const
	{
		return GetImpl().serverSideData_.speed.value_or(0.0);
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
		return GetImpl().serverSideData_.isEnabled.value_or(false);
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


	class PathAnimator::Impl : public std::enable_shared_from_this<PathAnimator::Impl>
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

		void LoadDataFromServer(const std::string& decorationId);
		void AsyncLoadDataFromServer(const std::string& decorationId,
			const std::function<void(IAnimationPathInfoPtr&)>& onPathLoaded,
			const std::function<void(expected<void, std::string> const&)>& onComplete);

		void AsyncSaveDataOnServer(const std::string& decorationId, std::function<void(bool)>&& onDataSavedFunc);
	};

	void PathAnimator::Impl::LoadDataFromServer(const std::string& decorationId)
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
			auto splinePtr = splinesManager->GetSplineByDBId(row.splineId.value());
			auto spline = splinePtr->GetRAutoLock();
			pathInfo->SetSplineId(spline->GetId());
			pathInfo->SetShouldSave(false);
			return {};
		});

		if (!ret)
		{
			BE_LOGW("ITwinDecoration", "Loading of animation paths failed. " << ret.error());
		}
	}

	void PathAnimator::Impl::AsyncLoadDataFromServer(const std::string& decorationId,
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
			auto spline = splinePtr->GetRAutoLock();
			pathInfo->SetSplineId(spline->GetId());
			pathInfo->SetShouldSave(false);
			if (onPathLoaded)
				onPathLoaded(pathInfoPtr);
			return {};
		},
			onComplete
		);
	}

	void PathAnimator::Impl::AsyncSaveDataOnServer(const std::string& decorationId, std::function<void(bool)>&& onDataSavedFunc)
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
				}
				else if (infoPtr->ShouldSave())
				{
					jInPut.AnimationPaths.emplace_back(infoPtr->GetServerSideData());
					updatedIndices.push_back(elem.first);
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


	PathAnimator::PathAnimator() : impl_(new Impl)
	{
	}

	void PathAnimator::SetInstanceManager(const std::shared_ptr<IInstancesManager>& instanceManager)
	{
		GetImpl().instanceManager_ = instanceManager;
	}

	void PathAnimator::SetSplinesManager(const std::shared_ptr<ISplinesManager>& splinesManager)
	{
		GetImpl().splinesManager_ = splinesManager;
	}

	size_t PathAnimator::GetNumberOfPaths() const
	{
		auto thdata = GetImpl().thdata_.GetRAutoLock();
		return thdata->infosMap_.size();
	}

	IAnimationPathInfoPtr PathAnimator::FindAnimationPathInfoByDBId(const std::string& id) const
	{
		return GetImpl().FindAnimationPathInfoByDBId(id);
	}

	IAnimationPathInfoPtr PathAnimator::AddAnimationPathInfo()
	{
		return GetImpl().AddAnimationPathInfo();
	}

	void PathAnimator::RemoveAnimationPathInfo(const RefID& id)
	{
		GetImpl().RemoveAnimationPathInfo(id);
	}

	IAnimationPathInfoPtr PathAnimator::GetAnimationPathInfo(const RefID& id) const
	{
		return GetImpl().GetAnimationPathInfo(id);
	}

	void PathAnimator::GetAnimationPathIds(std::set<AdvViz::SDK::RefID>& ids) const
	{
		GetImpl().GetAnimationPathIds(ids);
	}

	void PathAnimator::LoadDataFromServer(const std::string& decorationId)
	{
		GetImpl().LoadDataFromServer(decorationId);
	}

	void PathAnimator::AsyncLoadDataFromServer(const std::string& decorationId,
		const std::function<void(IAnimationPathInfoPtr&)>& onPathLoaded,
		const std::function<void(expected<void, std::string> const&)>& onComplete)
	{
		GetImpl().AsyncLoadDataFromServer(decorationId, onPathLoaded, onComplete);
	}

	void PathAnimator::AsyncSaveDataOnServer(const std::string& decorationId, std::function<void(bool)>&& onDataSavedFunc)
	{
		GetImpl().AsyncSaveDataOnServer(decorationId, std::move(onDataSavedFunc));
	}

	bool PathAnimator::HasAnimPathsToSave() const
	{
		return true; // TODO@DK
	}

	PathAnimator::Impl& PathAnimator::GetImpl()
	{
		return *impl_;
	}

	const PathAnimator::Impl& PathAnimator::GetImpl() const
	{
		return *impl_;
	}

	DEFINEFACTORYGLOBALS(PathAnimator);
 }