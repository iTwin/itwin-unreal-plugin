/*--------------------------------------------------------------------------------------+
|
|     $Source: Spline.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "Spline.h"
#include "../Singleton/singleton.h"

namespace AdvViz::SDK
{
	// ------------------------------------------------------------------------
	//                              SplinePoint

	class SplinePoint::Impl
	{
	public:
		std::string id_; // id defined by the server
		double3 position_;
		double3 upVector_;
		double3 inTangent_;
		double3 outTangent_;
		ESplineTangentMode inTangentMode_ = ESplineTangentMode::Linear;
		ESplineTangentMode outTangentMode_ = ESplineTangentMode::Linear;
		bool shouldSave_ = false;

		const std::string& GetId() const { return id_; };
		void SetId(const std::string& id) { id_ = id; };

		const double3& GetPosition() const { return position_; }
		void SetPosition(const double3& position) { position_ = position; }

		const double3& GetUpVector() const { return upVector_; }
		void SetUpVector(const double3& upVector) { upVector_ = upVector; }

		const double3& GetInTangent() const { return inTangent_; }
		void SetInTangent(const double3& tangent) { inTangent_ = tangent; }

		const double3& GetOutTangent() const { return outTangent_; }
		void SetOutTangent(const double3& tangent) { outTangent_ = tangent; }

		bool ShouldSave() const { return shouldSave_; }
		void SetShouldSave(bool value) { shouldSave_ = value; }
	};

	const std::string& SplinePoint::GetId() const { return impl_->GetId(); }
	void SplinePoint::SetId(const std::string& id) { impl_->SetId(id); };

	const double3& SplinePoint::GetPosition() const { return impl_->GetPosition(); }
	void SplinePoint::SetPosition(const double3& position) { impl_->SetPosition(position); }

	const double3& SplinePoint::GetUpVector() const { return impl_->GetUpVector(); }
	void SplinePoint::SetUpVector(const double3& upVector) { impl_->SetUpVector(upVector); }

	ESplineTangentMode SplinePoint::GetInTangentMode() const
	{
		return impl_->inTangentMode_;
	}
	void SplinePoint::SetInTangentMode(const ESplineTangentMode mode)
	{
		impl_->inTangentMode_ = mode;
	}

	const double3& SplinePoint::GetInTangent() const { return impl_->GetInTangent(); }
	void SplinePoint::SetInTangent(const double3& tangent) { impl_->SetInTangent(tangent); }

	ESplineTangentMode SplinePoint::GetOutTangentMode() const
	{
		return impl_->outTangentMode_;
	}
	void SplinePoint::SetOutTangentMode(const ESplineTangentMode mode)
	{
		impl_->outTangentMode_ = mode;
	}

	const double3& SplinePoint::GetOutTangent() const { return impl_->GetOutTangent(); }
	void SplinePoint::SetOutTangent(const double3& tangent) { impl_->SetOutTangent(tangent); }

	bool SplinePoint::ShouldSave() const { return impl_->ShouldSave(); }
	void SplinePoint::SetShouldSave(bool value) { impl_->SetShouldSave(value); }

	SplinePoint::SplinePoint():impl_(new Impl())
	{}

	SplinePoint::~SplinePoint() 
	{}

	SplinePoint::Impl& SplinePoint::GetImpl()
	{
		return *impl_;
	}

	template<>
	Tools::Factory<ISplinePoint>::Globals::Globals()
	{
		newFct_ = []() {
			ISplinePoint* p(static_cast<ISplinePoint*>(new SplinePoint()));
			return p;
		};
	}

	template<>
	Tools::Factory<ISplinePoint>::Globals& Tools::Factory<ISplinePoint>::GetGlobals()
	{
		return singleton<Tools::Factory<ISplinePoint>::Globals>();
	}

	// ------------------------------------------------------------------------
	//                                 Spline

	class Spline::Impl
	{
	private:
		RefID id_; // identifies the spline (and may hold id defined by the server)
		std::string name_;
		ESplineUsage usage_ = ESplineUsage::Undefined;
		bool closedLoop_ = false;
		std::string linkedModelType_; // only set if the spline is linked to a model
		std::string linkedModelId_; // only set if the spline is linked to a model
		dmat3x4 transform_;
		SharedSplinePointVect points_;
		SharedSplinePointVect removedPoints_; // used by the spline manager for the saving.

		bool shouldSave_ = false;

	public:
		const RefID& GetId() const { return id_; }
		void SetId(const RefID& id) { id_ = id; }

		const std::string& GetName() const { return name_; }
		void SetName(const std::string& name) { name_ = name; }

		const dmat3x4& GetTransform() const { return transform_; }
		void SetTransform(const dmat3x4& mat) { transform_ = mat; }

		SharedSplinePoint GetPoint(const size_t index) const
		{
			return index < points_.size() ? points_[index] : SharedSplinePoint();
		}

		void SetPoint(const size_t index, SharedSplinePoint point)
		{
			if (index < points_.size())
			{
				points_[index] = point;
			}
		}

		SharedSplinePoint InsertPoint(const size_t index)
		{
			SharedSplinePoint splinePoint;
			if (index <= points_.size())
			{
				splinePoint.reset(ISplinePoint::New());
				points_.insert(points_.cbegin() + index, splinePoint);
				splinePoint->SetShouldSave(true);
			}
			return splinePoint;
		}

		SharedSplinePoint AddPoint()
		{
			return InsertPoint(points_.size());
		}

		void RemovePoint(const size_t index)
		{
			if (index < points_.size())
			{
				SharedSplinePointVect::const_iterator itPoint = points_.cbegin() + index;
				removedPoints_.push_back(*itPoint);
				points_.erase(itPoint);
			}
		}

		size_t GetNumberOfPoints() const { return points_.size(); }

		void SetNumberOfPoints(size_t newNbPoints)
		{
			size_t const oldNumOfPts = GetNumberOfPoints();
			if (newNbPoints > oldNumOfPts)
			{
				points_.reserve(newNbPoints);
				size_t const nbToAdd = newNbPoints - oldNumOfPts;
				for (size_t i(0); i < nbToAdd; ++i)
				{
					AddPoint();
				}
			}
			else if (newNbPoints < oldNumOfPts)
			{
				size_t const nbToRemove = oldNumOfPts - newNbPoints;
				{
					auto itRevPoint = points_.rbegin();
					removedPoints_.insert(removedPoints_.end(), itRevPoint, itRevPoint + nbToRemove);
				}
				points_.resize(newNbPoints);
			}
			BE_ASSERT(newNbPoints == GetNumberOfPoints());
		}

		bool ShouldSave() const
		{ 
			if (shouldSave_ || !removedPoints_.empty())
				return true;

			for (auto const& point : points_)
			{
				if (point->ShouldSave())
					return true;
			}

			return false;
		}
		void SetShouldSave(bool value) { shouldSave_ = value; }

		const SharedSplinePointVect& GetPoints() const { return points_; }
		const SharedSplinePointVect& GetRemovedPoints() const { return removedPoints_; }
		void ClearPoints() { points_.clear(); }
		void ClearRemovedPoints() { removedPoints_.clear(); }

		ESplineUsage GetUsage() const { return usage_; }
		void SetUsage(const ESplineUsage usage)
		{
			if (usage_ != usage)
			{
				usage_ = usage;
				// Some usages require a closed spline
				if (usage == ESplineUsage::MapCutout
					|| usage == ESplineUsage::PopulationZone)
				{
					SetClosedLoop(true);
				}
				shouldSave_ = true;
			}
		}

		bool IsClosedLoop() const
		{
			BE_ASSERT(closedLoop_
				|| !(usage_ == ESplineUsage::MapCutout || usage_ == ESplineUsage::PopulationZone));
			return closedLoop_;
		}
		void SetClosedLoop(bool bInClosedLoop)
		{
			if (bInClosedLoop != closedLoop_)
			{
				closedLoop_ = bInClosedLoop;
				shouldSave_ = true;
			}
		}

		const std::string& GetLinkedModelType() const { return linkedModelType_; }
		void SetLinkedModelType(const std::string& type) { linkedModelType_ = type; }

		const std::string& GetLinkedModelId() const { return linkedModelId_; }
		void SetLinkedModelId(const std::string& id) { linkedModelId_ = id; }
	};

	const RefID& Spline::GetId() const { return impl_->GetId(); }
	void Spline::SetId(const RefID& id) { impl_->SetId(id); }

	const std::string& Spline::GetName() const { return impl_->GetName(); }
	void Spline::SetName(const std::string& name) { impl_->SetName(name); }

	ESplineUsage Spline::GetUsage() const { return impl_->GetUsage(); }
	void Spline::SetUsage(const ESplineUsage usage) { impl_->SetUsage(usage); }

	bool Spline::IsClosedLoop() const { return impl_->IsClosedLoop(); }
	void Spline::SetClosedLoop(bool bClosedLoop) { impl_->SetClosedLoop(bClosedLoop); }

	const std::string& Spline::GetLinkedModelType() const { return impl_->GetLinkedModelType(); }
	void Spline::SetLinkedModelType(const std::string& type) { impl_->SetLinkedModelType(type); }

	const std::string& Spline::GetLinkedModelId() const { return impl_->GetLinkedModelId(); }
	void Spline::SetLinkedModelId(const std::string& id) { impl_->SetLinkedModelId(id); }

	const dmat3x4& Spline::GetTransform() const { return impl_->GetTransform(); }
	void Spline::SetTransform(const dmat3x4& mat) { impl_->SetTransform(mat); }

	SharedSplinePoint Spline::GetPoint(const size_t index) const { return impl_->GetPoint(index); }
	void Spline::SetPoint(const size_t index, SharedSplinePoint point) { impl_->SetPoint(index,  point); }
	SharedSplinePoint Spline::InsertPoint(const size_t index) { return impl_->InsertPoint(index); }
	SharedSplinePoint Spline::AddPoint() { return impl_->AddPoint(); }
	void Spline::RemovePoint(const size_t index) { impl_->RemovePoint(index); }

	size_t Spline::GetNumberOfPoints() const { return impl_->GetNumberOfPoints(); }
	void Spline::SetNumberOfPoints(size_t nbPoints) { impl_->SetNumberOfPoints(nbPoints); }

	bool Spline::ShouldSave() const { return impl_->ShouldSave(); }
	void Spline::SetShouldSave(bool value) { impl_->SetShouldSave(value); }

	const SharedSplinePointVect& Spline::GetPoints() const { return impl_->GetPoints(); }
	const SharedSplinePointVect& Spline::GetRemovedPoints() const { return impl_->GetRemovedPoints(); }
	void Spline::ClearPoints() { impl_->ClearPoints(); }
	void Spline::ClearRemovedPoints() { impl_->ClearRemovedPoints(); }

	Spline::Spline():impl_(new Impl())
	{}

	Spline::~Spline() 
	{}

	Spline::Impl& Spline::GetImpl()
	{
		return *impl_;
	}

	template<>
	Tools::Factory<ISpline>::Globals::Globals()
	{
		newFct_ = []() {
			ISpline* p(static_cast<ISpline*>(new Spline()));
			return p;
		};
	}

	template<>
	Tools::Factory<ISpline>::Globals& Tools::Factory<ISpline>::GetGlobals()
	{
		return singleton<Tools::Factory<ISpline>::Globals>();
	}

}
