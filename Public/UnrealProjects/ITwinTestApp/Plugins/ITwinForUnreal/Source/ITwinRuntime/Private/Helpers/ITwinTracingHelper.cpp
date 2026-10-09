/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTracingHelper.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Helpers/ITwinTracingHelper.h>

#include <Compil/IsUsingBentleyUnreal.h>
#include <ITwinIModel.h>
#include <ITwinIModelInternals.h>
#include <ITwinMetadataConstants.h>
#include <ITwinRealityData.h>
#include <ITwinGoogle3DTileset.h>

#include <CesiumMetadataPickingBlueprintLibrary.h>
#include <CesiumMetadataValue.h>

#include <Camera/CameraTypes.h>
#include <Engine/GameViewportClient.h>
#include <Engine/LocalPlayer.h>
#include <Engine/World.h>
#include <GameFramework/PlayerController.h>
#include <RHIGlobals.h>

#if WITH_EDITOR
#include <EditorModeManager.h>
#include <EditorViewportClient.h>
#endif

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
	if (PlayerController) // in-game/PIE
	{
		if (CustomMousePosition)
			MousePosition = *CustomMousePosition;
		else
		{
			ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
			if (!LocalPlayer || !LocalPlayer->ViewportClient)
				return false;
			else if (!LocalPlayer->ViewportClient->GetMousePosition(MousePosition))
				return false;
		}
		FVector WorldLoc, WorldDir;
		if (!PlayerController->DeprojectScreenPositionToWorld(MousePosition.X, MousePosition.Y, WorldLoc, WorldDir))
			return false;
		OutTraceInput.TraceStart = WorldLoc;
		OutTraceInput.TraceDirection = WorldDir;
	}
	else
	{
#if WITH_EDITOR
		ensure(!CustomMousePosition); // TODO: unimplemented
		auto const& ModeTools = GLevelEditorModeTools();
		auto VpClient = ModeTools.GetHoveredViewportClient();
		if (!VpClient)
			VpClient = ModeTools.GetFocusedViewportClient();
		if (!VpClient)
			return false;
		FViewportCursorLocation const EdCursorLoc = VpClient->GetCursorWorldLocationFromMousePos();
		OutTraceInput.TraceStart = EdCursorLoc.GetOrigin();
		OutTraceInput.TraceDirection = EdCursorLoc.GetDirection();
		MousePosition = EdCursorLoc.GetCursorPos();
#else
		return false;
#endif
	}
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
bool FITwinTracingHelper::GetRayToTraceFromScreenCenter(const UObject* WorldContextObject,
														FITwinRayTraceInput& OutTraceInput)
{
	TArray<FITwinRayTraceInput> TraceInputs;
	if (!GetRayTraceInputsFromScreenRatios(WorldContextObject, { { 0.5, 0.5 } }, TraceInputs))
	{
		if (GUsingNullRHI || GIsAutomationTesting)
		{
			// For unit test, add a default input.
			FITwinRayTraceInput TraceInput;
			TraceInputs.Add(TraceInput);
		}
		else
			return false;
	}
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

/*static*/
bool FITwinTracingHelper::IsValidAndVisibleImpact(FHitResult const& HitResult, ITwinElementID& ElementID)
{
	if (!HitResult.HasValidHitObjectHandle())
		return false;
	auto HitActor = HitResult.GetActor();
	if (!HitActor || HitActor->IsHidden())
		return false;
	AITwinIModel* HitIModel = Cast<AITwinIModel>(HitActor->GetOwner());
	if (HitIModel)
	{
		// Note: this function to get the Element ID is different (less efficient) than the one passed to
		// SetTriangleHitFilter in UITwinSceneMappingBuilder::OnTileMeshPrimitiveLoaded: that is because here the
		// Cesium function we use must determine the FCesiumFeatureIdSet from the hit MeshComponent, while in the other
		// case we already obtained it and stored it in the lambda capture.
		auto&& CalcElemID = [&HitResult, &ElementID/*out var!*/]() mutable
			{
				TMap<FString, FCesiumMetadataValue> const Table =
					UCesiumMetadataPickingBlueprintLibrary::GetPropertyTableValuesFromHit(
						HitResult, ITwinCesium::Metada::ELEMENT_FEATURE_ID_SLOT);
				FCesiumMetadataValue const* const ElemIdFound = Table.Find(ITwinCesium::Metada::ELEMENT_NAME);
				if (ElemIdFound != nullptr)
					ElementID = ITwinElementID(
						CesiumMetadataValueAccess::GetUnsignedInteger64(*ElemIdFound, ITwin::NOT_ELEMENT.value()));
				else
					ElementID = ITwin::NOT_ELEMENT;
				return ElementID;
			};
#if BE_IS_USING_BENTLEY_UNREAL
		// Invisible impacts were already filtered thanks to pCollisionMesh->SetTriangleHitFilter
		// (see UITwinSceneMappingBuilder::OnTileMeshPrimitiveLoaded),
		// but we need to determine the ElementID: we could store it in the HitResult somewhere in Unreal's
		// intersection code, but it means touching a lot of code because the low-level intersection functions pass
		// the 'out' parameters one by one (position, vertex index, face index, etc.), not as a single struct, so
		// adding another parameter is not trivial and could affect performance.
		ElementID = CalcElemID();
#else
		// Test if the picked location is visible
		if (!GetInternals(*HitIModel).IsVisibleAtPoint(CalcElemID, HitResult.ImpactPoint))
			return false;
#endif // !BE_IS_USING_BENTLEY_UNREAL
	}
	else // Test if the picked location is visible (mostly = !cut-out)
	{
#if BE_IS_USING_BENTLEY_UNREAL
		// For both Reality Data and Google tilesets, invisible impacts were already filtered thanks to
		// pCollisionMesh->SetTriangleHitFilter (see UITwinClipping3DTilesetHelper::OnTileMeshPrimitiveLoaded)
		// => nothing to do
#else
		AITwinRealityData* HitRealityData = Cast<AITwinRealityData>(HitActor->GetOwner());
		if (HitRealityData)
		{
			if (!HitRealityData->IsVisibleAtPoint(HitResult.ImpactPoint))
				return false;
		}
		else
		{
			AITwinGoogle3DTileset* HitGoogleTileset = Cast<AITwinGoogle3DTileset>(HitActor);
			if (HitGoogleTileset && !HitGoogleTileset->IsVisibleAtPoint(HitResult.ImpactPoint))
				return false;
		}
#endif // !BE_IS_USING_BENTLEY_UNREAL
	}
	return true;
}

ITwinElementID FITwinTracingHelper::VisitElementsUnderCursor(UWorld const* World,
	FVector2D& MousePosition, FVector& OutTraceStart, FVector& OutTraceEnd,
	std::function<void(FHitResult const&, ITwinElementID const&)>&& HitResultHandler,
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
			ITwinElementID ElementID = ITwin::NOT_ELEMENT;
			if (!IsValidAndVisibleImpact(HitResult, ElementID))
				continue;
			HitResultHandler(HitResult, ElementID);

			if (FirstEltID == ITwin::NOT_ELEMENT)
			{
				FirstEltID = ElementID;
				DejaVu.insert(ElementID);//ok even if ITwin::NOT_ELEMENT, to break the loop
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

bool FITwinTracingHelper::FindNearestImpact(FHitResult& OutHitResult, UWorld const* World,
	FVector const& TraceStart, FVector const& TraceEnd)
{
	if (!Impl->LineTraceMulti(World, TraceStart, TraceEnd))
	{
		return false;
	}
	for (auto&& HitResult : Impl->AllHits)
	{
		ITwinElementID ElementID = ITwin::NOT_ELEMENT;
		if (!IsValidAndVisibleImpact(HitResult, ElementID))
			continue;
		OutHitResult = HitResult;
		return true;
	}
	return false;
}
