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

MODULE_EXPORT namespace SDK::Core 
{
	class IInstancesManager : public Tools::Factory<IInstancesManager>
	{
	public:
		/// Load the data from the server
		virtual void LoadDataFromServer(const std::string& decorationId, const std::string& accessToken) = 0;
		/// Save the data on the server
		virtual void SaveDataOnServer(const std::string& decorationId, const std::string& accessToken) = 0;

		/// Get the instance count by object reference
		virtual uint64_t GetInstanceCountByObjectRef(const std::string& objectRef) const = 0;
		/// Set the instance count by object reference
		virtual void SetInstanceCountByObjectRef(const std::string& objectRef, uint64_t count) = 0;
		/// Get instances by object reference
		virtual void GetInstancesByObjectRef(const std::string& objectRef, SharedInstVect& instances) const = 0;
		/// Remove instances by object reference (indices must be in descending order)
		virtual void RemoveInstancesByObjectRef(const std::string& objectRef, const std::vector<int32_t> indicesInDescendingOrder) = 0;
		/// Check if there are instances
		virtual bool HasInstances() const = 0;
		/// Check if there are instances to save on the server
		virtual bool HasInstancesToSave() const = 0;
		/// Get object references
		virtual std::vector<std::string> GetObjectReferences() const = 0;

		/// Add instances group
		virtual void AddInstancesGroup(const std::shared_ptr<IInstancesGroup>& instancesGroup) = 0;
		/// Get instances group
		virtual const SharedInstGroupVect& GetInstancesGroups() const = 0;
	};

	class InstancesManager : public Tools::ExtensionSupport, public IInstancesManager
	{
	public:
		/// Load the data from the server
		void LoadDataFromServer(const std::string& decorationId, const std::string& accessToken) override;
		/// Save the data on the server
		void SaveDataOnServer(const std::string& decorationId, const std::string& accessToken) override;

		/// Get the instance count by object reference
		uint64_t GetInstanceCountByObjectRef(const std::string& objectRef) const override;
		/// Set the instance count by object reference
		void SetInstanceCountByObjectRef(const std::string& objectRef, uint64_t count) override;
		/// Get instances by object reference
		void GetInstancesByObjectRef(const std::string& objectRef, SharedInstVect& instances) const override;
		/// Remove instances by object reference (indices must be in descending order)
		void RemoveInstancesByObjectRef(const std::string& objectRef, const std::vector<int32_t> indicesInDescendingOrder) override;
		/// Check if there are instances
		bool HasInstances() const override;
		/// Check if there are instances to save on the server
		bool HasInstancesToSave() const override;
		/// Get object references
		std::vector<std::string> GetObjectReferences() const override;

		/// Add instances group
		void AddInstancesGroup(const std::shared_ptr<IInstancesGroup>& instancesGroup) override;
		/// Get instances groups
		const SharedInstGroupVect& GetInstancesGroups() const override;

		/// Set Http server to use (if none provided, the default created by Config is used.)
		void SetHttp(std::shared_ptr<Http> http);

		InstancesManager(InstancesManager&) = delete;
		InstancesManager(InstancesManager&&) = delete;
		virtual ~InstancesManager();
		InstancesManager();
		
	protected:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl();
		const Impl& GetImpl() const;
	};
}