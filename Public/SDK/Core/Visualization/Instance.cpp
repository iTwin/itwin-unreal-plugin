/*--------------------------------------------------------------------------------------+
|
|     $Source: Instance.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "Instance.h"
#include "../Singleton/singleton.h"

namespace AdvViz::SDK
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
		std::optional<float3> colorShift_;
		dmat3x4 transform_;
		std::string animationid_;
		std::optional<RefID> animPathId_;

		RefID refId_; // identifies instances created at runtime when they are not yet saved on the server
		bool shouldSave_ = false;
	};

	const std::string& Instance::GetId() const { return impl_->id_; }
	void Instance::SetId(const std::string& id) { impl_->id_ = id; }

	const RefID& Instance::GetRefId() const { return impl_->refId_; }
	void Instance::SetRefId(const RefID& id) { impl_->refId_ = id; }

	const std::shared_ptr<IInstancesGroup>& Instance::GetGroup() const { return impl_->group_; }
	void Instance::SetGroup(const std::shared_ptr<IInstancesGroup>& group) { impl_->group_ = group; }

	const std::string& Instance::GetAnimId() const { return impl_->animationid_; }
	void Instance::SetAnimId(const std::string &id) { impl_->animationid_ = id; }

	const std::optional<RefID>& Instance::GetAnimPathId() const { return impl_->animPathId_; }
	void Instance::SetAnimPathId(const RefID& id) { impl_->animPathId_ = id; }
	void Instance::RemoveAnimPathId() { impl_->animPathId_.reset(); }
	
	const std::string& Instance::GetName() const { return impl_->name_; }
	void Instance::SetName(const std::string& name) { impl_->name_ = name; }

	const std::string& Instance::GetObjectRef() const { return impl_->objectRef_; }
	void Instance::SetObjectRef(const std::string& objectRef) { impl_->objectRef_ = objectRef; }

	std::optional<float3> Instance::GetColorShift() const { return impl_->colorShift_; }
	void Instance::SetColorShift(const float3& color) { impl_->colorShift_ = color; }

	const dmat3x4& Instance::GetTransform() const { return impl_->transform_; }
	void Instance::SetTransform(const dmat3x4& mat) { impl_->transform_ = mat; }

	bool Instance::ShouldSave() const { return impl_->shouldSave_; }
	void Instance::SetShouldSave(bool value) { impl_->shouldSave_ = value; }

	expected<void, std::string> Instance::Update()
	{
		return expected<void, std::string>();
	}

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