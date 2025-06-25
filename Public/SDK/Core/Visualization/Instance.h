/*--------------------------------------------------------------------------------------+
|
|     $Source: Instance.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
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

MODULE_EXPORT namespace AdvViz::SDK
{
	class IInstance : public Tools::Factory<IInstance>, public Tools::ExtensionSupport
	{
	public:
		virtual const std::string& GetId() const = 0;
		virtual void SetId(const std::string& id) = 0;

		virtual const std::shared_ptr<IInstancesGroup>& GetGroup() const = 0;
		virtual void SetGroup(const std::shared_ptr<IInstancesGroup>& group) = 0;

		virtual const std::string& GetName() const = 0;
		virtual void SetName(const std::string& name) = 0;

		virtual const std::string& GetObjectRef() const = 0;
		virtual void SetObjectRef(const std::string& objectRef) = 0;

		virtual const dmat3x4& GetTransform() const = 0;
		virtual void SetTransform(const dmat3x4& mat) = 0;

		virtual std::optional<float3> GetColorShift() const = 0;
		virtual void SetColorShift(const float3& color) = 0;

		virtual bool ShouldSave() const = 0;
		virtual void SetShouldSave(bool value) = 0;
		virtual void SetAnimId(const std::string& id) = 0;
		virtual const std::string& GetAnimId() const = 0;

		virtual expected<void, std::string> Update() = 0;
	};

	class ADVVIZ_LINK Instance : public IInstance, Tools::TypeId<Instance>
	{
	public:
		const std::string& GetId() const override;
		void SetId(const std::string& id) override;

		const std::shared_ptr<IInstancesGroup>& GetGroup() const override;
		void SetGroup(const std::shared_ptr<IInstancesGroup>& group) override;

		const std::string& GetAnimId() const override;
		void SetAnimId(const std::string& id) override;

		const std::string& GetName() const override;
		void SetName(const std::string& name) override;

		const std::string& GetObjectRef() const override;
		void SetObjectRef(const std::string& objectRef) override;

		const dmat3x4& GetTransform() const override;
		void SetTransform(const dmat3x4& mat) override;

		std::optional<float3> GetColorShift() const override;
		void SetColorShift(const float3& color) override;

		bool ShouldSave() const override;
		void SetShouldSave(bool value) override;

		virtual expected<void, std::string> Update() override;

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

	typedef std::vector<std::shared_ptr<IInstance>> SharedInstVect;
	typedef std::shared_ptr<IInstance> IInstancePtr;
	typedef std::weak_ptr<IInstance> IInstanceWPtr;
}