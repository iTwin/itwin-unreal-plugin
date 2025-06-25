/*--------------------------------------------------------------------------------------+
|
|     $Source: KeyframeAnimator.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once
#include <string>
#include "Core/Tools/Tools.h"
#include "KeyframeAnimation.h"

namespace AdvViz::SDK
{
	class IInstancesManager;
	class IInstancesGroup;

	class IKeyframeAnimator : public Tools::CommonInterfaceClass<IKeyframeAnimator>
	{
	public:
		struct Stat
		{
			unsigned numberVisibleItems;
			unsigned beforeCullingItems;
			unsigned itemsHidden;
			std::vector<unsigned> numberPerBbox;
		};
		virtual expected<void, std::string> AssociateInstances(const std::shared_ptr<IInstancesGroup>& gp) = 0;
		virtual expected<void, std::string> Process(float time, const std::vector<AdvViz::SDK::BoundingBox>& boundingBoxes, bool cameraMoved) = 0;
		virtual void SetAnimation(const IAnimationKeyframePtr& animationKeyframe) = 0;
		virtual IAnimationKeyframePtr GetAnimation() const = 0;
		virtual void SetInstanceManager(const std::shared_ptr<IInstancesManager>& instanceManager) = 0;
		virtual void OnResetTime() = 0;
		virtual void EnableStat(bool b) = 0;
		virtual const Stat& GetStat() const = 0;
	};

	class ADVVIZ_LINK KeyframeAnimator : public IKeyframeAnimator, protected Tools::TypeId<KeyframeAnimator>
	{
	public:
		KeyframeAnimator();
		expected<void, std::string> AssociateInstances(const std::shared_ptr<IInstancesGroup>& gp) override;
		expected<void, std::string> Process(float time, const std::vector<AdvViz::SDK::BoundingBox>& boundingBoxes, bool cameraMoved) override;
		void SetAnimation(const IAnimationKeyframePtr& animationKeyframe) override;
		IAnimationKeyframePtr GetAnimation() const override;
		void SetInstanceManager(const std::shared_ptr<IInstancesManager>& instanceManager) override;
		void OnResetTime() override;
		void EnableStat(bool b)override;
		const IKeyframeAnimator::Stat& GetStat() const override;

		using Tools::TypeId<KeyframeAnimator>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || IKeyframeAnimator::IsTypeOf(i); }

		const Id& GetId() const override;

		class Impl;
		Impl& GetImpl();
		const Impl& GetImpl() const;
	private:
		const std::unique_ptr<Impl> impl_;

		void QueryKeyFrameInfos(const std::vector<BoundingBox>& boundingBoxes, const TimeRange& timeRange, bool updateCurrentInfo);

	};

}
