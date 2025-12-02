/*--------------------------------------------------------------------------------------+
|
|     $Source: InstancesManager.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#include <string>
#include "Core/Network/Network.h"
#include "Core/Tools/Tools.h"
#include "Core/Visualization/Instance.h"
#include "Core/Visualization/InstancesGroup.h"

MODULE_EXPORT namespace AdvViz::SDK 
{
	class ISplinesManager;
	class IPathAnimator;

	class IInstancesManager : public Tools::Factory<IInstancesManager>, public Tools::ExtensionSupport
	{
	public:
		/// Load the data from the server
		virtual void LoadDataFromServer(const std::string& decorationId, const IInstancesGroupPtr& defaultGroup = {}) = 0;
		/// Save the data on the server
		virtual void SaveDataOnServer(const std::string& decorationId) = 0;

		/// Get the instance count by object reference
		virtual uint64_t GetInstanceCountByObjectRef(const std::string& objectRef, const RefID& gpId) const = 0;
		/// Set the instance count by object reference
		virtual void SetInstanceCountByObjectRef(const std::string& objectRef, const RefID& gpId, uint64_t count) = 0;
		virtual std::shared_ptr<IInstance> AddInstance(const std::string& objectRef, const RefID& gpId) = 0;
		/// Get instances by object reference
		virtual const SharedInstVect& GetInstancesByObjectRef(const std::string& objectRef, const RefID& gpId) const = 0;
		/// Remove instances by object reference (indices must be in descending order)
		virtual void RemoveInstancesByObjectRef(const std::string& objectRef, const RefID& gpId, const std::vector<int32_t> indicesInDescendingOrder) = 0;
		virtual void RemoveGroupInstances(const RefID& gpId) = 0;
		/// Called after restoring instances (undo/redo system).
		virtual void OnInstancesRestored(const std::string& objectRef, const RefID& gpId, const std::vector<RefID>& restoredInstances) = 0;
		/// Check if there are instances
		virtual bool HasInstances() const = 0;
		/// Check if there are instances to save on the server
		virtual bool HasInstancesToSave() const = 0;
		/// Get object references
		virtual std::vector<std::pair<std::string/*objRef*/, RefID /*gpId*/>> GetObjectReferences() const = 0;

		/// Add instances group
		virtual void AddInstancesGroup(const IInstancesGroupPtr& group) = 0;
		virtual void RemoveInstancesGroup(const IInstancesGroupPtr& group) = 0;
		/// Get instances group
		virtual const SharedInstGroupVect& GetInstancesGroups() const = 0;
		virtual IInstancesGroupPtr GetInstancesGroup(const RefID& gpId) const = 0;
		virtual IInstancesGroupPtr GetInstancesGroupByName(const std::string& name) const = 0;
		virtual IInstancesGroupPtr GetInstancesGroupBySplineID(const RefID& splineId) const = 0;

		virtual void SetSplineManager(std::shared_ptr<ISplinesManager> const& splineManager) = 0;
		virtual void SetAnimPathManager(std::shared_ptr<IPathAnimator> const& animPathManager) = 0;
	};

	class ADVVIZ_LINK InstancesManager : public IInstancesManager, Tools::TypeId<InstancesManager>
	{
	public:
		/// Load the data from the server
		void LoadDataFromServer(const std::string& decorationId, const IInstancesGroupPtr& defaultGroup = {}) override;
		/// Save the data on the server
		void SaveDataOnServer(const std::string& decorationId) override;

		/// Get the instance count by object reference
		/// if gpId is empty string, return total instances
		uint64_t GetInstanceCountByObjectRef(const std::string& objectRef, const RefID& gpId) const override;
		/// Set the instance count by object reference
		void SetInstanceCountByObjectRef(const std::string& objectRef, const RefID& gpId, uint64_t count) override;
		std::shared_ptr<IInstance> AddInstance(const std::string& objectRef, const RefID& gpId) override;
		/// Get instances by object reference
		const SharedInstVect& GetInstancesByObjectRef(const std::string& objectRef, const RefID& gpId) const override;
		/// Remove instances by object reference (indices must be in descending order)
		void RemoveInstancesByObjectRef(const std::string& objectRef, const RefID& gpId, const std::vector<int32_t> indicesInDescendingOrder) override;
		void RemoveGroupInstances(const RefID& gpId) override;
		/// Called after restoring instances (undo/redo system).
		void OnInstancesRestored(const std::string& objectRef, const RefID& gpId, const std::vector<RefID>& restoredInstances) override;
		/// Check if there are instances
		bool HasInstances() const override;
		/// Check if there are instances to save on the server
		bool HasInstancesToSave() const override;
		/// Get object references
		std::vector<std::pair<std::string/*objRef*/, RefID /*gpId*/>> GetObjectReferences() const override;

		/// Add instances group
		void AddInstancesGroup(const IInstancesGroupPtr& instancesGroup) override;
		void RemoveInstancesGroup(const IInstancesGroupPtr& group) override;

		/// Get instances groups
		const SharedInstGroupVect& GetInstancesGroups() const override;
		IInstancesGroupPtr GetInstancesGroup(const RefID& gpId) const override;
		IInstancesGroupPtr GetInstancesGroupByName(const std::string& name) const override;
		IInstancesGroupPtr GetInstancesGroupBySplineID(const RefID& splineId) const override;

		void SetSplineManager(std::shared_ptr<ISplinesManager> const& splineManager) override;
		void SetAnimPathManager(std::shared_ptr<IPathAnimator> const& animPathManager) override;

		/// Set Http server to use (if none provided, the default created by Config is used.)
		void SetHttp(std::shared_ptr<Http> http);

		InstancesManager(InstancesManager&) = delete;
		InstancesManager(InstancesManager&&) = delete;
		virtual ~InstancesManager();
		InstancesManager();
		
		using Tools::TypeId<InstancesManager>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || IInstancesManager::IsTypeOf(i); }

	protected:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl();
		const Impl& GetImpl() const;
	};
}