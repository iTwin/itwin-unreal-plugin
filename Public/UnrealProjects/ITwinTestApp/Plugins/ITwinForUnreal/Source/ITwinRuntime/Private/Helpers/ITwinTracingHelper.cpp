/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTracingHelper.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Helpers/ITwinTracingHelper.h>

#include <ITwinIModel.h>
#include <ITwinIModelInternals.h>
#include <ITwinMetadataConstants.h>

#include <CesiumMetadataPickingBlueprintLibrary.h>
#include <CesiumMetadataValue.h>

#include <Camera/CameraTypes.h>
#include <Engine/GameViewportClient.h>
#include <Engine/LocalPlayer.h>
#include <Engine/World.h>
#include <GameFramework/PlayerController.h>


struct FITwinTracingHelper::FImpl
{
	TArray<FHitResult> AllHits;
	TArray<FHitResult> BackHits;
	FComponentQueryParams QueryParams;

	FImpl() {
		QueryParams.bReturnFaceIndex = true;
	}
	bool LineTraceMulti(UWorld const* World, FVector const& TraceStart, FVector const& TraceEnd);
};


bool FITwinTracingHelper::FImpl::LineTraceMulti(UWorld const* World,
	FVector const& TraceStart, FVector const& TraceEnd)
{
	AllHits.Reset();

	bool bHasFrontHits = World->LineTraceMultiByObjectType(AllHits, TraceStart, TraceEnd,
			FCollisionObjectQueryParams::AllObjects, QueryParams);

	// our materials are double-sided, and LineTraceMultiByObjectType misses back hits, so let's launch
	// a second "ray" to get potential back hits (I did not find any option to do it in one call)
	BackHits.Reset();
	bool bHasBackHits = World->LineTraceMultiByObjectType(BackHits, TraceEnd, TraceStart,
		FCollisionObjectQueryParams::AllObjects, QueryParams);

	// merge both arrays
	if (bHasBackHits)
	{
		if (bHasFrontHits)
		{
			float const TraceExtent = (TraceEnd - TraceStart).Length();
			for (FHitResult& BackHit : BackHits)
			{
				float const DistFromStart = TraceExtent - BackHit.Distance;
				BackHit.Distance = DistFromStart;
				// Invert back impact normal
				BackHit.ImpactNormal = -BackHit.ImpactNormal;
				BackHit.Normal = -BackHit.Normal;
				auto Index = AllHits.FindLastByPredicate(
					[DistFromStart](FHitResult const& HitRes)
				{
					return HitRes.Distance > DistFromStart;
				});
				if (Index == INDEX_NONE)
				{
					AllHits.Push(BackHit);
				}
				else
				{
					AllHits.Insert(BackHit, Index);
				}
			}
		}
		else
		{
			AllHits = BackHits;
		}
	}
	return bHasBackHits || bHasFrontHits;
}


FITwinTracingHelper::FITwinTracingHelper()
	: Impl(MakePimpl<FImpl>())
{

}

void FITwinTracingHelper::AddIgnoredActors(const TArray<AActor*>& ActorsToIgnore)
{
	Impl->QueryParams.AddIgnoredActors(ActorsToIgnore);
}

ITwinElementID FITwinTracingHelper::VisitElementsUnderCursor(UWorld const* World, FVector2D& MousePosition,
	std::function<void(FHitResult const&, std::unordered_set<ITwinElementID>&)>&& HitResultHandler,
	std::optional<uint32> const& MaxUniqueElementsHit /*= std::nullopt*/,
	std::optional<float> const& CustomTraceExtentInMeters /*= std::nullopt*/,
	std::optional<FVector2D> const& CustomMousePosition /*= std::nullopt*/)
{
	if (!World)
		return ITwin::NOT_ELEMENT;
	APlayerController* PlayerController = World->GetFirstPlayerController();
	if (!PlayerController)
		return ITwin::NOT_ELEMENT;
	ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	if (!LocalPlayer || !LocalPlayer->ViewportClient)
		return ITwin::NOT_ELEMENT;
	if (CustomMousePosition)
		MousePosition = *CustomMousePosition;
	else if (!LocalPlayer->ViewportClient->GetMousePosition(MousePosition))
		return ITwin::NOT_ELEMENT;
	FVector WorldLoc, WorldDir;
	if (!PlayerController->DeprojectScreenPositionToWorld(MousePosition.X, MousePosition.Y,
		WorldLoc, WorldDir))
		return ITwin::NOT_ELEMENT;

	FVector::FReal const TraceExtentInMeters = static_cast<FVector::FReal>(
		CustomTraceExtentInMeters.value_or(1e6f)); // 1.000 km by default
	FVector::FReal const TraceExtent = TraceExtentInMeters * 100;
	FVector const TraceStart = WorldLoc;
	FVector const TraceEnd = WorldLoc + WorldDir * TraceExtent;

	bool const bHasHits = Impl->LineTraceMulti(World, TraceStart, TraceEnd);

	ITwinElementID FirstEltID = ITwin::NOT_ELEMENT;
	if (bHasHits)
	{
		std::unordered_set<ITwinElementID> DejaVu;
		for (auto&& HitResult : Impl->AllHits)
		{
			auto HitActor = HitResult.GetActor();
			if (!HitActor || HitActor->IsHidden())
				continue;
			HitResultHandler(HitResult, DejaVu);

			if (FirstEltID == ITwin::NOT_ELEMENT && DejaVu.size() == 1)
			{
				FirstEltID = *DejaVu.cbegin();
			}
			if (!MaxUniqueElementsHit || (uint32)DejaVu.size() >= (*MaxUniqueElementsHit))
			{
				break; // avoid overflowing the logs, stop now
			}
		}
	}
	return FirstEltID;
}


bool FITwinTracingHelper::PickVisibleElement(FHitResult const& HitResult, AITwinIModel& IModel,
	ITwinElementID& OutEltID, bool bSelectElement)
{
	ITwinElementID EltID = ITwin::NOT_ELEMENT;

	TMap<FString, FCesiumMetadataValue> const Table =
		UCesiumMetadataPickingBlueprintLibrary::GetPropertyTableValuesFromHit(
			HitResult, ITwinCesium::Metada::ELEMENT_FEATURE_ID_SLOT);
	FCesiumMetadataValue const* const ElemIdFound = Table.Find(ITwinCesium::Metada::ELEMENT_NAME);
	if (ElemIdFound != nullptr)
	{
		EltID = ITwinElementID(FCesiumMetadataValueAccess::GetUnsignedInteger64(
			*ElemIdFound, ITwin::NOT_ELEMENT.value()));
	}
	OutEltID = EltID;
	if (EltID != ITwin::NOT_ELEMENT)
	{
		FITwinIModelInternals& IModelInternals = GetInternals(IModel);
		if (IModelInternals.HasElementWithID(EltID)
			&& IModelInternals.OnClickedElement(EltID, HitResult, bSelectElement))
		{
			return true;
		}
	}
	return false;
}

bool FITwinTracingHelper::FindNearestImpact(FHitResult& OutHitResult, UWorld const* World,
	FVector const& TraceStart, FVector const& TraceEnd)
{
	if (!Impl->LineTraceMulti(World, TraceStart, TraceEnd))
	{
		return false;
	}

	// Filter hidden impacts
	for (auto&& HitResult : Impl->AllHits)
	{
		if (!HitResult.HasValidHitObjectHandle())
			continue;
		auto HitActor = HitResult.GetActor();
		if (!HitActor || HitActor->IsHidden())
			continue;
		AITwinIModel* HitIModel = Cast<AITwinIModel>(HitActor->GetOwner());
		if (HitIModel)
		{
			// Test if the picked element (if any) is visible
			ITwinElementID EltID = ITwin::NOT_ELEMENT;
			if (!PickVisibleElement(HitResult, *HitIModel, EltID, false /*bSelectElement*/))
			{
				continue;
			}
		}
		OutHitResult = HitResult;
		return true;
	}
	return false;
}
