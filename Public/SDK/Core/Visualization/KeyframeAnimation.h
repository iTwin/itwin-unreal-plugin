/*--------------------------------------------------------------------------------------+
|
|     $Source: KeyframeAnimation.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <string>
#include <set>
#include "Core/Network/Network.h"
#include "Core/Tools/Tools.h"

namespace AdvViz::SDK
{
	using namespace Tools;

	class IAnimationKeyframeChunk : public Tools::Factory<IAnimationKeyframeChunk>, 
		public WithStrongTypeId<IAnimationKeyframeChunk>, 
		public Tools::ExtensionSupport
	{
	public:
		// multiple of 3 floats
		virtual void SetTranslations(const std::vector<float>& v)= 0;
		virtual const std::vector<float>& GetTranslations() const = 0;

		// multiple of 4 floats
		virtual void SetQuaternions(const std::vector<float>& v)= 0;
		virtual const std::vector<float>& GetQuaternions() const = 0;

		// multiple of 3 floats
		virtual void SetScales(const std::vector<float>& v)= 0;
		virtual const std::vector<float>& GetScales() const = 0;

		virtual void SetStateIds(const std::vector<std::int8_t>& v)= 0;
		virtual const std::vector<std::int8_t>& GetStateIds() const = 0;

		virtual void SetBoundingBox(const BoundingBox& b)= 0;
		virtual const BoundingBox& GetBoundingBox() const = 0;

		virtual void SetTimeRange(const TimeRange& b)= 0;
		virtual const TimeRange& GetTimeRange() const = 0;

		virtual bool ShouldSave() const =0;

		virtual bool IsFullyLoaded() const = 0;
		virtual expected<void, std::string> Load() = 0;

		virtual expected<void, std::string> Save(std::shared_ptr<Http>& http, const std::string& animationId, const std::string& animationKeyFramesInfoId) = 0;
		virtual void AsyncSave(std::shared_ptr<Http>& http, const std::string& animationId, const std::string& animationKeyFramesInfoId, const std::function<void(long httpResult)>& callbackfct) = 0;
	};

	using IAnimationKeyframeChunkPtr = TSharedLockableDataPtr<IAnimationKeyframeChunk>;
	using IAnimationKeyframeChunkWPtr = TSharedLockableDataWPtr<IAnimationKeyframeChunk>;

	class ADVVIZ_LINK  AnimationKeyframeChunk : public IAnimationKeyframeChunk, public Tools::TypeId<AnimationKeyframeChunk>
	{
	public:
		AnimationKeyframeChunk();
		~AnimationKeyframeChunk();

		void SetTranslations(const std::vector<float> &v)override;
		const std::vector<float>& GetTranslations() const override;

		void SetQuaternions(const std::vector<float>& v)override;
		const std::vector<float>& GetQuaternions() const override;

		void SetScales(const std::vector<float>& v)override;
		const std::vector<float>& GetScales() const override;

		void SetStateIds(const std::vector<std::int8_t>& v)override;
		const std::vector<std::int8_t>& GetStateIds() const override;

		void SetBoundingBox(const BoundingBox& b)override;
		const BoundingBox& GetBoundingBox() const override;

		void SetTimeRange(const TimeRange& b)override;
		const TimeRange& GetTimeRange() const override;

		bool ShouldSave() const override;
		const IAnimationKeyframeChunk::Id& GetId() const override;

		using Tools::TypeId<AnimationKeyframeChunk>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || IAnimationKeyframeChunk::IsTypeOf(i); }

		expected<void, std::string> Save(std::shared_ptr<Http>& http, const std::string& animationId, const std::string& animationKeyFramesInfoId) override;
		void AsyncSave(std::shared_ptr<Http>& http, const std::string& animationId, const std::string& animationKeyFramesInfoId, const std::function<void(long httpResult)>& callbackfct)override;

		bool IsFullyLoaded() const override;
		expected<void, std::string> Load() override;

		struct Impl;
		Impl& GetImpl();
		const Impl& GetImpl() const;
	private:
		const std::shared_ptr<Impl> impl_;
	};

	class IAnimationKeyframeInfo : public Tools::Factory<IAnimationKeyframeInfo>, 
		public WithStrongTypeId<IAnimationKeyframeInfo>, 
		public Tools::ExtensionSupport
	{
	public:
		virtual void SetObjectId(const std::string& v) = 0;
		virtual const std::string& GetObjectId()const = 0;

		virtual void SetType(const std::string& v) = 0;
		virtual const std::string& GetType()const = 0;

		virtual void SetKeyframeInterval(float v) = 0;
		virtual float GetKeyframeInterval()const = 0;

		virtual void SetStartTime(double v) = 0;
		virtual double GetStartTime()const = 0;

		virtual void SetKeyframeCount(int v) = 0;
		virtual int GetKeyframeCount()const = 0;

		virtual void SetChunkSize(int v) = 0;
		virtual int GetChunkSize()const = 0;

		virtual void SetStates(const std::vector<std::string> &v) = 0;
		virtual const std::vector<std::string> &GetStates()const = 0;

		virtual void SetTags(const std::vector<std::string> &v) = 0;
		virtual const std::vector<std::string> &GetTags()const = 0;

		virtual void SetGCS(const GCS& v) = 0;
		virtual const GCS& GetGCS()const = 0;

		virtual bool ShouldSave() const;

		virtual expected<void, std::string> Save(std::shared_ptr<Http>& http, bool bChunk = true) = 0;

		//note: doesn't save chunks
		virtual void AsyncSave(std::shared_ptr<Http>& http, const std::function<void(const IAnimationKeyframeInfo::Id& id)>& callBackfct) =0;

		virtual IAnimationKeyframeChunkPtr CreateChunk() = 0;
		virtual size_t GetChunkCount() const = 0;
		virtual IAnimationKeyframeChunkPtr GetChunk(size_t i) const = 0;

		struct TimelineResult {
			std::vector<float> translations;
			std::vector<float> quaternions;
			std::optional<std::vector<float>> scales;
			std::optional<std::vector<std::int8_t>> stateIds;
			BoundingBox boundingBox;
			TimeRange timeRange;
		};
		virtual expected<void, std::string> QueryKeyframes(TimelineResult& result, double time, double duration) const = 0;
		virtual expected<void, std::string> AsyncQueryKeyframes(TSharedLockableData<TimelineResult>& dataPtr, const std::function<void(long httpResult, const TSharedLockableData<TimelineResult>&)> &callbackfct, double time, double duration) const = 0;

		struct TimelineValue {
			float3 translation;
			float4 quaternion;
			std::optional<float3> scale;
			std::optional<std::int8_t> stateId;
		};
		virtual expected<void, std::string> GetInterpolatedValue(const TimelineResult& result, double time, TimelineValue& value) const = 0;

		virtual expected<void, std::string> Delete() = 0;
		virtual expected<void, std::string> DeleteChunk(size_t chunkId) = 0;
	};

	using IAnimationKeyframeInfoPtr = TSharedLockableDataPtr<IAnimationKeyframeInfo>;
	using IAnimationKeyframeInfoWPtr = TSharedLockableDataWPtr<IAnimationKeyframeInfo>;

	class ADVVIZ_LINK  AnimationKeyframeInfo : public IAnimationKeyframeInfo, public Tools::TypeId<AnimationKeyframeInfo>
	{
	public:
		AnimationKeyframeInfo();
		virtual ~AnimationKeyframeInfo();

		void SetObjectId(const std::string& v) override;
		const std::string& GetObjectId() const override;

		void SetType(const std::string& v) override;
		const std::string& GetType() const override;

		void SetKeyframeInterval(float v) override;
		float GetKeyframeInterval() const override;

		void SetStartTime(double v) override;
		double GetStartTime() const override;

		void SetKeyframeCount(int v) override;
		int GetKeyframeCount() const override;

		void SetChunkSize(int v) override;
		int GetChunkSize() const override;

		void SetStates(const std::vector<std::string> &v) override;
		const std::vector<std::string>& GetStates() const override;

		void SetTags(const std::vector<std::string> &v) override;
		const std::vector<std::string>& GetTags() const override;
		
		void SetGCS(const GCS &v) override;
		const GCS &GetGCS() const override;

		expected<void, std::string> Save(std::shared_ptr<Http>& http, bool bChunks=true) override;
		void AsyncSave(std::shared_ptr<Http>& http, const std::function<void(const IAnimationKeyframeInfo::Id& id)>& callBackfct) override;
		IAnimationKeyframeChunkPtr CreateChunk() override;

		size_t GetChunkCount() const override;
		IAnimationKeyframeChunkPtr GetChunk(size_t i) const override;

		bool ShouldSave() const override;
		const IAnimationKeyframeInfo::Id& GetId() const override;

		using Tools::TypeId<AnimationKeyframeInfo>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || IAnimationKeyframeInfo::IsTypeOf(i); }

		expected<void, std::string> QueryKeyframes(TimelineResult& result, double time, double duration) const override;
		expected<void, std::string> AsyncQueryKeyframes(TSharedLockableData<TimelineResult>& dataPtr, const std::function<void(long httpResult, const TSharedLockableData<TimelineResult>&)> &callbackfct, double time, double duration) const override;

		expected<void, std::string> GetInterpolatedValue(const TimelineResult& result, double time, TimelineValue& value) const override;

		expected<void, std::string> Delete() override;
		expected<void, std::string> DeleteChunk(size_t chunkId) override;

		class Impl;
		Impl& GetImpl();
		const Impl& GetImpl() const;

	private:
		const std::shared_ptr<Impl> impl_;
	};


	class IAnimationKeyframe : public Tools::CommonInterfaceClass<IAnimationKeyframe>
	{
	public:
		virtual bool ShouldSave() const = 0;
		virtual expected<void, std::string> LoadAnimationKeyFrameInfos() = 0;
		virtual expected<void, std::string> Save(std::shared_ptr<Http>& http, bool bInfos = true) = 0;
		virtual expected<void, std::string> Delete() = 0;
		virtual IAnimationKeyframeInfoPtr AddAnimationKeyframeInfo(const std::string& objectId) = 0;
		virtual expected<IAnimationKeyframeInfoPtr, std::string> LoadKeyframesInfo(const IAnimationKeyframeInfo::Id& animationKeyframeInfoId) = 0;
		virtual IAnimationKeyframeInfoPtr GetAnimationKeyframeInfo(const IAnimationKeyframeInfo::Id& animationKeyframeInfoId) const = 0;
		virtual expected<void, std::string> AsyncQueryKeyframesInfos(const Tools::TSharedLockableData<std::set<IAnimationKeyframeInfo::Id>>& data
			, const std::function<void(long, std::set<IAnimationKeyframeInfo::Id>&)>& callbackfct
			, const std::vector<BoundingBox>& boundingBoxes
			, const TimeRange& timeRange) const = 0;
		virtual expected<std::vector<IAnimationKeyframeInfo::Id>, std::string> QueryKeyframesInfos(const std::vector<BoundingBox>& boundingBoxes, const TimeRange& timeRange) const = 0;
		virtual std::vector<IAnimationKeyframeInfo::Id> GetAnimationKeyframeInfoIds() const = 0;
		virtual std::shared_ptr<Tools::IGCSTransform> GetGCSTransform() const = 0;
		virtual void SetGCSTransform(const std::shared_ptr<Tools::IGCSTransform>&) = 0;
	};
	
	using IAnimationKeyframePtr = TSharedLockableDataPtr<IAnimationKeyframe>;
	using IAnimationKeyframeWPtr = TSharedLockableDataWPtr<IAnimationKeyframe>;

	class ADVVIZ_LINK  AnimationKeyframe : public IAnimationKeyframe, public Tools::TypeId<AnimationKeyframe>
	{
	public:
		AnimationKeyframe();
		virtual ~AnimationKeyframe();
		
		expected<void, std::string> LoadAnimationKeyFrameInfos() override;
		expected<void, std::string> Save(std::shared_ptr<Http>& http, bool bInfos = true) override;
		expected<void, std::string> Delete() override;

		IAnimationKeyframeInfoPtr AddAnimationKeyframeInfo(const std::string& objectId) override;
		expected<IAnimationKeyframeInfoPtr, std::string> LoadKeyframesInfo(const IAnimationKeyframeInfo::Id& animationKeyframeInfoId) override;
		IAnimationKeyframeInfoPtr GetAnimationKeyframeInfo(const IAnimationKeyframeInfo::Id& animationKeyframeInfoId) const override;
		std::vector<IAnimationKeyframeInfo::Id> GetAnimationKeyframeInfoIds() const override;

		expected<std::vector<IAnimationKeyframeInfo::Id>, std::string> QueryKeyframesInfos(const std::vector<BoundingBox>& boundingBoxes, const TimeRange& timeRange) const override;

		expected<void, std::string> AsyncQueryKeyframesInfos(const Tools::TSharedLockableData<std::set<IAnimationKeyframeInfo::Id>>& data
			, const std::function<void(long, std::set<IAnimationKeyframeInfo::Id>&)> &callbackfct
			, const std::vector<BoundingBox>& boundingBoxes
			, const TimeRange& timeRange) const override;
		
		bool ShouldSave() const override;
		const IAnimationKeyframe::Id& GetId() const override;

		using Tools::TypeId<AnimationKeyframe>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || IAnimationKeyframe::IsTypeOf(i); }

		std::shared_ptr<Tools::IGCSTransform> GetGCSTransform() const override;
		void SetGCSTransform(const std::shared_ptr<Tools::IGCSTransform>&) override;

		struct Impl;
		Impl& GetImpl();
		const Impl& GetImpl() const;
	private:
		const std::unique_ptr<Impl> impl_;
	};

	ADVVIZ_LINK std::vector<IAnimationKeyframePtr> GetITwinAnimationKeyframes(const std::string& itwinid);

	ADVVIZ_LINK expected<IAnimationKeyframePtr, std::string> CreateAnimationKeyframe(const std::string& itwinid, const std::string& name);

}