/*--------------------------------------------------------------------------------------+
|
|     $Source: InstancesGroup.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#ifndef SDK_CPPMODULES
	#include <memory>
	#include <string>
	#include <vector>
	#include <map>
	#ifndef MODULE_EXPORT
		#define MODULE_EXPORT
	#endif // !MODULE_EXPORT
#endif

#include <Core/Tools/Tools.h>

MODULE_EXPORT namespace SDK::Core
{	
	class IInstancesGroup : public Tools::Factory<IInstancesGroup>, public std::enable_shared_from_this<IInstancesGroup>
	{
	public:
		/// Get the group identifier
		virtual const std::string& GetId() = 0;
		/// Set the group identifier
		virtual void SetId(const std::string& id) = 0;
		/// Get the group name
		virtual const std::string& GetName() = 0;
		/// Set the group name
		virtual void SetName(const std::string& name) = 0;
	};

	class InstancesGroup : public Tools::ExtensionSupport, public IInstancesGroup
	{
	public:
		InstancesGroup();
		virtual ~InstancesGroup();

		/// Get the identifier of the group
		const std::string& GetId() override;
		/// Set the identifier of the group
		void SetId(const std::string& id) override;
		/// Get the group name
		const std::string& GetName() override;
		/// Set the group name
		void SetName(const std::string& name) override;

	protected:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl();
	};

	typedef std::vector<std::shared_ptr<IInstancesGroup>> SharedInstGroupVect;
	typedef std::map<std::string, std::shared_ptr<IInstancesGroup>> SharedInstGroupMap;
}
