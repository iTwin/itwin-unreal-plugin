/*--------------------------------------------------------------------------------------+
|
|     $Source: DecorationAsyncIOHelper.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Decoration/DecorationAsyncIOHelper.h>

#include <Decoration/DecorationWaitableLoadEvent.h>
#include <Decoration/ITwinDecorationServiceSettings.h>
#include <ITwinIModel.h>
#include <ITwinGeolocation.h>
#include <ITwinServerConnection.h>
#include <Material/ITwinMaterialLibrary.h>
#include <Material/ITwinTextureLoadingUtils.h>

#include <CesiumRuntime.h>
#include <CesiumAsync/IAssetAccessor.h>
#include <CesiumAsync/IAssetResponse.h>
#include <CesiumGltfReader/GltfReader.h>

#include <Kismet/GameplayStatics.h>
#include <ImageUtils.h>
#include <Misc/FileHelper.h>
#include <Misc/Paths.h>
#include <Misc/MessageDialog.h>
#include <Population/ITwinPopulation.h>
#include <HttpManager.h>
#include <HttpModule.h>
#include <Math/UEMathConversion.h>
#include <Engine/World.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Visualization/Config.h>
#	include <SDK/Core/Visualization/MaterialPersistence.h>
#	include "SDK/Core/Visualization/Timeline.h"
#	include "SDK/Core/Visualization/ScenePersistenceAPI.h"
#	include "SDK/Core/Visualization/ScenePersistenceDS.h"
#	include <SDK/Core/Visualization/SplinesManager.h>
#	include <SDK/Core/Visualization/KeyframeAnimation.h>
#	include <SDK/Core/Visualization/PathAnimation.h>
#	include <BeUtils/Gltf/GltfMaterialHelper.h>
#	include <BeUtils/Gltf/GltfMaterialTuner.h>
#	include <BeUtils/Misc/RWLock.h>
#	include <numbers>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>


static bool bDefaultUseOfDecorationService = true;
static std::string defaultSceneName = ITWIN_DEFAULT_SCENE_NAME;
namespace ITwin
{
	void ConnectLoadTexture();
	
	bool DownloadOneDecorationTexture(AdvViz::SDK::TextureKey const& texKey, TArray64<uint8>& Buffer)
	{
		using namespace AdvViz::SDK;
		BE_ASSERT(texKey.eSource == ETextureSource::Decoration);

		if (!ensure(AdvViz::SDK::GetDefaultHttp()->GetAccessToken()
				&& AITwinIModel::GetMaterialPersistenceManager()))
		{
			return false;
		}

		std::vector<CesiumAsync::IAssetAccessor::THeader> const tHeaders =
		{
			{
				"Authorization",
				std::string("Bearer ") + *AdvViz::SDK::GetDefaultHttp()->GetAccessToken()
			}
		};

		// This call should be very fast, as the image, if available, is already in Cesium cache.
		// And since we test TexAccess.cesiumImage before coming here, we *know* that the image is indeed
		// available.
		const std::shared_ptr<CesiumAsync::IAssetAccessor>& pAssetAccessor = getAssetAccessor();
		const CesiumAsync::AsyncSystem& asyncSystem = getAsyncSystem();
		const std::string textureURI = AITwinIModel::GetMaterialPersistenceManager()
			->GetTextureURL(texKey.id, ETextureSource::Decoration);
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

		return !Buffer.IsEmpty();
	}

	ITWINRUNTIME_API bool LoadTextureBuffer(AdvViz::SDK::TextureKey const& texKey,
		BeUtils::GltfMaterialHelper const& matHelper,
		BeUtils::RWLockBase const& lock,
		TArray64<uint8>& outBuffer,
		std::string& strError)
	{
		using namespace AdvViz::SDK;

		FString TexturePath;
		switch (texKey.eSource)
		{
		case ETextureSource::Library:
			TexturePath = FITwinMaterialLibrary::GetBentleyLibraryPath() / UTF8_TO_TCHAR(texKey.id.c_str());
			break;
		case ETextureSource::LocalDisk:
			TexturePath = UTF8_TO_TCHAR(texKey.id.c_str());
			break;
		case ETextureSource::ITwin:
		{
			// Textures coming from the iTwin model itself should be found in local cache.
			std::filesystem::path const& texLocalPath = matHelper.GetTextureLocalPath(texKey, lock);
			if (!texLocalPath.empty())
			{
				TexturePath = UTF8_TO_TCHAR(texLocalPath.generic_string().c_str());
			}
			break;
		}
		case ETextureSource::Decoration:
			if (!DownloadOneDecorationTexture(texKey, outBuffer))
			{
				strError = std::string("error downloading decoration texture ") + texKey.id;
				return false;
			}
			break;
		}

		if (!TexturePath.IsEmpty())
		{
			if (!FFileHelper::LoadFileToArray(outBuffer, *TexturePath))
			{
				strError = std::string("error loading file ") + TCHAR_TO_UTF8(*TexturePath);
				return false;
			}
		}
		return !outBuffer.IsEmpty();
	}

	void ConnectLoadTexture()
	{
		using namespace AdvViz::SDK;

		BeUtils::GltfMaterialTuner::ConnectLoadTextureBufferFunc(
			[](	TextureKey const& texKey,
				BeUtils::GltfMaterialHelper const& matHelper,
				BeUtils::RWLockBase const& lock,
				std::vector<std::byte>& cesiumBuffer,
				std::string& strError) -> bool
		{
			cesiumBuffer.clear();
			TArray64<uint8> Buffer;
			if (!LoadTextureBuffer(texKey, matHelper, lock, Buffer, strError))
			{
				return false;
			}
			if (!Buffer.IsEmpty())
			{
				cesiumBuffer.reserve(Buffer.Num());
				for (uint8 c : Buffer)
				{
					cesiumBuffer.push_back(static_cast<std::byte>(c));
				}
				return true;
			}
			else
			{
				return false;
			}
		});
	}
}


FDecorationAsyncIOHelper::~FDecorationAsyncIOHelper()
{

}

void FDecorationAsyncIOHelper::SetLoadedITwinId(const FString& ITwinId)
{
	AdvViz::SDK::Tools::GetCrashInfo()->AddInfo("ITwinId", TCHAR_TO_UTF8(*ITwinId));
	LoadedITwinId = ITwinId;
}

FString FDecorationAsyncIOHelper::GetLoadedITwinId() const
{
	return LoadedITwinId;
}

void FDecorationAsyncIOHelper::SetLoadedSceneId(FString InLoadedSceneId, bool inNewsScene /*= false*/)
{
	LoadedSceneID = InLoadedSceneId;
	bSceneIdIsForNewScene = inNewsScene;
}

void FDecorationAsyncIOHelper::RequestStop()
{
	*shouldStop = true;
}

bool FDecorationAsyncIOHelper::IsInitialized() const
{
	return (decoration && instancesManager_ && materialPersistenceMngr && splinesManager && annotationsManager && pathAnimator);
}

void FDecorationAsyncIOHelper::InitDecorationServiceConnection(const UWorld* WorldContextObject)
{
	using namespace AdvViz::SDK;
	// Initialize the connection to the decoration service
	if (bNeedInitConfig)
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

		Config::SConfig sconfig;

		if (DecoSettings->bUseLocalServer)
		{
			sconfig.server.server = "http://127.0.0.1";
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

		Tools::GetCrashInfo()->AddInfo("DecoService.Server", sconfig.server.server);
		Tools::GetCrashInfo()->AddInfo("DecoService.urlapiprefix", sconfig.server.urlapiprefix);

		Config::Init(sconfig);
		if (ServerConnection)
		{
			GetDefaultHttp()->SetAccessToken(ServerConnection->GetAccessTokenPtr());
		}
		ScenePersistenceAPI::SetDefaultHttp(GetDefaultHttp());
		if (ServerConnection && ServerConnection->SceneService != EITwinSceneService::Invalid)
		{
			bDefaultUseOfDecorationService = ServerConnection->SceneService == EITwinSceneService::DecorationService;
		}
		else
		{
			bDefaultUseOfDecorationService = Env == EITwinEnvironment::Prod;
		}

		// We may customize the activation of the Decoration Service for scene persistence from the
		// configuration file:
		if (!DecoSettings->CustomEnvsWithScenePersistenceDS.IsEmpty())
		{
			FString EnvStr = UEnum::GetValueAsString(Env);
			int32 Index(0);
			if (EnvStr.FindLastChar(TEXT(':'), Index) && ensure(Index != EnvStr.Len() - 1))
			{
				EnvStr.RightChopInline(Index + 1);
			}
			bDefaultUseOfDecorationService = DecoSettings->CustomEnvsWithScenePersistenceDS.Contains(*EnvStr);
		}

		// Connect the retrieval of cesium textures for Unreal packaged assets or decoration service.
		ITwin::ConnectLoadTexture();

		bNeedInitConfig = false;
	}
}

void FDecorationAsyncIOHelper::InitDecorationService(UWorld* WorldContextObject)
{
	using namespace AdvViz::SDK;
	if (decoration && instancesManager_ && materialPersistenceMngr && splinesManager && annotationsManager && pathAnimator)
	{
		// Already done.
		return;
	}
	InitDecorationServiceConnection(WorldContextObject);
	bUseDecorationService = bDefaultUseOfDecorationService;

	decoration.reset(IDecoration::New());
	decorationITwin = std::make_shared<FString>();

	instancesManager_.reset(IInstancesManager::New());

	staticInstancesGroup.reset(IInstancesGroup::New());
	staticInstancesGroup->SetName("staticInstances");
	instancesManager_->AddInstancesGroup(staticInstancesGroup);

	// Material persistence is managed by the decoration service, except for the (packaged) Material Library
	materialPersistenceMngr = std::make_shared<MaterialPersistenceManager>();
	FString const MaterialLibraryPath = FITwinMaterialLibrary::GetBentleyLibraryPath();
	materialPersistenceMngr->SetMaterialLibraryDirectory(TCHAR_TO_UTF8(*MaterialLibraryPath));
	AITwinIModel::SetMaterialPersistenceManager(materialPersistenceMngr);

	if (bUseDecorationService)
	{
		scene.reset(ScenePersistenceDS::New());
	}
	else
	{
		scene.reset(ScenePersistenceAPI::New());
	}
	splinesManager.reset(ISplinesManager::New());
	annotationsManager.reset(IAnnotationsManager::New());

	pathAnimator.reset(AdvViz::SDK::IPathAnimator::New());
	pathAnimator->SetInstanceManager(instancesManager_);
	pathAnimator->SetSplinesManager(splinesManager);

	// Connect the instance manager to the spline manager, in order to be able to reload instances groups
	// linked to splines correctly.
	instancesManager_->SetSplineManager(splinesManager);
	// Connect the instance manager to the animation path manager, in order to be able to save/reload animation paths associated with instances.
	instancesManager_->SetAnimPathManager(pathAnimator);
}

void FDecorationAsyncIOHelper::SetDecoGeoreference(const FVector& latLongHeightDeg)
{
	using namespace AdvViz::SDK;
	if (decoration && !decoration->GetGCS())
	{
		GCS WGS8gcs = GCSTransform::GetECEFWGS84WKT();
		WGS8gcs.center[0] = latLongHeightDeg[0];
		WGS8gcs.center[1] = latLongHeightDeg[1];
		WGS8gcs.center[2] = latLongHeightDeg[2];
		decoration->SetGCS(WGS8gcs);
		InitDecoGeoreference();
	}
}

class FUnrealDecorationGCSTransform : public AdvViz::SDK::Tools::IGCSTransform {
public:
	FMatrix ENU2ECEF; // from ENU (unreal local East North Up) to ECEF (Earth-Centered, Earth-Fixed)
	FMatrix ECEF2ENU; // from ECEF (Earth-Centered, Earth-Fixed) to ENU (unreal local East North Up)
	FVector scale;
	FVector invscale;
	void Init(const AdvViz::SDK::double3& latLonHeightDeg)
	{
		AdvViz::SDK::double3 latLonHeightRad = { latLonHeightDeg[0] * (std::numbers::pi / 180.0),
			latLonHeightDeg[1] * (std::numbers::pi / 180.0),
			latLonHeightDeg[2] };

		AdvViz::SDK::dmat4x4 ecf2enu = AdvViz::SDK::GCSTransform::WGS84ECEFToENUMatrix(latLonHeightRad);
		AdvViz::SDK::dmat4x4 enu2ecf = AdvViz::SDK::GCSTransform::WGS84ENUToECEFMatrix(latLonHeightRad);
		ENU2ECEF = toUnreal(enu2ecf);
		ECEF2ENU = toUnreal(ecf2enu);

		scale = FVector(1. / 100.0, -1. / 100.0, 1. / 100.0);
		invscale = FVector(100.0, -100.0, 100.0);
	}

	virtual AdvViz::SDK::double3 PositionFromClient(const AdvViz::SDK::double3& p)
	{
		FVector v = toUnreal(p);
		FVector r = ENU2ECEF.TransformPosition(v * scale);
		return toAdvizSdk(r);
	}
	virtual AdvViz::SDK::double3 PositionToClient(const AdvViz::SDK::double3& p)
	{
		FVector v = toUnreal(p);
		FVector r = ECEF2ENU.TransformPosition(v) * invscale;
		return toAdvizSdk(r);
	}

	virtual AdvViz::SDK::dmat4x4 MatrixFromClient(const AdvViz::SDK::dmat4x4& m)
	{
		FMatrix um = toUnreal(m);
		// transform translation
		FVector trans = FVector(um.M[3][0], um.M[3][1], um.M[3][2]);
		trans = ENU2ECEF.TransformPosition(trans * scale);
		// transform rotation
		FMatrix rm = ENU2ECEF * um;
		// set translation
		rm.M[3][0] = trans[0];
		rm.M[3][1] = trans[1];
		rm.M[3][2] = trans[2];
		return toAdvizSdk(rm);
	}
	virtual AdvViz::SDK::dmat4x4 MatrixToClient(const AdvViz::SDK::dmat4x4& m)
	{
		FMatrix um = toUnreal(m);
		// transform translation
		FVector trans = FVector(um.M[3][0], um.M[3][1], um.M[3][2]);
		trans = ECEF2ENU.TransformPosition(trans) * invscale;
		// transform rotation
		FMatrix rm = ECEF2ENU * um;
		// set translation
		rm.M[3][0] = trans[0];
		rm.M[3][1] = trans[1];
		rm.M[3][2] = trans[2];
		return toAdvizSdk(rm);
	}
};

AdvViz::expected<void, std::string> FDecorationAsyncIOHelper::InitDecoGeoreference()
{
	using namespace AdvViz::SDK;
	FITwinMathConversion::transform_.reset();
	if (decoration && decoration->GetGCS())
	{
		const std::string& wkt = decoration->GetGCS()->wkt;
		//Check if it is WGS84
		if (decoration->GetGCS()->wkt.find("WGS 84 (G2296)") == std::string::npos) // not WGS84
		{
			// use default identity transformation
			auto prevNew = IGCSTransform::GetNewFct();
			IGCSTransform::SetNewFct([]() {
				return static_cast<IGCSTransform*>(new GCSTransform()); //use default identity transform
				});
			IGCSTransformPtr UnrealDecorationGCSTransform(IGCSTransform::New());
			decoration->SetGCSTransform(UnrealDecorationGCSTransform);
			IGCSTransform::SetNewFct(prevNew);
			FITwinMathConversion::transform_ = decoration->GetGCSTransform();

			return AdvViz::make_unexpected("Only WGS84 georeference is supported for now.");
		}

		double3 latLonHeightDeg = decoration->GetGCS()->center;
		auto prevNew = IGCSTransform::GetNewFct();
		IGCSTransform::SetNewFct([]() {
			return static_cast<IGCSTransform*>(new FUnrealDecorationGCSTransform());
			});
		// Create the transform between Unreal coordinates (East, North, Up) and ECEF
		IGCSTransformPtr UnrealDecorationGCSTransform(IGCSTransform::New());
		static_cast<FUnrealDecorationGCSTransform*>(UnrealDecorationGCSTransform.get())->Init(latLonHeightDeg);
		decoration->SetGCSTransform(UnrealDecorationGCSTransform);
		IGCSTransform::SetNewFct(prevNew);
		FITwinMathConversion::transform_ = decoration->GetGCSTransform();
	}

	return {};
}

bool FDecorationAsyncIOHelper::LoadITwinDecoration()
{
	if (!decoration)
	{
		ensureMsgf(false, TEXT("InitDecorationService must be called before, in game thread"));
		return false;
	}
	if (LoadedITwinId.IsEmpty())
		return false;

	if (decoration
		&& !decoration->GetId().empty()
		&& decorationITwin
		&& *decorationITwin == LoadedITwinId)
	{
		// Decoration already loaded for current iTwin => nothing to do.
		return true;
	}

	decorationIsLinked = false;
	for (auto link : scene->GetLinks())
	{
		if (link->GetType() == "decoration")
		{
			decoration.reset(AdvViz::SDK::IDecoration::New());
			decoration->Get(link->GetRef());
			// If no georeference is defined in the decoration, we create a default one
			if (!decoration->GetGCS() || decoration->GetGCS()->wkt.empty()) // for compatibility with old decorations, we set a default local georeference
			{
				const char* unrealCrsWkt = "ENGCRS[\"Local Unreal 3D Cartesian CRS (cm)\","
					"DATUM[\"Local Datum\","
					"ELLIPSOID[\"Unit Sphere\", 1, 0, LENGTHUNIT[\"centimetre\", 0.01]]],"
					"CS[Cartesian, 3],"
					"AXIS[\"X\", forward],"
					"AXIS[\"Y\", right],"
					"AXIS[\"Z\", up],"
					"LENGTHUNIT[\"centimetre\", 0.01]]";
				AdvViz::SDK::GCS gcs;
				gcs.wkt = unrealCrsWkt;
				decoration->SetGCS(gcs);
			}
			auto decoGeoref = InitDecoGeoreference();
			if (!decoGeoref)
				BE_LOGW("ITwinDecoration", "Failed to init georeference: " << decoGeoref.error());
			decorationIsLinked = true;
			break;
		}
	}
	//else keep new default decoration and do not load a old one
	std::string itwinid = TCHAR_TO_UTF8(*(LoadedITwinId));
	if (decoration->GetId().empty())
		return false;
	*decorationITwin = LoadedITwinId;
	BE_LOGI("ITwinDecoration", "Selected decoration " << decoration->GetId() << " for itwin " << itwinid);
	AdvViz::SDK::Tools::GetCrashInfo()->AddInfo("decorationId", (std::string)decoration->GetId());

	return true;
}

bool FDecorationAsyncIOHelper::LoadPopulationsFromServer()
{
	if (!instancesManager_)
	{
		ensureMsgf(false, TEXT("InitDecorationService must be called before, in game thread"));
		return false;
	}

	LoadAnimationKeyframesFromServer();
	
	if (!LoadITwinDecoration())
	{
		return false;
	}
	
	AdvViz::SDK::IInstance::SetNewFct([]() {
		return static_cast<AdvViz::SDK::IInstance*>(new FITwinInstance());
		});

	instancesManager_->LoadDataFromServer(decoration->GetId(), staticInstancesGroup);
	

	return true;
}

static const FVector g_basedPos(-14.98, 221.96, -30.0);
//Vector basedPos(-99.40f, 30.35f, -13.28);
void GeneratePaths(const std::string& itwinid, auto& animationKeyframes)
{
#if 0 //D-O-NOTC
	// Temporary
	// create animation test
	using namespace AdvViz::SDK;
	using namespace AdvViz::SDK::Tools;
	auto res = CreateAnimationKeyframe(itwinid, "animTest");

	if (res)
	{
		IAnimationKeyframePtr animationKeyframePtr = res.value();
		auto lockanimationKeyframe(animationKeyframePtr->GetAutoLock());
		auto& animationKeyframe = lockanimationKeyframe.Get();
		std::string animationKeyframeId(animationKeyframe.GetId());
		BE_ASSERT(animationKeyframeId != "");
		std::atomic<int> taskFinishedCounter = 0;
		// add characters
		if (true)
		{
			for (int k = 0; k < 1000; ++k)
			{
				auto animationKeyframeInfoPtr = animationKeyframe.AddAnimationKeyframeInfo("obj1");
				BE_ASSERT((bool)animationKeyframeInfoPtr);
				if (animationKeyframeInfoPtr)
				{
					GetTaskManager().AddTask([animationKeyframeInfoPtr, k, animationKeyframeId, &taskFinishedCounter]() {
						auto lock(animationKeyframeInfoPtr->GetAutoLock());
						auto& animationKeyframeInfo = lock.Get();
						animationKeyframeInfo.SetType("baked");
						animationKeyframeInfo.SetKeyframeInterval(0.6f);
						animationKeyframeInfo.SetStartTime(0.0f);
						animationKeyframeInfo.SetKeyframeCount(60 * 4);
						animationKeyframeInfo.SetChunkSize(60);
						std::vector<std::string> tags = { "character" };
						animationKeyframeInfo.SetTags(tags);
						animationKeyframeInfo.AsyncSave(GetDefaultHttp(),
							[animationKeyframeInfoPtr, k, animationKeyframeId, &taskFinishedCounter](const IAnimationKeyframeInfo::Id animationKeyframeInfoId)
							{
								auto lock(animationKeyframeInfoPtr->GetAutoLock());
								auto& animationKeyframeInfo = lock.Get();
								BE_ASSERT(animationKeyframeInfoId != "");
								for (int j = 0; j < 4; ++j)
								{
									auto chunkPtr = animationKeyframeInfo.CreateChunk();
									BE_ASSERT((bool)chunkPtr);
									if (chunkPtr)
									{
										GetTaskManager().AddTask([chunkPtr, j, k, animationKeyframeId, animationKeyframeInfoId, &taskFinishedCounter]() {
											auto lockChunk(chunkPtr->GetAutoLock());
											auto& chunck = lockChunk.Get();
											std::vector<float> translations; translations.reserve(3 * 60);
											std::vector<float> quats; quats.reserve(4 * 60);
											FQuat quat;
											for (unsigned i = 0; i < 60; ++i)
											{
												translations.push_back(g_basedPos.X + float(i + j * 60)); // in meters
												translations.push_back(g_basedPos.Y + k);
												translations.push_back(g_basedPos.Z);
												FVector v(0, 0, i * 6);
												quat = quat.MakeFromEuler(v);
												quats.push_back(quat.X);
												quats.push_back(quat.Y);
												quats.push_back(quat.Z);
												quats.push_back(quat.W);
											}

											chunck.SetTranslations(translations);
											chunck.SetQuaternions(quats);
											chunck.AsyncSave(GetDefaultHttp(), (const std::string&)animationKeyframeId, (const std::string&)animationKeyframeInfoId, [&taskFinishedCounter](long) {
												taskFinishedCounter++;
												});

											}, ITaskManager::EType::background);
									}
								}
							});
						});
				}
			}
		while (taskFinishedCounter != 4000)
			Sleep(10);
		}

		taskFinishedCounter = 0;
		// add vehicles
		{
			for (int k = 0; k < 1000; ++k)
			{
				auto animationKeyframeInfoPtr = animationKeyframe.AddAnimationKeyframeInfo("obj2");
				BE_ASSERT((bool)animationKeyframeInfoPtr);
				if (animationKeyframeInfoPtr)
				{
					GetTaskManager().AddTask([animationKeyframeInfoPtr, k, animationKeyframeId, &taskFinishedCounter]() {
						auto lock(animationKeyframeInfoPtr->GetAutoLock());
						auto& animationKeyframeInfo = lock.Get();
						animationKeyframeInfo.SetType("baked");
						animationKeyframeInfo.SetKeyframeInterval(0.03333f);
						animationKeyframeInfo.SetStartTime(0.0f);
						animationKeyframeInfo.SetKeyframeCount(60 * 60);
						animationKeyframeInfo.SetChunkSize(60);
						std::vector<std::string> tags = { "car" };
						animationKeyframeInfo.SetTags(tags);
						animationKeyframeInfo.AsyncSave(GetDefaultHttp(),
							[animationKeyframeInfoPtr, k, animationKeyframeId, &taskFinishedCounter](const IAnimationKeyframeInfo::Id& animationKeyframeInfoId)
							{
								auto lock(animationKeyframeInfoPtr->GetAutoLock());
								auto& animationKeyframeInfo = lock.Get();
								BE_ASSERT(animationKeyframeInfoId != "");
								float directionAngle = 2.0f * 3.1415926535898f / 1000.f;
								float sinDir = sin(directionAngle * k);
								float cosDir = cos(directionAngle * k);
								for (int j = 0; j < 60; ++j)
								{
									auto chunkPtr = animationKeyframeInfo.CreateChunk();
									BE_ASSERT((bool)chunkPtr);
									if (chunkPtr)
										GetTaskManager().AddTask([chunkPtr, j, k, cosDir, sinDir, animationKeyframeId, animationKeyframeInfoId, &taskFinishedCounter]() 
										{
											auto lockChunk(chunkPtr->GetAutoLock());
											auto& chunck = lockChunk.Get();
											std::vector<float> translations; translations.reserve(3 * 60);
											std::vector<float> quats; quats.reserve(4 * 60);
											FQuat quat;
											const float distStep = 60.0f /*km/h speed*/ * 0.277777778 /*km/h => m/s*/ * 0.03333f /*s frame delta time */; //in meters
											for (unsigned i = 0; i < 60; ++i)
											{
												translations.push_back(g_basedPos.X + float(i + j * 60) * distStep * cosDir); // in meters
												translations.push_back(g_basedPos.Y + 5.f + float(i + j * 60) * distStep * sinDir); // in meters
												translations.push_back(g_basedPos.Z + 2.0f); // in meters
												FVector v(0, 0, i * 6);
												quat = quat.MakeFromEuler(v);
												quats.push_back(quat.X);
												quats.push_back(quat.Y);
												quats.push_back(quat.Z);
												quats.push_back(quat.W);
											}

											chunck.SetTranslations(translations);
											chunck.SetQuaternions(quats);
											chunck.AsyncSave(GetDefaultHttp(), (const std::string&)animationKeyframeId, (const std::string&)animationKeyframeInfoId, [&taskFinishedCounter](long) {
												taskFinishedCounter++;
												});
										}, ITaskManager::EType::background);
								}
							});
						});
				}
			}
		}
		while (taskFinishedCounter != 60000)
		{
			//FHttpModule::Get().GetHttpManager().Flush(EHttpFlushReason::Default);
			FPlatformProcess::Sleep(0.001);
		}

		animationKeyframe.Save(AdvViz::SDK::GetDefaultHttp());
		animationKeyframes.insert(std::make_pair(animationKeyframe.GetId(), animationKeyframePtr));
	}
#endif
}

bool FDecorationAsyncIOHelper::LoadAnimationKeyframesFromServer()
{
	using namespace AdvViz::SDK;
	std::string itwinid = TCHAR_TO_UTF8(*(LoadedITwinId));
	auto animationKeyframesVec = GetITwinAnimationKeyframes(itwinid);
	for (auto& it : animationKeyframesVec)
	{
		auto lock(it->GetAutoLock());
		IAnimationKeyframe& p = lock.Get();
		p.LoadAnimationKeyFrameInfos();
		animationKeyframes.insert(std::make_pair(p.GetId(), it));
	}

	if (!animationKeyframes.empty() && !LoadITwinDecoration()) //we need a decoration
		SaveDecorationToServer();

#if 0 //D-O-NOTC
// Temporary
// create animation test
	if (animationKeyframes.empty())
		GeneratePaths(itwinid, animationKeyframes);
#endif

	return true;
}

bool FDecorationAsyncIOHelper::LoadCustomMaterials(
	TMap<FString, TWeakObjectPtr<AITwinIModel>> const& idToIModel,
	std::set<std::string> const& specificModels /*= {}*/)
{
	if (!materialPersistenceMngr)
	{
		ensureMsgf(false, TEXT("InitDecorationService must be called before, in game thread"));
		return false;
	}

	for (auto const& [strIModelId, _] : idToIModel)
	{
		std::string const iModelId = TCHAR_TO_UTF8(*strIModelId);

		materialPersistenceMngr->SetLoadedModel(iModelId, false);
	}

	// Load material customizations from the Decoration Service
	if (!LoadITwinDecoration())
	{
		return false;
	}

	materialPersistenceMngr->LoadDataFromServer(decoration->GetId(), specificModels);

	std::map<std::string, AITwinIModel::GltfMaterialHelperPtr> imodelToMatHelper;
	for (auto const& [strIModelId, pImodel] : idToIModel)
	{
		if (pImodel.IsValid())
		{
			imodelToMatHelper.emplace(TCHAR_TO_UTF8(*strIModelId), pImodel->GetGltfMaterialHelper());
		}
	}

	if (!ITwin::ResolveDecorationTextures(*materialPersistenceMngr,
		materialPersistenceMngr->GetDecorationTexturesByIModel(),
		materialPersistenceMngr->GetTextureUsageMap(),
		imodelToMatHelper))
	{
		return false;
	}

	// Mark iModels just loaded in the manager, now that the *whole* process (including texture resolution)
	// is done.
	for (auto const& [iModelId, _] : imodelToMatHelper)
	{
		materialPersistenceMngr->SetLoadedModel(iModelId, true);
	}
	return true;
}

namespace Detail
{

inline CesiumAsync::HttpHeaders GetHeadersForSource(AdvViz::SDK::ETextureSource TexSource)
{
	if (TexSource == AdvViz::SDK::ETextureSource::Decoration)
	{
		return {
			{
				"Authorization",
				std::string("Bearer ") + *AdvViz::SDK::GetDefaultHttp()->GetAccessToken()
			}
		};
	}
	else
	{
		// No extra headers required for local textures
		return {};
	}
}

// Scoped W-Lock doing nothing if a lock is provided from outside
struct [[nodiscard]] OptionalWLock
{
	OptionalWLock(BeUtils::GltfMaterialHelper& MatHelper, BeUtils::WLock const* pLock = nullptr)
		: ExternalLock(pLock)
	{
		if (ExternalLock)
		{
			BE_ASSERT(ExternalLock->mutex() == &MatHelper.GetMutex());
		}
		else
		{
			LocalLock.emplace(MatHelper.GetMutex());
		}
	}

	BeUtils::WLock const& GetLock() const {
		BE_ASSERT(ExternalLock || LocalLock);
		return ExternalLock ? *ExternalLock : *LocalLock;
	}

	BeUtils::WLock const* const ExternalLock = nullptr;
	std::optional<BeUtils::WLock> LocalLock;
};


void ResolveTexturesMatchingSource(
	AdvViz::SDK::ETextureSource TexSource,
	AdvViz::SDK::MaterialPersistenceManager& matPersistenceMngr,
	AdvViz::SDK::PerIModelTextureSet const& perModelTextures,
	AdvViz::SDK::TextureUsageMap const& textureUsageMap,
	std::map<std::string, AITwinIModel::GltfMaterialHelperPtr> const& imodelToMatHelper,
	BeUtils::WLock const* pLock = nullptr)
{
	BE_ASSERT(TexSource == AdvViz::SDK::ETextureSource::Decoration
		|| TexSource == AdvViz::SDK::ETextureSource::Library);

	// Download decoration textures if needed.

	CesiumGltfReader::GltfReaderResult gltfResult;
	auto& model = gltfResult.model.emplace();
	auto& images = model.images;
	images.reserve(perModelTextures.size() * 5);

	struct LoadedImageInfo
	{
		size_t imgIndex = 0;
		AdvViz::SDK::TextureKey texKey;
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
		auto itMatHelper = imodelToMatHelper.find(imodelid);
		if (itMatHelper == imodelToMatHelper.end())
			continue;
		auto glTFMatHelper = itMatHelper->second;
		if (!glTFMatHelper)
			continue;

		IModelImageVec& imodelImgs = imageCorresp.emplace_back();
		imodelImgs.matHelper = glTFMatHelper;
		imodelImgs.imageInfos.reserve(textureSet.size());

		// Download (or read from sqlite cache) all decoration textures used by this model
		for (auto const& texKey : textureSet)
		{
			if (texKey.eSource == TexSource)
			{
				imodelImgs.imageInfos.push_back({ gltfImageIndex, texKey });
				auto& gltfImage = images.emplace_back();
				gltfImageIndex++;
				gltfImage.uri = matPersistenceMngr.GetRelativeURL(texKey);
			}
		}
	}

	if (gltfImageIndex == 0)
	{
		// Nothing to do.
		return;
	}

	// Actually download textures. Note that we use Cesium's sqlite caching system, so this should be fast
	// except for the very first time).
	const std::shared_ptr<CesiumAsync::IAssetAccessor>& pAssetAccessor = getAssetAccessor();
	const CesiumAsync::AsyncSystem& asyncSystem = getAsyncSystem();

	std::string const baseUrl = matPersistenceMngr.GetBaseURL(TexSource);

	// We restrict the formats to JPG and PNG, so we can leave the default options (no need to setup
	// Ktx2TranscodeTargets...)
	CesiumGltfReader::GltfReaderOptions gltfOptions;
	CesiumGltfReader::GltfReader::resolveExternalData(
		asyncSystem,
		baseUrl,
		GetHeadersForSource(TexSource),
		pAssetAccessor,
		gltfOptions,
		std::move(gltfResult))
		.thenImmediately([imageCorresp, pLock, &textureUsageMap](CesiumGltfReader::GltfReaderResult&& result)
	{
		auto& cesiumImages = result.model->images;
		// Dispatch the downloaded images to the appropriate material helper
		for (IModelImageVec const& imodelImgs : imageCorresp)
		{
			OptionalWLock OptLock(*imodelImgs.matHelper, pLock);
			auto const& lock = OptLock.GetLock();
			for (LoadedImageInfo const& info : imodelImgs.imageInfos)
			{
				imodelImgs.matHelper->StoreCesiumImage(info.texKey,
					std::move(cesiumImages[info.imgIndex]),
					textureUsageMap,
					lock);
			}
		}
	}).wait();
}

void ResolveTexturesLocatedOnDisk(
	std::unordered_map<AdvViz::SDK::TextureKey, std::string> const& LocalTextures,
	AdvViz::SDK::TextureUsageMap const& textureUsageMap,
	AITwinIModel::GltfMaterialHelperPtr gltfMatHelper,
	std::filesystem::path const& textureDir,
	BeUtils::WLock const* pLock = nullptr)
{
	// Remark: following merge with Cesium 2.14.1, we no longer use #resolveExternalData for local textures
	// (using the file:/// protocol): it does not work at all in packaged version...
	std::vector<CesiumGltf::Image> cesiumImages;
	cesiumImages.resize(LocalTextures.size());
	size_t imgIndex = 0;
	TArray64<uint8> Buffer;
	std::vector<std::byte> cesiumBuffer;
	for (auto const& [_, basename] : LocalTextures)
	{
		auto const StrFSPath = (textureDir / basename).generic_string();
		FString const TexturePath = UTF8_TO_TCHAR(StrFSPath.c_str());
		Buffer.Empty();
		cesiumBuffer.clear();
		if (FFileHelper::LoadFileToArray(Buffer, *TexturePath))
		{
			std::byte const* srcAsByte = reinterpret_cast<std::byte const*>(Buffer.GetData());
			cesiumBuffer.insert(cesiumBuffer.end(), srcAsByte, srcAsByte + Buffer.Num());

			auto loadResult = BeUtils::GltfMaterialTuner::LoadImageCesium(cesiumImages[imgIndex],
				cesiumBuffer, basename);
			if (!loadResult)
			{
				BE_LOGE("ITwinDecoration", "Could not load Cesium image '" << basename
					<< "' - error: " << loadResult.error().message);
			}
		}
		imgIndex++;
	}
	// Dispatch the read images.
	{
		OptionalWLock OptLock(*gltfMatHelper, pLock);
		auto const& lock = OptLock.GetLock();

		imgIndex = 0;

		// for custom material library, we will also store the full path of textures so that the image widget
		// can handle them without having to make a special case (see #UImageWidgetImpl).
		const bool bStoreLocalPaths = textureDir.empty();
		std::optional<std::filesystem::path> pathOnDiskOpt;

		for (auto const& [texKey, texPath] : LocalTextures)
		{
			if (bStoreLocalPaths) // in this context, the paths are absolute
				pathOnDiskOpt = texPath;
			gltfMatHelper->StoreCesiumImage(texKey,
				std::move(cesiumImages[imgIndex]),
				textureUsageMap,
				lock,
				std::nullopt,
				pathOnDiskOpt);
			imgIndex++;
		}
	}
}

}

bool ITwin::ResolveDecorationTextures(
	AdvViz::SDK::MaterialPersistenceManager& matPersistenceMngr,
	AdvViz::SDK::PerIModelTextureSet const& perModelTextures,
	AdvViz::SDK::TextureUsageMap const& textureUsageMap,
	std::map<std::string, AITwinIModel::GltfMaterialHelperPtr> const& imodelToMatHelper,
	bool bResolveLocalDiskTextures /*= false*/,
	BeUtils::WLock const* pLock /*= nullptr*/)
{
	// Following merge with cesium-unreal v2.14.1, we need to provide a valid base URL to
	// #resolveExternalData, so as a quick fix I am now downloading Decoration and Library textures
	// separately (and will ask cesium team whether the new behavior of
	// Uri::resolve("", "https://toto.com", true) should really be just "toto.com" as it is now...)
	Detail::ResolveTexturesMatchingSource(AdvViz::SDK::ETextureSource::Decoration,
		matPersistenceMngr,
		perModelTextures, textureUsageMap,
		imodelToMatHelper, pLock);

	// For the Material Library (local files which are packaged in Carrot context), we no longer use
	// #resolveExternalData - also since v2.14.1, which broke this case as well, but only in packaged mode.
	FString const MaterialLibraryPath = FITwinMaterialLibrary::GetBentleyLibraryPath();
	std::filesystem::path const MatLibraryDir = TCHAR_TO_UTF8(*MaterialLibraryPath);
	for (auto const& [imodelid, textureSet] : perModelTextures)
	{
		auto itMatHelper = imodelToMatHelper.find(imodelid);
		if (itMatHelper == imodelToMatHelper.end())
			continue;
		auto glTFMatHelper = itMatHelper->second;
		if (!glTFMatHelper)
			continue;
		std::unordered_map<AdvViz::SDK::TextureKey, std::string> MatLibraryTexMap;
		std::unordered_map<AdvViz::SDK::TextureKey, std::string> LocalDiskTexMap;
		for (auto const& texKey : textureSet)
		{
			if (texKey.eSource == AdvViz::SDK::ETextureSource::Library)
			{
				MatLibraryTexMap.emplace(texKey, texKey.id);
			}
			else if (bResolveLocalDiskTextures
				&& texKey.eSource == AdvViz::SDK::ETextureSource::LocalDisk)
			{
				ensure(std::filesystem::path(texKey.id).is_absolute());
				LocalDiskTexMap.emplace(texKey, texKey.id);
			}
		}
		Detail::ResolveTexturesLocatedOnDisk(MatLibraryTexMap, textureUsageMap, glTFMatHelper,
			MatLibraryDir, pLock);

		if (bResolveLocalDiskTextures)
		{
			Detail::ResolveTexturesLocatedOnDisk(LocalDiskTexMap, textureUsageMap, glTFMatHelper,
				{}, pLock);
		}
	}
	return true;
}

UTexture2D* ITwin::ResolveAsUnrealTexture(
	BeUtils::GltfMaterialHelper& GltfMatHelper,
	std::string const& TextureId,
	AdvViz::SDK::ETextureSource eSource)
{
	TArray64<uint8> Buffer;
	std::string strError;
	BeUtils::RLock lock(GltfMatHelper.GetMutex());
	if (!LoadTextureBuffer(AdvViz::SDK::TextureKey{ TextureId, eSource },
		GltfMatHelper, lock, Buffer, strError))
	{
		BE_LOGE("ITwinDecoration", "Could not load buffer for texture " << TextureId << ": " << strError);
		return nullptr;
	}
	return FImageUtils::ImportBufferAsTexture2D(Buffer);
}

void ITwin::ResolveITwinTextures(
	std::unordered_map<AdvViz::SDK::TextureKey, std::string> const& iTwinTextures,
	AdvViz::SDK::TextureUsageMap const& textureUsageMap,
	AITwinIModel::GltfMaterialHelperPtr GltfMatHelper,
	std::filesystem::path const& textureDir)
{
	Detail::ResolveTexturesLocatedOnDisk(iTwinTextures, textureUsageMap, GltfMatHelper, textureDir);
}

bool FDecorationAsyncIOHelper::SaveDecorationToServer()
{
	bool const saveInstances = instancesManager_	&& instancesManager_->HasInstancesToSave();
	bool const saveMaterials = materialPersistenceMngr && materialPersistenceMngr->NeedUpdateDB();
	bool const saveSplines = splinesManager && splinesManager->HasSplinesToSave();
	bool const saveAnnotations = annotationsManager && annotationsManager->HasAnnotationToSave();
	bool const saveAnimPaths = pathAnimator && pathAnimator->HasAnimPathsToSave();
	if (!saveInstances && !saveMaterials && !saveSplines && !saveAnnotations && !saveAnimPaths)
	{
		return false;
	}
	if (LoadedITwinId.IsEmpty() || !decoration)
	{
		return false;
	}
	std::string const itwinid = TCHAR_TO_UTF8(*(LoadedITwinId));

	if (decoration->GetId().empty())
	{
		decoration->Create("Decoration", itwinid);
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

		// Splines should now be saved *before* instances, as some instance groups may reference the spline
		// they were created from, and thus they need to know their identifier on the server to save the
		// information correctly.
		// For the same reason, animation paths should be saved *after* the splines and *before* the instances.
		if (saveSplines)
			splinesManager->SaveDataOnServer(decoration->GetId());
		if (saveAnimPaths)
			pathAnimator->SaveDataOnServer(decoration->GetId());
		if (saveInstances)
			instancesManager_->SaveDataOnServer(decoration->GetId());
		if (saveMaterials)
			materialPersistenceMngr->SaveDataOnServer(decoration->GetId());
		if (saveAnnotations)
			annotationsManager->SaveDataOnServerDS(decoration->GetId());
		return true;
	}
	return false;
}

bool FDecorationAsyncIOHelper::LoadSceneFromServer()
{
	if (!scene)
	{
		ensureMsgf(false, TEXT("InitDecorationService must be called before, in game thread"));
		return false;
	}
	if (LoadedITwinId.IsEmpty())
		return false;

	std::string const itwinid = TCHAR_TO_UTF8(*(LoadedITwinId));

	if (scene
		&& !scene->GetId().empty()
		&& scene->GetITwinId() == itwinid)
	{
		// scene already loaded for current iTwin => nothing to do.
		return true;
	}
	using namespace AdvViz::SDK;
	if (!bUseDecorationService)
	{
		auto scenes2res = GetITwinScenesAPI(itwinid);
		if (!scenes2res)
		{
			int status = scenes2res.error();
			if (status == 404 || status == 400)
			{
				//FMessageDialog::Open(EAppMsgCategory::Error, EAppMsgType::Ok,
				//	FText::FromString("You don't seem to have access to scene API for this ITwin. You will not be able to save the scene."),
				//	FText::FromString(""));
				BE_LOGE("ITwinScene", "No access to empty scene, Create empty scene");
				scene->PrepareCreation(defaultSceneName, itwinid);
				scene->SetTimeline(std::shared_ptr<AdvViz::SDK::ITimeline>(AdvViz::SDK::ITimeline::New()));
				return false;
			}
		}
		else
		{
			std::vector<std::shared_ptr<IScenePersistence>> scenes2 = *scenes2res;
			if (scenes2.empty())
			{
				if (!LoadedSceneID.IsEmpty())
				{
					std::string const sceneid = TCHAR_TO_UTF8(*(LoadedSceneID));
					if (bSceneIdIsForNewScene)
					{
						scene->PrepareCreation(sceneid, itwinid);
					}
					else
					{
						//FMessageDialog::Open(EAppMsgCategory::Error, EAppMsgType::Ok,
						//	FText::FromString("Cannot find scene with ID " + LoadedSceneID + ", Create empty scene"),
						//	FText::FromString(""));
						BE_LOGE("ITwinScene", "Cannot find scene with ID " + sceneid + ", Create empty scene");

						scene->PrepareCreation(defaultSceneName, itwinid);
					}
					scene->SetTimeline(std::shared_ptr<AdvViz::SDK::ITimeline>(AdvViz::SDK::ITimeline::New()));
				}
				else
				{
					std::vector<std::shared_ptr<IScenePersistence>> scenes = GetITwinScenesDS(itwinid);
					if (!scenes.empty())
					{
						bool sceneInited = false;
						for (auto s : scenes)
						{
							if (s->GetName() != "sub scene")
							{
								if (FMessageDialog::Open(EAppMsgCategory::Error, EAppMsgType::YesNo,
									FText::FromString("You have a scene in Decoration Service and no scene in SceneAPI , would you like to transfer it to Scene API service? "),
									FText::FromString("")) != EAppReturnType::Yes)
								{
									scene->PrepareCreation(defaultSceneName, itwinid);
									scene->SetTimeline(std::shared_ptr<AdvViz::SDK::ITimeline>(AdvViz::SDK::ITimeline::New()));
								}
								else
								{
									scene->PrepareCreation(defaultSceneName, itwinid);
									scene->SetAtmosphere(s->GetAtmosphere());
									scene->SetSceneSettings(s->GetSceneSettings());
									for (auto link : s->GetLinks())
									{
										auto nulink = scene->MakeLink();
										nulink->SetType(link->GetType());
										nulink->SetRef(link->GetRef());
										if (link->HasVisibility())
											nulink->SetVisibility(link->GetVisibility());
										if (link->HasGCS())
										{
											auto gcs = link->GetGCS();
											nulink->SetGCS(gcs.first, gcs.second);
										}
										if (link->HasQuality())
											nulink->SetQuality(link->GetQuality());
										if (link->HasTransform())
											nulink->SetTransform(link->GetTransform());
										if (link->HasName())
											nulink->SetName(link->GetName());
										scene->AddLink(nulink);
									}
									auto tmInfo = GetSceneTimelines(s->GetId());
									if (tmInfo && !(*tmInfo).empty())
									{
										auto timeline = std::shared_ptr<AdvViz::SDK::ITimeline>(AdvViz::SDK::ITimeline::New());
										auto timelineId = (*tmInfo)[0].id;
										auto ret = timeline->Load(s->GetId(), timelineId);
										if (!ret)
										{
											BE_LOGE("Timeline", "Load failed, id:" << (std::string&)timelineId << " error:" << ret.error());
											timeline = std::shared_ptr<AdvViz::SDK::ITimeline>(AdvViz::SDK::ITimeline::New());
										}
										//remove ids 
										for (size_t i(0); i < timeline->GetClipCount(); i++)
										{
											auto clipp = timeline->GetClipByIndex(i);
											if (!clipp)
												continue;
											(*clipp)->SetId(ITimelineClip::Id(std::string()));
										}
										scene->SetTimeline(timeline);
									}
									else
									{
										scene->SetTimeline(std::shared_ptr<AdvViz::SDK::ITimeline>(AdvViz::SDK::ITimeline::New()));
									}

									PostLoadSceneFromServer();
								}
								sceneInited = true;
								break;

							}
						}
						if (!sceneInited)
						{
							scene->PrepareCreation(defaultSceneName, itwinid);
							scene->SetTimeline(std::shared_ptr<AdvViz::SDK::ITimeline>(AdvViz::SDK::ITimeline::New()));
						}
						else
						{
							return true;
						}

					}
					else
					{
						scene->PrepareCreation(defaultSceneName, itwinid);
						scene->SetTimeline(std::shared_ptr<AdvViz::SDK::ITimeline>(AdvViz::SDK::ITimeline::New()));
					}
				}
				return false;
			}
			else
			{
				if (!LoadedSceneID.IsEmpty())
				{
					std::string const sceneid = TCHAR_TO_UTF8(*(LoadedSceneID));
					if (bSceneIdIsForNewScene)
					{
						scene->PrepareCreation(sceneid, itwinid);
						scene->SetTimeline(std::shared_ptr<AdvViz::SDK::ITimeline>(AdvViz::SDK::ITimeline::New()));
						return false;
					}
					else
					{
						bool found = false;
						for (auto scen : scenes2)
						{
							if (scen->GetId() == sceneid)
							{
								scene = scen;
								found = true; 
								break;
							}
						}
						if (!found)
						{
							FMessageDialog::Open(EAppMsgCategory::Error, EAppMsgType::Ok,
								FText::FromString("Cannot find scene with ID " + LoadedSceneID + ", first scene found loaded"),
								FText::FromString(""));
							BE_LOGE("ITwinScene", "Cannot find scene with ID " + sceneid + ", first scene found loaded");
							scene = scenes2[0];
						}

					}

				}
				else
				{
					bool found = false;
					//take default scene and not a dev scene by default
					for (auto sc : scenes2)
					{
						if (sc->GetName() == defaultSceneName)
						{
							scene =sc;
							found = true;
						}
					}
					if(!found)
						scene = scenes2[0];
				}
				PostLoadSceneFromServer();
			}
		}
	}
	if (bUseDecorationService)
	{
		std::vector<std::shared_ptr<IScenePersistence>> scenes = GetITwinScenesDS(itwinid);
		if (scenes.empty())
		{
			if (!LoadedSceneID.IsEmpty())
			{
				std::string const sceneid = TCHAR_TO_UTF8(*(LoadedSceneID));
				if (bSceneIdIsForNewScene)
				{
					scene->PrepareCreation(sceneid, itwinid);
				}
				else
				{
					/*FMessageDialog::Open(EAppMsgCategory::Error, EAppMsgType::Ok,
						FText::FromString("Cannot find scene with ID " + LoadedSceneID + ", Create empty scene"),
						FText::FromString(""));*/
					BE_LOGE("ITwinScene", "Cannot find scene with ID " + sceneid + ", Create empty scene");
						scene->PrepareCreation(defaultSceneName, itwinid);
				}
				scene->SetTimeline(std::shared_ptr<AdvViz::SDK::ITimeline>(AdvViz::SDK::ITimeline::New()));
			}
			else
			{

				scene->PrepareCreation(defaultSceneName, itwinid);
				scene->SetTimeline(std::shared_ptr<AdvViz::SDK::ITimeline>(AdvViz::SDK::ITimeline::New()));
			}
			return false;
		}
		else
		{
			if (!LoadedSceneID.IsEmpty())
			{
				std::string const sceneid = TCHAR_TO_UTF8(*(LoadedSceneID));
				if (bSceneIdIsForNewScene)
				{
					scene->PrepareCreation(sceneid, itwinid);
					scene->SetTimeline(std::shared_ptr<AdvViz::SDK::ITimeline>(AdvViz::SDK::ITimeline::New()));
					return false;
				}
				else
				{
					bool found = false;
					for (auto scen : scenes)
					{
						if (scen->GetId() == sceneid)
						{
							scene = scen;
							found = true;
							break;
						}
					}
					if (!found)
					{
						FMessageDialog::Open(EAppMsgCategory::Error, EAppMsgType::Ok,
							FText::FromString("Cannot find scene with ID " + LoadedSceneID + ", first scene found loaded"),
							FText::FromString(""));
						BE_LOGE("ITwinScene", "Cannot find scene with ID " + sceneid + ", first scene found loaded");
						scene = scenes[0];
					}
				}
			}
			else
			{
				bool found = false;
				//take default scene and not a dev scene by default
				for (auto sc : scenes)
				{
					if (sc->GetName() == defaultSceneName)
					{
						scene = sc;
						found = true;
					}
				}
				if (!found)
					scene = scenes[0];
			}
				
			std::string sceneId = scene->GetId();
			PostLoadSceneFromServer();
			// Load or create timeline
			auto tmInfo = GetSceneTimelines(sceneId);
			if (tmInfo && !(*tmInfo).empty())
			{
				auto timeline = std::shared_ptr<AdvViz::SDK::ITimeline>(AdvViz::SDK::ITimeline::New());
				auto timelineId = (*tmInfo)[0].id;
				auto ret = timeline->Load(sceneId, timelineId);
				if (!ret)
				{
					BE_LOGE("Timeline", "Load failed, id:" << (std::string&)timelineId << " error:" << ret.error());
				}
				else
				{
					scene->SetTimeline(timeline);
				}
			}
			if (!scene->GetTimeline())
			{
				std::string sceneName = "myscene";
				auto ret = AdvViz::SDK::AddSceneTimeline(sceneId, sceneName);
				if (!ret)
				{
					BE_LOGE("Timeline", "AddSceneTimeline failed, error:" << ret.error());
				}
				else
				{
					auto timeline = std::shared_ptr<AdvViz::SDK::ITimeline>(AdvViz::SDK::ITimeline::New());
					auto timelineId = *ret;
					auto ret2 = timeline->Load(sceneId, timelineId);
					if (!ret2)
					{
						BE_LOGE("Timeline", "Load failed, id:" << (std::string&)timelineId << "error:" << ret2.error());
					}
					else
					{
						scene->SetTimeline(timeline);
					}
				}
			}
		}
	}
	if (!scene->GetTimeline())
		scene->SetTimeline(std::shared_ptr<AdvViz::SDK::ITimeline>(AdvViz::SDK::ITimeline::New()));
	return true;
}

void FDecorationAsyncIOHelper::PostLoadSceneFromServer()
{
	links.clear();
	for (auto l : scene->GetLinks())
	{
		ITwin::ModelLink key;
		if (!ITwin::GetModelType(l->GetType(), key.first))
			continue;
		key.second = UTF8_TO_TCHAR(l->GetRef().c_str());
		links[key] = l;
		
	}
}

bool FDecorationAsyncIOHelper::SaveSceneToServer()
{
	if (LoadedITwinId.IsEmpty() || !scene)
	{
		return false;
	}
	std::string const itwinid = TCHAR_TO_UTF8(*(LoadedITwinId));

	if (!decorationIsLinked && decoration && !decoration->GetId().empty())
	{
		auto nulink = scene->MakeLink();
		nulink->SetType("decoration");
		nulink->SetRef(decoration->GetId());
		scene->AddLink(nulink);
		decorationIsLinked = true;
	}
	if (scene->ShouldSave()|| scene->GetId().empty())
	{
		scene->SetShouldSave(true);
		if (!scene->Save())
		{
			FMessageDialog::Open(EAppMsgCategory::Error, EAppMsgType::Ok,
				FText::FromString("The Scene failed to save"),
				FText::FromString(""));
			BE_LOGE("ITwinScene", "The Scene failed to save");
		}
		const auto count = std::erase_if(links, [](const auto& item) {
			auto const& [key, value] = item;
			return value->GetId().empty() && value->ShouldDelete();
			});
	}

	// save timeline
	if (bUseDecorationService)
	{
		if (scene->GetTimeline())
		{
			scene->GetTimeline()->Save(scene->GetId());
		}
	}

	return true;
}

bool FDecorationAsyncIOHelper::LoadAnnotationsFromServer()
{
	if (!annotationsManager)
	{
		ensureMsgf(false, TEXT("InitDecorationService must be called before, in game thread"));
		return false;
	}
	if (!LoadITwinDecoration())
	{
		return false;
	}
	annotationsManager->LoadDataFromServerDS(decoration->GetId());
	return true;

}

bool FDecorationAsyncIOHelper::LoadSplinesFromServer()
{
	if (!splinesManager)
	{
		ensureMsgf(false, TEXT("InitDecorationService must be called before, in game thread"));
		return false;
	}
	if (!LoadITwinDecoration())
	{
		return false;
	}
	splinesManager->LoadDataFromServer(decoration->GetId());
	return true;
}

bool FDecorationAsyncIOHelper::LoadPathAnimationFromServer()
{
	if (!pathAnimator)
	{
		ensureMsgf(false, TEXT("InitDecorationService must be called before, in game thread"));
		return false;
	}
	if (!LoadITwinDecoration())
	{
		return false;
	}
	pathAnimator->LoadDataFromServer(decoration->GetId());
	return true;
}

FDecorationAsyncIOHelper::LinkSharedPtr FDecorationAsyncIOHelper::CreateLink(ModelIdentifier const& Key)
{
	auto iter = links.find(Key);
	if (iter == links.end())
	{
		auto link = scene->MakeLink();
		link->SetType(ITwin::ModelTypeToString(Key.first));
		link->SetRef(TCHAR_TO_UTF8(*Key.second));
		scene->AddLink(link);
		links[Key] = link;
		return link;
	}
	else
	{
		return iter->second;
	}
}

std::shared_ptr<AdvViz::SDK::ISplinesManager> const& FDecorationAsyncIOHelper::GetSplinesManager()
{
	return splinesManager;
}

std::shared_ptr<AdvViz::SDK::IPathAnimator> const& FDecorationAsyncIOHelper::GetPathAnimator()
{
	return pathAnimator;
}

AdvViz::expected<std::vector<std::shared_ptr<AdvViz::SDK::IScenePersistence>>, int> FDecorationAsyncIOHelper::GetITwinScenes(const FString& iTwinid)
{

	std::string itwinid = TCHAR_TO_UTF8(*(iTwinid));
	if (bUseDecorationService)
	{
		return AdvViz::SDK::GetITwinScenesDS(itwinid);
	}
	else
	{
		return AdvViz::SDK::GetITwinScenesAPI(itwinid);
	}
}


void FDecorationAsyncIOHelper::RegisterWaitableLoadEvent(WaitableLoadEventUPtr&& LoadEventPtr)
{
	BeUtils::WLock WLock(WaitableLoadEventsMutex);
	WaitableLoadEvents.emplace_back(std::move(LoadEventPtr));
}

bool FDecorationAsyncIOHelper::ShouldWaitForLoadEvent(bool bLogInfo /*= false*/) const
{
	BeUtils::RLock RLock(WaitableLoadEventsMutex);
	for (auto const& EventPtr : WaitableLoadEvents)
	{
		if (EventPtr->ShouldWait())
		{
			if (bLogInfo)
			{
				BE_LOGI("ITwinDecoration", "Waiting for load event: " << EventPtr->Describe() << "...");
			}
			return true;
		}
	}
	return false;
}

void FDecorationAsyncIOHelper::ResetWaitableLoadEvents()
{
	BeUtils::WLock WLock(WaitableLoadEventsMutex);
	WaitableLoadEvents.clear();
}

void FDecorationAsyncIOHelper::WaitForExternalLoadEvents(int MaxSecondsToWait)
{
	int32 ElapsedSec = 0;
	while (ShouldWaitForLoadEvent() && ElapsedSec < MaxSecondsToWait)
	{
		FPlatformProcess::Sleep(1.f);
		ElapsedSec++;
	}
	if (ShouldWaitForLoadEvent(/*bLogInfo*/true))
	{
		BE_LOGW("ITwinDecoration", "Additional requests taking more than "
			<< MaxSecondsToWait << " seconds - continue scene loading");
	}
	ResetWaitableLoadEvents();
}
