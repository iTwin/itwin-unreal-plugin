/*--------------------------------------------------------------------------------------+
|
|     $Source: InstancesGroup.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "InstancesGroup.h"
#include "Config.h"
#include "../Singleton/singleton.h"

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