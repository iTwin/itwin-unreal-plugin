/*--------------------------------------------------------------------------------------+
|
|     $Source: DecorationAsyncIOHelper.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Decoration/DecorationAsyncIOHelper.h>
#include <Decoration/ITwinDecorationServiceSettings.h>
#include <ITwinIModel.h>
#include <ITwinServerConnection.h>
#include <Material/ITwinMaterialLibrary.h>

#include <ITwinCesiumRuntime.h>
#include <CesiumAsync/IAssetAccessor.h>
#include <CesiumAsync/IAssetResponse.h>
#include <CesiumGltfReader/GltfReader.h>

#include <Kismet/GameplayStatics.h>
#include <ImageUtils.h>
#include <Misc/Paths.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include "SDK/Core/Visualization/Config.h"
#	include "SDK/Core/Visualization/MaterialPersistence.h"
#	include "SDK/Core/Visualization/Timeline.h"
#	include <BeUtils/Gltf/GltfMaterialHelper.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>


namespace ITwin
{
	void InitDecorationServiceConnection(const UObject* WorldContextObject)
	{
		// Initialize the connection to the decoration service
		static bool needInitConfig = true;
		if (needInitConfig)
		{
			EITwinEnvironment Env = EITwinEnvironment::Prod;

			// Deduce environment from current iTwin authorization, if any.
			// Note that we must use the same environment both in iTwin IMS and decoration service, as the
			// token will be validated on both sides. Therefore, it is much preferable to have a valid
			// authorization at this point...
			AITwinServerConnection const* ServerConnection = Cast<AITwinServerConnection const>(
				UGameplayStatics::GetActorOfClass(WorldContextObject, AITwinServerConnection::StaticClass()));
			if (ensure(ServerConnection
				&& ServerConnection->Environment != EITwinEnvironment::Invalid))
			{
				Env = ServerConnection->Environment;
			}

			UITwinDecorationServiceSettings const* DecoSettings = GetDefault<UITwinDecorationServiceSettings>();

			SDK::Core::Config::SConfig sconfig;

			if (DecoSettings->bUseLocalServer)
			{
				sconfig.server.server = "localhost";
				sconfig.server.port = DecoSettings->LocalServerPort;
				sconfig.server.urlapiprefix = "/advviz/v1";
			}
			else
			{
				if (Env == EITwinEnvironment::Prod)
				{
					sconfig.server.server = "https://itwindecoration-eus.bentley.com";
				}
				else if (Env == EITwinEnvironment::Dev)
				{
					sconfig.server.server = "https://dev-itwindecoration-eus.bentley.com";
				}
				else
				{
					sconfig.server.server = "https://qa-itwindecoration-eus.bentley.com";
				}
				sconfig.server.urlapiprefix = "/advviz/v1";

				//if (Env == EITwinEnvironment::Dev)
				//{
				//	sconfig.server.server = "https://dev-api.bentley.com";
				//}
				//else
				//{
				//	sconfig.server.server = "https://api.bentley.com";
				//}
				//sconfig.server.urlapiprefix = "/";
			}

			if (!DecoSettings->CustomServer.IsEmpty())
			{
				sconfig.server.server = TCHAR_TO_UTF8(*DecoSettings->CustomServer);
			}
			if (!DecoSettings->CustomUrlApiPrefix.IsEmpty())
			{
				sconfig.server.urlapiprefix = TCHAR_TO_UTF8(*DecoSettings->CustomUrlApiPrefix);
			}
			SDK::Core::Config::Init(sconfig);
			needInitConfig = false;
		}
	}
}

void FDecorationAsyncIOHelper::SetLoadedITwinInfo(FITwinLoadInfo const& InLoadedITwinInfo)
{
	LoadedITwinInfo = InLoadedITwinInfo;
}

FITwinLoadInfo const& FDecorationAsyncIOHelper::GetLoadedITwinInfo() const
{
	return LoadedITwinInfo;
}

void FDecorationAsyncIOHelper::RequestStop()
{
	*shouldStop = true;
}

bool FDecorationAsyncIOHelper::IsInitialized() const
{
	return (decoration && instancesManager && materialPersistenceMngr);
}

void FDecorationAsyncIOHelper::InitDecorationService(const UObject* WorldContextObject)
{
	if (decoration && instancesManager && materialPersistenceMngr)
	{
		// Already done.
		return;
	}
	ITwin::InitDecorationServiceConnection(WorldContextObject);

	decoration.reset(SDK::Core::IDecoration::New());
	decorationITwin = std::make_shared<FString>();

	instancesManager.reset(SDK::Core::IInstancesManager::New());

	instancesGroup.reset(SDK::Core::IInstancesGroup::New());
	instancesGroup->SetName("InstGroup");
	instancesManager->AddInstancesGroup(instancesGroup);

	// Material persistence is managed by the decoration service, except for the (packaged) Material Library
	materialPersistenceMngr = std::make_shared<SDK::Core::MaterialPersistenceManager>();
	FString const MaterialLibraryPath = FPaths::ProjectContentDir() / ITwin::MAT_LIBRARY;
	materialPersistenceMngr->SetMaterialLibraryDirectory(TCHAR_TO_UTF8(*MaterialLibraryPath));
	AITwinIModel::SetMaterialPersistenceManager(materialPersistenceMngr);

	scene.reset(SDK::Core::IScenePersistence::New());
}


bool FDecorationAsyncIOHelper::LoadITwinDecoration(std::string const& accessToken)
{
	if (accessToken.empty())
	{
		ensureMsgf(false, TEXT("No authorization to load any decoration"));
		return false;
	}
	if (!decoration)
	{
		ensureMsgf(false, TEXT("InitDecorationService must be called before, in game thread"));
		return false;
	}
	if (LoadedITwinInfo.ITwinId.IsEmpty())
		return false;

	if (decoration
		&& !decoration->GetId().empty()
		&& decorationITwin
		&& *decorationITwin == LoadedITwinInfo.ITwinId)
	{
		// Decoration already loaded for current iTwin => nothing to do.
		return true;
	}

	decorationIsLinked = false;
	for (auto link : scene->GetLinks())
	{
		if (link->GetType() == "decoration")
		{
			decoration.reset(SDK::Core::IDecoration::New());
			decoration->Get(link->GetRef(), accessToken);
			decorationIsLinked = true;
			break;
		}
	}
	std::string itwinid = TCHAR_TO_UTF8(*(LoadedITwinInfo.ITwinId));
	if (!decoration
		|| decoration->GetId().empty())
	{

		// Get the decoration associated with the current ITwin
		std::vector<std::shared_ptr<SDK::Core::IDecoration>> decorations =
			SDK::Core::GetITwinDecorations(itwinid, accessToken);

		if (decorations.empty() || !decorations[0] ||
			decorations[0]->GetId().empty())
			return false;
		decoration = decorations[0];
	}

	if (*shouldStop)
	{
		BE_LOGI("ITwinDecoration", "aborted load decoration task - would select decoration " << decoration->GetId() << " for itwin " << itwinid);
		return false;
	}
	*decorationITwin = LoadedITwinInfo.ITwinId;

	BE_LOGI("ITwinDecoration", "Selected decoration " << decoration->GetId() << " for itwin " << itwinid);

	return true;
}

bool FDecorationAsyncIOHelper::LoadPopulationsFromServer(std::string const& accessToken)
{
	if (!instancesManager)
	{
		ensureMsgf(false, TEXT("InitDecorationService must be called before, in game thread"));
		return false;
	}
	if (!LoadITwinDecoration(accessToken))
	{
		return false;
	}
	instancesManager->LoadDataFromServer(decoration->GetId(), accessToken);
	return true;
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

bool FDecorationAsyncIOHelper::LoadCustomMaterials(std::string const& accessToken,
	TMap<FString, TWeakObjectPtr<AITwinIModel>> const& idToIModel)
{
	if (!materialPersistenceMngr)
	{
		ensureMsgf(false, TEXT("InitDecorationService must be called before, in game thread"));
		return false;
	}

	// Load material customizations from the Decoration Service
	if (!LoadITwinDecoration(accessToken))
	{
		return false;
	}

	materialPersistenceMngr->LoadDataFromServer(decoration->GetId(), accessToken);

	SDK::Core::PerIModelTextureSet perModelTextures;
	materialPersistenceMngr->GetDecorationTexturesByIModel(perModelTextures);

	return ITwin::ResolveDecorationTextures(*materialPersistenceMngr, perModelTextures, idToIModel, accessToken);
}

bool ITwin::ResolveDecorationTextures(
	SDK::Core::MaterialPersistenceManager& matPersistenceMngr,
	SDK::Core::PerIModelTextureSet const& perModelTextures,
	TMap<FString, TWeakObjectPtr<AITwinIModel>> const& idToIModel,
	std::string const& accessToken)
{
	// Download decoration textures if needed (note that we use Cesium's sqlite caching system, so this
	// should be fast except for the very first time).
	CesiumAsync::HttpHeaders const tHeaders =
	{
		{
			"Authorization",
			std::string("Bearer ") + accessToken
		}
	};

	const std::shared_ptr<CesiumAsync::IAssetAccessor>& pAssetAccessor = ITwinCesium::getAssetAccessor();
	const CesiumAsync::AsyncSystem& asyncSystem = ITwinCesium::getAsyncSystem();

	CesiumGltfReader::GltfReaderResult gltfResult;
	auto& model = gltfResult.model.emplace();
	auto& images = model.images;
	images.reserve(perModelTextures.size() * 5);

	struct LoadedImageInfo
	{
		size_t imgIndex = 0;
		SDK::Core::TextureKey texKey;
	};
	struct IModelImageVec
	{
		std::shared_ptr<BeUtils::GltfMaterialHelper> matHelper;
		std::vector<LoadedImageInfo> imageInfos;
	};
	std::vector<IModelImageVec> imageCorresp;
	imageCorresp.reserve(perModelTextures.size());

	size_t gltfImageIndex = 0;
	for (auto const& [imodelid, textureSet] : perModelTextures)
	{
		auto pImodel = idToIModel.Find(UTF8_TO_TCHAR(imodelid.c_str()));
		if (!pImodel || !pImodel->IsValid())
			continue;
		auto glTFMatHelper = pImodel->Get()->GetGltfMaterialHelper();
		if (!glTFMatHelper)
			continue;

		IModelImageVec& imodelImgs = imageCorresp.emplace_back();
		imodelImgs.matHelper = glTFMatHelper;
		imodelImgs.imageInfos.reserve(textureSet.size());

		// Download (or read from sqlite cache) all decoration textures used by this model
		for (auto const& texKey : textureSet)
		{
			imodelImgs.imageInfos.push_back({ gltfImageIndex, texKey });
			auto& gltfImage = images.emplace_back();
			gltfImageIndex++;
			gltfImage.uri = matPersistenceMngr.GetTextureURL(texKey);
		}
	}

	// We restrict the formats to JPG and PNG, so we can leave the default options (no need to setup
	// Ktx2TranscodeTargets...)
	CesiumGltfReader::GltfReaderOptions gltfOptions;
	CesiumGltfReader::GltfReader::resolveExternalData(
		asyncSystem,
		"",
		tHeaders,
		pAssetAccessor,
		gltfOptions,
		std::move(gltfResult))
		.thenImmediately([imageCorresp](CesiumGltfReader::GltfReaderResult&& result) {
		auto& cesiumImages = result.model->images;
		// Dispatch the downloaded images to the appropriate material helper
		for (IModelImageVec const& imodelImgs : imageCorresp)
		{
			BeUtils::GltfMaterialHelper::Lock lock(imodelImgs.matHelper->GetMutex());
			for (LoadedImageInfo const& info : imodelImgs.imageInfos)
			{
				imodelImgs.matHelper->StoreCesiumImage(info.texKey,
					std::move(cesiumImages[info.imgIndex]),
					lock);
			}
		}
	}).wait();

	return true;
}

UTexture2D* ITwin::ResolveMatLibraryTexture(
	BeUtils::GltfMaterialHelper const& GltfMatHelper,
	std::string const& TextureId)
{
	TArray64<uint8> Buffer;

	// Those textures are packaged locally, no need to use any authorization token.
	std::vector<CesiumAsync::IAssetAccessor::THeader> const tHeaders = {};

	const std::shared_ptr<CesiumAsync::IAssetAccessor>& pAssetAccessor = ITwinCesium::getAssetAccessor();
	const CesiumAsync::AsyncSystem& asyncSystem = ITwinCesium::getAsyncSystem();
	const std::string textureURI = GltfMatHelper.GetTextureURL(TextureId, SDK::Core::ETextureSource::Library);
	pAssetAccessor
		->get(asyncSystem, textureURI, tHeaders)
		.thenImmediately([&Buffer](
			std::shared_ptr<CesiumAsync::IAssetRequest>&& pRequest) {
		const CesiumAsync::IAssetResponse* pResponse = pRequest->response();
		if (pResponse) {
			Buffer.Append(
				reinterpret_cast<const uint8*>(pResponse->data().data()),
				pResponse->data().size());
		}
	}).wait();

	if (Buffer.IsEmpty())
	{
		BE_LOGE("ITwinDecoration", "[MAT_LIBRARY] could not load texture " << TextureId);
		return nullptr;
	}
	return FImageUtils::ImportBufferAsTexture2D(Buffer);
}

bool FDecorationAsyncIOHelper::SaveDecorationToServer(std::string const& accessToken)
{
	bool const saveInstances = instancesManager	&& instancesManager->HasInstancesToSave();
	bool const saveMaterials = materialPersistenceMngr && materialPersistenceMngr->NeedUpdateDB();
	if (!saveInstances && !saveMaterials)
	{
		return false;
	}
	if (accessToken.empty())
	{
		ensureMsgf(false, TEXT("No authorization to save decoration"));
		return false;
	}
	if (LoadedITwinInfo.ITwinId.IsEmpty() || !decoration)
	{
		return false;
	}
	std::string const itwinid = TCHAR_TO_UTF8(*(LoadedITwinInfo.ITwinId));

	if (decoration->GetId().empty())
	{
		decoration->Create("Decoration", itwinid, accessToken);
	}

	if (*shouldStop)
	{
		BE_LOGI("ITwinDecoration", "aborted save decoration task for itwin " << itwinid);
		return false;
	}

	if (!decoration->GetId().empty())
	{
		BE_LOGI("ITwinDecoration", "Saving decoration " << decoration->GetId()
			<< " for itwin " << itwinid << "...");

		if (saveInstances)
			instancesManager->SaveDataOnServer(decoration->GetId(), accessToken);
		if (saveMaterials)
			materialPersistenceMngr->SaveDataOnServer(decoration->GetId(), accessToken);

		return true;
	}
	return false;
}

bool FDecorationAsyncIOHelper::LoadSceneFromServer(std::string const& accessToken, std::shared_ptr<SDK::Core::ITimeline>& timeline)
{
	if (accessToken.empty())
	{
		ensureMsgf(false, TEXT("No authorization to load any decoration"));
		return false;
	}
	if (!scene)
	{
		ensureMsgf(false, TEXT("InitDecorationService must be called before, in game thread"));
		return false;
	}
	if (LoadedITwinInfo.ITwinId.IsEmpty())
		return false;

	std::string const itwinid = TCHAR_TO_UTF8(*(LoadedITwinInfo.ITwinId));

	if (scene
		&& !scene->GetId().empty()
		&& scene->GetITwinId() == itwinid)
	{
		// scene already loaded for current iTwin => nothing to do.
		return true;
	}

	auto createTimeline = [&timeline, &accessToken](const std::string& sceneId) {
		if (!timeline)
		{
			std::string sceneName = "myscene";
			auto ret = SDK::Core::AddSceneTimeline(sceneId, accessToken, sceneName);
			if (!ret)
			{
				BE_LOGE("Timeline", "AddSceneTimeline failed, error:" << ret.error());
			}
			else
			{
				timeline = std::shared_ptr<SDK::Core::ITimeline>(SDK::Core::ITimeline::New());
				auto timelineId = *ret;
				auto ret2 = timeline->Load(sceneId, accessToken, timelineId);
				if (!ret2)
				{
					BE_LOGE("Timeline", "Load failed, id:" << (std::string&)timelineId << "error:" << ret2.error());
					timeline.reset();
				}
			}
		}
		};

	using namespace SDK::Core;
	std::vector<std::shared_ptr<IScenePersistence>> scenes = GetITwinScenes(itwinid, accessToken);
	if (scenes.empty())
	{
		scene->PrepareCreation("default scene", itwinid);
		createTimeline(scene->GetId());
		return false;
	}
	else
	{
		std::string const iModelId = TCHAR_TO_UTF8(*(LoadedITwinInfo.IModelId));
		std::shared_ptr<IScenePersistence> bestSceneCandidate;
		for (auto scen : scenes)
		{
			for (auto link : scen->GetLinks())
			{
				if (link->GetType() == "iModel" && link->GetRef() == iModelId)
				{
					bestSceneCandidate = scen;
				}
			}
			if (bestSceneCandidate)
			{
				break;
			}
		}
		std::string sceneId = bestSceneCandidate ? bestSceneCandidate->GetId() : scenes[0]->GetId();//todo choose one scene
		LoadSceneFromServer(sceneId, accessToken);

		// Load or create timeline
		timeline.reset();
		auto tmInfo = GetSceneTimelines(sceneId, accessToken);
		if (tmInfo && !(*tmInfo).empty())
		{
			timeline = std::shared_ptr<SDK::Core::ITimeline>(SDK::Core::ITimeline::New());
			auto timelineId = (*tmInfo)[0].id;
			auto ret = timeline->Load(sceneId, accessToken, timelineId);
			if (!ret)
			{
				BE_LOGE("Timeline", "Load failed, id:" << (std::string&)timelineId << " error:" << ret.error());
				timeline.reset();
			}
		}
		createTimeline(sceneId);


		return true;
	}

}

bool FDecorationAsyncIOHelper::LoadSceneFromServer(std::string const& sceneid, std::string const& accessToken)
{
	scene.reset(SDK::Core::IScenePersistence::New());
	bool res = scene->Get(sceneid, accessToken);
	links.clear();
	for (auto l : scene->GetLinks())
	{
		if (l->GetType() == "iModel" || l->GetType() == "RealityData") // ignore other types for now 
		{
			std::pair<EITwinModelType, FString> key;
			key.first =(l->GetType() == "iModel") ? EITwinModelType::IModel : EITwinModelType::RealityData;
			key.second = UTF8_TO_TCHAR(l->GetRef().c_str());
			links[key] = l;
}
	}
	return res;
}

bool FDecorationAsyncIOHelper::SaveSceneToServer(std::string const& accessToken, const std::shared_ptr<SDK::Core::ITimeline> &timeline)
{
	if (accessToken.empty())
	{
		ensureMsgf(false, TEXT("No authorization to save decoration"));
		return false;
	}
	if (LoadedITwinInfo.ITwinId.IsEmpty() || !scene)
	{
		return false;
	}
	std::string const itwinid = TCHAR_TO_UTF8(*(LoadedITwinInfo.ITwinId));
	if (!decorationIsLinked && decoration && !decoration->GetId().empty())
	{
		std::shared_ptr<SDK::Core::Link> nulink(SDK::Core::Link::New());
		nulink->SetType("decoration");
		nulink->SetRef(decoration->GetId());
		scene->AddLink(nulink);
		decorationIsLinked = true;
	}
	std::pair<EITwinModelType, FString> loadedkey;
	loadedkey.first = LoadedITwinInfo.ModelType;
	loadedkey.second = (LoadedITwinInfo.ModelType == EITwinModelType::IModel)? LoadedITwinInfo.IModelId : LoadedITwinInfo.RealityDataId;

	if (links.find(loadedkey)== links.end() && !loadedkey.second.IsEmpty())
	{
		CreateLink(loadedkey.first, loadedkey.second);
	}
	if (scene->ShouldSave())
	{
		scene->Save(accessToken);
	}

	{// save timeline
		if (timeline)
			timeline->Save(scene->GetId(), accessToken);
	}

	return true;
}

std::shared_ptr <SDK::Core::Link>  FDecorationAsyncIOHelper::CreateLink(EITwinModelType ct, const FString& id)
{
	std::pair<EITwinModelType, FString> nukey = std::make_pair(ct,id);
	auto iter = links.find(nukey);
	if (iter == links.end())
	{
		auto link = std::shared_ptr<SDK::Core::Link>(SDK::Core::Link::New());
		link->SetType((ct == EITwinModelType::IModel) ? "iModel" : "RealityData");
		link->SetRef(TCHAR_TO_UTF8(*(id)));
		scene->AddLink(link);
		links[nukey] = link;
		return link;
	}
	else
	{
		return iter->second;
	}
}