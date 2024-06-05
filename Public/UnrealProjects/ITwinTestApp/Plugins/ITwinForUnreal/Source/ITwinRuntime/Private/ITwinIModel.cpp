/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinIModel.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinIModel.h>
#include <ITwinIModelInternals.h>
#include <ITwinIModel3DInfo.h>
#include <ITwinDigitalTwin.h>
#include <ITwinGeoLocation.h>
#include <ITwinSavedView.h>
#include <ITwinSceneMappingBuilder.h>
#include <ITwinServerConnection.h>
#include <ITwinSynchro4DAnimator.h>
#include <ITwinSynchro4DSchedules.h>
#include <ITwinSynchro4DSchedulesInternals.h>
#include <ITwinCesium3DTileset.h>
#include <ITwinCesium3DTilesetLoadFailureDetails.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <BeUtils/Gltf/GltfTuner.h>
#include <Dom/JsonObject.h>
#include <DrawDebugHelpers.h>
#include <EngineUtils.h>
#include <GameFramework/Pawn.h>
#include <GameFramework/PlayerController.h>
#include <HttpModule.h>
#include <Interfaces/IHttpResponse.h>
#include <Kismet/KismetMathLibrary.h>
#include <Math/Box.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>
#include <Timeline/Timeline.h>

DEFINE_LOG_CATEGORY(LrtuITwin);

namespace ITwinIModelImpl
{
	// TODO_GCO: move to Scheds comp + std::move param for the time being, there's a single observer anyway
	void OnNewTileMeshInScene(FITwinSynchro4DSchedulesInternals& SchedsInternals,
							  std::set<ITwinElementID> const& MeshElementIDs)
	{
		if (!ensure(SchedsInternals.MakeReady())) return;
		// QueryElementsTasks takes ownership of its parameter, so we'd need to copy it even if it was of the
		// same collection type (unless moving all the way from the inital caller of course):
		std::vector<ITwinElementID> ElementIDs(MeshElementIDs.size());
		for (auto&& Elem : MeshElementIDs)
		{
			ElementIDs.push_back(Elem);
		}
		SchedsInternals.SchedulesApi.QueryElementsTasks(std::move(ElementIDs));
	}
}

class AITwinIModel::FImpl
{
public:
	AITwinIModel& Owner;
	/// helper to fill/update SceneMapping
	TSharedPtr<FITwinSceneMappingBuilder> SceneMappingBuilder;
	std::shared_ptr<BeUtils::GltfTuner> GltfTuner = std::make_shared<BeUtils::GltfTuner>();
	FITwinIModelInternals Internals;
	uint32 TilesetLoadedCount = 0;
	FDelegateHandle OnTilesetLoadFailureHandle;

	FImpl(AITwinIModel& InOwner)
		: Owner(InOwner), Internals(InOwner)
	{
		// create a callback to fill our scene mapping when meshes are loaded
		SceneMappingBuilder = MakeShared<FITwinSceneMappingBuilder>(Internals.SceneMapping);
	}

	void Update()
	{
		Owner.UpdateWebServices();

		if (!Owner.bResolvedChangesetIdValid)
		{
			if (Owner.ChangesetId.IsEmpty())
			{
				Owner.WebServices->GetiModelChangesets(Owner.IModelId);
				return;
			}
			Owner.SetResolvedChangesetId(Owner.ChangesetId);
		}
		if (Owner.ExportStatus == EITwinExportStatus::Unknown || Owner.ExportStatus == EITwinExportStatus::InProgress)
		{
			Owner.ExportStatus = EITwinExportStatus::NoneFound;
			if (!Owner.bResolvedChangesetIdValid)
				return;

			Owner.WebServices->GetExports(Owner.IModelId, Owner.ResolvedChangesetId);
		}
	}

	static void CreateMissingSynchro4DSchedulesComponents(const TArray<FString>&, UWorld* World)
	{
		for (TActorIterator<AITwinIModel> IModelIter(World); IModelIter; ++IModelIter)
		{
			(*IModelIter)->Impl->CreateSynchro4DSchedulesComponent();
		}
	}

	void CreateSynchro4DSchedulesComponent()
	{
		if (!Owner.Synchro4DSchedules)
		{
			Owner.Synchro4DSchedules = NewObject<UITwinSynchro4DSchedules>(&Owner,
				UITwinSynchro4DSchedules::StaticClass(),
				FName(*(Owner.GetActorNameOrLabel() + "_4DSchedules")));
			Owner.Synchro4DSchedules->RegisterComponent();
			for (auto& Child : Owner.Children)
			{
				AITwinCesium3DTileset* AsTileset = Cast<AITwinCesium3DTileset>(Child.Get());
				if (AsTileset)
				{
					// Multiple calls, unlikely as they are, would not be a problem, except that the
					// call to SetNewTileMeshObserver would be redundant.
					SetupSynchro4DSchedules(*AsTileset);
				}
			}
		}
	}

	void SetupSynchro4DSchedules(AITwinCesium3DTileset& Tileset)
	{
		if (!Owner.Synchro4DSchedules)
			return;

		// Automatically query the schedules items related to the Elements in the new tile.
		// Note that the SchedulesApi will filter out all Elements which were already queried.
		Internals.SceneMapping.SetNewTileMeshObserver(
			[Scheds = TWeakObjectPtr<UITwinSynchro4DSchedules>(Owner.Synchro4DSchedules)]
			(CesiumTileID const&, std::set<ITwinElementID> const& MeshElementIDs)
			{
				if (MeshElementIDs.empty() || !Scheds.IsValid())
					return;
				ITwinIModelImpl::OnNewTileMeshInScene(GetInternals(*Scheds), MeshElementIDs);
			});
		// Note: moved at the end of this method because it will trigger a refresh of the tileset, which
		// will then trigger the NewTileMeshObserver set above, which is indeed what we want. This refresh
		// of the tileset happening automatically is also why we don't need to bother calling the observer
		// manually for tiles and meshes already received and displayed: the refresh does it all over again
		SetupMaterials(Tileset);
	}

	void SetupMaterials(AITwinCesium3DTileset& Tileset)
	{
		UMaterialInterface *OpaqueMaterial = nullptr, *TranslucentMaterial = nullptr;
		FITwinSynchro4DSchedulesInternals::GetAnimatableMaterials(OpaqueMaterial, TranslucentMaterial, Owner);
		if (OpaqueMaterial) Tileset.SetMaterial(OpaqueMaterial);
		if (TranslucentMaterial) Tileset.SetTranslucentMaterial(TranslucentMaterial);
	}

	/// Extracts the given element, in all known tiles.
	/// New Unreal entities may be created.
	/// \return the number of entities created in Unreal.
	uint32 ExtractElement(
		ITwinElementID const Element,
		FITwinMeshExtractionOptions const& Options)
	{
		return Internals.SceneMapping.ExtractElement(Element, Options);
	}

	/// Display per-feature bounding boxes for debugging.
	void DisplayFeatureBBoxes()
	{
	#if ENABLE_DRAW_DEBUG
		const UWorld* World = Owner.GetWorld();
		// this is for debugging
		for (auto const& [EltID, Box] : Internals.SceneMapping.GetKnownBBoxes())
		{
			if (Box.IsValid)
			{
				FColor lineColor = FColor::MakeRandomColor();
				FColor fillColor = lineColor;
				fillColor.A = 150;
				DrawDebugSolidBox(
					World,
					Box,
					fillColor,
					FTransform::Identity,
					/*bool bPersistent =*/ false,
					/*float LifeTime =*/ 10.f);
				FVector Center, Extent;
				Box.GetCenterAndExtents(Center, Extent);
				DrawDebugBox(
					World,
					Center,
					Extent,
					lineColor,
					/*bool bPersistent =*/ false,
					/*float LifeTime =*/ 20.f);
			}
		}
	#endif // ENABLE_DRAW_DEBUG
	}
	/// Extract some elements in a subset of the known tiles.
	/// \param percentageOfTiles Percentage (in range [0;1] of known tiles from which to
	/// extract.
	/// \param percentageOfEltsInTile Percentage of elements which will be extracted from
	/// each tile processed.
	/// (for debugging).
	void ExtractElementsOfSomeTiles(
		float percentageOfTiles,
		float percentageOfEltsInTile = 0.1f)
	{
		FITwinMeshExtractionOptions ExtractionOpts;
	#if ENABLE_DRAW_DEBUG
		ExtractionOpts.bPerElementColorationMode = true; // use coloring system for debugging...
	#endif
		Internals.SceneMapping.ExtractElementsOfSomeTiles(
			percentageOfTiles, percentageOfEltsInTile, ExtractionOpts);
	}

	/// Extract the given element from all known tiles.
	uint32 ExtractElement(ITwinElementID const Element)
	{
		FITwinMeshExtractionOptions ExtractionOpts;
#if ENABLE_DRAW_DEBUG
		ExtractionOpts.bPerElementColorationMode = true; // use coloring system for debugging...
#endif
		return Internals.SceneMapping.ExtractElement(Element, ExtractionOpts);
	}

	void HidePrimitivesWithExtractedEntities(bool bHide = true)
	{
		Internals.SceneMapping.HidePrimitivesWithExtractedEntities(bHide);
	}
	void HideExtractedEntities(bool bHide = true)
	{
		Internals.SceneMapping.HideExtractedEntities(bHide);
	}
	void BakeFeaturesInUVs_AllMeshes()
	{
		Internals.SceneMapping.BakeFeaturesInUVs_AllMeshes();
	}

	void InternalSynchro4DTest(bool bTestVisibilityAnim)
	{
#if ENABLE_DRAW_DEBUG
		// Use known boxes just to retrieve all ElementIDs...
		auto const& BBoxes = Internals.SceneMapping.GetKnownBBoxes();

		FITwinElementTimeline ModifiedTimeline(ITwinElementID(0));

		// Simulate an animation of transformation
		using ITwin::Timeline::PTransform;
		ITwin::Schedule::PropertyEntry<PTransform> Entry;
		Entry.time_ = 0.;
		ModifiedTimeline.transform_.list_.insert(Entry);
		for (auto const& [EltID, BBox] : BBoxes)
		{
			ModifiedTimeline.IModelElementID = EltID;
			Internals.OnElementTimelineModified(ModifiedTimeline);
		}
		ModifiedTimeline.transform_.list_.clear();

		if (bTestVisibilityAnim) {
			// Simulate an animation of visibility
			ModifiedTimeline.SetVisibilityAt(
				0., 0. /* alpha */, ITwin::Schedule::InterpolationMode::Linear);
			ModifiedTimeline.SetVisibilityAt(
				30., 1. /* alpha */, ITwin::Schedule::InterpolationMode::Linear);

			for (auto const& [EltID, BBox] : BBoxes)
			{
				ModifiedTimeline.IModelElementID = EltID;
				Internals.OnElementTimelineModified(ModifiedTimeline);
			}
		}
#endif //ENABLE_DRAW_DEBUG
	}
}; // class AITwinIModel::FImpl

class FITwinIModelImplAccess
{
public:
	static AITwinIModel::FImpl& Get(AITwinIModel& IModel)
	{
		return *IModel.Impl;
	}
};

FITwinIModelInternals& GetInternals(AITwinIModel& IModel)
{
	return FITwinIModelImplAccess::Get(IModel).Internals;
}


AITwinIModel::AITwinIModel()
	:Impl(MakePimpl<FImpl>(*this))
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("root")));
}

void AITwinIModel::UpdateIModel()
{
	check(!IModelId.IsEmpty()); // TODO_AW
	check(ServerConnection); // TODO_AW
	bResolvedChangesetIdValid = false;
	ExportStatus = EITwinExportStatus::Unknown;
	DestroyTileset();
	Impl->Update();
	UpdateSavedViews();
}

void AITwinIModel::GetModel3DInfo(FITwinIModel3DInfo& Info)
{
	FBox const& IModelBBox = Impl->Internals.SceneMapping.GetIModelBoundingBox();
	if (IModelBBox.IsValid)
	{
		Info.BoundingBoxMin = IModelBBox.Min;
		Info.BoundingBoxMax = IModelBBox.Max;
	}
	Info.ModelCenter = Impl->Internals.SceneMapping.GetModelCenter();
}

namespace ITwin
{
	void UpdateWebServices(AActor* Owner, IITwinWebServicesObserver* InObserver,
		TObjectPtr<AITwinServerConnection>& ServerConnection,
		TObjectPtr<UITwinWebServices>& WebServices)
	{
		if (!ServerConnection && UITwinWebServices::GetWorkingInstance())
		{
			// Happens when the requests are made from blueprints, typically in the previous 3DFT plugin
			UITwinWebServices::GetWorkingInstance()->GetServerConnection(ServerConnection);
		}
		const bool bHasValidWebServices = WebServices && WebServices->IsValidLowLevel();
		const bool bHasChangedConnection =
			bHasValidWebServices && !WebServices->HasSameConnection(ServerConnection.Get());
		if (!bHasValidWebServices || bHasChangedConnection)
		{
			WebServices = NewObject<UITwinWebServices>(Owner);
			WebServices->SetServerConnection(ServerConnection);
			WebServices->SetObserver(InObserver);
		}
	}
}

void AITwinIModel::UpdateWebServices()
{
	ITwin::UpdateWebServices(this, this, ServerConnection, WebServices);
}

void AITwinIModel::LoadModel(FString InExportId)
{
	UpdateWebServices();
	if (!InExportId.IsEmpty())
	{
		WebServices->GetExportInfo(InExportId);
	}
}

void AITwinIModel::SetResolvedChangesetId(FString const& InChangesetId)
{
	ResolvedChangesetId = InChangesetId;
	bResolvedChangesetIdValid = true;
}

void AITwinIModel::OnChangesetsRetrieved(bool bSuccess, FChangesetInfos const& Infos)
{
	if (!bSuccess)
		return;

	SetResolvedChangesetId(Infos.Changesets.IsEmpty() ? FString() : Infos.Changesets[0].Id);

	Impl->Update();
}

void AITwinIModel::OnTilesetLoadFailure(FITwinCesium3DTilesetLoadFailureDetails const& Details)
{
	if (Details.Tileset.IsValid() && Details.Tileset->GetOwner() == this)
	{
		this->OnIModelLoaded.Broadcast(false);
	}
}

void AITwinIModel::OnTilesetLoaded()
{
	// For internal reasons, this callback can be called several times (whenever the Cesium tileset has to be
	// updated depending on the camera frustum) => ensure we only call the OnIModelLoaded callback once, or
	// else some unwanted operations may occur, typically with old 3dft-plugin level blueprint, where this
	// signal triggered an adjustment of the initial camera...
	if (Impl->TilesetLoadedCount == 0)
	{
		this->OnIModelLoaded.Broadcast(true);
	}
	Impl->TilesetLoadedCount++;
}

void AITwinIModel::OnExportInfosRetrieved(bool bSuccess, FITwinExportInfos const& ExportInfosArray)
{
	if (!bSuccess)
		return;

	for (FITwinExportInfo const& Info : ExportInfosArray.ExportInfos)
	{
		if (Info.Status == "Complete")
		{
			ExportStatus = EITwinExportStatus::Complete;
			FActorSpawnParameters SpawnParams;
			SpawnParams.Owner = this;
			const auto Tileset = GetWorld()->SpawnActor<AITwinCesium3DTileset>(SpawnParams);
#if WITH_EDITOR
			Tileset->SetActorLabel(GetActorLabel() + TEXT(" tileset"));
#endif
			Tileset->AttachToActor(this, FAttachmentTransformRules::KeepRelativeTransform);
			// TODO_GCO: Necessary for picking, unless there is another method that does
			// not require the Physics data? Note that pawn collisions are disabled to
			// still allow navigation through meshes.
			//Tileset->SetCreatePhysicsMeshes(false);
			// connect mesh creation callback
			Tileset->SetMeshBuildCallbacks(Impl->SceneMappingBuilder);
			Tileset->SetGltfTuner(Impl->GltfTuner);
			Tileset->SetTilesetSource(ETilesetSource::FromUrl);
			Tileset->SetUrl(Info.MeshUrl);
			if (!Geolocation)
			{
				// can happen if the IModel was created manually, outside of any instance of AITwinDigitalTwin
				Geolocation = MakeShared<FITwinGeolocation>(*this);
			}
			// Because iModel geolocation info is not available yet:
			Tileset->SetGeoreference(Geolocation->NonLocatedGeoreference.Get());
			// Disabled in the plugin for now (schedules component not created), to hide functionality not
			// ready for release
			//Impl->SetupSynchro4DSchedules(*Tileset);
			Impl->SetupMaterials(*Tileset);//<== remove when uncommenting SetupSynchro4DSchedules!

			Impl->TilesetLoadedCount = 0;
			Tileset->OnTilesetLoaded.AddDynamic(
				this, &AITwinIModel::OnTilesetLoaded);
			Impl->OnTilesetLoadFailureHandle = OnCesium3DTilesetLoadFailure.AddUObject(
				this, &AITwinIModel::OnTilesetLoadFailure);
			return;
		}
		ExportStatus = EITwinExportStatus::InProgress;
	}
}

void AITwinIModel::OnExportInfoRetrieved(bool bSuccess, FITwinExportInfo const& ExportInfo)
{
	// This callback is called when an export was actually found and LoadModel was called with the latter
	// => update the IModelID and changeset ID accordingly.
	if (bSuccess
		&& ExportInfo.Status == "Complete")
	{
		ExportId = ExportInfo.Id;
		IModelId = ExportInfo.iModelId;
		ITwinId = ExportInfo.iTwinId;
		ChangesetId = ExportInfo.ChangesetId;
		SetResolvedChangesetId(ChangesetId);
	}
	// Actually load the Cesium tileset in case of success
	FITwinExportInfos infos;
	infos.ExportInfos.Push(ExportInfo);
	OnExportInfosRetrieved(bSuccess, infos);
}

void AITwinIModel::Retune()
{
	++Impl->GltfTuner->currentVersion;
}

void AITwinIModel::StartExport()
{
	UpdateWebServices();
	if (!IModelId.IsEmpty())
	{
		WebServices->StartExport(IModelId, ResolvedChangesetId);
	}
}

void AITwinIModel::OnExportStarted(bool bSuccess, FString const& InExportId)
{

}

void AITwinIModel::UpdateSavedViews()
{
	UpdateWebServices();
	if (WebServices
		&& !IModelId.IsEmpty()
		&& !ITwinId.IsEmpty())
	{
		WebServices->GetAllSavedViews(ITwinId, IModelId);
	}
}

void AITwinIModel::OnSavedViewInfosRetrieved(bool bSuccess, FSavedViewInfos const& SavedViews)
{
	if (!bSuccess)
		return;
	//clean IModel saved view children
	const auto ChildrenCopy = this->Children;
	for (auto& Child : ChildrenCopy)
	{
		if (Child->IsA(AITwinSavedView::StaticClass()))
			GetWorld()->DestroyActor(Child);
	}
	for (const auto& info : SavedViews.SavedViews)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		const auto savedViewActor = GetWorld()->SpawnActor<AITwinSavedView>(SpawnParams);
#if WITH_EDITOR
		savedViewActor->SetActorLabel(info.DisplayName);
#endif
		savedViewActor->DisplayName = info.DisplayName;
		//using the attachment to list savedViews in an iModel
		savedViewActor->AttachToActor(this, FAttachmentTransformRules::KeepRelativeTransform);
		savedViewActor->ServerConnection = ServerConnection;
		savedViewActor->SavedViewId = info.Id;
	}
}

void AITwinIModel::OnSavedViewsRetrieved(bool bSuccess, FSavedViewInfos SavedViews)
{
	OnSavedViewInfosRetrieved(bSuccess, SavedViews);
}

void AITwinIModel::OnSavedViewRetrieved(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo)
{

}

void AITwinIModel::OnSavedViewAdded(bool bSuccess, FSavedViewInfo const& SavedViewInfo)
{
	if (!bSuccess)
		return;
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	const auto savedViewActor = GetWorld()->SpawnActor<AITwinSavedView>(SpawnParams);
#if WITH_EDITOR
	savedViewActor->SetActorLabel(SavedViewInfo.DisplayName);
#endif
	// RQ/Note: I'm using the attachment to list savedViews in an iModel
	savedViewActor->AttachToActor(this, FAttachmentTransformRules::KeepRelativeTransform);
	savedViewActor->ServerConnection = ServerConnection;
	savedViewActor->SavedViewId = SavedViewInfo.Id;
}

void AITwinIModel::AddSavedView(const FString& displayName)
{
	auto camPos = GetWorld()->GetFirstPlayerController()->GetPawn()->GetActorLocation();
	auto camRot = GetWorld()->GetFirstPlayerController()->GetPawn()->GetActorRotation();
	//convert pos to itwin
	camPos /= FVector(100.0, -100.0, 100.0);
	//convert rot to itwin
	FRotator rot(-90.0, -90.0, 0.0);
	camRot = UKismetMathLibrary::ComposeRotators(rot.GetInverse(), camRot);
	camRot = camRot.GetInverse();
	camRot.Yaw *= -1.0;
	UpdateWebServices();
	if (WebServices
		&& !IModelId.IsEmpty()
		&& !ITwinId.IsEmpty())
	{
		WebServices->AddSavedView(ITwinId, IModelId, { camPos, {0.0,0.0,0.0}, camRot },
														   {"",displayName,true});
	}
}

void AITwinIModel::OnSavedViewDeleted(bool bSuccess, FString const& Response)
{
}

void AITwinIModel::OnSavedViewEdited(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo)
{
}

void AITwinIModel::Reset()
{
	DestroyTileset();
}

void AITwinIModel::DestroyTileset()
{
	const auto ChildrenCopy = Children;
	uint32 numDestroyed = 0;
	for (auto& Child : ChildrenCopy)
	{
		AITwinCesium3DTileset* AsTileset = Cast<AITwinCesium3DTileset>(Child.Get());
		if (AsTileset)
		{
			GetWorld()->DestroyActor(Child);
			numDestroyed++;
		}
	}
	check(Children.Num() + numDestroyed == ChildrenCopy.Num());
}

void AITwinIModel::Destroyed()
{
	if (WebServices)
	{
		WebServices->SetObserver(nullptr);
	}
	if (Impl->OnTilesetLoadFailureHandle.IsValid())
	{
		OnCesium3DTilesetLoadFailure.Remove(Impl->OnTilesetLoadFailureHandle);
	}
	const auto ChildrenCopy = Children;
	for (auto& Child: ChildrenCopy)
		GetWorld()->DestroyActor(Child);
}

void AITwinIModel::UpdateAfterLoadingUIEvent()
{
	if (LoadingMethod == ELoadingMethod::LM_Manual && !ExportId.IsEmpty())
	{
		DestroyTileset();
		LoadModel(ExportId);
	}
	else if (LoadingMethod == ELoadingMethod::LM_Automatic && !IModelId.IsEmpty() && !ChangesetId.IsEmpty())
	{
		UpdateIModel();
	}
}

void AITwinIModel::OnAuthorizationDone(bool bSuccess, FString const& AuthError)
{
	if (bSuccess)
	{
		UpdateWebServices();
		UpdateAfterLoadingUIEvent();
	}
	else
	{
		UE_LOG(LrtuITwin, Error, TEXT("AITwinIModel Authorization failure (%s)"), *AuthError);
	}
}

void AITwinIModel::OnITwinsRetrieved(bool bSuccess, FITwinInfos const& Infos)
{
	checkf(false, TEXT("an iModel cannot handle iTwins"));
}
void AITwinIModel::OnIModelsRetrieved(bool bSuccess, FIModelInfos const& Infos)
{
	checkf(false, TEXT("an iModel cannot handle other iModels"));
}
void AITwinIModel::OnRealityDataRetrieved(bool bSuccess, FITwinRealityDataInfos const& Infos)
{
	checkf(false, TEXT("an iModel cannot handle RealityData"));
}

#if WITH_EDITOR

void AITwinIModel::PostEditChangeProperty(struct FPropertyChangedEvent& e)
{
	UE_LOG(LrtuITwin, Display, TEXT("AITwinIModel::PostEditChangeProperty()"));
	Super::PostEditChangeProperty(e);
	FName PropertyName = (e.Property != nullptr) ? e.Property->GetFName() : NAME_None;
	if (PropertyName == "iModelId"
		|| PropertyName == "IModelId"
		|| PropertyName == "ChangesetId"
		|| PropertyName == "ExportId")
	{
		UpdateWebServices();
		// If no ServerConnection has been created yet, make sure we request an authentication and then
		// process the actual loading request(s).
		if (!ServerConnection)
		{
			WebServices->CheckAuthorization();
		}
		else
		{
			UpdateAfterLoadingUIEvent();
		}
	}
}
#endif

void FITwinIModelInternals::OnElementTimelineModified(FITwinElementTimeline const& ModifiedTimeline)
{
	// If the functors are not called, it just means the Element is unknown: nothing we can do, we have
	// received no tile with this Element yet. Note: when receiving new tiles, we also look for existing
	// timelines (see FITwinSceneMapping::OnNewTileMeshFromBuilder)
	ProcessElementInEachTile(ModifiedTimeline.IModelElementID,
		[&ModifiedTimeline, this](CesiumTileID const& TileID, FITwinSceneTile& SceneTile,
								  FITwinElementFeaturesInTile& ElementFeaturesInTile)
			{ SceneMapping.OnBatchedElementTimelineModified(TileID, SceneTile, ElementFeaturesInTile,
															ModifiedTimeline); },
		[&ModifiedTimeline, this](FITwinSceneTile& /*SceneTile*/,
								  FITwinExtractedEntity& ExtractedEntity)
			{ SceneMapping.OnExtractedElementTimelineModified(ExtractedEntity, ModifiedTimeline); });
	UITwinSynchro4DSchedules* Schedules = Owner.FindComponentByClass<UITwinSynchro4DSchedules>();
	if (!Schedules)
		return;
	GetInternals(*Schedules).Timeline().IncludeTimeRange(ModifiedTimeline);
}

void FITwinIModelInternals::OnClickedElement(ITwinElementID const Element,
										 FHitResult const& HitResult)
{
	FBox const BBox = SceneMapping.GetBoundingBox(Element);
	UE_LOG(LrtuITwin, Display, TEXT("ElementID 0x%I64x found in iModel %s with BBox %s"),
		Element.value(), *Owner.GetActorNameOrLabel(), *BBox.ToString());

	// TODO_JDE
	// To mimic 3dft-plugin behavior, we could highlight the corresponding element in the viewport, by
	// modifying our material shader (using a distinct parameter as the one already used for Synchro4D...)

#if ENABLE_DRAW_DEBUG
	// Draw element bounding box for a few seconds for debugging
	if (BBox.IsValid)
	{
		FVector Center, Extent;
		BBox.GetCenterAndExtents(Center, Extent);
		DrawDebugBox(
			Owner.GetWorld(),
			Center,
			Extent,
			FColor::Green,
			/*bool bPersistent =*/ false,
			/*float LifeTime =*/ 10.f);
	}
	// Also draw the GLTF primitive and owning tile bounding boxes
	if (HitResult.Component.IsValid())
	{
		DrawDebugBox(
			Owner.GetWorld(),
			HitResult.Component->Bounds.Origin,
			HitResult.Component->Bounds.BoxExtent,
			FColor::Blue,
			/*bool bPersistent =*/ false,
			/*float LifeTime =*/ 10.f);

		auto const TileID = SceneMapping.DrawOwningTileBox(
			HitResult.GetComponent(),
			Owner.GetWorld());
		if (TileID)
		{
			// Log the Tile ID
			FString const TileIdString(
				Cesium3DTilesSelection::TileIdUtilities::createTileIdString(
					*TileID).c_str());
			UE_LOG(LrtuITwin, Display, TEXT("Owning Tile: %s"), *TileIdString);
		}
	}
	// Another debugging option: extract clicked Element
	static bool bExtractElementOnClick = false;
	if (bExtractElementOnClick && Element != ITwin::NOT_ELEMENT)
	{
		FITwinMeshExtractionOptions ExtractOpts;
		ExtractOpts.bPerElementColorationMode = true;
		SceneMapping.ExtractElement(Element, ExtractOpts);
	}
#endif // ENABLE_DRAW_DEBUG

	UITwinSynchro4DSchedules* Schedules = Owner.FindComponentByClass<UITwinSynchro4DSchedules>();
	if (!Schedules)
		return;
	FString const ElementTimelineDescription = GetInternals(*Schedules).ElementTimelineAsString(Element);
	if (ElementTimelineDescription.IsEmpty())
		return;
	UE_LOG(LrtuITwin, Display, TEXT("ElementID 0x%I64x has a timeline:\n%s"),
			Element.value(), *ElementTimelineDescription);
}

void FITwinIModelInternals::SelectElement(ITwinElementID const InElementID)
{
	SceneMapping.SelectElement(InElementID);
}

namespace ITwin::Schedule
{
	extern float fProbaOfOpacityAnimation;
}

#if ENABLE_DRAW_DEBUG

// Console command to draw bounding boxes
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinDisplayFeaturesBBoxes(
	TEXT("cmd.ITwin_DisplayFeaturesBBoxes"),
	TEXT("Display per FeatureID bounding boxes."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	for (TActorIterator<AITwinIModel> IModelIter(World); IModelIter; ++IModelIter)
	{
		FITwinIModelImplAccess::Get(**IModelIter).DisplayFeatureBBoxes();
	}
})
);

// Console command to extract some meshes
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinExtractSomeMeshes(
	TEXT("cmd.ITwin_ExtractSomeMeshes"),
	TEXT("Extract some meshes from the known tiles."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	float percentageOfTiles = 0.25f;
	float percentageOfEltsInTiles = 0.20f;
	bool bHide = true;
	if (Args.Num() >= 1)
	{
		percentageOfTiles = FCString::Atof(*Args[0]);
	}
	if (Args.Num() >= 2)
	{
		percentageOfEltsInTiles = FCString::Atof(*Args[1]);
	}
	for (TActorIterator<AITwinIModel> IModelIter(World); IModelIter; ++IModelIter)
	{
		FITwinIModelImplAccess::Get(**IModelIter).ExtractElementsOfSomeTiles(
			percentageOfTiles,
			percentageOfEltsInTiles);
	}
})
);

// Console command to extract a given ITwin Element ID
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinExtractElement(
	TEXT("cmd.ITwin_ExtractElement"),
	TEXT("Extract a given ITwin Element from the known tiles."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	ITwinElementID Element = ITwin::NOT_ELEMENT;
	if (Args.Num() >= 1)
	{
		Element = ITwinElementID(FCString::Strtoui64(*Args[0], nullptr, 10 /*Base*/));
	}
	if (Element != ITwin::NOT_ELEMENT)
	{
		for (TActorIterator<AITwinIModel> IModelIter(World); IModelIter; ++IModelIter)
		{
			FITwinIModelImplAccess::Get(**IModelIter).ExtractElement(Element);
		}
	}
})
);

// Console command to hide GLTF meshes partly (or fully) extracted
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinHidePrimitivesWithExtractedEntities(
	TEXT("cmd.ITwin_HidePrimitivesWithExtractedEntities"),
	TEXT("Hide ITwin primitives from which some parts were extracted."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	bool bHide = true;
	if (!Args.IsEmpty())
	{
		bHide = Args[0].ToBool();
	}
	for (TActorIterator<AITwinIModel> IModelIter(World); IModelIter; ++IModelIter)
	{
		FITwinIModelImplAccess::Get(**IModelIter).HidePrimitivesWithExtractedEntities(bHide);
	}
})
);

// Console command to hide all extracted meshes.
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinHideExtractedEntities(
	TEXT("cmd.ITwin_HideExtractedEntities"),
	TEXT("Hide entities previously extracted from ITwin primitives."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	bool bHide = true;
	if (!Args.IsEmpty())
	{
		bHide = Args[0].ToBool();
	}
	for (TActorIterator<AITwinIModel> IModelIter(World); IModelIter; ++IModelIter)
	{
		FITwinIModelImplAccess::Get(**IModelIter).HideExtractedEntities(bHide);
	}
})
);

// Console command to bake features in UVs
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinBakeFeaturesInUVs(
	TEXT("cmd.ITwin_BakeFeaturesInUVs"),
	TEXT("Bake features in per-vertex UVs for all known ITwin primitives."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	for (TActorIterator<AITwinIModel> IModelIter(World); IModelIter; ++IModelIter)
	{
		FITwinIModelImplAccess::Get(**IModelIter).BakeFeaturesInUVs_AllMeshes();
	}
})
);

// Console command to test visibility animation translucent materials
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinAllowSynchro4DOpacityAnimation(
	TEXT("cmd.ITwinAllowSynchro4DOpacityAnimation"),
	TEXT("Allow opacity animation in Synchro4D random testing appearance profiles "
		 "(probability between 0 and 1, default 0.5)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	float fProbaOfOpacityAnimation = 0.5f;
	if (!Args.IsEmpty())
	{
		fProbaOfOpacityAnimation = FCString::Atof(*Args[0]);
	}
	ITwin::Schedule::fProbaOfOpacityAnimation = fProbaOfOpacityAnimation;
}));

// Console command equivalent to ITwinTestApp's ATopMenu::ZoomOnIModel, useful elsewhere, except that here
// we check all iModels in the current world
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinFitIModelInView(
	TEXT("cmd.ITwinFitIModelInView"),
	TEXT("Move the viewport pawn so that all iModels are visible in the viewport."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	FBox AllBBox(ForceInitToZero);
	for (TActorIterator<AITwinIModel> IModelIter(World); IModelIter; ++IModelIter)
	{
		FBox const& IModelBBox = GetInternals(**IModelIter).SceneMapping.GetIModelBoundingBox();
		if (IModelBBox.IsValid)
		{
			if (AllBBox.IsValid) AllBBox += IModelBBox;
			else AllBBox = IModelBBox;
		}
	}
	World->GetFirstPlayerController()->GetPawnOrSpectator()->SetActorLocation(
		// "0.5" is empirical, let's not be too far from the center of things, iModels tend to have
		// a large context around the actual area of interest...
		AllBBox.GetCenter() - FMath::Max(0.5 * AllBBox.GetSize().Length(), 10000)
			* ((AActor*)World->GetFirstPlayerController())->GetActorForwardVector(),
		false, nullptr, ETeleportType::TeleportPhysics);
}));

// Console command to create the Schedules components
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinSetupIModelSchedules(
	TEXT("cmd.ITwinSetupIModelSchedules"),
	TEXT("Creates a 4D Schedules component for each iModel."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		AITwinIModel::FImpl::CreateMissingSynchro4DSchedulesComponents));

#endif // ENABLE_DRAW_DEBUG
