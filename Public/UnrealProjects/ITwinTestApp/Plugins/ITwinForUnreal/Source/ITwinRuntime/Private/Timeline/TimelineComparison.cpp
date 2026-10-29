/*--------------------------------------------------------------------------------------+
|
|     $Source: TimelineComparison.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "Timeline/Timeline.h"

namespace ITwin::Timeline {

    namespace {

        // ---------------------------------------------------------------------------
        // Scalar / UE math near-equality helpers
        // ---------------------------------------------------------------------------

        bool NearlyEqual(double A, double B, float Tol)
        {
            return FMath::Abs(A - B) <= static_cast<double>(Tol);
        }

        bool NearlyEqual(float A, float B, float Tol)
        {
            return FMath::Abs(A - B) <= Tol;
        }

        bool NearlyEqual(FVector const& A, FVector const& B, float Tol)
        {
            return A.Equals(B, Tol);
        }

        bool NearlyEqual(FVector3f const& A, FVector3f const& B, float Tol)
        {
            // FVector3f has no Equals(); use component-wise max deviation
            return (A - B).GetAbsMax() <= Tol;
        }

        bool NearlyEqual(FQuat const& A, FQuat const& B, float Tol)
        {
            // FQuat::Equals accounts for q == -q (same rotation, opposite hemisphere)
            return A.Equals(B, Tol);
        }

        // ---------------------------------------------------------------------------
        // Per-property near-equality helpers
        // ---------------------------------------------------------------------------

        bool NearlyEqual(PVisibility const& A, PVisibility const& B, float Tol)
        {
            return NearlyEqual(A.Value, B.Value, Tol);
        }

        bool NearlyEqual(PColor const& A, PColor const& B, float Tol)
        {
            if (A.bHasColor != B.bHasColor)
                return false;
            return !A.bHasColor || NearlyEqual(A.Value, B.Value, Tol);
        }

        bool NearlyEqual(FDeferredAnchor const& A, FDeferredAnchor const& B, float Tol)
        {
            if (!A.bDeferred && !B.bDeferred)
                return NearlyEqual(A.Offset, B.Offset, Tol);
            else
                return A.AnchorPoint == B.AnchorPoint;
        }

        bool NearlyEqual(PTransform const& A, PTransform const& B, float Tol)
        {
            if (A.bIsTransformed != B.bIsTransformed)
                return false;
            if (!A.bIsTransformed)
                return true;
            return NearlyEqual(A.Position, B.Position, Tol)
                && NearlyEqual(A.Rotation, B.Rotation, Tol)
                && NearlyEqual(A.DefrdAnchor, B.DefrdAnchor, Tol);
        }

        bool NearlyEqual(FDeferredPlaneEquation const& A, FDeferredPlaneEquation const& B, float Tol)
        {
            return A.GrowthStatus == B.GrowthStatus
                && NearlyEqual(A.PlaneOrientation, B.PlaneOrientation, Tol)
                && NearlyEqual(A.PlaneW, B.PlaneW, Tol);
        }

        bool NearlyEqual(PClippingPlane const& A, PClippingPlane const& B, float Tol)
        {
            return NearlyEqual(A.DefrdPlaneEq, B.DefrdPlaneEq, Tol);
        }

        /// PropertyTimeline comparison (works for any property type above)
        template <typename TPropertyValues>
        bool AreNearlyEqualPropertyTimelines(
            PropertyTimeline<TPropertyValues> const& A,
            PropertyTimeline<TPropertyValues> const& B,
            float Tol)
        {
            if (A.Values.size() != B.Values.size())
                return false;

            auto ItA = A.Values.cbegin();
            auto ItB = B.Values.cbegin();
            for (; ItA != A.Values.cend(); ++ItA, ++ItB)
            {
                if (!NearlyEqual(ItA->Time, ItB->Time, 1e-3f/*1ms, Tol not suited to the range*/))
                    return false;
                if (ItA->Interpolation != ItB->Interpolation)
                    return false;
                if (!NearlyEqual(
                    static_cast<TPropertyValues const&>(*ItA),
                    static_cast<TPropertyValues const&>(*ItB),
                    Tol))
                    return false;
            }
            return true;
        }

        bool AreNearlyEqual(ElementTimelineEx const& A, ElementTimelineEx const& B, float Tol)
        {
            if (A.GetIModelElementsKey().Key != B.GetIModelElementsKey().Key)
                return false;

            if (!AreNearlyEqualPropertyTimelines(A.Visibility, B.Visibility, Tol))
                return A.Visibility.HasNoEffect() && B.Visibility.HasNoEffect();
            if (!AreNearlyEqualPropertyTimelines(A.Color, B.Color, Tol))
                return A.Color.HasNoEffect() && B.Color.HasNoEffect();
            if (!AreNearlyEqualPropertyTimelines(A.Transform, B.Transform, Tol))
                return A.Transform.HasNoEffect() && B.Transform.HasNoEffect();
            if (!AreNearlyEqualPropertyTimelines(A.ClippingPlane, B.ClippingPlane, Tol))
                return A.ClippingPlane.HasNoEffect() && B.ClippingPlane.HasNoEffect();

            return true;
        }

    } // anonymous namespace

    bool AreNearlyEqual(MainTimeline const& A, MainTimeline const& B, float Tolerance)
    {
        auto const& ContainerA = A.GetContainer();
        auto const& ContainerB = B.GetContainer();

        if (ContainerA.size() != ContainerB.size())
            return false;

        for (size_t i = 0; i < ContainerA.size(); ++i)
        {
            auto* MatchInB = B.GetElementTimelineFor(ContainerA[i]->GetIModelElementsKey());
            if (!MatchInB || !AreNearlyEqual(*ContainerA[i], *MatchInB, Tolerance))
                return false;
        }
        return true;
    }

} // namespace ITwin::Timeline