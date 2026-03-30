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
		double oldtime = GetImpl().keyframeData.time;
		auto id = GetImpl().keyframeData.id;
		GetImpl().keyframeData = data;
		GetImpl().keyframeData.time = oldtime;
		GetImpl().keyframeData.id = id;
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

		expected<void, std::string> Load(const std::shared_ptr<Http>& http, const std::string& sceneId, const std::string& timelineClipId)
		{
			sceneId_ = sceneId;
			serverSideData_.id = timelineClipId;
			http_ = http;
			std::string url = "scenes/" + sceneId_ + "/timelineClips/" + timelineClipId;

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
				SetShouldSave(false);
			}
			else
			{
				return make_unexpected(std::string("http failed: ") + url);
			}

			return {};
		}

		void AsyncSaveClipData(
			std::shared_ptr<Http> const& http,
			std::shared_ptr<AsyncRequestGroupCallback> callbackPtr);

		void AsyncSaveKeyframes(
			std::shared_ptr<Http> const& http,
			std::shared_ptr<AsyncRequestGroupCallback> callbackPtr,
			std::function<void(bool)>&& saveClipData);

		void AsyncDeleteKeyframes(
			std::shared_ptr<Http> const& http,
			std::shared_ptr<AsyncRequestGroupCallback> callbackPtr);

		void AsyncSave(const std::shared_ptr<Http>& http, const std::string& sceneId,
			std::shared_ptr<AsyncRequestGroupCallback> callbackPtr)
		{
			sceneId_ = sceneId;

			// In order to save the list of clips, we need to know the keyframes IDs on the server.
			// Therefore we save key-frames, then the clip data.
			AsyncSaveKeyframes(http, callbackPtr,
				[http, this, callbackPtr](bool bSuccess)
			{
				if (bSuccess && ShouldSave())
				{
					AsyncSaveClipData(http, callbackPtr);
				}
			});

			// delete keys
			AsyncDeleteKeyframes(http, callbackPtr);
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


	void TimelineClip::Impl::AsyncSaveKeyframes(
		std::shared_ptr<Http> const& http,
		std::shared_ptr<AsyncRequestGroupCallback> callbackPtr,
		std::function<void(bool)>&& saveClipData)
	{
		BE_ASSERT(!sceneId_.empty());
		std::string const url = "scenes/" + sceneId_ + "/timelineKeyFrames";

		// save frame keys in batch
		struct SJin {
			std::vector<ITimelineKeyframe::KeyframeData> timelineKeyFrames;
		};
		SJin keyframesToPost;
		SJin keyframesToPut;
		typedef SJin SJout;
		std::vector<std::shared_ptr<ITimelineKeyframe>> timelineKeyframesToCreate;
		std::vector<std::shared_ptr<ITimelineKeyframe>> timelineKeyframesToUpdate;

		bool bNeedCreateKeyframes(false);
		for (auto& k : keyframes_)
		{
			auto& keyData = k->GetData();
			if (keyData.id)
			{
				if (k->ShouldSave())
				{
					keyframesToPut.timelineKeyFrames.push_back(keyData);
					timelineKeyframesToUpdate.push_back(k);
					k->OnStartSave();
				}
			}
			else
			{
				keyframesToPost.timelineKeyFrames.push_back(keyData);
				timelineKeyframesToCreate.push_back(k);
				k->OnStartSave();
				bNeedCreateKeyframes = true;
			}
		}

		if (!keyframesToPut.timelineKeyFrames.empty())
		{
			struct Sout
			{
				int numUpdated = 0;
			};
			AsyncPutJsonJBody<Sout>(http, callbackPtr,
				[url, timelineKeyframesToUpdate](
					long httpCode,
					const Tools::TSharedLockableData<Sout>& /*joutPtr*/)
			{
				const bool bSuccess = (httpCode == 200);
				if (bSuccess)
				{
					for (auto const& kfPtr : timelineKeyframesToUpdate)
					{
						kfPtr->OnSaved();
					}
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Updating timeline keyframes failed."
						<< " url: " << url
						<< " Http status: " << httpCode);
				}
				return bSuccess;
			}, url, keyframesToPut);
		}


		if (bNeedCreateKeyframes)
		{
			AsyncPostJsonJBody<SJout>(http, callbackPtr,
				[url, timelineKeyframesToCreate,
				onKFCreated = std::move(saveClipData)](
					long httpCode,
					const Tools::TSharedLockableData<SJout>& joutPtr)
			{
				bool bSuccess = (httpCode == 201);
				if (bSuccess)
				{
					auto unlockedJout = joutPtr->GetAutoLock();
					SJout const& keyframesOut = unlockedJout.Get();
					bSuccess = (keyframesOut.timelineKeyFrames.size() == timelineKeyframesToCreate.size());
					if (bSuccess)
					{
						for (size_t i = 0; i < timelineKeyframesToCreate.size(); ++i)
						{
							timelineKeyframesToCreate[i]->InternalCreate(keyframesOut.timelineKeyFrames[i], false);
							timelineKeyframesToCreate[i]->OnSaved();
						}
					}
					else
					{
						BE_LOGW("ITwinDecoration", "Wrong number of created keyframes: "
							<< keyframesOut.timelineKeyFrames.size() << "/" << timelineKeyframesToCreate.size()
							<< " url: " << url);
					}
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Creating timeline keyframes failed."
						<< " url: " << url
						<< " Http status: " << httpCode);
				}
				onKFCreated(bSuccess);
				return bSuccess;
			}, url, keyframesToPost);
		}
		else
		{
			// All key-frames have their identifier => we can save clip data at once.
			saveClipData(true);
		}
	}

	void TimelineClip::Impl::AsyncDeleteKeyframes(
		std::shared_ptr<Http> const& http,
		std::shared_ptr<AsyncRequestGroupCallback> callbackPtr)
	{
		BE_ASSERT(!sceneId_.empty());
		std::string const url = "scenes/" + sceneId_ + "/timelineKeyFrames";

		using SJin = SJsonIds;
		using SJout = SJsonIds;

		SJin keyframesToDelete;
		for (auto& k : toDeleteKeyframes_)
		{
			auto& keyData = k->GetData();
			if (keyData.id)
				keyframesToDelete.ids.push_back(*keyData.id);
		}

		if (!keyframesToDelete.ids.empty())
		{
			// Beware in asynchronous mode, toDeleteKeyframes_ can be modified before the request is
			// done, hence the copy to 'deletedHere'.
			AsyncDeleteJsonJBody<SJout>(http, callbackPtr,
				[this, url, deletedHere = toDeleteKeyframes_](
					long httpCode,
					const Tools::TSharedLockableData<SJout>& /*joutPtr*/)
			{
				bool const bSuccess = (httpCode == 200 || httpCode == 204 /* No-Content*/);
				if (bSuccess)
				{
					for (auto const& deletedInServer : deletedHere)
					{
						std::erase(toDeleteKeyframes_, deletedInServer);
					}
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Delete timeline keyframes failed."
						<< " url: " << url
						<< " Http status: " << httpCode);
				}
				return bSuccess;
			}, url, keyframesToDelete);
		}
		else
		{
			toDeleteKeyframes_.clear();
		}
	}

	void TimelineClip::Impl::AsyncSaveClipData(
		std::shared_ptr<Http> const& http,
		std::shared_ptr<AsyncRequestGroupCallback> callbackPtr)
	{
		if (ShouldSave())
		{
			std::string url = "scenes/" + sceneId_ + "/timelineClips";

			serverSideData_.keyFrameIds.clear();
			serverSideData_.keyFrameIds.reserve(keyframes_.size());
			for (auto const& keyframe : keyframes_)
			{
				BE_ASSERT(keyframe->HasDBIdentifier()); // keyframe supposedly saved before
				serverSideData_.keyFrameIds.push_back(keyframe->GetDBIdentifier());
			}

			TimelineClipServerCreationData jin;
			jin.timelineClips[0] = serverSideData_;
			TCreateOrUpdateSingleItemOnServer(this, url, jin, http, callbackPtr);
		}
	}


	expected<void, std::string> TimelineClip::Load(const std::string& sceneId, const std::string& timelineClipId)
	{
		return GetImpl().Load(GetDefaultHttp(), sceneId, timelineClipId);
	}

	void TimelineClip::AsyncSave(const std::string& sceneId, std::shared_ptr<AsyncRequestGroupCallback> callbackPtr)
	{
		GetImpl().AsyncSave(GetDefaultHttp(), sceneId, callbackPtr);
	}

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

		expected<void, std::string> Load(const std::shared_ptr<Http>& http, const std::string& sceneId, const std::string& timelineId)
		{
			sceneId_ = sceneId;
			serverSideData_.id = timelineId;
			http_ = http;
			std::string url = "scenes/" + sceneId_ + "/timelines/" + timelineId;
			ServerSideData data;
			if (http->GetJson(data, url) == 200)
			{
				for (const auto& clipId : data.clipIds)
				{
					auto p = std::shared_ptr<ITimelineClip>(ITimelineClip::New());
					p->Load(sceneId, clipId);
					clips_.push_back(p);
				}
				BE_LOGI("ITwinDecoration", "Timeline loaded "<< data.clipIds.size() <<" clips " );
				SetShouldSave(false);
			}
			else
			{
				return make_unexpected(std::string("http failed: ") + url);
			}
				
			return {};
		}


		void AsyncSaveTimeline(
			std::shared_ptr<Http> const& http,
			std::string const& sceneId,
			std::shared_ptr<AsyncRequestGroupCallback> callbackPtr);

		void AsyncSave(
			std::shared_ptr<Http> const& http,
			std::string const& sceneId,
			std::function<void(bool)>&& onDataSavedFunc = {})
		{
			sceneId_ = sceneId;

			std::shared_ptr<AsyncRequestGroupCallback> callbackPtr =
				std::make_shared<AsyncRequestGroupCallback>(std::move(onDataSavedFunc), isThisValid_);

			// Gather clips to save or to delete.
			std::vector<std::shared_ptr<ITimelineClip>> savedClips;
			for (auto const& clip : clips_)
			{
				if (clip->GetKeyframeCount() > 0)
					savedClips.push_back(clip);
				else
					toDeleteClips_.push_back(clip);
			}

			callbackPtr->AddRequestToWait(); // One dummy request for the clips

			// In order to save the timeline, we need to know the clip IDs on the server, so we'll save the
			// timeline *after* all clips are saved.
			std::function<void(bool)> onAllClipsSavedFunc =
				[http, this, callbackPtr,
				 savedClips, sceneId](bool bSuccess)
			{
				if (bSuccess && callbackPtr->IsValid())
				{
					serverSideData_.clipIds.clear();
					serverSideData_.clipIds.reserve(savedClips.size());
					for (auto const& clip : savedClips)
					{
						BE_ASSERT(clip->HasDBIdentifier()); // clip supposedly saved before
						serverSideData_.clipIds.push_back(clip->GetDBIdentifier());
					}
					AsyncSaveTimeline(http, sceneId, callbackPtr);
				}
				callbackPtr->OnRequestDone(bSuccess);
			};

			std::shared_ptr<AsyncRequestGroupCallback> callback_clips =
				std::make_shared<AsyncRequestGroupCallback>(std::move(onAllClipsSavedFunc), isThisValid_);
			for (auto const& clip : savedClips)
			{
				clip->AsyncSave(sceneId, callback_clips);
			}
			callback_clips->OnFirstLevelRequestsRegistered();


			// delete clips
			if (!toDeleteClips_.empty())
			{
				using SJin = SJsonIds;
				using SJout = SJsonIds;

				SJin clipsToDelete;
				for (auto& c: toDeleteClips_)
				{
					if (c->HasDBIdentifier())
						clipsToDelete.ids.push_back(c->GetDBIdentifier());
					c->OnStartSave();
				}

				if (!clipsToDelete.ids.empty())
				{
					std::string url = "scenes/" + sceneId_ + "/timelineClips";
					// Beware in asynchronous mode, toDeleteClips_ can be modified before the request is
					// done, hence the copy to 'deletedHere'.
					AsyncDeleteJsonJBody<SJout>(http, callbackPtr,
						[this, url, deletedHere = toDeleteClips_](
							long httpCode,
							const Tools::TSharedLockableData<SJout>& /*joutPtr*/)
					{
						const bool bSuccess = (httpCode == 200 || httpCode == 204 /* No-Content*/);
						if (bSuccess)
						{
							for (auto const& deletedInServer : deletedHere)
							{
								deletedInServer->OnSaved();
								std::erase(toDeleteClips_, deletedInServer);
							}
							BE_LOGI("ITwinDecoration", "Timeline save: deleted " << deletedHere.size() << " clips");
						}
						else
						{
							BE_LOGW("ITwinDecoration", "Delete timeline clips failed."
								<< " url: " << url
								<< " Http status: " << httpCode);
						}
						return bSuccess;
					}, url, clipsToDelete);
				}
				else
				{
					toDeleteClips_.clear();
				}
			}

			callbackPtr->OnFirstLevelRequestsRegistered();
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

	void Timeline::Impl::AsyncSaveTimeline(
		std::shared_ptr<Http> const& http,
		std::string const& sceneId,
		std::shared_ptr<AsyncRequestGroupCallback> callbackPtr)
	{
		if (ShouldSave())
		{
			OnStartSave();
			std::string const url = "scenes/" + sceneId + "/timelines";

			TimelineServerCreationData jin;
			jin.timelines[0] = serverSideData_;
			TCreateOrUpdateSingleItemOnServer(this, url, jin, http, callbackPtr);
		}
	}


	expected<void, std::string> Timeline::Load(const std::string& sceneId, const std::string& timelineId)
	{
		return GetImpl().Load(GetDefaultHttp(), sceneId, timelineId);
	}

	void Timeline::AsyncSave(const std::string& sceneId,
		std::function<void(bool)>&& onDataSavedFunc /*= {}*/)
	{
		return GetImpl().AsyncSave(
			GetDefaultHttp(),
			sceneId, std::move(onDataSavedFunc));
	}

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

	expected<std::vector<SSceneTimelineInfo>, std::string> GetSceneTimelines(const std::string& sceneId)
	{
		std::shared_ptr<Http> const& http = GetDefaultHttp();

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
				SSceneTimelineInfo info = { data.name, *data.id };
				timelineIds.push_back(info);
				return {};
			});
		if (!ret)
			return make_unexpected(std::string("GetSceneTimelines failed.\nPrevious error:") + ret.error());

		return timelineIds;
	}

	expected<std::string, std::string> AddSceneTimeline(const std::string& sceneId, const std::string& sceneName)
	{
		std::shared_ptr<Http> const& http = GetDefaultHttp();
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
		std::string id;
		jin.timelines[0].name = sceneName;
		std::string url = "scenes/" + sceneId + "/timelines";

		if (http->PostJsonJBody(jout, url, jin) == 201)
		{
			if (jout.timelines[0].id)
				id = *jout.timelines[0].id;
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