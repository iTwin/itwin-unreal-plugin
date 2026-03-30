/*--------------------------------------------------------------------------------------+
|
|     $Source: KeyframeAnimation.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "KeyframeAnimation.h"
#include "Core/Network/HttpGetWithLink.h"
#include "Core/Singleton/singleton.h"
#include "Config.h"
#include "Core/Tools/FactoryClassInternalHelper.h"

//#define GLM_FORCE_ALIGNED_GENTYPES
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
//#include <glm/gtc/type_aligned.hpp>

namespace AdvViz::SDK::AnimInternal
{
	struct Vec3 {
		double x, y, z;
	};

	// SDK BoundingBox use min[3] but server uses min.x,.y,.z (same for max)
	struct BBoxImpl {
		Vec3 min, max;
		static BBoxImpl from_class(const BoundingBox& b) noexcept {
			BBoxImpl bb;
			bb.min.x = b.min[0];
			bb.min.y = b.min[1];
			bb.min.z = b.min[2];
			bb.max.x = b.max[0];
			bb.max.y = b.max[1];
			bb.max.z = b.max[2];
			return bb;
		}

		BoundingBox to_class() const {
			BoundingBox ret;
			ret.min[0] = min.x;
			ret.min[1] = min.y;
			ret.min[2] = min.z;
			ret.max[0] = max.x;
			ret.max[1] = max.y;
			ret.max[2] = max.z;
			return ret;
		}
	};
}
namespace rfl::parsing {

	template <class ReaderType, class WriterType, class ProcessorsType>
	struct Parser<ReaderType, WriterType, AdvViz::SDK::BoundingBox, ProcessorsType>
		: public CustomParser<ReaderType, WriterType, ProcessorsType, AdvViz::SDK::BoundingBox, AdvViz::SDK::AnimInternal::BBoxImpl> {};

}  // namespace rfl::parsing

namespace AdvViz::SDK
{
	struct AnimationKeyframeChunk::Impl : public std::enable_shared_from_this<AnimationKeyframeChunk::Impl>
	{
		struct SAnimationChunk
		{
			std::vector<float> translations;
			std::vector<float> quaternions;
			std::optional<std::vector<float>> scales;
			std::optional<std::vector<std::int8_t>> stateIds;
			std::optional<BoundingBox> boundingBox;
			std::optional<TimeRange> timeRange;

			int chunkId = -1;
			std::string	animationKeyFramesInfoId;
			std::optional<std::string> id;
		};

		SAnimationChunk serverSideData_;
		bool shouldSave_ = false;
		bool isFullyLoaded = false;
		std::string animationId_;
		std::shared_ptr<Http> http_;
		expected<void, std::string> Save(const std::shared_ptr<Http>& http, const std::string& animationId, const std::string &animationKeyFramesInfoId)
		{
			animationId_ = animationId;
			std::string url = "animations/" + animationId + "/animationKeyFramesChunks";
			http_ = http;

			if (shouldSave_)
			{
				BE_ASSERT((bool)http_);

				struct SJout {
					std::vector<std::string> ids;
				};

				struct SJin {
					std::array<SAnimationChunk*, 1> animationKeyFramesChunks;
				};
				serverSideData_.animationKeyFramesInfoId = animationKeyFramesInfoId;
				SJin jin;
				jin.animationKeyFramesChunks[0] = &serverSideData_;
				long status = 0;
				if (!serverSideData_.id.has_value())
				{
					SJout jout;
					status = http_->PostJsonJBody(jout, url, jin);
					if (status == 201)
					{
						if (!jout.ids.empty())
							serverSideData_.id = jout.ids[0];
						else
							return make_unexpected(std::string("Server returned no id value for saved anim key-frame."));
					}
					else
					{
						return make_unexpected(fmt::format("http failed: {} with status {}", url, status));
					}
				}
				else
				{
					struct SJout_Put
					{
						int numUpdated = 0;
					}; 
					SJout_Put jout;
					status = http_->PutJsonJBody(jout, url, jin);
					if (status != 200)
					{
						return make_unexpected(fmt::format("http failed: {} with status {}", url, status));
					}
				}
			}
			shouldSave_ = false;
			return {};
		}

		void AsyncSave(const std::shared_ptr<Http>& http, const std::string& animationId, const std::string& animationKeyFramesInfoId, const std::function<void(long httpResult)>& callbackfct)
		{
			animationId_ = animationId;
			std::string url = "animations/" + animationId + "/animationKeyFramesChunks";
			http_ = http;

			if (shouldSave_)
			{
				struct SJin {
					std::array<SAnimationChunk*, 1> animationKeyFramesChunks;
				};
				serverSideData_.animationKeyFramesInfoId = animationKeyFramesInfoId;
				SJin jin;
				jin.animationKeyFramesChunks[0] = &serverSideData_;
				if (!serverSideData_.id.has_value())
				{
					struct SJout {
						std::vector<std::string> ids;
					};
					TSharedLockableData<SJout> jout = MakeSharedLockableData<SJout>();
					std::weak_ptr<AnimationKeyframeChunk::Impl> thisWptr(shared_from_this());
					BE_ASSERT((bool)http_);
					http_->AsyncPostJsonJBody(jout, 
						[thisWptr, callbackfct](long httpResult, const TSharedLockableData<SJout> &joutPtr)
						{ 
							auto jout = joutPtr->GetAutoLock();
							if (Http::IsSuccessful(httpResult) && !jout->ids.empty())
							{
								if (callbackfct)
									callbackfct(httpResult);
								auto this_ = thisWptr.lock();
								if (this_)
									this_->serverSideData_.id = jout->ids[0];
							}
							else
							{
								BE_LOGI("keyframeAnim", "Server returned no id value.");
							}
						},
					url, jin);
				}
				else
				{
					struct SJout
					{
						int numUpdated = 0;
					};
					TSharedLockableData<SJout> jout = MakeSharedLockableData<SJout>();

					BE_ASSERT((bool)http_);
					http_->AsyncPutJsonJBody(jout, 
						[callbackfct](long httpResult, const TSharedLockableData<SJout>& joutPtr)
						{ 
							auto jout = joutPtr->GetAutoLock();
							if (!Http::IsSuccessful(httpResult) || jout->numUpdated != 1)
							{
								BE_LOGI("keyframeAnim", "Chunk update failed.");
							}
							else
							{
								if (callbackfct)
									callbackfct(httpResult);
							}
						},
					url, jin);
				}
			}
			shouldSave_ = false;
		}

		expected<void, std::string> Delete(const std::string& animationId)
		{
			if (!serverSideData_.id.has_value())
				return make_unexpected("this AnimationKeyframeChunk has no valid id.");

			struct SJin {
				std::array<std::string, 1> ids;
			};
			SJin jin;
			jin.ids[0] = serverSideData_.id.value();
			SJin jout;

			BE_ASSERT((bool)http_);
			std::string url = "animations/" + animationId + "/animationKeyFramesChunks";
			auto const httpCode = http_->DeleteJsonJBody(jout, url, jin);
			if (httpCode != 200 && httpCode != 204)
			{
				return make_unexpected(std::string("AnimationKeyframeChunk::Delete failed - code: ") + std::to_string(httpCode));
			}
			SAnimationChunk tmp;
			serverSideData_ = tmp;
			return {};
		}

		expected<void, std::string> Load()
		{
			if (isFullyLoaded)
				return {};

			if (!serverSideData_.id.has_value())
				return make_unexpected("this AnimationKeyframeChunk has no valid id.");

			SAnimationChunk jout;
			BE_ASSERT((bool)http_);
			std::string url = "animations/" + animationId_ + "/animationKeyFramesChunks/" + serverSideData_.id.value();
			if (http_->GetJson(jout, url) != 200)
				return make_unexpected("AnimationKeyframeChunk::Delete failed");

			BE_ASSERT(jout.id.value() == serverSideData_.id.value());
			serverSideData_ = jout;
			return {};
		}

		bool ShouldSave() const
		{
			return shouldSave_;
		}

	};

	AnimationKeyframeChunk::AnimationKeyframeChunk() :impl_(new Impl())
	{}
	
	AnimationKeyframeChunk::~AnimationKeyframeChunk()
	{}

	DEFINEFACTORYGLOBALS(AnimationKeyframeChunk);

	expected<void, std::string> AnimationKeyframeChunk::Save(const std::shared_ptr<Http>& http, const std::string& animationId, const std::string& animationKeyFramesInfoId)
	{
		return GetImpl().Save(http, animationId, animationKeyFramesInfoId);
	}

	void AnimationKeyframeChunk::AsyncSave(const std::shared_ptr<Http>& http, const std::string& animationId, const std::string& animationKeyFramesInfoId, const std::function<void(long httpResult)>& callbackfct)
	{
		return GetImpl().AsyncSave(http, animationId, animationKeyFramesInfoId, callbackfct);
	}

	AnimationKeyframeChunk::Impl& AnimationKeyframeChunk::GetImpl()
	{
		return *impl_;
	}

	const AnimationKeyframeChunk::Impl& AnimationKeyframeChunk::GetImpl() const
	{
		return *impl_;
	}

	void AnimationKeyframeChunk::SetTranslations(const std::vector<float>& v)
	{
		BE_ASSERT(v.size() % 3 == 0);
		GetImpl().serverSideData_.translations = v;
		GetImpl().shouldSave_ = true;
	}
	
	const std::vector<float>& AnimationKeyframeChunk::GetTranslations() const
	{
		return GetImpl().serverSideData_.translations;
	}

	void AnimationKeyframeChunk::SetQuaternions(const std::vector<float>& v)
	{
		BE_ASSERT(v.size() % 4 == 0);
		GetImpl().serverSideData_.quaternions = v;
		GetImpl().shouldSave_ = true;
	}

	const std::vector<float>& AnimationKeyframeChunk::GetQuaternions() const
	{
		return GetImpl().serverSideData_.quaternions;
	}

	void AnimationKeyframeChunk::SetScales(const std::vector<float>& v)
	{
		GetImpl().serverSideData_.scales = v;
		GetImpl().shouldSave_ = true;
	}

	const std::vector<float>& AnimationKeyframeChunk::GetScales() const
	{
		if (GetImpl().serverSideData_.scales.has_value())
			return GetImpl().serverSideData_.scales.value();

		static std::vector<float> dummy;
		return dummy;
	}

	void AnimationKeyframeChunk::SetStateIds(const std::vector<std::int8_t>& v)
	{
		GetImpl().serverSideData_.stateIds = v;
		GetImpl().shouldSave_ = true;
	}

	const std::vector<std::int8_t>& AnimationKeyframeChunk::GetStateIds() const
	{
		if (GetImpl().serverSideData_.stateIds.has_value())
			return GetImpl().serverSideData_.stateIds.value();

		static std::vector<std::int8_t> dummy;
		return dummy;
	}

	void AnimationKeyframeChunk::SetBoundingBox(const BoundingBox& b)
	{
		GetImpl().serverSideData_.boundingBox = b;
		GetImpl().shouldSave_ = true;
	}

	const BoundingBox& AnimationKeyframeChunk::GetBoundingBox() const
	{
		if (GetImpl().serverSideData_.boundingBox.has_value())
		{
			return GetImpl().serverSideData_.boundingBox.value();
		}
		static BoundingBox ret;
		return ret;
	}

	void AnimationKeyframeChunk::SetTimeRange(const TimeRange& b)
	{
		GetImpl().serverSideData_.timeRange = b;
		GetImpl().shouldSave_ = true;
	}

	const TimeRange& AnimationKeyframeChunk::GetTimeRange() const
	{
		if (GetImpl().serverSideData_.timeRange.has_value())
		{
			return GetImpl().serverSideData_.timeRange.value();
		}

		static TimeRange ret;
		return ret;
	}

	bool AnimationKeyframeChunk::ShouldSave() const
	{
		return GetImpl().shouldSave_;
	}

	const IAnimationKeyframeChunk::Id& AnimationKeyframeChunk::GetId() const
	{
		thread_local static IAnimationKeyframeChunk::Id id;
		if (GetImpl().serverSideData_.id.has_value())
			id = IAnimationKeyframeChunk::Id(GetImpl().serverSideData_.id.value());
		else
			id.Reset();
		return id;
	}

	bool AnimationKeyframeChunk::IsFullyLoaded() const
	{
		return GetImpl().isFullyLoaded;
	}

	expected<void, std::string> AnimationKeyframeChunk::Load()
	{
		return GetImpl().Load();
	}

	class AnimationKeyframeInfo::Impl : public std::enable_shared_from_this<AnimationKeyframeInfo::Impl>
	{
	public:
		struct SAnimationInfo
		{
			std::string objectId;
			std::string	type;
			std::optional<double> keyframeInterval;
			std::optional<double> startTime;
			std::optional<int> keyframeCount;
			std::optional<int> chunkSize;
			std::optional<std::vector<std::string>> states;
			std::optional<std::vector<std::string>> tags;
			std::optional<GCS> gcs;
			std::optional<std::string> id;
		};

		SAnimationInfo serverSideData_;

		std::vector<IAnimationKeyframeChunkPtr> chunks_;
		std::string animationId_;
		std::shared_ptr<Http> http_;
		bool shouldSave_ = false;

		friend struct AnimationKeyframe::Impl;

		expected<void, std::string> LoadAllChunks()
		{
			if (serverSideData_.type != "baked")
				return make_unexpected("Nothing to load, only baked animations are supported.");

			if (animationId_.empty())
				return make_unexpected("Can't load, invalid animationId.");

			if (!serverSideData_.id.has_value())
				return make_unexpected("Can't load, no valid id.");

			std::string url = "animations/" + animationId_ + "/query/animationKeyFramesChunks";
			
			struct JIn {
				std::string animationKeyFramesInfoId;
				std::array<int, 0> chunckIndexes;
			};
			JIn in;
			in.animationKeyFramesInfoId = serverSideData_.id.value();
			struct JOut {
				std::vector<std::string> ids;
			};
			JOut jout;

			if (http_->PostJsonJBody(jout, url, in) != 200)
			{
				return make_unexpected(std::string("http failed: ") + url);
			}

			chunks_.clear();
			for (auto& i : jout.ids)
			{
				auto p = IAnimationKeyframeChunk::New();
				auto p2 = Tools::DynamicCast<AnimationKeyframeChunk>(p);
				p2->GetImpl().serverSideData_.id = i;
				p2->GetImpl().isFullyLoaded = false;
				p2->GetImpl().animationId_ = animationId_;
				p2->GetImpl().http_ = http_;
				chunks_.push_back(MakeSharedLockableDataPtr(p));
			}

			shouldSave_ = false;
			return {};
		}

		expected<void, std::string> Load(const std::shared_ptr<Http>& http, const std::string& animationId, const std::string& infoId)
		{
			animationId_ = animationId;
			http_ = http;
			std::string url = "animations/" + animationId_ + "/animationKeyFramesInfos/"+ infoId;
			SAnimationInfo data;
			if (http->GetJson(serverSideData_, url) != 200)
			{
				return make_unexpected(std::string("http failed: ") + url);
			}
			
			return LoadAllChunks();
		}

		expected<void, std::string> Save(const std::shared_ptr<Http>& http, bool bChunks)
		{
			http_ = http;

			if (shouldSave_)
			{
				std::string url = "animations/" + animationId_ + "/animationKeyFramesInfos";
				struct SJin {
					std::array<SAnimationInfo, 1> animationKeyFramesInfos;
				};
				SJin jin;
				jin.animationKeyFramesInfos[0] = serverSideData_;

				long status = 0;
				if (!serverSideData_.id.has_value())
				{
					SJin jout;
					status = http->PostJsonJBody(jout, url, jin);
					if (status == 201)
					{
						BE_ASSERT(jout.animationKeyFramesInfos[0].id.has_value());
						if (jout.animationKeyFramesInfos[0].id)
							serverSideData_.id = jout.animationKeyFramesInfos[0].id;
						else
							return make_unexpected(std::string("Server returned no id value for KF infos."));
					}
					else
					{
						return make_unexpected(fmt::format("http failed: {} with status {}", url, status));
					}
				}
				else
				{
					struct SJout
					{
						int numUpdated = 0;
					};
					SJout jout;
					status = http->PutJsonJBody(jout, url, jin);
					if (status != 200)
					{
						return make_unexpected(fmt::format("http failed: {} with status {}", url, status));
					}
				}
			}

			if (bChunks)
				for (auto& i : chunks_)
				{
					if (i){
					auto lock = i->GetAutoLock();
					auto ret = lock->Save(http, animationId_, serverSideData_.id.value());
					if (!ret)
						return ret;
					} 
				}

			shouldSave_ = false;
			return {};
		}

		void AsyncSave(const std::shared_ptr<Http>& http, const std::function<void(const IAnimationKeyframeInfo::Id& id)>& callBackfct)
		{
			http_ = http;

			if (shouldSave_)
			{
				std::string url = "animations/" + animationId_ + "/animationKeyFramesInfos";
				struct SJin {
					std::array<SAnimationInfo, 1> animationKeyFramesInfos;
				};
				SJin jin;
				jin.animationKeyFramesInfos[0] = serverSideData_;
				std::weak_ptr<AnimationKeyframeInfo::Impl> thisWptr(shared_from_this());
				if (!serverSideData_.id.has_value())
				{
					TSharedLockableData<SJin> jout = MakeSharedLockableData<SJin>();
					http->AsyncPostJsonJBody(jout,
						[thisWptr, callBackfct](long /*httpCode*/, const TSharedLockableData<SJin>& outDataPtr)
						{
							auto jout(outDataPtr->GetAutoLock());
							BE_ASSERT(jout->animationKeyFramesInfos[0].id.has_value());
							if (jout->animationKeyFramesInfos[0].id.has_value())
							{
								auto this_ = thisWptr.lock();
								if (this_)
									this_->serverSideData_.id = jout->animationKeyFramesInfos[0].id;
								IAnimationKeyframeInfo::Id id(this_->serverSideData_.id.value());
								callBackfct(id);
							}
							else
								BE_LOGI("keyframeAnim", "AnimationKeyframeInfo, server returned no id value.");
						},
					url, jin);
				}
				else
				{
					struct SJout
					{
						int numUpdated = 0;
					};
					TSharedLockableData<SJout> jout = MakeSharedLockableData<SJout>();
					http->AsyncPutJsonJBody(jout,
						[thisWptr, callBackfct](long httpCode, const TSharedLockableData<SJout>& /*outDataPtr*/)
						{
							if (!Http::IsSuccessful(httpCode))
							{
								BE_LOGI("keyframeAnim", "AnimationKeyframeInfo, update failed.");
							}
							else
							{
								auto this_ = thisWptr.lock();
								if (callBackfct && this_ && this_->serverSideData_.id.has_value())
								{
									IAnimationKeyframeInfo::Id id(this_->serverSideData_.id.value());
									callBackfct(id);
								}
							}
						},
						url, jin);
				}
			}

			shouldSave_ = false;
		}
		bool ShouldSave() const
		{
			if (shouldSave_)
				return true;
			for (auto& i : chunks_)
			{
				if (i)
				{
					auto lock = i->GetAutoLock();
					if (lock->ShouldSave())
						return true;
				}
			}
			return false;
		}
	};

	AnimationKeyframeInfo::AnimationKeyframeInfo() :impl_(new Impl())
	{}

	AnimationKeyframeInfo::~AnimationKeyframeInfo()
	{}

	DEFINEFACTORYGLOBALS(AnimationKeyframeInfo);

	void AnimationKeyframeInfo::SetObjectId(const std::string & v)
	{
		GetImpl().serverSideData_.objectId = v;
	}

	const std::string& AnimationKeyframeInfo::GetObjectId() const
	{
		return GetImpl().serverSideData_.objectId;
	}

	void AnimationKeyframeInfo::SetType(const std::string& v)
	{
		GetImpl().serverSideData_.type = v;
		GetImpl().shouldSave_ = true;
	}

	const std::string& AnimationKeyframeInfo::GetType() const
	{
		return GetImpl().serverSideData_.type;
	}

	void AnimationKeyframeInfo::SetKeyframeInterval(float v)
	{
		GetImpl().serverSideData_.keyframeInterval = v;
		GetImpl().shouldSave_ = true;
	}

	float AnimationKeyframeInfo::GetKeyframeInterval() const
	{
		if (GetImpl().serverSideData_.keyframeInterval.has_value())
			return static_cast<float>(GetImpl().serverSideData_.keyframeInterval.value());
		return 0.0f;
	}

	void AnimationKeyframeInfo::SetStartTime(double v)
	{
		GetImpl().serverSideData_.startTime = v;
		GetImpl().shouldSave_ = true;
	}

	double AnimationKeyframeInfo::GetStartTime() const
	{
		if (GetImpl().serverSideData_.startTime.has_value())
			return GetImpl().serverSideData_.startTime.value();
		return 0.0;
	}

	void AnimationKeyframeInfo::SetKeyframeCount(int v)
	{
		GetImpl().serverSideData_.keyframeCount = v;
		GetImpl().shouldSave_ = true;
	}

	int AnimationKeyframeInfo::GetKeyframeCount() const
	{
		if (GetImpl().serverSideData_.keyframeCount.has_value())
			return GetImpl().serverSideData_.keyframeCount.value();
		return 0;
	}

	void AnimationKeyframeInfo::SetChunkSize(int v)
	{
		GetImpl().serverSideData_.chunkSize = v;
		GetImpl().shouldSave_ = true;
	}

	int AnimationKeyframeInfo::GetChunkSize() const
	{
		if (GetImpl().serverSideData_.chunkSize.has_value())
			return GetImpl().serverSideData_.chunkSize.value();
		return 0;
	}

	void AnimationKeyframeInfo::SetStates(const std::vector<std::string>& v)
	{
		GetImpl().serverSideData_.states = v;
		GetImpl().shouldSave_ = true;
	}

	const std::vector<std::string>& AnimationKeyframeInfo::GetStates() const
	{
		if (GetImpl().serverSideData_.states.has_value())
			return GetImpl().serverSideData_.states.value();
		static std::vector<std::string> dummy;
		return dummy;
	}

	void AnimationKeyframeInfo::SetTags(const std::vector<std::string>& v)
	{
		GetImpl().serverSideData_.tags = v;
		GetImpl().shouldSave_ = true;
	}

	const std::vector<std::string>& AnimationKeyframeInfo::GetTags() const
	{
		if (GetImpl().serverSideData_.tags.has_value())
			return GetImpl().serverSideData_.tags.value();
		static std::vector<std::string> dummy;
		return dummy;
	}

	void AnimationKeyframeInfo::SetGCS(const GCS& v)
	{
		GetImpl().serverSideData_.gcs = v;
		GetImpl().shouldSave_ = true;
	}

	const GCS& AnimationKeyframeInfo::GetGCS() const
	{
		if (GetImpl().serverSideData_.gcs.has_value())
			return GetImpl().serverSideData_.gcs.value();
		static GCS dummy;
		return dummy;
	}

	expected<void, std::string> AnimationKeyframeInfo::Save(const std::shared_ptr<Http>& http, bool bChunk)
	{
		return GetImpl().Save(http, bChunk);
	}

	void AnimationKeyframeInfo::AsyncSave(const std::shared_ptr<Http>& http, const std::function<void(const IAnimationKeyframeInfo::Id& id)> &callBackfct)
	{
		return GetImpl().AsyncSave(http, callBackfct);
	}

	IAnimationKeyframeChunkPtr AnimationKeyframeInfo::CreateChunk()
	{
		auto p = IAnimationKeyframeChunk::New();
		auto p2 = Tools::DynamicCast<AnimationKeyframeChunk>(p);
		p2->GetImpl().http_ = GetImpl().http_;
		int const chunkId = (int)GetImpl().chunks_.size();
		p2->GetImpl().serverSideData_.chunkId = chunkId;
		GetImpl().chunks_.push_back(MakeSharedLockableDataPtr(p));
		return GetImpl().chunks_.back();
	}

	size_t AnimationKeyframeInfo::GetChunkCount() const
	{
		return GetImpl().chunks_.size();
	}

	IAnimationKeyframeChunkPtr AnimationKeyframeInfo::GetChunk(size_t i) const
	{
		if (i < GetImpl().chunks_.size())
			return GetImpl().chunks_[i];
		return {};
	}

	bool AnimationKeyframeInfo::ShouldSave() const
	{
		return GetImpl().shouldSave_;
	}

	const IAnimationKeyframeInfo::Id& AnimationKeyframeInfo::GetId() const
	{
		thread_local static AnimationKeyframeInfo::Id id;

		if (GetImpl().serverSideData_.id)
			id = AnimationKeyframeInfo::Id(*GetImpl().serverSideData_.id);
		else
			id.Reset();

		return id;
	}

	expected<void, std::string> AnimationKeyframeInfo::AsyncQueryKeyframes(TSharedLockableData<TimelineResult>& dataPtr, const std::function<void(long httpResult, const TSharedLockableData<TimelineResult>&)> &callbackfct, double time, double duration) const
	{
		if (!impl_->serverSideData_.id.has_value())
			return make_unexpected("this AnimationKeyframeInfo has no valid id.");

		auto& impl = GetImpl();
		struct SJIn {
			std::string animationKeyFramesInfoId;
			double time, duration;
		};
		SJIn jin{ impl_->serverSideData_.id.value(), time, duration};

		std::string url = "animations/" + impl.animationId_ + "/query/animationKeyFrames";

		BE_ASSERT((bool)impl.http_);
		impl.http_->AsyncPostJsonJBody(dataPtr, callbackfct, url, jin);

		return {};
	}

	expected<void, std::string> AnimationKeyframeInfo::QueryKeyframes(TimelineResult& result, double time, double duration) const
	{
		if (!impl_->serverSideData_.id.has_value())
			return make_unexpected("this AnimationKeyframeInfo has no valid id.");

		auto& impl = GetImpl();
		struct SJIn {
			std::string animationKeyFramesInfoId;
			double time, duration;
		};
		SJIn jin{ impl_->serverSideData_.id.value(), time, duration };

		typedef TimelineResult SJout;
		SJout& jout = result;

		TimelineResult dummy;
		result = dummy; //clear

		std::string url = "animations/" + impl.animationId_ + "/query/animationKeyFrames";

		BE_ASSERT((bool)impl.http_);
		if (impl.http_->PostJsonJBody(jout, url, jin) != 200)
			return make_unexpected(std::string("query:") + url + " failed.");

		return expected<void, std::string>();
	}

	expected<void, std::string> AnimationKeyframeInfo::GetInterpolatedValue(const TimelineResult& result, double time, TimelineValue& value) const
	{
		if (!((result.timeRange.begin <= time) && (time <= result.timeRange.end)))
			return make_unexpected("time not in result range");

		const size_t nbKeys = result.translations.size() / 3;
		const float fkey = nbKeys * ((float)time - result.timeRange.begin) / (result.timeRange.end - result.timeRange.begin);
		const size_t keyIndex1 = (size_t)fkey; //integer part of fkey
		size_t keyIndex2 = keyIndex1 + 1;
		if (keyIndex1 == nbKeys - 1)
			keyIndex2 = keyIndex1;
		if (keyIndex1 > nbKeys -1)
			return make_unexpected("wrong keyframe");

		const float s = fkey - keyIndex1; //fractional part of fkey

		typedef glm::vec3 myvec3;
		myvec3 tr1(*(glm::vec3*)(&result.translations[keyIndex1 * 3]));
		myvec3 tr2(*(glm::vec3*)(&result.translations[keyIndex2 * 3]));
		myvec3 tr(tr1 + (tr2 - tr1) * s);
		*(glm::vec3*)(&value.translation[0]) = tr;

		glm::quat qu1(*(glm::quat*)(&result.quaternions[keyIndex1 * 4]));
		glm::quat qu2(*(glm::quat*)(&result.quaternions[keyIndex2 * 4]));
		glm::quat qu(glm::slerp(qu1, qu2, s));
		*(glm::quat*)(&value.quaternion[0]) = qu;

		if (value.scale.has_value())
		{
			value.scale.emplace();
			myvec3 sc1(*(glm::vec3*)(&result.scales.value()[keyIndex1 * 3]));
			myvec3 sc2(*(glm::vec3*)(&result.scales.value()[keyIndex2 * 3]));
			myvec3 sc(sc1 + (sc2 - sc1) * s);
			*(glm::vec3*)(&value.scale.value()[0]) = sc;
		}

		if (value.stateId.has_value())
			value.stateId = result.stateIds.value()[keyIndex1];

		return expected<void, std::string>();
	}

	expected<void, std::string> AnimationKeyframeInfo::Delete()
	{
		auto& impl = GetImpl();
		if (!impl.serverSideData_.id.has_value())
			return make_unexpected("this AnimationKeyframeInfo has no valid id.");

		struct SJin {
			std::array<std::string, 1> ids;
		};
		SJin jin;
		jin.ids[0] = impl.serverSideData_.id.value();
		SJin jout;

		BE_ASSERT((bool)impl.http_);
		std::string url = "animations/" + impl.animationId_ + "/animationKeyFramesInfos";

		auto const httpCode = impl.http_->DeleteJsonJBody(jout, url, jin);
		if (httpCode != 200 && httpCode != 204)
		{
			return make_unexpected(std::string("AnimationKeyframeInfo::Delete failed - code: ") + std::to_string(httpCode));
		}

		return {};
	}

	expected<void, std::string> AnimationKeyframeInfo::DeleteChunk(size_t chunkId)
	{
		if (chunkId >= GetImpl().chunks_.size())
			return make_unexpected("Chunk doesn't exist");

		auto p = GetImpl().chunks_[chunkId];
		if(p)
		{ 
		  auto lock = p->GetAutoLock();
		  auto p2 = Tools::DynamicCast<AnimationKeyframeChunk>(lock.GetPtr());
		  p2->GetImpl().Delete(GetImpl().animationId_);
		  GetImpl().chunks_[chunkId].reset();
		}

		if (chunkId == GetImpl().chunks_.size()-1)
			GetImpl().chunks_.pop_back();

		return {};
	}

	AnimationKeyframeInfo::Impl& AnimationKeyframeInfo::GetImpl()
	{
		return *impl_;
	}

	const AnimationKeyframeInfo::Impl& AnimationKeyframeInfo::GetImpl() const
	{
		return *impl_;
	}

	bool IAnimationKeyframeInfo::ShouldSave() const
	{
		return false;
	}


	struct AnimationKeyframe::Impl : std::enable_shared_from_this<AnimationKeyframe::Impl>
	{
		struct SAnimation
		{
			std::string name;
			std::string	itwinid;
			std::optional<std::string> id;
		};

		struct SThreadSafeData
		{
			std::vector<IAnimationKeyframeInfoPtr> infos_;
			std::unordered_map<IAnimationKeyframeInfo::Id, IAnimationKeyframeInfoPtr> infosMap_;
			std::vector<IAnimationKeyframeInfoPtr> toDeleteInfos_;
			SAnimation serverSideData_;
			std::shared_ptr<Tools::IGCSTransform> gcsTransform_;
		};
		Tools::RWLockableObject<SThreadSafeData> thdata_;

		std::shared_ptr<Http> http_;
		bool shouldSave_ = true;

		IAnimationKeyframeInfoPtr AddAnimationInfo(const std::string& objectId, const std::string& animationId)
		{
			auto p = IAnimationKeyframeInfo::New();
			p->SetObjectId(objectId);
			auto p2 = Tools::DynamicCast<AnimationKeyframeInfo>(p);
			BE_ASSERT(p2 != nullptr);
			p2->GetImpl().animationId_ = animationId;
			auto p3 = MakeSharedLockableDataPtr(p);
			auto thdata = thdata_.GetAutoLock();
			thdata->infos_.push_back(p3);
			shouldSave_ = true;
			return p3;
		}

		expected<void, std::string> LoadKeyFrameInfos()
		{
			std::optional<std::string> id;
			{
				auto thdata = thdata_.GetRAutoLock();
				id = thdata->serverSideData_.id;
			}
			if (!id.has_value())
				return make_unexpected("this AnimationKeyframe has no valid id.");

			std::string animationId = id.value();
			std::string url = "animations/" + animationId + "/animationKeyFramesInfos";

			auto ret = HttpGetWithLink<AnimationKeyframeInfo::Impl::SAnimationInfo>(http_, url, {},
				[animationId, this](AnimationKeyframeInfo::Impl::SAnimationInfo& data) -> expected<void, std::string> {
					auto p = IAnimationKeyframeInfo::New();
					auto p2 = Tools::DynamicCast<AnimationKeyframeInfo>(p);
					if (!p2)
						return make_unexpected("IAnimationKeyframeInfo should be based on class AnimationKeyframeInfo.");
					p2->GetImpl().serverSideData_ = data;
					p2->GetImpl().animationId_ = animationId;
					p2->GetImpl().http_ = http_;
					auto p3 = MakeSharedLockableDataPtr(p);
					auto thdata = thdata_.GetAutoLock();
					thdata->infos_.push_back(p3);
					thdata->infosMap_[IAnimationKeyframeInfo::Id(*data.id)] = p3;
					return {};
				});
			if (ret)
				shouldSave_ = false;
			return ret;
		}

		void AsyncLoadKeyFrameInfos(
			const std::function<void(IAnimationKeyframeInfoPtr&)> &onCreateKeyFrameInfo,
			const std::function<void(expected<void, std::string> const&)> &onFinish)
		{
			std::optional<std::string> id;
			{
				auto thdata = thdata_.GetRAutoLock();
				id = thdata->serverSideData_.id;
			}
			if (!id.has_value())
			{
				onFinish(make_unexpected("this AnimationKeyframe has no valid id."));
				return;
			}
			auto SThis(shared_from_this());
			std::string animationId = id.value();
			std::string url = "animations/" + animationId + "/animationKeyFramesInfos";

			AsyncHttpGetWithLink<AnimationKeyframeInfo::Impl::SAnimationInfo>(http_, url, {},
				[animationId, SThis, onCreateKeyFrameInfo](AnimationKeyframeInfo::Impl::SAnimationInfo& data) -> expected<void, std::string> {
					auto p = IAnimationKeyframeInfo::New();
					auto p2 = Tools::DynamicCast<AnimationKeyframeInfo>(p);
					if (!p2)
						return make_unexpected("IAnimationKeyframeInfo should be based on class AnimationKeyframeInfo.");
					p2->GetImpl().serverSideData_ = data;
					p2->GetImpl().animationId_ = animationId;
					p2->GetImpl().http_ = SThis->http_;
					auto p3 = MakeSharedLockableDataPtr(p);
					{
						auto thdata = SThis->thdata_.GetAutoLock();
						thdata->infos_.push_back(p3);
						thdata->infosMap_[IAnimationKeyframeInfo::Id(*data.id)] = p3;
					}
					onCreateKeyFrameInfo(p3);
					return {};
				},
				onFinish);
		}

		expected<void, std::string> Save(std::shared_ptr<Http> const& http, bool bInfos)
		{
			http_ = http;

			if (bInfos)
			{
				auto thdata = thdata_.GetAutoLock();
				for (auto& i : thdata->infos_)
				{
					auto lock = i->GetAutoLock();
					lock->Save(http_);
					thdata->infosMap_[lock->GetId()] = i;
				}
			}

			// delete infos
			auto ret = [this, &http]() -> expected<void, std::string>
				{
					auto thdata = thdata_.GetAutoLock();
					if (thdata->serverSideData_.id.has_value())
					{
						struct SJin {
							std::vector<std::string> ids;
						};
						SJin infosToDelete;
						typedef SJin SJout;
						SJout infosOut;
						for (auto& c : thdata->toDeleteInfos_)
						{
							auto lock = c->GetAutoLock();
							if (lock->GetId().IsValid())
								infosToDelete.ids.push_back(static_cast<const std::string>(lock->GetId()));
						}

						std::string url = "animations/" + thdata->serverSideData_.id.value() + "/animationKeyFramesInfos";
						if (!infosToDelete.ids.empty())
						{
							auto const httpCode = http->DeleteJsonJBody(infosOut, url, infosToDelete);
							if (httpCode != 200 && httpCode != 204)
							{
								return make_unexpected(std::string("DeleteInfos failed: ") + url + " - code: " + std::to_string(httpCode));
							}
						}

						thdata->toDeleteInfos_.clear();
					}
					return {};
				}();

			if (!ret)
				return ret;

			if (shouldSave_)
			{
				auto thdata = thdata_.GetAutoLock();
				long status = 0;
				if (!thdata->serverSideData_.id.has_value())
				{
					std::string url("animations");
					SAnimation jout;
					status = http->PostJsonJBody(jout, url, thdata->serverSideData_);
					if (status == 201)
					{
						BE_ASSERT(jout.id.has_value());
						if (jout.id)
							thdata->serverSideData_.id = jout.id;
						else
							return make_unexpected(std::string("Server returned no id value for AnimationKeyframe."));
					}
					else
					{
						return make_unexpected(fmt::format("http failed: {} with status {}", url, status));
					}
				}
				else
				{
					struct SJout
					{
						int numUpdated = 0;
					};
					SJout jout;
					std::string url("animations/");
					url += thdata->serverSideData_.id.value();
					status = http->PutJsonJBody(jout, url, thdata->serverSideData_);
					if (status != 200)
					{
						return make_unexpected(fmt::format("http failed: {} with status {}", url, status));
					}
				}
			}
			shouldSave_ = false;
			return {};
		}

		bool ShouldSave() const
		{
			if (shouldSave_)
				return true;
			auto thdata = thdata_.GetRAutoLock();
			for (auto& i : thdata->infos_)
			{
				if (i)
				{
					auto lock = i->GetAutoLock();
					if (lock->ShouldSave())
						return true;
				}
			}
			return false;
		}
	};

	AnimationKeyframe::AnimationKeyframe() :impl_(new Impl())
	{}

	AnimationKeyframe::~AnimationKeyframe()
	{}

	std::shared_ptr<Tools::IGCSTransform> AnimationKeyframe::GetGCSTransform() const
	{
		auto thdata = GetImpl().thdata_.GetRAutoLock();
		return thdata->gcsTransform_;
	}

	void AnimationKeyframe::SetGCSTransform(const std::shared_ptr<Tools::IGCSTransform>& t)
	{
		auto thdata = GetImpl().thdata_.GetAutoLock();
		thdata->gcsTransform_ = t;
	}

	AnimationKeyframe::Impl& AnimationKeyframe::GetImpl()
	{
		return *impl_;
	}

	const AnimationKeyframe::Impl& AnimationKeyframe::GetImpl() const
	{
		return *impl_;
	}

	DEFINEFACTORYGLOBALS(AnimationKeyframe);

	expected<void, std::string> AnimationKeyframe::LoadAnimationKeyFrameInfos()
	{
		return GetImpl().LoadKeyFrameInfos();
	}

	void AnimationKeyframe::AsyncLoadAnimationKeyFrameInfos(
		const std::function<void(IAnimationKeyframeInfoPtr&)>& onCreateKeyFrameInfo,
		const std::function<void(expected<void, std::string> const&)>& onFinish
	)
	{
		GetImpl().AsyncLoadKeyFrameInfos(onCreateKeyFrameInfo, onFinish);
	}

	expected<void, std::string> AnimationKeyframe::Save(std::shared_ptr<Http> const& http, bool bInfos)
	{
		return GetImpl().Save(http, bInfos);
	}

	bool AnimationKeyframe::ShouldSave() const
	{
		return GetImpl().ShouldSave();
	}

	const IAnimationKeyframe::Id& AnimationKeyframe::GetId() const
	{
		thread_local static IAnimationKeyframe::Id id;
		auto thdata = GetImpl().thdata_.GetRAutoLock();
		if (thdata->serverSideData_.id)
			id = IAnimationKeyframe::Id(*thdata->serverSideData_.id);
		else
			id.Reset();

		return id;
	}

	IAnimationKeyframeInfoPtr AnimationKeyframe::AddAnimationKeyframeInfo(const std::string& objectId)
	{
		std::optional<std::string> id;
		{
			auto thdata = GetImpl().thdata_.GetRAutoLock();
			id = thdata->serverSideData_.id;
		}
		BE_ASSERT(id.has_value());
		return GetImpl().AddAnimationInfo(objectId, id.value());
	}

	expected<IAnimationKeyframeInfoPtr, std::string> AnimationKeyframe::LoadKeyframesInfo(const IAnimationKeyframeInfo::Id& animationKeyframeInfoId)
	{
		std::optional<std::string> id;
		{
			auto thdata = GetImpl().thdata_.GetRAutoLock();
			id = thdata->serverSideData_.id;
		}

		IAnimationKeyframeInfo* p(IAnimationKeyframeInfo::New());
		auto p2 = Tools::DynamicCast<AnimationKeyframeInfo>(p);
		if (!p2)
			return make_unexpected("AnimationKeyframeInfo should be base class of IAnimationKeyframeInfo.");
		if (!id.has_value())
			return make_unexpected("AnimationKeyframe has no valid id.");

		auto ret = p2->GetImpl().Load(GetImpl().http_, id.value(), (std::string)animationKeyframeInfoId);
		if (!ret)
			return make_unexpected(std::string("LoadKeyframesInfo failed, previous error:") + ret.error());
		auto p3 = MakeSharedLockableDataPtr(p);
		{
			auto thdata = GetImpl().thdata_.GetAutoLock();
			thdata->infosMap_[animationKeyframeInfoId] = p3;
		}
		return p3;
	}

	IAnimationKeyframeInfoPtr AnimationKeyframe::GetAnimationKeyframeInfo(const IAnimationKeyframeInfo::Id& animationKeyframeInfoId) const
	{
		{
			auto thdata = GetImpl().thdata_.GetRAutoLock();
			auto it = thdata->infosMap_.find(animationKeyframeInfoId);
			if (it != thdata->infosMap_.end())
				return it->second;
		}
		return IAnimationKeyframeInfoPtr();
	}

	std::vector<IAnimationKeyframeInfo::Id> AnimationKeyframe::GetAnimationKeyframeInfoIds() const
	{
		std::vector<IAnimationKeyframeInfo::Id> ret;
		auto thdata = GetImpl().thdata_.GetRAutoLock();
		ret.reserve(thdata->infosMap_.size());
		for (auto it : thdata->infosMap_)
			ret.push_back(it.first);
		return ret;
	}

	expected<std::vector<IAnimationKeyframeInfo::Id>, std::string> AnimationKeyframe::QueryKeyframesInfos(const std::vector<BoundingBox>& boundingBoxes, const TimeRange& timeRange) const
	{
		std::vector<IAnimationKeyframeInfo::Id> animationsKeyframeInfoIds;
		
		auto& impl = GetImpl();
		struct SJin {
			std::vector<BoundingBox> boundingBoxes;
			TimeRange timeRange;
		};
		SJin jin;
		jin.boundingBoxes = boundingBoxes;
		jin.timeRange = timeRange;

		struct SJout {
			std::vector<std::string> ids;
		};
		SJout jout;

		std::optional<std::string> id;
		{
			auto thdata = GetImpl().thdata_.GetRAutoLock();
			id = thdata->serverSideData_.id;
		}

		if (!id.has_value())
			return make_unexpected("IAnimationKeyframeInfo has no id.");

		std::string url = "animations/" + id.value() + "/query/animationKeyFramesBBox";
		BE_ASSERT((bool)impl.http_);
		if (impl.http_->PostJsonJBody(jout, url, jin) != 200)
			return make_unexpected(std::string("query:") + url + " failed.");
	
		animationsKeyframeInfoIds.reserve(jout.ids.size());
		for (auto i : jout.ids)
			animationsKeyframeInfoIds.push_back(IAnimationKeyframeInfo::Id(i));

		return animationsKeyframeInfoIds;
	}

	expected<void, std::string> AnimationKeyframe::AsyncQueryKeyframesInfos( const Tools::TSharedLockableData<std::set<IAnimationKeyframeInfo::Id>> & dataPtr
			,const std::function<void(long, std::set<IAnimationKeyframeInfo::Id>&)> &callbackfct
			,const std::vector<BoundingBox>& boundingBoxes, const TimeRange& timeRange) const
	{
		auto& impl = GetImpl();
		std::optional<std::string> id;
		{
			auto thdata = GetImpl().thdata_.GetRAutoLock();
			id = thdata->serverSideData_.id;
		}
		if (!id.has_value())
			return make_unexpected("IAnimationKeyframeInfo has no id.");

		if (!dataPtr)
			return make_unexpected("dataPtr is null.");

		struct SJin {
			std::vector<BoundingBox> boundingBoxes;
			TimeRange timeRange;
		};
		SJin jin;
		jin.boundingBoxes = boundingBoxes;
		jin.timeRange = timeRange;

		struct SJout {
			std::vector<std::string> ids;
		};
		//Tools::TSharedLockableDataPtr<SJout> dataOut;
		//dataOut.reset(new Tools::RWLockableObject<SJout, std::shared_mutex>);
		auto dataOut = MakeSharedLockableData<SJout>();

		std::string url = "animations/" + id.value() + "/query/animationKeyFramesBBox";
		BE_ASSERT((bool)impl.http_);
		impl.http_->AsyncPostJsonJBody(dataOut, [dataPtr, callbackfct](long httpCode, const TSharedLockableData<SJout>& joutPtr) {
				auto unlockedJout = joutPtr->GetAutoLock();
				auto& jout = unlockedJout.Get();
				auto data(dataPtr->GetAutoLock());
				auto& idsOut = data.Get();
				for (auto i : jout.ids)
					idsOut.insert(IAnimationKeyframeInfo::Id(i));
				callbackfct(httpCode, data);
			}
			,url, jin);
		return {};
	}

	expected<void, std::string> AnimationKeyframe::Delete()
	{
		auto& impl = GetImpl();
		std::optional<std::string> id;
		{
			auto thdata = GetImpl().thdata_.GetRAutoLock();
			id = thdata->serverSideData_.id;
		}
		if (!id.has_value())
			return make_unexpected("this AnimationKeyframe has no valid id.");

		struct SJin {};
		SJin jin;
		struct SJout {
			std::string id;
		};
		SJout jout;

		BE_ASSERT((bool)impl.http_);
		std::string url = "animations/" + id.value();
		auto const httpCode = impl.http_->DeleteJsonJBody(jout, url, jin, {});
		if (httpCode != 200 && httpCode != 204)
		{
			return make_unexpected(std::string("query: ") + url + " failed - code: " + std::to_string(httpCode));
		}

		return {};
	}

	std::vector<IAnimationKeyframePtr> GetITwinAnimationKeyframes(const std::string& itwinid)
	{
		std::vector<IAnimationKeyframePtr> animationsKeyframe;
		std::shared_ptr<Http> http = GetDefaultHttp();
		if (!http)
			return animationsKeyframe;

		auto ret = HttpGetWithLink<AnimationKeyframe::Impl::SAnimation>(http, "animations?iTwinId=" + itwinid, {},
			[&animationsKeyframe, http](AnimationKeyframe::Impl::SAnimation& data) -> expected<void, std::string> {
				auto p = IAnimationKeyframe::New();
				auto p2 = Tools::DynamicCast<AnimationKeyframe>(p);
				{
					auto thdata = p2->GetImpl().thdata_.GetAutoLock();
					thdata->serverSideData_ = data;
				}
				p2->GetImpl().http_ = http;
				auto p3 = MakeSharedLockableDataPtr(p);
				animationsKeyframe.push_back(p3);
				return {};
			});

		return animationsKeyframe;
	}

	void AsyncGetITwinAnimationKeyframes(const std::string& itwinid,
		 std::function<void(IAnimationKeyframePtr&)> onCreateAnimationsKeyframe,
		 std::function<void(expected<void, std::string> const&)> onFinishCallback)
	{
		std::shared_ptr<Http> http = GetDefaultHttp();
		if (!http)
		{
			onFinishCallback(make_unexpected("AsyncGetITwinAnimationKeyframes: error, no http"));
			return;
		}

		AsyncHttpGetWithLink<AnimationKeyframe::Impl::SAnimation>(http, "animations?iTwinId=" + itwinid, {},
			[onCreateAnimationsKeyframe, http](AnimationKeyframe::Impl::SAnimation& data) -> expected<void, std::string> {
				auto p = IAnimationKeyframe::New();
				auto p2 = Tools::DynamicCast<AnimationKeyframe>(p);
				{
					auto thdata = p2->GetImpl().thdata_.GetAutoLock();
					thdata->serverSideData_ = data;
				}
				p2->GetImpl().http_ = http;
				auto p3 = MakeSharedLockableDataPtr(p);
				onCreateAnimationsKeyframe(p3);
				return {};
			},
			onFinishCallback);
	}

	expected<IAnimationKeyframePtr, std::string> CreateAnimationKeyframe(const std::string& itwinid, const std::string& name)
	{
		IAnimationKeyframePtr animationKeyframe;
		std::shared_ptr<Http> const& http = GetDefaultHttp();
		if (!http)
			return animationKeyframe;

		struct SJin {
			std::string itwinid;
			std::string name;
		};
		SJin jin{ itwinid , name };
		struct SJout{
			std::string itwinid;
			std::string name;
			std::string id;
		};
		SJout jout;
		std::string url = "animations";
		if (http->PostJsonJBody(jout, url, jin) != 201)
			return make_unexpected(std::string("CreateAnimationKeyframe:") + url + " failed.");
		else
		{
			IAnimationKeyframe* p(IAnimationKeyframe::New());
			auto p2 = Tools::DynamicCast<AnimationKeyframe>(p);
			BE_ASSERT((bool)p2);
			{
				auto thdata = p2->GetImpl().thdata_.GetAutoLock();
				thdata->serverSideData_.name = name;
				thdata->serverSideData_.itwinid = itwinid;
				thdata->serverSideData_.id = jout.id;
			}
			p2->GetImpl().http_ = http;
			animationKeyframe = MakeSharedLockableDataPtr(p);
		}

		return animationKeyframe;
	}
}