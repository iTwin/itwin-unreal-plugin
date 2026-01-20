/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinIModel.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinIModel.h>

#include <IncludeCesium3DTileset.h>
#include <ITwinDigitalTwin.h>
#include <ITwinExtractedMeshComponent.h>
#include <ITwinGeoLocation.h>
#include <ITwinIModelInternals.h>
#include <ITwinIModel3DInfo.h>
#include <ITwinIModelSettings.h>
#include <ITwinMetadataConstants.h>
#include <ITwinSavedView.h>
#include <ITwinSceneMappingBuilder.h>
#include <ITwinServerConnection.h>
#include <ITwinServerEnvironment.h>
#include <ITwinSetupMaterials.h>
#include <ITwinSynchro4DAnimator.h>
#include <ITwinSynchro4DSchedules.h>
#include <ITwinSynchro4DSchedulesInternals.h>
#include <ITwinTilesetAccess.h>
#include <ITwinTilesetAccess.inl>
#include <ITwinUtilityLibrary.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <Clipping/ITwinClippingCustomPrimitiveDataHelper.h>
#include <Clipping/ITwinClippingTool.h>
#include <Decoration/ITwinDecorationHelper.h>
#include <Helpers/ITwinConsoleCommandUtils.h>
#include <Helpers/WorldSingleton.h>
#include <Material/ITwinIModelMaterialHandler.h>
#include <Material/ITwinMaterialDefaultTexturesHolder.h>
#include <Math/UEMathExts.h>
#include <Cesium3DTilesetLoadFailureDetails.h>
#include <CesiumWgs84Ellipsoid.h>
#include <Network/JsonQueriesCache.h>
#include <Timeline/Timeline.h>

#include <Components/DirectionalLightComponent.h>
#include <Components/LightComponent.h>
#include <Components/StaticMeshComponent.h>
#include <Containers/Ticker.h>
#include <Dom/JsonObject.h>
#include <DrawDebugHelpers.h>
#include <Engine/RendererSettings.h>
#include <EngineUtils.h>
#include <GameFramework/FloatingPawnMovement.h>
#include <GameFramework/Pawn.h>
#include <GameFramework/PlayerController.h>
#include <GameFramework/PlayerStart.h>
#include <HttpModule.h>
#include <Interfaces/IHttpResponse.h>
#include <JsonObjectConverter.h>
#include <Kismet/GameplayStatics.h>
#include <Materials/MaterialInstanceDynamic.h>
#include <Materials/MaterialInterface.h>
#include <Math/Box.h>
#include <Misc/CString.h>
#include <Misc/EngineVersionComparison.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>
#include <TimerManager.h>
#include <UObject/ConstructorHelpers.h>
#include <UObject/StrongObjectPtr.h>
#include <Engine/Engine.h>
#include <GameFramework/GameUserSettings.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeUtils/Gltf/GltfTuner.h>
#	include <BeUtils/Misc/MiscUtils.h>
#	include <Core/ITwinAPI/ITwinMaterial.h>
#	include <Core/ITwinAPI/ITwinTypes.h>
#	include <Core/Tools/Log.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <memory>
#include <optional>
#include <unordered_set>


namespace ITwin
{
	void SetupMaterials(FITwinTilesetAccess const& TilesetAccess, UITwinSynchro4DSchedules* SchedulesComp /*= nullptr*/)
	{
		if (!ensure(TilesetAccess.HasTileset()))
			return;
		ACesium3DTileset& Tileset = *TilesetAccess.GetMutableTileset();
		//===================================================================================
		// Prototype for global clipping (planes or box).
		auto ClippingTool = TWorldSingleton<AITwinClippingTool>().Get(Tileset.GetWorld());
		if (ClippingTool)
		{
			ClippingTool->RegisterTileset(TilesetAccess);
		}
		//===================================================================================

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

	[[nodiscard]] ITwinElementID ParseElementID(FString FromStr)
	{
		// Removed this workaround for an early bug in the 4D API, where some internal ID could be prepended
		// in the form "[0x100] 0x100000000031c": (Note: this func was in SchedulesImport.cpp at the time)
		//if (FromStr.FindChar(TCHAR('['), IdxOpen)) ...
		uint64 Parsed = ITwin::NOT_ELEMENT.value();
		errno = 0;
		if (FromStr.StartsWith(TEXT("0x")))//Note: StartsWith ignores case by default!
			Parsed = FCString::Strtoui64(*FromStr, nullptr, /*base*/16);
		else
			Parsed = FCString::Strtoui64(*FromStr, nullptr, /*base*/10);
		return (errno == 0) ? ITwinElementID(Parsed) : ITwin::NOT_ELEMENT;
	}

	[[nodiscard]] std::unordered_set<ITwinElementID> InsertParsedIDs(const std::vector<std::string>& inputIds) {
		std::unordered_set<ITwinElementID> res;
		res.reserve(inputIds.size());
		for (const auto& Id : inputIds)
		{
			ITwinElementID PickedID = ITwin::ParseElementID(Id.c_str());
			res.insert(PickedID);
		}
		return res;
	}

	[[nodiscard]] FString ToString(ITwinElementID const& Elem)
	{
		return FString::Printf(TEXT("0x%I64x"), Elem.value());
	}

	void IncrementElementID(FString& ElemStr)
	{
		ITwinElementID ElementID = ITwin::ParseElementID(ElemStr);
		ElementID++;
		ElemStr = ITwin::ToString(ElementID);
	}

	extern bool ShouldLoadScene(FString const& ITwinID, UWorld const* World);
	extern void LoadScene(FString const& ITwinID, UWorld* World);
	extern void SaveScene(FString const& ITwinID, UWorld const* World);

	void DestroyTilesetsInActor(AActor& Owner)
	{
		const decltype(Owner.Children) ChildrenCopy = Owner.Children;
		uint32 numDestroyed = 0;
		for (auto& Child : ChildrenCopy)
		{
			if (Cast<ACesium3DTileset>(Child.Get()))
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
			auto const* pTileset = Cast<const ACesium3DTileset>(Child.Get());
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

#if ENABLE_DRAW_DEBUG
	/// Whether we display some debug bounding boxes (per Element, Tile...) when picking an Element with the
	/// mouse. Can be activated through console command ITwinTweakViewportClick, only if ENABLE_DRAW_DEBUG is
	/// activated.
	static bool bDrawDebugBoxes = false;
#endif
}

class FITwinIModelGltfTuner : public BeUtils::GltfTuner
{
public:
	FITwinIModelGltfTuner(AITwinIModel const& InOwner)
		: Owner(InOwner)
	{

	}

private:
	AITwinIModel const& Owner;
};



class AITwinIModel::FTilesetAccess : public FITwinTilesetAccess
{
public:
	FTilesetAccess(AITwinIModel* InIModel);
	virtual TUniquePtr<FITwinTilesetAccess> Clone() const override;

	virtual ITwin::ModelDecorationIdentifier GetDecorationKey() const override;
	virtual AITwinDecorationHelper* GetDecorationHelper() const override;

	virtual void OnModelOffsetLoaded() const override;

	virtual void RefreshTileset() const override;

private:
	TWeakObjectPtr<AITwinIModel> IModel;
};


class AITwinIModel::FImpl : public FITwinIModelMaterialHandler
{
public:
	AITwinIModel& Owner;
	/// helper to fill/update SceneMapping.
	TStrongObjectPtr<UITwinSceneMappingBuilder> SceneMappingBuilder;
	/// helper to activate clipping effects in the mesh components.
	TStrongObjectPtr<UITwinClippingCustomPrimitiveDataHelper> ClippingHelper;
	bool bInitialized = false;
	bool bWasLoadedFromDisk = false;
	FITwinIModelInternals Internals;
	uint32 TilesetLoadedCount = 0;
	FDelegateHandle OnTilesetLoadFailureHandle;
	std::optional<FITwinExportInfo> ExportInfoPendingLoad;
	//! Will be initialized when the "get attached reality data" request is complete.
	std::optional<TArray<FString>> AttachedRealityDataIds;
	TMap<FString,TArray<FString>> ChildrenModelIds;
	TMap<FString, TArray<FString>> SubCategoryIds;
	//! Used when the "get attached reality data" request is not yet complete,
	//! to store all pending promises.
	TArray<TSharedRef<TPromise<TArray<FString>>>> AttachedRealityDataIdsPromises;
	TArray<TSharedRef<TPromise<TArray<FString>>>> ChildrenModelIdsPromises;
	TArray<TSharedRef<TPromise<TArray<FString>>>> SubCategoryIdsPromises;
	HttpRequestID GetAttachedRealityDataRequestId;
	/// Protects GetAttachedRealityDataRequestId but also AttachedRealityDataIdsPromises (see dtor)
	ITwinHttp::FMutex GetAttachedRealityDataMutex;
	HttpRequestID GetChildrenModelsRequestId;
	ITwinHttp::FMutex GetChildrenModelsRequestIdMutex;
	HttpRequestID GetSubCategoriesRequestId;
	ITwinHttp::FMutex GetSubCategoriesRequestIdMutex;
	HttpRequestID ConvertBBoxCenterToGeoCoordsRequestId;
	ITwinHttp::FMutex ConvertBBoxCenterToGeoCoordsRequestIdMutex;
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
		/// A single query now combines parent-child relationships, Source ID's, and FederatedGuid's
		Combined,
		// We need nothing more at the moment
		//SourceIdentifiers
	};
	AITwinDecorationHelper* DecorationPersistenceMgr = nullptr;
	std::unordered_map<ITwinScene::TileIdx, bool> TilesChangingVisibility;
	//std::optional<FTransform> LastIModelTransformUpdated; <== look it up to find comment about that
	std::optional<FTransform> LastTilesetTransformUpdated;

	static TWeakObjectPtr<ULightComponent> LightForForcedShadowUpdate;
	static float ForceShadowUpdateMaxEvery;
	bool bForcedShadowUpdate = false;
	static double LastForcedShadowUpdate;
	void ForceShadowUpdatesIfNeeded();

	FImpl(AITwinIModel& InOwner)
		: FITwinIModelMaterialHandler(), Owner(InOwner), Internals(InOwner)
	{
		SavedViewsPageByPage.Add("", FRetrieveSavedViewsPageByPage(InOwner));
	}
	~FImpl()
	{
		ITwinHttp::FLock Lock(GetAttachedRealityDataMutex);
		// If request hasn't completed, eg. when unloading level very soon after loading it, we need this:
		// Unfulfilled promises are considered programming errors (check() fails => crash), but you can also
		// call GetFuture() only once, so GetFuture().Reset() doesn't work either... SetValue() is safe here
		// because AttachedRealityDataIdsPromises is emptied when SetValue() is called after the request has
		// been completed.
		for (const auto& Promise: AttachedRealityDataIdsPromises)
			Promise->SetValue(TArray<FString>{});
	}

	void Initialize();
	void OnWorldDestroyed(UWorld* InWorld);
	void ResetSceneMapping();
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
		UITwinUtilityLibrary::ZoomOn(FocusBBox, World, MinDistanceToCenter);
	}

	// TODO_GCO: does creating the comp "live" like this, which means having no 4D comp in the CDO,
	// mean that imodels loaded from a level will lose the config on their schedules? (since I witnessed
	// that creating the comp defaultsubobject in the imodel's ctor only when !CDO led to having no Sched
	// comp in the Editor when lauched with a configured level! at least as the default script, did not try
	// loading another level afterwards)
	// Note: it can always be worked around by using some kind of level construction script, I guess?
	void CreateSynchro4DSchedulesComponent(std::shared_ptr<BeUtils::GltfTuner> const& Tuner)
	{
		if (IsValid(Owner.Synchro4DSchedules))
			return;
		Owner.Synchro4DSchedules = NewObject<UITwinSynchro4DSchedules>(&Owner,
			UITwinSynchro4DSchedules::StaticClass(),
			FName(*(Owner.GetActorNameOrLabel() + "_4DSchedules")));
		Owner.Synchro4DSchedules->RegisterComponent();
		GetInternals(*Owner.Synchro4DSchedules).SetGltfTuner(Tuner);
		SetupSynchro4DSchedules(*GetDefault<UITwinIModelSettings>());
		ACesium3DTileset* Tileset = Owner.GetTileset();
		// Note: this will trigger a refresh of the tileset, thus unloading and reloading all tiles,
		// so we don't need to bother updating the materials and meshes already received and displayed
		if (Tileset)
			SetupMaterials();
	}

	void SetupSynchro4DSchedules(UITwinIModelSettings const& Settings)
	{
		if (!IsValid(Owner.Synchro4DSchedules))
			return;
		auto& S4D = *Owner.Synchro4DSchedules;
		S4D.MaxTimelineUpdateMilliseconds = Settings.Synchro4DMaxTimelineUpdateMilliseconds;
		S4D.ScheduleQueriesServerPagination = Settings.Synchro4DQueriesDefaultPagination;
		S4D.ScheduleQueriesBindingsPagination = Settings.Synchro4DQueriesBindingsPagination;
		S4D.bUseGltfTunerInsteadOfMeshExtraction = Settings.bSynchro4DUseGltfTunerInsteadOfMeshExtraction;
		S4D.GlTFTranslucencyRule = Settings.Synchro4DGlTFTranslucencyRule;
		S4D.bDisableColoring = Settings.bSynchro4DDisableColoring;
		S4D.bDisableVisibilities = Settings.bSynchro4DDisableVisibilities;
		S4D.bDisablePartialVisibilities = Settings.bSynchro4DDisablePartialVisibilities;
		S4D.bDisableCuttingPlanes = Settings.bSynchro4DDisableCuttingPlanes;
		S4D.bDisableTransforms = Settings.bSynchro4DDisableTransforms;
		S4D.bStream4DFromAPIM = Settings.bSynchro4DUseAPIM;

#if UE_VERSION_OLDER_THAN(5, 5, 0)
		if (!ensure(!GetDefault<URendererSettings>()->bOrderedIndependentTransparencyEnable))
		{
			// see OIT-related posts in
			// https://forums.unrealengine.com/t/ue5-gpu-crashed-or-d3d-device-removed/524297/168:
			// it could be a problem with all transparencies (and "mask opacity"), not just cutting planes!
			BE_LOGE("ITwinRender", "bOrderedIndependentTransparencyEnable=true will crash cut planes, sorry! See if 'r.OIT.SortedPixels' is in your DefaultEngine.ini, in section [/Script/Engine.RendererSettings], if not, add it set to False (and relaunch the app or Editor).\nDISABLING ALL Cutting Planes (aka. growth simulation) in the Synchro4D schedules!");
			S4D.bDisableCuttingPlanes = true;
		}
#endif
	}

	void SetupMaterials() const
	{
		ITwin::SetupMaterials(FTilesetAccess(&Owner), Owner.Synchro4DSchedules);
	}

	void DestroyTileset()
	{
		ITwin::DestroyTilesetsInActor(Owner);
	}

	void LoadDecorationIfNeeded();
	void OnLoadingUIEvent();
	void UpdateAfterLoadingUIEvent();
	void AutoExportAndLoad();
	void SetLastTransforms();
	void OnIModelTransformUpdated(USceneComponent* UpdatedComponent,
								  std::optional<FTransform>& LastTransformUpdated);

	void TestExportCompletionAfterDelay(FString const& InExportId, float DelayInSeconds);


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
			(*IModelIter)->Impl->InternalSynchro4DDebugElement(Args, (*IModelIter)->Impl->GetTuner());
		}
	}

	void InternalSynchro4DDebugElement(const TArray<FString>& Args,
		std::shared_ptr<BeUtils::GltfTuner> const& Tuner)
	{
		CreateSynchro4DSchedulesComponent(Tuner);
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
		static constexpr double MetadataRatioInTotalProgress = 0.35;

	private:
		AITwinIModel& Owner;
		EElementsMetadata const KindOfMetadata;
		FString const ECSQLQueryString;
		FString const ECSQLQueryCount;
		FString const BatchMsg;
		std::optional<AdvViz::SDK::ITwinAPIRequestInfo> RequestInfo;
		FString LastCacheFolderUsed;
		FJsonQueriesCache Cache;
		mutable ITwinHttp::FMutex Mutex;

		EState State = EState::NotStarted;
		int QueryRowStart = 0, TotalRowsParsed = 0, TotalRowsExpected = -1;
		HttpRequestID CurrentRequestID;
		static constexpr int QueryRowCount = 50000;

		void DoRestart()
		{
			Owner.ScheduleDownloadPercentComplete = 0.;
			QueryRowStart = TotalRowsParsed = 0;
			TotalRowsExpected = -1;
			FString const CacheFolder = QueriesCache::GetCacheFolder(
				QueriesCache::ESubtype::ElementsMetadataCombined,
				Owner.ServerConnection->Environment, Owner.ITwinId, Owner.IModelId, Owner.ResolvedChangesetId);
			if (LastCacheFolderUsed != CacheFolder && ensure(!CacheFolder.IsEmpty()))
			{
				if (!Cache.Initialize(CacheFolder, Owner.ServerConnection->Environment,
									  Owner.GetActorNameOrLabel() + TEXT(" - ") + BatchMsg))
				{
					BE_LOGW("ITwinQuery", "Something went wrong while setting up the local http cache for Elements metadata queries - cache will NOT be used!");
				}
				LastCacheFolderUsed = CacheFolder;
			}
			QueryNextPage();
		}

	public:
		FQueryElementMetadataPageByPage(AITwinIModel& InOwner, EElementsMetadata const InKindOfMetadata)
			: Owner(InOwner)
			, KindOfMetadata(InKindOfMetadata)
			, ECSQLQueryString(
				FString("SELECT e.ECInstanceId, e.Parent.Id, e.FederationGuid, a.Identifier")
				+ TEXT(" FROM bis.Element e")
				+ TEXT(" LEFT JOIN bis.ExternalSourceAspect a ON a.Element.Id = e.ECInstanceId"))
			, ECSQLQueryCount(TEXT("SELECT COUNT(*) FROM bis.Element"))
			, BatchMsg(TEXT("iModel Elements metadata"))
			, Cache(InOwner)
		{
			ensure(KindOfMetadata == EElementsMetadata::Combined);//only handling this case now
		}

		EState GetState() const { return State; }

		void Restart()
		{
			if (EState::NotStarted == State || EState::Finished == State || EState::StoppedOnError == State)
			{
				UninitializeCache(); // reinit, we may have a new changesetId for example
				UE_LOG(LogITwin, Display, TEXT("%s queries (re)starting..."), *BatchMsg);
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
			RequestInfo.emplace(Owner.WebServices->InfosToQueryIModel(
				Owner.ITwinId, Owner.IModelId, Owner.ResolvedChangesetId,
				(TotalRowsExpected == -1) ? ECSQLQueryCount : ECSQLQueryString, QueryRowStart, QueryRowCount));
			if (TotalRowsExpected != -1)
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
				Owner.WebServices->QueryIModelRows({}, {}, {}, {}, 0, 0, // everything's in RequestInfo
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
				UE_LOG(LogITwin, Display, TEXT("%s queries interrupted, will restart..."), *BatchMsg);
				DoRestart();
				return true;
			}
			if (!bSuccess)
			{
				State = EState::StoppedOnError;
				return true;
			}
			int RowsParsed = 0;
			bool bHasReceivedTableCount = false;
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
			auto& IModelInternals = GetInternals(Owner);
			if (JsonObj.IsValid())
			{
				TArray<TSharedPtr<FJsonValue>> const* JsonRows = nullptr;
				if (JsonObj->TryGetArrayField(TEXT("data"), JsonRows))
				{
					if (TotalRowsExpected == -1)
					{
						if (ensure(JsonRows->Num() == 1))
						{
							auto const& Entries = (*JsonRows)[0]->AsArray();
							if (ensure(!Entries.IsEmpty() && Entries[0]->TryGetNumber(TotalRowsExpected)))
							{
								bHasReceivedTableCount = true;
								if (TotalRowsExpected > 0)
									IModelInternals.SceneMapping.ReserveIModelMetadata(TotalRowsExpected);
							}
						}
					}
					else
					{
						RowsParsed = IModelInternals.SceneMapping.ParseIModelMetadata(*JsonRows);
					}
				}
			}
			TotalRowsParsed += RowsParsed;
			if (RowsParsed > 0 || bHasReceivedTableCount)
			{
				if (bHasReceivedTableCount)
				{
					UE_LOG(LogITwin, Display, TEXT("%s table count retrieved from %s: %d..."), *BatchMsg,
						bFromCache ? TEXT("cache") : TEXT("remote"), TotalRowsExpected);
				}
				else
				{
					UE_LOG(LogITwin, Verbose, TEXT("%s retrieved from %s: %d, asking for more..."), *BatchMsg,
						bFromCache ? TEXT("cache") : TEXT("remote"), TotalRowsParsed);
					if (TotalRowsExpected != -1)
					{
						Owner.ScheduleDownloadPercentComplete = 100. * MetadataRatioInTotalProgress
							* std::min(1., QueryRowStart / (double)TotalRowsExpected);
						IModelInternals.LogScheduleDownloadProgressed();
					}
				}
				QueryNextPage();
			}
			else
			{
				UE_LOG(LogITwin, Display, TEXT("Total %s retrieved from %s: %d."), *BatchMsg,
					/*likely all retrieved from same source...*/bFromCache ? TEXT("cache") : TEXT("remote"),
					TotalRowsParsed);
				// This call will release hold of the cache folder, which will "often" allow reuse by cloned
				// actor when entering PIE (unless it was not yet finished downloading, of course)
				UninitializeCache();
				IModelInternals.SceneMapping.FinishedParsingIModelMetadata();
				State = EState::Finished;
			}
			return true;
		}

		void UninitializeCache()
		{
			Cache.Uninitialize();
			LastCacheFolderUsed = {};// otw Cache is never re-init!! see azdev#1621189, Investigation Notes
		}

		void OnIModelUninit()
		{
			UninitializeCache();
		}

	}; // class FQueryElementMetadataPageByPage

	std::optional<FQueryElementMetadataPageByPage> ElementsMetadataQuerying;
	// No longer needed: blame here to reasonably easily recover the code in case a second batch
	// of requests is needed again in the future:
	//std::optional<FQueryElementMetadataPageByPage> (...);

	class FRetrieveSavedViewsPageByPage
	{
	public:
		enum class EState {
			NotStarted, Running, Finished
		};
	private:
		AITwinIModel& Owner;
		EState State = EState::NotStarted;
		int QuerySVStart = 0;
		static const int QuerySVCount = 100;
	public:
		FRetrieveSavedViewsPageByPage(AITwinIModel& InOwner) : Owner(InOwner) {}
		EState GetState() const { return State; }

		void RetrieveNextPage(const FString& GroupId = "")
		{
			State = EState::Running;
			UE_LOG(LogITwin, Display, TEXT("[SavedViews] Retrieving...GroupId: %s, QueryCount: %d, QueryStart: %d"), *GroupId, QuerySVCount,
				QuerySVStart);
			Owner.WebServices->GetAllSavedViews(Owner.ITwinId, Owner.IModelId, GroupId, QuerySVCount, QuerySVStart);
			QuerySVStart += QuerySVCount;
		}

		void OnSavedViewsRetrieved(bool bSuccess, const FSavedViewInfos& SavedViews)
		{
			if ((bSuccess && SavedViews.SavedViews.IsEmpty())
				|| !bSuccess)
			{
				State = EState::Finished;
				QuerySVStart = 0;
			}
			if (State != EState::Finished)
			{
				RetrieveNextPage(SavedViews.GroupId);
			}
		}
	};
	TMap<FString, FRetrieveSavedViewsPageByPage> SavedViewsPageByPage;

}; // class AITwinIModel::FImpl

/*static*/
TWeakObjectPtr<ULightComponent> AITwinIModel::FImpl::LightForForcedShadowUpdate;
double AITwinIModel::FImpl::LastForcedShadowUpdate = 0.;
float AITwinIModel::FImpl::ForceShadowUpdateMaxEvery = 1.f;

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

void AITwinIModel::FImpl::ResetSceneMapping()
{
	// A bit messy: TilesPendingRenderReadiness could entirely be in SceneMapping, but on the other hand
	// TilesChangingVisibility is purely an iModel implementation detail, so I'm leaving that here for the
	// time being...
	TilesChangingVisibility.clear();
	Internals.TilesPendingRenderReadiness.clear();
	Internals.SceneMapping.Reset(); // except visibility states
}

// Unused (was for testing, but interesting to keep...): converts some arbitrary ECEF coordinates to the
// intersection of the [Earth center;IModelEcef] segment with the WGS84 ellipsoid.
// Rewritten from the code at the beginning of cesium-native's Ellipsoid::scaleToGeodeticSurface
static FVector RadialIntersectionOnEllipsoidWGS84(FVector const& IModelEcef)
{
	auto&& Radii = CesiumGeospatial::Ellipsoid::WGS84.getRadii();
	const double SquaredNorm = (IModelEcef / FVector(Radii.x, Radii.y, Radii.z)).SquaredLength();
	return IModelEcef * sqrt(1.0 / SquaredNorm);
}

void AITwinIModel::FImpl::MakeTileset(std::optional<FITwinExportInfo> const& ExportInfo /*= {}*/)
{
	if (!ensure(ExportInfo || ExportInfoPendingLoad)) return;
	if (!ensure(IModelProperties)) return;

	if (IModelProperties->EcefLocation && IModelProperties->ProjectExtents
		&& IModelProperties->EcefLocation->bHasGeographicCoordinateSystem
		&& !IModelProperties->EcefLocation->bHasProjectExtentsCenterGeoCoords)
	{
		ITwinHttp::FLock Lock(ConvertBBoxCenterToGeoCoordsRequestIdMutex);
		if (ConvertBBoxCenterToGeoCoordsRequestId.IsEmpty())
		{
			auto const BoxCtrInIModelCoords = .5 * (IModelProperties->ProjectExtents->Low
												  + IModelProperties->ProjectExtents->High);
			Owner.WebServices->ConvertIModelCoordsToGeoCoords(
				Owner.ITwinId, Owner.IModelId, Owner.ResolvedChangesetId, BoxCtrInIModelCoords,
				[this, pOwner=&this->Owner](HttpRequestID const& ReqID)
				{
					if (!IsValid(pOwner))
						return;
					ITwinHttp::FLock Lock(ConvertBBoxCenterToGeoCoordsRequestIdMutex);
					ConvertBBoxCenterToGeoCoordsRequestId = ReqID;
				});
		}
		if (!ExportInfoPendingLoad) ExportInfoPendingLoad = ExportInfo;
		return;
	}

	// This was added following a situation where the iModel doesn't tick at all, which shouldn't happen...
	// But in any case, I'm not sure we are guaranteed that the iModel will have ticked at least once before
	// reaching this: all the preliminary requests might happen before the iModel first ticks, for example if
	// in the future we have all replies in cache, or no longer use http at all (eg. through Mango?)
	if (!bInitialized)
		Initialize();

	FITwinExportInfo const CompleteInfo = ExportInfo ? (*ExportInfo) : (*ExportInfoPendingLoad);
	ExportInfoPendingLoad.reset();
	// No need to keep former versions of the tileset
	ResetSceneMapping();
	DestroyTileset();

	// We need to query these metadata of iModel Elements using several "paginated" requests sent
	// successively, but we also need to support interrupting and restart queries from scratch
	// because this code path can be executed several times for an iModel, eg. upon UpdateIModel
	ElementsMetadataQuerying->Restart();
	// It seems risky to NOT do a ResetSchedules here: for example, FITwinElement::AnimationKeys are
	// not set, MainTimeline::NonAnimatedDuplicates is empty, etc.
	// We could just "reinterpret" the known schedule data, but since it is in a local cache...
	if (IsValid(Owner.Synchro4DSchedules))
		Owner.Synchro4DSchedules->ResetSchedules();

	// *before* SpawnActor otherwise Cesium will create its own default georef
	auto&& Geoloc = FITwinGeolocation::Get(*Owner.GetWorld());

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = &Owner;
	const auto Tileset = Owner.GetWorld()->SpawnActor<ACesium3DTileset>(SpawnParams);

	auto pFeaturesMetadataComponent = Cast<UCesiumFeaturesMetadataComponent>(
		Tileset->AddComponentByClass(UCesiumFeaturesMetadataComponent::StaticClass(), true,
									 FTransform::Identity, false));
	pFeaturesMetadataComponent->SetFlags(
		RF_Transient | RF_DuplicateTransient | RF_TextExportTransient);
	Tileset->AddInstanceComponent(pFeaturesMetadataComponent);

	pFeaturesMetadataComponent->Description.PrimitiveFeatures.FeatureIdSets.Add(
		FCesiumFeatureIdSetDescription{
			.Name = FString::Printf(TEXT("_FEATURE_ID_%d"),
									ITwinCesium::Metada::ELEMENT_FEATURE_ID_SLOT),
			.Type = ECesiumFeatureIdSetType::Attribute,
			.bHasNullFeatureId = true,
			.NullFeatureId = ITwin::NOT_FEATURE.value()
		});
	// Nothing needed for MATERIAL_FEATURE_ID_SLOT, as the corresponding primitive features are the same
	// (_FEATURE_ID_0), only the table are distinct - so basically, there is no _FEATURE_ID_1 attribute!
	//pFeaturesMetadataComponent->Description.PrimitiveFeatures.FeatureIdSets.Add(
	//	FCesiumFeatureIdSetDescription{
	//		.Name = FString::Printf(TEXT("_FEATURE_ID_%d"),
	//								ITwinCesium::Metada::MATERIAL_FEATURE_ID_SLOT),
	//		.Type = ECesiumFeatureIdSetType::Attribute,
	//		.bHasNullFeatureId = true,
	//		.NullFeatureId = ITwin::NOT_FEATURE.value()
	//	});
	// No property table is to be uploaded to the GPU: Element IDs stored in the ELEMENT_NAME table are
	// 64bits, which we've thus had to map internally to the 32bits FeatureIDs:
	//pFeaturesMetadataComponent->Description.ModelMetadata.PropertyTables.Add(...);
	// We don't need use property texture either:
	// pFeaturesMetadataComponent->Description.PrimitiveMetadata.PropertyTextureNames.Add(...);
	//pFeaturesMetadataComponent->Description.ModelMetadata.PropertyTextures.Add(...);

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
	Owner.bSynchro4DAutoLoadSchedule = Settings->bIModelAutoLoadSynchro4DSchedules;
	// TODO_GCO: Necessary for picking, unless there is another method that does
	// not require the Physics data? Note that pawn collisions need to be disabled to
	// still allow navigation through meshes (see SetActorEnableCollision).
	Tileset->SetCreatePhysicsMeshes(Settings->IModelCreatePhysicsMeshes);
	Tileset->SetDoubleSidedCollisions(true); // AdvViz #1927793
	Tileset->SetMaximumScreenSpaceError(Settings->TilesetMaximumScreenSpaceError);
	// connect mesh creation callback
	Tileset->SetLifecycleEventReceiver(SceneMappingBuilder.Get());
	Tileset->SetGltfModifier(GetTuner());
	Tileset->SetTilesetSource(ETilesetSource::FromUrl);
	Tileset->SetUrl(CompleteInfo.MeshUrl);

	Tileset->MaximumCachedBytes = std::max(0ULL, Settings->CesiumMaximumCachedMegaBytes * (1024 * 1024ULL));
	// Avoid unloading/reloading tiles when merely rotating the camera - implied by SetUseLodTransitions(true)
	// according to the documentation and the first lines of Tileset::updateView, but rotating still seems to
	// cycles tiles, so I must be missing something...
	//Tileset->EnableFrustumCulling = false;
	Tileset->SetUseLodTransitions(true);
	Tileset->LodTransitionLength = 1.f;
	Tileset->MaximumSimultaneousTileLoads = Settings->CesiumMaximumSimultaneousTileLoads;
	Tileset->LoadingDescendantLimit = Settings->CesiumLoadingDescendantLimit;
	Tileset->ForbidHoles = Settings->CesiumForbidHoles;

	if (IModelProperties->EcefLocation) // iModel is geolocated
	{
		Tileset->SetGeoreference(Geoloc->GeoReference.Get());
		auto const BoxCtrInIModelCoords = IModelProperties->ProjectExtents
			? (.5 * (IModelProperties->ProjectExtents->Low + IModelProperties->ProjectExtents->High))
			: FVector::ZeroVector;
		// GetIModelToEcefTransform does not depend on Geoloc->GeoReference, so we can indeed do this
		// "AccordingToIModel" because we'll hack EcefLocation->Origin below if bHasCartographicOrigin!)
		FVector const BoxCtrEcefWithLinearMapping = UITwinUtilityLibrary
			::GetIModelToEcefTransform(&Owner).TransformPosition(BoxCtrInIModelCoords);
		if (IModelProperties->EcefLocation->bHasProjectExtentsCenterGeoCoords)
		{
			// LongitudeLatitudeHeightToEarthCenteredEarthFixed(..) does exactly the same as
			// Geoloc->GeoReference->GetOriginEarthCenteredEarthFixed(..) but with the long./lat./height
			// passed, this way we can "fix" the iModel ECEF even if we're not using this specific iModel
			// for the geolocation.
			FVector const IModelEcefOffsetHack =
				UCesiumWgs84Ellipsoid::LongitudeLatitudeHeightToEarthCenteredEarthFixed(FVector(
					IModelProperties->EcefLocation->ProjectExtentsCenterGeoCoords.Longitude,
					IModelProperties->EcefLocation->ProjectExtentsCenterGeoCoords.Latitude,
					IModelProperties->EcefLocation->ProjectExtentsCenterGeoCoords.Height))
				- BoxCtrEcefWithLinearMapping;
			IModelProperties->EcefLocation->Origin += IModelEcefOffsetHack;
			if (IModelEcefOffsetHack.Length() > 10.)
			{
				BE_LOGW("ITwinAdvViz", "Moved ECEF location of iModel "
					<< TCHAR_TO_UTF8(*Owner.GetActorNameOrLabel()) << " by about ~"
					<< (int)std::ceil(IModelEcefOffsetHack.Length()) << "m to match geo-location");
			}
		}
		// If the shared georeference is not inited yet, let's initialize it according to this iModel location.
		if (Geoloc->GeoReference->GetOriginPlacement() == EOriginPlacement::TrueOrigin
			|| Geoloc->bCanBypassCurrentLocation)
		{
			Geoloc->bCanBypassCurrentLocation = false;
			Geoloc->bNeedElevationEvaluation = false;
			Geoloc->GeoReference->SetOriginPlacement(EOriginPlacement::CartographicOrigin);
			// Put georeference at the cartographic coordinates of the center of the iModel's extents:
			// either from the GCS-converted coordinates returned by a query, or from linear mapping from
			// the iModel's ECEF origin
			if (IModelProperties->EcefLocation->bHasProjectExtentsCenterGeoCoords)
			{
				Geoloc->GeoReference->SetOriginLongitudeLatitudeHeight(FVector(
					IModelProperties->EcefLocation->ProjectExtentsCenterGeoCoords.Longitude,
					IModelProperties->EcefLocation->ProjectExtentsCenterGeoCoords.Latitude,
					IModelProperties->EcefLocation->ProjectExtentsCenterGeoCoords.Height));
			}
			else
			{
				Geoloc->GeoReference->SetOriginEarthCenteredEarthFixed(BoxCtrEcefWithLinearMapping);
			}

			// update decoration geo-reference
			AITwinDecorationHelper* DecoHelper = Cast<AITwinDecorationHelper>(UGameplayStatics::GetActorOfClass(Owner.GetWorld(), AITwinDecorationHelper::StaticClass()));
			if (DecoHelper)
			{
				FVector v = Geoloc->GeoReference->GetOriginLongitudeLatitudeHeight();
				FVector latLongHeight(v.Y, v.X, v.Z);
				DecoHelper->SetDecoGeoreference(latLongHeight);
			}
		}
	}
	else
	{
		// iModel is not geolocated.
		Tileset->SetGeoreference(Geoloc->LocalReference.Get());
	}
	Internals.SceneMapping.SetIModel2UnrealTransfos(Owner);
	if (IsValid(Owner.Synchro4DSchedules))
		SetupSynchro4DSchedules(*Settings);
	SetupMaterials();

	TilesetLoadedCount = 0;
	Tileset->OnTilesetLoaded.AddDynamic(&Owner, &AITwinIModel::OnTilesetLoaded);
	OnTilesetLoadFailureHandle = OnCesium3DTilesetLoadFailure.AddUObject(
		&Owner, &AITwinIModel::OnTilesetLoadFailure);

	//if (IsValid(Owner.RootComponent))
	//{
	//	Owner.RootComponent->TransformUpdated.AddLambda(
	//		[this](USceneComponent* /*UpdatedComponent*/, EUpdateTransformFlags, ETeleportType)
	//		{
				// Calling OnIModelTransformUpdated here led to duplicate calls to OnIModelOffsetChanged
				// when changing offset from Carrot UI.
				//OnIModelTransformUpdated(UpdatedComponent, LastIModelTransformUpdated);

				// Alternatively, I thought propagating the transfo would be needed, but it is not:
				// changing the iModel transfo in the Editor Outliner already offsets the tileset,
				// based on my latest testing (even though once I had witnessed otherwise! :/)
				//auto* Tileset = Owner.GetTileset();
				//if (IsValid(Tileset))
				//	Tileset->SetActorTransform(Owner.GetActorTransform());
	//		});
	//}
	if (IsValid(Tileset->GetRootComponent()))
	{
		Tileset->GetRootComponent()->TransformUpdated.AddLambda(
			[this](USceneComponent* UpdatedComponent, EUpdateTransformFlags, ETeleportType)
			{
				OnIModelTransformUpdated(UpdatedComponent, LastTilesetTransformUpdated);
			});
	}
	if (!FImpl::LightForForcedShadowUpdate.IsValid())
	{
		ULightComponent* LightComp = nullptr;
		UDirectionalLightComponent* DirLightComp = nullptr;
		for (TActorIterator<AActor> Iter(Owner.GetWorld()); Iter; ++Iter)
		{
			LightComp = Cast<ULightComponent>((*Iter)->GetComponentByClass(ULightComponent::StaticClass()));
			DirLightComp = Cast<UDirectionalLightComponent>((*Iter)->GetComponentByClass(
				UDirectionalLightComponent::StaticClass()));
			if (DirLightComp) // use preferrably a dir light
				break;
		}
		FImpl::LightForForcedShadowUpdate = (DirLightComp ? DirLightComp : LightComp);
	}
}

void AITwinIModel::FImpl::OnIModelTransformUpdated(USceneComponent* UpdatedComponent,
												   std::optional<FTransform>& LastTransformUpdated)
{
	if (LastTransformUpdated
		&& FITwinMathExts::StrictlyEqualTransforms(
			*LastTransformUpdated, UpdatedComponent->GetComponentToWorld()))
	{
		return;
	}
	Owner.OnIModelOffsetChanged();
}

void AITwinIModel::FImpl::SetLastTransforms()
{
	//if (IsValid(Owner.GetRootComponent()))
	//	LastIModelTransformUpdated = Owner.GetRootComponent()->GetComponentToWorld();
	auto const* const Tileset = Owner.GetTileset();
	if (IsValid(Tileset) && IsValid(Tileset->GetRootComponent()))
		LastTilesetTransformUpdated = Tileset->GetRootComponent()->GetComponentToWorld();
}

AITwinIModel::AITwinIModel()
	: Impl(MakePimpl<FImpl>(*this))
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("root")));
	PrimaryActorTick.bCanEverTick = true;
}

void AITwinIModel::Tick(float Delta)
{
	if (!Impl->bInitialized)
		Impl->Initialize();
	Impl->HandleTilesHavingChangedVisibility();
	Impl->Internals.SceneMapping.HandleNewSelectingAndHidingTextures();
	if (bSynchro4DAutoLoadSchedule
		&& AITwinIModel::FImpl::FQueryElementMetadataPageByPage::EState::Finished
			== Impl->ElementsMetadataQuerying->GetState())
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
	Impl->ForceShadowUpdatesIfNeeded();
}


void AITwinIModel::FImpl::OnWorldDestroyed(UWorld* InWorld)
{
	if (IsValid(InWorld) && IsValid(&Owner) && Owner.GetWorld() == InWorld)
	{
		Internals.Uniniter->Run();
	}
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

	// ML material prediction is only accessible when customizing the application/plugin configuration.
	Owner.bEnableMLMaterialPrediction = ITwin::IsMLMaterialPredictionEnabled();

	std::shared_ptr<BeUtils::GltfTuner> GltfTunerPtr = std::make_shared<FITwinIModelGltfTuner>(Owner);
	FITwinIModelMaterialHandler::Initialize(GltfTunerPtr, &Owner);

	// create a callback to fill our scene mapping when meshes are loaded
	SceneMappingBuilder =
		TStrongObjectPtr<UITwinSceneMappingBuilder>(NewObject<UITwinSceneMappingBuilder>(&Owner));
	SceneMappingBuilder->SetIModel(Owner);
	ElementsMetadataQuerying.emplace(Owner, EElementsMetadata::Combined);
	Internals.Uniniter->Register([this] {
		ElementsMetadataQuerying->OnIModelUninit();
		SceneMappingBuilder.Reset();
		ClippingHelper.Reset();
	});
	// When loading a level (or doing "Save current Level as"), EndPlay is not called (because not in PIE...)
	// and detroying the old world crashes because of the leak! (at least starting from UE 5.6)
	GEngine->OnWorldDestroyed().AddRaw(this, &AITwinIModel::FImpl::OnWorldDestroyed);

	CreateSynchro4DSchedulesComponent(GetTuner());

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
	// Added for Carrot which now uses "LM_Automatic" and "latest" as changesetId
	else if (Owner.LoadingMethod == ELoadingMethod::LM_Automatic)
	{
		OnLoadingUIEvent();
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
	if (CheckServerConnection() != AdvViz::SDK::EITwinAuthStatus::Success)
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

bool AITwinIModel::GetBoundingBox(FBox& OutBox, bool bClampOutlandishValues) const
{
	auto* const TileSet = GetTileset();
	if (!TileSet)
		return false;
	if (!(Impl->IModelProperties && Impl->IModelProperties->ProjectExtents))
		return false;

	FITwinIModel3DInfo OutInfo;
	GetModel3DInfoInCoordSystem(OutInfo, EITwinCoordSystem::UE);
	FBox const IModelBBox(OutInfo.BoundingBoxMin, OutInfo.BoundingBoxMax);
	if (!IModelBBox.IsValid)
		return false;
	OutBox = IModelBBox;
	if (bClampOutlandishValues)
	{
		// hack around extravagant project extents: limit half size to 10km: it looks big but there is a x0.2
		// empirical ratio in FImpl::ZoomOn already...
		double const MaxHalfSize =
			Impl->Internals.SceneMapping.GetIModel2UnrealTransfo().TransformVector(FVector(10'000., 0., 0.))
			.GetAbsMax();// should be ~2e6
		FVector Ctr, HalfSize;
		IModelBBox.GetCenterAndExtents(Ctr, HalfSize);
		if (HalfSize.GetAbsMax() >= MaxHalfSize)
		{
			double Ratio = MaxHalfSize;
			if (std::abs(HalfSize.X) >= MaxHalfSize)
				Ratio /= std::abs(HalfSize.X);
			else if (std::abs(HalfSize.Y) >= MaxHalfSize)
				Ratio /= std::abs(HalfSize.Y);
			else
				Ratio /= std::abs(HalfSize.Z);
			OutBox = FBox(Ctr - Ratio * HalfSize, Ctr + Ratio * HalfSize);
		}
	}
	return true;
}

void AITwinIModel::ZoomOnIModel()
{
	// We zoom on the bounding box of the iModel, computed from the project extents converted to Unreal space
	FBox IModelBBox;
	if (!GetBoundingBox(IModelBBox, /*bClampOutlandishValues*/true))
		return;
	FImpl::ZoomOn(IModelBBox, GetWorld());
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
	ITwinHttp::FLock Lock(Impl->GetAttachedRealityDataMutex);
	Impl->AttachedRealityDataIdsPromises.Add(Promise);
	if (Impl->GetAttachedRealityDataRequestId.IsEmpty())
	{
		WebServices->QueryIModelRows(ITwinId, IModelId, ResolvedChangesetId,
			TEXT("SELECT Element.UserLabel, Model.JsonProperties FROM bis.Model JOIN bis.Element ON Element.ECInstanceId = Model.ECInstanceId WHERE Model.ECClassId IS (ScalableMesh.ScalableMeshModel)"),
			0, 1000,
			[Impl = this->Impl.Get()](HttpRequestID const& ReqID)
			{
				ITwinHttp::FLock Lock(Impl->GetAttachedRealityDataMutex);
				Impl->GetAttachedRealityDataRequestId = ReqID;
			},
			nullptr /*RequestInfo*/,
			[](long /*statusCode*/, std::string const& Error, bool& bAllowRetry, bool& bLogError)
			{
				// Filter some errors to avoid useless retries
				if (Error.find("ECClass 'ScalableMesh.ScalableMeshModel' does not exist or could not be loaded.") != std::string::npos)
				{
					bAllowRetry = bLogError = false;
				}
			});
	}
	return Promise->GetFuture();
}

TFuture<TArray<FString>> AITwinIModel::GetChildrenModelIds(const FString& ParentModelId)
{
	if (Impl->ChildrenModelIds.Contains(ParentModelId))
		return MakeFulfilledPromise<TArray<FString>>(Impl->ChildrenModelIds[ParentModelId]).GetFuture();
	const auto Promise = MakeShared<TPromise<TArray<FString>>>();
	Impl->ChildrenModelIdsPromises.Add(Promise);
	ITwinHttp::FLock Lock(Impl->GetChildrenModelsRequestIdMutex);
	{
		WebServices->QueryIModelRows(ITwinId, IModelId, ResolvedChangesetId,
			FString::Printf(TEXT("WITH RECURSIVE StartingSubject(SubjectId, IsFromModel) AS ("\
				"SELECT s.ECInstanceId, FALSE "\
				"FROM Bis.Subject s "\
				"WHERE s.ECInstanceId = %s "\

				"UNION "\

				"SELECT s.ECInstanceId, TRUE "\
				"FROM BisCore.GeometricModel3d m "\
				"JOIN Bis.ModelModelsElement mme ON mme.SourceECInstanceId = m.ECInstanceId "\
				"JOIN Bis.ElementOwnsChildElements owns ON owns.TargetECInstanceId = mme.TargetECInstanceId "\
				"JOIN Bis.Subject s ON s.ECInstanceId = owns.SourceECInstanceId "\
				"WHERE m.ECInstanceId = %s "\
				"), "\
				"SubjectHierarchy(SubjectId, IsFromModel) AS ("\
				"SELECT SubjectId, IsFromModel FROM StartingSubject "\
				
				"UNION ALL "\

				"SELECT child.ECInstanceId, parent.IsFromModel "\
				"FROM Bis.Subject child "\
				"JOIN SubjectHierarchy parent ON child.Parent.Id = parent.SubjectId"\
				") "\
				"SELECT sh.SubjectId AS Id FROM SubjectHierarchy sh WHERE sh.SubjectId = "\
				"%s UNION SELECT id FROM (SELECT p.ECInstanceId id, p.Parent.Id ParentId FROM bis.InformationPartitionElement p INNER JOIN bis.GeometricModel3d model ON model.ModeledElement.Id = p.ECInstanceId WHERE p.Parent.Id IN (SELECT sh.SubjectId AS Id "\
				"FROM SubjectHierarchy sh "\
				"WHERE NOT sh.IsFromModel) "\
				"AND EXISTS (SELECT 1 FROM BisCore.GeometricElement3d WHERE Model.Id = model.ECInstanceId)) "\
				"UNION "\
				"SELECT m.ECInstanceId AS Id "\
				"FROM SubjectHierarchy sh "\
				"JOIN Bis.ElementOwnsChildElements owns ON owns.SourceECInstanceId = sh.SubjectId "\
				"JOIN Bis.ModelModelsElement mme ON mme.TargetECInstanceId = owns.TargetECInstanceId "\
				"JOIN BisCore.GeometricModel3d m ON m.ECInstanceId = mme.SourceECInstanceId "\
				"JOIN BisCore.Element e ON e.ECInstanceId = m.ModeledElement.Id "\
				"WHERE NOT m.IsPrivate AND json_extract(e.JsonProperties, '$.PhysicalPartition.Model.Content') IS NULL "\
				"AND json_extract(e.JsonProperties, '$.GraphicalPartition3d.Model.Content') IS NULL AND EXISTS (SELECT 1 FROM BisCore.GeometricElement3d WHERE Model.Id = m.ECInstanceId)"), *ParentModelId, *ParentModelId, *ParentModelId),
			0, -1,
			[Impl = this->Impl.Get(), ParentModelId](HttpRequestID const& ReqID)
			{
				ITwinHttp::FLock Lock(Impl->GetChildrenModelsRequestIdMutex);
				Impl->GetChildrenModelsRequestId = ReqID;
			});
	}
	return Promise->GetFuture();
}

TFuture<TArray<FString>> AITwinIModel::GetSubCategoryIds(const FString& ParentCategoryId)
{
	if (Impl->SubCategoryIds.Contains(ParentCategoryId))
		return MakeFulfilledPromise<TArray<FString>>(Impl->SubCategoryIds[ParentCategoryId]).GetFuture();
	const auto Promise = MakeShared<TPromise<TArray<FString>>>();
	Impl->SubCategoryIdsPromises.Add(Promise);
	ITwinHttp::FLock Lock(Impl->GetSubCategoriesRequestIdMutex);
	{
		WebServices->QueryIModelRows(ITwinId, IModelId, ResolvedChangesetId,
			FString::Printf(TEXT("SELECT ECInstanceId as Id From bis.SpatialCategory c WHERE c.ECInstanceId = %s"\
								 "UNION "\
								 "SELECT ECInstanceId as Id FROM bis.SubCategory s WHERE s.Parent.Id = %s"), *ParentCategoryId, *ParentCategoryId),
			0, -1,
			[Impl = this->Impl.Get(), ParentCategoryId](HttpRequestID const& ReqID)
			{
				ITwinHttp::FLock Lock(Impl->GetSubCategoriesRequestIdMutex);
				Impl->GetSubCategoriesRequestId = ReqID;
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

UITwinSynchro4DSchedules* AITwinIModel::GetSynchro4DSchedules()
{
	return Synchro4DSchedules;
}

void AITwinIModel::OnChangesetsRetrieved(bool bSuccess, FChangesetInfos const& Infos)
{
	if (!bSuccess)
		return;

	SetResolvedChangesetId(Infos.Changesets.IsEmpty() ? FString() : Infos.Changesets[0].Id);

	Impl->Update();
}

void AITwinIModel::OnTilesetLoadFailure(FCesium3DTilesetLoadFailureDetails const& Details)
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
		Impl->SetLastTransforms();
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
			UE_LOG(LogITwin, Display, TEXT("iModel EPSG %d, Earth origin: %s, orientation: %s"),
				Impl->IModelProperties->EcefLocation->GeographicCoordinateSystemEPSG,
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
	Impl->MakeTileset();
}

void AITwinIModel::OnConvertedIModelCoordsToGeoCoords(bool bSuccess,
	AdvViz::SDK::GeoCoordsReply const& GeoCoords, HttpRequestID const& RequestID)
{
	ITwinHttp::FLock Lock(Impl->ConvertBBoxCenterToGeoCoordsRequestIdMutex);
	if (RequestID == Impl->ConvertBBoxCenterToGeoCoordsRequestId
		&& ensure(Impl->IModelProperties && Impl->IModelProperties->EcefLocation))
	{
		if (bSuccess && GeoCoords.geoCoords && !GeoCoords.geoCoords->empty()
			&& int(AdvViz::SDK::GeoServiceStatus::Success) == GeoCoords.geoCoords->begin()->s)
		{
			auto& GeoCoord = GeoCoords.geoCoords->begin()->p;
			Impl->IModelProperties->EcefLocation->ProjectExtentsCenterGeoCoords = FCartographicProps{
				.Height = GeoCoord[2],
				.Latitude = GeoCoord[1],
				.Longitude = GeoCoord[0]
			};
			Impl->IModelProperties->EcefLocation->bHasProjectExtentsCenterGeoCoords = true;
		}
		else
		{
			// Signal to MakeTileset to behave as if we had no GCS, since conversion failed anyway
			Impl->IModelProperties->EcefLocation->bHasGeographicCoordinateSystem = false;
			BE_LOGE("ITwinAPI", "Geographic conversion failed, geo-location for iModel "
				<< TCHAR_TO_UTF8(*(Impl->ExportInfoPendingLoad
					? Impl->ExportInfoPendingLoad->DisplayName : IModelId))
				<< " may be incorrect or imprecise");
		}
		// Construct the tileset anyway: if GCS conversion failed, it will fall back to linear mapping from
		// ECEF origin.
		Impl->MakeTileset();
	}
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

void AITwinIModel::GetPagedNodes(const FString& KeyString /*=""*/, int Offset /*= 0*/, int Count /*= 1000*/)
{
	WebServices->GetPagedNodes(ITwinId, IModelId, GetSelectedChangeset(), KeyString, Offset, Count);
}

void AITwinIModel::GetModelFilteredNodes(const FString& Filter)
{
	UE_LOG(LogITwin, Display, TEXT("AITwinIModel::GetModelFilteredNodes using Filter: %s"), *Filter);
	WebServices->GetModelFilteredNodes(ITwinId, IModelId, GetSelectedChangeset(), Filter);
}

void AITwinIModel::GetCategoryFilteredNodes(const FString& Filter)
{
	WebServices->GetCategoryFilteredNodes(ITwinId, IModelId, GetSelectedChangeset(), Filter);
}

void AITwinIModel::GetCategoryNodes(const FString& KeyString /*=""*/)
{
	WebServices->GetCategoryNodes(ITwinId, IModelId, GetSelectedChangeset(), KeyString);
}

void AITwinIModel::GetElementProperties(const FString& ElementId)
{
	if (ElementId.IsEmpty())
		return;
	WebServices->GetElementProperties(ITwinId, IModelId,
		GetSelectedChangeset(), ElementId);
}

void AITwinIModel::SelectElement(const FString& ElementId)
{
	if (ElementId.IsEmpty())
		return;
	ITwinElementID SelectedElement = ITwin::ParseElementID(ElementId);
	auto& IModelInt = GetInternals(*this);
	if (IModelInt.HasElementWithID(SelectedElement))
	{
		IModelInt.SceneMapping.PickVisibleElement(SelectedElement);
		IModelInt.DescribeElement(SelectedElement);
	}
}

void AITwinIModel::OnIModelPagedNodesRetrieved(bool bSuccess, FIModelPagedNodesRes const& IModelNodes)
{
}

void AITwinIModel::OnIModelCategoryNodesRetrieved(bool bSuccess, FIModelPagedNodesRes const& IModelNodes)
{
}

void AITwinIModel::OnModelFilteredNodesRetrieved(bool bSuccess, FFilteredNodesRes const& FilteredNodes, FString const& Filter)
{
}

void AITwinIModel::OnCategoryFilteredNodesRetrieved(bool bSuccess, FFilteredNodesRes const& FilteredNodes, FString const& Filter)
{
}

void AITwinIModel::OnIModelQueried(bool bSuccess, FString const& QueryResult, HttpRequestID const& RequestID)
{
	if ([&] { ITwinHttp::FLock Lock(Impl->GetAttachedRealityDataMutex);
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
				ITwinHttp::FLock Lock(Impl->GetAttachedRealityDataMutex);
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
				ensureMsgf(false, TEXT("expected exactly 2 rows"));
				continue;
			}
			// Parse json props: retrieve reality data id (if it exists).
			TSharedPtr<FJsonObject> JSonPropertiesJson;
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(RowArray[1]->AsString()), JSonPropertiesJson);

			FString RealityDataId;
			FString TilesetUrl;
			if (JSonPropertiesJson && JSonPropertiesJson->TryGetStringField(TEXT("tilesetUrl"), TilesetUrl))
			{
				RealityDataId = ANSI_TO_TCHAR(BeUtils::GetRealityDataIdFromUrl(TCHAR_TO_ANSI(*TilesetUrl)).c_str());
			}
			if (RealityDataId.IsEmpty())
				continue;
			// Retrieve metadata of reality data and check its format (we only support Cesium3DTiles).
			++(*PendingRequestCount);
			const auto Request = FHttpModule::Get().CreateRequest();
			Request->SetURL(TEXT("https://")+ITwinServerEnvironment::GetUrlPrefix(WebServices->GetEnvironment())+TEXT("api.bentley.com/reality-management/reality-data/")+RealityDataId);
			Request->SetHeader(TEXT("Accept"), TEXT("application/vnd.bentley.itwin-platform.v1+json"));
			const auto AccessToken = GetAccessToken();
			if (!AccessToken.IsEmpty())
				Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + AccessToken);
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
	else if ([&] { ITwinHttp::FLock Lock(Impl->GetChildrenModelsRequestIdMutex);
		return (RequestID == Impl->GetChildrenModelsRequestId); }
		())
	{
		const auto ModelIds = MakeShared<TArray<FString>>();
		TSharedPtr<FJsonObject> QueryResultJson;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(QueryResult), QueryResultJson);
		for (const auto& RowObject : QueryResultJson->GetArrayField(TEXT("data")))
		{
			const auto& RowString = RowObject->AsArray()[0]->AsString();
			ModelIds->Add(RowString);
		}
		ensure(!ModelIds->IsEmpty());
		if (!ModelIds->IsEmpty())
			Impl->ChildrenModelIds.Add({ (*ModelIds)[0],*ModelIds });
		for (const auto& Promise : Impl->ChildrenModelIdsPromises)
			Promise->SetValue(*ModelIds);
		Impl->ChildrenModelIdsPromises.Empty();
		return;
	}
	else if ([&] { ITwinHttp::FLock Lock(Impl->GetSubCategoriesRequestIdMutex);
		return (RequestID == Impl->GetSubCategoriesRequestId); }
		())
	{
		const auto SubCategoryIds = MakeShared<TArray<FString>>();
		TSharedPtr<FJsonObject> QueryResultJson;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(QueryResult), QueryResultJson);
		for (const auto& RowObject : QueryResultJson->GetArrayField(TEXT("data")))
		{
			const auto& RowString = RowObject->AsArray()[0]->AsString();
			SubCategoryIds->Add(RowString);
		}
		Impl->SubCategoryIds.Add({ (*SubCategoryIds)[0],*SubCategoryIds });
		for (const auto& Promise : Impl->SubCategoryIdsPromises)
			Promise->SetValue(*SubCategoryIds);
		Impl->SubCategoryIdsPromises.Empty();
		return;
	}
	// One and only one of the two "requesters" should handle this reply
	if (!Impl->ElementsMetadataQuerying->OnQueryCompleted(RequestID, bSuccess, QueryResult))
	{
		BE_LOGE("ITwinAPI", "iModel request ID not recognized: " << TCHAR_TO_UTF8(*RequestID));
	}
}

void AITwinIModel::OnMaterialPropertiesRetrieved(bool bSuccess, AdvViz::SDK::ITwinMaterialPropertiesMap const& props)
{
	if (bSuccess)
	{
		// In case we need to download / customize textures, setup a folder depending on current iModel
		Impl->OnMaterialPropertiesRetrieved(props, *this);
	}
}

void AITwinIModel::OnTextureDataRetrieved(bool bSuccess, std::string const& textureId, AdvViz::SDK::ITwinTextureData const& textureData)
{
	if (bSuccess)
	{
		Impl->OnTextureDataRetrieved(textureId, textureData);
	}
}

void AITwinIModel::OnMatMLPredictionRetrieved(bool bSuccess, AdvViz::SDK::ITwinMaterialPrediction const& Prediction,
	std::string const& error /*= {}*/)
{
	Impl->OnMatMLPredictionRetrieved(bSuccess, Prediction, error, *this);
}

void AITwinIModel::OnMatMLPredictionProgress(float fProgressRatio)
{
	Impl->OnMatMLPredictionProgress(fProgressRatio, *this);
}

void AITwinIModel::Retune()
{
	Impl->Retune();
}

void AITwinIModel::LoadDecoration()
{
	if (ITwinId.IsEmpty() || IModelId.IsEmpty())
	{
		BE_LOGE("ITwinAPI", "ITwinID and IModelId are required to load decoration");
		return;
	}

	// If no access token has been retrieved yet, make sure we request one.
	if (CheckServerConnection() != AdvViz::SDK::EITwinAuthStatus::Success)
	{
		Impl->PendingOperation = FImpl::EOperationUponAuth::LoadDecoration;
		return;
	}
	ITwin::LoadScene(ITwinId, GetWorld());
}

void AITwinIModel::FImpl::LoadDecorationIfNeeded()
{
	if (Owner.ITwinId.IsEmpty() || Owner.IModelId.IsEmpty())
		return;
	if (Owner.CheckServerConnection(false) != AdvViz::SDK::EITwinAuthStatus::Success)
		return;
	if (ITwin::ShouldLoadScene(Owner.ITwinId, Owner.GetWorld()))
	{
		ITwin::LoadScene(Owner.ITwinId, Owner.GetWorld());
	}
}

void AITwinIModel::SaveDecoration()
{
	ITwin::SaveScene(ITwinId, GetWorld());
}

void AITwinIModel::DetectCustomizedMaterials()
{
	// Detect user customizations (they are stored in the decoration service).
	Impl->DetectCustomizedMaterials(this);
}

void AITwinIModel::ReloadCustomizedMaterials()
{
	Impl->ReloadCustomizedMaterials();
	RefreshTileset();
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

FString AITwinIModel::GetMaterialName(uint64_t MaterialId, bool bForMaterialEditor /*= false*/) const
{
	FImpl::FITwinCustomMaterial const* Mat = Impl->GetCustomMaterials().Find(MaterialId);
	if (Mat)
	{
		if (bForMaterialEditor && !Mat->DisplayName.IsEmpty())
			return Mat->DisplayName;
		return Mat->Name;
	}
	else
		return {};
}

double AITwinIModel::GetMaterialChannelIntensity(uint64_t MaterialId, AdvViz::SDK::EChannelType Channel) const
{
	return Impl->GetMaterialChannelIntensity(MaterialId, Channel);
}

void AITwinIModel::SetMaterialChannelIntensity(uint64_t MaterialId, AdvViz::SDK::EChannelType Channel, double Intensity)
{
	Impl->SetMaterialChannelIntensity(MaterialId, Channel, Intensity,
		GetInternals(*this).SceneMapping);
}

FLinearColor AITwinIModel::GetMaterialChannelColor(uint64_t MaterialId, AdvViz::SDK::EChannelType Channel) const
{
	return Impl->GetMaterialChannelColor(MaterialId, Channel);
}

void AITwinIModel::SetMaterialChannelColor(uint64_t MaterialId, AdvViz::SDK::EChannelType Channel,
										   FLinearColor const& Color)
{
	Impl->SetMaterialChannelColor(MaterialId, Channel, Color,
		GetInternals(*this).SceneMapping);
}

UITwinMaterialDefaultTexturesHolder const& AITwinIModel::GetDefaultTexturesHolder()
{
	if (!IsValid(DefaultTexturesHolder))
	{
		CreateDefaultTexturesComponent();
	}
	return *DefaultTexturesHolder;
}

void AITwinIModel::CreateDefaultTexturesComponent()
{
	if (!IsValid(DefaultTexturesHolder))
	{
		DefaultTexturesHolder = NewObject<UITwinMaterialDefaultTexturesHolder>(this,
			UITwinMaterialDefaultTexturesHolder::StaticClass(),
			FName(*(GetActorNameOrLabel() + "_DftTexHolder")));
		DefaultTexturesHolder->RegisterComponent();
	}
}

void AITwinIModel::SetNeedForcedShadowUpdate()
{
	Impl->bForcedShadowUpdate = true;
}

FString AITwinIModel::GetMaterialChannelTextureID(uint64_t MaterialId, AdvViz::SDK::EChannelType Channel,
	AdvViz::SDK::ETextureSource& OutSource) const
{
	return Impl->GetMaterialChannelTextureID(MaterialId, Channel, OutSource);
}

void AITwinIModel::SetMaterialChannelTextureID(uint64_t MaterialId, AdvViz::SDK::EChannelType Channel,
	FString const& TextureId, AdvViz::SDK::ETextureSource eSource)
{
	Impl->SetMaterialChannelTextureID(MaterialId, Channel, TextureId, eSource,
		GetInternals(*this).SceneMapping,
		GetDefaultTexturesHolder());
}

AdvViz::SDK::ITwinUVTransform AITwinIModel::GetMaterialUVTransform(uint64_t MaterialId) const
{
	return Impl->GetMaterialUVTransform(MaterialId);
}

void AITwinIModel::SetMaterialUVTransform(uint64_t MaterialId, AdvViz::SDK::ITwinUVTransform const& UVTransform)
{
	Impl->SetMaterialUVTransform(MaterialId, UVTransform, GetInternals(*this).SceneMapping);
}

AdvViz::SDK::EMaterialKind AITwinIModel::GetMaterialKind(uint64_t MaterialId) const
{
	return Impl->GetMaterialKind(MaterialId);
}

void AITwinIModel::SetMaterialKind(uint64_t MaterialId, AdvViz::SDK::EMaterialKind NewKind)
{
	Impl->SetMaterialKind(MaterialId, NewKind, GetInternals(*this).SceneMapping);
}

bool AITwinIModel::GetMaterialCustomRequirements(uint64_t MaterialId, AdvViz::SDK::EMaterialKind& OutMaterialKind,
	bool& bOutRequiresTranslucency) const
{
	return Impl->GetMaterialCustomRequirements(MaterialId, OutMaterialKind, bOutRequiresTranslucency);
}

bool AITwinIModel::SetMaterialName(uint64_t MaterialId, FString const& NewName)
{
	return Impl->SetMaterialName(MaterialId, NewName);
}

bool AITwinIModel::LoadMaterialFromAssetFile(uint64_t MaterialId, FString const& AssetFilePath)
{
	return Impl->LoadMaterialFromAssetFile(MaterialId, AssetFilePath, *this);
}

std::shared_ptr<BeUtils::GltfMaterialHelper> const& AITwinIModel::GetGltfMaterialHelper() const
{
	return Impl->GetGltfMatHelper();
}

/*** Material persistence (through the Decoration Service for now...) ***/

/*static*/
void AITwinIModel::SetMaterialPersistenceManager(MaterialPersistencePtr const& Mngr)
{
	FITwinIModelMaterialHandler::SetGlobalPersistenceManager(Mngr);
}

/*static*/
AITwinIModel::MaterialPersistencePtr const& AITwinIModel::GetMaterialPersistenceManager()
{
	return FITwinIModelMaterialHandler::GetGlobalPersistenceManager();
}

void AITwinIModel::OnIModelOffsetChanged()
{
	Impl->SetLastTransforms();
	// Bounding boxes have always been stored in Unreal world space AS IF the iModel were untransformed
	// (see "Note 2" in UITwinSceneMappingBuilder::OnTileMeshPrimitiveConstructed), but this call _is_
	// necessary to reset the SceneMapping (esp. re-extract all translucent/transformed Elements to relocate
	// them correctly). Getting rid of this may be possible by manually relocating extracted Elements (but of
	// no use if we are to "soon" switch to retuning to replace extraction...)
	RefreshTileset();
	// Timelines store 3D paths keyframes as fully transformed Unreal-space coords (ie. including a possible
	// iModel transformation aka "offset"), directly in Add3DPathTransformToTimeline.
	// But even cut planes and static transforms, which are not immediately transformed to the final Unreal
	// world space, are then processed ("finalized") once in FinalizeCuttingPlaneEquation and
	// FinalizeAnchorPos, and these methods also apply the final iModel transformation, and are irreversible.
	// So getting rid of this is not so easy :/
	if (IsValid(Synchro4DSchedules) && ensure(bResolvedChangesetIdValid))
		// Note: already done in RefreshTileset for other reasons, but this only sets a flag anyway
		Synchro4DSchedules->ResetSchedules();
}



AITwinIModel::FTilesetAccess::FTilesetAccess(AITwinIModel* InIModel)
	: FITwinTilesetAccess(InIModel)
	, IModel(InIModel)
{

}

TUniquePtr<FITwinTilesetAccess> AITwinIModel::FTilesetAccess::Clone() const
{
	return MakeUnique<FTilesetAccess>(IModel.Get());
}

ITwin::ModelDecorationIdentifier AITwinIModel::FTilesetAccess::GetDecorationKey() const
{
	return std::make_pair(EITwinModelType::IModel,
		IModel.IsValid() ? IModel->IModelId : FString());
}

AITwinDecorationHelper* AITwinIModel::FTilesetAccess::GetDecorationHelper() const
{
	if (!IModel.IsValid())
		return nullptr;
	if (!IModel->Impl->DecorationPersistenceMgr)
	{
		IModel->Impl->FindPersistenceMgr();
	}
	return IModel->Impl->DecorationPersistenceMgr;
}


void AITwinIModel::FTilesetAccess::OnModelOffsetLoaded() const
{
	if (IModel.IsValid())
		IModel->OnIModelOffsetChanged();
}

void AITwinIModel::FTilesetAccess::RefreshTileset() const
{
	if (IModel.IsValid())
		IModel->RefreshTileset();
}

TUniquePtr<FITwinTilesetAccess> AITwinIModel::MakeTilesetAccess()
{
	return MakeUnique<FTilesetAccess>(this);
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
	UpdateWebServices();
	if (WebServices)
	{
		if (!WebServices->IsSetupForForMaterialMLPrediction())
		{
			WebServices->SetupForMaterialMLPrediction();
		}
		SetMaterialMLPredictionStatus(WebServices->GetMaterialMLPrediction(ITwinId,
			IModelId, GetSelectedChangeset()));
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

AITwinSavedView* AITwinIModel::GetITwinSavedViewActor(const FString& SavedViewId)
{
	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors);
	for (AActor* Actor : AttachedActors)
	{
		AITwinSavedView* SavedViewActor = Cast<AITwinSavedView>(Actor);
		if (SavedViewActor && SavedViewActor->ActorHasTag(*SavedViewId))
		{
			return SavedViewActor;
		}
	}
	return nullptr;
}

void AITwinIModel::UpdateSavedViews()
{
	//don't update saved views if it's already running an update
	if (bIsUpdatingSavedViews)
		return;
	UpdateWebServices();
	if (WebServices
		&& !IModelId.IsEmpty()
		&& !ITwinId.IsEmpty())
	{
		bIsUpdatingSavedViews = true;
		WebServices->GetSavedViewGroups(ITwinId, IModelId);
	}
}

void AITwinIModel::SetLightForForcedShadowUpdate(ULightComponent* DirLight)
{
	FImpl::LightForForcedShadowUpdate = DirLight;
	auto const Settings = GetDefault<UITwinIModelSettings>();
	FImpl::ForceShadowUpdateMaxEvery = Settings->IModelForceShadowUpdatesMillisec / 1000.f;
}

void AITwinIModel::ShowConstructionData(bool bShow)
{
	bShowConstructionData = bShow;
	GetInternals(*this).HideElements(
		bShowConstructionData ? std::unordered_set<ITwinElementID>()
		: GetInternals(*this).SceneMapping.ConstructionDataElements(),
		true);
	GetInternals(*this).HideModels(GetInternals(*this).SceneMapping.GetSavedViewHiddenModels(), true);
	GetInternals(*this).HideCategories(GetInternals(*this).SceneMapping.GetSavedViewHiddenCategories(), true);
	GetInternals(*this).HideElements(GetInternals(*this).SceneMapping.GetSavedViewHiddenElements(), false, true);
}

void AITwinIModel::UpdateConstructionData()
{
	GetInternals(*this).HideElements(
		bShowConstructionData ? std::unordered_set<ITwinElementID>()
		: GetInternals(*this).SceneMapping.ConstructionDataElements(),
		true, true);
}

void AITwinIModel::HideCategories(std::vector<std::string> const& InCategoryIDs, bool forceUpdate)
{
	GetInternals(*this).HideCategories(ITwin::InsertParsedIDs(InCategoryIDs), forceUpdate);
}

void AITwinIModel::HideModels(std::vector<std::string> const& InModelIDs, bool forceUpdate)
{
	GetInternals(*this).HideModels(ITwin::InsertParsedIDs(InModelIDs), forceUpdate);
}

void AITwinIModel::HideElements(std::vector<std::string> const& InElementIDs, bool forceUpdate)
{
	GetInternals(*this).HideElements(ITwin::InsertParsedIDs(InElementIDs), false, forceUpdate);
}

void AITwinIModel::ShowElements(std::vector<std::string> const& InElementIDs, bool forceUpdate)
{
	GetInternals(*this).ShowElements(ITwin::InsertParsedIDs(InElementIDs), forceUpdate);
}

void AITwinIModel::ShowCategoriesPerModel(std::vector<std::string> const& InModelIDs, std::vector<std::string> const& InCategoryIDs, bool forceUpdate)
{
	std::unordered_set<std::pair<ITwinElementID, ITwinElementID>, FITwinSceneTile::pair_hash> AlwaysDrawnCategoriesPerModel;
	for (int i = 0; i < InModelIDs.size(); i++)
	{
		AlwaysDrawnCategoriesPerModel.insert({ ITwin::ParseElementID(InCategoryIDs[i].c_str()),
											   ITwin::ParseElementID(InModelIDs[i].c_str())});
	}
	GetInternals(*this).ShowCategoriesPerModel(AlwaysDrawnCategoriesPerModel, /*false*/forceUpdate);
}

void AITwinIModel::HideCategoriesPerModel(std::vector<std::string> const& InModelIDs, std::vector<std::string> const& InCategoryIDs, bool forceUpdate)
{
	std::unordered_set<std::pair<ITwinElementID, ITwinElementID>, FITwinSceneTile::pair_hash> HiddenCategoriesPerModel;
	for (int i = 0; i < InModelIDs.size(); i++)
	{
		HiddenCategoriesPerModel.insert({ ITwin::ParseElementID(InCategoryIDs[i].c_str()),
										  ITwin::ParseElementID(InModelIDs[i].c_str()) });
	}
	GetInternals(*this).HideCategoriesPerModel(HiddenCategoriesPerModel, true);
}

/// Didn't find any other way to force an update of the shadows than to fake an update of the sun's rotation
/// (roll doesn't seem to be used anyway): tried using MarkRenderStateDirty() but didn't have an effect alone.
/// Also, using a timer here to delay the update a bit as it doesn't work the first time ShowConstruction() is
/// called (maybe because the update occurs before we actually hide anything).
/// Update frequency controlled by IModelForceShadowUpdatesMillisec setting from the UserEngine.ini file
void AITwinIModel::FImpl::ForceShadowUpdatesIfNeeded()
{
	if (FImpl::LightForForcedShadowUpdate.IsValid()
		&& (bForcedShadowUpdate
			|| (IsValid(Owner.Synchro4DSchedules) && Owner.Synchro4DSchedules->IsPlaying())))
	{
		double const CurTime = FImpl::ForceShadowUpdateMaxEvery == 0.f ? 0. : FPlatformTime::Seconds();

		bForcedShadowUpdate = false;
		// get current quality
		UGameUserSettings* Settings = GEngine->GetGameUserSettings();
		int32 OverallQualityLevel = 3;
		if (Settings)
			OverallQualityLevel = Settings->GetOverallScalabilityLevel();

		if (OverallQualityLevel >= 3 &&
			IsValid(Owner.Synchro4DSchedules) && !Owner.Synchro4DSchedules->IsPlaying())
		{
			Owner.Synchro4DSchedules->SetMeshesDynamicShadows(true);
			TWeakObjectPtr<UITwinSynchro4DSchedules> WeakSynchro4DSchedules(Owner.Synchro4DSchedules);
			Owner.GetWorld()->GetTimerManager().SetTimerForNextTick([WeakSynchro4DSchedules]()
				{
					if (WeakSynchro4DSchedules.IsValid())
						WeakSynchro4DSchedules->SetMeshesDynamicShadows(false);
				});
			return;
		}

		if (OverallQualityLevel < 3)
		{
			static double EpsilonRoll = 0.001;
			if (FImpl::ForceShadowUpdateMaxEvery == 0.f
				|| CurTime > (FImpl::LastForcedShadowUpdate + FImpl::ForceShadowUpdateMaxEvery))
			{
				FImpl::LastForcedShadowUpdate = CurTime;
				EpsilonRoll = -EpsilonRoll; // oscillate to avoid possible surprises with diverging value...
				FRotator SunRot = FImpl::LightForForcedShadowUpdate->GetComponentRotation();
				SunRot.Roll += EpsilonRoll;
				FImpl::LightForForcedShadowUpdate->SetWorldRotation(SunRot);
			}

			if (FImpl::ForceShadowUpdateMaxEvery != 0.f)
			{   // to make sure we have correct shadows at the end of the animation, we start a timer
				// we reuse the same handle, so it clears the previous one if any.
				static FTimerHandle TimerHandle;
				Owner.GetWorldTimerManager().SetTimer(TimerHandle,
					FTimerDelegate::CreateLambda([]
					{
						EpsilonRoll = -EpsilonRoll;  // oscillate to avoid possible surprises with diverging value...
						FRotator SunRot = FImpl::LightForForcedShadowUpdate->GetComponentRotation();
						SunRot.Roll += EpsilonRoll;
						FImpl::LightForForcedShadowUpdate->SetWorldRotation(SunRot);
					}),
					FImpl::ForceShadowUpdateMaxEvery, /*bLoop:*/false);
			}
		}

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
		savedViewActor->Tags.Add(*info.Id);
		savedViewActor->ServerConnection = ServerConnection;
		savedViewActor->SavedViewId = info.Id;
	}
	SavedViewsRetrieved.Broadcast(bSuccess, SavedViews);
	Impl->SavedViewsPageByPage[SavedViews.GroupId].OnSavedViewsRetrieved(bSuccess, SavedViews);
	if (groupsProgress.GroupsCount != 0 &&
		Impl->SavedViewsPageByPage[SavedViews.GroupId].GetState() ==
		AITwinIModel::FImpl::FRetrieveSavedViewsPageByPage::EState::Finished)
	{
		groupsProgress.GroupsProcessed++;
		ensure(groupsProgress.GroupsProcessed <= groupsProgress.GroupsCount + 1);
		if (SavedViews.GroupId == "")
		{
			UE_LOG(LogITwin, Display, TEXT("[SavedViews] Finished retrieving ungrouped saved views!"));
			groupsProgress.GroupsProcessed = 0;
			bAreSavedViewsLoaded = true;
			bIsUpdatingSavedViews = false;
			FinishedLoadingSavedViews.Broadcast(IModelId);
		}
		else if (groupsProgress.GroupsProcessed <= groupsProgress.GroupsCount)
		{
			UE_LOG(LogITwin, Display, TEXT("[SavedViews] Group Processed: %d/%d groups finished!"), groupsProgress.GroupsProcessed, groupsProgress.GroupsCount);
			if (groupsProgress.GroupsProcessed == groupsProgress.GroupsCount)
			{
				UE_LOG(LogITwin, Display, TEXT("[SavedViews] All groups finished...retrieving ungrouped saved views now!"));
				Impl->SavedViewsPageByPage[""].RetrieveNextPage();
			}
		}
	}
	else if (groupsProgress.GroupsCount == 0 &&
		Impl->SavedViewsPageByPage[SavedViews.GroupId].GetState() ==
		AITwinIModel::FImpl::FRetrieveSavedViewsPageByPage::EState::Finished)
	{
		UE_LOG(LogITwin, Display, TEXT("[SavedViews] Finished retrieving ungrouped saved views! (either this imodel does not contains any grouped ones or we called GetAllSavedViews directly instead of GetSavedViewGroups)"));
		bAreSavedViewsLoaded = true;
		bIsUpdatingSavedViews = false;
		FinishedLoadingSavedViews.Broadcast(IModelId);
	}
}

void AITwinIModel::OnSavedViewsRetrieved(bool bSuccess, FSavedViewInfos SavedViews)
{
	OnSavedViewInfosRetrieved(bSuccess, SavedViews);
}

void AITwinIModel::OnSavedViewGroupInfosRetrieved(bool bSuccess, FSavedViewGroupInfos const& SVGroups)
{
	SavedViewGroupsRetrieved.Broadcast(bSuccess, SVGroups);
	groupsProgress.GroupsCount = SVGroups.SavedViewGroups.Num();
	for (const auto& SavedViewGroup : SVGroups.SavedViewGroups)
	{
		Impl->SavedViewsPageByPage.Add(SavedViewGroup.Id, AITwinIModel::FImpl::FRetrieveSavedViewsPageByPage(*this));
		Impl->SavedViewsPageByPage[SavedViewGroup.Id].RetrieveNextPage(SavedViewGroup.Id);
	}
	// Currently, there is no way to only get the ungrouped saved views with GetAllSavedViews of the saved views api
	// (Unless we call GetSavedView on every saved view to retrieve its groupId, 
	// which can be time consuming depending on the number of saved views retrieved)
	// so we get all of them
	// Update: We should wait for previous calls to GetAllSavedViews to be finished before calling GetAllSavedViews again
	// Unless there are no groups
	if (groupsProgress.GroupsCount == 0)
	{
		Impl->SavedViewsPageByPage[""].RetrieveNextPage();
	}
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

const ACesium3DTileset* AITwinIModel::GetTileset() const
{
	return ITwin::TGetTileset<ACesium3DTileset const>(*this);
}

ACesium3DTileset* AITwinIModel::GetTileset()
{
	return ITwin::TGetTileset<ACesium3DTileset>(*this);
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
	savedViewActor->Tags.Add(*SavedViewInfo.Id);
	savedViewActor->ServerConnection = ServerConnection;
	savedViewActor->SavedViewId = SavedViewInfo.Id;
	SavedViewAdded.Broadcast(bSuccess, SavedViewInfo);
}

void AITwinIModel::OnSavedViewInfoAdded(bool bSuccess, FSavedViewInfo SavedViewInfo)
{
	OnSavedViewAdded(bSuccess, SavedViewInfo);
}

void AITwinIModel::OnSceneLoaded(bool success)
{

}

void AITwinIModel::AddSavedView(const FString& displayName, const FString& groupId /*=""*/)
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
	//check if there is a synchro schedule in the imodel
	if (ensure(Synchro4DSchedules != nullptr) && !(Synchro4DSchedules->ScheduleId.IsEmpty() || Synchro4DSchedules->ScheduleId.StartsWith(TEXT("Unknown"))))
	{
		//get current time of animation if any
		const auto& currentTime = Synchro4DSchedules->GetScheduleTime();
		NewSavedView.DisplayStyle.RenderTimeline = "0x20000003cda"; //fake Id for now
		NewSavedView.DisplayStyle.TimePoint = currentTime.ToUnixTimestamp();
	}
	UpdateWebServices();
	if (WebServices)
	{
		WebServices->AddSavedView(ITwinId, NewSavedView, {TEXT(""), displayName, true}, IModelId, groupId);
	}
}

void AITwinIModel::AddSavedViewGroup(const FString& groupName)
{
	FSavedViewGroupInfo GroupInfo = { "", groupName, true, false };
	UpdateWebServices();
	if (WebServices)
		WebServices->AddSavedViewGroup(ITwinId, IModelId, GroupInfo);
}

void AITwinIModel::OnSavedViewGroupAdded(bool bSuccess, FSavedViewGroupInfo const& GroupInfo)
{
	if (!bSuccess)
		return;
	SavedViewGroupAdded.Broadcast(bSuccess, GroupInfo);
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
		ACesium3DTileset* Tileset = Cast<ACesium3DTileset>(Child.Get());
		if (Tileset)
		{
			// Before refreshing the tileset, make sure we invalidate the mapping: note that 
			// OnTileMeshPrimitiveConstructed calls (during tile loading) occur intertwined with Element
			// metadata query replies: since both these functions will call ElementForSLOW which inserts new
			// FITwinElements into the AllElements collection, it ultimately means that Elements ranks are
			// RANDOM. And I emphasize this emphasis because when query replies are in the cache, the ranks
			// will APPEAR deterministic, because loading them from the cache is synchronous and blocks the
			// game thread (for simplification, and in contrary to reading the Schedule cache).
			// This is why, even though RefreshTileset is never called because of a change of changesetId
			// (MakeTileset would be), we must reload everything here, from Elements metadata to schedules,
			// otherwise Element ranks stored in 4D animation optim structures would be obsolete, which was
			// the underlying cause for azdev#1621189.
			Impl->ResetSceneMapping();
			Impl->ElementsMetadataQuerying->Restart();
			if (IsValid(Synchro4DSchedules) && ensure(bResolvedChangesetIdValid))
			{
				// Note: already done in RefreshTileset, but this only sets a flag anyway
				Synchro4DSchedules->ResetSchedules();
			}
			Impl->Internals.SceneMapping.SetIModel2UnrealTransfos(*this);
			// Also make sure we reload material info from the tuner
			Tileset->RefreshTileset();
			Tileset->SetLifecycleEventReceiver(Impl->SceneMappingBuilder.Get());
			Tileset->SetGltfModifier(Impl->GetTuner());
			break;
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
	if (Owner.CheckServerConnection() != AdvViz::SDK::EITwinAuthStatus::Success)
	{
		PendingOperation = FImpl::EOperationUponAuth::Load;
		return;
	}
	UpdateAfterLoadingUIEvent();
}

void AITwinIModel::ToggleMLMaterialPrediction(bool bActivate)
{
	this->ActivateMLMaterialPrediction(bActivate);
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

bool AITwinIModel::VisualizeMaterialMLPrediction() const
{
	return Impl->VisualizeMaterialMLPrediction();
}

void AITwinIModel::ValidateMLPrediction()
{
	if (!ensureMsgf(VisualizeMaterialMLPrediction(), TEXT("Material prediction not visible - cannot be validated")))
	{
		return;
	}
	Impl->ValidateMLPrediction();
	SetMaterialMLPredictionStatus(EITwinMaterialPredictionStatus::Validated);
}

void AITwinIModel::SetMaterialMLPredictionStatus(EITwinMaterialPredictionStatus InStatus)
{
	MLMaterialPredictionStatus = InStatus;

	// Should always been synced with the corresponding property in FITwinIModelMaterialHandler (this
	// duplication is kept to keep an access through blueprint and Editor in plugin context...)
	Impl->SetMaterialMLPredictionStatus(InStatus);
}

void AITwinIModel::ActivateMLMaterialPrediction(bool bActivate)
{
	bActivateMLMaterialPrediction = bActivate;

	// Same remark as above in #SetMaterialMLPredictionStatus
	Impl->ActivateMLMaterialPrediction(bActivate);
}

void AITwinIModel::SetMaterialMLPredictionObserver(IITwinWebServicesObserver* observer)
{
	Impl->SetMaterialMLPredictionObserver(observer);
}

IITwinWebServicesObserver* AITwinIModel::GetMaterialMLPredictionObserver() const
{
	return Impl->GetMaterialMLPredictionObserver();
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

void FITwinIModelInternals::OnVisibilityChanged(ITwin::CesiumTileID const& TileID, bool bVisible)
{
	auto* SceneTile = SceneMapping.FindKnownTileSLOW(TileID);
	// Actual processing is delayed because:
	// Short story: this is called from the middle of USceneComponent::SetVisibility, after the component's
	//		flag was set, but BEFORE it's children components' flags are set! (when applied recursively)
	// Long story: in the case of UITwinExtractedMeshComponent, the attachment hierarchy is thus...:
	//    Tileset --holds--> UCesiumGltfComponent --holds--> UCesiumGltfPrimitiveComponent
	// ...and when visibility is set from Cesium rules, this is the sequence of actions:
	//	1. UCesiumGltfComponent::SetVisibility
	//	2. USceneComponent::SetVisibility sets bVisible to true
	//	3. USceneComponent::SetVisibility calls OnVisibilityChanged
	//	4. our OnVisibilityChanged callbacks are called, and we reach this method
	//	5. USceneComponent::SetVisibility calls SetVisibility on the children (attached) components
	// If Synchro4DSchedules->OnVisibilityChanged is called from here directly (ie at 4.), ApplyAnimation is
	// processed and thus extracted element's UITwinExtractedMeshComponent::SetFullyHidden methods are called
	// BEFORE the UCesiumGltfPrimitiveComponent pParent below is thought to be visible! It would be
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
			Internals.SceneMapping.OnVisibilityChanged(SceneTile, bVisible,
				Owner.Synchro4DSchedules
					? Owner.Synchro4DSchedules->bUseGltfTunerInsteadOfMeshExtraction : false);
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
				SceneTile.pCesiumTile->SetRenderReady(true);
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
	if (IsValid(GEngine)) // GEngine is invalid when closing the Editor...
		GEngine->OnWorldDestroyed().RemoveAll(Impl.Get());
}

void AITwinIModel::EndPlay(const EEndPlayReason::Type EndPlayReason) /*override*/
{
	Impl->Internals.Uniniter->Run();
	GEngine->OnWorldDestroyed().RemoveAll(Impl.Get());
	Super::EndPlay(EndPlayReason);
}

void FITwinIModelInternals::OnNewTileBuilt(ITwin::CesiumTileID const& TileID)
{
	auto* SceneTile = SceneMapping.FindKnownTileSLOW(TileID);
	if (!SceneTile)
	{
		// No assert here: it can happen that a tile does not contain any primitive we support, or that its
		// content is subdivided (for raster overlay), and thus discarded.
		return;
	}
	SceneMapping.OnNewTileBuilt(*SceneTile);
	if (Owner.Synchro4DSchedules
		&& GetInternals(*Owner.Synchro4DSchedules).OnNewTileBuilt(*SceneTile))
	{
		TilesPendingRenderReadiness.insert(SceneMapping.KnownTileRank(*SceneTile));
	}
}

void FITwinIModelInternals::UnloadKnownTile(ITwin::CesiumTileID const& TileID)
{
	auto* Known = SceneMapping.FindKnownTileSLOW(TileID);
	if (Known)
	{
		if (IsValid(Owner.Synchro4DSchedules))
			GetInternals(*Owner.Synchro4DSchedules).UnloadKnownTile(*Known,
																	SceneMapping.KnownTileRank(*Known));
		SceneMapping.UnloadKnownTile(*Known);
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
	if (!SchedInternals.PrefetchWholeSchedule())
	{
		int Index = -1;
		// Optimize this if we use it again, as GetElementTimelineFor rehashes just to find the index...
		// (could static_assert that timeline container is a vector and use distance to timeline 0)
		ensure(false);
		(void)SchedInternals.GetTimeline().GetElementTimelineFor(ModifiedTimeline.GetIModelElementsKey(),
																 &Index);
		SceneMapping.ForEachKnownTile([&](FITwinSceneTile& SceneTile)
		{
			SceneMapping.OnElementsTimelineModified(SceneTile, ModifiedTimeline, OnlyForElements,
				Schedules->bUseGltfTunerInsteadOfMeshExtraction,
				SchedInternals.TileTunedForSchedule(SceneTile),
				Index);
		});
	}
}

void FITwinIModelInternals::LogScheduleDownloadProgressed()
{
	if (std::abs(LastScheduleDownloadProgressLogged - Owner.ScheduleDownloadPercentComplete) >= 1.)
	{
		LastScheduleDownloadProgressLogged = std::floor(Owner.ScheduleDownloadPercentComplete);
		UE_LOG(LogITwin, Display, TEXT("Total 4D download progress: %d%%..."),
			   (int)LastScheduleDownloadProgressLogged);
	}
}

void FITwinIModelInternals::OnScheduleDownloadProgressed(double PercentComplete)
{
	Owner.ScheduleDownloadPercentComplete =
		(100. * AITwinIModel::FImpl::FQueryElementMetadataPageByPage::MetadataRatioInTotalProgress)
		+ (1. - AITwinIModel::FImpl::FQueryElementMetadataPageByPage::MetadataRatioInTotalProgress)
			* PercentComplete;
	LogScheduleDownloadProgressed();
}

namespace
{
	static bool bPickUponClickInViewport = true;
	static bool bLogTimelineUponSelectElement = false;
	static bool bLogPropertiesUponSelectElement = true;
	static bool bLogTileUponSelectElement = false;
}

bool FITwinIModelInternals::OnClickedElement(ITwinElementID const Element, FHitResult const& HitResult,
	bool const bSelectElement /*= true*/)
{
	auto const LastSelected = SceneMapping.GetSelectedElement();
	if (bPickUponClickInViewport && !SceneMapping.PickVisibleElement(Element, bSelectElement))
	{
		// filtered out, most likely Element is masked out by Saved view, as Construction data, or by 4D
		return false;
	}
	// Do not "describe" Element (dev logs) when just picking for spline (cut-out), population, ...
	if (bSelectElement && LastSelected != SceneMapping.GetSelectedElement())
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
	ITwinScene::ElemIdx Rank;
	auto const* pElem = SceneMapping.GetElementForSLOW(Element, &Rank);
	FString Ancestry = FString::Printf(TEXT("0x%I64x"), Element.value());
	if (nullptr == pElem)
	{
		Ancestry += TEXT(" is UNKNOWN!");
	}
	else
	{
		while (ITwinScene::NOT_ELEM != pElem->ParentInVec)
		{
			pElem = &SceneMapping.ElementFor(pElem->ParentInVec);
			Ancestry += TEXT('>');
			Ancestry += FString::Printf(TEXT("0x%I64x"), pElem->ElementID.value());
		}
	}
	FGuid ElemGuid;
	(void)SceneMapping.FindGUIDForElement(Rank, ElemGuid);
	UE_LOG(LogITwin, Display,
		TEXT("Element %s %s (MeshComp 0x%I64x) is in iModel %s, BBox %s centered on %s (in iModel spatial coords.: %s)"),
		*Ancestry, *ElemGuid.ToString(EGuidFormats::DigitsWithHyphensInBraces), (uint64_t)HitComponent.Get(),
		*Owner.GetActorNameOrLabel(), *BBox.ToString(), *BBox.GetCenter().ToString(), *CtrIModel.ToString());

#if ENABLE_DRAW_DEBUG
	// Draw element bounding box for a few seconds for debugging
	if (BBox.IsValid && ITwin::bDrawDebugBoxes)
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
		if (ITwin::bDrawDebugBoxes)
		{
			DrawDebugBox(Owner.GetWorld(), HitComponent->Bounds.Origin, HitComponent->Bounds.BoxExtent,
				FColor::Blue, /*bool bPersistent =*/ false, /*float LifeTime =*/ 10.f);
		}
#endif // ENABLE_DRAW_DEBUG

		auto const Found = SceneMapping.FindOwningTileSLOW(HitComponent.Get());
		if (auto* SceneTile = Found.first)
		{
			// Display the owning tile's bounding box
#if ENABLE_DRAW_DEBUG
			if (ITwin::bDrawDebugBoxes)
			{
				ITwinDebugBoxNextLifetime = 5.f;

				SceneTile->DrawTileBox(Owner.GetWorld());
			}
#endif // ENABLE_DRAW_DEBUG
			// Log the Tile ID
			FString const TileIdString(SceneTile->GetIDString());
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
	if (bLogTimelineUponSelectElement)
	{
		if (!ElementTimelineDescription.IsEmpty())
		{
			UE_LOG(LogITwin, Display, TEXT("ElementID 0x%I64x has a timeline:\n%s"),
									  Element.value(), *ElementTimelineDescription);
		}
		else
		{
			UE_LOG(LogITwin, Display, TEXT("ElementID 0x%I64x has no timeline"), Element.value());
		}
		auto const& Duplicates = SceneMapping.GetDuplicateElements(Element);
		if (!Duplicates.empty())
		{
			FString DuplList;
			for (auto const& Dupl : Duplicates)
			{
				auto DuplID = SceneMapping.ElementFor(Dupl).ElementID;
				if (DuplID != Element)
					DuplList = FString::Printf(TEXT("%s 0x%I64x"), *DuplList, DuplID.value());
			}
			UE_LOG(LogITwin, Display, TEXT("ElementID 0x%I64x has duplicates:\n%s"), Element.value(), 
									  *DuplList);
		}
		else
		{
			UE_LOG(LogITwin, Display, TEXT("ElementID 0x%I64x has no duplicates."), Element.value());
		}
	}
	else if (!ElementTimelineDescription.IsEmpty())
	{
		UE_LOG(LogITwin, Display, TEXT("ElementID 0x%I64x has a timeline (call \"cmd.ITwinTweakViewportClick logtimeline on\" to log it)"), Element.value());
	}
	else
	{
		UE_LOG(LogITwin, Display, TEXT("ElementID 0x%I64x has no timeline"), Element.value());
	}
}

void FITwinIModelInternals::SetNeedForcedShadowUpdate() const
{
	Owner.SetNeedForcedShadowUpdate();
}

void FITwinIModelInternals::HideElements(std::unordered_set<ITwinElementID> const& InElementIDs,
										 bool IsConstruction, bool Force /*=false*/)
{
	SceneMapping.HideElements(InElementIDs, IsConstruction, Force);
	SetNeedForcedShadowUpdate();
}

void FITwinIModelInternals::ShowElements(std::unordered_set<ITwinElementID> const& InElementIDs, bool Force /*=false*/)
{
	SceneMapping.ShowElements(InElementIDs, Force);
	SetNeedForcedShadowUpdate();
}

void FITwinIModelInternals::HideModels(std::unordered_set<ITwinElementID> const& InModelIDs, bool Force /*=false*/)
{
	SceneMapping.HideModels(InModelIDs, Force);
	SetNeedForcedShadowUpdate();
}

void FITwinIModelInternals::HideCategories(std::unordered_set<ITwinElementID> const& InCategoryIDs, bool Force /*=false*/)
{
	SceneMapping.HideCategories(InCategoryIDs, Force);
	SetNeedForcedShadowUpdate();
}

void FITwinIModelInternals::HideCategoriesPerModel(std::unordered_set<std::pair<ITwinElementID,ITwinElementID>, FITwinSceneTile::pair_hash> const& InCategoryPerModelIDs, bool Force /*=false*/)
{
	SceneMapping.HideCategoriesPerModel(InCategoryPerModelIDs, Force);
	SetNeedForcedShadowUpdate();
}

void FITwinIModelInternals::ShowCategoriesPerModel(std::unordered_set<std::pair<ITwinElementID, ITwinElementID>, FITwinSceneTile::pair_hash> const& InCategoryPerModelIDs, bool Force /*=false*/)
{
	SceneMapping.ShowCategoriesPerModel(InCategoryPerModelIDs, Force);
	SetNeedForcedShadowUpdate();
}

ITwinElementID FITwinIModelInternals::GetSelectedElement() const
{
	return SceneMapping.GetSelectedElement();
}

void FITwinIModelInternals::SelectMaterial(ITwinMaterialID const& InMaterialID)
{
	std::optional<AdvViz::SDK::ITwinColor> ColorToRestore;
	// In material prediction mode, we directly override the material color for highlight => restore the
	// original color when de-selecting material.
	if (Owner.VisualizeMaterialMLPrediction()
		&& InMaterialID == ITwin::NOT_MATERIAL
		&& SceneMapping.GetSelectedMaterial() != ITwin::NOT_MATERIAL)
	{
		auto const LinearColor = Owner.GetMaterialChannelColor(
			SceneMapping.GetSelectedMaterial().getValue(),
			AdvViz::SDK::EChannelType::Color);
		ColorToRestore = { LinearColor.R, LinearColor.G, LinearColor.B, LinearColor.A };
	}
	SceneMapping.PickVisibleMaterial(InMaterialID, Owner.VisualizeMaterialMLPrediction(), ColorToRestore);
}

void FITwinIModelInternals::DeSelectAll()
{
	FITwinTextureUpdateDisabler TexUpdateDisabler(SceneMapping);
	SceneMapping.PickVisibleElement(ITwin::NOT_ELEMENT);
	SelectMaterial(ITwin::NOT_MATERIAL);
}

void AITwinIModel::DeSelectElements()
{
	GetInternals(*this).SceneMapping.PickVisibleElement(ITwin::NOT_ELEMENT);
}

void AITwinIModel::DeSelectMaterials()
{
	GetInternals(*this).SelectMaterial(ITwin::NOT_MATERIAL);
}

void AITwinIModel::DeSelectAll()
{
	GetInternals(*this).DeSelectAll();
}

void AITwinIModel::HighlightMaterial(uint64 MaterialID)
{
	FITwinIModelInternals& ModelInternals(GetInternals(*this));

	// Postpone texture updates to the end of the whole processing.
	FITwinTextureUpdateDisabler TexUpdateDisabler(ModelInternals.SceneMapping);

	// Remove previous highlight.
	ModelInternals.DeSelectAll();

	// Highlight the selected material (in all tiles of the iModel).
	ModelInternals.SelectMaterial(ITwinMaterialID{ MaterialID });
}


UITwinClippingCustomPrimitiveDataHelper* AITwinIModel::GetClippingHelper() const
{
	return Impl->ClippingHelper.Get();
}

bool AITwinIModel::MakeClippingHelper()
{
	if (IModelId.IsEmpty())
		return false;

	Impl->ClippingHelper =
		TStrongObjectPtr<UITwinClippingCustomPrimitiveDataHelper>(NewObject<UITwinClippingCustomPrimitiveDataHelper>(this));
	Impl->ClippingHelper->SetModelIdentifier(
		std::make_pair(EITwinModelType::IModel, IModelId));
	return true;
}


namespace ITwin
{
	std::optional<bool> ToggleFromCmdArg(const TArray<FString>& Args, int Idx)
	{
		if (Args.Num() <= Idx)
		{
			UE_LOG(LogITwin, Error, TEXT("Need at least %d args"), Idx + 1);
			return {};
		}
		std::optional<bool> toggle;
		if (Args[Idx] == TEXT("1") || Args[Idx] == TEXT("true") || Args[Idx] == TEXT("on"))
			toggle = true;
		else if (Args[Idx] == TEXT("0") || Args[Idx] == TEXT("false") || Args[Idx] == TEXT("off"))
			toggle = false;
		if (!toggle)
		{
			UE_LOG(LogITwin, Error, TEXT("arg #%d must be 0, 1, true, false, on or off"), Idx);
		}
		return toggle;
	}
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
				auto const* SceneElem = IModelInt.SceneMapping.GetElementForSLOW(SelectedElement);
				auto* Schedules = InIModel->FindComponentByClass<UITwinSynchro4DSchedules>();
				// If animated, try to set the current time to when the Element is (partly) visible
				if (Schedules && SceneElem && !SceneElem->AnimationKeys.empty())
				{
					auto* ElementTimeline = GetInternals(*Schedules).GetTimeline()
						.GetElementTimelineFor(SceneElem->AnimationKeys[0]);
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
				IModelInt.SceneMapping.PickVisibleElement(SelectedElement);
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
	if (ChannelId >= (int32)AdvViz::SDK::EChannelType::ENUM_END)
	{
		BE_LOGE("ITwinAPI", "Invalid material channel " << ChannelId);
		return;
	}
	for (TActorIterator<AITwinIModel> IModelIter(World); IModelIter; ++IModelIter)
	{
		(*IModelIter)->SetMaterialChannelIntensity(MatID,
			static_cast<AdvViz::SDK::EChannelType>(ChannelId), Intensity);
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
				FString const IdStr = Tile.GetIDString()/*.ToLower() <== Contains defaults to "ignore case"*/;
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
	TEXT("Configure what happens when clicking in the viewport, usually to pick an Element: first arg must *contain* one or more of 'pick', 'logtimeline' (to log timelines), 'logprop' (to log properties) and/or 'logtile' (to log tile impl details), second arg must be 0, 1, true, false, on or off."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	if (Args.Num() != 2)
	{
		UE_LOG(LogITwin, Error, TEXT("Need exactly 2 args"));
		return;
	}
	std::optional<bool> toggle = ITwin::ToggleFromCmdArg(Args, 1);
	if (!toggle)
		return;
	// "Contains" ignores case by default:
	bool const pick = Args[0].Contains(TEXT("pick"));
	bool const timeline = Args[0].Contains(TEXT("logtimeline"));
	bool const properties = Args[0].Contains(TEXT("logprop"));
	bool const tile = Args[0].Contains(TEXT("logtile"));
	bool const drawboxes = Args[0].Contains(TEXT("drawboxes"));
	if (!timeline && !properties && !pick && !tile && !drawboxes)
	{
		UE_LOG(LogITwin, Error, TEXT("First arg must *contain* one or more of 'pick', 'logtimeline', 'logprop', 'logtile' and/or 'drawboxes'"));
		return;
	}
	if (pick)
		bPickUponClickInViewport = *toggle;
	if (properties)
		bLogPropertiesUponSelectElement = *toggle;
	if (timeline)
		bLogTimelineUponSelectElement = *toggle;
	if (tile)
		bLogTileUponSelectElement = *toggle;
	if (drawboxes)
		ITwin::bDrawDebugBoxes = *toggle;
	UE_LOG(LogITwin, Display, TEXT("Summary of flags: Pick:%d LogProperties:%d LogTimelines:%d LogTiles:%d DrawBoxes:%d"),
							  bPickUponClickInViewport, bLogPropertiesUponSelectElement,
							  bLogTimelineUponSelectElement, bLogTileUponSelectElement, ITwin::bDrawDebugBoxes);
}));

FITwinSceneTile* SceneTileFrom1stCmdArgs(const TArray<FString>& Args, UWorld* World)
{
	const ITwinScene::TileIdx TileRank(FCString::Atoi(*Args[0]));
	TActorIterator<AITwinIModel> IModelIter(World);
	if (!IModelIter)
		return nullptr;
	auto const& IModelInt = GetInternals(**IModelIter);
	auto& ByRank = IModelInt.SceneMapping.KnownTiles.get<IndexByRank>();
	if (TileRank.value() < 0 || TileRank.value() >= ByRank.size())
		return nullptr;
	return const_cast<FITwinSceneTile*>(&ByRank[TileRank.value()]);
}

static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinResetTileMaterials(
	TEXT("cmd.ITwinResetTileMaterials"),
	TEXT("Set a new unconfigured material instance based on the Tileset material templates on all meshes " \
		"of the given tile (in the 1st iModel found ; pass -1 or any invalid index to process all tiles " \
		"of all iModels).\n" \
		"Pass 1/on/true to use the translucent material (with ForcedOpacity set to 0.5),\n" \
		"Pass 0/off/false or nothing to use the masked material."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	auto* SceneTile = SceneTileFrom1stCmdArgs(Args, World);
	//if (!SceneTile) <= will process ALL tiles
	std::optional<bool> optUseTranslucent;
	if (Args.Num() >= 2)
		optUseTranslucent = ITwin::ToggleFromCmdArg(Args, 1);
	else
		optUseTranslucent = false;
	static uint32_t NextMaterialId = 0;
	const FName ImportedSlotName(*(TEXT("ITwinResetTileMaterial_") + FString::FromInt(NextMaterialId++)));
	TActorIterator<AITwinIModel> IModelIter(World);
	if (!IModelIter || !IsValid((*IModelIter)->Synchro4DSchedules))
	{
		UE_LOG(LogITwin, Error, TEXT("No iModel, or invalid schedule component"));
		return;
	}
	auto* BaseMat = (*optUseTranslucent) ? (*IModelIter)->Synchro4DSchedules->BaseMaterialTranslucent
										 : (*IModelIter)->Synchro4DSchedules->BaseMaterialMasked;
	if (!IsValid(BaseMat))
	{
		UE_LOG(LogITwin, Error, TEXT("Invalid material!"));
		return;
	}
	UMaterialInstanceDynamic* pNewMaterial =
		UMaterialInstanceDynamic::Create(BaseMat, nullptr, ImportedSlotName);
	if ((*optUseTranslucent))
		FITwinSceneMapping::SetForcedOpacity(pNewMaterial, .5f);
	auto const& fncResetMat = [pNewMaterial](FITwinSceneTile& SceneTile)
		{
			for (auto&& M : SceneTile.GltfMeshWrappers())
			{
				auto MC = M.MeshComponent();
				if (MC)
					MC->SetMaterial(0, pNewMaterial);
			}
			SceneTile.ForEachExtractedEntity([pNewMaterial](FITwinExtractedEntity& Extr)
				{
					if (Extr.ExtractedMeshComponent.IsValid())
						Extr.ExtractedMeshComponent->SetMaterial(0, pNewMaterial);
				});
		};
	if (SceneTile)
		fncResetMat(*SceneTile);
	else
	{
		for ( ; IModelIter; ++IModelIter)
		{
			auto& IModelInt = GetInternals(**IModelIter);
			IModelInt.SceneMapping.ForEachKnownTile(fncResetMat);
		}
	}
}));

static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinStopAnimInTile(
	TEXT("cmd.ITwinStopAnimInTile"),
	TEXT("Force disabling 4D anim in the given tile (in the first iModel found)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	auto* SceneTile = SceneTileFrom1stCmdArgs(Args, World);
	TActorIterator<AITwinIModel> IModelIter(World);
	if (!IModelIter || !SceneTile || !IsValid((*IModelIter)->Synchro4DSchedules))
	{
		UE_LOG(LogITwin, Error, TEXT("No iModel, tile not found, or invalid schedule component"));
		return;
	}
	(*IModelIter)->Synchro4DSchedules->DisableAnimationInTile(SceneTile);
}));

static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinGetTicks(
	TEXT("cmd.ITwinGetTicks"),
	TEXT("Print tick count matching date string (useful to set up conditional breakpoints...)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
		{
			if (!Args.Num())
				return;
			FDateTime Date;
			if (FDateTime::ParseIso8601(*Args[0], Date))
			{
				UE_LOG(LogITwin, Display, TEXT("%s = %lld"), *Args[0], Date.GetTicks());
			}
			else
			{
				UE_LOG(LogITwin, Error, TEXT("Date/time parsing error with: %s"), *Args[0]);
			}
		}));

#endif // ENABLE_DRAW_DEBUG