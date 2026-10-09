/*--------------------------------------------------------------------------------------+
|
|     $Source: Timeline.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "Timeline.h"
#include "Core/Network/HttpGetWithLink.h"
#include "Core/Singleton/singleton.h"
#include "Core/Visualization/AsyncHelpers.h"
#include "Core/Visualization/AsyncHttp.inl"
#include "Config.h"

namespace AdvViz::SDK
{
	namespace
	{
		struct SJsonIds
		{
			std::vector<std::string> ids;
		};
	}


	template <typename TServerData>
	struct TServerDataProps
	{

	};

	// Create one timeline/clip on the server.
	template <typename TSavableItem, typename TSJin>
	void TCreateSingleItemOnServer(
		TSavableItem* itemPtr,
		std::string const& url,
		TSJin const& jin,
		std::shared_ptr<Http> const& http,
		std::shared_ptr<AsyncRequestGroupCallback> callbackPtr)
	{
		using TSJout = TSJin;
		using ServerDataPropsAccess = TServerDataProps<TSJin>;

		std::weak_ptr<TSavableItem> itemWptr(itemPtr->shared_from_this());

		AsyncPostJsonJBody<TSJout>(http, callbackPtr,
			[url, itemWptr,
			 itemName = ServerDataPropsAccess::GetName(jin)](
				long httpCode,
				const Tools::TSharedLockableData<TSJout>& joutPtr)
		{
			auto itemPtr = itemWptr.lock();
			if (!itemPtr)
				return false;

			bool bSuccess = (httpCode == 201);
			if (bSuccess)
			{
				auto unlockedJout = joutPtr->GetAutoLock();
				TSJout const& jout = unlockedJout.Get();

				auto const& idOnServer = ServerDataPropsAccess::GetId(jout);
				bSuccess = idOnServer.has_value();
				if (bSuccess)
				{
					if (!idOnServer->empty())
					{
						itemPtr->SetDBIdentifier(idOnServer.value());
					}
					itemPtr->serverSideData_.id = idOnServer;
					itemPtr->OnSaved();
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Server returned no id for timeline " << itemName
						<< " url: " << url);
				}
			}
			else
			{
				BE_LOGW("ITwinDecoration", "Save timeline " << itemName << " failed."
					<< " url: " << url
					<< " Http status: " << httpCode);
			}
			return bSuccess;
		}, url, jin);
	}

	// Update one existing timeline/clip on the server.
	template <typename TSavableItem, typename TSJin>
	void TUpdateSingleItemOnServer(
		TSavableItem* itemPtr,
		std::string const& url,
		TSJin const& jin,
		std::shared_ptr<Http> const& http,
		std::shared_ptr<AsyncRequestGroupCallback> callbackPtr)
	{
		struct Sout
		{
			int numUpdated = 0;
		};

		using ServerDataPropsAccess = TServerDataProps<TSJin>;

		std::weak_ptr<TSavableItem> itemWptr(itemPtr->shared_from_this());

		AsyncPutJsonJBody<Sout>(http, callbackPtr,
			[itemWptr, url,
			itemName = ServerDataPropsAccess::GetName(jin)](
				long httpCode,
				const Tools::TSharedLockableData<Sout>& joutPtr)
		{
			auto itemPtr = itemWptr.lock();
			if (!itemPtr)
				return false;
			bool bSuccess = (httpCode == 200);
			if (bSuccess)
			{
				auto unlockedJout = joutPtr->GetAutoLock();
				Sout const& jout = unlockedJout.Get();
				bSuccess = (jout.numUpdated == 1);
				if (bSuccess)
				{
					BE_LOGI("ITwinDecoration", "Updated timeline " << itemName);
					itemPtr->OnSaved();
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Update timeline " << itemName << " failed."
						<< " url: " << url
						<< " Http status: " << httpCode);
				}
			}
			else
			{
				BE_LOGW("ITwinDecoration", "Update timeline " << itemName << " failed."
					<< " url: " << url
					<< " Http status: " << httpCode);
			}
			return bSuccess;
		}, url, jin);
	}

	template <typename TSavableItem, typename TSJin>
	void TCreateOrUpdateSingleItemOnServer(
		TSavableItem* itemPtr,
		std::string const& url,
		TSJin const& jin,
		std::shared_ptr<Http> const& http,
		std::shared_ptr<AsyncRequestGroupCallback> callbackPtr)
	{
		using ServerDataPropsAccess = TServerDataProps<TSJin>;

		itemPtr->OnStartSave();

		if (!ServerDataPropsAccess::GetId(jin).has_value())
		{
			TCreateSingleItemOnServer(itemPtr, url, jin, http, callbackPtr);
		}
		else
		{
			TUpdateSingleItemOnServer(itemPtr, url, jin, http, callbackPtr);
		}
	}

	////////////////////////////////////////////////////////////////////// TimelineKeyframe /////////////////////////////////////
	struct TimelineKeyframe::Impl final : public SavableItemWithID
	{
		KeyframeData keyframeData;
		std::optional<std::string> snapshotId;

		void SetId(const RefID& id) override
		{
			SavableItemWithID::SetId(id);
			if (id.HasDBIdentifier())
			{
				keyframeData.id = id.GetDBIdentifier();
			}
		}
	};

	void TimelineKeyframe::InternalCreate(const KeyframeData& data, bool markAsChanged)
	{
		if (data.id && !data.id->empty())
		{
			GetImpl().SetDBIdentifier(data.id.value());
		}
		GetImpl().keyframeData = data;
		GetImpl().keyframeData.time = RoundTime(data.time); 
		GetImpl().SetShouldSave(markAsChanged);
	}

	void TimelineKeyframe::Update(const KeyframeData& data)
	{
		// note: time is immutable
		const double oldtime = GetImpl().keyframeData.time;
		auto id = GetImpl().keyframeData.id;
		GetImpl().keyframeData = data;
		GetImpl().keyframeData.time = oldtime;
		GetImpl().keyframeData.id = id;
		GetImpl().InvalidateDB();
	}

	void TimelineKeyframe::SetTime(double time)
	{
		const double oldtime = GetImpl().keyframeData.time;
		if (oldtime == time)
			return;
		GetImpl().keyframeData.time = time;
		GetImpl().InvalidateDB();
	}

	const TimelineKeyframe::KeyframeData& TimelineKeyframe::GetData() const
	{
		return GetImpl().keyframeData;
	}

	ESaveStatus TimelineKeyframe::GetSaveStatus() const
	{
		return GetImpl().GetSaveStatus();
	}
	void TimelineKeyframe::SetSaveStatus(ESaveStatus status)
	{
		GetImpl().SetSaveStatus(status);
	}
	const RefID& TimelineKeyframe::GetId() const
	{
		return GetImpl().GetId();
	}
	void TimelineKeyframe::SetId(const RefID& id)
	{
		GetImpl().SetId(id);
	}

	bool TimelineKeyframe::CompareForOrder(const ITimelineKeyframe* b) const
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


	struct TimelineClipServerSideData
	{
		std::string name;
		bool enable = true;
		std::vector<std::string> keyFrameIds;
		std::optional<std::string> id;
	};

	struct TimelineClipServerCreationData
	{
		std::array<TimelineClipServerSideData, 1> timelineClips;
	};

	struct TimelineClip::Impl final : public SavableItemWithID, public std::enable_shared_from_this<TimelineClip::Impl>
	{
		using ServerSideData = TimelineClipServerSideData;
		using ServerCreationData = TimelineClipServerCreationData;

		ServerSideData serverSideData_;
		std::set<std::shared_ptr<ITimelineKeyframe>, std::less<>> keyframes_;
		std::string sceneId_;
		std::shared_ptr<Http> http_;
		std::vector<std::shared_ptr<ITimelineKeyframe>> toDeleteKeyframes_;
		std::optional<std::string> snapshotId;

		void SetId(const RefID& id) override
		{
			SavableItemWithID::SetId(id);
			if (id.HasDBIdentifier())
			{
				serverSideData_.id = id.GetDBIdentifier();
			}
		}

	


		bool HasSomethingToSave() const
		{
			if (ShouldSave())
				return true;

			for (auto& kf : keyframes_)
				if (kf->ShouldSave())
					return true;
			return false;
		}

		void OnStartSaveKeyframes()
		{
			for (auto& kf : keyframes_)
			{
				kf->OnStartSave();
			}
		}

		void OnKeyframesSaved()
		{
			for (auto& kf : keyframes_)
			{
				kf->OnSaved();
			}
		}
	};


	template <>
	struct TServerDataProps<TimelineClipServerCreationData>
	{
		static std::string GetName(TimelineClipServerCreationData const& jin)
		{
			return std::string("clip ") + jin.timelineClips[0].name;
		}
		static std::optional<std::string> const& GetId(TimelineClipServerCreationData const& jout)
		{
			return jout.timelineClips[0].id;
		}
	};

	bool TimelineClip::HasSomethingToSave() const
	{
		return GetImpl().HasSomethingToSave();
	}

	void TimelineClip::OnStartSaveKeyframes()
	{
		GetImpl().OnStartSaveKeyframes();
	}

	void TimelineClip::OnKeyframesSaved()
	{
		GetImpl().OnKeyframesSaved();
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

	const std::string& TimelineClip::GetName() const
	{
		return GetImpl().serverSideData_.name;
	}

	void TimelineClip::SetName(const std::string& name)
	{
		GetImpl().serverSideData_.name = name;
		InvalidateDB();
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
		GetImpl().InvalidateDB();
		return p;
	}

	expected<void, std::string> TimelineClip::RemoveKeyframe(std::shared_ptr<ITimelineKeyframe> &k)
	{
		auto it = GetImpl().keyframes_.find(k);
		if (it == GetImpl().keyframes_.end())
			return make_unexpected(std::string("Keyframe not found"));
		GetImpl().toDeleteKeyframes_.push_back(*it);
		GetImpl().keyframes_.erase(it);
		GetImpl().InvalidateDB();
		return {};
	}

	ESaveStatus TimelineClip::GetSaveStatus() const
	{
		return GetImpl().GetSaveStatus();
	}
	void TimelineClip::SetSaveStatus(ESaveStatus status)
	{
		GetImpl().SetSaveStatus(status);
	}
	const RefID& TimelineClip::GetId() const
	{
		return GetImpl().GetId();
	}
	void TimelineClip::SetId(const RefID& id)
	{
		GetImpl().SetId(id);
	}

	expected<void, std::string> TimelineClip::SetKeyFrameTimes(const std::vector<float>& sortedTimes)
	{
		// Normally, times are not mutable in a key-frame, as we need to maintain the order of the key-frames.
		// But if we change all times in a way that preserves the existing order, we can do it.
		const size_t KFCount = GetKeyframeCount();
		if (sortedTimes.size() != KFCount)
		{
			return make_unexpected(
				std::string("new times size should match the existing key-frames: got ")
				+ std::to_string(sortedTimes.size()) + ", expected " + std::to_string(KFCount));
		}

		// Apply the same millisecond rounding as AddKeyframe/GetKeyframe: without it, a time
		// stored here could never be found again through GetKeyframe/GetKeyframeIndex (note
		// that widening a float to double rarely lands on an exact millisecond).
		std::vector<double> newTimes;
		newTimes.reserve(KFCount);
		for (float t : sortedTimes)
		{
			newTimes.push_back(RoundTime(static_cast<double>(t)));
		}

		// The key-frames are held in an ordered set keyed on time, and we mutate those keys in
		// place. This is only legitimate if the new times keep the exact same ordering, so they
		// must be strictly increasing: equal times would break the set invariant and leave two
		// key-frames sharing a time, which GetKeyframe could no longer disambiguate.
		const auto badIt = std::adjacent_find(newTimes.begin(), newTimes.end(),
			[](double a, double b) { return !(a < b); });
		if (badIt != newTimes.end())
		{
			return make_unexpected(
				std::string("new key-frame times must be strictly increasing once rounded to ms, got ")
				+ std::to_string(*badIt) + " then " + std::to_string(*std::next(badIt)));
		}

		// Validation is complete: from here on the update cannot fail, so it stays all-or-nothing.
		size_t kfIndex = 0;
		auto it = GetImpl().keyframes_.begin();
		for (; it != GetImpl().keyframes_.end(); ++it, ++kfIndex)
		{
			auto kf = *it;
			if (kf)
			{
				kf->SetTime(newTimes[kfIndex]);
			}
		}
		return {};
	}

	////////////////////////////////////////////////////////////////////// Timeline /////////////////////////////////////

	struct TimelineServerSideData
	{
		std::string name;
		std::vector<std::string> clipIds;
		std::optional<std::string> id;
	};

	struct TimelineServerCreationData
	{
		std::array<TimelineServerSideData, 1> timelines;
	};

	struct Timeline::Impl final : public SavableItemWithID, public std::enable_shared_from_this<Timeline::Impl>
	{
		using ServerSideData = TimelineServerSideData;
		using ServerCreationData = TimelineServerCreationData;

		ServerSideData serverSideData_;
		std::list<std::shared_ptr<ITimelineClip>> clips_;
		std::string sceneId_;
		std::shared_ptr<Http> http_;
		std::vector<std::shared_ptr<ITimelineClip>> toDeleteClips_;
		std::shared_ptr< std::atomic_bool > isThisValid_;

		Impl()
		{
			isThisValid_ = std::make_shared<std::atomic_bool>(true);
		}

		~Impl()
		{
			*isThisValid_ = false;
		}

		void SetId(const RefID& id) override
		{
			SavableItemWithID::SetId(id);
			if (id.HasDBIdentifier())
			{
				serverSideData_.id = id.GetDBIdentifier();
			}
		}

		std::shared_ptr<ITimelineClip> AddClip(const std::string &name)
		{
			auto p = std::shared_ptr<ITimelineClip>(ITimelineClip::New());
			p->SetName(name);
			clips_.push_back(p);
			InvalidateDB();
			return p;
		}

		std::shared_ptr<ITimelineClip> GetClipByRefID(RefID const& id) const
		{
			auto it = std::find_if(clips_.begin(), clips_.end(),
				[&id](std::shared_ptr<ITimelineClip> const& clipPtr) {
				return clipPtr->GetId() == id;
			});
			if (it != clips_.end())
			{
				return *it;
			}
			return {};
		}


		bool HasSomethingToSave() const
		{
			if (ShouldSave())
				return true;
			for (auto& clip : clips_)
				if (clip && clip->HasSomethingToSave())
					return true;
			return false;
		}
	};

	template <>
	struct TServerDataProps<TimelineServerCreationData>
	{
		static std::string GetName(TimelineServerCreationData const& jin)
		{
			return jin.timelines[0].name;
		}
		static std::optional<std::string> const& GetId(TimelineServerCreationData const& jout)
		{
			return jout.timelines[0].id;
		}
	};


	bool Timeline::HasSomethingToSave() const
	{
		return GetImpl().HasSomethingToSave();
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
		GetImpl().InvalidateDB();
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

	std::shared_ptr<ITimelineClip> Timeline::GetClipByRefID(RefID const& id) const
	{
		return GetImpl().GetClipByRefID(id);
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

	ESaveStatus Timeline::GetSaveStatus() const
	{
		return GetImpl().GetSaveStatus();
	}
	void Timeline::SetSaveStatus(ESaveStatus status)
	{
		GetImpl().SetSaveStatus(status);
	}
	const RefID& Timeline::GetId() const
	{
		return GetImpl().GetId();
	}
	void Timeline::SetId(const RefID& id)
	{
		GetImpl().SetId(id);
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

	void Timeline::SetShouldSaveRecursive(bool bshouldSave)
	{
		SetShouldSave(bshouldSave);
		for (auto clip : GetImpl().clips_)
		{
			clip->SetShouldSave(bshouldSave);
		}
	}

}