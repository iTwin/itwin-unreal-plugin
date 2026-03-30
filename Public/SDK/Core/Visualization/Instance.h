/*--------------------------------------------------------------------------------------+
|
|     $Source: Instance.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#ifndef SDK_CPPMODULES
	#include <memory>
	#include <string>
	#include <vector>
	#ifndef MODULE_EXPORT
		#define MODULE_EXPORT
	#endif // !MODULE_EXPORT
#endif

#include <Core/Tools/Tools.h>
#include <Core/Tools/Types.h>
#include <Core/Visualization/InstancesGroup.h>
#include <Core/Visualization/SavableItem.h>

MODULE_EXPORT namespace AdvViz::SDK
{
	class IInstance : public Tools::Factory<IInstance>, public ISavableItem, public Tools::ExtensionSupport
	{
	public:
		// Unique id (at runtime), and possibly holds data base identifier.
		inline const RefID& GetRefId() const { return GetId(); }
		inline void SetRefId(const RefID& id) { SetId(id); }

		virtual const IInstancesGroupPtr& GetGroup() const = 0;
		virtual void SetGroup(const IInstancesGroupPtr& group) = 0;

		// Animation id can correspond to key frame animation or path animation
		virtual const std::string& GetAnimId() const = 0;
		virtual void SetAnimId(const std::string& id) = 0;

		virtual const std::optional<RefID>& GetAnimPathId() const = 0;
		virtual void SetAnimPathId(const RefID& id) = 0;
		virtual void RemoveAnimPathId() = 0;

		virtual const std::string& GetName() const = 0;
		virtual void SetName(const std::string& name) = 0;

		virtual const std::string& GetObjectRef() const = 0;
		virtual void SetObjectRef(const std::string& objectRef) = 0;

		virtual const dmat3x4& GetTransform() const = 0;
		virtual void SetTransform(const dmat3x4& mat) = 0;

		virtual std::optional<float3> GetColorShift() const = 0;
		virtual void SetColorShift(const float3& color) = 0;

		virtual expected<void, std::string> Update() = 0;

		virtual void OnIndexChanged(const int32_t newIndex) = 0;
	};

	class ADVVIZ_LINK Instance : public IInstance, Tools::TypeId<Instance>
	{
	public:
		/// overridden from ISavableItem
		const RefID& GetId() const override;
		void SetId(const RefID& id) override;

		ESaveStatus GetSaveStatus() const override;
		void SetSaveStatus(ESaveStatus status) override;

		/// overridden from IInstance
		void RemoveAnimPathId() override;

		const IInstancesGroupPtr& GetGroup() const override;
		void SetGroup(const IInstancesGroupPtr& group) override;

		const std::string& GetAnimId() const override;
		void SetAnimId(const std::string& id) override;

		void SetAnimPathId(const RefID& id) override;
		const std::optional<RefID>& GetAnimPathId() const override;

		const std::string& GetName() const override;
		void SetName(const std::string& name) override;

		const std::string& GetObjectRef() const override;
		void SetObjectRef(const std::string& objectRef) override;

		const dmat3x4& GetTransform() const override;
		void SetTransform(const dmat3x4& mat) override;

		std::optional<float3> GetColorShift() const override;
		void SetColorShift(const float3& color) override;

		expected<void, std::string> Update() override;

		void OnIndexChanged(const int32_t newIndex) override;

		using Tools::TypeId<Instance>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || IInstance::IsTypeOf(i); }

		Instance();
		virtual ~Instance();

	protected:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl();
	};

	using IInstancePtr = TSharedLockableDataPtr<IInstance>;
	using IInstanceWPtr = TSharedLockableDataWPtr<IInstance>;	
	typedef std::vector<IInstancePtr> SharedInstVect;
}