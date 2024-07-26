/*--------------------------------------------------------------------------------------+
|
|     $Source: Instance.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
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

MODULE_EXPORT namespace SDK::Core
{
	enum EUpdateTarget : int
	{
		Database = 0,
		Display = 1
	};

	class IInstance : public Tools::Factory<IInstance>, public std::enable_shared_from_this<IInstance>
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

		virtual const crtmath::dmat4x3& GetMatrix() const = 0;
		virtual void SetMatrix(const crtmath::dmat4x3& mat) = 0;

		virtual const std::string& GetColorShift() const = 0;
		virtual void SetColorShift(const std::string& color) = 0;

		virtual bool IsMarkedForUpdate(const EUpdateTarget& target) const = 0;
		virtual void MarkForUpdate(const EUpdateTarget& target, bool flag = true) = 0;
	};

	class Instance : public Tools::ExtensionSupport, public IInstance
	{
	public:
		const std::string& GetId() const override;
		void SetId(const std::string& id) override;

		const std::shared_ptr<IInstancesGroup>& GetGroup() const override;
		void SetGroup(const std::shared_ptr<IInstancesGroup>& group) override;

		const std::string& GetName() const override;
		void SetName(const std::string& name) override;

		const std::string& GetObjectRef() const override;
		void SetObjectRef(const std::string& objectRef) override;

		const crtmath::dmat4x3& GetMatrix() const override;
		void SetMatrix(const crtmath::dmat4x3& mat) override;

		const std::string& GetColorShift() const override;
		void SetColorShift(const std::string& color) override;

		bool IsMarkedForUpdate(const EUpdateTarget& target) const override;
		void MarkForUpdate(const EUpdateTarget& target, bool flag = true) override;

		Instance();
		virtual ~Instance();

	protected:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl();
	};

	typedef std::vector<std::shared_ptr<IInstance>> SharedInstVect;
}