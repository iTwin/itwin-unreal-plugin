/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinIModel.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinIModel.h>

#include <ITwinDigitalTwin.h>
#include <ITwinGeoLocation.h>
#include <ITwinIModelInternals.h>
#include <ITwinIModel3DInfo.h>
#include <ITwinIModelSettings.h>
#include <ITwinSavedView.h>
#include <ITwinSceneMappingBuilder.h>
#include <ITwinServerConnection.h>
#include <ITwinServerEnvironment.h>
#include <ITwinSetupMaterials.h>
#include <ITwinSynchro4DAnimator.h>
#include <ITwinSynchro4DSchedules.h>
#include <ITwinSynchro4DSchedulesInternals.h>
#include <ITwinUtilityLibrary.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <Decoration/ITwinDecorationHelper.h>
#include <Material/ITwinMaterialLibrary.h>
#include <TimerManager.h>
#include <ITwinCesium3DTileset.h>
#include <ITwinCesium3DTilesetLoadFailureDetails.h>
#include <Materials/MaterialInterface.h>
#include <BeUtils/Gltf/GltfTuner.h>
#include <BeUtils/Misc/MiscUtils.h>
#include <Network/JsonQueriesCache.h>
#include <Timeline/Timeline.h>
#include <Containers/Ticker.h>
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
#include <JsonObjectConverter.h>
#include <Kismet/KismetMathLibrary.h>
#include <Math/Box.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>
#include <UObject/ConstructorHelpers.h>
#include <Components/LightComponent.h>
#include <Misc/Paths.h>
#include <UObject/StrongObjectPtr.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeBuildConfig/MaterialTuning.h>
#	include <BeUtils/Gltf/GltfMaterialHelper.h>
#	include <BeUtils/Gltf/GltfMaterialTuner.h>
#	include <Core/ITwinAPI/ITwinMaterial.h>
#	include <Core/ITwinAPI/ITwinMaterial.inl>
#	include <Core/ITwinAPI/ITwinMaterialPrediction.h>
#	include <Core/ITwinAPI/ITwinTypes.h>
#	include <Core/Visualization/MaterialPersistence.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <memory>
#include <optional>
#include <unordered_set>


namespace ITwin
{

	static constexpr double ITWIN_BEST_SCREEN_ERROR = 1.0;
	static constexpr double ITWIN_WORST_SCREEN_ERROR = 100.0;

	//! Get a max-screenspace-error value from a percentage (value in range [0;1])
	double ToScreenSpaceError(float QualityValue)
	{
		double const NormalizedQuality = std::clamp(QualityValue, 0.f, 1.f);
		double ScreenError = NormalizedQuality * ITWIN_BEST_SCREEN_ERROR + (1.0 - NormalizedQuality) * ITWIN_WORST_SCREEN_ERROR;
		return ScreenError;
	}

	//! Adjust the tileset quality, given a percentage (value in range [0;1])
	void SetTilesetQuality(AITwinCesium3DTileset& Tileset, float Value)
	{
		Tileset.SetMaximumScreenSpaceError(ToScreenSpaceError(Value));
	}


	//! Returns the tileset quality as a percentage (value in range [0;1])
	float GetTilesetQuality(AITwinCesium3DTileset const& Tileset)
	{
		double ScreenError = std::clamp(Tileset.MaximumScreenSpaceError, ITWIN_BEST_SCREEN_ERROR, ITWIN_WORST_SCREEN_ERROR);
		double NormalizedQuality = (ScreenError - ITWIN_WORST_SCREEN_ERROR) / (ITWIN_BEST_SCREEN_ERROR - ITWIN_WORST_SCREEN_ERROR);
		return static_cast<float>(NormalizedQuality);
	}

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
		}
		if (IsValid(SchedulesComp->BaseMaterialTranslucent))
		{
			Tileset.SetTranslucentMaterial(SchedulesComp->BaseMaterialTranslucent);
		}
	}

	FString ToString(ITwinElementID const& Elem)
	{
		return FString::Printf(TEXT("0x%I64x"), Elem.value());
	}

	extern bool ShouldLoadDecoration(FITwinLoadInfo const& Info, UWorld const* World);
	extern void LoadDecoration(FITwinLoadInfo const& Info, UWorld* World);
	extern void SaveDecoration(FITwinLoadInfo const& Info, UWorld const* World);

	void DestroyTilesetsInActor(AActor& Owner)
	{
		const decltype(Owner.Children) ChildrenCopy = Owner.Children;
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

	static bool HasTilesetWithLocalURL(AActor const& Owner)
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

	ITWINRUNTIME_API bool IsMLMaterialPredictionEnabled()
	{
		// Work-in-progress feature for Carrot.
		auto const Settings = GetDefault<UITwinIModelSettings>();
		return Settings->bEnableML_MaterialPrediction;
	}
}

class FITwinIModelGltfTuner : public BeUtils::GltfTuner
{
public:
	FITwinIModelGltfTuner(AITwinIModel const& InOwner)
		: Owner(InOwner)
	{

	}

	//! Propagate the access token to the requests retrieving textures from the decoration service
	virtual CesiumAsync::HttpHeaders GetHeadersForExternalData() const override
	{
		return {
			{	"Authorization",
				std::string("Bearer ") + Owner.GetAccessTokenStdString()
			}
		};
	}

private:
	AITwinIModel const& Owner;
};

class AITwinIModel::FImpl
{
public:
	AITwinIModel& Owner;
	/// helper to fill/update SceneMapping
	TSharedPtr<FITwinSceneMappingBuilder> SceneMappingBuilder;
	std::shared_ptr<BeUtils::GltfTuner> GltfTuner;
	bool bHasFilledMaterialInfoFromTuner = false;
	bool bInitialized = false;
	bool bWasLoadedFromDisk = false;
	std::unordered_set<uint64_t> MatIDsToSplit; // stored to detect the need for retuning
	std::shared_ptr<BeUtils::GltfMaterialHelper> GltfMatHelper = std::make_shared<BeUtils::GltfMaterialHelper>();
	FITwinIModelInternals Internals;
	uint32 TilesetLoadedCount = 0;
	FDelegateHandle OnTilesetLoadFailureHandle;
	std::optional<FITwinExportInfo> ExportInfoPendingLoad;
	//! Will be initialized when the "get attached reality data" request is complete.
	std::optional<TArray<FString>> AttachedRealityDataIds;
	//! Used when the "get attached reality data" request is not yet complete,
	//! to store all pending promises.
	TArray<TSharedRef<TPromise<TArray<FString>>>> AttachedRealityDataIdsPromises;
	HttpRequestID GetAttachedRealityDataRequestId;
	ITwinHttp::FMutex GetAttachedRealityDataRequestIdMutex;
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
	AITwinDecorationHelper* DecorationPersistenceMgr = nullptr;
	std::unordered_map<ITwinScene::TileIdx, bool> TilesChangingVisibility;


	struct FITwinCustomMaterial
	{
		FString Name;
		bool bAdvancedConversion = false;
	};
	//! Map of iTwin materials (the IDs are retrieved from the meta-data provided by the M.E.S)
	TMap<uint64, FITwinCustomMaterial> ITwinMaterials;
	//! Map of materials returned by the ML-based prediction service.
	TMap<uint64, FITwinCustomMaterial> MLPredictionMaterials;
	//! Secondary (optional) observer used for UI typically.
	IITwinWebServicesObserver* MLPredictionMaterialObserver = nullptr;

	struct FMatPredictionEntry
	{
		uint64_t MatID = 0;
		std::vector<uint64_t> Elements;
	};
	std::vector<FMatPredictionEntry> MaterialMLPredictions;


	FImpl(AITwinIModel& InOwner)
		: Owner(InOwner), Internals(InOwner)
	{
	}

	void Initialize();
	void HandleTilesHavingChangedVisibility();
	void HandleTilesRenderReadiness();

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

	void FindPersistenceMgr()
	{
		if (DecorationPersistenceMgr)
			return;
		//Look if a helper already exists:
		for (TActorIterator<AITwinDecorationHelper> DecoIter(Owner.GetWorld()); DecoIter; ++DecoIter)
		{
			DecorationPersistenceMgr = *DecoIter;
		}
		if (DecorationPersistenceMgr)
		{
			DecorationPersistenceMgr->OnSceneLoaded.AddDynamic(&Owner, &AITwinIModel::OnSceneLoaded);
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
		if (!IsValid(Owner.Synchro4DSchedules))
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
		if (!ensure(!GetDefault<URendererSettings>()->bOrderedIndependentTransparencyEnable))
		{
			// see OIT-related posts in
			// https://forums.unrealengine.com/t/ue5-gpu-crashed-or-d3d-device-removed/524297/168:
			// it could be a problem with all transparencies (and "mask opacity"), not just cutting planes!
			UE_LOG(LogITwin, Error, TEXT("bOrderedIndependentTransparencyEnable=true will crash cut planes, sorry! See if 'r.OIT.SortedPixels' is in your DefaultEngine.ini, in section [/Script/Engine.RendererSettings], if not, add it set to False (and relaunch the app or Editor).\nDISABLING ALL Cutting Planes (aka. growth simulation) in the Synchro4D schedules!"));
			S4D.bDisableCuttingPlanes = true;
		}
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

	void SetupMaterials(AITwinCesium3DTileset& Tileset) const
	{
		ITwin::SetupMaterials(Tileset, Owner.Synchro4DSchedules);
	}

	void DestroyTileset()
	{
		ITwin::DestroyTilesetsInActor(Owner);
	}

	void LoadDecorationIfNeeded();

	void OnLoadingUIEvent();
	void UpdateAfterLoadingUIEvent();
	void AutoExportAndLoad();

	void TestExportCompletionAfterDelay(FString const& InExportId, float DelayInSeconds);

	/// Fills the map of known iTwin materials, if it was read from the tileset.
	void FillMaterialInfoFromTuner();

	void InitTextureDirectory();

	TMap<uint64, FITwinCustomMaterial> const& GetCustomMaterials() const
	{
		return Owner.VisualizeMaterialMLPrediction() ? MLPredictionMaterials : ITwinMaterials;
	}

	TMap<uint64, FITwinCustomMaterial>& GetMutableCustomMaterials()
	{
		return Owner.VisualizeMaterialMLPrediction() ? MLPredictionMaterials : ITwinMaterials;
	}

	/// Retune the tileset if needed, to ensure that all materials customized by the user (or about to be...)
	/// can be applied to individual meshes.
	void SplitGltfModelForCustomMaterials(bool bForceRetune = false);

	//! Enforce reloading material definitions as read from the decoration service.
	void ReloadCustomizedMaterials();

	template <typename MaterialParamHelper>
	void TSetMaterialChannelParam(MaterialParamHelper const& Helper, uint64_t MaterialId);

	FString GetMaterialChannelColorTextureID(uint64_t MaterialId, SDK::Core::EChannelType Channel,
		SDK::Core::ETextureSource& OutSource) const;
	FString GetMaterialChannelIntensityTextureID(uint64_t MaterialId, SDK::Core::EChannelType Channel,
		SDK::Core::ETextureSource& OutSource) const;

	void SetMaterialChannelColorTextureID(uint64_t MaterialId, SDK::Core::EChannelType Channel,
		FString const& TextureId, SDK::Core::ETextureSource eSource);
	void SetMaterialChannelIntensityTextureID(uint64_t MaterialId, SDK::Core::EChannelType Channel,
		FString const& TextureId, SDK::Core::ETextureSource eSource);

	bool SetMaterialName(uint64_t MaterialId, FString const& NewName);

	bool LoadMaterialFromAssetFile(uint64_t MaterialId, FString const& AssetFilePath);

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
	void DisplayFeatureBBoxes() const
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
		FElementsGroup IModelElements;
		for (auto const& Elem : AllElems)
		{
			IModelElements.insert(Elem.ElementID);
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
		auto& SchedulesInternals = GetInternals(*Owner.Synchro4DSchedules);

		auto CreateDebugTimeline = [this, &SchedulesInternals](ITwinElementID const ElementID)
			{
				FITwinElementTimeline& ElementTimeline = SchedulesInternals.Timeline()
				   .ElementTimelineFor(FIModelElementsKey(ElementID), FElementsGroup{ ElementID });

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
				CreateDebugTimeline(Elem.ElementID);
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
		mutable ITwinHttp::FMutex Mutex;

		EState State = EState::NotStarted;
		int QueryRowStart = 0, TotalRowsParsed = 0;
		HttpRequestID CurrentRequestID;
		static const int QueryRowCount = 50000;

		void DoRestart()
		{
			QueryRowStart = TotalRowsParsed = 0;
			FString const CacheFolder = QueriesCache::GetCacheFolder(
				(KindOfMetadata == EElementsMetadata::Hierarchy) 
					? QueriesCache::ESubtype::ElementsHierarchies : QueriesCache::ESubtype::ElementsSourceIDs,
				Owner.ServerConnection->Environment, Owner.ITwinId, Owner.IModelId, Owner.ResolvedChangesetId);
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
				Cache.Uninitialize(); // reinit, we may have a new changesetId for example
				UE_LOG(LogITwin, Display, TEXT("Elements metadata queries (re)starting..."));
				DoRestart();
			}
			else
			{
				State = EState::NeedRestart;
			}
		}

		void SetCurrentRequestID(HttpRequestID const& ReqID)
		{
			ITwinHttp::FLock Lock(Mutex);
			CurrentRequestID = ReqID;
		}

		bool TestIsCurrentRequestID(HttpRequestID const& ReqID) const
		{
			ITwinHttp::FLock Lock(Mutex);
			return (CurrentRequestID == ReqID);
		}

		void QueryNextPage()
		{
			State = EState::Running;
			RequestInfo.emplace(Owner.WebServices->InfosToQueryIModel(Owner.ITwinId, Owner.IModelId, 
				Owner.ResolvedChangesetId, ECSQLQueryString, QueryRowStart, QueryRowCount));
			QueryRowStart += QueryRowCount;
			auto const Hit = Cache.IsValid() ? Cache.LookUp(*RequestInfo, Mutex) : std::nullopt;
			if (Hit)
			{
				SetCurrentRequestID({});
				OnQueryCompleted({}, true, Cache.Read(*Hit));
				// Now just return, ie read only one response per tick even if everything's in the cache.
				// The alternative, while still not blocking the game thread, would be to use a worker thread,
				// but there is no synchronization mechanism on SceneMapping to allow that yet.
			}
			else
			{
				Owner.WebServices->QueryIModelRows(Owner.ITwinId, Owner.IModelId, Owner.ResolvedChangesetId,
					ECSQLQueryString, QueryRowStart, QueryRowCount,
					std::bind(&FQueryElementMetadataPageByPage::SetCurrentRequestID, this,
							  std::placeholders::_1),
					&(*RequestInfo));
			}
		}

		/// \return Whether the reply was to a request emitted by this instance of metadata requester, and was
		///			thus parsed here.
		bool OnQueryCompleted(HttpRequestID const& RequestID, bool const bSuccess,
			std::variant<FString, TSharedPtr<FJsonObject>> const& QueryResult)
		{
			bool const bFromCache = (QueryResult.index() == 1);
			if (!bFromCache && !TestIsCurrentRequestID(RequestID))
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
				State = EState::StoppedOnError;
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
				if (Cache.IsValid())
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
				QueryNextPage();
			}
			else
			{
				UE_LOG(LogITwin, Display, TEXT("Total %s retrieved from %s: %d."), *BatchMsg,
					/*likely all retrieved from same source...*/bFromCache ? TEXT("cache") : TEXT("remote"),
					TotalRowsParsed);
				// This call will release hold of the cache folder, which will "often" allow reuse by cloned
				// actor when entering PIE (unless it was not yet finished downloading, of course)
				Cache.Uninitialize();
				State = EState::Finished;
			}
			return true;
		}

		void OnIModelUninit()
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

	// This was added following a situation where the iModel doesn't tick at all, which shouldn't happen...
	// But in any case, I'm not sure we are guaranteed that the iModel will have ticked at least once before
	// reaching this: all the preliminary requests might happen before the iModel first ticks, for example if
	// in the future we have all replies in cache, or no longer use http at all (eg. through Mango?)
	if (!bInitialized)
		Initialize();

	FITwinExportInfo const CompleteInfo = ExportInfo ? (*ExportInfo) : (*ExportInfoPendingLoad);
	ExportInfoPendingLoad.reset();
	// No need to keep former versions of the tileset
	GetInternals(Owner).SceneMapping.Reset();
	DestroyTileset();

	// We need to query these metadata of iModel Elements using several "paginated" requests sent
	// successively, but we also need to support interrupting and restart queries from scratch
	// because this code path can be executed several times for an iModel, eg. upon UpdateIModel
	ElementsHierarchyQuerying->Restart();
	ElementsSourceQuerying->Restart();
	// It seems risky to NOT do a ResetSchedules here: for example, FITwinElement::AnimationKeys are
	// not set, MainTimeline::NonAnimatedDuplicates is empty, etc.
	// We could just "reinterpret" the known schedule data, but since it is in a local cache...
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

	auto const Settings = GetDefault<UITwinIModelSettings>();
	// TODO_GCO: Necessary for picking, unless there is another method that does
	// not require the Physics data? Note that pawn collisions need to be disabled to
	// still allow navigation through meshes (see SetActorEnableCollision).
	Tileset->SetCreatePhysicsMeshes(Settings->IModelCreatePhysicsMeshes);
	Tileset->SetMaximumScreenSpaceError(Settings->TilesetMaximumScreenSpaceError);
	// connect mesh creation callback
	Tileset->SetMeshBuildCallbacks(SceneMappingBuilder);
	Tileset->SetGltfTuner(GltfTuner);
	Tileset->SetTilesetSource(EITwinTilesetSource::FromUrl);
	Tileset->SetUrl(CompleteInfo.MeshUrl);

	Tileset->MaximumCachedBytes = std::max(0ULL, Settings->CesiumMaximumCachedMegaBytes * (1024 * 1024ULL));
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
	Internals.SceneMapping.SetIModel2UnrealTransfos(Owner);
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

void AITwinIModel::FImpl::InitTextureDirectory()
{
	// In case we need to download textures, setup a destination folder depending on current iModel
	FString TextureDir = FPlatformProcess::UserSettingsDir();
	if (!TextureDir.IsEmpty() && !Owner.IModelId.IsEmpty())
	{
		// TODO_JDE - Should it depend on the changeset?
		TextureDir = FPaths::Combine(TextureDir,
			TEXT("Bentley"), TEXT("Cache"), TEXT("Textures"),
			Owner.IModelId);
		BeUtils::GltfMaterialHelper::Lock Lock(GltfMatHelper->GetMutex());
		GltfMatHelper->SetTextureDirectory(*TextureDir, Lock);
	}
}

bool AITwinIModel::FImpl::SetMaterialName(uint64_t MaterialId, FString const& NewName)
{
	if (NewName.IsEmpty())
	{
		return false;
	}
	FITwinCustomMaterial* CustomMat = GetMutableCustomMaterials().Find(MaterialId);
	if (!ensureMsgf(CustomMat, TEXT("unknown material ID")))
	{
		return false;
	}

	if (GltfMatHelper->SetMaterialName(MaterialId, TCHAR_TO_UTF8(*NewName)))
	{
		CustomMat->Name = NewName;
		return true;
	}
	else
	{
		return false;
	}
}


AITwinIModel::AITwinIModel()
	: Impl(MakePimpl<FImpl>(*this))
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("root")));
	PrimaryActorTick.bCanEverTick = true;

	// Fetch default textures for material tuning. Note that they are provided by the Cesium plugin.
	// Use a structure to hold one-time initialization like in #UITwinSynchro4DSchedules's ctor
	struct FConstructorStatics {
		ConstructorHelpers::FObjectFinder<UTexture2D> NoColor;
		ConstructorHelpers::FObjectFinder<UTexture2D> NoNormal;
		ConstructorHelpers::FObjectFinder<UTexture2D> NoMetallicRoughness;
		FConstructorStatics()
			: NoColor(TEXT("/ITwinForUnreal/Textures/NoColorTexture"))
			, NoNormal(TEXT("/ITwinForUnreal/Textures/NoNormalTexture"))
			, NoMetallicRoughness(TEXT("/ITwinForUnreal/Textures/NoMetallicRoughnessTexture"))
		{}
	};
	static FConstructorStatics ConstructorStatics;
	this->NoColorTexture = ConstructorStatics.NoColor.Object;
	this->NoNormalTexture = ConstructorStatics.NoNormal.Object;
	this->NoMetallicRoughnessTexture = ConstructorStatics.NoMetallicRoughness.Object;
}

void AITwinIModel::Tick(float Delta)
{
	if (!Impl->bInitialized)
		Impl->Initialize();
	Impl->HandleTilesHavingChangedVisibility();
	Impl->Internals.SceneMapping.HandleNewSelectingAndHidingTextures();
	if (AITwinIModel::FImpl::FQueryElementMetadataPageByPage::EState::Finished
		== Impl->ElementsHierarchyQuerying->GetState()
		&& AITwinIModel::FImpl::FQueryElementMetadataPageByPage::EState::Finished
		== Impl->ElementsSourceQuerying->GetState())
	{
		// Could also use the Component tick, overriding ShouldTickIfViewportsOnly like for iModels, only
		// enabling ticking here when above queries are indeed finished but no longer needing a custom
		// TickSchedules method.
		// BUT when exactly is the component ticked? Leaving this here I'm sure of the order of calls, for
		// example HandleTilesRenderReadiness() is better called after TickSchedules, otherwise tile
		// render-readiness might only be notified at the next tick
		Synchro4DSchedules->TickSchedules(Delta);
	}
	Impl->HandleTilesRenderReadiness();
}

/// Lazy-initialize most of the stuff formerly done either in the constructor (iModel's or its Impl's), or
/// even in PostLoad. The problem was that this stuff is only useful for "active" actors, ie either played
/// in-game (or PIE), or used interactively in the Editor, BUT various other actors are instantiated, eg. the
/// ClassDefaultObject, but also when packaging levels, etc. Even deleting an actor from the Editor's Outliner
/// apparently reloads the current level and re-created an ("invisible") iModel, popping an auth browser if
/// needed, re-downloading its schedule in the background (because its local cache was already used), etc.
/// All that for a transient object! Only "activating" the iModel in its Tick() method should prevent all this.
void AITwinIModel::FImpl::Initialize()
{
	ensure(!Owner.HasAnyFlags(RF_ClassDefaultObject));
	bInitialized = true;

	GltfTuner = std::make_shared<FITwinIModelGltfTuner>(Owner);
	GltfTuner->SetMaterialHelper(GltfMatHelper);
	// create a callback to fill our scene mapping when meshes are loaded
	SceneMappingBuilder = MakeShared<FITwinSceneMappingBuilder>(Owner);
	ElementsHierarchyQuerying.emplace(Owner, EElementsMetadata::Hierarchy);
	ElementsSourceQuerying.emplace(Owner, EElementsMetadata::SourceIdentifiers);
	Internals.Uniniter->Register([this] {
		ElementsHierarchyQuerying->OnIModelUninit();
		ElementsSourceQuerying->OnIModelUninit();
	});

	CreateSynchro4DSchedulesComponent();

	// ML material prediction is only accessible when customizing the application/plugin configuration.
	Owner.bEnableMLMaterialPrediction = ITwin::IsMLMaterialPredictionEnabled();

	if (ITwin::HasMaterialTuning())
	{
		// As soon as material IDs are read, launch a request to RPC service to get the corresponding material
		// properties
		GltfTuner->SetMaterialInfoReadCallback(
			[this](std::vector<BeUtils::ITwinMaterialInfo> const& MaterialInfos)
		{
			// Initialize the map of customizable materials at once.
			FillMaterialInfoFromTuner();

			// Initialize persistence at low level, if any.
			if (MaterialPersistenceMngr && ensure(!Owner.IModelId.IsEmpty()))
			{
				GltfMatHelper->SetPersistenceInfo(TCHAR_TO_ANSI(*Owner.IModelId), MaterialPersistenceMngr);
			}

			// Pre-fill material slots in the material helper, and detect potential user customizations
			// (in Carrot MVP, they are stored in the decoration service).
			int NumCustomMaterials = 0;
			{
				BeUtils::GltfMaterialHelper::Lock Lock(GltfMatHelper->GetMutex());
				for (BeUtils::ITwinMaterialInfo const& MatInfo : MaterialInfos)
				{
					GltfMatHelper->CreateITwinMaterialSlot(MatInfo.id, Lock);
				}
			}
			// If material customizations were already loaded from the decoration server (this is done
			// asynchronously), detect them at once so that the 1st displayed tileset directly shows the
			// customized materials. If not, this will be done when the decoration data has finished loading,
			// which may trigger a re-tuning, and thus some visual artifacts...
			Owner.DetectCustomizedMaterials();

			// Then launch a request to fetch all material properties.
			TArray<FString> MaterialIds;
			MaterialIds.Reserve(MaterialInfos.size());
			Algo::Transform(MaterialInfos, MaterialIds,
				[](BeUtils::ITwinMaterialInfo const& V) -> FString
			{
				return FString::Printf(TEXT("0x%I64x"), V.id);
			});
			Owner.GetMutableWebServices()->GetMaterialListProperties(
				Owner.ITwinId, Owner.IModelId, Owner.GetSelectedChangeset(), MaterialIds);
		});
	}
	// Formerly in PostLoad (see method comment for why it was a problem)
	if (bWasLoadedFromDisk)
	{
		// If the loaded iModel uses custom materials, notify the tuner so that it splits the model accordingly
		SplitGltfModelForCustomMaterials();

		// need to fetch a new changesetId if we have saved one but user asks to always use latest
		if (UseLatestChangeset())
		{
			Owner.bResolvedChangesetIdValid = false;
			Owner.ExportId = FString();
		}

		// When the loaded level contains an iModel already configured, it is imperative to call UpdateIModel
		// for various reasons: to get a new URL for the tileset, with a valid signature ("sig" parameter, part
		// of the URL), to trigger the download of Elements metadata (see ElementMetadataQuerying), which in
		// turn will enable and trigger the download of Synchro4 schedules, etc.
		if (!Owner.IModelId.IsEmpty())
		{
			// Exception: if the user has replaced the cesium URL by a local one, do not reload the tileset
			// (this is mostly used for debugging...)
			if (!ITwin::HasTilesetWithLocalURL(Owner))
			{
				OnLoadingUIEvent();
			}
		}
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
	Impl->DestroyTileset();
	Impl->Update();
	UpdateSavedViews();
}

void AITwinIModel::ZoomOnIModel()
{
	// We zoom on the bounding box of the iModel, computed from the project extents converted to Unreal space
	auto* const TileSet = GetTileset();
	if (!TileSet)
		return;
	if (!(Impl->IModelProperties && Impl->IModelProperties->ProjectExtents))
		return;
	FImpl::ZoomOn(FBox(Impl->IModelProperties->ProjectExtents->Low,
					   Impl->IModelProperties->ProjectExtents->High)
					.TransformBy(UITwinUtilityLibrary::GetIModelToUnrealTransform(this)),
		GetWorld());
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

void AITwinIModel::FImpl::AutoExportAndLoad()
{
	if (ensure(Owner.LoadingMethod == ELoadingMethod::LM_Automatic
		&& !Owner.IModelId.IsEmpty()))
	{
		// automatically start the export if necessary.
		bAutoStartExportIfNeeded = true;
		Owner.UpdateIModel();
	}
}

void AITwinIModel::GetModel3DInfoInCoordSystem(FITwinIModel3DInfo& OutInfo, EITwinCoordSystem CoordSystem) const
{
	if (Impl->IModelProperties->ProjectExtents)
	{
		if (EITwinCoordSystem::UE == CoordSystem)
		{
			FBox const Box = FBox(Impl->IModelProperties->ProjectExtents->Low
									- Impl->IModelProperties->ProjectExtents->GlobalOrigin,
								  Impl->IModelProperties->ProjectExtents->High
									- Impl->IModelProperties->ProjectExtents->GlobalOrigin)
				.TransformBy(Impl->Internals.SceneMapping.GetIModel2UnrealTransfo());
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

TFuture<TArray<FString>> AITwinIModel::GetAttachedRealityDataIds()
{
	// Check if result is already available.
	if (Impl->AttachedRealityDataIds)
		return MakeFulfilledPromise<TArray<FString>>(*Impl->AttachedRealityDataIds).GetFuture();
	// Result is not available yet, create promise and send request if not done yet.
	const auto Promise = MakeShared<TPromise<TArray<FString>>>();
	Impl->AttachedRealityDataIdsPromises.Add(Promise);
	ITwinHttp::FLock Lock(Impl->GetAttachedRealityDataRequestIdMutex);
	if (Impl->GetAttachedRealityDataRequestId.IsEmpty())
	{
		WebServices->QueryIModelRows(ITwinId, IModelId, ResolvedChangesetId,
			TEXT("SELECT Element.UserLabel, Model.JsonProperties FROM bis.Model JOIN bis.Element ON Element.ECInstanceId = Model.ECInstanceId WHERE Model.ECClassId IS (ScalableMesh.ScalableMeshModel)"),
			0, 1000,
			[Impl = this->Impl.Get()](HttpRequestID const& ReqID)
			{
				ITwinHttp::FLock Lock(Impl->GetAttachedRealityDataRequestIdMutex);
				Impl->GetAttachedRealityDataRequestId = ReqID;
			});
	}
	return Promise->GetFuture();
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
		this->OnIModelLoaded.Broadcast(false, IModelId);
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
		this->OnIModelLoaded.Broadcast(true, IModelId);
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
		if (Info.Status == TEXT("Complete") && !Info.MeshUrl.IsEmpty())
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
	if (CompleteInfo->iTwinId.IsEmpty() || CompleteInfo->Id.IsEmpty() || CompleteInfo->MeshUrl.IsEmpty())
	{
		BE_LOGE("ITwinAPI", "Invalid export info received for iModelId \"" <<
			TCHAR_TO_UTF8(*IModelId) << "\", those are required fields: "
			<< (CompleteInfo->iTwinId.IsEmpty() ? "iTwinId " : "")
			<< (CompleteInfo->Id.IsEmpty() ? "exportId " : "")
			<< (CompleteInfo->MeshUrl.IsEmpty() ? "MeshUrl " : ""));
		return;
	}
	UE_LOG(LogITwin, Verbose, TEXT("Proceeding to load iTwin %s with export %s"),
							  *CompleteInfo->iTwinId, *CompleteInfo->Id);
	ExportStatus = EITwinExportStatus::Complete;
	// in Automatic mode, it is still empty and must be set here because the 4D apis require it:
	ITwinId = CompleteInfo->iTwinId;
	ExportId = CompleteInfo->Id; // informative only (needed here for  Automatic mode)
	// Start retrieving the decoration if needed (usually, it is triggered before)
	Impl->LoadDecorationIfNeeded();
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
		Impl->TestExportCompletionAfterDelay(ExportInfo.Id, 3.f);
	}
}

void AITwinIModel::OnElementPropertiesRetrieved(bool bSuccess, FElementProperties const& ElementProps,
												FString const& ElementId)
{
	if (!bSuccess)
		return;
	FString JSONString;
	FJsonObjectConverter::UStructToJsonObjectString(ElementProps, JSONString, 0, 0);
	UE_LOG(LogITwin, Display, TEXT("Element properties retrieved: %s"), *JSONString);
}

void AITwinIModel::OnIModelQueried(bool bSuccess, FString const& QueryResult, HttpRequestID const& RequestID)
{
	if ([&] { ITwinHttp::FLock Lock(Impl->GetAttachedRealityDataRequestIdMutex);
			  return (RequestID == Impl->GetAttachedRealityDataRequestId); }
		())
	{
		const auto RealityDataIds = MakeShared<TArray<FString>>();
		const auto PendingRequestCount = MakeShared<size_t>(0);
		const auto OnRequestComplete = [=, this]
			{
				--(*PendingRequestCount);
				if (*PendingRequestCount != 0)
					return;
				// List of attached reality data is complete, store it and fill pending promises.
				Impl->AttachedRealityDataIds = *RealityDataIds;
				for (const auto& Promise: Impl->AttachedRealityDataIdsPromises)
					Promise->SetValue(*RealityDataIds);
				Impl->AttachedRealityDataIdsPromises.Empty();
			};
		// Increment pending request count (and decrement it before returning),
		// to make sure OnRequestComplete is called even when there is no attached reality data,
		// or there was an error processing the request.
		++(*PendingRequestCount);
		ON_SCOPE_EXIT{OnRequestComplete();};
		// The request may fail if the schema of the iModel does not have the ScalableMeshModel class.
		if (!bSuccess)
			return;
		TSharedPtr<FJsonObject> QueryResultJson;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(QueryResult), QueryResultJson);
		for (const auto& RowObject: QueryResultJson->GetArrayField(TEXT("data")))
		{
			const auto& RowArray = RowObject->AsArray();
			// The row should contain 2 fields: display name and json props.
			if (RowArray.Num() != 2)
			{
				check(false);
				continue;
			}
			// Parse json props: retrieve reality data id (if it exists).
			TSharedPtr<FJsonObject> JSonPropertiesJson;
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(RowArray[1]->AsString()), JSonPropertiesJson);
			const auto RealityDataId = FString(BeUtils::GetRealityDataIdFromUrl(StringCast<ANSICHAR>(*JSonPropertiesJson->GetStringField(TEXT("tilesetUrl"))).Get()).c_str());
			if (RealityDataId.IsEmpty())
				continue;
			// Retrieve metadata of reality data and check its format (we only support Cesium3DTiles).
			++(*PendingRequestCount);
			const auto Request = FHttpModule::Get().CreateRequest();
			Request->SetURL(TEXT("https://")+ITwinServerEnvironment::GetUrlPrefix(WebServices->GetEnvironment())+TEXT("api.bentley.com/reality-management/reality-data/")+RealityDataId);
			Request->SetHeader(TEXT("Accept"), TEXT("application/vnd.bentley.itwin-platform.v1+json"));
			Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ")+ServerConnection->GetAccessToken());
			Request->OnProcessRequestComplete().BindLambda([=, this](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
				{
					ON_SCOPE_EXIT{OnRequestComplete();};
					if (!AITwinServerConnection::CheckRequest(Request, Response, bConnectedSuccessfully))
						return;
					TSharedPtr<FJsonObject> ResponseJson;
					FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), ResponseJson);
					if (ResponseJson->GetObjectField(TEXT("realityData"))->GetStringField(TEXT("type")) != TEXT("Cesium3DTiles"))
						return;
					RealityDataIds->Add(RealityDataId);
				});
			Request->ProcessRequest();
		}
		return;
	}
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

	// In case we need to download / customize textures, setup a folder depending on current iModel
	Impl->InitTextureDirectory();

	BeUtils::GltfMaterialHelper::Lock lock(Impl->GltfMatHelper->GetMutex());

	auto& CustomMaterials = Impl->ITwinMaterials;

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
			FImpl::FITwinCustomMaterial* CustomMat = CustomMaterials.Find(id64.value());
			ensureMsgf(CustomMat != nullptr, TEXT("Material mismatch: ID %s not found in tileset.json (%s)"),
				*MaterialID, *FString(matProperties.name.c_str()));

			Impl->GltfMatHelper->SetITwinMaterialProperties(id64.value(), matProperties, lock);
		}
	}

	// Start downloading iTwin textures.
	if (ensure(WebServices))
	{
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

void AITwinIModel::OnMatMLPredictionRetrieved(bool bSuccess, SDK::Core::ITwinMaterialPrediction const& Prediction)
{
	if (!ensure(ITwin::IsMLMaterialPredictionEnabled()))
		return;

	if (!bSuccess || Prediction.data.empty())
	{
		MLMaterialPredictionStatus = EITwinMaterialPredictionStatus::Failed;
		if (Impl->MLPredictionMaterialObserver)
		{
			Impl->MLPredictionMaterialObserver->OnMatMLPredictionRetrieved(false, {});
		}
		return;
	}

	MLMaterialPredictionStatus = EITwinMaterialPredictionStatus::Complete;

	// Deduce a new tuning from material prediction.
	// For the initial version, this will replace the materials retrieved previously from the decoration
	// service (to be defined: should we allow the user to revert to the previous version afterwards?)
	auto& MLPredMaterials = Impl->MLPredictionMaterials;
	MLPredMaterials = {};
	MLPredMaterials.Reserve(Prediction.data.size());

	Impl->MaterialMLPredictions.clear();
	Impl->MaterialMLPredictions.reserve(Prediction.data.size());

	std::unordered_map<uint64_t, std::string> MatIDToName;

	// Reload/Save material customizations from/to a file in order to create a collection of materials for
	// the predefined material categories (Wood, Steel, Aluminum etc.)
	auto const& MatIOMngr = GetMaterialPersistenceManager();
	if (MatIOMngr)
	{
		// This path will be independent from the current iModel, so that it is easier to locate
		std::filesystem::path const MaterialDirectory =
			TCHAR_TO_UTF8(*QueriesCache::GetCacheFolder(
				QueriesCache::ESubtype::MaterialMLPrediction,
				ServerConnection->Environment, {}, {}, {}));

		MatIOMngr->SetLocalMaterialDirectory(MaterialDirectory);

		// Try to load local collection, if any
		MatIOMngr->LoadMaterialCollection(MaterialDirectory / "materials.json",
			TCHAR_TO_UTF8(*IModelId), MatIDToName);
	}

	// We will use local material IDs (but try to keep the same ID for a given material)
	std::unordered_map<std::string, uint64_t> NameToMatID;
	uint64 NextMaterialID = 0x4051981;
	bool hasFoundNewNames = false;
	for (auto const& [matId, name] : MatIDToName)
	{
		NameToMatID[name] = matId;
		if (NextMaterialID <= matId)
		{
			NextMaterialID = matId + 1;
		}
	}

	BeUtils::GltfMaterialHelper::Lock Lock(Impl->GltfMatHelper->GetMutex());

	for (auto const& MatEntry : Prediction.data)
	{
		// Assign a unique ID for each material of the classification, trying to reuse predefined ones loaded
		// from default collections.
		auto const itID = NameToMatID.find(MatEntry.material);
		uint64_t MatID;
		if (itID == NameToMatID.end())
		{
			MatID = NextMaterialID++;
			MatIDToName.emplace(MatID, MatEntry.material);
			hasFoundNewNames = true;
		}
		else
		{
			MatID = itID->second;
		}
		FImpl::FITwinCustomMaterial& CustomMat = MLPredMaterials.FindOrAdd(MatID);
		CustomMat.Name = UTF8_TO_TCHAR(MatEntry.material.c_str());

		auto& MatPredictionEntry = Impl->MaterialMLPredictions.emplace_back();
		MatPredictionEntry.Elements = MatEntry.elements;
		MatPredictionEntry.MatID = MatID;

		// Also create the corresponding entries in the material helper (important for edition), and enable
		// material tuning if we do have a custom definition.
		auto const MatInfo = Impl->GltfMatHelper->CreateITwinMaterialSlot(MatID, Lock);
		if (MatInfo.second && SDK::Core::HasCustomSettings(*MatInfo.second))
		{
			CustomMat.bAdvancedConversion = true;
		}
	}

	if (MatIOMngr && hasFoundNewNames)
	{
		MatIOMngr->AppendMaterialCollectionNames(MatIDToName);
	}

	if (Impl->MLPredictionMaterialObserver)
	{
		Impl->MLPredictionMaterialObserver->OnMatMLPredictionRetrieved(bSuccess, Prediction);
	}

	Impl->SplitGltfModelForCustomMaterials();
}

void AITwinIModel::OnMatMLPredictionProgress(float fProgressRatio)
{
	// Just log progression
	FString strProgress = TEXT("computing material predictions for ") + GetActorNameOrLabel();
	if (fProgressRatio < 1.f)
		strProgress += FString::Printf(TEXT("... (%.0f%%)"), 100.f * fProgressRatio);
	else
		strProgress += TEXT(" -> done");
	BE_LOGI("ITwinAPI", "[ML_MaterialPrediction] " << TCHAR_TO_UTF8(*strProgress));

	if (Impl->MLPredictionMaterialObserver)
	{
		Impl->MLPredictionMaterialObserver->OnMatMLPredictionProgress(fProgressRatio);
	}
}

void AITwinIModel::Retune()
{
	++Impl->GltfTuner->currentVersion;
}

void AITwinIModel::FImpl::FillMaterialInfoFromTuner()
{
	// In case we need to download / customize textures, setup a folder depending on current iModel
	InitTextureDirectory();

	auto const Materials = GltfTuner->GetITwinMaterialInfo();
	int32 const NbMaterials = (int32)Materials.size();
	ITwinMaterials.Reserve(NbMaterials);

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
		FITwinCustomMaterial& CustomMat = ITwinMaterials.FindOrAdd(MatInfo.id);
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
			CustomMat.Name = BuildMaterialNameFromInteger(ITwinMaterials.Num());
		}
	}
	bHasFilledMaterialInfoFromTuner = true;
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

void AITwinIModel::FImpl::LoadDecorationIfNeeded()
{
	if (Owner.ITwinId.IsEmpty() || Owner.IModelId.IsEmpty())
		return;
	if (Owner.CheckServerConnection(false) != SDK::Core::EITwinAuthStatus::Success)
		return;
	FITwinLoadInfo const Info = Owner.GetModelLoadInfo();
	if (ITwin::ShouldLoadDecoration(Info, Owner.GetWorld()))
	{
		ITwin::LoadDecoration(Info, Owner.GetWorld());
	}
}

void AITwinIModel::SaveDecoration()
{
	ITwin::SaveDecoration(GetModelLoadInfo(), GetWorld());
}

void AITwinIModel::DetectCustomizedMaterials()
{
	// Detect user customizations (they are stored in the decoration service).
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

	auto& CustomMaterials = Impl->ITwinMaterials;
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
		Impl->SplitGltfModelForCustomMaterials();
	}
}

void AITwinIModel::FImpl::ReloadCustomizedMaterials()
{
	int NumCustomMaterials = 0;
	BeUtils::GltfMaterialHelper::Lock Lock(GltfMatHelper->GetMutex());
	size_t LoadedSettings = GltfMatHelper->LoadMaterialCustomizations(Lock, true);
	for (auto& [MatID, CustomMat] : ITwinMaterials)
	{
		CustomMat.bAdvancedConversion = GltfMatHelper->HasCustomDefinition(MatID, Lock);
	}
	SplitGltfModelForCustomMaterials();
}

void AITwinIModel::ReloadCustomizedMaterials()
{
	Impl->ReloadCustomizedMaterials();
	RefreshTileset();
}

void AITwinIModel::FImpl::SplitGltfModelForCustomMaterials(bool bForceRetune /*= false*/)
{
	std::unordered_set<uint64_t> NewMatIDsToSplit;

	auto const& CustomMaterials = GetCustomMaterials();
	for (auto const& [MatID, CustomMat] : CustomMaterials)
	{
		if (CustomMat.bAdvancedConversion)
		{
			NewMatIDsToSplit.insert(MatID);
		}
	}
	if (NewMatIDsToSplit != this->MatIDsToSplit || bForceRetune)
	{
		this->MatIDsToSplit = NewMatIDsToSplit;

		BeUtils::GltfTuner::Rules rules;

		if (Owner.VisualizeMaterialMLPrediction())
		{
			rules.elementGroups_.reserve(MaterialMLPredictions.size());
			for (auto const& MatEntry : MaterialMLPredictions)
			{
				auto& ElementGroup = rules.elementGroups_.emplace_back();
				ElementGroup.elements_ = MatEntry.Elements;
				ElementGroup.itwinMaterialID_ = MatEntry.MatID;
				ElementGroup.material_ = 0; // does not matter much (will be overridden), but needs to be >= 0
			}
		}

		rules.itwinMatIDsToSplit_.swap(NewMatIDsToSplit);
		GltfTuner->SetRules(std::move(rules));

		Owner.Retune();
	}
}

TMap<uint64, FString> AITwinIModel::GetITwinMaterialMap() const
{
	TMap<uint64, FString> itwinMats;
	auto const& CustomMaterials = Impl->GetCustomMaterials();
	itwinMats.Reserve(CustomMaterials.Num());
	for (auto const& [MatID, CustomMat] : CustomMaterials)
	{
		itwinMats.Emplace(MatID, CustomMat.Name);
	}
	return itwinMats;
}

FString AITwinIModel::GetMaterialName(uint64_t MaterialId) const
{
	FImpl::FITwinCustomMaterial const* Mat = Impl->GetCustomMaterials().Find(MaterialId);
	if (Mat)
		return Mat->Name;
	else
		return {};
}

double AITwinIModel::GetMaterialChannelIntensity(uint64_t MaterialId, SDK::Core::EChannelType Channel) const
{
	return Impl->GltfMatHelper->GetChannelIntensity(MaterialId, Channel);
}


namespace ITwin
{
	bool ResolveDecorationTextures(
		SDK::Core::MaterialPersistenceManager& matPersistenceMngr,
		SDK::Core::PerIModelTextureSet const& perModelTextures,
		TMap<FString, TWeakObjectPtr<AITwinIModel>> const& idToIModel,
		std::string const& accessToken);

	UTexture2D* ResolveMatLibraryTexture(
		BeUtils::GltfMaterialHelper const& GltfMatHelper,
		std::string const& TextureId);
}

namespace
{
	using EITwinChannelType = SDK::Core::EChannelType;

	enum class EITwinMaterialParamType
	{
		Scalar,
		Color,
		Map,
		UVTransform,
		Kind,
	};

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

		static EITwinMaterialParamType EditedType() { return EITwinMaterialParamType::Scalar; }

		MaterialIntensityHelper(BeUtils::GltfMaterialHelper& GltfHelper,
								SDK::Core::EChannelType InChannel,
								double NewIntensity)
			: Super(GltfHelper, InChannel, NewIntensity)
		{}

		ParamType GetCurrentValue(uint64_t MaterialId) const
		{
			return GltfMatHelper.GetChannelIntensity(MaterialId, this->Channel);
		}

		bool WillRequireNewCesiumTexture(ParamType const& /*CurrentValue*/, uint64_t /*MaterialId*/) const
		{
			// Obviously not (not a map)
			return false;
		}

		void SetNewValue(uint64_t MaterialId, bool& bModifiedValue) const
		{
			GltfMatHelper.SetChannelIntensity(MaterialId, this->Channel, this->NewValue, bModifiedValue);
		}

		void ApplyNewValueToScene(uint64_t MaterialId, FITwinSceneMapping& SceneMapping) const
		{
			SceneMapping.SetITwinMaterialChannelIntensity(MaterialId, this->Channel, this->NewValue);
		}

		bool NeedGltfTuning(const double CurrentIntensity) const
		{
			// Test if we need to switch the alphaMode of the material (which requires a re-tuning, because
			// we have to change the base material of the Unreal meshes, and not just change parameters in
			// some dynamic material instances...)
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

	// glTF format merges color with alpha, and metallic with roughness.
	inline std::optional<EITwinChannelType> GetGlTFCompanionChannel(EITwinChannelType Channel)
	{
		switch (Channel)
		{
		case EITwinChannelType::Color: return EITwinChannelType::Alpha;
		case EITwinChannelType::Alpha: return EITwinChannelType::Color;

		case EITwinChannelType::Metallic: return EITwinChannelType::Roughness;
		case EITwinChannelType::Roughness: return EITwinChannelType::Metallic;

		default: return std::nullopt;
		}
	}

	class MaterialMapParamHelper : public MaterialParamHelperBase<SDK::Core::ITwinChannelMap>
	{
		using Super = MaterialParamHelperBase<SDK::Core::ITwinChannelMap>;

	public:
		using ParamType = SDK::Core::ITwinChannelMap;

		static EITwinMaterialParamType EditedType() { return EITwinMaterialParamType::Map; }

		MaterialMapParamHelper(AITwinIModel const& InIModel,
			std::shared_ptr<BeUtils::GltfMaterialHelper> const& GltfHelperPtr,
			SDK::Core::EChannelType InChannel,
			SDK::Core::ITwinChannelMap const& NewMap)
			: Super(*GltfHelperPtr, InChannel, NewMap)
			, IModel(InIModel)
			, MatHelperPtr(GltfHelperPtr)
			, NewTexPath(NewMap.texture)
		{

		}

		bool BuildMergedTexture(uint64_t MaterialId) const
		{
			if (!NewTexPath.empty())
			{
				// Some channels require to be merged together (color+alpha), (metallic+roughness) or
				// formatted to use a given R,G,B,A component
				// => handle those cases here or else we would lose some information in case no tuning is
				// triggered.
				BeUtils::GltfMaterialTuner MatTuner(MatHelperPtr);
				auto const FormattingResult = MatTuner.ConvertChannelTextureToGltf(MaterialId,
					this->Channel, this->bNeedTranslucentMat);
				if (FormattingResult)
				{
					std::filesystem::path const glTFTexturePath = FormattingResult->filePath;
					if (!glTFTexturePath.empty())
					{
						NewTexPath = glTFTexturePath.generic_string();
						bHasBuiltMergedTexture = true;
						return true;
					}
				}
				else
				{
					BE_LOGE("ITwinMaterial", "Error while formatting texture for glTF material: "
						<< FormattingResult.error().message);
				}
			}
			return false;
		}

		bool NeedTranslucency() const { return this->bNeedTranslucentMat; }

		bool WillRequireNewCesiumTexture(ParamType const& CurrentMap, uint64_t MaterialId) const
		{
			// Detect if a new Cesium texture (baseColortTexture, metallicRoughnessTexture etc.) will be
			// needed compared to previous state.
			// Due to the way textures are loaded in Cesium, they do have an impact on the primitives as well
			// (see #updateTextureCoordinates)
			if (!this->NewValue.HasTexture() || CurrentMap.HasTexture())
			{
				return false;
			}
			// Some channels are merged together, test the companion channel in such case:
			auto const CompanionChannel = GetGlTFCompanionChannel(this->Channel);
			if (CompanionChannel
				&& GltfMatHelper.GetChannelMap(MaterialId, *CompanionChannel).HasTexture())
			{
				// The companion channel already uses a texture, which implies that the Cesium material does
				// have the corresponding texture already => no new Cesium texture will be added.
				return false;
			}
			return true;
		}

		void ApplyNewValueToScene(uint64_t MaterialId, FITwinSceneMapping& SceneMapping) const
		{
			UTexture2D* NewTexture = nullptr;
			if (NewTexPath.empty() || NewTexPath == SDK::Core::NONE_TEXTURE)
			{
				// Beware setting a null texture in a dynamic material through SetTextureParameterValueByInfo
				// would not nullify the texture, so instead, we use the default texture (depending on the
				// channel) to discard the effect.
				NewTexture = IModel.GetDefaultTextureForChannel(this->Channel);
			}
			else if (this->NewValue.eSource == SDK::Core::ETextureSource::Library
				&& !bHasBuiltMergedTexture)
			{
				// In packaged mode, there is no physical file for such textures => use Cesium asset accessor
				// mechanism to deal with those textures.
				NewTexture = ITwin::ResolveMatLibraryTexture(GltfMatHelper, NewTexPath);
			}
			else
			{
				NewTexture = FImageUtils::ImportFileAsTexture2D(UTF8_TO_TCHAR(NewTexPath.c_str()));
			}
			SceneMapping.SetITwinMaterialChannelTexture(MaterialId, this->Channel, NewTexture);
		}

	protected:
		AITwinIModel const& IModel;
		std::shared_ptr<BeUtils::GltfMaterialHelper> const& MatHelperPtr;
		mutable bool bNeedTranslucentMat = false;
		mutable bool bHasBuiltMergedTexture = false;
		mutable std::string NewTexPath;
	};

	class MaterialIntensityMapHelper : public MaterialMapParamHelper
	{
		using Super = MaterialMapParamHelper;

	public:
		MaterialIntensityMapHelper(AITwinIModel const& InIModel,
			std::shared_ptr<BeUtils::GltfMaterialHelper> const& GltfHelperPtr,
			SDK::Core::EChannelType InChannel,
			SDK::Core::ITwinChannelMap const& NewMap)
			: Super(InIModel, GltfHelperPtr, InChannel, NewMap)
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

		bool NeedGltfTuning(ParamType const& /*CurrentMap*/) const
		{
			// If the user changes the opacity map but the previous one was already requiring translucency,
			// there is no need to re-tune. Therefore we compare with the current alpha mode (if the latter
			// is unknown, it means we have never customized this material before, and thus we will have to
			// do it now...)
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

		static EITwinMaterialParamType EditedType() { return EITwinMaterialParamType::Color; }

		MaterialColorHelper(BeUtils::GltfMaterialHelper& GltfHelper,
							SDK::Core::EChannelType InChannel,
							ITwinColor const& NewColor)
			: Super(GltfHelper, InChannel, NewColor)
		{}

		ParamType GetCurrentValue(uint64_t MaterialId) const
		{
			return GltfMatHelper.GetChannelColor(MaterialId, this->Channel);
		}

		bool WillRequireNewCesiumTexture(ParamType const& /*CurrentValue*/, uint64_t /*MaterialId*/) const
		{
			// Obviously not (not a map)
			return false;
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

		bool NeedGltfTuning(ParamType const& /*CurrentIntensity*/) const
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
		MaterialColorMapHelper(AITwinIModel const& InIModel,
			std::shared_ptr<BeUtils::GltfMaterialHelper> const& GltfHelperPtr,
			SDK::Core::EChannelType InChannel,
			SDK::Core::ITwinChannelMap const& NewMap)
			: Super(InIModel, GltfHelperPtr, InChannel, NewMap)
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

		bool NeedGltfTuning(ParamType const& CurrentMap) const
		{
			// If we activate normal mapping now whereas it was off previously, we will have to trigger
			// a new tuning, because this has an impact on the primitives themselves (which need tangents
			// in such case - see code in #loadPrimitive (search 'needsTangents' and 'hasTangents').
			if (this->Channel == SDK::Core::EChannelType::Normal
				&& !CurrentMap.HasTexture()
				&& this->NewValue.HasTexture())
			{
				return true;
			}
			return false;
		}
	};


	// For now UV transformation is global: applies to all textures in the material
	class MaterialUVTransformHelper : public MaterialParamHelperBase<SDK::Core::ITwinUVTransform>
	{
		using Super = MaterialParamHelperBase<SDK::Core::ITwinUVTransform>;

	public:
		using ParamType = SDK::Core::ITwinUVTransform;

		static EITwinMaterialParamType EditedType() { return EITwinMaterialParamType::UVTransform; }

		MaterialUVTransformHelper(BeUtils::GltfMaterialHelper& GltfHelper,
			SDK::Core::ITwinUVTransform const& NewUVTsf)
			: Super(GltfHelper, SDK::Core::EChannelType::ENUM_END, NewUVTsf)
		{}

		ParamType GetCurrentValue(uint64_t MaterialId) const
		{
			return GltfMatHelper.GetUVTransform(MaterialId);
		}

		bool WillRequireNewCesiumTexture(ParamType const& /*CurrentValue*/, uint64_t /*MaterialId*/) const
		{
			// Obviously not (not a map)
			return false;
		}

		void SetNewValue(uint64_t MaterialId, bool& bModifiedValue) const
		{
			GltfMatHelper.SetUVTransform(MaterialId, this->NewValue, bModifiedValue);
		}

		void ApplyNewValueToScene(uint64_t MaterialId, FITwinSceneMapping& SceneMapping) const
		{
			SceneMapping.SetITwinMaterialUVTransform(MaterialId, this->NewValue);
		}

		bool NeedGltfTuning(ParamType const& /*CurrentMap*/) const
		{
			return false;
		}
	};


	class MaterialKindHelper : public MaterialParamHelperBase<SDK::Core::EMaterialKind>
	{
		using Super = MaterialParamHelperBase<SDK::Core::EMaterialKind>;

	public:
		using ParamType = SDK::Core::EMaterialKind;

		static EITwinMaterialParamType EditedType() { return EITwinMaterialParamType::Kind; }

		MaterialKindHelper(BeUtils::GltfMaterialHelper& GltfHelper,
			SDK::Core::EMaterialKind const& NewKind)
			: Super(GltfHelper, SDK::Core::EChannelType::ENUM_END, NewKind)
		{}

		ParamType GetCurrentValue(uint64_t MaterialId) const
		{
			return GltfMatHelper.GetMaterialKind(MaterialId);
		}

		bool WillRequireNewCesiumTexture(ParamType const& /*CurrentValue*/, uint64_t /*MaterialId*/) const
		{
			// Obviously not (not a map)
			return false;
		}

		void SetNewValue(uint64_t MaterialId, bool& bModifiedValue) const
		{
			GltfMatHelper.SetMaterialKind(MaterialId, this->NewValue, bModifiedValue);

			// When turning a material to glass, ensure we have some transparency
			if (bModifiedValue
				&& this->NewValue == SDK::Core::EMaterialKind::Glass
				&& std::fabs(GltfMatHelper.GetChannelIntensity(MaterialId, SDK::Core::EChannelType::Opacity) - 1.0) < 1e-4)
			{
				bool bSubValueModified(false);
				GltfMatHelper.SetChannelIntensity(MaterialId, SDK::Core::EChannelType::Opacity, 0.5, bSubValueModified);
				// Also set a default metallic factor
				GltfMatHelper.SetChannelIntensity(MaterialId, SDK::Core::EChannelType::Metallic, 0.5, bSubValueModified);
			}
		}

		void ApplyNewValueToScene(uint64_t /*MaterialId*/, FITwinSceneMapping& /*SceneMapping*/) const
		{
			ensureMsgf(false, TEXT("changing material kind requires a retuning"));
		}

		bool NeedGltfTuning(ParamType const& /*CurrentMap*/) const
		{
			return true;
		}
	};

	using ChannelBoolArray = std::array<bool, (size_t)EITwinChannelType::ENUM_END>;

	struct IntensityUpdateInfo
	{
		double Value = -1.;
		bool bHasChanged = false;
	};
	using ChannelIntensityInfos = std::array<IntensityUpdateInfo, (size_t)EITwinChannelType::ENUM_END>;

	using MapHelperPtr = std::unique_ptr<MaterialMapParamHelper>;

	inline bool HasHelperForChannel(std::vector<MapHelperPtr> const& MapHelpers, EITwinChannelType Channel)
	{
		return std::find_if(MapHelpers.begin(), MapHelpers.end(),
			[Channel](MapHelperPtr const& Helper) { return Helper->Channel == Channel; })
			!= MapHelpers.end();
	}

	inline bool HasCompanionChannel(std::vector<MapHelperPtr> const& MapHelpers, EITwinChannelType Channel)
	{
		auto const CompanionChan = GetGlTFCompanionChannel(Channel);
		return CompanionChan && HasHelperForChannel(MapHelpers, *CompanionChan);
	}
}

template <typename MaterialParamHelper>
void AITwinIModel::FImpl::TSetMaterialChannelParam(MaterialParamHelper const& Helper, uint64_t MaterialId)
{
	using ParameterType = typename MaterialParamHelper::ParamType;
	FITwinCustomMaterial* CustomMat = GetMutableCustomMaterials().Find(MaterialId);
	if (!ensureMsgf(CustomMat, TEXT("unknown material ID")))
	{
		return;
	}

	// Depending on the channel and its previous value, we may have to trigger a new glTF tuning.
	SDK::Core::EChannelType const Channel(Helper.Channel);
	bool const bTestNeedRetuning = (Helper.EditedType() == EITwinMaterialParamType::Map
		|| Helper.EditedType() == EITwinMaterialParamType::Kind
		|| Channel == SDK::Core::EChannelType::Transparency
		|| Channel == SDK::Core::EChannelType::Alpha);
	std::optional<ParameterType> CurrentValueOpt;
	// The MES does not produce any UVs when the original material does not have textures.
	// So we'll have to produce some the first time the user adds a texture.
	// Also, the actual presence of texture does have an impact on the way UV coordinates are stored, so we
	// need to re-tune as soon as we detect *new* requirement for a Cesium texture
	bool bNeedGenerateUVs = false;
	if (bTestNeedRetuning)
	{
		// Store initial value before changing it (see test below).
		CurrentValueOpt = Helper.GetCurrentValue(MaterialId);

		if (Helper.EditedType() == EITwinMaterialParamType::Map
			&& Helper.WillRequireNewCesiumTexture(*CurrentValueOpt, MaterialId))
		{
			bNeedGenerateUVs = true;
		}
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
	if (bTestNeedRetuning)
	{
		bNeedGltfTuning = bNeedGltfTuning || bNeedGenerateUVs;
		bNeedGltfTuning = bNeedGltfTuning || Helper.NeedGltfTuning(*CurrentValueOpt);
	}

	// Make sure the new value is applied to the Unreal mesh
	if (bNeedGltfTuning)
	{
		// The whole tileset will be reloaded with updated materials.
		// Here we enforce a Retune in all cases, because of the potential switch opaque/translucent, or the
		// need for tangents in case of normal mapping.
		SplitGltfModelForCustomMaterials(true);
	}
	else
	{
		// No need to rebuild the full tileset. Instead, change the corresponding parameter in the
		// Unreal material instance, using the mapping.
		Helper.ApplyNewValueToScene(MaterialId, GetInternals(Owner).SceneMapping);
	}
}

void AITwinIModel::SetMaterialChannelIntensity(uint64_t MaterialId, SDK::Core::EChannelType Channel, double Intensity)
{
	MaterialIntensityHelper Helper(*Impl->GltfMatHelper, Channel, Intensity);
	Impl->TSetMaterialChannelParam(Helper, MaterialId);
}

FLinearColor AITwinIModel::GetMaterialChannelColor(uint64_t MaterialId, SDK::Core::EChannelType Channel) const
{
	auto const Color = Impl->GltfMatHelper->GetChannelColor(MaterialId, Channel);
	return { (float)Color[0], (float)Color[1], (float)Color[2], (float)Color[3] };
}

void AITwinIModel::SetMaterialChannelColor(uint64_t MaterialId, SDK::Core::EChannelType Channel,
										   FLinearColor const& Color)
{
	MaterialColorHelper Helper(*Impl->GltfMatHelper, Channel,
							   SDK::Core::ITwinColor{ Color.R, Color.G, Color.B, Color.A });
	Impl->TSetMaterialChannelParam(Helper, MaterialId);
}

FString AITwinIModel::FImpl::GetMaterialChannelColorTextureID(uint64_t MaterialId, SDK::Core::EChannelType Channel,
	SDK::Core::ETextureSource& OutSource) const
{
	auto const ChanMap = GltfMatHelper->GetChannelColorMap(MaterialId, Channel);
	OutSource = ChanMap.eSource;
	return UTF8_TO_TCHAR(ChanMap.texture.c_str());
}

FString AITwinIModel::FImpl::GetMaterialChannelIntensityTextureID(uint64_t MaterialId, SDK::Core::EChannelType Channel,
	SDK::Core::ETextureSource& OutSource) const
{
	auto const ChanMap = GltfMatHelper->GetChannelIntensityMap(MaterialId, Channel);
	OutSource = ChanMap.eSource;
	return UTF8_TO_TCHAR(ChanMap.texture.c_str());
}

FString AITwinIModel::GetMaterialChannelTextureID(uint64_t MaterialId, SDK::Core::EChannelType Channel,
	SDK::Core::ETextureSource& OutSource) const
{
	// Distinguish color from intensity textures.
	if (Channel == SDK::Core::EChannelType::Color
		|| Channel == SDK::Core::EChannelType::Normal)
	{
		return Impl->GetMaterialChannelColorTextureID(MaterialId, Channel, OutSource);
	}
	else
	{
		// For other channels, the map defines an intensity
		return Impl->GetMaterialChannelIntensityTextureID(MaterialId, Channel, OutSource);
	}
}

void AITwinIModel::FImpl::SetMaterialChannelColorTextureID(uint64_t MaterialId, SDK::Core::EChannelType Channel,
	FString const& TextureId, SDK::Core::ETextureSource eSource)
{
	SDK::Core::ITwinChannelMap NewMap;
	NewMap.texture = TCHAR_TO_UTF8(*TextureId);
	NewMap.eSource = eSource;
	MaterialColorMapHelper Helper(Owner, GltfMatHelper, Channel, NewMap);
	TSetMaterialChannelParam(Helper, MaterialId);
}

void AITwinIModel::FImpl::SetMaterialChannelIntensityTextureID(uint64_t MaterialId, SDK::Core::EChannelType Channel,
	FString const& TextureId, SDK::Core::ETextureSource eSource)
{
	SDK::Core::ITwinChannelMap NewMap;
	NewMap.texture = TCHAR_TO_UTF8(*TextureId);
	NewMap.eSource = eSource;
	MaterialIntensityMapHelper Helper(Owner, GltfMatHelper, Channel, NewMap);
	TSetMaterialChannelParam(Helper, MaterialId);
}

void AITwinIModel::SetMaterialChannelTextureID(uint64_t MaterialId, SDK::Core::EChannelType Channel,
	FString const& TextureId, SDK::Core::ETextureSource eSource)
{
	// Distinguish color from intensity textures.
	if (Channel == SDK::Core::EChannelType::Color
		|| Channel == SDK::Core::EChannelType::Normal)
	{
		Impl->SetMaterialChannelColorTextureID(MaterialId, Channel, TextureId, eSource);
	}
	else
	{
		// For other channels, the map defines an intensity
		Impl->SetMaterialChannelIntensityTextureID(MaterialId, Channel, TextureId, eSource);
	}
}

UTexture2D* AITwinIModel::GetDefaultTextureForChannel(SDK::Core::EChannelType Channel) const
{
	switch (Channel)
	{
	case SDK::Core::EChannelType::Color:
	case SDK::Core::EChannelType::Alpha:
	case SDK::Core::EChannelType::Transparency:
	case SDK::Core::EChannelType::AmbientOcclusion:
		// Btw the 'NoColorTexture' is used as default texture for more than just color...
		// You can see it in CesiumGlTFFunction (and MF_ITwinCesiumGlTF) material function
		return NoColorTexture;

	case SDK::Core::EChannelType::Normal:
		return NoNormalTexture;

	case SDK::Core::EChannelType::Metallic:
	case SDK::Core::EChannelType::Roughness:
		return NoMetallicRoughnessTexture;

	default:
		BE_ISSUE("No default texture for channel", (uint8_t)Channel);
		return NoColorTexture;
	}
}

SDK::Core::ITwinUVTransform AITwinIModel::GetMaterialUVTransform(uint64_t MaterialId) const
{
	return Impl->GltfMatHelper->GetUVTransform(MaterialId);
}

void AITwinIModel::SetMaterialUVTransform(uint64_t MaterialId, SDK::Core::ITwinUVTransform const& UVTransform)
{
	MaterialUVTransformHelper Helper(*Impl->GltfMatHelper, UVTransform);
	Impl->TSetMaterialChannelParam(Helper, MaterialId);
}

SDK::Core::EMaterialKind AITwinIModel::GetMaterialKind(uint64_t MaterialId) const
{
	return Impl->GltfMatHelper->GetMaterialKind(MaterialId);
}

void AITwinIModel::SetMaterialKind(uint64_t MaterialId, SDK::Core::EMaterialKind NewKind)
{
	MaterialKindHelper Helper(*Impl->GltfMatHelper, NewKind);
	Impl->TSetMaterialChannelParam(Helper, MaterialId);
}

bool AITwinIModel::SetMaterialName(uint64_t MaterialId, FString const& NewName)
{
	return Impl->SetMaterialName(MaterialId, NewName);
}

bool AITwinIModel::FImpl::LoadMaterialFromAssetFile(uint64_t MaterialId, FString const& AssetFilePath)
{
	using namespace SDK::Core;
	using EITwinChannelType = SDK::Core::EChannelType;

	FITwinCustomMaterial* CustomMat = GetMutableCustomMaterials().Find(MaterialId);
	if (!ensureMsgf(CustomMat, TEXT("unknown material ID")))
	{
		return false;
	}
	auto const& MatIOMngr = GetMaterialPersistenceManager();
	if (!ensureMsgf(MatIOMngr, TEXT("no material persistence manager")))
	{
		return false;
	}

	ITwinMaterial NewMaterial;
	TextureKeySet NewTextures;
	if (!FITwinMaterialLibrary::LoadMaterialFromAssetPath(AssetFilePath, NewMaterial, NewTextures))
	{
		return false;
	}
	// If no color map is defined, ensure we use the NONE_TEXTURE tag in order to *remove* any imported
	// iTwin color texture.
	if (!NewMaterial.GetChannelMapOpt(EITwinChannelType::Color))
	{
		NewMaterial.SetChannelColorMap(EITwinChannelType::Color,
			ITwinChannelMap{
				.texture = NONE_TEXTURE,
				.eSource = ETextureSource::Library
			});
	}

	ITwinMaterial CurMaterial;
	if (!GltfMatHelper->GetMaterialFullDefinition(MaterialId, CurMaterial))
	{
		return false;
	}
	if (CurMaterial == NewMaterial)
	{
		// Avoid useless glTF splitting or DB invalidation.
		return true;
	}

	// Resolve the textures, if any (they should all exist in the Material Library...)
	if (!NewTextures.empty())
	{
		// Use maps with just one ID here...
		PerIModelTextureSet PerModelTextures;
		TMap<FString, TWeakObjectPtr<AITwinIModel>> IDToIModel;
		PerModelTextures.emplace(TCHAR_TO_UTF8(*Owner.IModelId), NewTextures);
		IDToIModel.Emplace(Owner.IModelId, &Owner);
		if (!ITwin::ResolveDecorationTextures(
			*MatIOMngr, PerModelTextures, IDToIModel, Owner.GetAccessTokenStdString()))
		{
			return false;
		}
	}

	// Fetch current alpha mode before modifying the material, to detect a switch of translucency mode.
	std::string CurrentAlphaMode;
	{
		BeUtils::GltfMaterialHelper::Lock Lock(GltfMatHelper->GetMutex());
		GltfMatHelper->GetCurrentAlphaMode(MaterialId, CurrentAlphaMode, Lock);
	}

	GltfMatHelper->SetMaterialFullDefinition(MaterialId, NewMaterial);

	// Test the need for re-tuning.
	// Inspired by TSetMaterialChannelParam, but taking *all* material parameters into account.

	// First gather all changes compared to current definition
	ChannelBoolArray hasTex_Cur, hasTex_New;
	ChannelIntensityInfos newIntensities;
	std::vector<MapHelperPtr> MapHelpers;

	for (uint8_t i(0); i < (uint8_t)EITwinChannelType::ENUM_END; ++i)
	{
		EITwinChannelType const Channel = static_cast<EITwinChannelType>(i);

		// Scalar value.
		auto const IntensOpt_Cur = CurMaterial.GetChannelIntensityOpt(Channel);
		auto const IntensOpt_New = NewMaterial.GetChannelIntensityOpt(Channel);
		double const Intens_Cur = IntensOpt_Cur ? *IntensOpt_Cur
			: GltfMatHelper->GetChannelDefaultIntensity(Channel, {});
		double const Intens_New = IntensOpt_New ? *IntensOpt_New
			: GltfMatHelper->GetChannelDefaultIntensity(Channel, {});
		newIntensities[i] = {
			.Value = Intens_New,
			.bHasChanged = std::fabs(Intens_New - Intens_Cur) > 1e-5
		};

		// Texture value.
		auto const MapOpt_Cur = CurMaterial.GetChannelMapOpt(Channel);
		auto const MapOpt_New = NewMaterial.GetChannelMapOpt(Channel);
		hasTex_Cur[i] = MapOpt_Cur && MapOpt_Cur->HasTexture();
		hasTex_New[i] = MapOpt_New && MapOpt_New->HasTexture();

		if ((MapOpt_New != MapOpt_Cur)
			/* Beware the groups { color, alpha } and { metallic, roughness }... */
			&& !HasCompanionChannel(MapHelpers, Channel))
		{
			// We will reuse code from MaterialMapParamHelper to apply texture changes.
			if (Channel == EITwinChannelType::Color || Channel == EITwinChannelType::Normal)
			{
				MapHelpers.emplace_back(std::make_unique<MaterialColorMapHelper>(
					Owner, GltfMatHelper, Channel,
					MapOpt_New.value_or(ITwinChannelMap{}))
				);
			}
			else
			{
				MapHelpers.emplace_back(std::make_unique<MaterialIntensityMapHelper>(
					Owner, GltfMatHelper, Channel,
					MapOpt_New.value_or(ITwinChannelMap{}))
				);
			}
		}
	}

	bool bNeedTranslucentMat(false);
	for (auto const& MapHelper : MapHelpers)
	{
		MapHelper->BuildMergedTexture(MaterialId);
		bNeedTranslucentMat |= MapHelper->NeedTranslucency();
	}

	// Detect translucency switch (induced by either map or intensity scalar value)
	bool bDifferingTranslucency = (bNeedTranslucentMat != (CurrentAlphaMode == CesiumGltf::Material::AlphaMode::BLEND));
	if (!bDifferingTranslucency)
	{
		double const alpha_Cur = CurMaterial.GetChannelIntensityOpt(EITwinChannelType::Alpha).value_or(1.);
		double const alpha_New = NewMaterial.GetChannelIntensityOpt(EITwinChannelType::Alpha).value_or(1.);
		bDifferingTranslucency = ((std::fabs(1 - alpha_Cur) > 1e-5) != (std::fabs(1 - alpha_New) > 1e-5));
	}

	// Detect a change in the presence of a texture in channels supported by Cesium, taking
	// "companion" channels into account as done in WillRequireNewCesiumTexture
	auto const completeCompanionChannels = [](ChannelBoolArray& hasTex)
	{
		bool const hasColorOrAlpha = hasTex[(uint8_t)EITwinChannelType::Color]
			|| hasTex[(uint8_t)EITwinChannelType::Alpha];
		hasTex[(uint8_t)EITwinChannelType::Color] = hasColorOrAlpha;
		hasTex[(uint8_t)EITwinChannelType::Alpha] = hasColorOrAlpha;

		bool const hasMetallicRough = hasTex[(uint8_t)EITwinChannelType::Metallic]
			|| hasTex[(uint8_t)EITwinChannelType::Roughness];
		hasTex[(uint8_t)EITwinChannelType::Metallic] = hasMetallicRough;
		hasTex[(uint8_t)EITwinChannelType::Roughness] = hasMetallicRough;
	};

	completeCompanionChannels(hasTex_Cur);
	completeCompanionChannels(hasTex_New);
	bool const bDifferingTextureSlots = (hasTex_Cur != hasTex_New);


	// If this is the first time we edit this material, we will have to request a new glTF tuning.
	bool const bNeedGltfTuning = !CustomMat->bAdvancedConversion
		|| bDifferingTranslucency
		|| bDifferingTextureSlots
		|| (NewMaterial.kind != CurMaterial.kind);

	CustomMat->bAdvancedConversion = true;

	// Make sure the new value is applied to the Unreal mesh
	if (bNeedGltfTuning)
	{
		// The whole tileset will be reloaded with updated materials.
		SplitGltfModelForCustomMaterials(true);
	}
	else
	{
		// No need for re-tuning. Just apply each parameter to the existing Unreal material instances.
		FITwinSceneMapping& SceneMapping = GetInternals(Owner).SceneMapping;
		for (size_t i(0); i < newIntensities.size(); ++i)
		{
			EITwinChannelType const Channel = static_cast<EITwinChannelType>(i);
			if (newIntensities[i].bHasChanged)
			{
				SceneMapping.SetITwinMaterialChannelIntensity(MaterialId, Channel, newIntensities[i].Value);
			}
		}
		for (auto const& MapHelper : MapHelpers)
		{
			MapHelper->ApplyNewValueToScene(MaterialId, SceneMapping);
		}
		SceneMapping.SetITwinMaterialChannelColor(MaterialId, EITwinChannelType::Color,
			GltfMatHelper->GetChannelColor(MaterialId, EITwinChannelType::Color));
		SceneMapping.SetITwinMaterialUVTransform(MaterialId, NewMaterial.uvTransform);
	}

	if (!NewMaterial.displayName.empty())
	{
		CustomMat->Name = UTF8_TO_TCHAR(NewMaterial.displayName.c_str());
	}

	return true;
}



bool AITwinIModel::LoadMaterialFromAssetFile(uint64_t MaterialId, FString const& AssetFilePath)
{
	return Impl->LoadMaterialFromAssetFile(MaterialId, AssetFilePath);
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

void AITwinIModel::OnIModelOffsetChanged()
{
	// Bounding boxes have always been stored in Unreal world space AS IF the iModel were untransformed
	// (see "Note 2" in FITwinSceneMappingBuilder::OnMeshConstructed), but this call _is_ necessary to
	// reset the SceneMapping (esp. re-extract all translucent/transformed Elements to relocate them
	// correctly). Getting rid of this may be possible by manually relocating extracted Elements (but of
	// no use if we are to "soon" switch to retuning to replace extraction...)
	if (GetTileset())
		RefreshTileset();

	// Timelines store 3D paths keyframes as fully transformed Unreal-space coords (ie. including a possible
	// iModel transformation aka "offset"), directly in Add3DPathTransformToTimeline.
	// But even cut planes and static transforms, which are not immediately transformed to the final Unreal
	// world space, are then processed ("finalized") once in FinalizeCuttingPlaneEquation and
	// FinalizeAnchorPos, and these methods also apply the final iModel transformation, and are irreversible.
	// So getting rid of this is not so easy :/
	if (IsValid(Synchro4DSchedules))
		Synchro4DSchedules->ResetSchedules();
}

void AITwinIModel::SetModelOffset(FVector Pos, FVector Rot)
{
	SetActorLocationAndRotation(Pos, FQuat::MakeFromEuler(Rot));
	OnIModelOffsetChanged();
	if (!Impl->DecorationPersistenceMgr)
		Impl->FindPersistenceMgr();
	if (Impl->DecorationPersistenceMgr)
	{
		auto ss = Impl->DecorationPersistenceMgr->GetSceneInfo(EITwinModelType::IModel, IModelId);
		if (!ss.Offset.has_value() || !ss.Offset->Equals(GetTransform()))
		{
			ss.Offset = GetTransform();
			Impl->DecorationPersistenceMgr->SetSceneInfo(EITwinModelType::IModel, IModelId, ss);
		}
	}
}

void AITwinIModel::LoadMaterialMLPrediction()
{
	if (!ITwin::IsMLMaterialPredictionEnabled())
	{
		BE_LOGE("ITwinAPI", "ML Material Prediction feature is disabled");
		return;
	}
	if (IModelId.IsEmpty() || ITwinId.IsEmpty())
	{
		BE_LOGE("ITwinAPI", "IModelId and ITwinId are required to start material predictions");
		return;
	}
	if (!MaterialMLPredictionWS)
	{
		// Instantiate a custom WebService actor, as the MaterialAssignment service is currently hosted on
		// a dedicated server, not part of iTwin... This is (probably) temporary...
		UpdateWebServices();
		if (ServerConnection)
		{
			MaterialMLPredictionWS = NewObject<UITwinWebServices>(this);
			// Setup for this specific feature
			MaterialMLPredictionWS->SetupForMaterialMLPrediction();
			MaterialMLPredictionWS->SetServerConnection(ServerConnection);
			MaterialMLPredictionWS->SetObserver(this);
		}
		else
		{
			BE_LOGE("ITwinAPI", "No server connection to initiate material predictions");
			return;
		}
	}
	if (MaterialMLPredictionWS)
	{
		MLMaterialPredictionStatus = MaterialMLPredictionWS->GetMaterialMLPrediction(ITwinId,
			IModelId, GetSelectedChangeset());
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
	Impl->TestExportCompletionAfterDelay(InExportId, 3.f); // 3s
}

void AITwinIModel::FImpl::TestExportCompletionAfterDelay(FString const& InExportId, float DelayInSeconds)
{
	// Create a ticker to test the new export completion.
	// Note: 'This' used to be a(n anonymous) TStrongObjectPtr, to prevent it from being GC'd, and avoid the
	// test on IsValid, but it apparently does not work when leaving PIE, only when switching Levels, etc.
	// The comment in ITwinGeoLocation.h also hints that preventing a Level from unloading by keeping strong
	// ptr is not a good idea, hence the weak ptr here.
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([InExportId, This = TWeakObjectPtr<AITwinIModel>(&Owner)](float Delta)
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

void AITwinIModel::ShowConstructionData(bool bShow, ULightComponent* SunLight /*=nullptr*/)
{
	bShowConstructionData = bShow;
	GetInternals(*this).HideElements(
		bShowConstructionData ? std::unordered_set<ITwinElementID>()
						  : GetInternals(*this).SceneMapping.ConstructionDataElements(),
		true);
	if (SunLight)
	{
		//Didn't find any other way to force an update of the shadows
		//than to fake an update of the sun's rotation (roll doesn't seem to be used anyway)
		//tried using MarkRenderStateDirty() but didn't have an effect alone
		//Also, using a timer here to delay the update a bit as it doesn't work the first time ShowConstruction() is called
		//(maybe because the update occurs before we actually hide anything)
		float BlendTime = 0.2;
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(TimerHandle, FTimerDelegate::CreateLambda([=, this, _ = TStrongObjectPtr<AITwinIModel>(this)]
			{
				FRotator SunRot = SunLight->GetComponentRotation();
				SunRot.Roll += 0.0001;
				SunLight->SetWorldRotation(SunRot);
			}), BlendTime, false);
	}
}

void AITwinIModel::OnSavedViewInfosRetrieved(bool bSuccess, FSavedViewInfos const& SavedViews)
{
	if (!bSuccess)
		return;
	//clean IModel saved view children that have already been added
	const decltype(Children) ChildrenCopy = Children;
	std::unordered_set<FString> Ids;
	Ids.reserve(SavedViews.SavedViews.Num());
	for (const auto& info : SavedViews.SavedViews)
		Ids.insert(info.Id);
	for (auto& Child : ChildrenCopy)
	{
		if (AITwinSavedView* SavedView = Cast<AITwinSavedView>(Child))
		{
			if (std::find(Ids.begin(), Ids.end(), SavedView->SavedViewId) != Ids.end())
				GetWorld()->DestroyActor(Child);
		}
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

	if (!Impl->DecorationPersistenceMgr)
		Impl->FindPersistenceMgr();
	if (Impl->DecorationPersistenceMgr)
	{
		auto ss = Impl->DecorationPersistenceMgr->GetSceneInfo(EITwinModelType::IModel, IModelId);
		if (!ss.Visibility.has_value() || *ss.Visibility != !bHide)
		{
			ss.Visibility = !bHide;
			Impl->DecorationPersistenceMgr->SetSceneInfo(EITwinModelType::IModel, IModelId, ss);
		}
	}
}

bool AITwinIModel::IsTilesetHidden()
{
	auto tileset = GetTileset();
	if (!tileset)
		return false;
	return tileset->IsHidden();
}

void AITwinIModel::SetMaximumScreenSpaceError(double InMaximumScreenSpaceError)
{
	for (auto& Child : Children)
		if (auto* Tileset = Cast<AITwinCesium3DTileset>(Child.Get()))
			Tileset->SetMaximumScreenSpaceError(InMaximumScreenSpaceError);
}

void AITwinIModel::SetTilesetQuality(float Value)
{
	SetMaximumScreenSpaceError(ITwin::ToScreenSpaceError(Value));
	if (!Impl->DecorationPersistenceMgr)
		Impl->FindPersistenceMgr();
	if (Impl->DecorationPersistenceMgr)
	{
		auto ss = Impl->DecorationPersistenceMgr->GetSceneInfo(EITwinModelType::IModel, IModelId);
		if (!ss.Quality.has_value() || fabs(*ss.Quality - Value) > 1e-5)
		{
			ss.Quality = Value;
			Impl->DecorationPersistenceMgr->SetSceneInfo(EITwinModelType::IModel, IModelId, ss);
		}
	}
}



float AITwinIModel::GetTilesetQuality() const
{
	AITwinCesium3DTileset const* Tileset = GetTileset();
	if (Tileset)
	{
		return ITwin::GetTilesetQuality(*Tileset);
	}
	else
	{
		return 0.f;
	}
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

void AITwinIModel::OnSceneLoaded(bool success)
{

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
		WebServices->AddSavedView(ITwinId, NewSavedView, {TEXT(""), displayName, true}, IModelId);
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
	Impl->DestroyTileset();
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
			Impl->Internals.SceneMapping.SetIModel2UnrealTransfos(*this);
			// Also make sure we reload material info from the tuner
			Impl->bHasFilledMaterialInfoFromTuner = false;
			Tileset->SetMeshBuildCallbacks(Impl->SceneMappingBuilder);
			Tileset->SetGltfTuner(Impl->GltfTuner);
			Tileset->RefreshTileset();
		}
	}
}

void AITwinIModel::BeginDestroy()
{
	if (MaterialMLPredictionWS)
	{
		// See comment in AITwinServiceActor::BeginDestroy...
		MaterialMLPredictionWS->SetObserver(nullptr);
	}
	Super::BeginDestroy();
}

void AITwinIModel::Destroyed()
{
	Super::Destroyed();
	if (Impl->OnTilesetLoadFailureHandle.IsValid())
	{
		OnCesium3DTilesetLoadFailure.Remove(Impl->OnTilesetLoadFailureHandle);
	}
	const decltype(Children) ChildrenCopy = Children;
	for (auto& Child: ChildrenCopy)
		GetWorld()->DestroyActor(Child);
}

void AITwinIModel::FImpl::UpdateAfterLoadingUIEvent()
{
	if (Owner.LoadingMethod == ELoadingMethod::LM_Manual)
	{
		if (Owner.ExportId.IsEmpty())
		{
			Owner.UpdateIModel();
		}
		else
		{
			DestroyTileset();
			Owner.LoadModel(Owner.ExportId);
		}
	}
	else if (Owner.LoadingMethod == ELoadingMethod::LM_Automatic
		&& !Owner.IModelId.IsEmpty()
		&& !Owner.ChangesetId.IsEmpty())
	{
		AutoExportAndLoad();
	}
}

void AITwinIModel::UpdateOnSuccessfulAuthorization()
{
	switch (Impl->PendingOperation)
	{
	case FImpl::EOperationUponAuth::Load:
		Impl->UpdateAfterLoadingUIEvent();
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

void AITwinIModel::FImpl::OnLoadingUIEvent()
{
	// If no access token has been retrieved yet, make sure we request an authentication and then
	// process the actual loading request(s).
	if (Owner.CheckServerConnection() != SDK::Core::EITwinAuthStatus::Success)
	{
		PendingOperation = FImpl::EOperationUponAuth::Load;
		return;
	}
	UpdateAfterLoadingUIEvent();
}

void AITwinIModel::ToggleMLMaterialPrediction(bool bActivate)
{
	this->bActivateMLMaterialPrediction = bActivate;
	if (this->bActivateMLMaterialPrediction)
	{
		// Start retrieving material predictions from the service. They will be visualized later, when the
		// result callback is executed (see #OnMatMLPredictionRetrieved).
		LoadMaterialMLPrediction();
	}
	else
	{
		// Revert to default visualization
		Impl->SplitGltfModelForCustomMaterials(true);
		// For now, a full refresh is needed (which has the drawback to hide the tileset for a while). The
		// reason behind is that the materials used by the primitives are those defined by the different
		// groups of elements defined by the ML inference, and since the tuning is incremental, we cannot
		// easily "undo" this material assignment and recover the original materials as defined in the
		// initial model. So we must restart the tuning from the beginning...
		RefreshTileset();
	}
}

void AITwinIModel::SetMaterialMLPredictionObserver(IITwinWebServicesObserver* observer)
{
	Impl->MLPredictionMaterialObserver = observer;
}

IITwinWebServicesObserver* AITwinIModel::GetMaterialMLPredictionObserver() const
{
	return Impl->MLPredictionMaterialObserver;
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
		Impl->OnLoadingUIEvent();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AITwinIModel, bShowConstructionData))
	{
		ShowConstructionData(bShowConstructionData);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(AITwinIModel, bActivateMLMaterialPrediction))
	{
		ToggleMLMaterialPrediction(bActivateMLMaterialPrediction);
	}
}

#endif // WITH_EDITOR

bool FITwinIModelInternals::IsElementHiddenInSavedView(ITwinElementID const Element) const
{
	for (const auto& SceneTile : SceneMapping.KnownTiles)
	{
		return SceneTile.IsElementHiddenInSavedView(Element);
	}
	return false;
}

void FITwinIModelInternals::OnVisibilityChanged(const Cesium3DTilesSelection::TileID& TileID, bool bVisible)
{
	auto* SceneTile = SceneMapping.FindKnownTileSLOW(TileID);
	// Actual processing is delayed because:
	// Short story: this is called from the middle of USceneComponent::SetVisibility, after the component's
	//		flag was set, but BEFORE it's children components' flags are set! (when applied recursively)
	// Long story: in the case of UITwinExtractedMeshComponent, the attachment hierarchy is thus...:
	//    Tileset --holds--> UITwinCesiumGltfComponent --holds--> UITwinCesiumGltfPrimitiveComponent
	// ...and when visibility is set from Cesium rules, this is the sequence of actions:
	//	1. UITwinCesiumGltfComponent::SetVisibility
	//	2. USceneComponent::SetVisibility sets bVisible to true
	//	3. USceneComponent::SetVisibility calls OnVisibilityChanged
	//	4. our OnVisibilityChanged callbacks are called, and we reach this method
	//	5. USceneComponent::SetVisibility calls SetVisibility on the children (attached) components
	// If Synchro4DSchedules->OnVisibilityChanged is called from here directly (ie at 4.), ApplyAnimation is
	// processed and thus extracted element's UITwinExtractedMeshComponent::SetFullyHidden methods are called
	// BEFORE the UITwinCesiumGltfPrimitiveComponent pParent below is thought to be visible! It would be
	// possible to check the grand-parent's visible flags instead of the parent's (see SetFullyHidden...), but
	// it would be an ugly assumption, and the whole situation felt really unsafe.
	if (SceneTile)
	{
		auto const TileRank = SceneMapping.KnownTileRank(*SceneTile);
		// not "emplace", need to overwrite when hiding then showing in same tick! (eg after a retune!)
		FITwinIModelImplAccess::Get(Owner).TilesChangingVisibility[TileRank] = bVisible;
	}
}

void AITwinIModel::FImpl::HandleTilesHavingChangedVisibility()
{
	for (auto const [TileRank, bVisible] : TilesChangingVisibility)
	{
		auto& SceneTile = Internals.SceneMapping.KnownTile(TileRank);
		if (bVisible != SceneTile.bVisible)
		{
			Internals.SceneMapping.OnVisibilityChanged(SceneTile, bVisible);
			if (Owner.Synchro4DSchedules)
			{
				Owner.Synchro4DSchedules->OnVisibilityChanged(SceneTile, bVisible);
			}
			SceneTile.bVisible = bVisible;
		}
	}
	TilesChangingVisibility.clear();
}

// TODO_GCO: to avoid remaining lags when loading a new tile, OnNewTileBuilt should have a time budget to
// stick to, and ApplyTimeline as well, in order to handle extraction in a non-blocking manner (at least to the
// granularity of individual Elements...). Then we could call ApplyTimeline from this method until it is
// finished, and notify the render-readiness only then.
void AITwinIModel::FImpl::HandleTilesRenderReadiness()
{
	decltype(Internals.TilesPendingRenderReadiness) StillNotReady;
	for (auto&& TileRank : Internals.TilesPendingRenderReadiness)
	{
		auto& SceneTile = Internals.SceneMapping.KnownTile(TileRank);
		if (SceneTile.IsLoaded() && ensure(SceneTile.pCesiumTile))
		{
			if (SceneTile.bIsSetupFor4DAnimation // also means ApplyAnimation has been called
				&& !SceneTile.Need4DAnimTexturesSetupInMaterials()
				&& !SceneTile.NeedSelectingAndHidingTexturesSetupInMaterials())
			{
				SceneTile.pCesiumTile->setRenderEngineReadiness(true);
			}
			else
				StillNotReady.insert(TileRank);
		}
	}
	Internals.TilesPendingRenderReadiness.swap(StillNotReady);
}

void AITwinIModel::PostLoad()
{
	Super::PostLoad();
	// Synchro4DSchedules will be destroyed! Maybe only when the saved component was unchanged from its CDO?
	// I think it is because the iModel CDO has a null Synchro4DSchedules, and the CDO kinda "forces itself"
	// (how? Diffing properties between saved CDO and current CDO??) on any new iModel instance, but many
	// methods being inlined (MarkAsGarbage, etc.) it's hardly possible to set a breakpoint to confirm what's
	// happening...
	// Note that if we want to preserve properties from the saved level's component, we can store them here
	// and restore them on the "final" component when it is recreated in  CreateSynchro4DSchedulesComponent
	if (Synchro4DSchedules) Synchro4DSchedules = nullptr;
	ensure(!Impl->bInitialized);
	Impl->bWasLoadedFromDisk = true;
}

void AITwinIModel::PostActorCreated()
{
	Super::PostActorCreated();
	SetActorLocation(FVector::ZeroVector);
}

AITwinIModel::~AITwinIModel()
{
	Impl->Internals.Uniniter->Run();
}

void AITwinIModel::EndPlay(const EEndPlayReason::Type EndPlayReason) /*override*/
{
	Impl->Internals.Uniniter->Run();
	Super::EndPlay(EndPlayReason);
}

void FITwinIModelInternals::OnNewTileBuilt(Cesium3DTilesSelection::TileID const& TileID)
{
	auto* SceneTile = SceneMapping.FindKnownTileSLOW(TileID);
	if (!ensure(SceneTile))
		return;
	SceneMapping.OnNewTileBuilt(*SceneTile);
	if (Owner.Synchro4DSchedules
		&& GetInternals(*Owner.Synchro4DSchedules).OnNewTileBuilt(*SceneTile))
	{
		TilesPendingRenderReadiness.insert(SceneMapping.KnownTileRank(*SceneTile));
	}
}

void FITwinIModelInternals::OnElementsTimelineModified(FITwinElementTimeline& ModifiedTimeline,
	std::vector<ITwinElementID> const* OnlyForElements/*= nullptr*/)
{
	UITwinSynchro4DSchedules* Schedules = Owner.FindComponentByClass<UITwinSynchro4DSchedules>();
	if (!Schedules)
		return;
	auto&& SchedInternals = GetInternals(*Schedules);
	SchedInternals.Timeline().OnElementsTimelineModified(ModifiedTimeline);
	if (!SchedInternals.PrefetchAllElementAnimationBindings())
	{
		SceneMapping.ForEachKnownTile([&](FITwinSceneTile& SceneTile)
		{
			SceneMapping.OnElementsTimelineModified(SceneTile, ModifiedTimeline, OnlyForElements);
		});
	}
}

namespace
{
	static bool bSelectUponClickInViewport = true;
	static bool bLogTimelineUponSelectElement = false;
	static bool bLogPropertiesUponSelectElement = true;
	static bool bLogTileUponSelectElement = false;
}

bool FITwinIModelInternals::OnClickedElement(ITwinElementID const Element,
											 FHitResult const& HitResult)
{
	if (bSelectUponClickInViewport && !SelectVisibleElement(Element))
		return false; // filtered out internally, most likely Element is masked out
	DescribeElement(Element, HitResult.Component);
	return true;
}

void FITwinIModelInternals::DescribeElement(ITwinElementID const Element,
											TWeakObjectPtr<UPrimitiveComponent> HitComponent /*= {}*/)
{
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
		DrawDebugBox(Owner.GetWorld(), Center, Extent, FColor::Green, /*bool bPersistent =*/ false,
			/*float LifeTime =*/ 10.f);
	}
#endif // ENABLE_DRAW_DEBUG

	if (HitComponent.IsValid())
	{
#if ENABLE_DRAW_DEBUG
		// Draw the GLTF primitive bounding box
		DrawDebugBox(Owner.GetWorld(), HitComponent->Bounds.Origin, HitComponent->Bounds.BoxExtent,
			FColor::Blue, /*bool bPersistent =*/ false, /*float LifeTime =*/ 10.f);
#endif // ENABLE_DRAW_DEBUG

		auto const* SceneTile = SceneMapping.FindOwningTile(HitComponent.Get());
		if (SceneTile)
		{
			// Display the owning tile's bounding box
#if ENABLE_DRAW_DEBUG
			ITwinDebugBoxNextLifetime = 5.f;
#endif // ENABLE_DRAW_DEBUG
			SceneTile->DrawTileBox(Owner.GetWorld());
			// Log the Tile ID
			FString const TileIdString(Cesium3DTilesSelection::TileIdUtilities::createTileIdString(
				SceneTile->TileID).c_str());
			if (bLogTileUponSelectElement)
			{
				UE_LOG(LogITwin, Display, TEXT("%s"), *SceneTile->ToString());
			}
			else
			{
				UE_LOG(LogITwin, Display, TEXT("Owning Tile: %s"), *TileIdString);
			}
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

	if (bLogPropertiesUponSelectElement)
	{
		Owner.GetMutableWebServices()->GetElementProperties(Owner.ITwinId, Owner.IModelId,
			Owner.GetSelectedChangeset(), ITwin::ToString(Element));
	}
	UITwinSynchro4DSchedules* Schedules = Owner.FindComponentByClass<UITwinSynchro4DSchedules>();
	if (!Schedules)
		return;
	FString const ElementTimelineDescription = GetInternals(*Schedules).ElementTimelineAsString(Element);
	if (ElementTimelineDescription.IsEmpty())
		return;
	if (bLogTimelineUponSelectElement)
	{
		UE_LOG(LogITwin, Display, TEXT("ElementID 0x%I64x has a timeline:\n%s"),
				Element.value(), *ElementTimelineDescription);
	}
	else
	{
		UE_LOG(LogITwin, Display, TEXT("ElementID 0x%I64x has a timeline (call \"cmd.ITwinTweakViewportClick logtimeline on\" to log it)"), Element.value());
	}
}

bool FITwinIModelInternals::SelectVisibleElement(ITwinElementID const InElementID)
{
	return SceneMapping.SelectVisibleElement(InElementID);
}

void FITwinIModelInternals::HideElements(std::unordered_set<ITwinElementID> const& InElementIDs,
										 bool IsConstruction)
{
	SceneMapping.HideElements(InElementIDs, IsConstruction);
}

ITwinElementID FITwinIModelInternals::GetSelectedElement() const
{
	return SceneMapping.GetSelectedElement();
}

void AITwinIModel::DeSelectElements()
{
	GetInternals(*this).SelectVisibleElement(ITwin::NOT_ELEMENT);
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
	static void ZoomOnIModelsOrElement(ITwinElementID const ElementID, UWorld* World,
		AITwinIModel *const InIModel = nullptr)
	{
		FBox FocusedBBox(ForceInitToZero);
		if (ITwin::NOT_ELEMENT == ElementID)
		{
			for (TActorIterator<AITwinIModel> IModelIter(World); IModelIter; ++IModelIter)
			{
				if (InIModel && InIModel != (*IModelIter))
					continue;
				FITwinIModel3DInfo OutInfo;
				(*IModelIter)->GetModel3DInfoInCoordSystem(OutInfo, EITwinCoordSystem::UE);
				FBox const IModelBBox(OutInfo.BoundingBoxMin, OutInfo.BoundingBoxMax);
				if (IModelBBox.IsValid)
				{
					if (FocusedBBox.IsValid) FocusedBBox += IModelBBox;
					else FocusedBBox = IModelBBox;
				}
				if (InIModel)
					break;
			}
		}
		else
		{
			for (TActorIterator<AITwinIModel> IModelIter(World); IModelIter; ++IModelIter)
			{
				if (InIModel && InIModel != (*IModelIter))
					continue;
				FBox const& ElemBBox = GetInternals(**IModelIter).SceneMapping.GetBoundingBox(ElementID);
				if (ElemBBox.IsValid)
				{
					if (FocusedBBox.IsValid) FocusedBBox += ElemBBox;
					else FocusedBBox = ElemBBox;
					break;
				}
			}
		}
		// When zooming on an Element, we want to go closer than 100 meters
		double const MinCamDist = (ITwin::NOT_ELEMENT == ElementID) ? 10000. : 500.;
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

// Console command to zoom on the supplied Element (assumed to be in the first iModel found), or on the first
// iModel's selected element, if not supplied and any Element is currently selected.
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinZoomOnSelectedElement(
	TEXT("cmd.ITwinZoomOnSelectedElement"),
	TEXT("Move the viewport pawn close to either the supplied Element, or to the first selected Element, if any. If animated, try to set the current time to when the Element is (partly) visible."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	ITwinElementID SelectedElement = ITwin::NOT_ELEMENT;
	AITwinIModel* InIModel = nullptr;
	if (Args.IsEmpty())
	{
		for (TActorIterator<AITwinIModel> IModelIter(World); IModelIter; ++IModelIter)
		{
			SelectedElement = GetInternals(**IModelIter).GetSelectedElement();
			if (SelectedElement != ITwin::NOT_ELEMENT)
			{
				InIModel = *IModelIter;
				break;
			}
		}
	}
	else
	{
		SelectedElement = ITwin::ParseElementID(Args[0]);
		for (TActorIterator<AITwinIModel> IModelIter(World); IModelIter; ++IModelIter)
		{
			if (GetInternals(**IModelIter).HasElementWithID(SelectedElement))
			{
				InIModel = *IModelIter;
				auto& IModelInt = GetInternals(*InIModel);
				auto const& SceneElem = IModelInt.SceneMapping.ElementForSLOW(SelectedElement);
				auto* Schedules = InIModel->FindComponentByClass<UITwinSynchro4DSchedules>();
				// If animated, try to set the current time to when the Element is (partly) visible
				if (Schedules && !SceneElem.AnimationKeys.empty())
				{
					auto* ElementTimeline = GetInternals(*Schedules).GetTimeline()
						.GetElementTimelineFor(SceneElem.AnimationKeys[0]);
					if (ElementTimeline)
					{
						auto const& Timerange = ElementTimeline->GetTimeRange();
						if (Timerange != ITwin::Time::Undefined())
						{
							Schedules->Pause();
							Schedules->SetScheduleTime(ITwin::Time::ToDateTime(
								.5 * (Timerange.first + Timerange.second)));
						}
					}
				}
				// Don't call OnClickedElement, it may well be masked out by the 4D animation
				IModelInt.SelectVisibleElement(SelectedElement);
				IModelInt.DescribeElement(SelectedElement);
				break;
			}
		}
		if (!InIModel)
			return;
	}
	if (SelectedElement != ITwin::NOT_ELEMENT)
	{
		ITwin::ZoomOnIModelsOrElement(SelectedElement, World, InIModel);
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

static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinDescribeTiles(
	TEXT("cmd.ITwinDescribeTiles"),
	TEXT("Look in all loaded iModels for tiles which ID matches (partially) the passed strings and logs a description for each (max 42 tiles). Also log stats for the SceneMapping struct itself"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	if (Args.IsEmpty())
	{
		UE_LOG(LogITwin, Error, TEXT("Pass at least one (partial) tile ID string"));
		return;
	}
	int tilesDumped = 0;
	for (TActorIterator<AITwinIModel> IModelIter(World); IModelIter; ++IModelIter)
	{
		auto const& IModelInt = GetInternals(**IModelIter);
		IModelInt.SceneMapping.ForEachKnownTile([&](FITwinSceneTile const& Tile)
			{
				if (tilesDumped == 42)
					return;
				FString const IdStr = FString(Cesium3DTilesSelection::TileIdUtilities::createTileIdString(
					Tile.TileID).c_str())/*.ToLower() <== Contains defaults to "ignore case"*/;
				auto const TileRank = IModelInt.SceneMapping.KnownTileRank(Tile);
				for (auto const& Arg : Args)
				{
					if (IdStr.Contains(Arg))
					{
						UE_LOG(LogITwin, Display, TEXT("Rank #%d, %s"), TileRank.value(), *Tile.ToString());
						++tilesDumped;
						if (tilesDumped == 42)
							break;
					}
				}
			});
		UE_LOG(LogITwin, Display, TEXT("%s's %s"), *IModelIter->GetActorNameOrLabel(),
												   *IModelInt.SceneMapping.ToString());
	}
}));

static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinTweakViewportClick(
	TEXT("cmd.ITwinTweakViewportClick"),
	TEXT("Configure what happens when clicking in the viewport, usually to select an Element: first arg must *contain* one or more of 'select', 'logtimeline' (to log timelines), 'logprop' (to log properties) and/or 'logtile' (to log tile impl details), second arg must be 0, 1, true, false, on or off."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	if (Args.Num() != 2)
	{
		UE_LOG(LogITwin, Error, TEXT("Need exactly 2 args"));
		return;
	}
	std::optional<bool> toggle;
	if (Args[1] == TEXT("1") || Args[1] == TEXT("true") || Args[1] == TEXT("on"))
		toggle = true;
	else if (Args[1] == TEXT("0") || Args[1] == TEXT("false") || Args[1] == TEXT("off"))
		toggle = false;
	if (!toggle)
	{
		UE_LOG(LogITwin, Error, TEXT("Second arg must be 0, 1, true, false, on or off"));
		return;
	}
	// "Contains" ignores case by default:
	bool const select = Args[0].Contains(TEXT("select"));
	bool const timeline = Args[0].Contains(TEXT("logtimeline"));
	bool const properties = Args[0].Contains(TEXT("logprop"));
	bool const tile = Args[0].Contains(TEXT("logtile"));
	if (!timeline && !properties && !select && !tile)
	{
		UE_LOG(LogITwin, Error, TEXT("First arg must *contain* one or more of 'select', 'logtimeline', 'logprop' and/or 'logtile'"));
		return;
	}
	if (select)
		bSelectUponClickInViewport = *toggle;
	if (properties)
		bLogPropertiesUponSelectElement = *toggle;
	if (timeline)
		bLogTimelineUponSelectElement = *toggle;
	if (tile)
		bLogTileUponSelectElement = *toggle;
	UE_LOG(LogITwin, Display, TEXT("Summary of flags: Select:%d LogProperties:%d LogTimelines:%d LogTiles:%d"),
							  bSelectUponClickInViewport, bLogPropertiesUponSelectElement,
							  bLogTimelineUponSelectElement, bLogTileUponSelectElement);
}));

#endif // ENABLE_DRAW_DEBUG
