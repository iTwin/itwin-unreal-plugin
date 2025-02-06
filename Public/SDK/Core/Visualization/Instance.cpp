/*--------------------------------------------------------------------------------------+
|
|     $Source: Instance.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "Instance.h"
#include "../Singleton/singleton.h"

namespace SDK::Core
{
	class Instance::Impl
	{
	public:
		// ids defined by the server
		std::string id_;
		std::shared_ptr<IInstancesGroup> group_;

		// other data
		std::string name_;
		std::string objectRef_;
		std::string colorShift_;
		dmat3x4 matrix_;

		// update flags
		bool updateFlags_[2];
		bool deletionFlag_;

		const std::string& GetId() const { return id_; }
		void SetId(const std::string& id) { id_ = id; }

		const std::shared_ptr<IInstancesGroup>& GetGroup() const { return group_; }
		void SetGroup(const std::shared_ptr<IInstancesGroup>& group) { group_ = group; }

		const std::string& GetName() const { return name_; }
		void SetName(const std::string& name) { name_ = name; }

		const std::string& GetObjectRef() const { return objectRef_; }
		void SetObjectRef(const std::string& objectRef) { objectRef_ = objectRef; }

		const std::string& GetColorShift() const { return colorShift_; }
		void SetColorShift(const std::string& color) { colorShift_ = color; }

		const dmat3x4& GetMatrix() const { return matrix_; }
		void SetMatrix(const dmat3x4& mat) { matrix_ = mat; }

		bool IsMarkedForUpdate(const EUpdateTarget& target) const { return updateFlags_[target]; }
		void MarkForUpdate(const EUpdateTarget& target, bool flag) { updateFlags_[target] = flag; }

		Impl() { updateFlags_[Database] = updateFlags_[Display] = false; deletionFlag_ = false; }
	};

	const std::string& Instance::GetId() const { return impl_->GetId(); }
	void Instance::SetId(const std::string& id) { impl_->SetId(id); }

	const std::shared_ptr<IInstancesGroup>& Instance::GetGroup() const { return impl_->GetGroup(); }
	void Instance::SetGroup(const std::shared_ptr<IInstancesGroup>& group) { impl_->SetGroup(group); }

	const std::string& Instance::GetName() const { return impl_->GetName(); }
	void Instance::SetName(const std::string& name) { impl_->SetName(name); }

	const std::string& Instance::GetObjectRef() const { return impl_->GetObjectRef(); }
	void Instance::SetObjectRef(const std::string& objectRef) { impl_->SetObjectRef(objectRef); }

	const std::string& Instance::GetColorShift() const { return impl_->GetColorShift(); }
	void Instance::SetColorShift(const std::string& color) { impl_->SetColorShift(color); }

	const dmat3x4& Instance::GetMatrix() const { return impl_->GetMatrix(); }
	void Instance::SetMatrix(const dmat3x4& mat) { impl_->SetMatrix(mat); }

	bool Instance::IsMarkedForUpdate(const EUpdateTarget& target) const { return impl_->IsMarkedForUpdate(target); }
	void Instance::MarkForUpdate(const EUpdateTarget& target, bool flag /*= true*/) { impl_->MarkForUpdate(target, flag); }

	Instance::Instance():impl_(new Impl())
	{}

	Instance::~Instance() 
	{}

	Instance::Impl& Instance::GetImpl()
	{
		return *impl_;
	}

	template<>
	Tools::Factory<IInstance>::Globals::Globals()
	{
		newFct_ = []() {
			IInstance* p(static_cast<IInstance*>(new Instance()));
			return p;
		};
	}

	template<>
	Tools::Factory<IInstance>::Globals& Tools::Factory<IInstance>::GetGlobals()
	{
		return singleton<Tools::Factory<IInstance>::Globals>();
	}

}