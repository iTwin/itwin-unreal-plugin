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
#include <ITwinIModelSettings.h>
#include <ITwinDigitalTwin.h>
#include <ITwinGeoLocation.h>
#include <ITwinSavedView.h>
#include <ITwinSceneMappingBuilder.h>
#include <ITwinServerConnection.h>
#include <ITwinSetupMaterials.h>
#include <ITwinSynchro4DAnimator.h>
#include <ITwinSynchro4DSchedules.h>
#include <ITwinSynchro4DSchedulesInternals.h>
#include <ITwinCesium3DTileset.h>
#include <ITwinCesium3DTilesetLoadFailureDetails.h>
#include <ITwinUtilityLibrary.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <BeUtils/Gltf/GltfTuner.h>
#include <Network/JsonQueriesCache.h>
#include <Timeline/Timeline.h>

#include <Dom/JsonObject.h>
#include <DrawDebugHelpers.h>
#include <Engine/RendererSettings.h>
#include <Engine/Texture2D.h>
#include <EngineUtils.h>
#include <GameFramework/FloatingPawnMovement.h>
#include <GameFramework/Pawn.h>
#include <GameFramework/PlayerController.h>
#include <HttpModule.h>
#include <ImageUtils.h>
#include <Interfaces/IHttpResponse.h>
#include <Kismet/KismetMathLibrary.h>
#include <Math/Box.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>
#include <JsonObjectConverter.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/BuildConfig/MaterialTuning.h>
#	include <BeHeaders/BuildConfig/AdvancedMaterialConversion.h>
#	include <BeUtils/Gltf/GltfMaterialHelper.h>
#	include <BeUtils/Gltf/GltfMaterialTuner.h>
#	include <Core/ITwinAPI/ITwinMaterial.h>
#	include <Core/ITwinAPI/ITwinTypes.h>
#	include <Core/Visualization/MaterialPersistence.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <memory>
#include <optional>
#include <unordered_set>


namespace ITwin
{
	void SetupMaterials(AITwinCesium3DTileset& Tileset, UITwinSynchro4DSchedules* SchedulesComp /*= nullptr*/)
	{
		if (!SchedulesComp)
		{
			SchedulesComp = Cast<UITwinSynchro4DSchedules>(
				UITwinSynchro4DSchedules::StaticClass()->GetDefaultObject());
		}
		if (IsValid(SchedulesComp->BaseMaterialMasked))
		{
			Tileset.SetMaterial(SchedulesComp->BaseMaterialMasked);
			if (SchedulesComp)
				SchedulesComp->BaseMaterialMasked = SchedulesComp->BaseMaterialMasked;
		}
		if (SchedulesComp->BaseMaterialTranslucent)
		{
			Tileset.SetTranslucentMaterial(SchedulesComp->BaseMaterialTranslucent);
			if (SchedulesComp)
				SchedulesComp->BaseMaterialTranslucent = SchedulesComp->BaseMaterialTranslucent;
		}
	}

	FString ToString(ITwinElementID const& Elem)
	{
		return FString::Printf(TEXT("0x%I64x"), Elem.value());
	}

	extern bool ShouldLoadDecoration(FITwinLoadInfo const& Info, UWorld const* World);
	extern void LoadDecoration(FITwinLoadInfo const& Info, UWorld* World);
	extern void SaveDecoration(FITwinLoadInfo const& Info, UWorld const* World);
}

class AITwinIModel::FImpl
{
public:
	AITwinIModel& Owner;
	/// helper to fill/update SceneMapping
	TSharedPtr<FITwinSceneMappingBuilder> SceneMappingBuilder;
	std::shared_ptr<BeUtils::GltfTuner> GltfTuner = std::make_shared<BeUtils::GltfTuner>();
	bool bHasFilledMaterialInfoFromTuner = false;
	std::unordered_set<uint64_t> MatIDsToSplit; // stored to detect the need for retuning
	std::shared_ptr<BeUtils::GltfMaterialHelper> GltfMatHelper = std::make_shared<BeUtils::GltfMaterialHelper>();
	FITwinIModelInternals Internals;
	uint32 TilesetLoadedCount = 0;
	FDelegateHandle OnTilesetLoadFailureHandle;
	std::optional<FITwinExportInfo> ExportInfoPendingLoad;

	// Some operations require to first fetch a valid server connection
	enum class EOperationUponAuth : uint8
	{
		None,
		Load,
		Update,
		LoadDecoration
	};
	EOperationUponAuth PendingOperation = EOperationUponAuth::None;
	bool bAutoStartExportIfNeeded = false;
	struct FIModelProperties
	{
		std::optional<FProjectExtents> ProjectExtents;
		std::optional<FEcefLocation> EcefLocation;
	};
	std::optional<FIModelProperties> IModelProperties; //!< Empty means not inited yet.

	enum class EElementsMetadata : uint8 {
		Hierarchy, SourceIdentifiers
	};

	FImpl(AITwinIModel& InOwner)
		: Owner(InOwner), Internals(InOwner)
	{
		if (!Owner.HasAnyFlags(RF_ClassDefaultObject))
		{
			ElementsHierarchyQuerying.emplace(InOwner, EElementsMetadata::Hierarchy);
			ElementsSourceQuerying.emplace(InOwner, EElementsMetadata::SourceIdentifiers);
		}
		// create a callback to fill our scene mapping when meshes are loaded
		SceneMappingBuilder = MakeShared<FITwinSceneMappingBuilder>(Internals.SceneMapping, Owner);

		GltfTuner->SetMaterialHelper(GltfMatHelper);
	}

	bool UseLatestChangeset() const
	{
		return (TEXT("latest") == Owner.ChangesetId.ToLower())
			|| (ELoadingMethod::LM_Manual == Owner.LoadingMethod && Owner.ChangesetId.IsEmpty());
	}

	void Update()
	{
		Owner.UpdateWebServices();

		if (!Owner.bResolvedChangesetIdValid)
		{
			if (UseLatestChangeset())
			{
				Owner.WebServices->GetiModelLatestChangeset(Owner.IModelId);
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

	void MakeTileset(std::optional<FITwinExportInfo> const& ExportInfo = {});

	static void ZoomOn(FBox const& FocusBBox, UWorld* World, double MinDistanceToCenter = 10000)
	{
		if (!ensure(World)) return;
		auto* PlayerController = World->GetFirstPlayerController();
		APawn* Pawn = PlayerController ? PlayerController->GetPawnOrSpectator() : nullptr;
		if (Pawn)
		{
			auto const BBoxLen = FocusBBox.GetSize().Length();
			Pawn->SetActorLocation(
				// "0.2" is empirical, "projectExtents" is usually quite larger than the model itself
				FocusBBox.GetCenter()
					- FMath::Max(0.2 * BBoxLen, MinDistanceToCenter)
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
					SetupSynchro4DSchedules(*AsTileset, *GetDefault<UITwinIModelSettings>());
				}
			}
		}
	}

	void SetupSynchro4DSchedules(AITwinCesium3DTileset& Tileset, UITwinIModelSettings const& Settings)
	{
		if (!Owner.Synchro4DSchedules)
			return;
		auto& S4D = *Owner.Synchro4DSchedules;
		S4D.MaxTimelineUpdateMilliseconds = Settings.Synchro4DMaxTimelineUpdateMilliseconds;
		S4D.ScheduleQueriesServerPagination = Settings.Synchro4DQueriesDefaultPagination;
		S4D.ScheduleQueriesBindingsPagination = Settings.Synchro4DQueriesBindingsPagination;
		S4D.bDisableColoring = Settings.bSynchro4DDisableColoring;
		S4D.bDisableVisibilities = Settings.bSynchro4DDisableVisibilities;
		S4D.bDisableCuttingPlanes = Settings.bSynchro4DDisableCuttingPlanes;
		S4D.bDisableTransforms = Settings.bSynchro4DDisableTransforms;
		S4D.bMaskTilesUntilFullyAnimated = Settings.bSynchro4DMaskTilesUntilFullyAnimated;
		if (!ensure(!GetDefault<URendererSettings>()->bOrderedIndependentTransparencyEnable))
		{
			// see OIT-related posts in
			// https://forums.unrealengine.com/t/ue5-gpu-crashed-or-d3d-device-removed/524297/168:
			// it could be a problem with all transparencies (and "mask opacity"), not just cutting planes!
			UE_LOG(LogITwin, Error, TEXT("bOrderedIndependentTransparencyEnable=true will crash cut planes, sorry! See if 'r.OIT.SortedPixels' is in your DefaultEngine.ini, in section [/Script/Engine.RendererSettings], if not, add it set to False (and relaunch the app or Editor).\nDISABLING ALL Cutting Planes (aka. growth simulation) in the Synchro4D schedules!"));
			S4D.bDisableCuttingPlanes = true;
		}
		Internals.SceneMapping.OnNewTileMeshBuilt = [&SchedulesInternals = GetInternals(S4D)]
			(CesiumTileID const& TileID, std::set<ITwinElementID>&& MeshElements,
			 TWeakObjectPtr<UMaterialInstanceDynamic> const& pMaterial, bool const bFirstTimeSeenTile,
			 FITwinSceneTile& SceneTile)
			{
				SchedulesInternals.OnNewTileMeshBuilt(TileID, std::move(MeshElements), pMaterial,
													  bFirstTimeSeenTile, SceneTile);
			};
		// Note: placed at the end of this method because it will trigger a refresh of the tileset, which
		// will then trigger the OnNewTileMeshBuilt set above, which is indeed what we want. This refresh
		// of the tileset happening automatically is also why we don't need to bother calling the observer
		// manually for tiles and meshes already received and displayed: the refresh does it all over again
		SetupMaterials(Tileset);
		// Now done in the main ticker function (see AITwinIModel's ctor) because we need to wait for the end
		// of the Elements metadata properties queries (Elements hierarchy + Source IDs) to make proper sense
		// of the animation bindings received.
		//GetInternals(S4D).MakeReady(); <== MakeReady used to call ResetSchedules
	}

	void SetupMaterials(AITwinCesium3DTileset& Tileset)
	{
		ITwin::SetupMaterials(Tileset, Owner.Synchro4DSchedules);
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
	/// a paginated series of HTTP RPC requests. We'll use an instance to get the the parent-child
	/// relationships, and (concurrently) another for the "Source Element ID" (aka "Identifier") information,
	/// successively.
	class FQueryElementMetadataPageByPage
	{
	public:
		enum class EState {
			NotStarted, Running, NeedRestart, Finished, StoppedOnError
		};

	private:
		AITwinIModel& Owner;
		EElementsMetadata const KindOfMetadata;
		FString const ECSQLQueryString;
		FString const BatchMsg;
		std::optional<SDK::Core::ITwinAPIRequestInfo> RequestInfo;
		FString LastCacheFolderUsed;
		FJsonQueriesCache Cache;
		ITwinHttp::FMutex Mutex;

		EState State = EState::NotStarted;
		int QueryRowStart = 0, TotalRowsParsed = 0, RetriesLeft = 0/*see DoRestart*/;
		HttpRequestID CurrentRequestID;
		static const int QueryRowCount = 50000;

		void DoRestart()
		{
			QueryRowStart = TotalRowsParsed = 0;
			RetriesLeft = 2;
			FString const CacheFolder = QueriesCache::GetCacheFolder(
				(KindOfMetadata == EElementsMetadata::Hierarchy) 
					? QueriesCache::ESubtype::ElementsHierarchies : QueriesCache::ESubtype::ElementsSourceIDs,
				Owner.ServerConnection->Environment, Owner.ITwinId, Owner.IModelId, Owner.ChangesetId);
			if (LastCacheFolderUsed != CacheFolder && ensure(!CacheFolder.IsEmpty()))
			{
				if (!Cache.Initialize(CacheFolder, Owner.ServerConnection->Environment,
									  Owner.GetActorNameOrLabel() + TEXT(" - ") + BatchMsg))
				{
					UE_LOG(LogITwin, Warning, TEXT("Something went wrong while setting up the local http cache for Elements metadata queries - cache will NOT be used!"));
				}
				LastCacheFolderUsed = CacheFolder;
			}
			// TODO_GCO: we could get count first with "SELECT COUNT(*) FROM [Table]", which would allow to
			// parallelize requests more but also reserve vectors optimally in ParseHierarchyTree (and in
			// ParseSourceElementIDs maybe too), and also implement progress reporting.
			QueryNextPage();
		}

	public:
		FQueryElementMetadataPageByPage(AITwinIModel& InOwner, EElementsMetadata const InKindOfMetadata)
			: Owner(InOwner)
			, KindOfMetadata(InKindOfMetadata)
			, ECSQLQueryString((InKindOfMetadata == EElementsMetadata::Hierarchy)
					? TEXT("SELECT ECInstanceID, SourceECInstanceID FROM bis.ElementOwnsChildElements")
					: TEXT("SELECT Element.Id, Identifier FROM bis.ExternalSourceAspect"))
			, BatchMsg((InKindOfMetadata == EElementsMetadata::Hierarchy) ? TEXT("Element parent-child pairs")
																		  : TEXT("Source Element IDs"))
			, Cache(InOwner)
		{}

		EState GetState() const { return State; }

		void Restart()
		{
			if (EState::NotStarted == State || EState::Finished == State || EState::StoppedOnError == State)
			{
				Cache.Reset(); // reinit, we may have a new changesetId for example
				UE_LOG(LogITwin, Display, TEXT("Elements metadata queries (re)starting..."));
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
			RequestInfo.emplace(Owner.WebServices->InfosToQueryIModel(Owner.ITwinId, Owner.IModelId, 
				Owner.ResolvedChangesetId, ECSQLQueryString, QueryRowStart, QueryRowCount));
			QueryRowStart += QueryRowCount;
			auto const Hit = Cache.LookUp(*RequestInfo, Mutex);
			if (Hit)
			{
				CurrentRequestID.Empty();
				OnQueryCompleted({}, true, Cache.Read(*Hit));
				// Now just return, ie read only one response per tick even if everything's in the cache.
				// The alternative, while still not blocking the game thread, would be to use a worker thread,
				// but there is no synchronization mechanism on SceneMapping to allow that yet.
			}
			else
			{
				CurrentRequestID = Owner.WebServices->QueryIModelRows(Owner.ITwinId, Owner.IModelId,
					Owner.ResolvedChangesetId, ECSQLQueryString, QueryRowStart, QueryRowCount,
					&(*RequestInfo));
			}
		}

		/// \return Whether the reply was to a request emitted by this instance of metadata requester, and was
		///			thus parsed here.
		bool OnQueryCompleted(HttpRequestID const& RequestID, bool const bSuccess,
			std::variant<FString, TSharedPtr<FJsonObject>> const& QueryResult)
		{
			bool const bFromCache = (QueryResult.index() == 1);
			if (!bFromCache && CurrentRequestID != RequestID)
			{
				return false; // we didn't emit this request
			}
			if (EState::NeedRestart == State)
			{
				UE_LOG(LogITwin, Display, TEXT("Elements metadata queries interrupted, will restart..."));
				DoRestart();
				return true;
			}
			if (!bSuccess)
			{
				if (RetriesLeft > 0)
				{
					BE_LOGW("ITwinAPI", "Total " << TCHAR_TO_UTF8(*BatchMsg)
						<< " retrieved: " << TotalRowsParsed << ", got an http error! (will retry)");
					--RetriesLeft;
					QueryRowStart -= QueryRowCount; // TODO_GCO: maybe reduce QueryRowCount too at some point?
					FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
						[this, IModel = TWeakObjectPtr<AITwinIModel>(&Owner)] (float /*Delta*/)
						{
							if (IModel.IsValid())
							{
								QueryNextPage();
							}
							return false;//single call
						}),
						(RetriesLeft == 0) ? 8. : 2.); // delay in seconds before retry
				}
				else
				{
					BE_LOGE("ITwinAPI", "Total " << TCHAR_TO_UTF8(*BatchMsg)
						<< " retrieved: " << TotalRowsParsed << ", stopping on error after 2 retries!");
					State = EState::StoppedOnError;
				}
				return true;
			}
			int RowsParsed = 0;
			TSharedPtr<FJsonObject> JsonObj;
			if (bFromCache)
			{
				JsonObj = std::get<1>(QueryResult);
			}
			else
			{
				Cache.Write(*RequestInfo, std::get<0>(QueryResult), true, Mutex);
				auto Reader = TJsonReaderFactory<TCHAR>::Create(std::get<0>(QueryResult));
				if (!FJsonSerializer::Deserialize(Reader, JsonObj))
					JsonObj.Reset();
			}
			if (JsonObj.IsValid())
			{
				TArray<TSharedPtr<FJsonValue>> const* JsonRows = nullptr;
				if (JsonObj->TryGetArrayField(TEXT("data"), JsonRows))
				{
					RowsParsed = (EElementsMetadata::Hierarchy == KindOfMetadata)
						? GetInternals(Owner).SceneMapping.ParseHierarchyTree(*JsonRows)
						: GetInternals(Owner).SceneMapping.ParseSourceElementIDs(*JsonRows);
				}
			}
			TotalRowsParsed += RowsParsed;
			if (RowsParsed > 0)
			{
				UE_LOG(LogITwin, Verbose, TEXT("%s retrieved from %s: %d, asking for more..."), *BatchMsg,
					bFromCache ? TEXT("cache") : TEXT("remote"), TotalRowsParsed);
				RetriesLeft = 2;
				QueryNextPage();
			}
			else
			{
				UE_LOG(LogITwin, Display, TEXT("Total %s retrieved from %s: %d."), *BatchMsg,
					/*likely all retrieved from same source...*/bFromCache ? TEXT("cache") : TEXT("remote"),
					TotalRowsParsed);
				State = EState::Finished;
			}
			return true;
		}

		void OnIModelEndPlay()
		{
			Cache.Uninitialize();
		}
	};
	std::optional<FQueryElementMetadataPageByPage> ElementsHierarchyQuerying;
	std::optional<FQueryElementMetadataPageByPage> ElementsSourceQuerying;

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

void AITwinIModel::FImpl::MakeTileset(std::optional<FITwinExportInfo> const& ExportInfo /*= {}*/)
{
	if (!ensure(ExportInfo || ExportInfoPendingLoad)) return;
	if (!ensure(IModelProperties)) return;

	FITwinExportInfo const CompleteInfo = ExportInfo ? (*ExportInfo) : (*ExportInfoPendingLoad);
	ExportInfoPendingLoad.reset();
	// No need to keep former versions of the tileset
	GetInternals(Owner).SceneMapping.Reset();
	Owner.DestroyTileset();

	// We need to query these metadata of iModel Elements using several "paginated" requests sent
	// successively, but we also need to support interrupting and restart queries from scratch
	// because this code path can be executed several times for an iModel, eg. upon UpdateIModel
	ElementsHierarchyQuerying->Restart();
	ElementsSourceQuerying->Restart();
	// It seems risky to NOT do a ResetSchedules here: for example, FITwinElement::AnimationKeys are
	// not set, MainTimeline::NonAnimatedDuplicates is empty, etc.
	// To avoid redownloading everything, we could just "reinterpret" the known schedule data...?
	if (Owner.Synchro4DSchedules)
		Owner.Synchro4DSchedules->ResetSchedules();

	// *before* SpawnActor otherwise Cesium will create its own default georef
	auto&& Geoloc = FITwinGeolocation::Get(*Owner.GetWorld());

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = &Owner;
	const auto Tileset = Owner.GetWorld()->SpawnActor<AITwinCesium3DTileset>(SpawnParams);
#if WITH_EDITOR
	// in manual mode, the name is usually not set at this point => adjust it now
	if (!CompleteInfo.DisplayName.IsEmpty()
		&& (Owner.GetActorLabel().StartsWith(TEXT("ITwinIModel"))
			|| Owner.GetActorLabel().StartsWith(TEXT("IModel"))))
	{
		Owner.SetActorLabel(CompleteInfo.DisplayName);
	}
	Tileset->SetActorLabel(Owner.GetActorLabel() + TEXT(" tileset"));
#endif
	Tileset->AttachToActor(&Owner, FAttachmentTransformRules::KeepRelativeTransform);
	// TODO_GCO: Necessary for picking, unless there is another method that does
	// not require the Physics data? Note that pawn collisions need to be disabled to
	// still allow navigation through meshes (see SetActorEnableCollision).
	//Tileset->SetCreatePhysicsMeshes(false);
	// connect mesh creation callback
	Tileset->SetMeshBuildCallbacks(SceneMappingBuilder);
	Tileset->SetGltfTuner(GltfTuner);
	Tileset->SetTilesetSource(EITwinTilesetSource::FromUrl);
	Tileset->SetUrl(CompleteInfo.MeshUrl);

	auto const Settings = GetDefault<UITwinIModelSettings>();
	Tileset->MaximumCachedBytes = std::max(0ULL, Settings->CesiumMaximumCachedMegaBytes * 1024 * 1024ULL);
	// Avoid unloading/reloading tiles when merely rotating the camera
	//Tileset->EnableFrustumCulling = false; // implied by SetUseLodTransitions(true)
	Tileset->SetUseLodTransitions(true);
	Tileset->LodTransitionLength = 1.f;

	if (IModelProperties->EcefLocation)
	{
		// iModel is geolocated.
		Tileset->SetGeoreference(Geoloc->GeoReference.Get());
		// If the shared georeference is not inited yet, let's initialize it according to this iModel location.
		if (Geoloc->GeoReference->GetOriginPlacement() == EITwinOriginPlacement::TrueOrigin)
		{
			Geoloc->GeoReference->SetOriginPlacement(EITwinOriginPlacement::CartographicOrigin);
			// Put georeference at the cartographic coordinates of the center of the iModel's extents.
			Geoloc->GeoReference->SetOriginEarthCenteredEarthFixed(
				UITwinUtilityLibrary::GetIModelToEcefTransform(&Owner)
					.TransformPosition(IModelProperties->ProjectExtents
						? 0.5 * (IModelProperties->ProjectExtents->Low + IModelProperties->ProjectExtents->High)
						: FVector::ZeroVector));
		}
	}
	else
	{
		// iModel is not geolocated.
		Tileset->SetGeoreference(Geoloc->LocalReference.Get());
	}
	Internals.SceneMapping.SetIModel2UnrealInfos(UITwinUtilityLibrary::GetIModelToUnrealTransform(&Owner),
		IModelProperties->ProjectExtents
			? (0.5 * (IModelProperties->ProjectExtents->Low + IModelProperties->ProjectExtents->High))
			: FVector::ZeroVector);
	if (Owner.Synchro4DSchedules)
		SetupSynchro4DSchedules(*Tileset, *Settings);
	else
		// useful when commenting out schedules component's default creation for testing?
		SetupMaterials(*Tileset);

	TilesetLoadedCount = 0;
	Tileset->OnTilesetLoaded.AddDynamic(&Owner, &AITwinIModel::OnTilesetLoaded);
	OnTilesetLoadFailureHandle = OnCesium3DTilesetLoadFailure.AddUObject(
		&Owner, &AITwinIModel::OnTilesetLoadFailure);
}

AITwinIModel::AITwinIModel()
	:Impl(MakePimpl<FImpl>(*this))
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("root")));
	Synchro4DSchedules = CreateDefaultSubobject<UITwinSynchro4DSchedules>(TEXT("Schedules"));

	// Forbid the replacement of the base material (it was introduced for debugging/investigation purposes,
	// at the end, we will only allow editing a given set of parameters).
	bCanReplaceMaterials = ITwin::HasAdvancedMaterialConversion();
	if (!HasAnyFlags(RF_ClassDefaultObject) && ITwin::HasMaterialTuning())
	{
		// As soon as material IDs are read, launch a request to RPC service to get the corresponding material
		// properties
		Impl->GltfTuner->SetMaterialInfoReadCallback(
			[this](std::vector<BeUtils::ITwinMaterialInfo> const& MaterialInfos)
		{
			// Initialize the map of customizable materials at once.
			FillMaterialInfoFromTuner();

			// Initialize persistence at low level, if any.
			if (MaterialPersistenceMngr && ensure(!IModelId.IsEmpty()))
			{
				Impl->GltfMatHelper->SetPersistenceInfo(TCHAR_TO_ANSI(*IModelId), MaterialPersistenceMngr);
			}

			// Pre-fill material slots in the material helper, and detect potential user customizations
			// (in Carrot MVP, they are stored in the decoration service).
			int NumCustomMaterials = 0;
			{
				BeUtils::GltfMaterialHelper::Lock Lock(Impl->GltfMatHelper->GetMutex());
				for (BeUtils::ITwinMaterialInfo const& MatInfo : MaterialInfos)
				{
					Impl->GltfMatHelper->CreateITwinMaterialSlot(MatInfo.id, Lock);
				}
			}
			// If material customizations were already loaded from the decoration server (this is done
			// asynchronously), detect them at once so that the 1st displayed tileset directly shows the
			// customized materials. If not, this will be done when the decoration data has finished loading,
			// which may trigger a -re-tuning, and thus some visual artifacts...
			DetectCustomizedMaterials();

			// Then launch a request to fetch all material properties.
			TArray<FString> MaterialIds;
			MaterialIds.Reserve(MaterialInfos.size());
			Algo::Transform(MaterialInfos, MaterialIds,
				[](BeUtils::ITwinMaterialInfo const& V) -> FString
			{
				return FString::Printf(TEXT("0x%I64x"), V.id);
			});
			GetMutableWebServices()->GetMaterialListProperties(ITwinId, IModelId, GetSelectedChangeset(),
				MaterialIds);
		});
	}

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		// Single ticker for all iModel updates (selection highlight, schedules queries and replay...)
		// to avoid spawning multiple ones everywhere needed.
		// Ticker _required_, using TickActor would mean working only in-game, not in the editor!
		// (see 5e882a5b690c5c553fc1ad064a98cc496902cbe0...)
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[This = TWeakObjectPtr<AITwinIModel>(this)](float Delta)
			{
				if (This.IsValid())
				{
					GetInternals(*This).SceneMapping.UpdateSelectionAndHighlightTextures();
					if (This->Synchro4DSchedules
						&& AITwinIModel::FImpl::FQueryElementMetadataPageByPage::EState::Finished
						== This->Impl->ElementsHierarchyQuerying->GetState()
						&& AITwinIModel::FImpl::FQueryElementMetadataPageByPage::EState::Finished
						== This->Impl->ElementsSourceQuerying->GetState())
					{
						This->Synchro4DSchedules->TickSchedules(Delta);
					}
					return true; // repeated tick
				}
				else return false;
			}));
	}
}

void AITwinIModel::UpdateIModel()
{
	if (IModelId.IsEmpty())
	{
		BE_LOGE("ITwinAPI", "ITwinIModel with no IModelId cannot be updated");
		return;
	}

	// If no access token has been retrieved yet, make sure we request an authentication and then process
	// the actual update.
	if (CheckServerConnection() != SDK::Core::EITwinAuthStatus::Success)
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
	// We zoom on the bounding box of the iModel, computed like this:
	// Make a box from the project extents, then convert it to Unreal space, then add the (manual) tileset transform.
	auto* const TileSet = GetTileset();
	if (!TileSet)
		return;
	if (!(Impl->IModelProperties && Impl->IModelProperties->ProjectExtents))
		return;
	FImpl::ZoomOn(FBox(Impl->IModelProperties->ProjectExtents->Low, Impl->IModelProperties->ProjectExtents->High).TransformBy(
		UITwinUtilityLibrary::GetIModelToUnrealTransform(this)*TileSet->GetTransform()), GetWorld());
}

void AITwinIModel::AdjustPawnSpeedToExtents()
{
	auto* Pawn = (GetWorld() && GetWorld()->GetFirstPlayerController())
		? GetWorld()->GetFirstPlayerController()->GetPawnOrSpectator() : nullptr;
	if (!ensure(Pawn))
		return;
	auto* MvtComp = Cast<UFloatingPawnMovement>(Pawn->GetMovementComponent());
	if (MvtComp)
	{
		float const OldSpeed = MvtComp->MaxSpeed;
		FITwinIModel3DInfo OutInfo;
		GetModel3DInfoInCoordSystem(OutInfo, EITwinCoordSystem::UE);
		// Adjust max speed to project extent (constant factors are empirical...)
		MvtComp->MaxSpeed = FVector::Distance(OutInfo.BoundingBoxMin, OutInfo.BoundingBoxMax) / 25.f;
		MvtComp->Acceleration *= 0.25 * (MvtComp->MaxSpeed / OldSpeed);
		MvtComp->Deceleration *= MvtComp->MaxSpeed / OldSpeed; // hard breaking, hold tight!
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

void AITwinIModel::GetModel3DInfoInCoordSystem(FITwinIModel3DInfo& OutInfo, EITwinCoordSystem CoordSystem) const
{
	if (Impl->IModelProperties->ProjectExtents)
	{
		if (EITwinCoordSystem::UE == CoordSystem
			&& Impl->Internals.SceneMapping.GetIModel2UnrealTransfo())
		{
			FBox const Box = FBox(Impl->IModelProperties->ProjectExtents->Low
									- Impl->IModelProperties->ProjectExtents->GlobalOrigin,
								  Impl->IModelProperties->ProjectExtents->High
									- Impl->IModelProperties->ProjectExtents->GlobalOrigin)
				.TransformBy(*Impl->Internals.SceneMapping.GetIModel2UnrealTransfo());
			OutInfo.BoundingBoxMin = Box.Min;
			OutInfo.BoundingBoxMax = Box.Max;
		}
		else
		{
			OutInfo.BoundingBoxMin = Impl->IModelProperties->ProjectExtents->Low
				- Impl->IModelProperties->ProjectExtents->GlobalOrigin;
			OutInfo.BoundingBoxMax = Impl->IModelProperties->ProjectExtents->High
				- Impl->IModelProperties->ProjectExtents->GlobalOrigin;
		}
	}
	// TODO_GCO GlobalOrigin here?
	OutInfo.ModelCenter = FVector::ZeroVector;
	if (EITwinCoordSystem::ITwin == CoordSystem)
	{
		FRotator DummyRotator;
		UITwinUtilityLibrary::GetIModelBaseFromUnrealTransform(this, FTransform(OutInfo.ModelCenter), 
															   OutInfo.ModelCenter, DummyRotator);
	}
}

void AITwinIModel::GetModel3DInfo(FITwinIModel3DInfo& Info) const
{
	// For compatibility with former 3DFT plugin, we work in the iTwin coordinate system here.
	GetModel3DInfoInCoordSystem(Info, EITwinCoordSystem::ITwin);
	// The 'ModelCenter' used in 3DFT plugin is defined as the translation of the iModel (which is an offset
	// existing in the iModel itself, and can be found in Cesium export by retrieving the translation of the
	// root tile). It was mainly (and probably only) to adjust the savedViews in the 3DFT plugin, due to the
	// way the coordinate system of the iModel was handled in the legacy 3DFT display engine.
	// This offset should *not* be done with Cesium tiles, so historically we decided to always return zero
	// here to avoid breaking savedViews totally:
	Info.ModelCenter = FVector::ZeroVector;
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
}

void AITwinIModel::SetModelLoadInfo(FITwinLoadInfo Info)
{
	ITwinId = Info.ITwinId;
	IModelId = Info.IModelId;
	ChangesetId = Info.ChangesetId;
	ExportId = Info.ExportId;
#if WITH_EDITOR
	// Use the display name of the iModel preferably
	if (!Info.IModelDisplayName.IsEmpty())
		SetActorLabel(Info.IModelDisplayName);
#endif
}

void AITwinIModel::LoadModel(FString InExportId)
{
	UpdateWebServices();
	if (WebServices && !InExportId.IsEmpty())
	{
		WebServices->GetExportInfo(InExportId);
	}
}

FITwinLoadInfo AITwinIModel::GetModelLoadInfo() const
{
	FITwinLoadInfo Info;
	Info.ITwinId = ITwinId;
	Info.IModelId = IModelId;
	Info.ChangesetId = GetSelectedChangeset();
	Info.ExportId = ExportId;
	return Info;
}

void AITwinIModel::LoadModelFromInfos(FITwinExportInfo const& ExportInfo)
{
	UpdateWebServices();
	OnExportInfoRetrieved(true, ExportInfo);
}

FString AITwinIModel::GetSelectedChangeset() const
{
	// By construction, this is generally ResolvedChangesetId - it can be empty if no export has been loaded
	// yet, *or* in the particular case of an iModel without any changeset...
	if (bResolvedChangesetIdValid)
		return ResolvedChangesetId;
	else
		return Impl->UseLatestChangeset() ? FString() : ChangesetId;
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
	if (CompleteInfo->iTwinId.IsEmpty() || CompleteInfo->Id.IsEmpty())
	{
		BE_LOGE("ITwinAPI", "Invalid export info received for iModelId \"" <<
			TCHAR_TO_UTF8(*IModelId) << "\": " << (CompleteInfo->iTwinId.IsEmpty() ? "iTwinId" : "exportId")
			<< " is empty!");
		return;
	}
	UE_LOG(LogITwin, Verbose, TEXT("Proceeding to load iTwin %s with export %s"),
							  *CompleteInfo->iTwinId, *CompleteInfo->Id);
	ExportStatus = EITwinExportStatus::Complete;
	// in Automatic mode, it is still empty and must be set here because the 4D apis require it:
	ITwinId = CompleteInfo->iTwinId;
	ExportId = CompleteInfo->Id; // informative only (needed here for  Automatic mode)
	// Start retrieving the decoration if needed (usually, it is triggered before)
	LoadDecorationIfNeeded();
	// To assign the correct georeference to the tileset, we need some properties of the iModel
	// (whether it is geolocated, its extents...), which are retrieved by a specific request.
	// At this point, it is very likely the properties have not been retrieved yet
	if (Impl->IModelProperties)
	{
		// Properties have already been retrieved.
		Impl->MakeTileset(*CompleteInfo);
	}
	else
	{
		Impl->ExportInfoPendingLoad.emplace(*CompleteInfo);
		WebServices->GetIModelProperties(ITwinId, IModelId, GetSelectedChangeset());
	}
}

void AITwinIModel::OnIModelPropertiesRetrieved(bool bSuccess, bool bHasExtents, FProjectExtents const& Extents,
	bool bHasEcefLocation, FEcefLocation const& EcefLocation) /*override*/
{
	Impl->IModelProperties.emplace();
	if (bSuccess)
	{
		if (bHasExtents || Extents.GlobalOrigin != FVector::ZeroVector)
		{
			Impl->IModelProperties->ProjectExtents = Extents;
			if (bHasExtents)
			{
				UE_LOG(LogITwin, Display,
					TEXT("iModel project extents: min=%s, max=%s, centered on %s"),
					*Impl->IModelProperties->ProjectExtents->Low.ToString(),
					*Impl->IModelProperties->ProjectExtents->High.ToString(),
					*(0.5 * (Impl->IModelProperties->ProjectExtents->Low
						   + Impl->IModelProperties->ProjectExtents->High)).ToString());
			}
			UE_LOG(LogITwin, Display, TEXT("iModel global origin: %s"),
				*Impl->IModelProperties->ProjectExtents->GlobalOrigin.ToString());
		}
		if (bHasEcefLocation)
		{
			Impl->IModelProperties->EcefLocation = EcefLocation;
			UE_LOG(LogITwin, Display, TEXT("iModel Earth origin: %s, orientation: %s"),
				*Impl->IModelProperties->EcefLocation->Origin.ToString(),
				*Impl->IModelProperties->EcefLocation->Orientation.ToString());
			if (Impl->IModelProperties->EcefLocation->bHasCartographicOrigin)
			{
				UE_LOG(LogITwin, Display, TEXT("iModel cartographic origin: lg.=%.8f lat.=%.8f H=%.2f"),
					Impl->IModelProperties->EcefLocation->CartographicOrigin.Longitude,
					Impl->IModelProperties->EcefLocation->CartographicOrigin.Latitude,
					Impl->IModelProperties->EcefLocation->CartographicOrigin.Height);
			}
		}
	}
	// Now that properties have been retrieved, we can construct the tileset.
	Impl->MakeTileset();
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
		if (!Impl->UseLatestChangeset())
			ChangesetId = ExportInfo.ChangesetId;
		SetResolvedChangesetId(ExportInfo.ChangesetId);
	}
	// Actually load the Cesium tileset if the request was successful and the export is complete
	FITwinExportInfos infos;
	infos.ExportInfos.Push(ExportInfo);
	OnExportInfosRetrieved(bSuccess, infos);

	if (!bSuccess || ExportInfo.Status == TEXT("Invalid"))
	{
		// server error, or the export may have been interrupted on the server, or deleted...
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

void AITwinIModel::OnIModelQueried(bool bSuccess, FString const& QueryResult, HttpRequestID const& RequestID)
{
	// One and only one of the two "requesters" should handle this reply
	if (!Impl->ElementsHierarchyQuerying->OnQueryCompleted(RequestID, bSuccess, QueryResult)
		&& !Impl->ElementsSourceQuerying->OnQueryCompleted(RequestID, bSuccess, QueryResult))
	{
		BE_LOGE("ITwinAPI", "iModel request ID not recognized: " << TCHAR_TO_UTF8(*RequestID));
	}
}

void AITwinIModel::OnMaterialPropertiesRetrieved(bool bSuccess, SDK::Core::ITwinMaterialPropertiesMap const& props)
{
	if (!bSuccess)
		return;
	// TODO_JDE convert ITwinMaterialProperties into something we can use to convert the iTwin material into
	// an Unreal material
	BeUtils::GltfMaterialHelper::Lock lock(Impl->GltfMatHelper->GetMutex());

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

			Impl->GltfMatHelper->SetITwinMaterialProperties(id64.value(), matProperties, lock);
		}
	}

	// Start downloading missing textures (disabled in Carrot MVP).
	if (ITwin::HasAdvancedMaterialConversion()
		&& ensure(WebServices))
	{
		// In case we need to download textures, setup a destination folder depending on current iModel
		FString TextureDir = FPlatformProcess::UserSettingsDir();
		if (!TextureDir.IsEmpty())
		{
			// TODO_JDE - Should it depend on the changeset?
			TextureDir = FPaths::Combine(TextureDir, TEXT("Bentley"), TEXT("Cache"), TEXT("Textures"), IModelId);
			Impl->GltfMatHelper->SetTextureDirectory(*TextureDir, lock);
		}

		std::vector<std::string> textureIds;
		Impl->GltfMatHelper->ListITwinTexturesToDownload(textureIds, lock);
		for (std::string const& texId : textureIds)
		{
			WebServices->GetTextureData(ITwinId, IModelId, GetSelectedChangeset(), FString(texId.c_str()));
		}
	}
}

void AITwinIModel::OnTextureDataRetrieved(bool bSuccess, std::string const& textureId, SDK::Core::ITwinTextureData const& textureData)
{
	if (!bSuccess)
		return;
	Impl->GltfMatHelper->SetITwinTextureData(textureId, textureData);
}

void AITwinIModel::Retune()
{
	++Impl->GltfTuner->currentVersion;
}

void AITwinIModel::FillMaterialInfoFromTuner()
{
	auto const Materials = Impl->GltfTuner->GetITwinMaterialInfo();
	int32 const NbMaterials = (int32)Materials.size();
	CustomMaterials.Reserve(NbMaterials);

	// FString::Printf expects a litteral, so I wont spend too much time making this code generic...
	const bool bPadd2 = NbMaterials < 100;
	const bool bPadd3 = NbMaterials >= 100 && NbMaterials < 1000;
	auto const BuildMaterialNameFromInteger = [&](int32 MaterialIndex) -> FString
	{
		if (bPadd2)
			return FString::Printf(TEXT("Material #%02d"), MaterialIndex);
		else if (bPadd3)
			return FString::Printf(TEXT("Material #%03d"), MaterialIndex);
		else
			return FString::Printf(TEXT("Material #%d"), MaterialIndex);
	};

	for (BeUtils::ITwinMaterialInfo const& MatInfo : Materials)
	{
		FITwinCustomMaterial& CustomMat = CustomMaterials.FindOrAdd(MatInfo.id);
		CustomMat.Name = UTF8_TO_TCHAR(MatInfo.name.c_str());
		// Material names usually end with a suffix in the form of ": <IMODEL_NAME>"
		// => discard this part
		int32 LastColon = UE::String::FindLast(CustomMat.Name, TEXT(":"));
		if (LastColon > 0)
		{
			CustomMat.Name.RemoveAt(LastColon, CustomMat.Name.Len() - LastColon);
		}
		else if (CustomMat.Name.IsEmpty()
			|| (CustomMat.Name.Len() >= 16 && !CustomMat.Name.Contains(TEXT(" "))))
		{
			// Sometimes (often in real projects?) the material name is just a random set of letters
			// => try to detect this case and display a default name then.
			CustomMat.Name = BuildMaterialNameFromInteger(CustomMaterials.Num());
		}
	}
	Impl->bHasFilledMaterialInfoFromTuner = true;
}



void AITwinIModel::LoadDecoration()
{
	if (ITwinId.IsEmpty() || IModelId.IsEmpty())
	{
		BE_LOGE("ITwinAPI", "ITwinID and IModelId are required to load decoration");
		return;
	}

	// If no access token has been retrieved yet, make sure we request one.
	if (CheckServerConnection() != SDK::Core::EITwinAuthStatus::Success)
	{
		Impl->PendingOperation = FImpl::EOperationUponAuth::LoadDecoration;
		return;
	}
	ITwin::LoadDecoration(GetModelLoadInfo(), GetWorld());
}

void AITwinIModel::LoadDecorationIfNeeded()
{
	if (ITwinId.IsEmpty() || IModelId.IsEmpty())
		return;
	if (CheckServerConnection(false) != SDK::Core::EITwinAuthStatus::Success)
		return;
	FITwinLoadInfo const Info = GetModelLoadInfo();
	if (ITwin::ShouldLoadDecoration(Info, GetWorld()))
	{
		ITwin::LoadDecoration(Info, GetWorld());
	}
}

void AITwinIModel::SaveDecoration()
{
	ITwin::SaveDecoration(GetModelLoadInfo(), GetWorld());
}

void AITwinIModel::DetectCustomizedMaterials()
{
	// Detect user customizations (in Carrot MVP, they are stored in the decoration service).
	int NumCustomMaterials = 0;
	BeUtils::GltfMaterialHelper::Lock Lock(Impl->GltfMatHelper->GetMutex());

	// Initialize persistence at low level, if it has not yet been done (eg. when creating the iModel
	// manually in Editor, and clicking LoadDecoration...).
	if (MaterialPersistenceMngr
		&& !Impl->GltfMatHelper->HasPersistenceInfo()
		&& ensure(!IModelId.IsEmpty()))
	{
		Impl->GltfMatHelper->SetPersistenceInfo(TCHAR_TO_ANSI(*IModelId), MaterialPersistenceMngr);
	}

	// See if some material definitions can be loaded from the decoration service.
	size_t LoadedSettings = Impl->GltfMatHelper->LoadMaterialCustomizations(Lock);
	if (LoadedSettings == 0)
	{
		return;
	}

	for (auto& [MatID, CustomMat] : CustomMaterials)
	{
		if (Impl->GltfMatHelper->HasCustomDefinition(MatID, Lock))
		{
			// If the material uses custom settings, activate advanced conversion so that the tuning
			// can handle it.
			if (!CustomMat.bAdvancedConversion)
			{
				CustomMat.bAdvancedConversion = true;
				NumCustomMaterials++;
			}
		}
	}
	if (NumCustomMaterials > 0)
	{
		// Request a different gltf tuning.
		SplitGltfModelForCustomMaterials();
	}
}

void AITwinIModel::ReloadCustomizedMaterials()
{
	int NumCustomMaterials = 0;
	BeUtils::GltfMaterialHelper::Lock Lock(Impl->GltfMatHelper->GetMutex());
	size_t LoadedSettings = Impl->GltfMatHelper->LoadMaterialCustomizations(Lock, true);
	for (auto& [MatID, CustomMat] : CustomMaterials)
	{
		CustomMat.bAdvancedConversion = Impl->GltfMatHelper->HasCustomDefinition(MatID, Lock);
	}
	SplitGltfModelForCustomMaterials();
	RefreshTileset();
}

void AITwinIModel::SplitGltfModelForCustomMaterials(bool bForceRetune /*= false*/)
{
	std::unordered_set<uint64_t> MatIDsToSplit;
	for (auto const& [MatID, CustomMat] : CustomMaterials)
	{
		if (CustomMat.bAdvancedConversion || CustomMat.Material.Get())
		{
			MatIDsToSplit.insert(MatID);
		}
	}
	if (MatIDsToSplit != Impl->MatIDsToSplit || bForceRetune)
	{
		Impl->MatIDsToSplit = MatIDsToSplit;

		BeUtils::GltfTuner::Rules rules;
		rules.itwinMatIDsToSplit_.swap(MatIDsToSplit);
		Impl->GltfTuner->SetRules(std::move(rules));

		Retune();
	}
}

TMap<uint64, FString> AITwinIModel::GetITwinMaterialMap() const
{
	TMap<uint64, FString> itwinMats;
	itwinMats.Reserve(CustomMaterials.Num());
	for (auto const& [MatID, CustomMat] : CustomMaterials)
	{
		itwinMats.Emplace(MatID, CustomMat.Name);
	}
	return itwinMats;
}

FString AITwinIModel::GetMaterialName(uint64_t MaterialId) const
{
	FITwinCustomMaterial const* Mat = CustomMaterials.Find(MaterialId);
	if (Mat)
		return Mat->Name;
	else
		return {};
}

double AITwinIModel::GetMaterialChannelIntensity(uint64_t MaterialId, SDK::Core::EChannelType Channel) const
{
	return Impl->GltfMatHelper->GetChannelIntensity(MaterialId, Channel);
}

namespace
{
	template <typename ParamType>
	struct MaterialParamHelperBase
	{
		using ParameterType = ParamType;

		BeUtils::GltfMaterialHelper& GltfMatHelper;
		SDK::Core::EChannelType const Channel;
		ParamType const NewValue;

		MaterialParamHelperBase(BeUtils::GltfMaterialHelper& GltfHelper,
								SDK::Core::EChannelType InChannel,
								ParamType const& NewParamValue)
			: GltfMatHelper(GltfHelper)
			, Channel(InChannel)
			, NewValue(NewParamValue)
		{}
	};

	class MaterialIntensityHelper : public MaterialParamHelperBase<double>
	{
		using Super = MaterialParamHelperBase<double>;

	public:
		using ParamType = double;

		MaterialIntensityHelper(BeUtils::GltfMaterialHelper& GltfHelper,
								SDK::Core::EChannelType InChannel,
								double NewIntensity)
			: MaterialParamHelperBase(GltfHelper, InChannel, NewIntensity)
		{}

		ParamType GetCurrentValue(uint64_t MaterialId) const
		{
			return GltfMatHelper.GetChannelIntensity(MaterialId, this->Channel);
		}

		void SetNewValue(uint64_t MaterialId, bool& bModifiedValue) const
		{
			GltfMatHelper.SetChannelIntensity(MaterialId, this->Channel, this->NewValue, bModifiedValue);
		}

		void ApplyNewValueToScene(uint64_t MaterialId, FITwinSceneMapping& SceneMapping) const
		{
			SceneMapping.SetITwinMaterialChannelIntensity(MaterialId, this->Channel, this->NewValue);
		}

		bool NeedSwitchTranslucency(const double CurrentIntensity) const
		{
			double const CurrentTransparency = (Channel == SDK::Core::EChannelType::Transparency)
				? CurrentIntensity
				: (1. - CurrentIntensity);
			double const NewTransparency = (Channel == SDK::Core::EChannelType::Transparency)
				? this->NewValue
				: (1. - this->NewValue);

			bool const bCurrentTranslucent = (std::fabs(CurrentTransparency) > 1e-5);
			bool const bNewTranslucent = (std::fabs(NewTransparency) > 1e-5);
			return bCurrentTranslucent != bNewTranslucent;
		}
	};

	class MaterialMapParamHelper : public MaterialParamHelperBase<SDK::Core::ITwinChannelMap>
	{
		using Super = MaterialParamHelperBase<SDK::Core::ITwinChannelMap>;

	public:
		using ParamType = SDK::Core::ITwinChannelMap;

		MaterialMapParamHelper(std::shared_ptr<BeUtils::GltfMaterialHelper> const& GltfHelperPtr,
			SDK::Core::EChannelType InChannel,
			SDK::Core::ITwinChannelMap const& NewMap)
			: Super(*GltfHelperPtr, InChannel, NewMap)
			, MatHelperPtr(GltfHelperPtr)
			, NewTexPath(NewMap.texture)
		{

		}

		bool BuildMergedTexture(uint64_t MaterialId) const
		{
			if (!NewTexPath.empty())
			{
				BeUtils::GltfMaterialHelper::Lock Lock(GltfMatHelper.GetMutex());
				// Some channels require to be merged together (color+alpha), (metallic+roughness) or
				// formatted to use a given R,G,B,A component
				// => handle those cases here or else we would lose some information.
				if (this->Channel == SDK::Core::EChannelType::Alpha
					|| (this->Channel == SDK::Core::EChannelType::Color && GltfMatHelper.HasChannelMap(MaterialId, SDK::Core::EChannelType::Alpha, Lock)))
				{
					BeUtils::GltfMaterialTuner MatTuner(MatHelperPtr);
					auto const glTFTexturePath = MatTuner.MergeColorAlpha(
						GltfMatHelper.GetChannelColorMap(MaterialId, SDK::Core::EChannelType::Color, Lock).texture,
						GltfMatHelper.GetChannelIntensityMap(MaterialId, SDK::Core::EChannelType::Alpha, Lock).texture,
						this->bNeedTranslucentMat,
						Lock);
					if (!glTFTexturePath.empty())
					{
						NewTexPath = glTFTexturePath.generic_string();
						return true;
					}
				}
			}
			return false;
		}

		void ApplyNewValueToScene(uint64_t MaterialId, FITwinSceneMapping& SceneMapping) const
		{
			UTexture2D* NewTexture = nullptr;
			if (!NewTexPath.empty())
			{
				NewTexture = FImageUtils::ImportFileAsTexture2D(UTF8_TO_TCHAR(NewTexPath.c_str()));
			}
			SceneMapping.SetITwinMaterialChannelTexture(MaterialId, this->Channel, NewTexture);
		}

	protected:
		std::shared_ptr<BeUtils::GltfMaterialHelper> const& MatHelperPtr;
		mutable bool bNeedTranslucentMat = false;
		mutable std::string NewTexPath;
	};

	class MaterialIntensityMapHelper : public MaterialMapParamHelper
	{
		using Super = MaterialMapParamHelper;

	public:
		MaterialIntensityMapHelper(std::shared_ptr<BeUtils::GltfMaterialHelper> const& GltfHelperPtr,
			SDK::Core::EChannelType InChannel,
			SDK::Core::ITwinChannelMap const& NewMap)
			: Super(GltfHelperPtr, InChannel, NewMap)
		{}

		ParamType GetCurrentValue(uint64_t MaterialId) const
		{
			return GltfMatHelper.GetChannelIntensityMap(MaterialId, this->Channel);
		}

		void SetNewValue(uint64_t MaterialId, bool& bModifiedValue) const
		{
			// Before changing the alpha map, retrieve the current alpha mode, if any.
			{
				BeUtils::GltfMaterialHelper::Lock Lock(GltfMatHelper.GetMutex());
				GltfMatHelper.GetCurrentAlphaMode(MaterialId, CurrentAlphaMode, Lock);
			}

			GltfMatHelper.SetChannelIntensityMap(MaterialId, this->Channel, this->NewValue, bModifiedValue);

			BuildMergedTexture(MaterialId);
		}

		bool NeedSwitchTranslucency(ParamType const& /*CurrentMap*/) const
		{
			// If the user changes the opacity map but the previous one was already requiring translucency,
			// there is no need to re-tune. Therefore we compare with the current alpha mode (if the latter
			// is unknown, it means we have never customized this material before, and thus we will have to
			// do it now...
			return bNeedTranslucentMat != (CurrentAlphaMode == CesiumGltf::Material::AlphaMode::BLEND);
		}

	private:
		mutable std::string CurrentAlphaMode;
	};

	class MaterialColorHelper : public MaterialParamHelperBase<SDK::Core::ITwinColor>
	{
		using ITwinColor = SDK::Core::ITwinColor;
		using Super = MaterialParamHelperBase<ITwinColor>;

	public:
		using ParamType = ITwinColor;


		MaterialColorHelper(BeUtils::GltfMaterialHelper& GltfHelper,
							SDK::Core::EChannelType InChannel,
							ITwinColor const& NewColor)
			: MaterialParamHelperBase(GltfHelper, InChannel, NewColor)
		{}

		ParamType GetCurrentValue(uint64_t MaterialId) const
		{
			return GltfMatHelper.GetChannelColor(MaterialId, this->Channel);
		}

		void SetNewValue(uint64_t MaterialId, bool& bModifiedValue) const
		{
			GltfMatHelper.SetChannelColor(MaterialId, this->Channel, this->NewValue, bModifiedValue);
		}

		void ApplyNewValueToScene(uint64_t MaterialId, FITwinSceneMapping& SceneMapping) const
		{
			SceneMapping.SetITwinMaterialChannelColor(
				MaterialId, this->Channel, this->NewValue);
		}

		bool NeedSwitchTranslucency(ParamType const& /*CurrentIntensity*/) const
		{
			// Changing the color of transparency/opacity just makes no sense!
			ensureMsgf(false, TEXT("invalid combination (color vs opacity)"));
			return false;
		}
	};

	class MaterialColorMapHelper : public MaterialMapParamHelper
	{
		using Super = MaterialMapParamHelper;

	public:
		MaterialColorMapHelper(std::shared_ptr<BeUtils::GltfMaterialHelper> const& GltfHelperPtr,
			SDK::Core::EChannelType InChannel,
			SDK::Core::ITwinChannelMap const& NewMap)
			: Super(GltfHelperPtr, InChannel, NewMap)
		{}

		ParamType GetCurrentValue(uint64_t MaterialId) const
		{
			return GltfMatHelper.GetChannelColorMap(MaterialId, this->Channel);
		}

		void SetNewValue(uint64_t MaterialId, bool& bModifiedValue) const
		{
			GltfMatHelper.SetChannelColorMap(MaterialId, this->Channel, this->NewValue, bModifiedValue);

			BuildMergedTexture(MaterialId);
		}

		bool NeedSwitchTranslucency(ParamType const& /*CurrentMap*/) const
		{
			// Changing the color of transparency/opacity just makes no sense!
			ensureMsgf(false, TEXT("invalid combination (color vs opacity)"));
			return false;
		}
	};
}

template <typename MaterialParamHelper>
void AITwinIModel::TSetMaterialChannelParam(MaterialParamHelper const& Helper, uint64_t MaterialId)
{
	using ParameterType = typename MaterialParamHelper::ParamType;
	FITwinCustomMaterial* CustomMat = CustomMaterials.Find(MaterialId);
	if (!ensureMsgf(CustomMat, TEXT("unknown material ID")))
	{
		return;
	}

	SDK::Core::EChannelType const Channel(Helper.Channel);
	bool const bTestTranslucencySwitch = (Channel == SDK::Core::EChannelType::Transparency
		|| Channel == SDK::Core::EChannelType::Alpha);
	std::optional<ParameterType> CurrentValueOpt;
	if (bTestTranslucencySwitch)
	{
		// Store initial value before changing it (see test below).
		CurrentValueOpt = Helper.GetCurrentValue(MaterialId);
	}

	bool bModifiedValue(false);
	Helper.SetNewValue(MaterialId, bModifiedValue);
	if (!bModifiedValue)
	{
		// Avoid useless glTF splitting! (this method is called when selecting a material in the panel, with
		// initial value...)
		return;
	}

	// If this is the first time we edit this material, we will have to request a new glTF tuning.
	bool bNeedGltfTuning = !CustomMat->bAdvancedConversion;
	CustomMat->bAdvancedConversion = true;

	// Special case for transparency/alpha: may require we change the base material (translucent or not)
	if (bTestTranslucencySwitch)
	{
		if (Helper.NeedSwitchTranslucency(*CurrentValueOpt))
		{
			// The base material itself will have to change (from opaque to translucent or vice versa)
			// => trigger a retuning (I am not sure if it is safe to change the base material of a material
			// instance in the middle of the lifetime of a mesh...)
			bNeedGltfTuning = true;
		}
	}

	// Make sure the new value is applied to the Unreal mesh
	if (bNeedGltfTuning)
	{
		// The whole tileset will be reloaded with updated materials.
		// Here we enforce a Retune in all cases, because of the potential switch opaque/translucent.
		SplitGltfModelForCustomMaterials(true);
	}
	else
	{
		// No need to rebuild the full tileset. Instead, change the corresponding parameter in the
		// Unreal material instance, using the mapping.
		Helper.ApplyNewValueToScene(MaterialId, GetInternals(*this).SceneMapping);
	}
}

void AITwinIModel::SetMaterialChannelIntensity(uint64_t MaterialId, SDK::Core::EChannelType Channel, double Intensity)
{
	MaterialIntensityHelper Helper(*Impl->GltfMatHelper, Channel, Intensity);
	TSetMaterialChannelParam(Helper, MaterialId);
}

FLinearColor AITwinIModel::GetMaterialChannelColor(uint64_t MaterialId, SDK::Core::EChannelType Channel) const
{
	auto const Color = Impl->GltfMatHelper->GetChannelColor(MaterialId, Channel);
	return { (float)Color[0], (float)Color[1], (float)Color[2], (float)Color[3] };
}

void AITwinIModel::SetMaterialChannelColor(uint64_t MaterialId, SDK::Core::EChannelType Channel, FLinearColor const& Color)
{
	SDK::Core::ITwinColor NewColor;
	NewColor[0] = Color.R;
	NewColor[1] = Color.G;
	NewColor[2] = Color.B;
	NewColor[3] = Color.A;
	MaterialColorHelper Helper(*Impl->GltfMatHelper, Channel, NewColor);
	TSetMaterialChannelParam(Helper, MaterialId);
}

FString AITwinIModel::GetMaterialChannelColorTextureID(uint64_t MaterialId, SDK::Core::EChannelType Channel) const
{
	return UTF8_TO_TCHAR(Impl->GltfMatHelper->GetChannelColorMap(MaterialId, Channel).texture.c_str());
}

FString AITwinIModel::GetMaterialChannelIntensityTextureID(uint64_t MaterialId, SDK::Core::EChannelType Channel) const
{
	return UTF8_TO_TCHAR(Impl->GltfMatHelper->GetChannelIntensityMap(MaterialId, Channel).texture.c_str());
}

FString AITwinIModel::GetMaterialChannelTextureID(uint64_t MaterialId, SDK::Core::EChannelType Channel) const
{
	// Distinguish color from intensity textures.
	if (Channel == SDK::Core::EChannelType::Color
		|| Channel == SDK::Core::EChannelType::Normal)
	{
		return GetMaterialChannelColorTextureID(MaterialId, Channel);
	}
	else
	{
		// For other channels, the map defines an intensity
		return GetMaterialChannelIntensityTextureID(MaterialId, Channel);
	}
}

void AITwinIModel::SetMaterialChannelColorTextureID(uint64_t MaterialId, SDK::Core::EChannelType Channel,
	FString const& TextureId)
{
	SDK::Core::ITwinChannelMap NewMap;
	NewMap.texture = TCHAR_TO_UTF8(*TextureId);
	MaterialColorMapHelper Helper(Impl->GltfMatHelper, Channel, NewMap);
	TSetMaterialChannelParam(Helper, MaterialId);
}

void AITwinIModel::SetMaterialChannelIntensityTextureID(uint64_t MaterialId, SDK::Core::EChannelType Channel,
	FString const& TextureId)
{
	SDK::Core::ITwinChannelMap NewMap;
	NewMap.texture = TCHAR_TO_UTF8(*TextureId);
	MaterialIntensityMapHelper Helper(Impl->GltfMatHelper, Channel, NewMap);
	TSetMaterialChannelParam(Helper, MaterialId);
}

void AITwinIModel::SetMaterialChannelTextureID(uint64_t MaterialId, SDK::Core::EChannelType Channel, FString const& TextureId)
{
	// Distinguish color from intensity textures.
	if (Channel == SDK::Core::EChannelType::Color
		|| Channel == SDK::Core::EChannelType::Normal)
	{
		SetMaterialChannelColorTextureID(MaterialId, Channel, TextureId);
	}
	else
	{
		// For other channels, the map defines an intensity
		SetMaterialChannelIntensityTextureID(MaterialId, Channel, TextureId);
	}
}

std::shared_ptr<BeUtils::GltfMaterialHelper> const& AITwinIModel::GetGltfMaterialHelper() const
{
	return Impl->GltfMatHelper;
}

/*** Material persistence (Carrot Application only for now) ***/

/*static*/
AITwinIModel::MaterialPersistencePtr AITwinIModel::MaterialPersistenceMngr;

/*static*/
void AITwinIModel::SetMaterialPersistenceManager(MaterialPersistencePtr const& Mngr)
{
	MaterialPersistenceMngr = Mngr;
}

/*static*/
AITwinIModel::MaterialPersistencePtr const& AITwinIModel::GetMaterialPersistenceManager()
{
	return MaterialPersistenceMngr;
}

void AITwinIModel::SetModelOffset(FVector Pos, FVector Rot, EOffsetContext Context /*= UserEdition*/)
{
	SetActorLocationAndRotation(Pos, FQuat::MakeFromEuler(Rot));
	if (Context == EOffsetContext::Reload)
	{
		// If the tileset has already been created (which can happen since decoration loading is
		// asynchronous), let's enforce its refresh, to avoid huge issues with bounding boxes
		// computed for 4D...
		if (GetTileset())
		{
			RefreshTileset();
		}
	}
	else
	{
		// From Object Properties panel => notify the persistence manager...
		if (MaterialPersistenceMngr && !IModelId.IsEmpty())
		{
			MaterialPersistenceMngr->SetModelOffset(
				TCHAR_TO_UTF8(*IModelId), { Pos.X, Pos.Y, Pos.Z }, { Rot.X, Rot.Y, Rot.Z });
		}
	}
}


void AITwinIModel::StartExport()
{
	if (IModelId.IsEmpty())
	{
		BE_LOGE("ITwinAPI", "IModelId is required to start an export");
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
	return (Impl->IModelProperties && Impl->IModelProperties->ProjectExtents
			&& Impl->IModelProperties->ProjectExtents->High != Impl->IModelProperties->ProjectExtents->Low)
		? &*Impl->IModelProperties->ProjectExtents : nullptr;
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

void AITwinIModel::HideTileset(bool bHide)
{
	for (auto& Child : Children)
		if (auto* Tileset = Cast<AITwinCesium3DTileset>(Child.Get()))
			Tileset->SetActorHiddenInGame(bHide);
}

void AITwinIModel::SetMaximumScreenSpaceError(double InMaximumScreenSpaceError)
{
	for (auto& Child : Children)
		if (auto* Tileset = Cast<AITwinCesium3DTileset>(Child.Get()))
			Tileset->SetMaximumScreenSpaceError(InMaximumScreenSpaceError);
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

void AITwinIModel::AddSavedView(const FString& displayName)
{
	if (IModelId.IsEmpty())
	{
		BE_LOGE("ITwinAPI", "IModelId is required to create a new SavedView");
		return;
	}
	if (ITwinId.IsEmpty())
	{
		BE_LOGE("ITwinAPI", "ITwinId is required to create a new SavedView");
		return;
	}

	FSavedView NewSavedView;
	if (!UITwinUtilityLibrary::GetSavedViewFromPlayerController(this, NewSavedView))
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
	if (LoadingMethod == ELoadingMethod::LM_Manual)
	{
		if (ExportId.IsEmpty())
		{
			UpdateIModel();
		}
		else
		{
			DestroyTileset();
			LoadModel(ExportId);
		}
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
	case FImpl::EOperationUponAuth::LoadDecoration:
		LoadDecoration();
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
	if (CheckServerConnection() != SDK::Core::EITwinAuthStatus::Success)
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
		&&
		(   PropertyName == GET_MEMBER_NAME_CHECKED(FITwinCustomMaterial, Material)
		||	PropertyName == GET_MEMBER_NAME_CHECKED(FITwinCustomMaterial, bAdvancedConversion)))
	{
		// The user may have set a custom material: re-tune the model if needed.
		// There will be a delay before the new material appears, as the whole tileset will be reloaded.
		SplitGltfModelForCustomMaterials();
	}
}

#endif // WITH_EDITOR

bool FITwinIModelInternals::IsElementHiddenInSavedView(ITwinElementID const Element) const
{
	for (const auto& [TileID, SceneTile] : SceneMapping.KnownTiles)
	{
		return SceneTile.IsElementHiddenInSavedView(Element);
	}
	return false;
}

void AITwinIModel::PostLoad()
{
	Super::PostLoad();

	// If the loaded iModel uses custom materials, notify the tuner so that it splits the model accordingly
	SplitGltfModelForCustomMaterials();

	// need to fetch a new changesetId if we have saved one but user asks to always use latest
	if (Impl->UseLatestChangeset())
	{
		bResolvedChangesetIdValid = false;
		ExportId = FString();
	}

	// When the loaded level contains an iModel already configured, it is imperative to call UpdateIModel
	// for various reasons: to get a new URL for the tileset, with a valid signature ("sig" parameter, part
	// of the URL), to trigger the download of Elements metadata (see ElementMetadataQuerying), which in
	// turn will enable and trigger the download of Synchro4 schedules, etc.
	if (!IModelId.IsEmpty())
	{
		// Exception: if the user has replaced the cesium URL by a local one, do not reload the tileset
		// (this is mostly used for debugging...)
		if (!ITwin::HasTilesetWithLocalURL(*this))
		{
			OnLoadingUIEvent();
		}
	}
}

void AITwinIModel::EndPlay(const EEndPlayReason::Type EndPlayReason) /*override*/
{
	if (Impl->ElementsHierarchyQuerying)
		Impl->ElementsHierarchyQuerying->OnIModelEndPlay();
	if (Impl->ElementsSourceQuerying)
		Impl->ElementsSourceQuerying->OnIModelEndPlay();
	if (Synchro4DSchedules)
		Synchro4DSchedules->OnIModelEndPlay();
	Super::EndPlay(EndPlayReason);
}

void FITwinIModelInternals::OnElementsTimelineModified(FITwinElementTimeline& ModifiedTimeline,
	std::vector<ITwinElementID> const* OnlyForElements/*= nullptr*/)
{
	UITwinSynchro4DSchedules* Schedules = Owner.FindComponentByClass<UITwinSynchro4DSchedules>();
	if (!Schedules)
		return;
	auto&& SchedInternals = GetInternals(*Schedules);
	SchedInternals.Timeline().OnElementsTimelineModified(ModifiedTimeline);
	// In practice, we no longer enter this section now: the whole schedule is pre-fetched and, before it is
	// fully received, the test below skips the KnownTiles loop and, when it's full, the function is never
	// called again.
	if (Schedules->IsAvailable())
	{
		for (auto& SceneTile : SceneMapping.KnownTiles)
		{
			SceneMapping.OnElementsTimelineModified(SceneTile.first, SceneTile.second,
													ModifiedTimeline, OnlyForElements);
		}
	}
}

bool FITwinIModelInternals::OnClickedElement(ITwinElementID const Element,
											 FHitResult const& HitResult)
{
	if (!SelectElement(Element))
		return false; // filtered out internally, most likely Element is masked out

	FBox const& BBox = SceneMapping.GetBoundingBox(Element);
	FVector CtrIModel; FRotator Trash(ForceInitToZero);
	UITwinUtilityLibrary::GetIModelBaseFromUnrealTransform(
		&Owner, FTransform(Trash, BBox.GetCenter()), CtrIModel, Trash);
	UE_LOG(LogITwin, Display,
		TEXT("ElementID 0x%I64x found in iModel %s with BBox %s centered on %s (in iModel spatial coords.: %s)"),
		Element.value(), *Owner.GetActorNameOrLabel(), *BBox.ToString(), *BBox.GetCenter().ToString(),
		*CtrIModel.ToString());

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

	Owner.GetMutableWebServices()->GetElementProperties(Owner.ITwinId, Owner.IModelId, 
		Owner.GetSelectedChangeset(), ITwin::ToString(Element));

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

void FITwinIModelInternals::HideElements(std::vector<ITwinElementID> const& InElementIDs)
{
	SceneMapping.HideElements(InElementIDs);
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
		BE_LOGE("ITwinAPI", "A name is required to create a new SavedView");
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
				FITwinIModel3DInfo OutInfo;
				(*IModelIter)->GetModel3DInfoInCoordSystem(OutInfo, EITwinCoordSystem::UE);
				FBox const IModelBBox(OutInfo.BoundingBoxMin, OutInfo.BoundingBoxMax);
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

// Console command to change the metallic factor of a given material.
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinSetMaterialChannelIntensity(
	TEXT("cmd.ITwinSetMaterialChannelIntensity"),
	TEXT("Set the intensity factor of the given channel for the given material."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	if (Args.Num() < 3)
	{
		BE_LOGE("ITwinAPI", "A material ID and metallic factor is required");
		return;
	}
	const uint64_t MatID = ITwin::ParseElementID(Args[0]).value();
	const int32 ChannelId = FCString::Atoi(*Args[1]);
	const double Intensity = FCString::Atod(*Args[2]);
	if (ChannelId >= (int32)SDK::Core::EChannelType::ENUM_END)
	{
		BE_LOGE("ITwinAPI", "Invalid material channel " << ChannelId);
		return;
	}
	for (TActorIterator<AITwinIModel> IModelIter(World); IModelIter; ++IModelIter)
	{
		(*IModelIter)->SetMaterialChannelIntensity(MatID,
			static_cast<SDK::Core::EChannelType>(ChannelId), Intensity);
	}
}));

#endif // ENABLE_DRAW_DEBUG
