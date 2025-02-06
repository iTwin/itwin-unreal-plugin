/*--------------------------------------------------------------------------------------+
|
|     $Source: Timeline.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once
#include <string>
#include "Core/Network/Network.h"
#include "Core/Tools/Tools.h"

namespace SDK::Core
{
	class ITimelineKeyframe : public Tools::Factory<ITimelineKeyframe>, public WithStrongTypeId<ITimelineKeyframe>, public Tools::IDynType
	{
	public:
		// note: the data are directly streamed to/from decoration server 
		struct CameraData {
			dmat3x4 transform = { 1.,0.,0.,0., 0.,1.,0.,0., 0.,0.,1.,0. };
			bool isPause = false;
		};
		struct AtmoData
		{
			std::string time;
			float cloudCoverage = 0.0f;
			float fog = 0.0f;
		};
		struct SynchroData
		{
			std::string date;
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

		// should be use only by ITimelineClip
		virtual void InternalCreate(const KeyframeData& data, bool markAsChanged = true) = 0;
	};

	class TimelineKeyframe : public Tools::ExtensionSupport, public ITimelineKeyframe, public Tools::TypeId<TimelineKeyframe>
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

		 std::uint64_t GetDynTypeId() override { return GetTypeId(); }
		 bool IsTypeOf(std::uint64_t i) override { return (i == GetTypeId()); }
	protected:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl();
		const Impl& GetImpl() const;
	};


	class ITimelineClip : public Tools::Factory<ITimelineClip>, public WithStrongTypeId<ITimelineClip>, public Tools::IDynType
	{
	public:
		virtual expected<std::shared_ptr<ITimelineKeyframe>, std::string> GetKeyframe(double time) const = 0;
		virtual expected<std::shared_ptr<ITimelineKeyframe>, std::string> GetKeyframeByIndex(size_t index) const = 0;
		virtual expected<size_t, std::string > GetKeyframeIndex(double time) const = 0;
		virtual size_t GetKeyframeCount() const = 0;
		virtual expected<std::shared_ptr<ITimelineKeyframe>, std::string> AddKeyframe(const ITimelineKeyframe::KeyframeData& data) = 0;
		virtual expected<void, std::string> RemoveKeyframe(std::shared_ptr<ITimelineKeyframe>& k) = 0;
		virtual expected<void, std::string> Load(const std::string& sceneId, const std::string& accessToken, const Id& clipId) = 0;
		virtual expected<void, std::string> Save(const std::string& sceneId, const std::string& accessToken) = 0;
		virtual const std::string& GetName() const = 0;
		virtual void SetName(const std::string& name) = 0;
		virtual bool IsEnabled() const = 0;
		virtual void SetEnable(bool e) = 0;
		virtual bool ShouldSave() const = 0;
		virtual void SetShouldSave(bool value) = 0;

		friend class ITimelineKeyframe;
	};

	class TimelineClip : public Tools::ExtensionSupport, public ITimelineClip, public Tools::TypeId<TimelineClip>
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
		expected<void, std::string> Load(const std::string& sceneId, const std::string& accessToken, const ITimelineClip::Id& clipId) override;
		expected<void, std::string> Save(const std::string& sceneId, const std::string& accessToken) override;
		const ITimelineClip::Id& GetId() const override;
		const std::string& GetName() const override;
		void SetName(const std::string& name)override;
		bool IsEnabled() const override;
		void SetEnable(bool e)override;
		bool ShouldSave() const override;
		void SetShouldSave(bool value) override;

		std::uint64_t GetDynTypeId() override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) override { return (i == GetTypeId()); }
	protected:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl();
		const Impl& GetImpl() const;
	};

	class ITimeline : public Tools::Factory<ITimeline>, public WithStrongTypeId<ITimeline>, public Tools::IDynType
	{
	public:
		virtual expected<std::shared_ptr<ITimelineClip>, std::string> GetClipByIndex(size_t index) const = 0;
		virtual std::shared_ptr<ITimelineClip> AddClip(const std::string &name) = 0;
		virtual expected<void, std::string> RemoveClip(size_t index) = 0;
		virtual size_t GetClipCount() const = 0;
		virtual expected<void, std::string> Load(const std::string& sceneId, const std::string& accessToken, const ITimeline::Id& timelineId) = 0;
		virtual expected<void, std::string> Save(const std::string& sceneId, const std::string& accessToken) = 0;
		virtual bool ShouldSave() const = 0;
		virtual void SetShouldSave(bool value) = 0;
	};

	class Timeline : public Tools::ExtensionSupport, public ITimeline, public Tools::TypeId<Timeline>
	{
	public:
		Timeline();
		~Timeline();
		expected<std::shared_ptr<ITimelineClip>, std::string> GetClipByIndex(size_t index) const override;
		std::shared_ptr<ITimelineClip> AddClip(const std::string& name) override;
		expected<void, std::string> RemoveClip(size_t index) override;
		size_t GetClipCount() const override;
		expected<void, std::string> Load(const std::string& sceneId, const std::string& accessToken, const ITimeline::Id& timelineId) override;
		expected<void, std::string> Save(const std::string& sceneId, const std::string& accessToken) override;
		bool ShouldSave() const override;
		void SetShouldSave(bool value) override;
		const ITimeline::Id& GetId() const override;

		std::uint64_t GetDynTypeId() override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) override { return (i == GetTypeId()); }
	protected:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl();
		const Impl& GetImpl() const;
	};
	
	struct SSceneTimelineInfo
	{
		std::string name;
		ITimeline::Id id;
	};
	expected<std::vector<SSceneTimelineInfo>, std::string> GetSceneTimelines(const std::string& sceneId, const std::string& accessToken);
	expected<ITimeline::Id, std::string> AddSceneTimeline(const std::string& sceneId, const std::string& accessToken, const std::string& sceneName);
}
