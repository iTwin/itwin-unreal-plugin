/*--------------------------------------------------------------------------------------+
|
|     $Source: Spline.h $
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
#include <Core/Visualization/RefID.h>

MODULE_EXPORT namespace AdvViz::SDK
{
	enum class ESplineUsage : uint8_t
	{
		Undefined = 0,
		MapCutout = 1,
		TrafficPath = 2,
		PopulationZone = 3,
		PopulationPath = 4,
		AnimPath = 5
	};

	enum class ESplineTangentMode : uint8_t
	{
		Linear = 0,
		Smooth = 1,
		Custom = 2
	};

	// ------------------------------------------------------------------------
	//                              ISplinePoint

	class ISplinePoint : public Tools::Factory<ISplinePoint>, public Tools::ExtensionSupport
	{
	public:
		virtual const std::string& GetId() const = 0;
		virtual void SetId(const std::string& id) = 0;

		virtual const double3& GetPosition() const = 0;
		virtual void SetPosition(const double3& position) = 0;

		virtual const double3& GetUpVector() const = 0;
		virtual void SetUpVector(const double3& upVector) = 0;
		
		virtual ESplineTangentMode GetInTangentMode() const = 0;
		virtual void SetInTangentMode(ESplineTangentMode mode) = 0;
		
		virtual const double3& GetInTangent() const = 0;
		virtual void SetInTangent(const double3& tangent) = 0;
		
		virtual ESplineTangentMode GetOutTangentMode() const = 0;
		virtual void SetOutTangentMode(ESplineTangentMode mode) = 0;
		
		virtual const double3& GetOutTangent() const = 0;
		virtual void SetOutTangent(const double3& tangent) = 0;
		
		virtual bool ShouldSave() const = 0;
		virtual void SetShouldSave(bool value) = 0;

		virtual std::shared_ptr<ISplinePoint> Clone() const = 0;
	};

	// ------------------------------------------------------------------------
	//                              SplinePoint

	class SplinePoint : public ISplinePoint, public Tools::TypeId<SplinePoint>
	{
	public:
		const std::string& GetId() const override;
		void SetId(const std::string& id) override;

		const double3& GetPosition() const override;
		void SetPosition(const double3& position) override;
		
		const double3& GetUpVector() const override;
		void SetUpVector(const double3& upVector) override;
		
		ESplineTangentMode GetInTangentMode() const override;
		void SetInTangentMode(const ESplineTangentMode mode) override;
		
		const double3& GetInTangent() const override;
		void SetInTangent(const double3& tangent) override;
		
		ESplineTangentMode GetOutTangentMode() const override;
		void SetOutTangentMode(const ESplineTangentMode mode) override;
		
		const double3& GetOutTangent() const override;
		void SetOutTangent(const double3& tangent) override;
		
		bool ShouldSave() const override;
		void SetShouldSave(bool value) override;

		SplinePoint();
		virtual ~SplinePoint();

		std::shared_ptr<ISplinePoint> Clone() const override;

		using Tools::TypeId<SplinePoint>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || ISplinePoint::IsTypeOf(i); }

	protected:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl();
	};

	typedef std::shared_ptr<ISplinePoint> SharedSplinePoint;
	typedef std::vector<SharedSplinePoint> SharedSplinePointVect;

	// ------------------------------------------------------------------------
	//                                 ISpline

	struct SplineLinkedModel
	{
		std::string modelType;
		std::string modelId; // can be left empty for GlobalMapLayer (Google tileset)

		bool operator==(SplineLinkedModel const& other) const {
			return modelType == other.modelType && modelId == other.modelId;
		}
	};

	class ISpline : public Tools::Factory<ISpline>
	{
	public:
		virtual const RefID& GetId() const = 0;
		virtual void SetId(const RefID& id) = 0;

		virtual const std::string& GetName() const = 0;
		virtual void SetName(const std::string& name) = 0;

		virtual ESplineUsage GetUsage() const = 0;
		virtual void SetUsage(const ESplineUsage usage) = 0;

		virtual bool IsClosedLoop() const = 0;
		virtual void SetClosedLoop(bool bClosedLoop) = 0;

		virtual const std::vector<SplineLinkedModel>& GetLinkedModels() const = 0;
		virtual void SetLinkedModels(const std::vector<SplineLinkedModel>& models) = 0;

		virtual bool IsEnabledEffect() const = 0;
		virtual void EnableEffect(bool bEnable) = 0;

		virtual bool GetInvertEffect() const = 0;
		virtual void SetInvertEffect(bool bInvert) = 0;

		virtual const dmat3x4& GetTransform() const = 0;
		virtual void SetTransform(const dmat3x4& mat) = 0;

		virtual SharedSplinePoint GetPoint(const size_t index) const = 0;
		virtual void SetPoint(const size_t index, SharedSplinePoint point) = 0;
		virtual SharedSplinePoint InsertPoint(const size_t index) = 0;
		virtual SharedSplinePoint AddPoint() = 0;
		virtual void RemovePoint(const size_t index) = 0;

		virtual size_t GetNumberOfPoints() const = 0;
		virtual void SetNumberOfPoints(size_t nbPoints) = 0;

		virtual bool ShouldSave() const = 0;
		virtual void SetShouldSave(bool value) = 0;

		// These functions should only be used by the splines manager.
		virtual const SharedSplinePointVect& GetPoints() const = 0;
		virtual const SharedSplinePointVect& GetRemovedPoints() const = 0;
		virtual void ClearPoints() = 0;
		virtual void ClearRemovedPoints() = 0;

		// Make a full clone of this spline. The clone should be made totally independent from the source
		// (not sharing points, typically).
		virtual std::shared_ptr<ISpline> Clone() const = 0;
	};

	// ------------------------------------------------------------------------
	//                                 Spline

	class Spline : public ISpline, public Tools::TypeId<Spline>
	{
	public:
		const RefID& GetId() const override;
		void SetId(const RefID& id) override;

		const std::string& GetName() const override;
		void SetName(const std::string& name) override;

		ESplineUsage GetUsage() const override;
		void SetUsage(const ESplineUsage usage) override;

		bool IsClosedLoop() const override;
		void SetClosedLoop(bool bClosedLoop) override;

		const std::vector<SplineLinkedModel>& GetLinkedModels() const override;
		void SetLinkedModels(const std::vector<SplineLinkedModel>& models) override;

		bool IsEnabledEffect() const override;
		void EnableEffect(bool bEnable) override;

		bool GetInvertEffect() const override;
		void SetInvertEffect(bool bInvert) override;

		const dmat3x4& GetTransform() const override;
		void SetTransform(const dmat3x4& mat) override;

		SharedSplinePoint GetPoint(const size_t index) const override;
		void SetPoint(const size_t index, SharedSplinePoint point) override;
		SharedSplinePoint InsertPoint(const size_t index) override;
		SharedSplinePoint AddPoint() override;
		void RemovePoint(const size_t index) override;

		size_t GetNumberOfPoints() const override;
		void SetNumberOfPoints(size_t nbPoints) override;

		bool ShouldSave() const override;
		void SetShouldSave(bool value) override;

		// These functions should only be used by the splines manager.
		const SharedSplinePointVect& GetPoints() const override;
		const SharedSplinePointVect& GetRemovedPoints() const override;
		void ClearPoints() override;
		void ClearRemovedPoints() override;

		std::shared_ptr<ISpline> Clone() const override;

		using Tools::TypeId<Spline>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || ISpline::IsTypeOf(i); }

		Spline();
		virtual ~Spline();

	protected:
		class Impl;
		const std::unique_ptr<Impl> impl_;
		Impl& GetImpl();
	};

	typedef std::shared_ptr<ISpline> SharedSpline;
	typedef std::vector<SharedSpline> SharedSplineVect;
}