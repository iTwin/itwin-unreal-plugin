/*--------------------------------------------------------------------------------------+
|
|     $Source: IModelRenderTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS && WITH_EDITOR

#include <CesiumRuntime.h>
#include <ITwinIModel.h>
#include <ITwinWebServices/ITwinAuthorizationManager.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <ITwinWebServices/ITwinWebServices_Info.h>
#include <Decoration/ITwinDecorationHelper.h>
#include <Decoration/ITwinDecorationServiceSettings.h>

#include <Tests/ITwinFunctionalTest.h>
#include <Tests/ITwinFileBasedMockServer.h>
#include <Tests/IModelTestHelper.h>

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <httpmockserver/port_searcher.h>
	#include <Core/ITwinAPI/ITwinEnvironment.h>
	#include <Core/ITwinAPI/ITwinRequestDump.h>
	#include <Core/Json/Json.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <Camera/CameraActor.h>
#include <Engine/DirectionalLight.h>
#include <GameFramework/PlayerController.h>
#include <GameFramework/WorldSettings.h>
#include <Interfaces/IPluginManager.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace ITwin
{
	AITwinDecorationHelper* GetOrCreateDecorationHelper(FString const& ITwinId, UWorld* World);
	bool ShouldLoadScene(FString const& ITwinID, UWorld const* World);

	namespace UnitTests
	{
		void InitLogs();
	}
}

namespace
{

std::wstring ConvertUtf8ToWide(const std::string& s)
{
	return StringCast<wchar_t>(StringCast<TCHAR>(s.c_str()).Get()).Get();
}

std::string ConvertWideToUtf8(const std::wstring& s)
{
	return (const char*)StringCast<UTF8CHAR>(StringCast<TCHAR>(s.c_str()).Get()).Get();
}

FString ConvertWideToFString(const std::wstring& s)
{
	return StringCast<TCHAR>(s.c_str()).Get();
}

} // unnamed namespace

void FITwinFileBasedMockServer::SetRelativeCachedResponsesFolder(std::filesystem::path const& InRelativeFolder)
{
	auto const PluginPtr = IPluginManager::Get().FindPlugin(TEXT("ITwinForUnreal"));
	if (ensureMsgf(PluginPtr, TEXT("Could not find ITwinForUnreal plugin")))
	{
		CachedResponsesFolder = *PluginPtr->GetBaseDir();
		CachedResponsesFolder /= "Resources";
		CachedResponsesFolder /= InRelativeFolder;
	}
}

inline std::filesystem::path FITwinFileBasedMockServer::GetResponsePath(
	const std::string& url,
	const std::string& data) const
{
	return CachedResponsesFolder / AdvViz::SDK::RequestDump::GetRequestHash(url, data) / "response.json";
}


	httpmock::MockServer::Response FITwinFileBasedMockServer::responseHandler(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers)
	{
		// Look for the folder corresponding to this request, containing the pre-recorded response.
		BE_ASSERT(!CachedResponsesFolder.empty());
		auto ResponseJsonPath = GetResponsePath(url, data);
		bool bHasResponseInCache = std::filesystem::exists(ResponseJsonPath);
		if (!bHasResponseInCache)
		{
			std::vector<std::filesystem::path> alternatePaths;

			std::string urlSuffix;
			if (!urlArguments.empty())
			{
				// sometimes the original url used when dumping the response contained the arguments...
				urlSuffix = "?";
				size_t argIndex = 0;
				for (const auto& arg : urlArguments)
				{
					if (argIndex > 0)
					{
						urlSuffix += "&";
					}
					urlSuffix += arg.key;
					if (arg.hasValue)
					{
						urlSuffix += "=" + rfl::internal::strings::replace_all(
							arg.value, " ", "+");
					}
					++argIndex;
				}
			}

			if (!urlSuffix.empty())
			{
				alternatePaths.emplace_back(GetResponsePath(url + urlSuffix, data));
			}

			if (url.starts_with("/"))
			{
				// Handle case when the original url did not start by the url separator
				std::string const urlWithoutSlash = url.substr(1);
				if (!urlSuffix.empty())
				{
					alternatePaths.emplace_back(GetResponsePath(urlWithoutSlash + urlSuffix, data));
				}
				alternatePaths.emplace_back(GetResponsePath(urlWithoutSlash, data));
			}

			for (auto const& Path : alternatePaths)
			{
				if (std::filesystem::exists(Path))
				{
					ResponseJsonPath = Path;
					bHasResponseInCache = true;
					break;
				}
			}
		}
		if (!bHasResponseInCache)
		{
			const auto msg = (std::wstringstream() << "Could not find file \"" << ResponseJsonPath.wstring() <<
				"\", url=\"" <<  ConvertUtf8ToWide(url) << "\", data=\"" << ConvertUtf8ToWide(data) << "\"").str();
			UE_LOG(LogTemp, Error, TEXT("%s"), *ConvertWideToFString(msg));
			return Response{404, ConvertWideToUtf8(msg)};
		}
		// Parse the pre-recorded response.
		AdvViz::SDK::RequestDump::Response ResponseInfo;
		{
			std::string ParseError;
			std::ifstream Ifs(ResponseJsonPath);
			if (!AdvViz::SDK::Json::FromStream(ResponseInfo, Ifs, ParseError))
			{
				const auto msg = (std::wstringstream() << "Could not parse file \"" << ResponseJsonPath.wstring() <<
					"\", error=" << ConvertUtf8ToWide(ParseError)).str();
				UE_LOG(LogTemp, Error, TEXT("%s"), *ConvertWideToFString(msg));
				return Response{404, ConvertWideToUtf8(msg)};
			}
		}
		// If there is a binary response file, use its content instead of the response stored in the json file.
		const auto ResponseBinPath = std::filesystem::path(ResponseJsonPath).replace_extension(".bin");
		if (std::filesystem::exists(ResponseBinPath))
		{
			std::ifstream Ifs(ResponseBinPath, std::ios::in|std::ios::binary);
			return Response{ResponseInfo.status, std::string(std::istreambuf_iterator<char>{Ifs}, {})};
		}
		return Response{ResponseInfo.status, ResponseInfo.body};
	}


std::unique_ptr<FITwinFileBasedMockServer> GetITwinFileBasedMockServer(
	std::filesystem::path const& InRelativeFolder, unsigned int DefaultPort /*= 8080*/)
{
	auto Server = std::unique_ptr<FITwinFileBasedMockServer>(
		static_cast<FITwinFileBasedMockServer*>(httpmock::getFirstRunningMockServer<FITwinFileBasedMockServer>(DefaultPort).release()));
	if (ensure(Server))
	{
		Server->SetRelativeCachedResponsesFolder(InRelativeFolder);
	}
	return Server;
}


/*static*/ FIModelTestHelper::FIModelLoadingResult FIModelTestHelper::InitiateIModelLoading(UWorld* World,
	FITwinExportInfo const& ExportInfo,
	std::filesystem::path const& InRelativeCacheFolder,
	FString const& SceneId,
	FOptions const& Options /*= {}*/)
{
	FIModelLoadingResult Result;

	ITwin::UnitTests::InitLogs();

	// Disable error logs from WebServices, because some error messages are actually not errors and should be warnings.
	const auto bLogErrorsBackup = UITwinWebServices::ShouldLogErrors();
	UITwinWebServices::SetLogErrors(false);

	const bool bHasDecorationScopeBackup = AdvViz::SDK::ITwinAuthManager::HasScope(ITWIN_DECORATIONS_SCOPE);
	if (!bHasDecorationScopeBackup)
	{
		// Add the decoration scope, which is required to load the scene.
		AdvViz::SDK::ITwinAuthManager::AddScope(ITWIN_DECORATIONS_SCOPE);
	}

	const bool bSavedViewsUpdatesEnabledBackup = AITwinIModel::AreSavedViewsUpdatesEnabled();
	AITwinIModel::EnableSavedViewsUpdates(Options.bAutoLoadSavedViews);
	// Enable material tuning for those tests.
	const bool bMaterialTuningEnabledBackup = AITwinIModel::IsMaterialTuningEnabled();
	AITwinIModel::EnableMaterialTuning(Options.bEnableMaterialTuning);
	// Disable 4D schedule querying if needed.
	AITwinIModel::Disable4DAutoLoadSchedule(Options.bDisableSynchro4DAutoLoad);

	Result.CleanupGuard = std::make_unique<Be::CleanUpGuard>([bLogErrorsBackup, bHasDecorationScopeBackup, bSavedViewsUpdatesEnabledBackup, bMaterialTuningEnabledBackup]
	{
		// Upon test exit, also clear the "override token", which is useful when running tests manually in
		// the editor. It allows to then launch the app without having to restart UE.
		FITwinAuthorizationManager::GetInstance(AdvViz::SDK::EITwinEnvironment::Prod)->ResetOverrideAccessToken();
		UITwinWebServices::SetLogErrors(bLogErrorsBackup);
		if (!bHasDecorationScopeBackup)
		{
			// Remove the decoration scope that was added for the test.
			AdvViz::SDK::ITwinAuthManager::RemoveScope(ITWIN_DECORATIONS_SCOPE);
		}
		AITwinIModel::EnableSavedViewsUpdates(bSavedViewsUpdatesEnabledBackup);
		AITwinIModel::EnableMaterialTuning(bMaterialTuningEnabledBackup);
		AITwinIModel::Disable4DAutoLoadSchedule(false);
	});

	static int NextMockServerPort = 8050;
	auto MockServer = GetITwinFileBasedMockServer(InRelativeCacheFolder, NextMockServerPort);
	if (MockServer)
	{
		// Avoid conflicting with other tests that may be running in parallel, by using a different port for
		// each test.
		NextMockServerPort = MockServer->getPort() + 1;
	}

	// Disable World bound checks as recommended by Cesium plugin.
	World->GetWorldSettings()->bEnableWorldBoundsChecks = false;

	if (Options.bEnableRendering)
	{
		// Create camera that will point to the iModel.
		auto* const Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass());
		Camera->SetActorLocationAndRotation({-15900, 14900, 16300}, {-34.4, -19.6, 0});
		World->GetFirstPlayerController()->SetViewTarget(Camera);
		// Add a light so that we see something.
		World->SpawnActor(ADirectionalLight::StaticClass());
	}

	// Before loading the iModel, select the scene we want to load.
	Result.DecoHelper = ITwin::GetOrCreateDecorationHelper(ExportInfo.iTwinId, World);
	if (ensure(Result.DecoHelper))
	{
		// Reset the state of the decoration helper between tests.
		Result.DecoHelper->ResetLoadedScene();

		// Initialize Scene API configuration to point to our mock server.
		if (MockServer)
		{
			Result.DecoHelper->SetMockServerPort(MockServer->getPort());
		}
		Result.DecoHelper->SetLoadedSceneId(SceneId);
		Result.bShouldLoadScene = ITwin::ShouldLoadScene(ExportInfo.iTwinId, World);
	}

	// Load an iModel if needed.
	AITwinIModel* IModel = nullptr;
	if (!ExportInfo.iModelId.IsEmpty())
	{
		IModel = World->SpawnActor<AITwinIModel>();
		IModel->SetTestMode(MockServer->GetUrl());
		IModel->DisableAutoRefresh();

		if (ExportInfo.Id.IsEmpty())
		{
			// If no export ID is provided, try to discover an export.
			IModel->IModelId = ExportInfo.iModelId;
			IModel->ChangesetId = ExportInfo.ChangesetId;
			IModel->LoadingMethod = ELoadingMethod::LM_Automatic;
			IModel->UpdateIModel();
		}
		else
		{
			// Directly load the iModel with the provided Export ID.
			IModel->LoadModelFromInfos(FITwinExportInfo{
				.Id = ExportInfo.Id,
				.DisplayName = ExportInfo.DisplayName,
				.Status = TEXT("Complete"),
				.iModelId = ExportInfo.iModelId,
				.iTwinId = ExportInfo.iTwinId,
				.ChangesetId = ExportInfo.ChangesetId,
				.MeshUrl = MockServer->GetUrl() + TEXT("/Mesh/tileset.json"),
			});
		}
	}
	Result.IModel = IModel;

	Result.MockServer = std::move(MockServer);

	return Result;
}

//! Test is disabled due to random failures in "Publish" ADO pipeline.
ITWIN_FUNCTIONAL_TEST_EX(IModelRender, false)
{
	FITwinExportInfo ExportInfo;
	ExportInfo.Id = TEXT("a4feb6ee-bb1c-40d3-8659-a1765b1d179d");
	ExportInfo.iTwinId = TEXT("5e15184e-6d3c-43fd-ad04-e28b4b39485e");
	ExportInfo.iModelId = TEXT("b53cebea-451f-4433-942f-eabda9c11d21");
	ExportInfo.ChangesetId = TEXT("");
	ExportInfo.DisplayName = TEXT("Z");

	// Use the default scene of this iTwin.
	const FString SceneId = TEXT("b0814356-cbf0-4c4f-8f05-02a636cefa2d");

	FString const RelativeCacheFolder = FString("FunctionalTests") / TestName;

	FIModelTestHelper::FOptions Opts;
	Opts.bEnableRendering = true;
	// This iModel has no 4D schedule, so we disable the automatic loading of the 4D schedule to avoid
	// unnecessary requests (and errors in the logs).
	Opts.bDisableSynchro4DAutoLoad = true;

	auto IModelLoadingResult = FIModelTestHelper::InitiateIModelLoading(World, ExportInfo,
		std::filesystem::path(*RelativeCacheFolder),
		SceneId,
		Opts);

	// Make sure we will wait for the decoration to be fully loaded (even though there is no decoration
	// attached to the tested model, we must ensure the dummy access token is available for the whole
	// asynchronous decoration loading (see #GetDecorationAccessToken...)
	auto DecoHelper = IModelLoadingResult.DecoHelper;
	if (DecoHelper && DecoHelper->IsLoadingScene())
	{
		TPromise<bool/*dummy, void no longer compiles*/> DecoPromise;
		const auto DelegateHandle = DecoHelper->OnDecorationLoaded.AddLambda([&]()
		{
			DecoPromise.SetValue(true/*dummy*/);

			// Stop the mock server now that we are done with the test, to avoid conflicts with other tests
			// that may be running in parallel (see FITwinMockServerBase::HasRunningInstance).
			IModelLoadingResult.MockServer.reset();
		});
		co_await DecoPromise.GetFuture();
		DecoHelper->OnDecorationLoaded.Remove(DelegateHandle);
	}

	// Here we have to wait for the tileset to be loaded and displayed.
	// Ideally there should be a dedicated event upon which we could wait.
	co_await UE5Coro::Async::PlatformSeconds(1);
	// Take a screenshot.
	co_await TakeScreenshot(TEXT("Screenshot1"));
	// Take another (useless) screenshot, just to check that it is possible to do so.
	co_await TakeScreenshot(TEXT("Screenshot2"));
}

#endif // WITH_TESTS && WITH_EDITOR
