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
#include <Engine/RendererSettings.h>
#include <EngineUtils.h>
#include <GameFramework/Pawn.h>
#include <GameFramework/PlayerController.h>
#include <HttpModule.h>
#include <Interfaces/IHttpResponse.h>
#include <Kismet/KismetMathLibrary.h>
#include <Math/Box.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>
#include <JsonObjectConverter.h>
#include <Timeline/Timeline.h>
#include <ITwinUtilityLibrary.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/ITwinAPI/ITwinTypes.h>
#include <Compil/AfterNonUnrealIncludes.h>

class AITwinIModel::FImpl
{
public:
	AITwinIModel& Owner;
	/// helper to fill/update SceneMapping
	TSharedPtr<FITwinSceneMappingBuilder> SceneMappingBuilder;
	std::shared_ptr<BeUtils::GltfTuner> GltfTuner = std::make_shared<BeUtils::GltfTuner>();
	bool bHasFilledMaterialInfoFromTuner = false;
	std::unordered_set<uint64_t> MatIDsToSplit; // stored to detect the need for retuning
	FITwinIModelInternals Internals;
	uint32 TilesetLoadedCount = 0;
	FDelegateHandle OnTilesetLoadFailureHandle;

	// Some operations require to first fetch a valid server connection
	enum class EOperationUponAuth : uint8
	{
		None,
		Load,
		Update,
	};
	EOperationUponAuth PendingOperation = EOperationUponAuth::None;
	bool bAutoStartExportIfNeeded = false;
	struct FIModelProperties
	{
		std::optional<FProjectExtents> ProjectExtents;
		std::optional<FEcefLocation> EcefLocation;
	};
	std::optional<FIModelProperties> IModelProperties; //!< Empty means not inited yet.

	FImpl(AITwinIModel& InOwner)
		: Owner(InOwner), Internals(InOwner), ElementMetadataQuerying(InOwner)
	{
		// create a callback to fill our scene mapping when meshes are loaded
		SceneMappingBuilder = MakeShared<FITwinSceneMappingBuilder>(Internals.SceneMapping, Owner);
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

			Owner.WebServices->GetExports(Owner.IModelId, Owner.GetSelectedChangeset());
		}
	}

	static void ZoomOn(FBox const& FocusBBox, UWorld* World, double MinDistanceToCenter = 10000)
	{
		if (!ensure(World)) return;
		auto* PlayerController = World->GetFirstPlayerController();
		APawn* Pawn = PlayerController ? PlayerController->GetPawnOrSpectator() : nullptr;
		if (Pawn)
		{
			auto const BBoxLen = FocusBBox.GetSize().Length();
			Pawn->SetActorLocation(
				// "0.5" is empirical, let's not be too far from the center of things, iModels tend to have
				// a large context around the actual area of interest...
				FocusBBox.GetCenter()
					- FMath::Max(0.5 * BBoxLen, MinDistanceToCenter)
						* ((AActor*)PlayerController)->GetActorForwardVector(),
				false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

#if ENABLE_DRAW_DEBUG
	static void CreateMissingSynchro4DSchedules(UWorld* World)
	{
		for (TActorIterator<AITwinIModel> IModelIter(World); IModelIter; ++IModelIter)
		{
			(*IModelIter)->Impl->CreateSynchro4DSchedulesComponent();
		}
	}
#endif // ENABLE_DRAW_DEBUG

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
					// Multiple calls, unlikely as they are, would not be a problem, except that setting
					// OnNewTileMeshBuilt would be redundant.
					SetupSynchro4DSchedules(*AsTileset);
				}
			}
		}
	}

	void SetupSynchro4DSchedules(AITwinCesium3DTileset& Tileset)
	{
		if (!Owner.Synchro4DSchedules)
			return;
		if (!ensure(!GetDefault<URendererSettings>()->bOrderedIndependentTransparencyEnable))
		{
			// see OIT-related posts in
			// https://forums.unrealengine.com/t/ue5-gpu-crashed-or-d3d-device-removed/524297/168:
			// it could be a problem with all transparencies (and "mask opacity"), not just cutting planes!
			UE_LOG(LogTemp, Error, TEXT("bOrderedIndependentTransparencyEnable=true will crash cut planes, sorry! See if 'r.OIT.SortedPixels' is in your DefaultEngine.ini, in section [/Script/Engine.RendererSettings], if not, add it set to False (and relaunch the app or Editor).\nDISABLING ALL Cutting Planes (aka. growth simulation) in the Synchro4D schedules!"));
			Owner.Synchro4DSchedules->bDisableCuttingPlanes = true;
		}
		// Automatically query the schedules items related to the Elements in the new tile. Note that the
		// SchedulesApi will filter out all Elements which were already queried in their entirety (ie without
		// time range restriction)
		Internals.SceneMapping.OnNewTileMeshBuilt =
			[&SchedulesInternals = GetInternals(*Owner.Synchro4DSchedules)]
			(CesiumTileID const& TileID, std::set<ITwinElementID>&& MeshElements)
			{
				SchedulesInternals.OnNewTileMeshBuilt(TileID, std::move(MeshElements));
			};
		// Note: placed at the end of this method because it will trigger a refresh of the tileset, which
		// will then trigger the OnNewTileMeshBuilt set above, which is indeed what we want. This refresh
		// of the tileset happening automatically is also why we don't need to bother calling the observer
		// manually for tiles and meshes already received and displayed: the refresh does it all over again
		SetupMaterials(Tileset);
		// Now done in the main ticker function (see AITwinIModel's ctor) because we need to wait for the end
		// of the Elements metadata properties queries (Elements hierarchy + Source IDs) to make proper sense
		// of the animation bindings received.
		//GetInternals(*Owner.Synchro4DSchedules).MakeReady(); <== MakeReady used to call ResetSchedules
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
		for (auto const& Elem : Internals.SceneMapping.GetElements())
		{
			if (Elem.bHasMesh && Elem.BBox.IsValid)
			{
				FColor lineColor = FColor::MakeRandomColor();
				FColor fillColor = lineColor;
				fillColor.A = 150;
				DrawDebugSolidBox(
					World,
					Elem.BBox,
					fillColor,
					FTransform::Identity,
					/*bool bPersistent =*/ false,
					/*float LifeTime =*/ 10.f);
				FVector Center, Extent;
				Elem.BBox.GetCenterAndExtents(Center, Extent);
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

#if ENABLE_DRAW_DEBUG
	void InternalSynchro4DTest(bool bTestVisibilityAnim)
	{
		auto const& AllElems = Internals.SceneMapping.GetElements();
		std::set<ITwinElementID> IModelElements;
		for (auto const& Elem : AllElems)
		{
			IModelElements.insert(Elem.Id);
		}
		FITwinElementTimeline ModifiedTimeline(FIModelElementsKey((size_t)0)/*GroupIdx*/, IModelElements);

		// Simulate an animation of transformation
		using ITwin::Timeline::PTransform;
		ITwin::Timeline::PropertyEntry<PTransform> Entry;
		Entry.Time = 0.;
		ModifiedTimeline.Transform.Values.insert(Entry);
		Internals.OnElementsTimelineModified(ModifiedTimeline);
		ModifiedTimeline.Transform.Values.clear();

		if (bTestVisibilityAnim) {
			// Simulate an animation of visibility
			ModifiedTimeline.SetVisibilityAt(
				0., 0. /* alpha */, ITwin::Timeline::EInterpolation::Linear);
			ModifiedTimeline.SetVisibilityAt(
				30., 1. /* alpha */, ITwin::Timeline::EInterpolation::Linear);
			Internals.OnElementsTimelineModified(ModifiedTimeline);
		}
	}

	static void InternalSynchro4DDebugElement(const TArray<FString>& Args, UWorld* World)
	{
		for (TActorIterator<AITwinIModel> IModelIter(World); IModelIter; ++IModelIter)
		{
			(*IModelIter)->Impl->InternalSynchro4DDebugElement(Args);
		}
	}

	void InternalSynchro4DDebugElement(const TArray<FString>& Args)
	{
		CreateSynchro4DSchedulesComponent();
		if (!Owner.Synchro4DSchedules) return;
		auto& SchedulesInternals = GetInternals(*Owner.Synchro4DSchedules);

		auto CreateDebugTimeline = [this, &SchedulesInternals](ITwinElementID const ElementID)
			{
				FITwinElementTimeline& ElementTimeline = SchedulesInternals.Timeline()
				   .ElementTimelineFor(FIModelElementsKey(ElementID), std::set<ITwinElementID>{ ElementID });

				// Simulate an animation of cutting plane
				using ITwin::Timeline::FDeferredPlaneEquation;
				ElementTimeline.SetCuttingPlaneAt(0.,
					FVector::ZAxisVector, ITwin::Timeline::EGrowthStatus::FullyRemoved,
					ITwin::Timeline::EInterpolation::Linear);
				ElementTimeline.SetCuttingPlaneAt(30.,
					FVector::ZAxisVector, ITwin::Timeline::EGrowthStatus::FullyGrown,
					ITwin::Timeline::EInterpolation::Linear);

				// Simulate an animation of visibility
				ElementTimeline.SetVisibilityAt(
					0., 0.2 /* alpha */, ITwin::Timeline::EInterpolation::Linear);
				ElementTimeline.SetVisibilityAt(
					30., 0.8 /* alpha */, ITwin::Timeline::EInterpolation::Linear);

				Internals.OnElementsTimelineModified(ElementTimeline);
			};
		if (Args.IsEmpty())
		{
			auto const& AllElems = Internals.SceneMapping.GetElements();
			for (auto const& Elem : AllElems)
			{
				CreateDebugTimeline(Elem.Id);
			}
		}
		else
		{
			// single-Element mode intended for the bug with 0x20000002623 in Civil ConCenter 2023...
			ITwinElementID const ElementID = ITwin::ParseElementID(Args[0]);
			if (ITwin::NOT_ELEMENT != ElementID)
				CreateDebugTimeline(ElementID);
		}
	}
#endif //ENABLE_DRAW_DEBUG

	/// Used to query info about all Elements of the iModel by reading rows from its database tables through
	/// a paginated series of HTTP RPC requests. We'll read first the parent-child relationships, then the
	/// "Source Element ID" (aka "Identifier") information, successively.
	class FQueryElementMetadataPageByPage
	{
	public:
		enum class EState {
			NotStarted, Running, NeedRestart, Finished, StoppedOnError
		};

	private:
		AITwinIModel& Owner;
		/// TODO_GCO: a bit hacky, would be cleaner to use 2 instances of this class, working in succession,
		/// or maybe in parallel (which would mean using the RequestID to determine which emitter to forward
		/// the replies to)
		enum class EElementsMetadata {
			Hierarchy, SourceIdentifiers
		};
		EState State = EState::NotStarted;
		int QueryRowStart = 0, TotalRowsParsed = 0;
		EElementsMetadata KindOfMetadata = EElementsMetadata::Hierarchy;
		FString BatchMsg;
		static const int QueryRowCount = 50000;

		void DoRestart()
		{
			UE_LOG(LogITwin, Display, TEXT("Elements metadata queries interrupted, restarting..."));
			QueryRowStart = TotalRowsParsed = 0;
			KindOfMetadata = EElementsMetadata::Hierarchy;
			BatchMsg = TEXT("Element parent-child pairs");
			QueryNextPage();
		}

	public:
		FQueryElementMetadataPageByPage(AITwinIModel& InOwner) : Owner(InOwner) {}
		EState GetState() const { return State; }

		void Restart()
		{
			if (EState::NotStarted == State || EState::Finished == State || EState::StoppedOnError == State)
			{
				DoRestart();
			}
			else
			{
				State = EState::NeedRestart;
			}
		}

		void QueryNextPage()
		{
			State = EState::Running;
			switch (KindOfMetadata)
			{
			case EElementsMetadata::Hierarchy:
				Owner.WebServices->QueryIModel(Owner.ITwinId, Owner.IModelId, Owner.ResolvedChangesetId,
					TEXT("SELECT ECInstanceID, SourceECInstanceID FROM bis.ElementOwnsChildElements"),
					QueryRowStart, QueryRowCount);
				break;
			case EElementsMetadata::SourceIdentifiers:
				Owner.WebServices->QueryIModel(Owner.ITwinId, Owner.IModelId, Owner.ResolvedChangesetId,
					TEXT("SELECT Element.Id, Identifier FROM bis.ExternalSourceAspect"),
					QueryRowStart, QueryRowCount);
				break;
			default:
				ensure(false);
				break;
			}
			QueryRowStart += QueryRowCount;
		}

		void OnQueryCompleted(bool bSuccess, FString const& QueryResult)
		{
			if (EState::NeedRestart == State)
			{
				DoRestart();
				return;
			}
			if (!ensure(bSuccess))
			{
				UE_LOG(LogITwin, Error, TEXT("Total %s retrieved: %d, stopping on error!"), *BatchMsg,
										TotalRowsParsed);
				State = EState::StoppedOnError;
				return;
			}
			// Testing RowsParsed vs QueryRowCount would be risky since we might get errors
			// when parsing some rows...
			int const RowsParsed = (EElementsMetadata::Hierarchy == KindOfMetadata)
				? GetInternals(Owner).SceneMapping.ParseHierarchyTree(QueryResult)
				: GetInternals(Owner).SceneMapping.ParseSourceElementIDs(QueryResult);
			TotalRowsParsed += RowsParsed;
			if (RowsParsed > 0)
			{
				UE_LOG(LogITwin, Verbose, TEXT("%s retrieved: %d, asking for more..."), *BatchMsg,
										  TotalRowsParsed);
				QueryNextPage();
			}
			else
			{
				UE_LOG(LogITwin, Display, TEXT("Total %s retrieved: %d."), *BatchMsg, TotalRowsParsed);
				if (EElementsMetadata::Hierarchy == KindOfMetadata)
				{
					KindOfMetadata = EElementsMetadata::SourceIdentifiers;
					QueryRowStart = TotalRowsParsed = 0;
					BatchMsg = TEXT("Source Element IDs");
					QueryNextPage();
				}
				else State = EState::Finished;
			}
		}
	};
	FQueryElementMetadataPageByPage ElementMetadataQuerying;

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
	Synchro4DSchedules = CreateDefaultSubobject<UITwinSynchro4DSchedules>(TEXT("Schedules"));

	// As soon as material IDs are read, launch a request to RPC service to get the corresponding material
	// properties
	Impl->GltfTuner->SetMaterialInfoReadCallback(
		[this](std::vector<BeUtils::ITwinMaterialInfo> const& materialInfos)
	{
		// Initialize the map of customizable materials at once.
		FillMaterialInfoFromTuner();

		// Launch a request to fetch all material properties.
		TArray<FString> MaterialIds;
		MaterialIds.Reserve(materialInfos.size());
		Algo::Transform(materialInfos, MaterialIds,
			[](BeUtils::ITwinMaterialInfo const& V) -> FString
		{
			return FString::Printf(TEXT("0x%I64x"), V.id);
		});
		GetMutableWebServices()->GetMaterialListProperties(ITwinId, IModelId, GetSelectedChangeset(),
			MaterialIds);
	});

	// Single ticker for all iModel updates (selection highlight, schedules queries and replay...)
	// to avoid spawning multiple ones everywhere needed
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[This = TWeakObjectPtr<AITwinIModel>(this)] (float Delta)
		{
			if (This.IsValid())
			{
				GetInternals(*This).SceneMapping.UpdateSelectionAndHighlightTextures();
				if (This->Synchro4DSchedules && 
					AITwinIModel::FImpl::FQueryElementMetadataPageByPage::EState::Finished
						== This->Impl->ElementMetadataQuerying.GetState())
				{
					This->Synchro4DSchedules->TickSchedules(Delta);
				}
				return true; // repeated tick
			}
			else return false;
		}));
}

void AITwinIModel::UpdateIModel()
{
	if (IModelId.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("ITwinIModel with no IModelId cannot be updated"));
		return;
	}

	// If no access token has been retrieved yet, make sure we request an authentication and then process
	// the actual update.
	if (CheckServerConnection() != AITwinServiceActor::EConnectionStatus::Connected)
	{
		Impl->PendingOperation = FImpl::EOperationUponAuth::Update;
		return;
	}

	bResolvedChangesetIdValid = false;
	ExportStatus = EITwinExportStatus::Unknown;
	DestroyTileset();
	Impl->Update();
	UpdateSavedViews();
}

void AITwinIModel::ZoomOnIModel()
{
	FBox const& IModelBox = Impl->Internals.SceneMapping.GetIModelBoundingBox(EITwinCoordSystem::UE);
	// Not always valid (if no tile was ever loaded!)
	if (IModelBox.IsValid)
	{
		FImpl::ZoomOn(IModelBox, GetWorld());
	}
}

void AITwinIModel::AutoExportAndLoad()
{
	if (ensure(LoadingMethod == ELoadingMethod::LM_Automatic
		&& !IModelId.IsEmpty()))
	{
		// automatically start the export if necessary.
		Impl->bAutoStartExportIfNeeded = true;
		UpdateIModel();
	}
}

void AITwinIModel::GetModel3DInfoInCoordSystem(FITwinIModel3DInfo& OutInfo, EITwinCoordSystem CoordSystem,
	bool bGetLegacy3DFTValue /*= false*/) const
{
	FBox const& IModelBBox = Impl->Internals.SceneMapping.GetIModelBoundingBox(CoordSystem);
	if (IModelBBox.IsValid)
	{
		OutInfo.BoundingBoxMin = IModelBBox.Min;
		OutInfo.BoundingBoxMax = IModelBBox.Max;
	}
	// The 'ModelCenter' used in 3DFT plugin is defined as the translation of the iModel (which is an offset
	// existing in the iModel itself, and can be found in Cesium export by retrieving the translation of the
	// root tile). It was mainly (and probably only) to adjust the savedViews in the 3DFT plugin, due to the
	// way the coordinate system of the iModel was handled in the legacy 3DFT display engine.
	// This offset should *not* be done with Cesium tiles, so we decided to always return zero except if the
	// customer explicitly request this former value (and then it would be interesting to know why he does
	// this...)
	if (bGetLegacy3DFTValue)
	{
		OutInfo.ModelCenter = Impl->Internals.SceneMapping.GetModelCenter(CoordSystem);
	}
	else
	{
		OutInfo.ModelCenter = FVector(0, 0, 0);
	}
}

void AITwinIModel::GetModel3DInfo(FITwinIModel3DInfo& Info) const
{
	// For compatibility with former 3DFT plugin, we work in the iTwin coordinate system here.
	// However, we'll retrieve a null ModelCenter to avoid breaking savedViews totally (see comments in
	// ATopMenu::#StartCameraMovementToSavedView, which is the C++ version of the blueprint provided in
	// default 3DFT level.
	GetModel3DInfoInCoordSystem(Info, EITwinCoordSystem::ITwin, false);
}

namespace ITwin
{
	void DestroyTilesetsInActor(AActor& Owner)
	{
		const auto ChildrenCopy = Owner.Children;
		uint32 numDestroyed = 0;
		for (auto& Child : ChildrenCopy)
		{
			if (Cast<AITwinCesium3DTileset>(Child.Get()))
			{
				Owner.GetWorld()->DestroyActor(Child);
				numDestroyed++;
			}
		}
		ensureMsgf(Owner.Children.Num() + numDestroyed == ChildrenCopy.Num(),
			TEXT("UWorld::DestroyActor should notify the owner"));
	}

	bool HasTilesetWithLocalURL(AActor const& Owner)
	{
		for (auto const& Child : Owner.Children)
		{
			auto const* pTileset = Cast<const AITwinCesium3DTileset>(Child.Get());
			if (pTileset && pTileset->GetUrl().StartsWith(TEXT("file:///")))
			{
				return true;
			}
		}
		return false;
	}

	ITwinElementID GetMaterialIDFromHit(FHitResult const& HitResult);
}

void AITwinIModel::LoadModel(FString InExportId)
{
	UpdateWebServices();
	if (WebServices && !InExportId.IsEmpty())
	{
		WebServices->GetExportInfo(InExportId);
	}
}

FString AITwinIModel::GetSelectedChangeset() const
{
	// By construction, this is generally ResolvedChangesetId - it can be empty if no export has been loaded
	// yet, *or* in the particular case of an iModel without any changeset...
	if (bResolvedChangesetIdValid)
		return ResolvedChangesetId;
	else
		return ChangesetId;
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

	FITwinExportInfo const* CompleteInfo = nullptr;
	for (FITwinExportInfo const& Info : ExportInfosArray.ExportInfos)
	{
		if (Info.Status == TEXT("Complete"))
		{
			CompleteInfo = &Info;
			break;
		}
		else
		{
			ExportStatus = EITwinExportStatus::InProgress;
		}
	}
	if (!CompleteInfo)
	{
		if (ExportStatus == EITwinExportStatus::NoneFound && Impl->bAutoStartExportIfNeeded)
		{
			// in manual mode, automatically start an export if none exists yet
			StartExport();
		}
		return;
	}
	ExportStatus = EITwinExportStatus::Complete;
	// in Automatic mode, it is still empty and must be set here because the 4D apis require it:
	ITwinId = CompleteInfo->iTwinId;
	ExportId = CompleteInfo->Id; // informative only (needed here for  Automatic mode)
	const auto MakeTileset = [this, CompleteInfo = *CompleteInfo]
		{
			// No need to keep former versions of the tileset
			GetInternals(*this).SceneMapping.Reset();
			DestroyTileset();

			// We need to query the hierarchy of iModel Elements using several "paginated" requests sent
			// successively, but we also need to support interrupting and restart queries from scratch
			// because this code path can be executed several times for an iModel, eg. upon UpdateIModel
			Impl->ElementMetadataQuerying.Restart();
			// It seems risky to NOT do a ResetSchedules here: for example, FITwinElement::AnimationKeys are
			// not set, MainTimeline::NonAnimatedDuplicates is empty, etc.
			// To avoid redownloading everything, we could just "reinterpret" the known schedule data...?
			if (Synchro4DSchedules)
				Synchro4DSchedules->ResetSchedules();

			// *before* SpawnActor otherwise Cesium will create its own default georef
			auto&& Geoloc = FITwinGeolocation::Get(*GetWorld());

			FActorSpawnParameters SpawnParams;
			SpawnParams.Owner = this;
			const auto Tileset = GetWorld()->SpawnActor<AITwinCesium3DTileset>(SpawnParams);
#if WITH_EDITOR
			// in manual mode, the name is usually not set at this point => adjust it now
			if (!CompleteInfo.DisplayName.IsEmpty()
				&& (GetActorLabel().StartsWith(TEXT("ITwinIModel")) || GetActorLabel().StartsWith(TEXT("IModel"))))
			{
				this->SetActorLabel(CompleteInfo.DisplayName);
			}
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

			Tileset->SetTilesetSource(EITwinTilesetSource::FromUrl);
			Tileset->SetUrl(CompleteInfo.MeshUrl);
			check(Impl->IModelProperties);
			if (Impl->IModelProperties->EcefLocation)
			{
				// iModel is geolocated.
				Tileset->SetGeoreference(Geoloc->GeoReference.Get());
				// If the shared georeference is not inited yet, let's initialize it according to this iModel location.
				if (Geoloc->GeoReference->GetOriginPlacement() == EITwinOriginPlacement::TrueOrigin)
				{
					Geoloc->GeoReference->SetOriginPlacement(EITwinOriginPlacement::CartographicOrigin);
					// Put georeference at the cartographic coordinates of the center of the iModel's extents.
					Geoloc->GeoReference->SetOriginEarthCenteredEarthFixed(
						FTransform(UITwinUtilityLibrary::ConvertRotator_ITwinToUnreal(Impl->IModelProperties->EcefLocation->Orientation),
							Impl->IModelProperties->EcefLocation->Origin).TransformPosition(
							Impl->IModelProperties->ProjectExtents ?
								0.5*(Impl->IModelProperties->ProjectExtents->Low+Impl->IModelProperties->ProjectExtents->High) :
								FVector::ZeroVector));
				}
			}
			else
			{
				// iModel is not geolocated.
				Tileset->SetGeoreference(Geoloc->LocalReference.Get());
			}
			if (Synchro4DSchedules)
				Impl->SetupSynchro4DSchedules(*Tileset);
			else
				// useful when commenting out schedules component's default creation for testing?
				Impl->SetupMaterials(*Tileset);

			Impl->TilesetLoadedCount = 0;
			Tileset->OnTilesetLoaded.AddDynamic(
				this, &AITwinIModel::OnTilesetLoaded);
			Impl->OnTilesetLoadFailureHandle = OnCesium3DTilesetLoadFailure.AddUObject(
				this, &AITwinIModel::OnTilesetLoadFailure);
		};
	// To assign the correct georeference to the tileset, we need some properties of the iModel
	// (whether it is geolocated, its extents...), which are retrieved by a specific request.
	// At this point, it is very likely the properties have not been retrieved yet
	if (Impl->IModelProperties)
	{
		// Properties have already been retrieved.
		MakeTileset();
	}
	else
	{
		// Properties have not been retrieved yet, we have to send the request.
		// We could implement AITwinIModel::OnIModelPropertiesRetrieved() and construct the tileset here,
		// but in this case we would need to keep FITwinExportInfo as a member variable since it is needed
		// to initialize the tileset.
		// So to keep everything locally to this function, we create a local temporary observer that will
		// handle the request result and construct the tileset.
		class FTempObserver: public FITwinDefaultWebServicesObserver
		{
		public:
			//! Keep a shared pointer on this to control destruction.
			std::shared_ptr<FTempObserver> This;
			AITwinIModel& Owner;
			decltype(MakeTileset) MakeTileset;
			FTempObserver(AITwinIModel& InOwner, const decltype(MakeTileset)& InMakeTileset)
				:Owner(InOwner)
				,MakeTileset(InMakeTileset)
			{
			}
			virtual void OnIModelPropertiesRetrieved(bool bSuccess, bool bHasExtents, FProjectExtents const& Extents, bool bHasEcefLocation, FEcefLocation const& EcefLocation) override
			{
				Owner.Impl->IModelProperties.emplace();
				if (bSuccess)
				{
					if (bHasExtents)
						Owner.Impl->IModelProperties->ProjectExtents = Extents;
					if (bHasEcefLocation)
						Owner.Impl->IModelProperties->EcefLocation = EcefLocation;
				}
				// Restore the WebServices' observer.
				Owner.WebServices->SetObserver(&Owner);
				// Now that properties have been retrieved, we can construct the tileset.
				MakeTileset();

				// Self-destruct. Note that if the changeset is empty, GetIModelProperties will exit at once
				// and thus the shared_ptr usage is still 2 at this point...
				ensureMsgf(This.use_count() == 1 || (This.use_count() == 2 && Owner.GetSelectedChangeset().IsEmpty()),
					TEXT("FTempObserver self-destruction issue!"));
				This.reset();
			}
			virtual const TCHAR* GetObserverName() const override
			{
				return Owner.GetObserverName();
			}
		};
		const auto Observer = std::make_shared<FTempObserver>(*this, MakeTileset);
		Observer->This = Observer;
		// Temporarily change the WebServices' observer before sending the request.
		WebServices->SetObserver(Observer.get());
		check(!ITwinId.IsEmpty());
		WebServices->GetIModelProperties(ITwinId, IModelId, GetSelectedChangeset());
	}
}

void AITwinIModel::BeginPlay()
{
	Super::BeginPlay();
	// If a tileset was already loaded (eg. in the Editor, priori to PIE), we won't be receiving any calls to
	// OnMeshConstructed for existing meshes: so either 1/ we declare everything in SceneMapping as UPROPERTY,
	// so that they are copied over from Editor to PIE (non-UPROPERTY data is lost when entering PIE!),
	// or 2/ we need to refresh the Tileset to rebuild all over again.
	// Solution 1/ is obviously better as that's what Unreal wants us to do, but it's more work and thus a
	// longer term target.
	//Do NOT test this, obviously it IS empty since data is not carried over from Editor mode!!
	//if (!Impl->Internals.SceneMapping.GetElements().empty())
	if (!IModelId.IsEmpty() && !ChangesetId.IsEmpty())
		UpdateIModel();
}

void AITwinIModel::OnExportInfoRetrieved(bool bSuccess, FITwinExportInfo const& ExportInfo)
{
	// This callback is called when an export was actually found and LoadModel was called with the latter
	// => update the IModelID and changeset ID accordingly.
	if (bSuccess
		&& ExportInfo.Status == TEXT("Complete"))
	{
		ExportId = ExportInfo.Id;
		IModelId = ExportInfo.iModelId;
		ITwinId = ExportInfo.iTwinId;
		ChangesetId = ExportInfo.ChangesetId;
		SetResolvedChangesetId(ChangesetId);
	}
	// Actually load the Cesium tileset if the request was successful and the export is complete
	FITwinExportInfos infos;
	infos.ExportInfos.Push(ExportInfo);
	OnExportInfosRetrieved(bSuccess, infos);

	if (!bSuccess || ExportInfo.Status == TEXT("Invalid"))
	{
		// the export may have been interrupted on the server, or deleted...
		ExportStatus = EITwinExportStatus::Unknown;
	}

	if (ExportStatus == EITwinExportStatus::InProgress)
	{
		// Still in progress => test again in 3 seconds
		TestExportCompletionAfterDelay(ExportInfo.Id, 3.f);
	}
}

void AITwinIModel::OnElementPropertiesRetrieved(bool bSuccess, FElementProperties const& ElementProps)
{
	if (!bSuccess)
		return;
	FString JSONString;
	FJsonObjectConverter::UStructToJsonObjectString(ElementProps, JSONString, 0, 0);
	UE_LOG(LogITwin, Display, TEXT("Element properties retrieved: %s"), *JSONString);
}

void AITwinIModel::OnIModelQueried(bool bSuccess, FString const& QueryResult)
{
	Impl->ElementMetadataQuerying.OnQueryCompleted(bSuccess, QueryResult);
}

void AITwinIModel::OnMaterialPropertiesRetrieved(bool bSuccess, SDK::Core::ITwinMaterialPropertiesMap const& props)
{
	if (!bSuccess)
		return;
	// TODO_JDE convert ITwinMaterialProperties into something we can use to convert the iTwin material into
	// an Unreal material
	for (auto const& [matId, matProperties] : props.data_)
	{
		ensureMsgf(matId == matProperties.id, TEXT("material ID mismatch vs map key!"));
		FString const MaterialID(matId.c_str());
		auto const id64 = ITwin::ParseElementID(MaterialID);
		// If the list of iTwin material IDs was read from tileset.json, the material being inspected should be
		// found in (the map) CustomMaterials which we filled from the latter.
		if (!CustomMaterials.IsEmpty()
			&&
			ensureMsgf(id64 != ITwin::NOT_ELEMENT, TEXT("Invalid material ID %s"), *MaterialID))
		{
			FITwinCustomMaterial* CustomMat = CustomMaterials.Find(id64.value());
			ensureMsgf(CustomMat != nullptr, TEXT("Material mismatch: ID %s not found in tileset.json (%s)"),
				*MaterialID, *FString(matProperties.name.c_str()));
		}
	}
}

void AITwinIModel::Retune()
{
	++Impl->GltfTuner->currentVersion;
}

bool AITwinIModel::ShouldFillMaterialInfoFromTuner() const
{
	return !Impl->bHasFilledMaterialInfoFromTuner
		&& Impl->GltfTuner->HasITwinMaterialInfo();
}

void AITwinIModel::FillMaterialInfoFromTuner()
{
	auto const materials = Impl->GltfTuner->GetITwinMaterialInfo();
	CustomMaterials.Reserve((int32)materials.size());
	for (BeUtils::ITwinMaterialInfo const& matInfo : materials)
	{
		FITwinCustomMaterial& CustomMat = CustomMaterials.FindOrAdd(matInfo.id);
		CustomMat.Name = matInfo.name.c_str();
		// Material names usually end with a suffix in the form of ": <IMODEL_NAME>"
		// => discard this part
		int32 LastColon = UE::String::FindLast(CustomMat.Name, TEXT(":"));
		if (LastColon > 0)
		{
			CustomMat.Name.RemoveAt(LastColon, CustomMat.Name.Len() - LastColon);
		}
	}
	Impl->bHasFilledMaterialInfoFromTuner = true;
}

void AITwinIModel::SplitGltfModelForCustomMaterials()
{
	std::unordered_set<uint64_t> MatIDsToSplit;
	for (auto const& [MatID, CustomMat] : CustomMaterials)
	{
		if (CustomMat.Material.Get())
		{
			MatIDsToSplit.insert(MatID);
		}
	}
	if (MatIDsToSplit != Impl->MatIDsToSplit)
	{
		Impl->MatIDsToSplit = MatIDsToSplit;

		BeUtils::GltfTuner::Rules rules;
		rules.itwinMatIDsToSplit_.swap(MatIDsToSplit);
		Impl->GltfTuner->SetRules(std::move(rules));

		Retune();
	}
}

void AITwinIModel::StartExport()
{
	if (IModelId.IsEmpty())
	{
		UE_LOG(LogITwin, Error, TEXT("IModelId is required to start an export"));
		return;
	}
	if (ExportStatus == EITwinExportStatus::InProgress)
	{
		// do not accumulate exports...
		UE_LOG(LogITwin, Display, TEXT("Export is already in progress for ITwinIModel %s"), *IModelId);
		return;
	}
	UpdateWebServices();
	if (WebServices)
	{
		WebServices->StartExport(IModelId, GetSelectedChangeset());
	}
}

void AITwinIModel::OnExportStarted(bool bSuccess, FString const& InExportId)
{
	if (!bSuccess)
		return;
	ExportStatus = EITwinExportStatus::InProgress;
	TestExportCompletionAfterDelay(InExportId, 3.f); // 3s
}

void AITwinIModel::TestExportCompletionAfterDelay(FString const& InExportId, float DelayInSeconds)
{
	// Create a ticker to test the new export completion.
	// Note: 'This' used to be a(n anonymous) TStrongObjectPtr, to prevent it from being GC'd, and avoid the
	// test on IsValid, but it apparently does not work when leaving PIE, only when switching Levels, etc.
	// The comment in ITwinGeoLocation.h also hints that preventing a Level from unloading by keeping strong
	// ptr is not a good idea, hence the weak ptr here.
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([InExportId, This = TWeakObjectPtr<AITwinIModel>(this)](float Delta)
	{
		if (This.IsValid())
			This->LoadModel(InExportId);
		return false; // One tick
	}), DelayInSeconds);
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

const FProjectExtents* AITwinIModel::GetProjectExtents() const
{
	return Impl->IModelProperties && Impl->IModelProperties->ProjectExtents ?
		&*Impl->IModelProperties->ProjectExtents : nullptr;
}

const FEcefLocation* AITwinIModel::GetEcefLocation() const
{
	return Impl->IModelProperties && Impl->IModelProperties->EcefLocation ?
		&*Impl->IModelProperties->EcefLocation : nullptr;
}

const AITwinCesium3DTileset* AITwinIModel::GetTileset() const
{
	for (auto& Child : Children)
		if (auto* Tileset = Cast<AITwinCesium3DTileset>(Child.Get()))
			return Tileset;
	return nullptr;
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

void AITwinIModel::OnSavedViewInfoAdded(bool bSuccess, FSavedViewInfo SavedViewInfo)
{
	OnSavedViewAdded(bSuccess, SavedViewInfo);
}

namespace ITwin
{
	extern bool GetSavedViewFromPlayerController(UWorld const* World, FSavedView& OutSavedView);
}

void AITwinIModel::AddSavedView(const FString& displayName)
{
	if (IModelId.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("IModelId is required to create a new SavedView"));
		return;
	}
	if (ITwinId.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("ITwinId is required to create a new SavedView"));
		return;
	}

	FSavedView NewSavedView;
	if (!ITwin::GetSavedViewFromPlayerController(GetWorld(), NewSavedView))
	{
		return;
	}

	UpdateWebServices();
	if (WebServices)
	{
		WebServices->AddSavedView(ITwinId, IModelId, NewSavedView, { TEXT(""), displayName, true });
	}
}

void AITwinIModel::OnSavedViewDeleted(bool bSuccess, FString const& SavedViewId, FString const& Response)
{
}

void AITwinIModel::OnSavedViewEdited(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo)
{
}

const TCHAR* AITwinIModel::GetObserverName() const
{
	return TEXT("ITwinIModel");
}

void AITwinIModel::Reset()
{
	DestroyTileset();
}

void AITwinIModel::DestroyTileset()
{
	ITwin::DestroyTilesetsInActor(*this);
}

void AITwinIModel::RefreshTileset()
{
	for (auto& Child : Children)
	{
		AITwinCesium3DTileset* Tileset = Cast<AITwinCesium3DTileset>(Child.Get());
		if (Tileset)
		{
			// Before refreshing the tileset, make sure we invalidate the mapping
			GetInternals(*this).SceneMapping.Reset();
			// Also make sure we reload material info from the tuner
			Impl->bHasFilledMaterialInfoFromTuner = false;
			Tileset->SetMeshBuildCallbacks(Impl->SceneMappingBuilder);
			Tileset->SetGltfTuner(Impl->GltfTuner);
			Tileset->RefreshTileset();
		}
	}
}

void AITwinIModel::Destroyed()
{
	Super::Destroyed();
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
		AutoExportAndLoad();
	}
}

void AITwinIModel::UpdateOnSuccessfulAuthorization()
{
	switch (Impl->PendingOperation)
	{
	case FImpl::EOperationUponAuth::Load:
		UpdateAfterLoadingUIEvent();
		break;
	case FImpl::EOperationUponAuth::Update:
		UpdateIModel();
		break;
	case FImpl::EOperationUponAuth::None:
		break;
	}
	Impl->PendingOperation = FImpl::EOperationUponAuth::None;
}

void AITwinIModel::OnLoadingUIEvent()
{
	// If no access token has been retrieved yet, make sure we request an authentication and then
	// process the actual loading request(s).
	if (CheckServerConnection() != AITwinServiceActor::EConnectionStatus::Connected)
	{
		Impl->PendingOperation = FImpl::EOperationUponAuth::Load;
		return;
	}
	UpdateAfterLoadingUIEvent();
}

#if WITH_EDITOR

void AITwinIModel::PostEditChangeProperty(struct FPropertyChangedEvent& e)
{
	UE_LOG(LogITwin, Display, TEXT("AITwinIModel::PostEditChangeProperty()"));
	Super::PostEditChangeProperty(e);

	FName const PropertyName = (e.Property != nullptr) ? e.Property->GetFName() : NAME_None;
	FName const MemberPropertyName = (e.MemberProperty != nullptr) ? e.MemberProperty->GetFName() : NAME_None;
	if (   PropertyName == GET_MEMBER_NAME_CHECKED(AITwinIModel, IModelId)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(AITwinIModel, ChangesetId)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(AITwinIModel, ExportId))
	{
		OnLoadingUIEvent();
	}
	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(AITwinIModel, CustomMaterials)
		&& PropertyName == GET_MEMBER_NAME_CHECKED(FITwinCustomMaterial, Material))
	{
		// The user may have set a custom material: re-tune the model if needed.
		// There will be a delay before the new material appears, as the whole tileset will be reloaded.
		SplitGltfModelForCustomMaterials();
	}
}

#endif // WITH_EDITOR


void AITwinIModel::PostLoad()
{
	Super::PostLoad();

	// If the loaded iModel uses custom materials, notify the tuner so that it splits the model accordingly
	SplitGltfModelForCustomMaterials();

	// just in case the loaded level contains an iModel already configured...
	if ((LoadingMethod == ELoadingMethod::LM_Manual && !ExportId.IsEmpty())
		||
		(LoadingMethod == ELoadingMethod::LM_Automatic && !IModelId.IsEmpty() && !ChangesetId.IsEmpty()))
	{
		// Exception: if the user has replaced the cesium URL by a local one, do not reload the tileset
		// (this is mostly used for debugging...)
		if (!ITwin::HasTilesetWithLocalURL(*this))
		{
			OnLoadingUIEvent();
		}
	}
}

void FITwinIModelInternals::OnElementsTimelineModified(FITwinElementTimeline& ModifiedTimeline,
	std::vector<ITwinElementID> const* OnlyForElements/*= nullptr*/)
{
	UITwinSynchro4DSchedules* Schedules = Owner.FindComponentByClass<UITwinSynchro4DSchedules>();
	if (!Schedules)
		return;
	GetInternals(*Schedules).Timeline().OnElementsTimelineModified(ModifiedTimeline);
	for (auto& SceneTile : SceneMapping.KnownTiles)
	{
		SceneMapping.OnElementsTimelineModified(SceneTile.first, SceneTile.second,
												ModifiedTimeline, OnlyForElements);
	}
}

bool FITwinIModelInternals::OnClickedElement(ITwinElementID const Element,
											 FHitResult const& HitResult)
{
	if (!SelectElement(Element))
		return false; // filtered out internally, most likely Element is masked out

	FBox const& BBox = SceneMapping.GetBoundingBox(Element);
	UE_LOG(LogITwin, Display, TEXT("ElementID 0x%I64x found in iModel %s with BBox %s centered on %s"),
		Element.value(), *Owner.GetActorNameOrLabel(), *BBox.ToString(), *BBox.GetCenter().ToString());

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
			UE_LOG(LogITwin, Display, TEXT("Owning Tile: %s"), *TileIdString);
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

	Owner.GetMutableWebServices()->GetElementProperties(Owner.ITwinId, Owner.IModelId, Owner.GetSelectedChangeset(),
		FString::Printf(TEXT("0x%I64x"), Element.value()));

	UITwinSynchro4DSchedules* Schedules = Owner.FindComponentByClass<UITwinSynchro4DSchedules>();
	if (!Schedules)
		return true;
	FString const ElementTimelineDescription = GetInternals(*Schedules).ElementTimelineAsString(Element);
	if (ElementTimelineDescription.IsEmpty())
		return true;
	UE_LOG(LogITwin, Display, TEXT("ElementID 0x%I64x has a timeline:\n%s"),
			Element.value(), *ElementTimelineDescription);
	return true;
}

bool FITwinIModelInternals::SelectElement(ITwinElementID const InElementID)
{
	return SceneMapping.SelectElement(InElementID, Owner.GetWorld());
}

ITwinElementID FITwinIModelInternals::GetSelectedElement() const
{
	return SceneMapping.GetSelectedElement();
}

namespace ITwin::Timeline
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
		Element = ITwin::ParseElementID(Args[0]);
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

// Console command to create a new saved view
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinAddSavedView(
	TEXT("cmd.ITwin_AddSavedView"),
	TEXT("Create a new ITwin SavedView for all iModels in the scene, using current point of view."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	FString SavedViewName;
	if (!Args.IsEmpty())
	{
		SavedViewName = Args[0];
		SavedViewName.TrimQuotesInline(); // would yield an invalid string in json
		SavedViewName.TrimCharInline(TEXT('\''), nullptr); // single quotes probably unwanted too
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("A name is required to create a new SavedView"));
		return;
	}
	for (TActorIterator<AITwinIModel> IModelIter(World); IModelIter; ++IModelIter)
	{
		(*IModelIter)->AddSavedView(SavedViewName);
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
	ITwin::Timeline::fProbaOfOpacityAnimation = fProbaOfOpacityAnimation;
}));

namespace ITwin
{
	void ZoomOnIModelsOrElement(ITwinElementID const ElementID, UWorld* World)
	{
		FBox FocusedBBox(ForceInitToZero);
		if (ITwin::NOT_ELEMENT == ElementID)
		{
			for (TActorIterator<AITwinIModel> IModelIter(World); IModelIter; ++IModelIter)
			{
				FBox const& IModelBBox =
					GetInternals(**IModelIter).SceneMapping.GetIModelBoundingBox(EITwinCoordSystem::UE);
				if (IModelBBox.IsValid)
				{
					if (FocusedBBox.IsValid) FocusedBBox += IModelBBox;
					else FocusedBBox = IModelBBox;
				}
			}
		}
		else
		{
			for (TActorIterator<AITwinIModel> IModelIter(World); IModelIter; ++IModelIter)
			{
				FBox const& IModelBBox = GetInternals(**IModelIter).SceneMapping.GetBoundingBox(ElementID);
				if (IModelBBox.IsValid)
				{
					if (FocusedBBox.IsValid) FocusedBBox += IModelBBox;
					else FocusedBBox = IModelBBox;
					break;
				}
			}
		}
		// When zooming on an Element, we want to go closer than 100 meters
		double const MinCamDist = ITwin::NOT_ELEMENT == ElementID ? 10000. : 500.;
		AITwinIModel::FImpl::ZoomOn(FocusedBBox, World, MinCamDist);
	}
}

// Console command equivalent to ITwinTestApp's ATopMenu::ZoomOnIModel, useful elsewhere, except that here
// we check all iModels in the current world
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinFitIModelInView(
	TEXT("cmd.ITwinFitIModelInView"),
	TEXT("Move the viewport pawn so that all iModels are visible in the viewport (or the specified Element only, when passed as argument)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	ITwinElementID const ElementID = Args.IsEmpty() ? ITwin::NOT_ELEMENT : ITwin::ParseElementID(Args[0]);
	ITwin::ZoomOnIModelsOrElement(ElementID, World);
}));

// Console command to zoom on the first iModel's selected element, if any.
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinZoomOnSelectedElement(
	TEXT("cmd.ITwinZoomOnSelectedElement"),
	TEXT("Move the viewport pawn close to the first selected Element, if any."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	ITwinElementID SelectedElement = ITwin::NOT_ELEMENT;
	for (TActorIterator<AITwinIModel> IModelIter(World); IModelIter; ++IModelIter)
	{
		SelectedElement = GetInternals(**IModelIter).GetSelectedElement();
		if (SelectedElement != ITwin::NOT_ELEMENT)
			break;
	}
	if (SelectedElement != ITwin::NOT_ELEMENT)
	{
		ITwin::ZoomOnIModelsOrElement(SelectedElement, World);
	}
}));

// Console command to create the Schedules components
static FAutoConsoleCommandWithWorld FCmd_ITwinSetupIModelSchedules(
	TEXT("cmd.ITwinSetupIModelSchedules"),
	TEXT("Creates a 4D Schedules component for each iModel."),
	FConsoleCommandWithWorldDelegate::CreateStatic(AITwinIModel::FImpl::CreateMissingSynchro4DSchedules));

// Console command to create the Schedules components
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinSynchro4DDebugElement(
	TEXT("cmd.ITwinSynchro4DDebugElement"),
	TEXT("Creates a 4D Schedules component for each iModel as well as a dummy animation for each Element, or for the Element passed as argument."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(AITwinIModel::FImpl::InternalSynchro4DDebugElement));

// Console command to refresh an iModel's tileset, ensuring the scene mapping will be fully reconstructed
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinRefreshIModelTileset(
	TEXT("cmd.ITwinRefreshIModelTileset"),
	TEXT("Refresh all iModel tilesets."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	for (TActorIterator<AITwinIModel> IModelIter(World); IModelIter; ++IModelIter)
	{
		(*IModelIter)->RefreshTileset();
	}
}));

#endif // ENABLE_DRAW_DEBUG
