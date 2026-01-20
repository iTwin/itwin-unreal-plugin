/*--------------------------------------------------------------------------------------+
|
|     $Source: PathAnimation.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <string>
#include <set>
#include "Core/Network/Network.h"
#include "Core/Tools/Tools.h"
#include <Core/Visualization/RefID.h>

namespace AdvViz::SDK
{
	class IInstance;
	class IInstancesManager;
	class IInstancesGroup;
	class ISplinesManager;
	class Http;

	using namespace Tools;

	class IAnimationPathInfo : public Tools::Factory<IAnimationPathInfo>, 
		//public WithStrongTypeId<IAnimationPathInfo>, 
		public Tools::ExtensionSupport
	{
	public:
		struct SPathAnimationInfo
		{
			std::optional<std::string> id; // animation id defined by the server
			std::optional<std::string> splineId; // id defined by the server
			std::optional<double> speed;
			std::optional<double> offsetX;
			std::optional<double> offsetY;
			std::optional<double> startTime;
			std::optional<bool> hasLoop;
			std::optional<bool> isEnabled;
		};

		virtual const RefID& GetId() const = 0;
		virtual void SetId(const RefID& id) = 0;

		virtual const RefID& GetSplineId() const = 0;
		virtual void SetSplineId(const RefID& id) = 0;

		virtual void SetSpeed(double v) = 0;
		virtual double GetSpeed() const = 0;

		virtual void SetOffsetX(double v) = 0;
		virtual double GetOffsetX() const = 0;

		virtual void SetOffsetY(double v) = 0;
		virtual double GetOffsetY() const = 0;

		virtual void SetStartTime(double v) = 0;
		virtual double GetStartTime() const = 0;

		virtual void SetIsLooping(bool b) = 0;
		virtual bool IsLooping() const = 0;

		virtual void SetIsEnabled(bool b) = 0;
		virtual bool IsEnabled() const = 0;

		virtual void SetShouldSave(bool b) = 0;
		virtual bool ShouldSave() const = 0;

		virtual void SetServerSideData(const SPathAnimationInfo &data) = 0;
		virtual const SPathAnimationInfo& GetServerSideData() const = 0;

		//struct TimelineValue {
		//	float3 translation;
		//	float4 quaternion;
		//};
	};

	using IAnimationPathInfoPtr = TSharedLockableDataPtr<IAnimationPathInfo>;
	using IAnimationPathInfoWPtr = TSharedLockableDataWPtr<IAnimationPathInfo>;

	typedef std::shared_ptr<IAnimationPathInfo> SharedPathInfo;

	class ADVVIZ_LINK  AnimationPathInfo : public IAnimationPathInfo, public Tools::TypeId<AnimationPathInfo>
	{
	public:
		AnimationPathInfo();
		virtual ~AnimationPathInfo();

		const RefID& GetId() const override;
		void SetId(const RefID& id) override;

		const RefID& GetSplineId() const override;
		void SetSplineId(const RefID& id) override;

		void SetSpeed(double v) override;
		double GetSpeed() const override;

		void SetOffsetX(double v) override;
		double GetOffsetX() const override;

		void SetOffsetY(double v) override;
		double GetOffsetY() const override;

		void SetStartTime(double v) override;
		double GetStartTime() const override;

		void SetIsLooping(bool b) override;
		bool IsLooping() const override;

		void SetIsEnabled(bool b) override;
		bool IsEnabled() const override;

		void SetShouldSave(bool b) override;
		bool ShouldSave() const override;

		void SetServerSideData(const SPathAnimationInfo& data) override;
		const SPathAnimationInfo& GetServerSideData() const override;

		using Tools::TypeId<AnimationPathInfo>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || IAnimationPathInfo::IsTypeOf(i); }

		class Impl;
		Impl& GetImpl();
		const Impl& GetImpl() const;

	private:
		const std::shared_ptr<Impl> impl_;
	};

	class IPathAnimator : public Tools::Factory<IPathAnimator>// : public Tools::CommonInterfaceClass<IPathAnimator>
	{
	public:
		virtual void SetInstanceManager(const std::shared_ptr<IInstancesManager>& instanceManager) = 0;
		virtual void SetSplinesManager(const std::shared_ptr<ISplinesManager>& splinesManager) = 0;

		virtual size_t GetNumberOfPaths() const = 0;
		virtual	SharedPathInfo FindAnimationPathInfoByDBId(const std::string& id) const = 0;
		virtual SharedPathInfo AddAnimationPathInfo() = 0;
		virtual void RemoveAnimationPathInfo(const RefID& id) = 0;
		virtual SharedPathInfo GetAnimationPathInfo(const RefID& id) const = 0;
		virtual void GetAnimationPathIds(std::set<AdvViz::SDK::RefID> &ids) const = 0;

		virtual void LoadDataFromServer(const std::string& decorationId) = 0;
		virtual void SaveDataOnServer(const std::string& decorationId) = 0;

		virtual bool HasAnimPathsToSave() const = 0;
	};
	
	using IPathAnimatorPtr = TSharedLockableDataPtr<IPathAnimator>;
	using IPathAnimatorWPtr = TSharedLockableDataWPtr<IPathAnimator>;

	typedef std::shared_ptr<IPathAnimator> SharedPathAnimator;

	class ADVVIZ_LINK  PathAnimator : public IPathAnimator, public Tools::TypeId<PathAnimator>
	{
	public:
		PathAnimator();

		void SetInstanceManager(const std::shared_ptr<IInstancesManager>& instanceManager) override;
		void SetSplinesManager(const std::shared_ptr<ISplinesManager>& splinesManager) override;

		size_t GetNumberOfPaths() const override;
		SharedPathInfo FindAnimationPathInfoByDBId(const std::string& id) const override;
		SharedPathInfo AddAnimationPathInfo() override;
		void RemoveAnimationPathInfo(const RefID& id) override;
		SharedPathInfo GetAnimationPathInfo(const RefID& id) const override;
		void GetAnimationPathIds(std::set<AdvViz::SDK::RefID>& ids) const override;

		void LoadDataFromServer(const std::string& decorationId) override;
		void SaveDataOnServer(const std::string& decorationId) override;

		bool HasAnimPathsToSave() const override;

		using Tools::TypeId<PathAnimator>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || IPathAnimator::IsTypeOf(i); }

		class Impl;
		Impl& GetImpl();
		const Impl& GetImpl() const;
	private:
		const std::unique_ptr<Impl> impl_;
	};
}
