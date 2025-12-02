/*--------------------------------------------------------------------------------------+
|
|     $Source: KeyframeAnimator.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#include "KeyframeAnimator.h"
#include "KeyframeAnimation.h"
#include "InstancesManager.h"
#include "Core/Tools/FactoryClassInternalHelper.h"
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Core/Tools/Log.h"

//#define DEBUGCULLING //D-O-NOTC

namespace AdvViz::SDK
{
	using namespace Tools;

	inline bool operator<(float v, const TimeRange& t) // note: used by TTimelineResults below, compare is reversed 
	{
		return t.begin < v;
	}

	inline bool operator<(const TimeRange& t, float v) // note: used by TTimelineResults below, compare is reversed 
	{
		return v < t.begin;
	}

	typedef	TSharedLockableData<IAnimationKeyframeInfo::TimelineResult> TimelineResultLockPtr;

	class InstanceWithPathExt : public Tools::Extension, public Tools::TypeId<InstanceWithPathExt>, public std::enable_shared_from_this<InstanceWithPathExt>
	{
	public:
		typedef std::map<TimeRange, TimelineResultLockPtr, std::less<>> TTimelineResults; // order is reversed

		InstanceWithPathExt(const IInstancePtr inst, const IAnimationKeyframeInfoWPtr kfInfo, const std::weak_ptr<IGCSTransform> &trans):instance_(inst), kfInfoPtr_(kfInfo), transform_(trans)
		{}

		bool ProcessTransform(float time)
		{
			auto kfInfoPtr = kfInfoPtr_.lock(); // lock shared_ptr
			if (kfInfoPtr)
			{
				auto autoKeyframeslock(keyframes_.GetRAutoLock());
				auto it = autoKeyframeslock.Get().lower_bound(time);
				if (it == autoKeyframeslock.Get().end())
					return false;
				if (it->second.get() == nullptr)
					return false;

				auto autoResulock(it->second.get()->GetRAutoLock());
				auto autolock(kfInfoPtr->GetRAutoLock());
				const auto& kfInfo = autolock.Get();
				if (autoResulock.Get().translations.empty())
					return false;
				IAnimationKeyframeInfo::TimelineValue value;
				auto ret = kfInfo.GetInterpolatedValue(autoResulock.Get(), time, value);
				if (!ret)
					return false;
				glm::mat3 matrix(glm::mat3_cast(glm::make_quat(&value.quaternion[0])));
				if (value.scale.has_value())
				{
					auto& s = value.scale.value();
					matrix[0] *= s[0];
					matrix[1] *= s[1];
					matrix[2] *= s[2];
				}
				dmat3x4 mat2;
				for (int i = 0; i < 3; i++)
					for (int j = 0; j < 3; j++)
						ColRow3x4(mat2, i, j) = matrix[i][j];

				double3 tr = { value.translation[0], value.translation[1], value.translation[2]};
				if (auto transform = transform_.lock())
					tr = transform->PositionToClient(tr);
				
				ColRow3x4(mat2, 0, 3) = tr[0];
				ColRow3x4(mat2, 1, 3) = tr[1];
				ColRow3x4(mat2, 2, 3) = tr[2];
				instance_->SetTransform(mat2);

				#ifdef DEBUGCULLING
					float3 col = { 1.f, 1.f, 1.f };
					instance_->SetColorShift(col);
				#endif
				return true;
			}
			return false;
		}
		
		const dmat3x4& GetTransform() const
		{
			return instance_->GetTransform();
		}

		void Update()
		{
			instance_->Update();
		}

		void Hide()
		{
#ifndef DEBUGCULLING
			dmat3x4 mat;
			for (int i = 0; i < 3; i++)
				for (int j = 0; j < 3; j++)
					ColRow3x4(mat, i, j) = 0.0f;
			ColRow3x4(mat, 0, 3) = 0.f;
			ColRow3x4(mat, 1, 3) = 0.f;
			ColRow3x4(mat, 2, 3) = 0.f;
			instance_->SetTransform(mat);
#else
			float3 col = { 1.f, 0.f, 0.f };
			instance_->SetColorShift(col);
#endif
			instance_->Update();
		}

		void RequestLoad(const TimeRange &timeRange)
		{
			auto autoKeyframeslock(keyframes_.GetRAutoLock());
			auto it = autoKeyframeslock->find(timeRange);
			if (it == autoKeyframeslock->end())
			{
				// Check if a request is not already in progress
				{
					auto loadInProgressLoked = loadInProgress_.GetAutoLock();
					auto pair = loadInProgressLoked->insert(timeRange);
					if (pair.second == false)
						return;
				}

				std::weak_ptr<InstanceWithPathExt> thisW(shared_from_this());
				auto keyframes = MakeSharedLockableData<IAnimationKeyframeInfo::TimelineResult>();
				auto kfInfoPtr = kfInfoPtr_.lock(); // lock shared_ptr
				if (!kfInfoPtr) //IAnimationKeyframeInfo has been deleted
					return;

				auto autolock(kfInfoPtr->GetRAutoLock());
				const auto& kfInfo = autolock.Get();
				std::string kjInfoId = (std::string)kfInfo.GetId();
				BE_LOGD("keyframeAnim", "AsyncQueryKeyframes(" << kjInfoId << "): timeRange:" << timeRange.begin << ", " << timeRange.end);
				kfInfo.AsyncQueryKeyframes(keyframes,
					[timeRange, thisW, kjInfoId=move(kjInfoId)](long httpRes, const TSharedLockableData<IAnimationKeyframeInfo::TimelineResult>& keyframes) {
						BE_LOGD("keyframeAnim", "AsyncQueryKeyframesEnd(" << kjInfoId << "): timeRange:" << timeRange.begin << ", " << timeRange.end);
						auto This(thisW.lock());
						if (This)
						{
							{
								auto loadInProgressLocked = This->loadInProgress_.GetAutoLock();
								loadInProgressLocked->erase(timeRange);
							}

							if (!(200 <= httpRes && httpRes < 300))
								return;
							This->AddKeyframes(timeRange, keyframes);
						}
					},
					timeRange.begin, timeRange.end - timeRange.begin);
			}
		}

		void AddKeyframes(const TimeRange& timeRange, const TimelineResultLockPtr &p)
		{
			auto lock = keyframes_.GetAutoLock();
			lock.Get().insert(std::make_pair(timeRange, p));
		}

		using Tools::TypeId<InstanceWithPathExt>::GetTypeId;

	private:
		IInstancePtr instance_;
		IAnimationKeyframeInfoWPtr kfInfoPtr_;
		Tools::RWLockableObject<TTimelineResults> keyframes_;
		Tools::RWLockableObject<std::set<TimeRange>> loadInProgress_;
		std::weak_ptr<IGCSTransform> transform_;
	};

	class KeyframeAnimator::Impl
	{
	public:
		IAnimationKeyframeWPtr animationKeyframe_;
		std::weak_ptr<IInstancesManager> instanceManager_;

		TSharedLockableData<std::pair<std::uint64_t /*queryId*/, std::set<IAnimationKeyframeInfo::Id>>> bboxInfoIds_;

		std::set<IAnimationKeyframeInfo::Id> infoIds_;

		std::uint64_t requestCounterG_ = 0;

		IKeyframeAnimator::Stat stat_;
		bool bStatEnable = false;

		double lastGetKeyframeInfoTime_ = -1.0f;
		double lastGetKeyframeInfoTime2_ = -1.0f;
		std::vector<BoundingBox> boundingBoxesTransformed_;
	};

	KeyframeAnimator::KeyframeAnimator(): impl_(new Impl)
	{
		GetImpl().bboxInfoIds_ = MakeSharedLockableData<std::pair<std::uint64_t /*queryId*/, std::set<IAnimationKeyframeInfo::Id>>>();
	}

	expected<void, std::string> KeyframeAnimator::AssociateInstances(const std::shared_ptr<IInstancesGroup>& gp)
	{
		auto animationKeyframePtr(GetImpl().animationKeyframe_.lock());
		if (!animationKeyframePtr)
			return make_unexpected("no animationKeyframe associated");

		auto instanceManager(GetImpl().instanceManager_.lock());
		if (!instanceManager)
			return make_unexpected("no instanceManager associated");

		if (!gp)
			return make_unexpected("no valid group");

		auto instances = gp->GetInstances();

		auto lock(animationKeyframePtr->GetAutoLock());
		IAnimationKeyframe& animationKeyframe = lock.Get();
		auto transform = animationKeyframe.GetGCSTransform();
		//const SharedInstVect& instances = GetImpl().population->GetInstanceManager()->GetInstancesByObjectRef(GetImpl().population->GetObjectRef(), GetImpl().population->GetInstancesGroup()->GetId());
		for (auto& it : instances)
		{
			auto inst = it.lock();
			if (inst)
			{
				auto& keyframeInfoId = inst->GetAnimId();
				auto animInfo = animationKeyframe.GetAnimationKeyframeInfo(IAnimationKeyframeInfo::Id(keyframeInfoId));
				auto lockInfo(animInfo->GetAutoLock());
				[[maybe_unused]] auto& info = lockInfo.Get();
				lockInfo->AddExtension(std::make_shared<InstanceWithPathExt>(inst, animInfo, transform));
			}
		}

		//BE_GETLOG("keyframeAnim")->SetLevel(AdvViz::SDK::Tools::Level::debug); //D-O-NOTC

		return {};
	}

	void KeyframeAnimator::OnResetTime()
	{
		GetImpl().lastGetKeyframeInfoTime_ = -1.0;
		GetImpl().lastGetKeyframeInfoTime2_ = -1.0;
	}

	void KeyframeAnimator::EnableStat(bool b)
	{
		GetImpl().bStatEnable = b;
	}

	const IKeyframeAnimator::Stat& KeyframeAnimator::GetStat() const
	{
		return GetImpl().stat_;
	}

	void KeyframeAnimator::QueryKeyFrameInfos(const std::vector<BoundingBox>& boundingBoxes, const TimeRange& timeRange, bool updateCurrentInfo)
	{
		auto animationKeyframePtr = GetImpl().animationKeyframe_.lock();
		if (!animationKeyframePtr)
			return;

		auto lock(animationKeyframePtr->GetRAutoLock());
		const IAnimationKeyframe& animationKeyframe = lock.Get();
		std::uint64_t& requestCounterG = GetImpl().requestCounterG_;
		std::uint64_t requestCounter = requestCounterG; requestCounterG++;
		TSharedLockableData<std::set<IAnimationKeyframeInfo::Id>> infoIds = MakeSharedLockableData<std::set<IAnimationKeyframeInfo::Id>>();
		auto bboxInfoIds = GetImpl().bboxInfoIds_;
		BE_LOGD("keyframeAnim", "AsyncQueryKeyframesInfos(" << requestCounter << ") " << " timeRange:" << timeRange.begin <<", " << timeRange.end);
		// retreive keyframesinfo that match timerange & boundingboxes
		auto ret = animationKeyframe.AsyncQueryKeyframesInfos(infoIds, [updateCurrentInfo, timeRange, animationKeyframePtr, requestCounter, bboxInfoIds](long httpRes, std::set<IAnimationKeyframeInfo::Id>& infoIds) {
			if (!(200 <= httpRes && httpRes < 300))
				return;
			BE_LOGD("keyframeAnim", "AsyncQueryKeyframesInfos(" << requestCounter << ") End");

			// for each keyframesinfo check if keyframe are not already in memory, else query them
			for (auto& infoId : infoIds)
			{
				GetTaskManager().AddTask([animationKeyframePtr, timeRange, infoId]() {
					auto timeRange2 = timeRange;
					auto lock(animationKeyframePtr->GetRAutoLock());
					const IAnimationKeyframe& animationKeyframe = lock.Get();
					auto info = animationKeyframe.GetAnimationKeyframeInfo(infoId);
					if (info)
					{
						auto lockInfo(info->GetRAutoLock());
						const IAnimationKeyframeInfo& animationKeyframeInfo = lockInfo.Get();
						float infoTimeEnd((float)animationKeyframeInfo.GetStartTime() + animationKeyframeInfo.GetKeyframeCount() * animationKeyframeInfo.GetKeyframeInterval());
						if (timeRange2.begin > infoTimeEnd)
							return;

						timeRange2.end = std::min(timeRange2.end, infoTimeEnd);
						if (auto ext = animationKeyframeInfo.GetExtension<InstanceWithPathExt>())
							ext->RequestLoad(timeRange);
					}
					}, ITaskManager::EType::background, updateCurrentInfo ? ITaskManager::EPriority::high : ITaskManager::EPriority::normal);
			}

			// swap infoIds
			if (updateCurrentInfo)
			{
				auto bboxInfoLocked = bboxInfoIds->GetAutoLock();
				if (requestCounter > bboxInfoLocked->first) // use it only if it's a recent one
				{
					bboxInfoLocked->second.swap(infoIds);
					bboxInfoLocked->first = requestCounter;
				}
			}

			}, boundingBoxes, timeRange);

		BE_ASSERT((bool)ret, std::string("AsyncQueryKeyframesInfos failed:") + ret.error());
	}

	inline void BoundingBoxAddPoint(BoundingBox& b, double3& p)
	{
		b.min[0] = std::min(p[0], b.min[0]);
		b.min[1] = std::min(p[1], b.min[1]);
		b.min[2] = std::min(p[2], b.min[2]);

		b.max[0] = std::max(p[0], b.max[0]);
		b.max[1] = std::max(p[1], b.max[1]);
		b.max[2] = std::max(p[2], b.max[2]);
	}

	expected<void, std::string> KeyframeAnimator::Process(float time, const std::vector<BoundingBox>& clientBoundingBoxes, bool /*cameraMoved*/)
	{
		if (clientBoundingBoxes.empty())
			return {};
		
		std::shared_ptr<IGCSTransform> transform;
		{
			auto animationKeyframePtr = GetImpl().animationKeyframe_.lock();
			if (!animationKeyframePtr)
				return make_unexpected("no animationKeyframe associated");
			auto lock(animationKeyframePtr->GetRAutoLock());
			const IAnimationKeyframe& animationKeyframe = lock.Get();

			transform = animationKeyframe.GetGCSTransform();
		}

		std::vector<BoundingBox> &boundingBoxesTransformed = GetImpl().boundingBoxesTransformed_;

		if (transform)
		{
			boundingBoxesTransformed.clear();
			boundingBoxesTransformed.reserve(clientBoundingBoxes.size());
			double3 p;
			for (const auto& bbox : clientBoundingBoxes)
			{
				BoundingBox b;
				p = { bbox.min[0], bbox.min[1], bbox.min[2]};
				p = transform->PositionFromClient(p);
				BoundingBoxAddPoint(b, p);
				p = { bbox.max[0], bbox.max[1], bbox.max[2]};
				p = transform->PositionFromClient(p);
				BoundingBoxAddPoint(b, p);
				boundingBoxesTransformed.push_back(b);
			}
		}
		else
		{
			boundingBoxesTransformed = clientBoundingBoxes;
		}

		if (time > GetImpl().lastGetKeyframeInfoTime_ + 1.0)
		{
			GetImpl().lastGetKeyframeInfoTime_ = time;

			BE_LOGD("keyframeAnim", "AsyncQueryKeyframesInfos time:" << time);

			// query current with time
			TimeRange timeRange1;
			{
				timeRange1.begin = std::floor((time+1.0f) / 20.0f) * 20.0f;
				timeRange1.end = timeRange1.begin + 20.0f;
				QueryKeyFrameInfos(boundingBoxesTransformed, timeRange1, true /*updateCurrentInfo*/);
			}

			// query futur : current time + 10s
			if (time > GetImpl().lastGetKeyframeInfoTime2_ + 5.0)
			{
				GetImpl().lastGetKeyframeInfoTime2_ = time;
				TimeRange timeRange;
				timeRange.begin = std::ceil((time + 10.0f) / 20.0f) * 20.0f;
				timeRange.end = timeRange.begin + 20.0f;
				if (std::fabs(timeRange.begin - timeRange1.begin) > 1.0f)
					QueryKeyFrameInfos(boundingBoxesTransformed, timeRange, false /*updateCurrentInfo*/);
			}
				
		}


		auto animationKeyframePtr = GetImpl().animationKeyframe_.lock();
		if (!animationKeyframePtr)
			return make_unexpected("no animationKeyframe associated");
		auto lock(animationKeyframePtr->GetRAutoLock());
		const IAnimationKeyframe& animationKeyframe = lock.Get();

		bool bStatEnable = GetImpl().bStatEnable;
		auto& stat = GetImpl().stat_;

		if (bStatEnable)
			stat.numberPerBbox.clear();

		auto infosIdLock = GetImpl().bboxInfoIds_->GetRAutoLock();
		GetImpl().infoIds_.insert(infosIdLock.Get().second.begin(), infosIdLock.Get().second.end());
		if (bStatEnable)
			stat.numberPerBbox.push_back((unsigned)infosIdLock.Get().second.size());

		//const std::shared_ptr<const std::vector<BoundingBox>> bboxPtr = std::make_shared<std::vector<BoundingBox>>(clientBoundingBoxes);

		stat.beforeCullingItems = (unsigned)GetImpl().infoIds_.size();
		stat.itemsHidden = 0;
		// update populations
		for (auto it = GetImpl().infoIds_.begin(); it != GetImpl().infoIds_.end();)
		{
			const auto& infoId = *it;
			auto info = animationKeyframe.GetAnimationKeyframeInfo(IAnimationKeyframeInfo::Id(infoId));
			if (!info)
				continue;
			auto lockInfo(info->GetRAutoLock());
			if (auto ext = lockInfo->GetExtension<InstanceWithPathExt>())
			{
				bool erase = [this, ext, time, &clientBoundingBoxes, infoId]() {
					bool validTrans = ext->ProcessTransform(time);
					if (!validTrans) // we keep the item because it's probably just not ready (download in progress)
					{
						ext->Hide();
						return false;
					}
					const dmat3x4& trans = ext->GetTransform(); //is in client coordinate system
					double3 pos = { ColRow3x4(trans, 0, 3), ColRow3x4(trans, 1, 3), ColRow3x4(trans, 2, 3) };
					for (auto& bb : clientBoundingBoxes)
						if (bb.Contains(pos))
						{
							ext->Update();
							return false;
						}
					ext->Hide();
					return true;					
				}();
				if (erase)
				{
					it = GetImpl().infoIds_.erase(it);
					stat.itemsHidden++;
				}
				else
					++it;
			}
		}

		if (bStatEnable)
		{
			stat.numberVisibleItems = (unsigned)GetImpl().infoIds_.size();
			std::stringstream s;
			for (unsigned i = 0; i < stat.numberPerBbox.size(); ++i)
				s << "\n" << i << ":" << stat.numberPerBbox[i];
			BE_LOGD("keyframeAnim", "Stats:" << s.str() << "\n" << " itemsHidden:" << stat.itemsHidden << " numberVisibleItems" << stat.numberVisibleItems << " beforeCulling:" << stat.beforeCullingItems);
		}

		return {};
	}

	void KeyframeAnimator::SetAnimation(const IAnimationKeyframePtr& animationKeyframe)
	{
		GetImpl().animationKeyframe_ = animationKeyframe;
	}

	IAnimationKeyframePtr KeyframeAnimator::GetAnimation() const
	{
		return GetImpl().animationKeyframe_.lock();
	}

	void KeyframeAnimator::SetInstanceManager(const std::shared_ptr<IInstancesManager>& instanceManager)
	{
		GetImpl().instanceManager_ = instanceManager;
	}

	const KeyframeAnimator::Id& KeyframeAnimator::GetId() const
	{
		static KeyframeAnimator::Id id;
		return id;
	}

	KeyframeAnimator::Impl& KeyframeAnimator::GetImpl()
	{
		return *impl_;
	}

	const KeyframeAnimator::Impl& KeyframeAnimator::GetImpl() const
	{
		return *impl_;
	}

	DEFINEFACTORYGLOBALS(KeyframeAnimator);

}