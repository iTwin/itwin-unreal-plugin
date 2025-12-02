/*--------------------------------------------------------------------------------------+
|
|     $Source: Timeline.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "Timeline.h"
#include "Core/Network/HttpGetWithLink.h"
#include "Core/Singleton/singleton.h"
#include "Config.h"

namespace AdvViz::SDK
{
	////////////////////////////////////////////////////////////////////// TimelineKeyframe /////////////////////////////////////
	struct TimelineKeyframe::Impl
	{
		KeyframeData keyframeData;
		std::optional<std::string> snapshotId;
		bool changed = false;
	};

	void TimelineKeyframe::InternalCreate(const KeyframeData& data, bool markAsChanged)
	{
		GetImpl().keyframeData = data;
		GetImpl().keyframeData.time = RoundTime(data.time); 
		GetImpl().changed = markAsChanged;
	}

	void TimelineKeyframe::Update(const KeyframeData& data)
	{
		// note: time is immutable
		double oldtime = GetImpl().keyframeData.time;
		auto id = GetImpl().keyframeData.id;
		GetImpl().keyframeData = data;
		GetImpl().keyframeData.time = oldtime;
		GetImpl().keyframeData.id = id;
		GetImpl().changed = true;
	}

	const TimelineKeyframe::KeyframeData& TimelineKeyframe::GetData() const
	{
		return GetImpl().keyframeData;
	}

	bool TimelineKeyframe::Changed() const
	{
		return GetImpl().changed;
	}

	bool TimelineKeyframe::ShouldSave() const
	{
		return GetImpl().changed /*|| !GetImpl().keyframeData.id*/;
	}

	void TimelineKeyframe::SetShouldSave(bool value)
	{
		GetImpl().changed = value;
	}

	bool TimelineKeyframe::CompareForOrder(ITimelineKeyframe* b) const
	{
		return GetData().time < b->GetData().time;
	}

	TimelineKeyframe::~TimelineKeyframe()
	{}

	TimelineKeyframe::TimelineKeyframe():impl_(new Impl())
	{}

	TimelineKeyframe::Impl& TimelineKeyframe::GetImpl()
	{
		return *impl_;
	}

	const TimelineKeyframe::Impl& TimelineKeyframe::GetImpl() const
	{
		return *impl_;
	}

	const ITimelineKeyframe::Id& TimelineKeyframe::GetId() const {
		thread_local static ITimelineKeyframe::Id id;

		if (GetImpl().keyframeData.id)
			id = ITimelineKeyframe::Id(*GetImpl().keyframeData.id);
		
		return id;
	}


	void TimelineKeyframe::SetSnapshotId(const std::string& Id)
	{
		GetImpl().snapshotId = Id;
	}

	std::string TimelineKeyframe::GetSnapshotId() const
	{
		return GetImpl().snapshotId ? *(GetImpl().snapshotId) : std::string();
	}

	template<>
	Tools::Factory<ITimelineKeyframe>::Globals::Globals()
	{
		newFct_ = []() {
			ITimelineKeyframe* p(static_cast<ITimelineKeyframe*>(new TimelineKeyframe()));
			return p;
			};
	}

	template<>
	Tools::Factory<ITimelineKeyframe>::Globals& Tools::Factory<ITimelineKeyframe>::GetGlobals()
	{
		return singleton<Tools::Factory<ITimelineKeyframe>::Globals>();
	}

	////////////////////////////////////////////////////////////////////// TimelineClip /////////////////////////////////////

	bool operator<(const std::shared_ptr<ITimelineKeyframe>& fk, const double& lk) { return fk->GetData().time < lk; }
	bool operator<(const double& lk, const std::shared_ptr<ITimelineKeyframe>& fk) { return lk < fk->GetData().time; }
	bool operator<(const std::shared_ptr<ITimelineKeyframe>& a, const std::shared_ptr<ITimelineKeyframe>& b) { return a->CompareForOrder(b.get()); }

	struct TimelineClip::Impl
	{
		struct ServerSideData {
			std::string name;
			bool enable = true;
			std::vector<std::string> keyFrameIds;
			std::optional<std::string> id;
		};
		ServerSideData serverSideData_;
		std::set<std::shared_ptr<ITimelineKeyframe>, std::less<>> keyframes_;
		std::string sceneId_;
		std::shared_ptr<Http> http_;
		bool shouldSave_ = false;
		std::vector<std::shared_ptr<ITimelineKeyframe>> toDeleteKeyframes_;
		std::optional<std::string> snapshotId;

		expected<void, std::string> Load(std::shared_ptr<Http>& http, const std::string& sceneId, const ITimelineClip::Id& timelineClipId)
		{
			sceneId_ = sceneId;
			serverSideData_.id = static_cast<const std::string>(timelineClipId);
			http_ = http;
			std::string url = "scenes/" + sceneId_ + "/timelineClips/" + static_cast<const std::string>(timelineClipId);

			if (http->GetJson(serverSideData_, url) == 200)
			{
				for (auto& i : serverSideData_.keyFrameIds)
				{
					std::string urlkeys = "scenes/" + sceneId_ + "/timelineKeyFrames/" + i;
					ITimelineKeyframe::KeyframeData data;
					if (http->GetJson(data, urlkeys) == 200)
					{
						auto p = std::shared_ptr<ITimelineKeyframe>(ITimelineKeyframe::New());
						p->InternalCreate(data, false);
						keyframes_.insert(p);
					}
					else
					{
						return make_unexpected(std::string("http failed: ") + urlkeys);
					}
				}
				shouldSave_ = false;
			}
			else
			{
				return make_unexpected(std::string("http failed: ") + url);
			}

			return {};
		}

		expected<void, std::string> Save(std::shared_ptr<Http>& http, const std::string& sceneId)
		{
			sceneId_ = sceneId;

			// save framekeys in batch
			auto ret = [this, &http]() -> expected<void, std::string>
			{
				struct SJin {
					std::vector<ITimelineKeyframe::KeyframeData> timelineKeyFrames;
				};
				SJin keyframesToPost;
				SJin keyframesToPut;
				typedef SJin SJout;
				SJout keyframesOut;
				std::vector<std::shared_ptr<ITimelineKeyframe>> timelineKeyframesToUpdate;

				for (auto& k : keyframes_)
				{
					auto& keyData = k->GetData();
					if (keyData.id)
					{
						if (k->Changed())
							keyframesToPut.timelineKeyFrames.push_back(keyData);
					}
					else
					{
						keyframesToPost.timelineKeyFrames.push_back(keyData);
						timelineKeyframesToUpdate.push_back(k);
					}
				}

				std::string url = "scenes/" + sceneId_ + "/timelineKeyFrames";

				if (!keyframesToPut.timelineKeyFrames.empty())
				{
					struct Sout
					{
						int numUpdated = 0;
					};
					Sout jout;
					if (http->PutJsonJBody(jout, url, keyframesToPut) != 200)
					{
						return make_unexpected(std::string("http failed: ") + url);
					}
				}
				
				keyframesOut.timelineKeyFrames.clear();
				if (!keyframesToPost.timelineKeyFrames.empty())
				{
					if (http->PostJsonJBody(keyframesOut, url, keyframesToPost) == 201)
					{
						BE_ASSERT(keyframesOut.timelineKeyFrames.size() == keyframesToPost.timelineKeyFrames.size());
						size_t loopCount = std::min(keyframesOut.timelineKeyFrames.size(), keyframesToPost.timelineKeyFrames.size());
						for (size_t i = 0; i < loopCount; ++i)
						{
							timelineKeyframesToUpdate[i]->InternalCreate(keyframesOut.timelineKeyFrames[i], false);
						}
					}
					else
					{
						return make_unexpected(std::string("http failed: ") + url);
					}
				}
				return expected<void, std::string>();
				}();

			if (!ret)
				return ret;

			// delete keys
			ret = [this, &http]() -> expected<void, std::string>
				{
					struct SJin {
						std::vector<std::string> ids;
					};
					SJin keyframesToDelete;
					typedef SJin SJout;
					SJout keyframesOut;
					for (auto& k : toDeleteKeyframes_)
					{
						auto& keyData = k->GetData();
						if (keyData.id)
							keyframesToDelete.ids.push_back(static_cast<const std::string>(*keyData.id));
					}

					std::string url = "scenes/" + sceneId_ + "/timelineKeyFrames";
					if (!keyframesToDelete.ids.empty())
						if (http->DeleteJsonJBody(keyframesOut, url, keyframesToDelete) != 200)
							return make_unexpected(std::string("http failed: ") + url);

					toDeleteKeyframes_.clear();
					return {};
				}();

			if (!ret)
				return ret;

			if (shouldSave_)
			{
				std::string url = "scenes/" + sceneId_ + "/timelineClips";

				serverSideData_.keyFrameIds.clear();
				for (auto& clip : keyframes_)
					serverSideData_.keyFrameIds.push_back(static_cast<const std::string>(clip->GetId()));

				struct SJin {
					std::array<ServerSideData, 1> timelineClips;
				};
				SJin jin;
				jin.timelineClips[0] = serverSideData_;

				if (!serverSideData_.id.has_value())
				{
					SJin jout;
					if (http->PostJsonJBody(jout, url, jin) == 201)
					{
						BE_ASSERT(jout.timelineClips[0].id.has_value());
						serverSideData_.id = jout.timelineClips[0].id;
					}
					else
					{
						return make_unexpected(std::string("http failed: ") + url);
					}
				}
				else
				{
					struct SJout
					{
						int numUpdated = 0;
					};
					SJout jout;
					if (http->PutJsonJBody(jout, url, jin) != 200)
					{
						return make_unexpected(std::string("http failed: ") + url);
					}
				}
			}
			shouldSave_ = false;
			return {};
		}

		bool ShouldSave() const
		{
			if (/*!serverSideData_.id ||*/ shouldSave_)
				return true;

			for (auto& kf : keyframes_)
				if (kf->ShouldSave())
					return true;
			return false;
		}

		void SetShouldSave(bool value)
		{
			shouldSave_ = value;
			for (auto& kf : keyframes_)
				kf->SetShouldSave(value);
		}
	};

	expected<void, std::string> TimelineClip::Load(const std::string& sceneId, const ITimelineClip::Id& timelineClipId)
	{
		std::shared_ptr<Http>& http = GetDefaultHttp();
		return GetImpl().Load(http, sceneId, timelineClipId);
	}

	expected<void, std::string> TimelineClip::Save(const std::string& sceneId)
	{
		std::shared_ptr<Http>& http = GetDefaultHttp();
		return GetImpl().Save(http, sceneId);
	}


	expected<std::shared_ptr<ITimelineKeyframe>, std::string> TimelineClip::GetKeyframe(double time) const
	{
		time = RoundTime(time);
		auto it = GetImpl().keyframes_.find(time);
		if (it == GetImpl().keyframes_.end())
			return make_unexpected(std::string("Keyframe not found"));
		return *it;
	}

	expected<std::shared_ptr<ITimelineKeyframe>, std::string> TimelineClip::GetKeyframeByIndex(size_t index) const
	{
		if (index >= GetImpl().keyframes_.size())
			return make_unexpected(std::string("Bad index for Keyframes"));

		auto it = GetImpl().keyframes_.begin();
		std::advance(it, index);
		return *it;
	}

	expected<size_t, std::string> TimelineClip::GetKeyframeIndex(double time) const
	{
		time = RoundTime(time);
		auto it = GetImpl().keyframes_.find(time);
		if (it == GetImpl().keyframes_.end())
			return make_unexpected(std::string("Keyframe not found"));
		return std::distance(GetImpl().keyframes_.begin(), it);
	}

	size_t TimelineClip::GetKeyframeCount() const
	{
		return GetImpl().keyframes_.size();
	}

	template<>
	Tools::Factory<ITimeline>::Globals::Globals()
	{
		newFct_ = []() {
			ITimeline* p(static_cast<ITimeline*>(new Timeline()));
			return p;
			};
	}

	template<>
	Tools::Factory<ITimeline>::Globals& Tools::Factory<ITimeline>::GetGlobals()
	{
		return singleton<Tools::Factory<ITimeline>::Globals>();
	}

	TimelineClip::Impl& TimelineClip::GetImpl() {
		return *impl_;
	}

	const TimelineClip::Impl& TimelineClip::GetImpl() const
	{
		return *impl_;
	}

	TimelineClip::TimelineClip() :impl_(new Impl())
	{}

	TimelineClip::~TimelineClip()
	{}

	const TimelineClip::Id& TimelineClip::GetId() const {
		thread_local static TimelineClip::Id id;
		if (GetImpl().serverSideData_.id)
			id = TimelineClip::Id(*GetImpl().serverSideData_.id);
		else
			id.Reset();
		return id;
	}
	void TimelineClip::SetId(const ITimelineClip::Id& id)
	{
		GetImpl().serverSideData_.id = static_cast<const std::string>(id);
	}

	const std::string& TimelineClip::GetName() const
	{
		return GetImpl().serverSideData_.name;
	}

	void TimelineClip::SetName(const std::string& name)
	{
		GetImpl().serverSideData_.name = name;
		GetImpl().shouldSave_ = true;
	}

	bool TimelineClip::IsEnabled() const
	{
		return GetImpl().serverSideData_.enable;
	}

	void TimelineClip::SetEnable(bool e)
	{
		GetImpl().serverSideData_.enable = e;
	}

	void TimelineClip::SetSnapshotId(const std::string& Id)
	{
		GetImpl().snapshotId = Id;
	}

	std::string TimelineClip::GetSnapshotId() const
	{
		return GetImpl().snapshotId ? *(GetImpl().snapshotId) : std::string();
	}

	void TimelineClip::GetKeyFrameSnapshotIds(std::vector<std::string> &Ids) const
	{
		for (auto& kf : GetImpl().keyframes_)
			Ids.push_back(kf->GetSnapshotId());
	}

	expected<std::shared_ptr<ITimelineKeyframe>, std::string> TimelineClip::AddKeyframe(const ITimelineKeyframe::KeyframeData& data)
	{
		std::shared_ptr<ITimelineKeyframe> p(ITimelineKeyframe::New());
		p->InternalCreate(data, true);
		auto it = GetImpl().keyframes_.insert(p);
		if (!it.second)
			return make_unexpected(std::string("Keyframe already exists"));
		auto dit = std::find_if(GetImpl().toDeleteKeyframes_.begin(), GetImpl().toDeleteKeyframes_.end(), 
		[p](const std::shared_ptr<AdvViz::SDK::ITimelineKeyframe>& other)
			{
				return p->GetData().id == other->GetData().id;
			}
		);
		if (dit != GetImpl().toDeleteKeyframes_.end()) // moving key frame by removing and adding again, need to ensure the deleted is clear
		{
			GetImpl().toDeleteKeyframes_.erase(dit);
		}
		GetImpl().shouldSave_ = true;
		return p;
	}

	expected<void, std::string> TimelineClip::RemoveKeyframe(std::shared_ptr<ITimelineKeyframe> &k)
	{
		auto it = GetImpl().keyframes_.find(k);
		if (it == GetImpl().keyframes_.end())
			return make_unexpected(std::string("Keyframe not found"));
		GetImpl().toDeleteKeyframes_.push_back(*it);
		GetImpl().keyframes_.erase(it);
		GetImpl().shouldSave_ = true;
		return {};
	}

	bool TimelineClip::ShouldSave() const {
		return GetImpl().ShouldSave();
	}

	void TimelineClip::SetShouldSave(bool value)
	{
		return GetImpl().SetShouldSave(value);

	}


	////////////////////////////////////////////////////////////////////// Timeline /////////////////////////////////////

	struct Timeline::Impl
	{
		struct ServerSideData {
			std::string name;
			std::vector<std::string> clipIds;
			std::optional<std::string> id;
		};
		ServerSideData serverSideData_;
		std::list<std::shared_ptr<ITimelineClip>> clips_;
		std::string sceneId_;
		std::shared_ptr<Http> http_;
		bool shouldSave_ = false;
		std::vector<std::shared_ptr<ITimelineClip>> toDeleteClips_;

		std::shared_ptr<ITimelineClip> AddClip(const std::string &name)
		{
			auto p = std::shared_ptr<ITimelineClip>(ITimelineClip::New());
			p->SetName(name);
			clips_.push_back(p);
			shouldSave_ = true;
			return p;
		}

		expected<void, std::string> Load(std::shared_ptr<Http>& http, const std::string& sceneId, const ITimeline::Id& timelineId)
		{
			sceneId_ = sceneId;
			serverSideData_.id = static_cast<const std::string>(timelineId);
			http_ = http;
			std::string url = "scenes/" + sceneId_ + "/timelines/" + static_cast<const std::string>(timelineId);
			ServerSideData data;
			if (http->GetJson(data, url) == 200)
			{
				for (const auto& clipId : data.clipIds)
				{
					auto p = std::shared_ptr<ITimelineClip>(ITimelineClip::New());
					p->Load(sceneId, ITimelineClip::Id(clipId));
					clips_.push_back(p);
				}
				BE_LOGI("ITwinDecoration", "Timeline loaded "<< data.clipIds.size() <<" clips " );
				shouldSave_ = false;
			}
			else
			{
				return make_unexpected(std::string("http failed: ") + url);
			}
				
			return {};
		}

		expected<void, std::string> Save(std::shared_ptr<Http>& http, const std::string& sceneId)
		{
			sceneId_ = sceneId;

			std::string url = "scenes/" + sceneId_ + "/timelines";

			serverSideData_.clipIds.clear();
			for (auto& clip : clips_)
			{
				if (clip->GetKeyframeCount() > 0)
				{
					clip->Save(sceneId);
					serverSideData_.clipIds.push_back(static_cast<const std::string>(clip->GetId()));
				}
				else
				{
					toDeleteClips_.push_back(clip);
				}
			}

			// delete clips
			auto ret = [this, &http]() -> expected<void, std::string>
			{
				struct SJin {
					std::vector<std::string> ids;
				};
				SJin clipsToDelete;
				typedef SJin SJout;
				SJout clipsOut;
				for (auto& c: toDeleteClips_)
				{
					if (c->GetId().IsValid())
						clipsToDelete.ids.push_back(static_cast<const std::string>(c->GetId()));
					c->SetShouldSave(false);
				}

				std::string url = "scenes/" + sceneId_ + "/timelineClips";
				if (!clipsToDelete.ids.empty())
					if (http->DeleteJsonJBody(clipsOut, url, clipsToDelete) != 200)
						return make_unexpected(std::string("http failed: ") + url);

				BE_LOGI("ITwinDecoration","Timeline save : deleted "<<toDeleteClips_.size()<<" clips");
				toDeleteClips_.clear();
				return {};
			}();

			if (!ret)
				return ret;

			if (shouldSave_)
			{
				struct SJin {
					std::array<ServerSideData, 1> timelines;
				};
				SJin jin;
				jin.timelines[0] = serverSideData_;

				long status = 0;
				if (!serverSideData_.id.has_value())
				{
					SJin jout;
					status = http->PostJsonJBody(jout, url, jin);
					if (status == 201)
					{
						BE_ASSERT(jout.timelines[0].id.has_value());
						if (jout.timelines[0].id)
							serverSideData_.id = jout.timelines[0].id;
						else
							return make_unexpected(std::string("Server returned no id value for saved timeline."));

						BE_LOGI("ITwinDecoration", "Timeline saved new clip " << jin.timelines[0].name);

					}
					else
					{
						return make_unexpected(fmt::format("http failed: {} with status {}", url, status));
					}
				}
				else
				{
					struct Sout
					{
						int numUpdated = 0;
					};
					Sout jout;
					status = http->PutJsonJBody(jout, url, jin);
					if (status != 200)
					{
						return make_unexpected(fmt::format("http failed: {} with status {}", url, status));
					}
					BE_LOGI("ITwinDecoration", "Timeline saved new clip " << jin.timelines[0].name);
					BE_ASSERT(jout.numUpdated == 1);
				}
			}
			shouldSave_ = false;
			return {};
		}

		bool ShouldSave() const
		{
			if (shouldSave_)
				return true;
			for (auto& clip : clips_)
				if (clip && clip->ShouldSave())
					return true;
			return false;
		}
		void SetShouldSave(bool value)
		{
			shouldSave_ = value;
			for (auto& clip : clips_)
				clip->SetShouldSave(value);
		}
	};

	expected<void, std::string> Timeline::Load(const std::string& sceneId, const ITimeline::Id& timelineId)
	{
		std::shared_ptr<Http>& http = GetDefaultHttp();
		return GetImpl().Load(http, sceneId, timelineId);
	}

	expected<void, std::string> Timeline::Save(const std::string& sceneId)
	{
		std::shared_ptr<Http>& http = GetDefaultHttp();
		return GetImpl().Save(http, sceneId);
	}

	bool Timeline::ShouldSave() const
	{
		return GetImpl().ShouldSave();
	}

	std::shared_ptr<ITimelineClip> Timeline::AddClip(const std::string& name)
	{
		return GetImpl().AddClip(name);
	}

	expected<void, std::string> Timeline::RemoveClip(size_t index)
	{
		if (index >= GetImpl().clips_.size())
			return make_unexpected(std::string("Bad index for Timeline Clips"));

		auto it = GetImpl().clips_.begin();
		std::advance(it, index);
		GetImpl().toDeleteClips_.push_back(*it);
		GetImpl().clips_.erase(it);
		GetImpl().shouldSave_ = true;
		return {};
	}

	expected<std::shared_ptr<ITimelineClip>, std::string> Timeline::GetClipByIndex(size_t index) const
	{
		if (index >= GetImpl().clips_.size())
			return make_unexpected(std::string("Bad index for Timeline Clips"));

		auto it = GetImpl().clips_.begin();
		std::advance(it, index);
		return *it;
	}

	size_t Timeline::GetClipCount() const
	{
		return GetImpl().clips_.size();
	}

	void Timeline::MoveClip(size_t indexSrc, size_t indexDst)
	{
		if (indexSrc < 0 || indexSrc >= GetImpl().clips_.size() || indexDst < 0 || indexDst >= GetImpl().clips_.size())
			return;

		auto it = std::next(GetImpl().clips_.begin(), indexSrc);
		auto clipPtr = *it;
		GetImpl().clips_.erase(it);

		if (indexDst == GetImpl().clips_.size())
			GetImpl().clips_.push_back(clipPtr);
		else
		{
			indexDst = std::min(indexDst, GetImpl().clips_.size()-1);
			it = std::next(GetImpl().clips_.begin(), indexDst);
			GetImpl().clips_.insert(it, clipPtr);
		}
	}

	template<>
	Tools::Factory<ITimelineClip>::Globals::Globals()
	{
		newFct_ = []() {
			ITimelineClip* p(static_cast<ITimelineClip*>(new TimelineClip()));
			return p;
			};
	}

	template<>
	Tools::Factory<ITimelineClip>::Globals& Tools::Factory<ITimelineClip>::GetGlobals()
	{
		return singleton<Tools::Factory<ITimelineClip>::Globals>();
	}

	Timeline::Impl& Timeline::GetImpl() {
		return *impl_;
	}

	const Timeline::Impl& Timeline::GetImpl() const {
		return *impl_;
	}

	Timeline::Timeline() :impl_(new Impl())
	{}

	Timeline::~Timeline()
	{}

	const Timeline::Id& Timeline::GetId() const {
		thread_local static Timeline::Id id;

		if (GetImpl().serverSideData_.id)
			id = Timeline::Id(*GetImpl().serverSideData_.id);
		else
			id.Reset();
		return id;
	}
	void Timeline::SetId(const ITimeline::Id& id)
	{
		GetImpl().serverSideData_.id = static_cast<const std::string>(id);
	}

	void Timeline::SetShouldSave(bool value)
	{
		return GetImpl().SetShouldSave(value);
	}

	std::vector<std::shared_ptr<AdvViz::SDK::ITimelineClip>> Timeline::GetObsoleteClips() const
	{
		return GetImpl().toDeleteClips_;
	}

	void Timeline::RemoveObsoleteClip(const std::shared_ptr<ITimelineClip>& clipp)
	{
		GetImpl().toDeleteClips_.erase(
			std::remove_if(GetImpl().toDeleteClips_.begin(), GetImpl().toDeleteClips_.end(), [clipp](auto& vclip) {return clipp == vclip; }),
			GetImpl().toDeleteClips_.end());
	}

	expected<std::vector<SSceneTimelineInfo>, std::string> GetSceneTimelines(const std::string& sceneId)
	{
		std::shared_ptr<Http>& http = GetDefaultHttp();

		struct SJout {
			std::string name;
			std::vector<std::string> clipIds;
			std::optional<std::string> id;
		};
		SJout jout;

		std::string url = "scenes/" + sceneId + "/timelines";
		std::vector<SSceneTimelineInfo> timelineIds;

		auto ret = HttpGetWithLink<SJout>(http, url, {},
			[&timelineIds](SJout& data) -> expected<void, std::string> {
				if (!data.id)
					return make_unexpected(std::string("Server returned no id value."));
				SSceneTimelineInfo info = { data.name, ITimeline::Id(*data.id) };
				timelineIds.push_back(info);
				return {};
			});
		if (!ret)
			return make_unexpected(std::string("GetSceneTimelines failed.\nPrevious error:") + ret.error());

		return timelineIds;
	}

	expected<ITimeline::Id, std::string> AddSceneTimeline(const std::string& sceneId, const std::string& sceneName)
	{
		std::shared_ptr<Http>& http = GetDefaultHttp();
		struct ServerSideData {
			std::string name;
			std::vector<std::string> clipIds;
			std::optional<std::string> id;
		};
		struct SJin {
			std::array<ServerSideData, 1> timelines;
		};
		SJin jin;
		typedef SJin SJout;
		SJout jout;
		ITimeline::Id id;
		jin.timelines[0].name = sceneName;
		std::string url = "scenes/" + sceneId + "/timelines";

		if (http->PostJsonJBody(jout, url, jin) == 201)
		{
			if (jout.timelines[0].id)
				id = ITimeline::Id(*jout.timelines[0].id);
			else
				return make_unexpected(std::string("Server returned no id value."));
		}
		else
		{
			return make_unexpected(std::string("AddSceneTimeline http post failed."));
		}
		return id;
	}



}