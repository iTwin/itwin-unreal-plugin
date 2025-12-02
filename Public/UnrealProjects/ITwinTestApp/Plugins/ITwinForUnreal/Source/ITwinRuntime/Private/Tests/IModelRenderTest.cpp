/*--------------------------------------------------------------------------------------+
|
|     $Source: IModelRenderTest.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS && WITH_EDITOR

#include <CesiumRuntime.h>
#include <ITwinIModel.h>
#include <ITwinWebServices/ITwinAuthorizationManager.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <ITwinWebServices/ITwinWebServices_Info.h>
#include <Decoration/ITwinDecorationHelper.h>

#include <Tests/ITwinFunctionalTest.h>

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <httpmockserver/mock_server.h>
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
	AITwinDecorationHelper* GetDecorationHelper(FString const& Info, UWorld const* World);
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

class FMockServer: public httpmock::MockServer
{
private:
	using Super = httpmock::MockServer;
public:
	using Super::Super;
	FString TestName;
	virtual Response responseHandler(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) override
	{
		// Look for the folder corresponding to this request, containing the pre-recorded response.
		const auto ResponseJsonPath = std::filesystem::path(*IPluginManager::Get().FindPlugin(TEXT("ITwinForUnreal"))->GetBaseDir())/
			"Resources/FunctionalTests"/(*TestName)/AdvViz::SDK::RequestDump::GetRequestHash(url, data)/"response.json";
		if (!std::filesystem::exists(ResponseJsonPath))
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
	FString GetUrl() const
	{
		return FString::Format(TEXT("http://localhost:{0}"), {getPort()});
	}
};

std::unique_ptr<FMockServer> GetMockServer(const FString& TestName)
{
	auto Server = std::unique_ptr<FMockServer>(static_cast<FMockServer*>(httpmock::getFirstRunningMockServer<FMockServer>().release()));
	Server->TestName = TestName;
	return Server;
}

} // unnamed namespace

//! Test is disabled due to random failures in "Publish" ADO pipeline.
ITWIN_FUNCTIONAL_TEST_EX(IModelRender, false)
{
	// Disable error logs from WebServices, because some error messages are actually not errors and should be warnings.
	const auto bLogErrorsBackup = UITwinWebServices::ShouldLogErrors();
	UITwinWebServices::SetLogErrors(false);
	ON_SCOPE_EXIT
	{
		// Upon test exit, also clear the "override token", which is useful when running tests manually in the editor.
		// It allows to then launch the app without having to restart UE.
		FITwinAuthorizationManager::GetInstance(AdvViz::SDK::EITwinEnvironment::Prod)->ResetOverrideAccessToken();
		UITwinWebServices::SetLogErrors(bLogErrorsBackup);
	};
	auto MockServer = GetMockServer(TestName);
	// Disable World bound checks as recommended by Cesium plugin.
	World->GetWorldSettings()->bEnableWorldBoundsChecks = false;
	// Create camera that will point to the iModel.
	auto* const Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass());
	Camera->SetActorLocationAndRotation({-15900, 14900, 16300}, {-34.4, -19.6, 0});
	World->GetFirstPlayerController()->SetViewTarget(Camera);
	// Add a light so that we see something.
	World->SpawnActor(ADirectionalLight::StaticClass());
	// Load an iModel.
	auto* const IModel = World->SpawnActor<AITwinIModel>();
	IModel->SetTestMode(MockServer->GetUrl());
	IModel->LoadModelFromInfos(FITwinExportInfo{
		.Id = TEXT("0"),
		.DisplayName = TEXT("Z"),
		.Status = TEXT("Complete"),
		.iModelId = TEXT("b53cebea-451f-4433-942f-eabda9c11d21"),
		.iTwinId = TEXT("5e15184e-6d3c-43fd-ad04-e28b4b39485e"),
		.ChangesetId = TEXT(""),
		.MeshUrl = MockServer->GetUrl()+TEXT("/Mesh/tileset.json"),
	});

	// Make sure we will wait for the decoration to be fully loaded (even though there is no decoration
	// attached to the tested model, we must ensure the dummy access token is available for the whole
	// asynchronous decoration loading (see #GetDecorationAccessToken...)
	AITwinDecorationHelper* DecoHelper = ITwin::GetDecorationHelper(IModel->GetModelLoadInfo().ITwinId, World);
	if (DecoHelper && DecoHelper->IsLoadingScene())
	{
		TPromise<bool/*dummy, void no longer compiles*/> DecoPromise;
		const auto DelegateHandle = DecoHelper->OnDecorationLoaded.AddLambda([&]()
		{
			DecoPromise.SetValue(true/*dummy*/);
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
