/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPickingActor.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "Helpers/ITwinPickingActor.h"

#include "Helpers/ITwinTracingHelper.h"
#include <ITwinIModel.h>
#include <ITwinIModelInternals.h>
#include <CesiumMetadataPickingBlueprintLibrary.h>
#include <CesiumMetadataValue.h>
#include <ITwinMetadataConstants.h>

#include <Components/InputComponent.h>
#include <Containers/Ticker.h>
#include <DrawDebugHelpers.h>
#include <EngineUtils.h>

#if WITH_EDITOR
	#include "Helpers/ITwinPickingEdMode.h"
	#include <Editor.h>
	#include <EditorModeManager.h>
	#include <EditorModes.h>
#endif

#include <optional>
#include <unordered_set>

namespace ITwin
{
	std::optional<uint64_t> GetMaterialIDFromHit(FHitResult const& HitResult, AITwinIModel& IModel)
	{
		// Blame here to see how we used to handle material prediction here (the feature was removed).

		// General case: test meta-data produced by the Mesh Export Service. In this case, we will get
		// the iModel's RenderMaterial ID.
		TMap<FString, FCesiumMetadataValue> const Table1 =
			UCesiumMetadataPickingBlueprintLibrary::GetPropertyTableValuesFromHit(
				HitResult, ITwinCesium::Metada::MATERIAL_FEATURE_ID_SLOT);
		FCesiumMetadataValue const* const MaterialIdFound = Table1.Find(ITwinCesium::Metada::MATERIAL_NAME);
		if (MaterialIdFound != nullptr)
		{
			return CesiumMetadataValueAccess::GetUnsignedInteger64(
				*MaterialIdFound, ITwin::NOT_ELEMENT.value());
		}
		return std::nullopt;
	}
}

AITwinPickingActor::AITwinPickingActor()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("root")));
}

void AITwinPickingActor::PostLoad()
{
	Super::PostLoad();
	if (!bEnablePicking)
		return;
	OnToggledPicking();
}

#if WITH_EDITOR
void AITwinPickingActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	auto const Name = PropertyChangedEvent.Property->GetFName();
	if (Name == GET_MEMBER_NAME_CHECKED(AITwinPickingActor, bEnablePicking))
		OnToggledPicking();
}
#endif // WITH_EDITOR

void AITwinPickingActor::OnToggledPicking()
{
	if (bEnablePicking)
	{
		if (GetWorld()->GetFirstPlayerController())
		{
			if (InputComponent)
			{
				EnableInput(GetWorld()->GetFirstPlayerController());
				InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this,
										&AITwinPickingActor::PickUnderCursor);
			}
		}
		else if (!IsRunningCommandlet())
		{
#if WITH_EDITOR
			GLevelEditorModeTools().AddDefaultMode(FITwinPickingEdMode::EM_ITwinPicking);
			GLevelEditorModeTools().RemoveDefaultMode(FBuiltinEditorModes::EM_Default);
			GLevelEditorModeTools().ActivateDefaultMode();
#endif
		}
	}
	else
	{
		if (GetWorld()->GetFirstPlayerController())
		{
			if (InputComponent)
				InputComponent->ClearActionBindings();
		}
		else if (!IsRunningCommandlet())
		{
#if WITH_EDITOR
			GLevelEditorModeTools().AddDefaultMode(FBuiltinEditorModes::EM_Default);
			GLevelEditorModeTools().RemoveDefaultMode(FITwinPickingEdMode::EM_ITwinPicking);
			GLevelEditorModeTools().ActivateDefaultMode();
#endif
		}
	}
}

void AITwinPickingActor::PickUnderCursor()
{
	FString ElementId;
	FVector2D MousePosition;
	FHitResult HitResult;
	PickUnderCursorWithOptions(ElementId, MousePosition, /*ThisIModelOnly =*/nullptr, HitResult,
								FITwinPickingOptions{});
}

void AITwinPickingActor::PickUnderCursorWithOptions(FPickingResult& OutPickingResult,
	AITwinIModel* PickedIModel, FITwinPickingOptions const& Options)
{
	FString& ElementId(OutPickingResult.ElementId);
	FHitResult& VisibleHit(OutPickingResult.HitResult);
	OutPickingResult.MaterialId.Reset();

	std::optional<uint32> const MaxUniqueElementsHit = 1;
	VisibleHit.Reset();
	ITwinElementID PickedEltID = ITwin::NOT_ELEMENT;
	std::optional<uint64_t> PickedMaterial;
	AITwinIModel* PickedMaterialIModel = nullptr;
	std::optional<float> CustomTraceExtentInMeters;
	std::optional<FVector2D> CustomMousePosition;
	if (Options.CustomTraceExtentInMeters > 0.f)
	{
		CustomTraceExtentInMeters = Options.CustomTraceExtentInMeters;
	}
	if (Options.CustomMousePosition)
	{
		CustomMousePosition.emplace(*Options.CustomMousePosition);
	}

	// For additive selection, constrain picking to the iModel that already owns the selection
	static FString LastPickedIModelId = FString("");
	if (Options.bAdditiveSelection && !LastPickedIModelId.IsEmpty() && !PickedIModel)
	{
		for (TActorIterator<AITwinIModel> Iter(GetWorld()); Iter; ++Iter)
		{
			if ((*Iter)->IModelId == LastPickedIModelId)
			{
				PickedIModel = *Iter;
				break;
			}
		}
	}

	FITwinTracingHelper TracingHelper;
	if (Options.ActorsToIgnore.Num() > 0)
		TracingHelper.AddIgnoredActors(Options.ActorsToIgnore);
	if (Options.ComponentsToIgnore.Num() > 0)
		TracingHelper.AddIgnoredComponents(Options.ComponentsToIgnore);
	TracingHelper.VisitElementsUnderCursor(GetWorld(),
		OutPickingResult.MousePosition, OutPickingResult.TraceStart, OutPickingResult.TraceEnd,
		[this, PickedIModel, &Options, &PickedEltID, &PickedMaterial, &PickedMaterialIModel, &VisibleHit,
			&TracingHelper]
		(FHitResult const& HitResult, ITwinElementID const& EltID)
		{
			AITwinIModel* iModel = PickedIModel;
			// If passed, use it as a filter, otherwise, set it.
			// Using GetOwner() because the hit actor is actually the cesium tileset
			AActor* HitTilesetOwner = nullptr;
			if (HitResult.HasValidHitObjectHandle())
				if (AActor* HitTileset = HitResult.GetActor())
					HitTilesetOwner = HitTileset->GetOwner(); // may be null or sth else than an iModel of course
			if (iModel && iModel != HitTilesetOwner)
				return;
			if (!iModel)
				iModel = Cast<AITwinIModel>(HitTilesetOwner);

			if (EltID != ITwin::NOT_ELEMENT)
				PickedEltID = EltID;
			if (iModel)
			{
				FITwinIModelInternals& IModelInternals = GetInternals(*iModel);
				if (Options.bAdditiveSelection && Options.bSelectElement
					&& IModelInternals.GetSelectedElements().contains(EltID))
				{
					// Toggling inside a multi-selection: if the element is already selected, just deselect it
					auto SceneMappingLock = IModelInternals.SceneMapping->GetAutoLock();
					std::unordered_set<ITwinElementID> ToDeselect{ EltID };
					SceneMappingLock->DeselectElements(ToDeselect);
				}
				else if (IModelInternals.HasElementWithID(EltID))
				{
					IModelInternals.OnClickedElement(EltID, HitResult, Options.bSelectElement,
						Options.bAdditiveSelection);
				}
				if (Options.bSelectMaterial)
				{
					PickedMaterial = ITwin::GetMaterialIDFromHit(HitResult, *iModel);
					PickedMaterialIModel = iModel;
				}
			}
			if (!VisibleHit.HasValidHitObjectHandle())
				VisibleHit = HitResult;

		//if (Options.bSelectMaterial && !PickedMaterialIModel && iModel) {
		// IsValidAndVisibleImpact now returns true for those impacts so this should be redundant:
			// Some primitive parts may not be assigned any ElementID but still have a valid ITwin material.
		//	...

		}, MaxUniqueElementsHit, CustomTraceExtentInMeters, CustomMousePosition);

	if (!PickedIModel && VisibleHit.HasValidHitObjectHandle())
		PickedIModel = Cast<AITwinIModel>(VisibleHit.GetActor()->GetOwner());
	if (Options.bSelectElement)
	{
		if (!Options.bAdditiveSelection)
		{
			// remove highlights from all iModels except the one (possibly) selected
			for (TActorIterator<AITwinIModel> Iter(GetWorld()); Iter; ++Iter)
			{
				if ((*Iter) != PickedIModel)
					DeSelect(*Iter);
			}
		}
		if (PickedEltID != ITwin::NOT_ELEMENT)
		{
			// convert picked element ID to string.
			ElementId = ITwin::ToString(PickedEltID);
			OnElemPicked.Broadcast(ElementId, PickedIModel->IModelId);
			LastPickedIModelId = PickedIModel->IModelId;
		}
		else
		{
			if (!Options.bAdditiveSelection)
			{
				if (PickedIModel)
					DeSelect(PickedIModel);
				OnElemPicked.Broadcast("", LastPickedIModelId);
			}
		}
	}

	OutPickingResult.PickedMaterialIModel = nullptr;
	if (PickedMaterial && ensure(PickedMaterialIModel))
	{
#if ENABLE_DRAW_DEBUG
		UE_LOG(LogITwin, Display, TEXT("iTwin MaterialID: %llu - name: %s"),
			*PickedMaterial, *PickedMaterialIModel->GetMaterialName(*PickedMaterial));
#endif

		if (Options.bHighlightSelectedMaterial)
		{
			// Highlight the selected material (in all tiles of the iModel).
			PickedMaterialIModel->HighlightMaterial(*PickedMaterial);
		}
		if (Options.bBroadcastMaterialSelection)
		{
			OnMaterialPicked.Broadcast(*PickedMaterial, PickedMaterialIModel->IModelId);
		}
		OutPickingResult.MaterialId = *PickedMaterial;
		OutPickingResult.PickedMaterialIModel = PickedMaterialIModel;
	}
}

void AITwinPickingActor::PickUnderCursorWithOptions(FString& ElementId, FVector2D& MousePosition,
	AITwinIModel* PickedIModel, FHitResult& VisibleHit, FITwinPickingOptions const& Options)
{
	FPickingResult PickingResult;
	PickUnderCursorWithOptions(PickingResult, PickedIModel, Options);

	ElementId = MoveTemp(PickingResult.ElementId);
	MousePosition = MoveTemp(PickingResult.MousePosition);
	VisibleHit = MoveTemp(PickingResult.HitResult);
}

void AITwinPickingActor::PickObjectAtMousePosition(FString& ElementId, FVector2D& MousePosition,
	AITwinIModel* iModel, FHitResult& VisibleHit)
{
	PickUnderCursorWithOptions(ElementId, MousePosition, iModel, VisibleHit,
		FITwinPickingOptions{ .bSelectElement = false, .bSelectMaterial = false });
}

void AITwinPickingActor::DeSelect(AITwinIModel* iModel)
{
	if (ensure(iModel))
	{
		iModel->DeSelectAll();
	}
}
