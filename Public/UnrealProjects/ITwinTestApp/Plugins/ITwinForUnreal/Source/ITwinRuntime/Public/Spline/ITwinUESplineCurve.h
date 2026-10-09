/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinUESplineCurve.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Templates/PimplPtr.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <BeUtils/SplineSampling/SplineSampling.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

class USplineComponent;

/// Thread-safe snapshot of an Unreal Engine Spline Component, exposing the SplineCurve interface expected
/// by BeUtils::SampleSpline.
/// The snapshot must be built on the game thread, but can then be evaluated from any thread as it holds
/// its own copy of the spline curves and component transform (no UObject access).
/// It mimics the behavior of FITwinUESplineCurve (world coordinates, non-constant velocity time mapping).
/// Note: deliberately not exported (ITWINRUNTIME_API), as dllexport would force the instantiation of the
/// BeUtils::path::GenericCurve<> base members, which are declared 'extern template' (C4661).
class FITwinSplineSnapshotCurve final : public BeUtils::SplineCurve
{
public:
	explicit FITwinSplineSnapshotCurve(USplineComponent const& InSpline);
	virtual ~FITwinSplineSnapshotCurve();
	virtual glm::dvec3 GetPositionAtCoord(value_type const& u) const override;
	virtual glm::dvec3 GetTangentAtCoord(value_type const& u) const override;
	virtual size_t PointCount(const bool /*accountForCyclicity*/) const override;
	virtual glm::dvec3 GetPositionAtIndex(size_t idx) const override;
	virtual bool IsCyclic() const override;

private:
	// Implementation hidden to avoid including Components/SplineComponent.h (FSplineCurves) here.
	class FImpl;
	TPimplPtr<FImpl> Impl;
};

/// Adapts an Unreal Engine Spline Component to the SplineCurve interface expected by BeUtils::SampleSpline.
class FITwinUESplineCurve final : public BeUtils::SplineCurve
{
public:
	FITwinUESplineCurve(USplineComponent const& InSpline);
	virtual glm::dvec3 GetPositionAtCoord(value_type const& u) const override;
	virtual glm::dvec3 GetTangentAtCoord(value_type const& u) const override;
	virtual size_t PointCount(const bool /*accountForCyclicity*/) const override;
	virtual glm::dvec3 GetPositionAtIndex(size_t idx) const override;
	virtual bool IsCyclic() const override;

private:
	USplineComponent const& UESpline;
};

/// Adapter for a spline chunk (segment of a spline between two consecutive control points) to the
/// SplineCurve interface expected by BeUtils::SampleSpline.
class FITwinUESplineChunkCurve final : public BeUtils::SplineCurve
{
public:
	FITwinUESplineChunkCurve(USplineComponent const& InSpline, int32 InChunkIndex);
	virtual glm::dvec3 GetPositionAtCoord(value_type const& u) const override;
	virtual glm::dvec3 GetTangentAtCoord(value_type const& u) const override;
	virtual size_t PointCount(const bool /*accountForCyclicity*/) const override;
	virtual glm::dvec3 GetPositionAtIndex(size_t idx) const override;
	virtual bool IsCyclic() const override;

private:
	USplineComponent const& UESpline;
	float const StartInputKey;
};

