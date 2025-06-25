/*--------------------------------------------------------------------------------------+
|
|     $Source: InstancesGroup.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#ifndef SDK_CPPMODULES
	#include <memory>
	#include <string>
	#include <vector>
	#include <map>
	#include <set>
	#ifndef MODULE_EXPORT
		#define MODULE_EXPORT
	#endif // !MODULE_EXPORT
#endif

#include <Core/Tools/Tools.h>
#include <Core/Visualization/RefID.h>

MODULE_EXPORT namespace AdvViz::SDK
{	
	class IInstance;
	class IInstancesGroup : public Tools::ExtensionSupport, public Tools::Factory<IInstancesGroup>
	{
	public:
		/// Get the group identifier
		virtual const RefID& GetId() const = 0;
		/// Set the group identifier
		virtual void SetId(const RefID& id) = 0;
		/// Get the group name
		virtual const std::string& GetName() const = 0;
		/// Set the group name
		virtual void SetName(const std::string& name) = 0;

		/// Set type
		virtual void SetType(const std::string& name) = 0;
		// Get type
		virtual const std::string& GetType() const = 0;

		/// Set linked spline ID.
		virtual void SetLinkedSplineId(const RefID& splineId) = 0;
		// Returns linked spline ID, if any.
		virtual const std::optional<RefID>& GetLinkedSplineId() const = 0;

		typedef std::set<std::weak_ptr<IInstance>, std::owner_less<std::weak_ptr<IInstance>>> InstanceList;

		virtual InstanceList GetInstances() = 0;
		virtual void AddInstance(const std::weak_ptr<IInstance>& inst) = 0;
		virtual void RemoveInstance(const std::weak_ptr<IInstance>& inst) = 0;

	};
	
	class ADVVIZ_LINK InstancesGroup : public IInstancesGroup, Tools::TypeId<InstancesGroup>
	{
	public:
		InstancesGroup();
		virtual ~InstancesGroup();

		/// Get the identifier of the group
		const RefID& GetId() const override;
		/// Set the identifier of the group
		void SetId(const RefID& id) override;
		/// Get the group name
		const std::string& GetName() const override;
		/// Set the group name
		void SetName(const std::string& name) override;

		/// Set type
		void SetType(const std::string& name) override;
		// Get type
		const std::string& GetType() const override;

		/// Set linked spline ID.
		void SetLinkedSplineId(const RefID& splineId) override;
		// Returns linked spline ID, if any.
		const std::optional<RefID>& GetLinkedSplineId() const override;

		using Tools::TypeId<InstancesGroup>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || IInstancesGroup::IsTypeOf(i); }

		InstanceList GetInstances() override;
		void AddInstance(const std::weak_ptr<IInstance>& inst) override;
		void RemoveInstance(const std::weak_ptr<IInstance>& inst) override;

	protected:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl();
		const Impl& GetImpl() const;
	};

	typedef std::shared_ptr<IInstancesGroup> IInstancesGroupPtr;
	typedef std::vector<IInstancesGroupPtr> SharedInstGroupVect;
	typedef std::map<RefID, IInstancesGroupPtr> SharedInstGroupMap;

	
}
