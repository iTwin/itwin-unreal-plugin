/*--------------------------------------------------------------------------------------+
|
|     $Source: Timeline.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <cmath>
#include <string>
#include <type_traits>

#include "Core/Network/Network.h"
#include "Core/Tools/Tools.h"

namespace AdvViz::SDK
{
	class ADVVIZ_LINK  ITimelineKeyframe : public Tools::CommonInterfaceClass<ITimelineKeyframe>
	{
	public:
		// note: the data are directly streamed to/from decoration server 
		struct CameraData {
			dmat3x4 transform = { 1.,0.,0.,0., 0.,1.,0.,0., 0.,0.,1.,0. };
			bool isPause = false;
		};
		struct AtmoData
		{
			std::string time; // heliodon date
			float cloudCoverage = 0.0f;
			float fog = 0.0f;
		};
		struct SynchroData
		{
			std::string date;
			rfl::Skip<std::string> scheduleId; //for sceneAPI only
		};
		// note: the data are directly streamed to/from decoration server 
		struct KeyframeData {
			double time = 0.0;
			std::optional<CameraData> camera;
			std::optional<AtmoData> atmo;
			std::optional<SynchroData> synchro;
			std::optional<std::string> id; // is set by decoration service, optional not set when not already saved
		};
		// keyframe should be sorted by time first
		virtual bool CompareForOrder(ITimelineKeyframe*) const = 0;
		virtual const KeyframeData& GetData() const = 0;
		virtual bool Changed() const = 0;
		// note: time and id are immutable, changes on these properties will be ignored. If you need to change them recreate a new keyframe in ITimelineClip.
		virtual void Update(const KeyframeData& data) = 0;
		virtual bool ShouldSave() const = 0;
		virtual void SetShouldSave(bool value) = 0;
		virtual void SetSnapshotId(const std::string& Id) = 0;
		virtual std::string GetSnapshotId() const = 0;

		// should be use only by ITimelineClip
		virtual void InternalCreate(const KeyframeData& data, bool markAsChanged = true) = 0;
	};

	class ADVVIZ_LINK  TimelineKeyframe : public ITimelineKeyframe, public Tools::TypeId<TimelineKeyframe>
	{
	public:
		 TimelineKeyframe();
		 bool CompareForOrder(ITimelineKeyframe*) const override;
		 void InternalCreate(const KeyframeData& data, bool markAsChanged = true) override;
		 const KeyframeData& GetData() const override;
		 bool Changed() const override;
		 bool ShouldSave() const override;
		 void SetShouldSave(bool value)  override;
		 const Id& GetId() const override;
		 void Update(const KeyframeData& data) override;
		 virtual ~TimelineKeyframe();

		 void SetSnapshotId(const std::string& Id);
		 std::string GetSnapshotId() const;

		 using Tools::TypeId<TimelineKeyframe>::GetTypeId;
		 std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		 bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()); }
	protected:
		struct Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl();
		const Impl& GetImpl() const;
	};


	class ADVVIZ_LINK  ITimelineClip : public Tools::CommonInterfaceClass<ITimelineClip>
	{
	public:
		virtual expected<std::shared_ptr<ITimelineKeyframe>, std::string> GetKeyframe(double time) const = 0;
		virtual expected<std::shared_ptr<ITimelineKeyframe>, std::string> GetKeyframeByIndex(size_t index) const = 0;
		virtual expected<size_t, std::string > GetKeyframeIndex(double time) const = 0;
		virtual size_t GetKeyframeCount() const = 0;
		virtual expected<std::shared_ptr<ITimelineKeyframe>, std::string> AddKeyframe(const ITimelineKeyframe::KeyframeData& data) = 0;
		virtual expected<void, std::string> RemoveKeyframe(std::shared_ptr<ITimelineKeyframe>& k) = 0;
		virtual expected<void, std::string> Load(const std::string& sceneId, const Id& clipId) = 0;
		virtual expected<void, std::string> Save(const std::string& sceneId) = 0;
		virtual const std::string& GetName() const = 0;
		virtual void SetName(const std::string& name) = 0;
		virtual bool IsEnabled() const = 0;
		virtual void SetEnable(bool e) = 0;
		virtual bool ShouldSave() const = 0;
		virtual void SetShouldSave(bool value) = 0;
		virtual void SetSnapshotId(const std::string& Id) = 0;
		virtual std::string GetSnapshotId() const = 0;
		virtual void GetKeyFrameSnapshotIds(std::vector<std::string>& Ids) const = 0;
		virtual void SetId(const ITimelineClip::Id&) = 0;
		virtual const ITimelineClip::Id& GetId() const = 0;

		friend class ITimelineKeyframe;
	};

	class ADVVIZ_LINK  TimelineClip : public ITimelineClip, public Tools::TypeId<TimelineClip>
	{
	public:
		TimelineClip();
		~TimelineClip();
		expected<std::shared_ptr<ITimelineKeyframe>, std::string> GetKeyframe(double time) const override;
		expected<std::shared_ptr<ITimelineKeyframe>, std::string> GetKeyframeByIndex(size_t index) const override;
		expected<size_t, std::string > GetKeyframeIndex(double time) const override;
		size_t GetKeyframeCount() const override;
		expected<std::shared_ptr<ITimelineKeyframe>, std::string> AddKeyframe(const ITimelineKeyframe::KeyframeData& data) override;
		expected<void, std::string> RemoveKeyframe(std::shared_ptr<ITimelineKeyframe>& k) override;
		expected<void, std::string> Load(const std::string& sceneId, const ITimelineClip::Id& clipId) override;
		expected<void, std::string> Save(const std::string& sceneId) override;
		const ITimelineClip::Id& GetId() const override;
		void SetId(const ITimelineClip::Id&) override;

		const std::string& GetName() const override;
		void SetName(const std::string& name)override;
		bool IsEnabled() const override;
		void SetEnable(bool e)override;
		bool ShouldSave() const override;
		void SetShouldSave(bool value) override;

		void SetSnapshotId(const std::string& Id);
		std::string GetSnapshotId() const;
		void GetKeyFrameSnapshotIds(std::vector<std::string>& Ids) const;

		using Tools::TypeId<TimelineClip>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()); }
	protected:
		struct Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl();
		const Impl& GetImpl() const;
	};

	class ADVVIZ_LINK  ITimeline : public Tools::CommonInterfaceClass<ITimeline>
	{
	public:
		virtual expected<std::shared_ptr<ITimelineClip>, std::string> GetClipByIndex(size_t index) const = 0;
		virtual std::shared_ptr<ITimelineClip> AddClip(const std::string &name) = 0;
		virtual expected<void, std::string> RemoveClip(size_t index) = 0;
		virtual void MoveClip(size_t indexSrc, size_t indexDst) = 0;
		virtual size_t GetClipCount() const = 0;
		virtual expected<void, std::string> Load(const std::string& sceneId, const ITimeline::Id& timelineId) = 0;
		virtual expected<void, std::string> Save(const std::string& sceneId) = 0;
		virtual bool ShouldSave() const = 0;
		virtual void SetShouldSave(bool value) = 0;
		virtual void SetId(const ITimeline::Id&) = 0;

		//sceneAPI functions
		virtual std::vector<std::shared_ptr<ITimelineClip>> GetObsoleteClips() const = 0;
		virtual void RemoveObsoleteClip(const std::shared_ptr<ITimelineClip>&) = 0;
	};

	class ADVVIZ_LINK  Timeline : public ITimeline, public Tools::TypeId<Timeline>
	{
	public:
		Timeline();
		~Timeline();
		expected<std::shared_ptr<ITimelineClip>, std::string> GetClipByIndex(size_t index) const override;
		std::shared_ptr<ITimelineClip> AddClip(const std::string& name) override;
		expected<void, std::string> RemoveClip(size_t index) override;
		void MoveClip(size_t indexSrc, size_t indexDst) override;
		size_t GetClipCount() const override;

		bool ShouldSave() const override;
		void SetShouldSave(bool value) override;
		const ITimeline::Id& GetId() const override;
		void SetId(const ITimeline::Id&) override;

		using Tools::TypeId<Timeline>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()); }


		//decoration service function
		//decoration service function
		expected<void, std::string> Load(const std::string& sceneId, const ITimeline::Id& timelineId) override;
		expected<void, std::string> Save(const std::string& sceneId) override;


		//sceneAPI functions
		std::vector<std::shared_ptr<ITimelineClip>> GetObsoleteClips() const override;
		void RemoveObsoleteClip(const std::shared_ptr<ITimelineClip>&) override;

	protected:
		struct Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl();
		const Impl& GetImpl() const;
	};
	
	struct SSceneTimelineInfo
	{
		std::string name;
		ITimeline::Id id;
	};
	ADVVIZ_LINK expected<std::vector<SSceneTimelineInfo>, std::string> GetSceneTimelines(const std::string& sceneId);
	ADVVIZ_LINK expected<ITimeline::Id, std::string> AddSceneTimeline(const std::string& sceneId, const std::string& sceneName);
	/// Round times to ms, to enable equality comparisons
	template<typename T> T RoundTime(T d)
	{
		static_assert(std::is_floating_point_v<T>);
		return std::round(d * T(1000.)) / T(1000.);
	}
}
