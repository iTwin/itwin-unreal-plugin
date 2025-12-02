/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinIModelMaterialHandler.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#include <Material/ITwinIModelMaterialHandler.h>

#include <Material/ITwinMaterialDefaultTexturesHolder.h>
#include <Material/ITwinMaterialLibrary.h>
#include <Material/ITwinMaterialLoadingUtils.h>
#include <Material/ITwinMaterialPreviewHolder.h>
#include <Material/ITwinTextureLoadingUtils.h>

#include <Network/JsonQueriesCache.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <Misc/Paths.h>

#include <ITwinElementID.h>
#include <ITwinExtractedMeshComponent.h>
#include <ITwinSceneMapping.h>
#include <ITwinIModel.h>
#include <ITwinIModelInternals.h>
#include <ITwinSceneMappingBuilder.h>

#include <Components/StaticMeshComponent.h>
#include <Engine/StaticMesh.h>
#include <Engine/StaticMeshActor.h>
#include <Engine/Texture2D.h>
#include <EngineUtils.h> // for TActorIterator<>
#include <ImageUtils.h>
#include <Materials/MaterialInstanceDynamic.h>
#include <Misc/Paths.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeBuildConfig/MaterialTuning.h>
#	include <BeHeaders/Compil/AlwaysFalse.h>
#	include <BeHeaders/Util/CleanUpGuard.h>
#	include <BeUtils/Gltf/GltfMaterialHelper.h>
#	include <BeUtils/Gltf/GltfMaterialTuner.h>
#	include <BeUtils/Gltf/GltfTuner.h>
#	include <BeUtils/Misc/MiscUtils.h>
#	include <Core/ITwinAPI/ITwinMaterial.h>
#	include <Core/ITwinAPI/ITwinMaterial.inl>
#	include <Core/ITwinAPI/ITwinMaterialPrediction.h>
#	include <Core/ITwinAPI/ITwinTypes.h>
#	include <Core/Tools/Assert.h>
#	include <Core/Tools/Log.h>
#	include <Core/Visualization/MaterialPersistence.h>
#include <Compil/AfterNonUnrealIncludes.h>

// We no longer change the material names in the iModel's material list.
// (it makes no sense as it would not be reflected elsewhere)
#define ITWIN_EDIT_MATERIAL_NAME_IN_MODEL() 0

namespace ITwin
{
	ITWINRUNTIME_API bool IsMLMaterialPredictionEnabled();
}


FITwinIModelMaterialHandler::FITwinIModelMaterialHandler()
{
	GltfMatHelper = std::make_shared<BeUtils::GltfMaterialHelper>();
}

/// Lazy-initialize most of the stuff formerly done either in the constructor (iModel's or its Impl's), or
/// even in PostLoad. The problem was that this stuff is only useful for "active" actors, ie either played
/// in-game (or PIE), or used interactively in the Editor, BUT various other actors are instantiated, eg. the
/// ClassDefaultObject, but also when packaging levels, etc. Even deleting an actor from the Editor's Outliner
/// apparently reloads the current level and re-created an ("invisible") iModel, popping an auth browser if
/// needed, re-downloading its schedule in the background (because its local cache was already used), etc.
/// All that for a transient object! Only "activating" the iModel in its Tick() method should prevent all this.
void FITwinIModelMaterialHandler::Initialize(std::shared_ptr<BeUtils::GltfTuner> const& InTuner,
	AITwinIModel* OwnerIModel /*= nullptr*/)
{
	GltfTuner = InTuner;
	if (!GltfTuner) // debugging only...
		return;
	GltfTuner->SetMaterialHelper(GltfMatHelper);
	// create a callback to fill our scene mapping when meshes are loaded

	if (ITwin::HasMaterialTuning())
	{
		// In case we need to download / customize textures, setup a folder depending on current iModel
		InitTextureDirectory(OwnerIModel);

		// As soon as material IDs are read, launch a request to RPC service to get the corresponding material
		// properties.
		GltfTuner->SetMaterialInfoReadCallback(
			[this, IModelPtr = TWeakObjectPtr<AITwinIModel>(OwnerIModel)]
			(std::vector<BeUtils::ITwinMaterialInfo> const& MaterialInfos)
		{
			// Initialize the map of customizable materials at once.

			bool const bAttachedToImodel = IModelPtr.IsValid();

			FillMaterialInfoFromTuner(bAttachedToImodel ? IModelPtr.Get() : nullptr);

			// Initialize persistence at low level, if any.
			auto const& MaterialPersistenceMngr = GetPersistenceManager();
			if (MaterialPersistenceMngr
				&& bAttachedToImodel
				&& ensure(!IModelPtr->IModelId.IsEmpty()))
			{
				GltfMatHelper->SetPersistenceInfo(TCHAR_TO_ANSI(*IModelPtr->IModelId), MaterialPersistenceMngr);
			}

			// Pre-fill material slots in the material helper, and detect potential user customizations
			// (in Carrot MVP, they are stored in the decoration service).
			int NumCustomMaterials = 0;
			{
				BeUtils::WLock Lock(GltfMatHelper->GetMutex());
				for (BeUtils::ITwinMaterialInfo const& MatInfo : MaterialInfos)
				{
					GltfMatHelper->CreateITwinMaterialSlot(MatInfo.id, MatInfo.name, Lock);
				}
			}
			// If material customizations were already loaded from the decoration server (this is done
			// asynchronously), detect them at once so that the 1st displayed tileset directly shows the
			// customized materials. If not, this will be done when the decoration data has finished loading,
			// which may trigger a re-tuning, and thus some visual artifacts...
			DetectCustomizedMaterials(bAttachedToImodel ? IModelPtr.Get() : nullptr);

			// Then launch a request to fetch all material properties.
			TArray<FString> MaterialIds;
			MaterialIds.Reserve(MaterialInfos.size());
			Algo::Transform(MaterialInfos, MaterialIds,
				[](BeUtils::ITwinMaterialInfo const& V) -> FString
			{
				return FString::Printf(TEXT("0x%I64x"), V.id);
			});
			if (bAttachedToImodel)
			{
				IModelPtr->GetMutableWebServices()->GetMaterialListProperties(
					IModelPtr->ITwinId, IModelPtr->IModelId, IModelPtr->GetSelectedChangeset(),
					MaterialIds);
			}
		});
	}
}

TMap<uint64, FITwinIModelMaterialHandler::FITwinCustomMaterial> const&
FITwinIModelMaterialHandler::GetCustomMaterials() const
{
	return VisualizeMaterialMLPrediction() ? MLPredictionMaterials : ITwinMaterials;
}

TMap<uint64, FITwinIModelMaterialHandler::FITwinCustomMaterial>&
FITwinIModelMaterialHandler::GetMutableCustomMaterials()
{
	return VisualizeMaterialMLPrediction() ? MLPredictionMaterials : ITwinMaterials;
}

namespace
{
	static FString BuildTextureDirectoryForIModel(AITwinIModel const* IModel,
		FString const& DirNameIfNoModel = TEXT("Common"))
	{
		FString TextureDir = FPlatformProcess::UserSettingsDir();
		if (!TextureDir.IsEmpty())
		{
			// TODO_JDE - Should it depend on the changeset?
			FString const IModelId = IModel ? IModel->IModelId : DirNameIfNoModel;
			TextureDir = FPaths::Combine(TextureDir,
				TEXT("Bentley"), TEXT("Cache"), TEXT("Textures"), *IModelId);
		}
		return TextureDir;
	}
}

void FITwinIModelMaterialHandler::InitTextureDirectory(AITwinIModel const* IModel)
{
	// In case we need to download textures, setup a destination folder depending on current iModel
	const FString TextureDir = BuildTextureDirectoryForIModel(IModel);
	if (!TextureDir.IsEmpty())
	{
		BeUtils::WLock Lock(GltfMatHelper->GetMutex());
		GltfMatHelper->SetTextureDirectory(*TextureDir, Lock);
	}
}

bool FITwinIModelMaterialHandler::SetMaterialName(uint64_t MaterialId, FString const& NewName)
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
		CustomMat->DisplayName = NewName;
#if ITWIN_EDIT_MATERIAL_NAME_IN_MODEL()
		CustomMat->Name = NewName;
#endif
		return true;
	}
	else
	{
		return false;
	}
}


void FITwinIModelMaterialHandler::OnMaterialPropertiesRetrieved(AdvViz::SDK::ITwinMaterialPropertiesMap const& props,
	AITwinIModel& IModel)
{
	// In case we need to download / customize textures, setup a folder depending on current iModel
	InitTextureDirectory(&IModel);

	BeUtils::WLock lock(GltfMatHelper->GetMutex());

	auto& CustomMaterials = ITwinMaterials;

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

			GltfMatHelper->SetITwinMaterialProperties(id64.value(), matProperties,
				TCHAR_TO_UTF8(*CustomMat->Name),
				lock);
		}
	}

	// Start downloading iTwin textures.
	auto* WebServices = IModel.GetMutableWebServices();
	if (ensure(WebServices))
	{
		std::vector<std::string> textureIds;
		GltfMatHelper->ListITwinTexturesToDownload(textureIds, lock);
		for (std::string const& texId : textureIds)
		{
			WebServices->GetTextureData(IModel.ITwinId, IModel.IModelId, IModel.GetSelectedChangeset(),
				FString(texId.c_str()));
		}
	}

	// Also convert available textures to Cesium format, if they are needed in the tuning.
	std::unordered_map<AdvViz::SDK::TextureKey, std::string> texturesToResolve;
	AdvViz::SDK::TextureUsageMap usageMap;
	GltfMatHelper->ListITwinTexturesToResolve(texturesToResolve, usageMap, lock);
	std::filesystem::path const textureDir = GltfMatHelper->GetTextureDirectory(lock);

	lock.unlock();
	if (!texturesToResolve.empty())
	{
		ITwin::ResolveITwinTextures(texturesToResolve, usageMap, GltfMatHelper, textureDir);
	}
}

void FITwinIModelMaterialHandler::OnTextureDataRetrieved(std::string const& textureId, AdvViz::SDK::ITwinTextureData const& textureData)
{
	std::filesystem::path texturePath;
	if (GltfMatHelper->SetITwinTextureData(textureId, textureData, texturePath))
	{
		// Convert texture to Cesium format at once (or else any future material tuning involving the parent
		// material would fail in packaged mode...)
		const AdvViz::SDK::TextureKey texKey = { textureId, AdvViz::SDK::ETextureSource::ITwin };

		std::unordered_map<AdvViz::SDK::TextureKey, std::string> texturesToResolve;
		texturesToResolve.emplace(texKey, texturePath.filename().generic_string());

		AdvViz::SDK::TextureUsageMap usageMap;
		auto const& MaterialPersistenceMngr = GetPersistenceManager();
		if (ensure(MaterialPersistenceMngr))
		{
			usageMap.emplace(texKey, MaterialPersistenceMngr->GetTextureUsage(texKey));
		}
		BE_ASSERT(AdvViz::SDK::FindTextureUsage(usageMap, texKey).flags_ != 0);

		ITwin::ResolveITwinTextures(texturesToResolve, usageMap, GltfMatHelper, texturePath.parent_path());
	}
}


void FITwinIModelMaterialHandler::ActivateMLMaterialPrediction(bool bActivate)
{
	const bool bUseMatPrediction_Old = VisualizeMaterialMLPrediction();
	bActivateMLMaterialPrediction = bActivate;

	// Persistence
	if (bUseMatPrediction_Old != VisualizeMaterialMLPrediction())
	{
		SaveMLPredictionState();
	}
}

void FITwinIModelMaterialHandler::ValidateMLPrediction()
{
	// Called when the user clicks the "Apply" button to validate the prediction.
	SaveMLPredictionState();
}

void FITwinIModelMaterialHandler::SaveMLPredictionState()
{
	// For Carrot EAP2, just use a predefined material slot (in the future, we may save a true material
	// mapping, with more complex rules...)
	AdvViz::SDK::ITwinMaterial MLSwitcherMat;
	MLSwitcherMat.kind = VisualizeMaterialMLPrediction() ? AdvViz::SDK::EMaterialKind::Glass
		: AdvViz::SDK::EMaterialKind::PBR;

	BeUtils::WLock Lock(GltfMatHelper->GetMutex());
	GltfMatHelper->CreateITwinMaterialSlot(ITwin::NOT_MATERIAL.value(), "", Lock);
	GltfMatHelper->SetMaterialFullDefinition(ITwin::NOT_MATERIAL.value(), MLSwitcherMat, Lock);
}

void FITwinIModelMaterialHandler::LoadMLPredictionState(bool& bActivateML, BeUtils::WLock const& Lock)
{
	// See if we have to activate material prediction (see corresponding code in #SaveMLPredictionState.
	// Note that this is a temporary solution to avoid having to modify the Decoration Server close to a
	// release).
	bActivateML = false;

	if (!ensure(GltfMatHelper->HasPersistenceInfo()))
		return;

	// Only create the special slot if it exists in loaded decoration.
	auto const MLSwitcherMatInfo = GltfMatHelper->CreateITwinMaterialSlot(ITwin::NOT_MATERIAL.value(),
		"", Lock, /*bOnlyIfCustomDefinitionExists*/true);
	auto const* MLSwitcherMat = MLSwitcherMatInfo.second;
	bActivateML = (MLSwitcherMat
		&& MLSwitcherMat->kind == AdvViz::SDK::EMaterialKind::Glass);
}

void FITwinIModelMaterialHandler::SetMaterialMLPredictionStatus(EITwinMaterialPredictionStatus InStatus)
{
	MLMaterialPredictionStatus = InStatus;
}

void FITwinIModelMaterialHandler::SetMaterialMLPredictionObserver(IITwinWebServicesObserver* observer)
{
	MLPredictionMaterialObserver = observer;
}

void FITwinIModelMaterialHandler::UpdateModelFromMatMLPrediction(bool bSuccess,
	AdvViz::SDK::ITwinMaterialPrediction const& Prediction, std::string const& error,
	AITwinIModel& IModel)
{
	if (!ensure(ITwin::IsMLMaterialPredictionEnabled()))
		return;

	BeUtils::WLock Lock(GltfMatHelper->GetMutex());

	if (!bSuccess || Prediction.data.empty())
	{
		IModel.SetMaterialMLPredictionStatus(EITwinMaterialPredictionStatus::Failed);
		if (MLPredictionMaterialObserver)
		{
			MLPredictionMaterialObserver->OnMatMLPredictionRetrieved(false, {}, error);
		}
		return;
	}

	if (IModel.VisualizeMaterialMLPrediction())
	{
		// Already done by another thread.
		return;
	}
	IModel.SetMaterialMLPredictionStatus(EITwinMaterialPredictionStatus::Complete);

	const std::string IModelId = TCHAR_TO_UTF8(*IModel.IModelId);

	// Deduce a new tuning from material prediction.
	// For the initial version, this will replace the materials retrieved previously from the decoration
	// service (to be defined: should we allow the user to revert to the previous version afterwards?)
	auto& MLPredMaterials = MLPredictionMaterials;
	MLPredMaterials = {};
	MLPredMaterials.Reserve(Prediction.data.size());

	MaterialMLPredictions.clear();
	MaterialMLPredictions.reserve(Prediction.data.size());

	std::unordered_map<uint64_t, std::string> MatIDToName;

	// Reload/Save material customizations from/to a file in order to create a collection of materials for
	// the predefined material categories (Wood, Steel, Aluminum etc.)
	auto const& MatIOMngr = GetPersistenceManager();
	if (MatIOMngr)
	{
		// This path will be independent from the current iModel, so that it is easier to locate
		std::filesystem::path MaterialDirectory =
			TCHAR_TO_UTF8(*QueriesCache::GetCacheFolder(
				QueriesCache::ESubtype::MaterialMLPrediction,
				EITwinEnvironment::Prod, {}, {}, {}));
		if (MaterialDirectory.has_parent_path())
		{
			// Step back because the custom mapping should not depend on the server environment.
			MaterialDirectory = MaterialDirectory.parent_path();
		}

		MatIOMngr->SetLocalMaterialDirectory(MaterialDirectory);

		// Try to load local collection, if any.
		if (MatIOMngr->LoadMaterialCollection(MaterialDirectory / "materials.json",
			IModelId, MatIDToName) > 0)
		{
			// Resolve textures, if any.
			AdvViz::SDK::PerIModelTextureSet const& perModelTextures =
				MatIOMngr->GetDecorationTexturesByIModel();
			auto itIModelTex = perModelTextures.find(IModelId);
			const bool bHasLoadedTextures =
				itIModelTex != perModelTextures.end() && !itIModelTex->second.empty();
			if (bHasLoadedTextures)
			{
				AdvViz::SDK::PerIModelTextureSet IModelTextures;
				IModelTextures.emplace(IModelId, itIModelTex->second);

				std::map<std::string, std::shared_ptr<BeUtils::GltfMaterialHelper>> IModelIdToMatHelper;
				IModelIdToMatHelper.emplace(IModelId, GltfMatHelper);

				ITwin::ResolveDecorationTextures(*MatIOMngr,
					IModelTextures, MatIOMngr->GetTextureUsageMap(),
					IModelIdToMatHelper, false, &Lock);
			}
		}
	}

	// Hard-coded mapping for now...
	struct MLMaterialMappingInfo
	{
		FString MaterialName;
		FString AssetPath;
	};
	static const std::vector<MLMaterialMappingInfo> MLMaterialMapping = {
		{ TEXT("Aluminum"),			TEXT("Metal/Aluminum") },
		{ TEXT("Asphalt"),			TEXT("Road_Pavers/Asphalt__Grey_") },
		{ TEXT("Ceramic tiles"),	TEXT("Marble_Granite/Marble_grayish_pink") },
		{ TEXT("Concrete"),			TEXT("Concrete/Concrete_gray") },
		{ TEXT("Concrete with rebar"), TEXT("Concrete/Concrete_new") },
		{ TEXT("Glass"),			TEXT("Glass/Glass_-_1") },
		{ TEXT("Metal"),			TEXT("Metal/Cast_metal") },
		{ TEXT("Plastic"),			TEXT("Plastic/Blue_reflective_plastic") },
		{ TEXT("Steel"),			TEXT("Metal/Stainless_steel") },
		{ TEXT("Wood"),				TEXT("Wood/Wood11") },
	};

	// Get the maximum material ID in the iModel.
	uint64 MaxMaterialID = 0;
	for (auto const& [MatId, _] : ITwinMaterials)
	{
		MaxMaterialID = std::max(MaxMaterialID, MatId);
	}
	const uint64 FirstMLMaterialID = MaxMaterialID + 1;

	// We will use local material IDs (but try to keep the same ID for a given material).
	std::unordered_map<std::string, uint64_t> NameToMatID;
	uint64 NextMaterialID = FirstMLMaterialID + MLMaterialMapping.size();
	bool hasFoundNewNames = false;
	for (auto const& [matId, name] : MatIDToName)
	{
		NameToMatID[name] = matId;
		if (NextMaterialID <= matId)
		{
			NextMaterialID = matId + 1;
		}
	}

	// First load the materials corresponding to the prediction (usually, they are loaded from the material
	// library, but if the user makes some modifications afterwards, they are stored in the decoration
	// service for that particular iModel.
	for (auto const& MatEntry : Prediction.data)
	{
		const FString MatName = UTF8_TO_TCHAR(MatEntry.material.c_str());
		// Assign a unique ID for each material of the classification, trying to reuse predefined ones loaded
		// from default collections.
		uint64_t MatID(0);
		auto const itID = NameToMatID.find(MatEntry.material);
		if (itID != NameToMatID.end())
		{
			// This material was overridden from local configuration file.
			MatID = itID->second;
		}
		else
		{
			// Try to find a correspondence in the Material Library.
			const auto mappingIt = std::find_if(
				MLMaterialMapping.begin(),
				MLMaterialMapping.end(),
				[&MatName](const MLMaterialMappingInfo& Candidate) {
				return Candidate.MaterialName == MatName;
			});
			if (mappingIt != MLMaterialMapping.end())
			{
				MatID = FirstMLMaterialID + uint64(mappingIt - MLMaterialMapping.begin());
			}
			else
			{
				// Totally unknown material...
				MatID = NextMaterialID++;
				hasFoundNewNames = true;
			}
			MatIDToName.emplace(MatID, MatEntry.material);

			// Load material definition from the library if we have not reloaded it from the decoration
			// service.
			if (mappingIt != MLMaterialMapping.end()
				&& !MatIOMngr->HasMaterialDefinition(IModelId, MatID))
			{
				AdvViz::SDK::ITwinMaterial NewMaterial;
				[[maybe_unused]] bool const bLoadOK = LoadMaterialWithoutRetuning(NewMaterial, MatID,
					FITwinMaterialLibrary::GetBeLibraryPathForLoading(mappingIt->AssetPath),
					IModel.IModelId, Lock);
				ensureMsgf(bLoadOK, TEXT("Could not load material from %s"), *mappingIt->AssetPath);
			}
		}

		FITwinCustomMaterial& CustomMat = MLPredMaterials.FindOrAdd(MatID);
		CustomMat.Name = MatName;

		auto& MatPredictionEntry = MaterialMLPredictions.emplace_back();
		MatPredictionEntry.Elements = MatEntry.elements;
		MatPredictionEntry.MatID = MatID;
	}

	// Then create the corresponding entries in the material helper (important for edition), and enable
	// material tuning if we do have a custom definition.
	{
//		BeUtils::WLock Lock(GltfMatHelper->GetMutex());

		BeUtils::ITwinToGltfTextureConverter TexConverter(GltfMatHelper);

		for (auto& [MatID, CustomMat] : MLPredMaterials)
		{
			// Also create the corresponding entries in the material helper (important for edition), and enable
			// material tuning if we do have a custom definition.
			auto const MatInfo = GltfMatHelper->CreateITwinMaterialSlot(MatID,
																		TCHAR_TO_UTF8(*CustomMat.Name),
																		Lock);
			if (MatInfo.second && AdvViz::SDK::HasCustomSettings(*MatInfo.second))
			{
				CustomMat.bAdvancedConversion = true;

				// Perform texture conversions at once.
				TexConverter.ConvertTexturesToGltf(MatID, Lock);
			}
		}
	}

	if (MatIOMngr && hasFoundNewNames)
	{
		MatIOMngr->AppendMaterialCollectionNames(MatIDToName);
	}
}


void FITwinIModelMaterialHandler::OnMatMLPredictionRetrieved(bool bSuccess,
	AdvViz::SDK::ITwinMaterialPrediction const& Prediction, std::string const& error,
	AITwinIModel& IModel)
{
	// Update the material mapping based on material ML predictions. Only one thread should do it!
	UpdateModelFromMatMLPrediction(bSuccess, Prediction, error, IModel);

	if (MLPredictionMaterialObserver)
	{
		MLPredictionMaterialObserver->OnMatMLPredictionRetrieved(bSuccess, Prediction);
	}

	// Re-tune the glTF model accordingly.
	SplitGltfModelForCustomMaterials();
}

void FITwinIModelMaterialHandler::OnMatMLPredictionProgress(float fProgressRatio, AITwinIModel const& IModel)
{
	// Just log progression
	FString strProgress = TEXT("computing material predictions for ") + IModel.GetActorNameOrLabel();
	if (fProgressRatio < 1.f)
		strProgress += FString::Printf(TEXT("... (%.0f%%)"), 100.f * fProgressRatio);
	else
		strProgress += TEXT(" -> done");
	BE_LOGI("ITwinAPI", "[ML_MaterialPrediction] " << TCHAR_TO_UTF8(*strProgress));

	if (MLPredictionMaterialObserver)
	{
		MLPredictionMaterialObserver->OnMatMLPredictionProgress(fProgressRatio);
	}
}

void FITwinIModelMaterialHandler::FillMaterialInfoFromTuner(AITwinIModel const* IModel)
{
	if (!GltfTuner) // debugging only...
		return;
	// In case we need to download / customize textures, setup a folder depending on current iModel
	InitTextureDirectory(IModel);

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
		if (!CustomMat.Name.IsEmpty())
		{
			// Beware we can call FillMaterialInfoFromTuner several times for a same model. We should not
			// modify a name already computed, or we will end with duplicated names, as we deduce the material
			// index from the size of the map below... (see bug #1619696)
			continue;
		}
		CustomMat.Name = UTF8_TO_TCHAR(MatInfo.name.c_str());
		// Material names usually end with a suffix in the form of ": <IMODEL_NAME>"
		// => discard this part
		int32 LastColon = UE::String::FindLast(CustomMat.Name, TEXT(":"));
		if (LastColon > 0)
		{
			CustomMat.Name.RemoveAt(LastColon, CustomMat.Name.Len() - LastColon);
		}
		// Sometimes (often in real projects?) the material name is just a random set of letters
		// => try to detect this case and display a default name then.
		const bool bBuildDefaultName = CustomMat.Name.IsEmpty()
			|| (CustomMat.Name.Len() >= 16 && !CustomMat.Name.Contains(TEXT(" ")))
			|| BeUtils::ContainsUUIDLikeSubstring(TCHAR_TO_UTF8(*CustomMat.Name));
		if (bBuildDefaultName)
		{
			CustomMat.Name = BuildMaterialNameFromInteger(ITwinMaterials.Num());
		}
	}
}


void FITwinIModelMaterialHandler::DetectCustomizedMaterials(AITwinIModel* OwnerIModel /*= nullptr*/)
{
	// Detect user customizations (they are stored in the decoration service).
	int NumCustomMaterials = 0;
	BeUtils::WLock Lock(GltfMatHelper->GetMutex());

	// Initialize persistence at low level, if it has not yet been done (eg. when creating the iModel
	// manually in Editor, and clicking LoadDecoration...).
	auto const& MaterialPersistenceMngr = GetPersistenceManager();
	if (MaterialPersistenceMngr
		&& !GltfMatHelper->HasPersistenceInfo()
		&& ensure(OwnerIModel && !OwnerIModel->IModelId.IsEmpty()))
	{
		GltfMatHelper->SetPersistenceInfo(TCHAR_TO_ANSI(*OwnerIModel->IModelId), MaterialPersistenceMngr);
	}

	// Detect the activation of ML material prediction.
	bool bActivateMLMatPrediction = false;
	if (MaterialPersistenceMngr)
	{
		LoadMLPredictionState(bActivateMLMatPrediction, Lock);
	}

	// Use a cleanup guard in case of early exit.
	Be::CleanUpGuard ToggleMLGuard([&Lock, OwnerIModel, bActivateMLMatPrediction]
	{
		if (bActivateMLMatPrediction && OwnerIModel)
		{
			OwnerIModel->ToggleMLMaterialPrediction(true);
			// If the material prediction can be reloaded from cache (which will be the case if we reload
			// a scene on the same machine as earlier), directly set the status to validated, so that the
			// Revert/Apply buttons do not show up in the Object Material panel.
			if (OwnerIModel->VisualizeMaterialMLPrediction())
			{
				OwnerIModel->SetMaterialMLPredictionStatus(EITwinMaterialPredictionStatus::Validated);
			}
		}
	});

	// See if some material definitions can be loaded from the decoration service.
	size_t LoadedSettings = GltfMatHelper->LoadMaterialCustomizations(Lock);
	if (LoadedSettings == 0)
	{
		return;
	}

	// Helper for texture conversions.
	BeUtils::ITwinToGltfTextureConverter TexConverter(GltfMatHelper);

	auto& CustomMaterials = ITwinMaterials;
	for (auto& [MatID, CustomMat] : CustomMaterials)
	{
		if (GltfMatHelper->HasCustomDefinition(MatID, Lock))
		{
			// If the material uses custom settings, activate advanced conversion so that the tuning
			// can handle it.
			if (!CustomMat.bAdvancedConversion)
			{
				CustomMat.bAdvancedConversion = true;
				NumCustomMaterials++;
			}

			// Perform some conversion from iTwin textures to glTF format. This is now done before any tuning
			// can occur, to avoid w-locking the material helper during the tuning, which slows down the
			// rendering.
			TexConverter.ConvertTexturesToGltf(MatID, Lock);
		}
	}
	Lock.unlock(); // we are done editing GltfMatHelper

	// Toggle material prediction at the end if needed. This may trigger a custom glTF tuning.
	ToggleMLGuard.cleanup();

	const bool bHasTriggeredTuning = (bActivateMLMatPrediction && OwnerIModel
		&& OwnerIModel->VisualizeMaterialMLPrediction());

	// Request a glTF tuning if needed (and if it has not just been done for the visualization of the
	// material prediction).
	if (NumCustomMaterials > 0 && !bHasTriggeredTuning)
	{
		SplitGltfModelForCustomMaterials();
	}
}

void FITwinIModelMaterialHandler::ReloadCustomizedMaterials()
{
	{
		BeUtils::WLock Lock(GltfMatHelper->GetMutex());
		size_t LoadedSettings = GltfMatHelper->LoadMaterialCustomizations(Lock, true);
		for (auto& [MatID, CustomMat] : ITwinMaterials)
		{
			CustomMat.bAdvancedConversion = GltfMatHelper->HasCustomDefinition(MatID, Lock);
		}
	}
	SplitGltfModelForCustomMaterials();
}

void FITwinIModelMaterialHandler::SplitGltfModelForCustomMaterials(bool bForceRetune /*= false*/)
{
	if (!GltfTuner) // debugging only...
		return;
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

		if (VisualizeMaterialMLPrediction())
		{
			rules.materialGroups_.reserve(MaterialMLPredictions.size());
			for (auto const& MatEntry : MaterialMLPredictions)
			{
				rules.materialGroups_.emplace_back(BeUtils::GltfTuner::Rules::MaterialGroup{
						.elements_ = MatEntry.Elements,
						.material_ = 0,// does not matter much (will be overridden), but needs to be >= 0
						.itwinMaterialID_ = MatEntry.MatID
					});
			}
		}

		rules.itwinMatIDsToSplit_.swap(NewMatIDsToSplit);
		GltfTuner->SetMaterialRules(std::move(rules));
		//Retune(); <== version increment (and thus retuning) now automatic in SetMaterialRules/SetAnim4DRules
	}
}

void FITwinIModelMaterialHandler::Retune()
{
	GltfTuner->trigger();
}

double FITwinIModelMaterialHandler::GetMaterialChannelIntensity(uint64_t MaterialId, AdvViz::SDK::EChannelType Channel) const
{
	return GltfMatHelper->GetChannelIntensity(MaterialId, Channel);
}

namespace
{
	using EITwinChannelType = AdvViz::SDK::EChannelType;

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
		AdvViz::SDK::EChannelType const Channel;
		ParamType const NewValue;

		MaterialParamHelperBase(BeUtils::GltfMaterialHelper& GltfHelper,
			AdvViz::SDK::EChannelType InChannel,
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
			AdvViz::SDK::EChannelType InChannel,
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

		bool DetectChangeOfTranslucency(const double CurrentIntensity) const
		{
			// Test if the modification we are doing will imply a change of the translucency mode.
			double const CurrentTransparency = (Channel == AdvViz::SDK::EChannelType::Transparency)
				? CurrentIntensity
				: (1. - CurrentIntensity);
			double const NewTransparency = (Channel == AdvViz::SDK::EChannelType::Transparency)
				? this->NewValue
				: (1. - this->NewValue);

			bool const bCurrentTranslucent = (std::fabs(CurrentTransparency) > 1e-5);
			bool const bNewTranslucent = (std::fabs(NewTransparency) > 1e-5);
			return bCurrentTranslucent != bNewTranslucent;
		}

		void SetNewValue(uint64_t MaterialId, bool& bModifiedValue) const
		{
			std::optional<double> CurValueOpt;
			if (this->Channel == AdvViz::SDK::EChannelType::Alpha)
			{
				CurValueOpt = GetCurrentValue(MaterialId);
			}

			GltfMatHelper.SetChannelIntensity(MaterialId, this->Channel, this->NewValue, bModifiedValue);

			if (bModifiedValue
				&& CurValueOpt
				&& DetectChangeOfTranslucency(*CurValueOpt))
			{
				GltfMatHelper.UpdateCurrentAlphaMode(MaterialId);
			}
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
			return DetectChangeOfTranslucency(CurrentIntensity);
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


	class MaterialMapParamHelper : public MaterialParamHelperBase<AdvViz::SDK::ITwinChannelMap>
	{
		using Super = MaterialParamHelperBase<AdvViz::SDK::ITwinChannelMap>;

	public:
		using ParamType = AdvViz::SDK::ITwinChannelMap;

		static EITwinMaterialParamType EditedType() { return EITwinMaterialParamType::Map; }

		MaterialMapParamHelper(UITwinMaterialDefaultTexturesHolder const& InDefaultTexturesHolder,
			std::shared_ptr<BeUtils::GltfMaterialHelper> const& GltfHelperPtr,
			AdvViz::SDK::EChannelType InChannel,
			AdvViz::SDK::ITwinChannelMap const& NewMap)
			: Super(*GltfHelperPtr, InChannel, NewMap)
			, DefaultTexturesHolder(InDefaultTexturesHolder)
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
				BeUtils::ITwinToGltfTextureConverter MatTuner(MatHelperPtr);
				auto const MergedTexAccess = MatTuner.ConvertChannelTextureToGltf(MaterialId,
					this->Channel, this->bNeedTranslucentMat);
				if (MergedTexAccess.IsValid())
				{
					std::filesystem::path const glTFTexturePath = MergedTexAccess.filePath;
					if (!glTFTexturePath.empty())
					{
						NewTexPath = glTFTexturePath.generic_string();
						bHasBuiltMergedTexture = true;
						return true;
					}
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
			if (NewTexPath.empty() || NewTexPath == AdvViz::SDK::NONE_TEXTURE)
			{
				// Beware setting a null texture in a dynamic material through SetTextureParameterValueByInfo
				// would not nullify the texture, so instead, we use the default texture (depending on the
				// channel) to discard the effect.
				NewTexture = DefaultTexturesHolder.GetDefaultTextureForChannel(this->Channel);
			}
			else if (this->NewValue.eSource != AdvViz::SDK::ETextureSource::LocalDisk
				&& !bHasBuiltMergedTexture)
			{
				// There may be no physical file yet for such textures => use Cesium asset accessor
				// mechanism to deal with those textures.
				NewTexture = ITwin::ResolveAsUnrealTexture(GltfMatHelper, NewTexPath, this->NewValue.eSource);
			}
			else
			{
				NewTexture = FImageUtils::ImportFileAsTexture2D(UTF8_TO_TCHAR(NewTexPath.c_str()));
			}
			SceneMapping.SetITwinMaterialChannelTexture(MaterialId, this->Channel, NewTexture);
		}

	protected:
		UITwinMaterialDefaultTexturesHolder const& DefaultTexturesHolder;
		std::shared_ptr<BeUtils::GltfMaterialHelper> const& MatHelperPtr;
		mutable bool bNeedTranslucentMat = false;
		mutable bool bHasBuiltMergedTexture = false;
		mutable std::string NewTexPath;
	};

	class MaterialIntensityMapHelper : public MaterialMapParamHelper
	{
		using Super = MaterialMapParamHelper;

	public:
		MaterialIntensityMapHelper(UITwinMaterialDefaultTexturesHolder const& InDefaultTexturesHolder,
			std::shared_ptr<BeUtils::GltfMaterialHelper> const& GltfHelperPtr,
			AdvViz::SDK::EChannelType InChannel,
			AdvViz::SDK::ITwinChannelMap const& NewMap)
			: Super(InDefaultTexturesHolder, GltfHelperPtr, InChannel, NewMap)
		{}

		ParamType GetCurrentValue(uint64_t MaterialId) const
		{
			return GltfMatHelper.GetChannelIntensityMap(MaterialId, this->Channel);
		}

		void SetNewValue(uint64_t MaterialId, bool& bModifiedValue) const
		{
			// Before changing the alpha map, retrieve the current alpha mode, if any.
			if (this->Channel == AdvViz::SDK::EChannelType::Alpha)
			{
				BeUtils::WLock Lock(GltfMatHelper.GetMutex());
				GltfMatHelper.StoreInitialAlphaModeIfNeeded(MaterialId, CurrentAlphaMode, Lock);
			}

			GltfMatHelper.SetChannelIntensityMap(MaterialId, this->Channel, this->NewValue, bModifiedValue);

			BuildMergedTexture(MaterialId);

			if (bModifiedValue
				&& this->Channel == AdvViz::SDK::EChannelType::Alpha)
			{
				GltfMatHelper.UpdateCurrentAlphaMode(MaterialId, this->bNeedTranslucentMat);
			}
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

	class MaterialColorHelper : public MaterialParamHelperBase<AdvViz::SDK::ITwinColor>
	{
		using ITwinColor = AdvViz::SDK::ITwinColor;
		using Super = MaterialParamHelperBase<ITwinColor>;

	public:
		using ParamType = ITwinColor;

		static EITwinMaterialParamType EditedType() { return EITwinMaterialParamType::Color; }

		MaterialColorHelper(BeUtils::GltfMaterialHelper& GltfHelper,
			AdvViz::SDK::EChannelType InChannel,
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
		MaterialColorMapHelper(UITwinMaterialDefaultTexturesHolder const& InDefaultTexturesHolder,
			std::shared_ptr<BeUtils::GltfMaterialHelper> const& GltfHelperPtr,
			AdvViz::SDK::EChannelType InChannel,
			AdvViz::SDK::ITwinChannelMap const& NewMap)
			: Super(InDefaultTexturesHolder, GltfHelperPtr, InChannel, NewMap)
		{}

		ParamType GetCurrentValue(uint64_t MaterialId) const
		{
			return GltfMatHelper.GetChannelColorMap(MaterialId, this->Channel);
		}

		void SetNewValue(uint64_t MaterialId, bool& bModifiedValue) const
		{
			GltfMatHelper.SetChannelColorMap(MaterialId, this->Channel, this->NewValue, bModifiedValue);

			BuildMergedTexture(MaterialId);

			if (bModifiedValue
				&& this->Channel == AdvViz::SDK::EChannelType::Color)
			{
				GltfMatHelper.UpdateCurrentAlphaMode(MaterialId, this->bNeedTranslucentMat);
			}
		}

		bool NeedGltfTuning(ParamType const& CurrentMap) const
		{
			// If we activate normal mapping now whereas it was off previously, we will have to trigger
			// a new tuning, because this has an impact on the primitives themselves (which need tangents
			// in such case - see code in #loadPrimitive (search 'needsTangents' and 'hasTangents').
			if (this->Channel == AdvViz::SDK::EChannelType::Normal
				&& !CurrentMap.HasTexture()
				&& this->NewValue.HasTexture())
			{
				return true;
			}
			return false;
		}
	};


	// For now UV transformation is global: applies to all textures in the material
	class MaterialUVTransformHelper : public MaterialParamHelperBase<AdvViz::SDK::ITwinUVTransform>
	{
		using Super = MaterialParamHelperBase<AdvViz::SDK::ITwinUVTransform>;

	public:
		using ParamType = AdvViz::SDK::ITwinUVTransform;

		static EITwinMaterialParamType EditedType() { return EITwinMaterialParamType::UVTransform; }

		MaterialUVTransformHelper(BeUtils::GltfMaterialHelper& GltfHelper,
			AdvViz::SDK::ITwinUVTransform const& NewUVTsf)
			: Super(GltfHelper, AdvViz::SDK::EChannelType::ENUM_END, NewUVTsf)
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


	class MaterialKindHelper : public MaterialParamHelperBase<AdvViz::SDK::EMaterialKind>
	{
		using Super = MaterialParamHelperBase<AdvViz::SDK::EMaterialKind>;

	public:
		using ParamType = AdvViz::SDK::EMaterialKind;

		static EITwinMaterialParamType EditedType() { return EITwinMaterialParamType::Kind; }

		MaterialKindHelper(BeUtils::GltfMaterialHelper& GltfHelper,
			AdvViz::SDK::EMaterialKind const& NewKind)
			: Super(GltfHelper, AdvViz::SDK::EChannelType::ENUM_END, NewKind)
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
				&& this->NewValue == AdvViz::SDK::EMaterialKind::Glass
				&& std::fabs(GltfMatHelper.GetChannelIntensity(MaterialId, AdvViz::SDK::EChannelType::Opacity) - 1.0) < 1e-4)
			{
				bool bSubValueModified(false);
				GltfMatHelper.SetChannelIntensity(MaterialId, AdvViz::SDK::EChannelType::Opacity, 0.5, bSubValueModified);
				// Also set a default metallic factor
				GltfMatHelper.SetChannelIntensity(MaterialId, AdvViz::SDK::EChannelType::Metallic, 0.5, bSubValueModified);
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
		bool bHasNonDefaultValue = false;
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
void FITwinIModelMaterialHandler::TSetMaterialChannelParam(MaterialParamHelper const& Helper, uint64_t MaterialId,
	FITwinSceneMapping& SceneMapping)
{
	using ParameterType = typename MaterialParamHelper::ParamType;
	FITwinCustomMaterial* CustomMat = GetMutableCustomMaterials().Find(MaterialId);
	if (!ensureMsgf(CustomMat, TEXT("unknown material ID")))
	{
		return;
	}

	// Depending on the channel and its previous value, we may have to trigger a new glTF tuning.
	AdvViz::SDK::EChannelType const Channel(Helper.Channel);
	bool const bTestNeedRetuning = (Helper.EditedType() == EITwinMaterialParamType::Map
		|| Helper.EditedType() == EITwinMaterialParamType::Kind
		|| Channel == AdvViz::SDK::EChannelType::Transparency
		|| Channel == AdvViz::SDK::EChannelType::Alpha);
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
	const bool bFirstTimeMaterialIsCustomized = !CustomMat->bAdvancedConversion;
	CustomMat->bAdvancedConversion = true;
	bool bNeedGltfTuning = bFirstTimeMaterialIsCustomized;

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
		if (bFirstTimeMaterialIsCustomized)
		{
			// If the original MES material uses textures (color textures, for now, as the MES does not
			// export other channels yet), and the user edits another parameter, we must resolve the iTwin
			// texture now, as it will be used in the tuned material as well.
			bool bIsReplacingColorTex = Helper.EditedType() == EITwinMaterialParamType::Map
				&& Channel == AdvViz::SDK::EChannelType::Color;
			if (!bIsReplacingColorTex)
			{
				std::unordered_map<AdvViz::SDK::TextureKey, std::string> itwinTextures;
				AdvViz::SDK::TextureUsageMap usageMap;
				BeUtils::RLock Lock(GltfMatHelper->GetMutex());
				GltfMatHelper->AppendITwinTexturesToResolveFromMaterial(itwinTextures, usageMap, MaterialId, Lock);
				if (!itwinTextures.empty())
				{
					auto const TexDir = GltfMatHelper->GetTextureDirectory(Lock);
					Lock.unlock();
					ITwin::ResolveITwinTextures(itwinTextures, usageMap, GltfMatHelper, TexDir);
				}
			}
		}
		SplitGltfModelForCustomMaterials(true);
	}
	else
	{
		// No need to rebuild the full tileset. Instead, change the corresponding parameter in the
		// Unreal material instance, using the mapping.
		Helper.ApplyNewValueToScene(MaterialId, SceneMapping);
	}
}

void FITwinIModelMaterialHandler::SetMaterialChannelIntensity(uint64_t MaterialId, AdvViz::SDK::EChannelType Channel, double Intensity,
	FITwinSceneMapping& SceneMapping)
{
	MaterialIntensityHelper Helper(*GltfMatHelper, Channel, Intensity);
	TSetMaterialChannelParam(Helper, MaterialId, SceneMapping);
}


FLinearColor FITwinIModelMaterialHandler::GetMaterialChannelColor(uint64_t MaterialId, AdvViz::SDK::EChannelType Channel) const
{
	auto const Color = GltfMatHelper->GetChannelColor(MaterialId, Channel);
	return { (float)Color[0], (float)Color[1], (float)Color[2], (float)Color[3] };
}

void FITwinIModelMaterialHandler::SetMaterialChannelColor(uint64_t MaterialId, AdvViz::SDK::EChannelType Channel,
	FLinearColor const& Color, FITwinSceneMapping& SceneMapping)
{
	MaterialColorHelper Helper(*GltfMatHelper, Channel,
		AdvViz::SDK::ITwinColor{ Color.R, Color.G, Color.B, Color.A });
	TSetMaterialChannelParam(Helper, MaterialId, SceneMapping);
}


FString FITwinIModelMaterialHandler::GetMaterialChannelTextureID(uint64_t MaterialId, AdvViz::SDK::EChannelType Channel,
	AdvViz::SDK::ETextureSource& OutSource) const
{
	auto const ChanMap = GltfMatHelper->GetChannelMap(MaterialId, Channel);
	OutSource = ChanMap.eSource;
	return UTF8_TO_TCHAR(ChanMap.texture.c_str());
}

void FITwinIModelMaterialHandler::SetMaterialChannelTextureID(uint64_t MaterialId, AdvViz::SDK::EChannelType Channel,
	FString const& TextureId, AdvViz::SDK::ETextureSource eSource,
	FITwinSceneMapping& SceneMapping,
	UITwinMaterialDefaultTexturesHolder const& DefaultTexturesHolder)
{
	AdvViz::SDK::ITwinChannelMap NewMap;
	NewMap.texture = TCHAR_TO_UTF8(*TextureId);
	NewMap.eSource = eSource;

	// Distinguish color from intensity textures.
	if (Channel == AdvViz::SDK::EChannelType::Color
		|| Channel == AdvViz::SDK::EChannelType::Normal)
	{
		MaterialColorMapHelper Helper(DefaultTexturesHolder, GltfMatHelper, Channel, NewMap);
		TSetMaterialChannelParam(Helper, MaterialId, SceneMapping);
	}
	else
	{
		// For other channels, the map defines an intensity
		MaterialIntensityMapHelper Helper(DefaultTexturesHolder, GltfMatHelper, Channel, NewMap);
		TSetMaterialChannelParam(Helper, MaterialId, SceneMapping);
	}
}

AdvViz::SDK::ITwinUVTransform FITwinIModelMaterialHandler::GetMaterialUVTransform(uint64_t MaterialId) const
{
	return GltfMatHelper->GetUVTransform(MaterialId);
}

void FITwinIModelMaterialHandler::SetMaterialUVTransform(uint64_t MaterialId, AdvViz::SDK::ITwinUVTransform const& UVTransform,
	FITwinSceneMapping& SceneMapping)
{
	MaterialUVTransformHelper Helper(*GltfMatHelper, UVTransform);
	TSetMaterialChannelParam(Helper, MaterialId, SceneMapping);
}

AdvViz::SDK::EMaterialKind FITwinIModelMaterialHandler::GetMaterialKind(uint64_t MaterialId) const
{
	return GltfMatHelper->GetMaterialKind(MaterialId);
}

void FITwinIModelMaterialHandler::SetMaterialKind(uint64_t MaterialId, AdvViz::SDK::EMaterialKind NewKind,
	FITwinSceneMapping& SceneMapping)
{
	MaterialKindHelper Helper(*GltfMatHelper, NewKind);
	TSetMaterialChannelParam(Helper, MaterialId, SceneMapping);
}

bool FITwinIModelMaterialHandler::GetMaterialCustomRequirements(uint64_t MaterialId, AdvViz::SDK::EMaterialKind& OutMaterialKind,
	bool& bOutRequiresTranslucency) const
{
	return GltfMatHelper->GetCustomRequirements(MaterialId, OutMaterialKind, bOutRequiresTranslucency);
}

bool FITwinIModelMaterialHandler::LoadMaterialWithoutRetuning(
	AdvViz::SDK::ITwinMaterial& OutNewMaterial,
	uint64_t MaterialId,
	FMaterialAssetInfo const& MaterialAssetInfo,
	FString const& IModelId,
	BeUtils::WLock const& Lock,
	bool bForceRefreshAllParameters /*= false*/)
{
	using namespace AdvViz::SDK;
	using EITwinChannelType = AdvViz::SDK::EChannelType;

	auto const& MatIOMngr = GetPersistenceManager();
	if (!ensureMsgf(MatIOMngr, TEXT("no material persistence manager")))
	{
		return false;
	}

	// List of all channels which can hold a texture:
	auto const ChansWithTex = {
		AdvViz::SDK::EChannelType::Color,
		AdvViz::SDK::EChannelType::Metallic,
		AdvViz::SDK::EChannelType::Normal,
		AdvViz::SDK::EChannelType::Roughness,
		AdvViz::SDK::EChannelType::Opacity,
		AdvViz::SDK::EChannelType::AmbientOcclusion,
	};

	ITwinMaterial& NewMaterial(OutNewMaterial);
	TextureKeySet NewTextures;
	TextureUsageMap NewTextureUsageMap;
	ETextureSource TexSource = ETextureSource::Library;

	bool bValidMaterial = false;
	std::visit([&](auto&& AssetInfo)
	{
		using T = std::decay_t<decltype(AssetInfo)>;
		if constexpr (std::is_same_v<T, FString>)
		{
			// This is the path to a material file.
			bValidMaterial = FITwinMaterialLibrary::LoadMaterialFromAssetPath(AssetInfo, NewMaterial,
				NewTextures, NewTextureUsageMap, TexSource, MatIOMngr);
		}
		else if constexpr (std::is_same_v<T, ITwin::FMaterialPtr>)
		{
			if (AssetInfo)
			{
				NewMaterial = *AssetInfo;
				TexSource = ETextureSource::LocalDisk;
				bValidMaterial = true;

				// Gather textures used by this material.
				for (auto eChan : ChansWithTex)
				{
					auto const MapOpt = NewMaterial.GetChannelMapOpt(eChan);
					if (MapOpt && MapOpt->HasTexture())
					{
						const TextureKey texKey = { MapOpt->texture, MapOpt->eSource };
						NewTextures.insert(texKey);
						NewTextureUsageMap[texKey].AddChannel(eChan);
					}
				}
			}
		}
		else static_assert(always_false_v<T>, "non-exhaustive visitor!");
	}, MaterialAssetInfo);

	if (!bValidMaterial)
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
	if (bForceRefreshAllParameters)
	{
		// Set NONE_TEXTURE in all unused slot, to enforce material update in UE (typically for the material
		// preview...)
		for (auto eChan : ChansWithTex)
		{
			if (!NewMaterial.GetChannelMapOpt(eChan))
			{
				NewMaterial.SetChannelMap(eChan,
					ITwinChannelMap{
						.texture = NONE_TEXTURE,
						.eSource = ETextureSource::Library
					});
			}
		}
	}

	// Resolve the textures, if any (they should all exist in the Material Library...)
	if (!NewTextures.empty())
	{
		// Use maps with just one ID here...
		std::string const imodelId = TCHAR_TO_UTF8(*IModelId);

		PerIModelTextureSet PerModelTextures;
		std::map<std::string, std::shared_ptr<BeUtils::GltfMaterialHelper>> imodelIdToMatHelper;
		PerModelTextures.emplace(imodelId, NewTextures);
		imodelIdToMatHelper.emplace(imodelId, GltfMatHelper);

		if (!ITwin::ResolveDecorationTextures(
			*MatIOMngr,
			PerModelTextures, NewTextureUsageMap,
			imodelIdToMatHelper,
			/*bResolveLocalDiskTextures =*/ TexSource == ETextureSource::LocalDisk,
			&Lock))
		{
			return false;
		}
	}

	GltfMatHelper->SetMaterialFullDefinition(MaterialId, NewMaterial, Lock);

	return true;
}

bool FITwinIModelMaterialHandler::LoadMaterialFromAssetFile(uint64_t MaterialId,
	FMaterialAssetInfo const& MaterialAssetInfo,
	FString const& IModelId,
	FITwinSceneMapping& SceneMapping,
	UITwinMaterialDefaultTexturesHolder const& DefaultTexturesHolder,
	bool bForceRefreshAllParameters /*= false*/,
	std::function<void(AdvViz::SDK::ITwinMaterial&)> const& CustomizeMaterialFunc /*= {}*/)
{
	using namespace AdvViz::SDK;
	using EITwinChannelType = AdvViz::SDK::EChannelType;

	FITwinCustomMaterial* CustomMat = GetMutableCustomMaterials().Find(MaterialId);
	if (!ensureMsgf(CustomMat, TEXT("unknown material ID")))
	{
		return false;
	}

	ITwinMaterial CurMaterial;
	if (!GltfMatHelper->GetMaterialFullDefinition(MaterialId, CurMaterial))
	{
		return false;
	}

	std::string CurrentAlphaMode;
	ITwinMaterial NewMaterial;

	{
		BeUtils::WLock Lock(GltfMatHelper->GetMutex());

		// Fetch current alpha mode before modifying the material, to detect a switch of translucency mode.
		GltfMatHelper->StoreInitialAlphaModeIfNeeded(MaterialId, CurrentAlphaMode, Lock);

		// Actually load the material.
		if (!LoadMaterialWithoutRetuning(NewMaterial, MaterialId, MaterialAssetInfo, IModelId,
										 Lock, bForceRefreshAllParameters))
		{
			return false;
		}
	}

	if (CustomizeMaterialFunc)
	{
		// Introduced to adjust the material previews...
		CustomizeMaterialFunc(NewMaterial);
	}

	if (CurMaterial == NewMaterial)
	{
		// Avoid useless glTF splitting or DB invalidation.
		return true;
	}

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
			.bHasNonDefaultValue = IntensOpt_New.has_value(),
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
					DefaultTexturesHolder, GltfMatHelper, Channel,
					MapOpt_New.value_or(ITwinChannelMap{}))
				);
			}
			else
			{
				MapHelpers.emplace_back(std::make_unique<MaterialIntensityMapHelper>(
					DefaultTexturesHolder, GltfMatHelper, Channel,
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
	if (bNeedGltfTuning && GltfTuner)
	{
		// The whole tileset will be reloaded with updated materials.
		SplitGltfModelForCustomMaterials(true);
	}
	else
	{
		// No need for re-tuning. Just apply each parameter to the existing Unreal material instances.
		for (size_t i(0); i < newIntensities.size(); ++i)
		{
			EITwinChannelType const Channel = static_cast<EITwinChannelType>(i);
			if (newIntensities[i].bHasChanged ||
				(bForceRefreshAllParameters && newIntensities[i].bHasNonDefaultValue))
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
		CustomMat->DisplayName = UTF8_TO_TCHAR(NewMaterial.displayName.c_str());
#if ITWIN_EDIT_MATERIAL_NAME_IN_MODEL()
		CustomMat->Name = CustomMat->DisplayName;
#endif
	}

	return true;
}

bool FITwinIModelMaterialHandler::LoadMaterialFromAssetFile(uint64_t MaterialId, FString const& AssetFilePath,
	AITwinIModel& IModel)
{
	return LoadMaterialFromAssetFile(MaterialId, AssetFilePath, IModel.IModelId,
		GetInternals(IModel).SceneMapping,
		IModel.GetDefaultTexturesHolder());
}

/*** Material persistence ***/

/*static*/
FITwinIModelMaterialHandler::MaterialPersistencePtr FITwinIModelMaterialHandler::GlobalPersistenceMngr;

/*static*/
void FITwinIModelMaterialHandler::SetGlobalPersistenceManager(MaterialPersistencePtr const& Mngr)
{
	GlobalPersistenceMngr = Mngr;
}

/*static*/
FITwinIModelMaterialHandler::MaterialPersistencePtr const& FITwinIModelMaterialHandler::GetGlobalPersistenceManager()
{
	return GlobalPersistenceMngr;
}

void FITwinIModelMaterialHandler::SetSpecificPersistenceManager(MaterialPersistencePtr const& Mngr)
{
	SpecificPersistenceMngr = Mngr;
}

FITwinIModelMaterialHandler::MaterialPersistencePtr const& FITwinIModelMaterialHandler::GetPersistenceManager() const
{
	if (SpecificPersistenceMngr)
		return SpecificPersistenceMngr;
	else
		return GetGlobalPersistenceManager();
}

void FITwinIModelMaterialHandler::InitForSingleMaterial(FString const& IModelId, uint64_t const MaterialId,
	std::shared_ptr<BeUtils::GltfMaterialHelper> const& SrcIModelMatHelper)
{
	// Do not call Initialize: here we do not want to use any glTF tuner nor iModel: we just want to edit
	// the material instance through the SceneMapping updates.
	// Just create one slot:
	GetMutableCustomMaterials().Emplace(MaterialId, { TEXT("DUMMY_MAT") });
	GltfMatHelper->SetPersistenceInfo(TCHAR_TO_ANSI(*IModelId), GetPersistenceManager());
	// Also create one entry in the glTF material helper (or else it will refuse to perform any edition
	// of the material).
	{
		BeUtils::WLock Lock(GltfMatHelper->GetMutex());
		GltfMatHelper->CreateITwinMaterialSlot(MaterialId, "DUMMY_MAT", Lock);

		if (SrcIModelMatHelper)
		{
			// By copying the texture data map, we fasten the generation of the preview computed for the UI,
			// as we will reuse both merged texture (metallic+roughness...) and textures downloaded in cache
			// (for iTwin source, coming from the MES).
			// During export, this is not as important since the textures were just duplicated in a new
			// directory.
			GltfMatHelper->CopyTextureDataFrom(*SrcIModelMatHelper, Lock);
		}
		else
		{
			// Make sure the material helper has a valid texture directory in case it needs to merge some
			// channels together (metallic+roughness, color+alpha...)
			GltfMatHelper->SetTextureDirectory(
				*BuildTextureDirectoryForIModel(nullptr, IModelId), Lock, true /*bCreateAtOnce*/);
		}
	}
}

namespace ITwin
{
	ITWINRUNTIME_API bool LoadMaterialOnGenericMesh(FMaterialAssetInfo const& MaterialAssetInfo,
		AStaticMeshActor& MeshActor,
		std::shared_ptr<BeUtils::GltfMaterialHelper> const& SrcIModelMatHelper /*= {}*/)
	{
		using namespace AdvViz::SDK;

		// Use default values for iModel and material ID (it has to be constant so that the mechanism of
		// incremental updates works correctly when generating a batch of previews).
		FString const IModelId = TEXT("PREVIEW_IMODEL_ID");
		uint64_t const MaterialId = 0;

		// Use a distinct material handler for each preview mesh.
		using MaterialHandlerPtr = std::shared_ptr<FITwinIModelMaterialHandler>;
		static std::unordered_map<UStaticMeshComponent*, MaterialHandlerPtr> PerMeshMaterialHandlers;

		MaterialHandlerPtr MaterialHandler;
		UStaticMeshComponent* MeshComponent = MeshActor.GetStaticMeshComponent();
		auto itMaterialHandler = PerMeshMaterialHandlers.find(MeshComponent);
		if (itMaterialHandler != PerMeshMaterialHandlers.end())
		{
			MaterialHandler = itMaterialHandler->second;
		}
		else
		{
			MaterialHandler = std::make_shared<FITwinIModelMaterialHandler>();

			// Use a temporary persistence manager, to avoid messing real materials.
			std::shared_ptr<AdvViz::SDK::MaterialPersistenceManager> SpecificPersistenceMngr;
			SpecificPersistenceMngr = std::make_shared<AdvViz::SDK::MaterialPersistenceManager>();
			auto const& GlobalPersistenceMngr = FITwinIModelMaterialHandler::GetGlobalPersistenceManager();
			if (ensure(GlobalPersistenceMngr))
			{
				SpecificPersistenceMngr->CopyPathsAndURLsFrom(*GlobalPersistenceMngr);
			}

			MaterialHandler->SetSpecificPersistenceManager(SpecificPersistenceMngr);
			MaterialHandler->InitForSingleMaterial(IModelId, MaterialId, SrcIModelMatHelper);
			PerMeshMaterialHandlers[MeshComponent] = MaterialHandler;
		}

		bool bValidMaterial = false;
		ITwinMaterial NewMaterial;

		std::visit([&](auto&& AssetInfo)
		{
			using T = std::decay_t<decltype(AssetInfo)>;
			if constexpr (std::is_same_v<T, FString>)
			{
				// This is the path to a material file.
				TextureKeySet NewTextures;
				TextureUsageMap NewTextureUsageMap;
				ETextureSource TexSource = ETextureSource::Library;
				bValidMaterial = FITwinMaterialLibrary::LoadMaterialFromAssetPath(AssetInfo, NewMaterial,
					NewTextures, NewTextureUsageMap, TexSource,
					MaterialHandler->GetPersistenceManager());
			}
			else if constexpr (std::is_same_v<T, ITwin::FMaterialPtr>)
			{
				if (AssetInfo)
				{
					NewMaterial = *AssetInfo;
					bValidMaterial = true;
				}
			}
			else static_assert(always_false_v<T>, "non-exhaustive visitor!");
		}, MaterialAssetInfo);

		if (!bValidMaterial)
		{
			return false;
		}

		UITwinMaterialPreviewHolder* MatPreviewComp = Cast<UITwinMaterialPreviewHolder>(
			UITwinMaterialPreviewHolder::StaticClass()->GetDefaultObject());
		UMaterialInterface* pBaseMaterial = nullptr;
		if (NewMaterial.kind == AdvViz::SDK::EMaterialKind::PBR)
		{
			if (NewMaterial.GetChannelIntensityOpt(AdvViz::SDK::EChannelType::Opacity).value_or(1.) < 1.)
				pBaseMaterial = MatPreviewComp->BaseMaterialTranslucent;
			else
				pBaseMaterial = MatPreviewComp->BaseMaterialMasked;
		}
		else // Glass
		{
			pBaseMaterial = MatPreviewComp->BaseMaterialGlass;
		}
		if (!ensure(IsValid(pBaseMaterial)))
		{
			return false;
		}

		UMaterialInstanceDynamic* pMaterial =
			ITwin::ChangeBaseMaterialInUEMesh(*MeshComponent, pBaseMaterial);
		if (ensure(pMaterial))
		{
			MeshComponent->SetMaterial(0, pMaterial);
		}

		// Now configure the material instance.
		// We will use a dummy scene mapping, with just one material instance (the one just created).
		auto DefaultTexturesHolder = NewObject<UITwinMaterialDefaultTexturesHolder>(&MeshActor,
			UITwinMaterialDefaultTexturesHolder::StaticClass(),
			FName(*(MeshActor.GetActorNameOrLabel() + "_DftTexHolder")));
		DefaultTexturesHolder->RegisterComponent();

		FITwinSceneMapping SceneMapping(false);
		UITwinSceneMappingBuilder::BuildFromNonCesiumMesh(SceneMapping, MeshComponent, MaterialId);

		return MaterialHandler->LoadMaterialFromAssetFile(MaterialId, MaterialAssetInfo,
			IModelId, SceneMapping,
			*DefaultTexturesHolder,
			true /*bForceRefreshAllParameters*/,
			[](ITwinMaterial& NewMat) {
				// Reduce normal mapping effect.
				if (NewMat.DefinesChannel(AdvViz::SDK::EChannelType::Normal))
				{
					NewMat.SetChannelIntensity(AdvViz::SDK::EChannelType::Normal,
						std::min(0.5, NewMat.GetChannelIntensityOpt(AdvViz::SDK::EChannelType::Normal).value_or(0.0)));
				}
			}
		);
	}
}


#if ENABLE_DRAW_DEBUG

// Console command to apply a dynamic material instance based on iTwin shaders to a mesh.
static FAutoConsoleCommandWithWorldAndArgs FCmd_ITwinSetMaterialToMesh(
	TEXT("cmd.ITwinSetMaterialToMesh"),
	TEXT("Create a dynamic material instance based on iTwin material and assign it to a mesh."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	if (Args.Num() != 2)
	{
		UE_LOG(LogITwin, Error, TEXT("Need exactly 2 args: <mesh_name> <material_name>"));
		return;
	}

	FString const MeshName = Args[0];
	FString const MaterialName = Args[1];

	AStaticMeshActor* EditedMesh = nullptr;
	TArray<AActor*> MeshActors;

	for (TActorIterator<AStaticMeshActor> Iter(World); Iter; ++Iter)
	{
		if (Iter->GetActorNameOrLabel() == MeshName)
		{
			EditedMesh = *Iter;
			break;
		}
	}
	if (EditedMesh == nullptr)
	{
		UE_LOG(LogITwin, Error, TEXT("no mesh found with name %s"), *MeshName);
		return;
	}

	ITwin::LoadMaterialOnGenericMesh(
		FITwinMaterialLibrary::GetBeLibraryPathForLoading(MaterialName),
		*EditedMesh);
}));

#endif // ENABLE_DRAW_DEBUG
