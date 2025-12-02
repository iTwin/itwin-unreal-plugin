/*--------------------------------------------------------------------------------------+
|
|     $Source: PathAnimation.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "PathAnimation.h"
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
	{
	public:
		SPathAnimationInfo serverSideData_;

		RefID animationId_; // identifies the animation path (and may hold id defined by the server)
		RefID splineId_; // identifies the associated spline (and may hold id defined by the server)

		bool shouldSave_ = false;
	};

	AnimationPathInfo::AnimationPathInfo() :impl_(new Impl())
	{}

	AnimationPathInfo::~AnimationPathInfo()
	{}

	DEFINEFACTORYGLOBALS(AnimationPathInfo);

	const RefID& AnimationPathInfo::GetId() const
	{
		return GetImpl().animationId_;
	}

	void AnimationPathInfo::SetId(const RefID& id)
	{
		GetImpl().animationId_ = id;
		if (id.HasDBIdentifier())
			GetImpl().serverSideData_.id = id.GetDBIdentifier();
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
		GetImpl().shouldSave_ = true;
	}

	double AnimationPathInfo::GetSpeed() const
	{
		if (GetImpl().serverSideData_.speed.has_value())
			return GetImpl().serverSideData_.speed.value();
		return 0.0;
	}

	void AnimationPathInfo::SetOffsetX(double v)
	{
		GetImpl().serverSideData_.offsetX = v;
		GetImpl().shouldSave_ = true;
	}

	double AnimationPathInfo::GetOffsetX() const
	{
		if (GetImpl().serverSideData_.offsetX.has_value())
			return GetImpl().serverSideData_.offsetX.value();
		return 0.0;
	}

	void AnimationPathInfo::SetOffsetY(double v)
	{
		GetImpl().serverSideData_.offsetY = v;
		GetImpl().shouldSave_ = true;
	}

	double AnimationPathInfo::GetOffsetY() const
	{
		if (GetImpl().serverSideData_.offsetY.has_value())
			return GetImpl().serverSideData_.offsetY.value();
		return 0.0;
	}

	void AnimationPathInfo::SetStartTime(double v)
	{
		GetImpl().serverSideData_.startTime = v;
		GetImpl().shouldSave_ = true;
	}

	double AnimationPathInfo::GetStartTime() const
	{
		if (GetImpl().serverSideData_.startTime.has_value())
			return GetImpl().serverSideData_.startTime.value();
		return 0.0;
	}

	void AnimationPathInfo::SetIsLooping(bool b)
	{
		GetImpl().serverSideData_.hasLoop = b;
		GetImpl().shouldSave_ = true;
	}

	bool AnimationPathInfo::IsLooping() const
	{
		if (GetImpl().serverSideData_.hasLoop.has_value())
			return GetImpl().serverSideData_.hasLoop.value();
		return false;
	}
	void AnimationPathInfo::SetIsEnabled(bool b)
	{
		GetImpl().serverSideData_.isEnabled = b;
		GetImpl().shouldSave_ = true;
	}

	bool AnimationPathInfo::IsEnabled() const
	{
		if (GetImpl().serverSideData_.isEnabled.has_value())
			return GetImpl().serverSideData_.isEnabled.value();
		return false;
	}

	void AnimationPathInfo::SetShouldSave(bool b)
	{
		GetImpl().shouldSave_ = b;
	}

	bool AnimationPathInfo::ShouldSave() const
	{
		return GetImpl().shouldSave_;
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


	class PathAnimator::Impl
	{
	public:
		std::shared_ptr<Http> http_;
		std::weak_ptr<IInstancesManager> instanceManager_;
		std::weak_ptr<ISplinesManager> splinesManager_;

		std::unordered_map<RefID, SharedPathInfo> infosMap_; // contains RefIDs without database ids
		std::unordered_map<RefID, SharedPathInfo> removedInfosMap_;

		struct SJsonIds { std::vector<std::string> ids; };
		struct SJsonEmpty {};

		std::shared_ptr<Http>& GetHttp() { return http_; }

		void SetHttp(const std::shared_ptr<Http>& http) { http_ = http; }

		SharedPathInfo FindAnimationPathInfoByDBId(const std::string& id) const
		{
			for (const auto& [_, animpathinfo] : infosMap_)
			{
				if (animpathinfo->GetId().HasDBIdentifier() && animpathinfo->GetId().GetDBIdentifier() == id)
					return animpathinfo;
			}
			return SharedPathInfo();
		}

		SharedPathInfo AddAnimationPathInfo()
		{
			SharedPathInfo AnimPath(IAnimationPathInfo::New());
			infosMap_[AnimPath->GetId()] = AnimPath;
			return AnimPath;
		}

		void RemoveAnimationPathInfo(const RefID& id)
		{
			removedInfosMap_[id] = infosMap_[id];
			infosMap_.erase(id);
		}

		SharedPathInfo GetAnimationPathInfo(const RefID& id) const
		{
			auto it = infosMap_.find(id);
			return (it != infosMap_.end()) ? it->second : SharedPathInfo();
		}

		void GetAnimationPathIds(std::set<AdvViz::SDK::RefID>& ids) const
		{
			ids.clear();
			for (const auto& [key, _] : infosMap_)
			{
				ids.insert(key);
			}
		}

		void LoadDataFromServer(const std::string& decorationId)
		{
			auto ret = HttpGetWithLink<IAnimationPathInfo::SPathAnimationInfo>(GetHttp(),
				"decorations/" + decorationId + "/animationpaths",
				{} /* extra headers*/,
				[this](IAnimationPathInfo::SPathAnimationInfo const& row) -> expected<void, std::string>
				{
					if (!row.id)
						return make_unexpected(std::string("Server returned no id for animation path."));
					auto pathInfo = AddAnimationPathInfo();
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
					auto splinesManager(splinesManager_.lock());
					auto spline = splinesManager->GetSplineByDBId(row.splineId.value());
					pathInfo->SetSplineId(spline->GetId());
					pathInfo->SetShouldSave(false);
					return {};
				});

			if (!ret)
			{
				BE_LOGW("ITwinDecoration", "Loading of animation paths failed. " << ret.error());
			}
		}
		
		void SaveDataOnServer(const std::string& decorationId)
		{
			struct SJsonAnimPathVect
			{
				std::vector<IAnimationPathInfo::SPathAnimationInfo> AnimationPaths;
			};
			SJsonAnimPathVect jInPost, jInPut;

			std::vector<RefID> newIndices;
			std::vector<RefID> updatedIndices;

			// Sort splines for requests (addition/update)
			for (auto const& elem : infosMap_)
			{
				// init spline database id for new splines (splines should always be saved before the animation paths!)
				auto splinesManager(splinesManager_.lock());
				auto spline = splinesManager->GetSplineById(elem.second->GetSplineId());
				elem.second->SetSplineId(spline->GetId());

				if (!elem.second->GetId().HasDBIdentifier())
				{
					jInPost.AnimationPaths.emplace_back(elem.second->GetServerSideData());
					newIndices.push_back(elem.first);
				}
				else if (elem.second->ShouldSave())
				{
					jInPut.AnimationPaths.emplace_back(elem.second->GetServerSideData());
					updatedIndices.push_back(elem.first);
				}
			}

			// Post (new splines)
			if (!jInPost.AnimationPaths.empty())
			{
				SJsonIds jOutPost;
				long status = GetHttp()->PostJsonJBody(
					jOutPost, "decorations/" + decorationId + "/animationpaths", jInPost);

				if (status == 200 || status == 201)
				{
					if (newIndices.size() == jOutPost.ids.size())
					{
						for (size_t i = 0; i < newIndices.size(); ++i)
						{
							if (auto pathInfo = GetAnimationPathInfo(newIndices[i]))
							{
								// Update the DB identifier only.
								RefID refId = pathInfo->GetId();
								refId.SetDBIdentifier(jOutPost.ids[i]);
								pathInfo->SetId(refId);
								pathInfo->SetShouldSave(false);
							}
						}
					}
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Saving new animation paths failed. Http status: " << status);
				}
			}

			// Put (updated splines)
			if (!jInPut.AnimationPaths.empty())
			{
				struct SJsonSplineOutUpd { int64_t numUpdated = 0; };
				SJsonSplineOutUpd jOutPut;
				long status = GetHttp()->PutJsonJBody(
					jOutPut, "decorations/" + decorationId + "/animationpaths", jInPut);

				if (status == 200 || status == 201)
				{
					if (updatedIndices.size() == static_cast<size_t>(jOutPut.numUpdated))
					{
						for (size_t i = 0; i < updatedIndices.size(); ++i)
						{
							if (auto pathInfo = GetAnimationPathInfo(newIndices[i]))
								pathInfo->SetShouldSave(false);
						}
					}
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Updating animation paths failed. Http status: " << status);
				}
			}

			// delete obsolete animation paths
			SJsonIds jIn;
			SJsonEmpty jOut;

			jIn.ids.reserve(removedInfosMap_.size());
			for (auto const& elem : removedInfosMap_)
			{
				auto const& refId = elem.second->GetId();
				if (refId.HasDBIdentifier())
					jIn.ids.push_back(refId.GetDBIdentifier());
			}

			if (!jIn.ids.empty())
			{
				long status = GetHttp()->DeleteJsonJBody(
					jOut, "decorations/" + decorationId + "/animationpaths", jIn);

				if (status == 200 || status == 201)
				{
					removedInfosMap_.clear();
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Deleting animation paths failed. Http status: " << status);
				}
			}
		}
	};

	PathAnimator::PathAnimator() : impl_(new Impl)
	{
		GetImpl().SetHttp(GetDefaultHttp());
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
		return GetImpl().infosMap_.size();
	}

	SharedPathInfo PathAnimator::FindAnimationPathInfoByDBId(const std::string& id) const
	{
		return GetImpl().FindAnimationPathInfoByDBId(id);
	}

	SharedPathInfo PathAnimator::AddAnimationPathInfo()
	{
		return GetImpl().AddAnimationPathInfo();
	}

	void PathAnimator::RemoveAnimationPathInfo(const RefID& id)
	{
		GetImpl().RemoveAnimationPathInfo(id);
	}

	SharedPathInfo PathAnimator::GetAnimationPathInfo(const RefID& id) const
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

	void PathAnimator::SaveDataOnServer(const std::string& decorationId)
	{
		GetImpl().SaveDataOnServer(decorationId);
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