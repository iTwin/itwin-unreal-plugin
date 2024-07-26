/*--------------------------------------------------------------------------------------+
|
|     $Source: InstancesGroup.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "InstancesGroup.h"
#include "Config.h"

namespace SDK::Core
{
	class InstancesGroup::Impl
	{
	public:
		std::string id_;
		std::string name_;
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

	const std::string& InstancesGroup::GetId()
	{
		return GetImpl().id_;
	}

	void InstancesGroup::SetId(const std::string& id)
	{
		GetImpl().id_ = id;
	}

	const std::string& InstancesGroup::GetName()
	{
		return GetImpl().name_;
	}

	void InstancesGroup::SetName(const std::string& name)
	{
		GetImpl().name_ = name;
	}

	template<>
	std::function<std::shared_ptr<IInstancesGroup>()> Tools::Factory<IInstancesGroup>::newFct_ = []() {
		std::shared_ptr<IInstancesGroup> p(static_cast<IInstancesGroup*>(new InstancesGroup()));
		return p;
		};
}