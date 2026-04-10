/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTracingHelper.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
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
	// Note: to get "back hits" as well, the static mesh component's UBodySetup::bDoubleSidedGeometry must be set to true
	return World->LineTraceMultiByObjectType(AllHits, TraceStart, TraceEnd,
			FCollisionObjectQueryParams::AllObjects, QueryParams);
}

FITwinTracingHelper::FITwinTracingHelper()
	: Impl(MakePimpl<FImpl>())
{
}

void FITwinTracingHelper::AddIgnoredActors(const TArray<AActor*>& ActorsToIgnore)
{
	Impl->QueryParams.AddIgnoredActors(ActorsToIgnore);
}

void FITwinTracingHelper::AddIgnoredActors(const TArray<const AActor*>& ActorsToIgnore)
{
	Impl->QueryParams.AddIgnoredActors(ActorsToIgnore);
}

void FITwinTracingHelper::AddIgnoredComponents(const TArray<UPrimitiveComponent*>& ComponentsToIgnore)
{
	Impl->QueryParams.AddIgnoredComponents(ComponentsToIgnore);
}

/*static*/
bool FITwinTracingHelper::GetRayFromMousePosition(UWorld const* World,
	FVector2D& MousePosition,
	FITwinRayTraceInput& OutTraceInput,
	std::optional<FVector2D> const& CustomMousePosition /*= std::nullopt*/)
{
	if (!World)
		return false;
	APlayerController* PlayerController = World->GetFirstPlayerController();
	if (!PlayerController)
		return false;
	ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	if (!LocalPlayer || !LocalPlayer->ViewportClient)
		return false;
	if (CustomMousePosition)
		MousePosition = *CustomMousePosition;
	else if (!LocalPlayer->ViewportClient->GetMousePosition(MousePosition))
		return false;
	FVector WorldLoc, WorldDir;
	if (!PlayerController->DeprojectScreenPositionToWorld(MousePosition.X, MousePosition.Y,
		WorldLoc, WorldDir))
		return false;
	OutTraceInput.TraceStart = WorldLoc;
	OutTraceInput.TraceDirection = WorldDir;
	return true;
}

/*static*/
int32 FITwinTracingHelper::GetRayTraceInputsFromScreenRatios(const UObject* WorldContextObject,
	TArray<FVector2d> const& InScreenRatios,
	TArray<FITwinRayTraceInput>& OutTraceInputs)
{
	if (InScreenRatios.IsEmpty())
	{
		ensureMsgf(false, TEXT("wrong input: missing ratio(s)"));
		return 0;
	}
	UWorld const* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!ensure(World))
		return 0;
	APlayerController const* PlayerController = World->GetFirstPlayerController();
	if (!PlayerController)
		return 0;

	int32 Width(0), Height(0);
	PlayerController->GetViewportSize(Width, Height);
	if (Width <= 0 || Height <= 0)
		return 0;

	OutTraceInputs.Reserve(InScreenRatios.Num());
	for (FVector2d const& ScreenRatio : InScreenRatios)
	{
		ensureMsgf(ScreenRatio.X >= 0 && ScreenRatio.X <= 1 && ScreenRatio.Y >= 0 && ScreenRatio.Y <= 1,
			TEXT("wrong input: ratio should be between 0 and 1"));
		FVector2D const ScreenPos(Width * ScreenRatio.X, Height * ScreenRatio.Y);
		FITwinRayTraceInput TraceInput;
		if (!UGameplayStatics::DeprojectScreenToWorld(PlayerController, ScreenPos,
			TraceInput.TraceStart, TraceInput.TraceDirection))
		{
			continue;
		}
		OutTraceInputs.Add(TraceInput);
	}
	return OutTraceInputs.Num();
}

/*static*/
bool FITwinTracingHelper::GetRayToTraceFromScreenCenter(const UObject* WorldContextObject, FITwinRayTraceInput& OutTraceInput)
{
	TArray<FITwinRayTraceInput> TraceInputs;
	if (!GetRayTraceInputsFromScreenRatios(WorldContextObject, { { 0.5, 0.5 } }, TraceInputs))
		return false;
	if (ensure(TraceInputs.Num() == 1))
	{
		OutTraceInput = TraceInputs[0];
		return true;
	}
	else
	{
		return false;
	}
}


ITwinElementID FITwinTracingHelper::VisitElementsUnderCursor(UWorld const* World,
	FVector2D& MousePosition, FVector& OutTraceStart, FVector& OutTraceEnd,
	std::function<void(FHitResult const&, std::unordered_set<ITwinElementID>&)>&& HitResultHandler,
	std::optional<uint32> const& MaxUniqueElementsHit /*= std::nullopt*/,
	std::optional<float> const& CustomTraceExtentInMeters /*= std::nullopt*/,
	std::optional<FVector2D> const& CustomMousePosition /*= std::nullopt*/)
{
	FITwinRayTraceInput TraceInput;
	if (!GetRayFromMousePosition(World, MousePosition, TraceInput, CustomMousePosition))
	{
		return ITwin::NOT_ELEMENT;
	}
	FVector::FReal const TraceExtentInMeters = static_cast<FVector::FReal>(
		CustomTraceExtentInMeters.value_or(1e6f)); // 1.000 km by default
	FVector::FReal const TraceExtent = TraceExtentInMeters * 100;
	FVector const TraceEnd = TraceInput.TraceStart + (TraceInput.TraceDirection * TraceExtent);

	bool const bHasHits = Impl->LineTraceMulti(World, TraceInput.TraceStart, TraceEnd);

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
	OutTraceStart = TraceInput.TraceStart;
	OutTraceEnd = TraceEnd;

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
		EltID = ITwinElementID(CesiumMetadataValueAccess::GetUnsignedInteger64(
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
