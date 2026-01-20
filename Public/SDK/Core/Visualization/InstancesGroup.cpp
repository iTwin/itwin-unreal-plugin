/*--------------------------------------------------------------------------------------+
|
|     $Source: InstancesGroup.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "InstancesGroup.h"
#include "Config.h"
#include "../Singleton/singleton.h"
#include "../Tools/LockableObject.h"
#include <mutex>

namespace AdvViz::SDK
{
	class InstancesGroup::Impl
	{
	public:
		RefID id_;
		std::string name_;
		std::optional<std::string> type_;
		std::optional<RefID> splineId_;
		Tools::LockableObject<
			IInstancesGroup::InstanceList
			, std::mutex > intances_;
		Impl() {}
	};

	InstancesGroup::InstancesGroup():impl_(new Impl())
	{}

	InstancesGroup::~InstancesGroup() 
	{}

	InstancesGroup::Impl& InstancesGroup::GetImpl()
	{
		return *impl_;
	}

	const InstancesGroup::Impl& InstancesGroup::GetImpl() const
	{
		return *impl_;
	}

	const RefID& InstancesGroup::GetId() const
	{
		return GetImpl().id_;
	}

	void InstancesGroup::SetId(const RefID& id)
	{
		GetImpl().id_ = id;
	}

	const std::string& InstancesGroup::GetName() const
	{
		return GetImpl().name_;
	}

	void InstancesGroup::SetName(const std::string& name)
	{
		GetImpl().name_ = name;
	}

	void InstancesGroup::SetType(const std::string& type)
	{
		GetImpl().type_ = type;
	}

	const std::string& InstancesGroup::GetType() const
	{
		if (GetImpl().type_.has_value())
			return GetImpl().type_.value();
		static std::string empty;
		return empty;
	}

	void InstancesGroup::SetLinkedSplineId(const RefID& splineId)
	{
		GetImpl().splineId_ = splineId;
	}

	const std::optional<RefID>& InstancesGroup::GetLinkedSplineId() const
	{
		return GetImpl().splineId_;
	}

	IInstancesGroup::InstanceList InstancesGroup::GetInstances()
	{
		Tools::AutoLockObject<decltype(GetImpl().intances_)> lockedInstanceList(GetImpl().intances_);
		return lockedInstanceList.Get();
	}

	void InstancesGroup::AddInstance(const std::weak_ptr<IInstance>& inst)
	{
		Tools::AutoLockObject<decltype(GetImpl().intances_)> lockedInstanceList(GetImpl().intances_);
		lockedInstanceList.Get().insert(inst);
	}

	void InstancesGroup::RemoveInstance(const std::weak_ptr<IInstance>& inst)
	{
		Tools::AutoLockObject<decltype(GetImpl().intances_)> lockedInstanceList(GetImpl().intances_);
		lockedInstanceList.Get().erase(inst);
	}

	template<>
	Tools::Factory<IInstancesGroup>::Globals::Globals()
	{
		newFct_ = []() {
			IInstancesGroup* p(static_cast<IInstancesGroup*>(new InstancesGroup()));
			return p;
			};
	}

	template<>
	Tools::Factory<IInstancesGroup>::Globals& Tools::Factory<IInstancesGroup>::GetGlobals()
	{
		return singleton<Tools::Factory<IInstancesGroup>::Globals>();
	}

}