/*--------------------------------------------------------------------------------------+
|
|     $Source: AuthorizationTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#if WITH_TESTS 

#include <ITwinWebServices/ITwinAuthorizationManager.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <ITwinWebServices/ITwinWebServicesObserver.h>
#include <ITwinServerConnection.h>
#include <ITwinServiceActor.h>

#include <Tests/ITwinAutomationTestBaseNoLogs.h>
#include <Network/HttpUtils.h>
#include <Tests/ITwinMockServerBase.h>
#include <Tests/ITwinTestServiceActor.h>
#include <Tests/WebTestHelpers.h>


#include <Engine/World.h>
#include <HAL/PlatformProcess.h>
#include <HttpModule.h>
#include <Interfaces/IHttpResponse.h>
#include <Misc/LowLevelTestAdapter.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/ITwinAPI/ITwinAuthObserver.h>
#	include <Core/Network/IHttpRouter.h>
#	include <Core/Network/http.h>
#include <Compil/AfterNonUnrealIncludes.h>


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FITwinAuthorizationHelpersTest, "Bentley.ITwinForUnreal.ITwinRuntime.AuthorizationHelpers", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinAuthorizationHelpersTest::RunTest(const FString& /*Parameters*/)
{
	SECTION("TokenEncryption")
	{
		FString SrcToken;
		// Build a random token
		FString const ValidChars = TEXT("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
		int32 const NbValidChars = ValidChars.Len() - 1;
		SrcToken.Reserve(512);
		for (uint32 i = 0; i < 512; ++i)
		{
			SrcToken.AppendChar(ValidChars[FMath::RandRange(0, NbValidChars)]);
		}
		// Since tokens are saved on a per-environment basis, we need to choose one, even though this has no
		// real impact on the test
		AdvViz::SDK::EITwinEnvironment const TestEnv = AdvViz::SDK::EITwinEnvironment::Prod;
		// Avoid conflicting with the true application (or another instance of the same test running in
		// parallel...), and provide with a default iTwin App ID if there is none currently.
		FString TokenFileSuffixForTest = FString::Printf(TEXT("_Test_%d"), FPlatformProcess::GetCurrentProcessId());
		FITwinAuthorizationManager::SetupTestMode(TestEnv, TokenFileSuffixForTest);

		UTEST_TRUE("SaveToken", FITwinAuthorizationManager::SaveToken(SrcToken, TestEnv));
		FString ReadToken;
		UTEST_TRUE("LoadToken", FITwinAuthorizationManager::LoadToken(ReadToken, TestEnv));
		UTEST_EQUAL("Unchanged Token", ReadToken, SrcToken);

		// Cleanup
		FITwinAuthorizationManager::DeleteTokenFile(TestEnv);
	}

	SECTION("Encode-DecodeToken")
	{
		auto const& AuthMngr = AdvViz::SDK::ITwinAuthManager::GetInstance(AdvViz::SDK::EITwinEnvironment::Prod);
		if (!ensure(AuthMngr))
		{
			return false;
		}
		std::string const SrcToken = "This is a test token";
		std::string const KeyRoot = "This is a test key root";
		std::string OutEncoded;
		UTEST_TRUE("EncodeToken", AuthMngr->EncodeToken(SrcToken, KeyRoot, /*Out*/ OutEncoded));
		UTEST_TRUE("encoded token differs from source", OutEncoded != SrcToken);
		std::string OutDecoded;
		UTEST_TRUE("DecodeToken", AuthMngr->DecodeToken(OutEncoded, KeyRoot, /*Out*/ OutDecoded));
		UTEST_EQUAL("Decoded Token", OutDecoded, SrcToken);
	}

	return true;
}


/// Records what ITwinAuthManager::CheckAuthorization bound, and lets the test replay the
/// redirect that the external browser would normally trigger on 127.0.0.1:<port>/signin-callback.
struct FITwinAuthRouteSpy
{
	int Port = 0;
	std::string Endpoint;
	AdvViz::SDK::IHttpRouter::RequestHandlerCallback Callback;
	AdvViz::SDK::IHttpRouter::RouteHandlePtr Handle;
	int BindCount = 0;
	AdvViz::SDK::IHttpRouter const* Router = nullptr;

	bool IsBound() const { return BindCount > 0 && Handle && Handle->IsValid(); }

	/// Simulates the browser hitting the redirect URI, and mimics what FUEHttpRouter does with
	/// the returned disposition. Returns the produced HTML.
	std::string Replay(std::map<std::string, std::string> const& QueryParams) const;
};
using FITwinAuthRouteSpyPtr = std::shared_ptr<FITwinAuthRouteSpy>;


std::string FITwinAuthRouteSpy::Replay(std::map<std::string, std::string> const& QueryParams) const
{
	std::string HtmlText;
	if (Callback
		&& Callback(QueryParams, HtmlText) == AdvViz::SDK::IHttpRouter::ERouteDisposition::Unbind)
	{
		if (Router)
		{
			Router->UnbindRoute(Handle);
		}
	}
	return HtmlText;
}


/// Fake IHttpRouter: never opens a socket, just captures the handler.
class FITwinHttpRouterMock : public AdvViz::SDK::IHttpRouter
{
public:
	explicit FITwinHttpRouterMock(FITwinAuthRouteSpyPtr const& InSpy) : Spy(InSpy) {}

	struct FHandle : public AdvViz::SDK::IHttpRouter::RouteHandle
	{
		bool bBound = true;
		virtual bool IsValid() const override { return bBound; }
	};

	virtual RouteHandlePtr MakeRouteHandler() const override
	{
		return std::make_shared<FHandle>();
	}

	virtual bool BindRoute(
		RouteHandlePtr& RouteHandle,
		int Port, std::string const& RedirectUriEndpoint, AdvViz::SDK::EVerb /*Verb*/,
		RequestHandlerCallback const& RequestHandlerCB) const override
	{
		if (!RouteHandle)
		{
			RouteHandle = MakeRouteHandler();
		}
		if (Spy)
		{
			Spy->Port = Port;
			Spy->Endpoint = RedirectUriEndpoint;
			Spy->Callback = RequestHandlerCB;
			Spy->Router = this;
			Spy->Handle = RouteHandle;
			Spy->BindCount++;
		}
		return true;
	}

	virtual bool UnbindRoute(RouteHandlePtr const& RouteHandle) const override
	{
		auto const MockHandle = std::static_pointer_cast<FHandle>(RouteHandle);
		if (!MockHandle || !MockHandle->bBound)
		{
			return false; // already released - mirrors FUERouteHandle::Release()
		}
		MockHandle->bBound = false;
		return true;
	}

private:
	FITwinAuthRouteSpyPtr Spy;
};


/// Installs a mock IHttpRouter factory for the lifetime of this object, and restores the
/// production one on destruction.
///
/// This matters because ITwinAuthManager creates its router lazily and *once* (see
/// CheckAuthorization), so a leaked mock factory keeps being handed out to every manager that has
/// not built its router yet - including those used by subsequent tests, which would then wait
/// forever for a redirect that no socket can ever receive.
class [[nodiscard]] FITwinScopedHttpRouterMock
{
public:
	explicit FITwinScopedHttpRouterMock(FITwinAuthRouteSpyPtr const& InSpy)
	{
		AdvViz::SDK::IHttpRouter::SetNewFct([Spy = InSpy]() {
			AdvViz::SDK::IHttpRouter* p(
				static_cast<AdvViz::SDK::IHttpRouter*>(new FITwinHttpRouterMock(Spy)));
			return p;
		});
	}

	~FITwinScopedHttpRouterMock()
	{
		FITwinAuthorizationManager::RestoreHttpRouterFactory();
	}

	FITwinScopedHttpRouterMock(FITwinScopedHttpRouterMock const&) = delete;
	FITwinScopedHttpRouterMock& operator=(FITwinScopedHttpRouterMock const&) = delete;
};


#define ITWINTEST_AUTH_CODE "AUTH_CODE_123"
#define ITWINTEST_REFRESH_TOKEN "FakeRefreshToken_42"
#define ITWINTEST_NEW_ACCESS_TOKEN "FakeAccessTokenFromMockServer"

/// Captures the outcome of the asynchronous token exchange.
class FITwinAuthObserverMock : public AdvViz::SDK::ITwinAuthObserver
{
public:
	std::atomic_bool bDone = false;
	bool bSuccess = false;
	std::string Error;

	virtual void OnAuthorizationDone(bool bInSuccess, std::string const& InError) override
	{
		bSuccess = bInSuccess;
		Error = InError;
		bDone = true;
	}
};

/// Common state and teardown for the latent commands driving an authorization test.
///
/// Those commands all run *after* RunTest has returned, so they own the cleanup: scope-based
/// cleanup in the test body would fire far too early, while the asynchronous outcome is still
/// pending. They also run across garbage collections, hence the strong references below.
///
/// Derived classes only implement Update(), and must call Cleanup() on *every* path that returns
/// true - including error and timeout paths - or the altered global state would leak into the
/// following tests.
class FITwinAuthLatentCommandBase : public IAutomationLatentCommand
{
public:
	FITwinAuthLatentCommandBase(
		FAutomationTestBase& InTest,
		std::shared_ptr<FITwinAuthObserverMock> const& InObserver,
		AdvViz::SDK::ITwinAuthManager::SharedInstance const& InAuthMngr,
		AdvViz::SDK::EITwinEnvironment InEnv,
		UITwinWebServices* InWebServices,
		AITwinServerConnection* InTestConnection,
		AITwinTestServiceActor* InTestServiceActor,
		double InDelaySeconds)
		: Test(InTest)
		, Observer(InObserver)
		, AuthMngr(InAuthMngr)
		, Env(InEnv)
		, WebServices(InWebServices)
		, TestConnection(InTestConnection)
		, TestServiceActor(InTestServiceActor)
		, EndTime(FPlatformTime::Seconds() + InDelaySeconds)
	{}

protected:
	/// Restores everything the test has altered on the (process-wide) authorization singleton.
	void Cleanup()
	{
		AuthMngr->RemoveObserver(Observer.get());
		AuthMngr->SetIMSBaseUrlForTesting(std::nullopt);
		AuthMngr->Logout(); // also cancels the "refreshAuth" delayed call
		FITwinAuthorizationManager::DeleteTokenFile(Env);

		CleanupWebServices();
		CleanupServerConnection();
		CleanupServiceActor();
	}

	/// True once the delay passed to the constructor has elapsed. Interpreted as a timeout by
	/// commands waiting for an event, and as a grace period by those asserting nothing happens.
	bool IsDelayElapsed() const
	{
		return FPlatformTime::Seconds() > EndTime;
	}

	UITwinWebServices* GetWebServices() const
	{
		return WebServices.Get();
	}

	AITwinServerConnection* GetServerConnection() const
	{
		return TestConnection.Get();
	}

	AITwinTestServiceActor* GetTestServiceActor() const
	{
		return TestServiceActor.Get();
	}

	FAutomationTestBase& Test;
	std::shared_ptr<FITwinAuthObserverMock> Observer;
	AdvViz::SDK::ITwinAuthManager::SharedInstance AuthMngr;
	AdvViz::SDK::EITwinEnvironment Env;

private:
	/// Destroys the UITwinWebServices object created for this test, so that GetActiveEnvironment()
	/// does not return a stale value and cause subsequent tests to fail.
	void CleanupWebServices()
	{
		if (UITwinWebServices* const WS = WebServices.Get())
		{
			WS->RemoveFromRoot();
		}
		// Release our strong reference, so object can be collected.
		WebServices.Reset();
	}

	void CleanupServerConnection()
	{
		if (AITwinServerConnection* const Conn = TestConnection.Get())
		{
			if (IsValid(Conn))
			{
				Conn->Destroy();
			}
		}
		TestConnection.Reset();
	}

	void CleanupServiceActor()
	{
		if (AITwinServiceActor* const Actor = TestServiceActor.Get())
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}
		TestServiceActor.Reset();
	}

	/// Strong references: this command outlives RunTest, and runs across garbage collections.
	TStrongObjectPtr<UITwinWebServices> WebServices;
	TStrongObjectPtr<AITwinServerConnection> TestConnection;
	TStrongObjectPtr<AITwinTestServiceActor> TestServiceActor;
	double EndTime = 0.;
};

/// Waits (across game thread ticks) for the asynchronous /connect/token exchange to complete, then
/// checks its outcome.
/// Unreal's HTTP responses are dispatched from the game thread tick, so we must *not* block it:
/// hence a latent command rather than a busy wait.
class FITwinWaitForAuthorizationDone : public FITwinAuthLatentCommandBase
{
public:
	using FITwinAuthLatentCommandBase::FITwinAuthLatentCommandBase;

	virtual bool Update() override
	{
		if (!Observer->bDone)
		{
			if (IsDelayElapsed())
			{
				Test.AddError(TEXT("Timed out waiting for the token exchange to complete"));
				Cleanup();
				return true; // stop ticking
			}
			return false; // not done yet - will be called again next tick
		}

		Test.TestTrue(TEXT("Authorization succeeded"), Observer->bSuccess);
		Test.TestTrue(TEXT("Error is empty"), Observer->Error.empty());
		Test.TestTrue(TEXT("HasAccessToken after success"), AuthMngr->HasAccessToken());
		Test.TestEqual(TEXT("Access token value"),
			FString(AuthMngr->GetRegularAccessToken().c_str()),
			TEXT(ITWINTEST_NEW_ACCESS_TOKEN));
		Test.TestFalse(TEXT("IsAuthorizationInProgress after success"),
			AuthMngr->IsAuthorizationInProgress());

		if (UITwinWebServices* const WS = GetWebServices())
		{
			Test.TestTrue(TEXT("HasAccessToken after success (web-services)"), WS->HasAccessToken());
			Test.TestEqual(TEXT("Access token value (web-services)"),
				WS->GetAccessToken(),
				TEXT(ITWINTEST_NEW_ACCESS_TOKEN));
		}
		if (AITwinServerConnection* const Conn = GetServerConnection())
		{
			Test.TestTrue(TEXT("HasAccessToken after success (server connection)"), Conn->HasAccessToken());
			Test.TestEqual(TEXT("Access token value (server connection)"),
				Conn->GetAccessToken(),
				TEXT(ITWINTEST_NEW_ACCESS_TOKEN));
		}
		if (AITwinTestServiceActor* const Actor = GetTestServiceActor())
		{
			Test.TestTrue(TEXT("HasAccessToken after success (service actor)"), Actor->HasAccessToken());
			Test.TestEqual(TEXT("Access token value (service actor)"),
				Actor->GetAccessToken(),
				TEXT(ITWINTEST_NEW_ACCESS_TOKEN));
			Test.TestTrue(TEXT("TestServiceActor received OnAuthorizationDone"), Actor->bHasReceivedAuthorizationDone);
		}

		Cleanup();
		return true;
	}
};


/// Waits for a grace period, then checks that a cancelled authorization did *not* silently
/// complete.
/// Unlike FITwinWaitForAuthorizationDone, there is no completion event to wait for here: we are
/// asserting that nothing happens, so a timed wait is the only option. The delay must simply be
/// comfortably longer than a mock-server round trip.
class FITwinCheckAuthorizationCancelled : public FITwinAuthLatentCommandBase
{
public:
	using FITwinAuthLatentCommandBase::FITwinAuthLatentCommandBase;

	virtual bool Update() override
	{
		if (!IsDelayElapsed())
		{
			return false; // keep ticking
		}
		Test.TestFalse(TEXT("No access token after a cancelled authorization"),
			AuthMngr->HasAccessToken());
		Test.TestTrue(TEXT("Regular access token is empty"),
			AuthMngr->GetRegularAccessToken().empty());
		Test.TestFalse(TEXT("Observer was not notified of a cancelled authorization"),
			Observer->bDone.load());
		Test.TestFalse(TEXT("IsAuthorizationInProgress after cancellation"),
			AuthMngr->IsAuthorizationInProgress());

		if (UITwinWebServices* const WS = GetWebServices())
		{
			Test.TestFalse(TEXT("No access token after a cancelled authorization (web-services)"), WS->HasAccessToken());
			Test.TestTrue(TEXT("Access token is empty (web-services)"),
				WS->GetAccessToken().IsEmpty());
		}
		if (AITwinServerConnection* const Conn = GetServerConnection())
		{
			Test.TestFalse(TEXT("No access token after a cancelled authorization (server connection)"), Conn->HasAccessToken());
			Test.TestTrue(TEXT("Access token is empty (server connection)"), Conn->GetAccessToken().IsEmpty());
		}

		Cleanup();
		return true;
	}
};

/// Mock server implementation for iTwin authorization tests (the /connect/token endpoint).

class FITwinAuthorizationMockServer : public FITwinMockServerBase
{
public:
	static std::unique_ptr<httpmock::MockServer> MakeServer(
		unsigned startPort, unsigned tryCount = 1000);

	explicit FITwinAuthorizationMockServer(int port) : FITwinMockServerBase(port) {}

	virtual Response responseHandler(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) override
	{
		if (isUrl(url, "/arg_test"))
		{
			return ProcessArgTest(urlArguments);
		}
		if (isUrl(url, "/connect/token"))
		{
			return ProcessTokenRequestTest(method, data);
		}
		return Response(MHD_HTTP_NOT_FOUND,
			std::string("Page not found: ") + url);
	}

private:
	/// Process the OAuth token exchange (/connect/token)
	Response ProcessTokenRequestTest(const std::string& method, const std::string& data) const
	{
		if (method != "POST")
		{
			return Response(MHD_HTTP_METHOD_NOT_ALLOWED, "Expecting POST");
		}
		// The body is application/x-www-form-urlencoded (see ITwinAuthManager::ProcessTokenRequest).
		if (data.find("grant_type=authorization_code") == std::string::npos)
		{
			return Response(MHD_HTTP_BAD_REQUEST, "Unexpected grant_type");
		}
		if (data.find("code=" ITWINTEST_AUTH_CODE) == std::string::npos)
		{
			return Response(MHD_HTTP_BAD_REQUEST, "Missing or wrong authorization code");
		}
		// PKCE: the verifier must be sent back, and the client identified.
		if (data.find("code_verifier=") == std::string::npos
			|| data.find("client_id=") == std::string::npos
			|| data.find("redirect_uri=") == std::string::npos)
		{
			return Response(MHD_HTTP_BAD_REQUEST, "Missing PKCE/client parameters");
		}
		return Response(MHD_HTTP_OK,
			"{\"access_token\":\"" ITWINTEST_NEW_ACCESS_TOKEN "\","
			"\"token_type\":\"Bearer\","
			"\"expires_in\":3600,"
			"\"refresh_token\":\"" ITWINTEST_REFRESH_TOKEN "\"}");
	}
};

/*static*/
std::unique_ptr<httpmock::MockServer> FITwinAuthorizationMockServer::MakeServer(
	unsigned startPort, unsigned tryCount /*= 1000*/)
{
	return httpmock::getFirstRunningMockServer<FITwinAuthorizationMockServer>(startPort, tryCount);
}


class ITwinTestAuthorizationObserver : public FITwinDefaultWebServicesObserver
{
public:
	virtual const TCHAR* GetObserverName() const override {
		return TEXT("TestAuthorizationObserver");
	}

	// Must be called *before* a new request is made, as the name says.
	void AddPendingRequest() { NumPendingRequests++; }
	void OnResponseReceived() {
		NumProcessedRequests++;
		ensureMsgf(NumProcessedRequests <= NumPendingRequests, TEXT("received more answers than expected!"));
	}
	bool IsWaitingForServerResponse() const { return NumProcessedRequests < NumPendingRequests; }
	int GetPendingRequestCount() const { return NumPendingRequests - NumProcessedRequests; }

private:
	int NumPendingRequests = 0;
	int NumProcessedRequests = 0;
};

using TestAuthorizationObserverPtr = std::shared_ptr<ITwinTestAuthorizationObserver>;



class FITwinAuthorizationTestHelper : public FITwinAPITestHelperBase
{
public:
	static FITwinAuthorizationTestHelper& Instance();
	~FITwinAuthorizationTestHelper();

	TestAuthorizationObserverPtr const& GetObserver() const { return Observer; }
	TObjectPtr<UITwinWebServices> const& GetWebServices() const { return WebServices; }

protected:
	virtual bool DoInit(AdvViz::SDK::EITwinEnvironment Env) override;
	virtual void DoCleanup() override;

private:
	FITwinAuthorizationTestHelper() {}

	TObjectPtr<UITwinWebServices> WebServices;
	TestAuthorizationObserverPtr Observer;
};

/*static*/
FITwinAuthorizationTestHelper& FITwinAuthorizationTestHelper::Instance()
{
	static FITwinAuthorizationTestHelper instance;
	return instance;
}

bool FITwinAuthorizationTestHelper::DoInit(AdvViz::SDK::EITwinEnvironment Env)
{
	/// Port number server is tried to listen on
	/// Number is being incremented while free port has not been found.
	static constexpr int DEFAULT_SERVER_PORT = 8085;

	if (!InitServer(FITwinAuthorizationMockServer::MakeServer(DEFAULT_SERVER_PORT)))
	{
		return false;
	}

	// totally disable error logs (even though SuppressLogErrors avoids making the unit-test fail, the test
	// target still fails at the end because of the logs...)
	UITwinWebServices::SetLogErrors(false);

	const std::string url = GetServerUrl();

	WebServices = NewObject<UITwinWebServices>();
	WebServices->SetEnvironment(static_cast<EITwinEnvironment>(Env));
	WebServices->SetTestServerURL(url.c_str());
	WebServices->SetRetryDelayFactor(0.1f); // wait less before retrying.

	Observer = std::make_shared<ITwinTestAuthorizationObserver>();
	WebServices->SetObserver(Observer.get());
	return true;
}

void FITwinAuthorizationTestHelper::DoCleanup()
{
	if (WebServices)
	{
		WebServices->SetObserver(nullptr);
	}
	WebServices = nullptr;
	Observer.reset();
}

FITwinAuthorizationTestHelper::~FITwinAuthorizationTestHelper()
{
	Cleanup();
}
	

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FNUTWaitForAuthMockServerResponse, TestAuthorizationObserverPtr, Observer);

bool FNUTWaitForAuthMockServerResponse::Update()
{
	if (Observer && Observer->IsWaitingForServerResponse())
	{
		static double LastLog = 0.;
		if (FPlatformTime::Seconds() - LastLog > 2.)
		{
			LastLog = FPlatformTime::Seconds();
			UE_LOG(LogTemp, Warning, TEXT("Still waiting for %d authorization response(s)"),
				Observer->GetPendingRequestCount());
		}
		return false;
	}
	return true;
}


IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FITwinAuthorizationTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.Authorization", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FITwinAuthorizationTest::RunTest(const FString& /*Parameters*/)
{
	FITwinAuthorizationTestHelper& Helper = FITwinAuthorizationTestHelper::Instance();
	if (!Helper.Init())
	{
		return false;
	}
	auto const& Observer = Helper.GetObserver();
	auto const& WebServices = Helper.GetWebServices();
	const std::string url = Helper.GetServerUrl();

	SECTION("MockServer Validation")
	{
		// Most basic test, just to validate the mock server
		const auto Request = FHttpModule::Get().CreateRequest();
		Request->SetVerb(TEXT("GET"));
		const std::string fullUrl = url + "/arg_test?x=0&b=2";
		Request->SetURL(fullUrl.c_str());
		Observer->AddPendingRequest();
		Request->OnProcessRequestComplete().BindLambda(
			[this, Observer]
			(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully) mutable
			{
				TestTrue(TEXT("Mock server arg_test response"),
					ITwinHttp::CheckRequest(Request, Response, ITwinHttp::ConnectionSuccess(bConnectedSuccessfully)));
				Observer->OnResponseReceived();
			});
		Request->ProcessRequest();
	}

	SECTION("AuthorizationCallback")
	{
		// Deliberately *not* the environment used by WebServices (Prod): ITwinAuthManager is a
		// singleton per environment, and the other sections still have requests in flight that
		// rely on the Prod token.
		constexpr AdvViz::SDK::EITwinEnvironment AuthTestEnv = AdvViz::SDK::EITwinEnvironment::QA;

		// Dedicated instance, so that the shared 'WebServices' (Prod) - and the requests still in
		// flight from the previous sections - are left untouched.
		UWorld* const TestWorld = FITwinAPITestHelperBase::GetTestWorld();
		if (!ensure(TestWorld))
		{
			return false;
		}

		// Use a service actor to test the authorization.
		AITwinTestServiceActor* ServiceActor = TestWorld->SpawnActor<AITwinTestServiceActor>();
		if (!ensure(ServiceActor))
		{
			return false;
		}
		// Use a manual environment selection, so that the service actor does not switch to Prod
		// automatically (as a Prod access token is already available in the test process).
		ServiceActor->EnvironmentSelectionMethod = EEnvironmentSelectionMethod::ESM_Manual;
		ServiceActor->SetEnvironment(static_cast<EITwinEnvironment>(AuthTestEnv));

		// Create a server connection for the same environment.
		AITwinServerConnection* const TestConnection = TestWorld->SpawnActor<AITwinServerConnection>();
		if (!ensure(TestConnection))
		{
			return false;
		}
		TestConnection->Environment = static_cast<EITwinEnvironment>(AuthTestEnv);

		// Replays the redirect that the external browser would normally perform, without
		// opening any browser nor binding any real port.
		// NB: this installs the router mock for the remainder of the test, which is also what
		// SECTION("InitiateAuthorizationURL") below wants (it must not bind a real port either).
		auto const RouteSpy = std::make_shared<FITwinAuthRouteSpy>();
		// Restores the production factory when this section ends.
		FITwinScopedHttpRouterMock const ScopedRouterMock(RouteSpy);

		// Isolate the refresh/access token files from the real application (and from parallel runs),
		// so that TryLoadRefreshToken cannot short-circuit CheckAuthorization with a cached token.
		FITwinAuthorizationManager::SetupTestMode(AuthTestEnv,
			FString::Printf(TEXT("_AuthCb_%d"), FPlatformProcess::GetCurrentProcessId()));

		// No external browser: StartAuthorizationInstance will only build the URL.
		FITwinAuthorizationManager::FExternalBrowserDisabler const BrowserDisabler;

		auto& AuthMngr = AdvViz::SDK::ITwinAuthManager::GetInstance(AuthTestEnv);
		if (!ensure(AuthMngr))
		{
			return false;
		}
		AuthMngr->ResetOverrideAccessToken();

		TestConnection->Logout();
		UTEST_FALSE("HasAccessToken before authorization", AuthMngr->HasAccessToken());
		UTEST_FALSE("IsAuthorizationInProgress before authorization", AuthMngr->IsAuthorizationInProgress());

		//---------------------------------------------------------------------------
		// The route is bound on the expected port/endpoint.
		//---------------------------------------------------------------------------
		UTEST_EQUAL("CheckAuthorization result",
			ServiceActor->CheckAuthStatus(), AdvViz::SDK::EITwinAuthStatus::InProgress);
		UTEST_TRUE("Route was bound", RouteSpy->IsBound());
		UTEST_EQUAL("Bound port", RouteSpy->Port,
			AdvViz::SDK::ITwinAuthManager::GetRedirectUriPort());
		UTEST_TRUE("Bound endpoint", RouteSpy->Endpoint == "/signin-callback");
		UTEST_TRUE("IsAuthorizationInProgress after CheckAuthorization",
			AuthMngr->IsAuthorizationInProgress());

		// The 'state' is unknown to the test (randomly generated inside CheckAuthorization), but it
		// is published in the authorization URL built by StartAuthorizationInstance.
		FString const AuthorizationURL(AuthMngr->GetCurrentAuthorizationURL().c_str());
		FString State;
		UTEST_TRUE("AuthorizationURL contains state",
			FParse::Value(*AuthorizationURL.Replace(TEXT("&"), TEXT(" ")), TEXT("state="), State));
		UTEST_FALSE("state is not empty", State.IsEmpty());

		//---------------------------------------------------------------------------
		// A redirect carrying a wrong 'state' must be ignored (CSRF protection).
		//---------------------------------------------------------------------------
		std::string Html = RouteSpy->Replay({ {"code", "AUTH_CODE_123"}, {"state", "wrong-state"} });
		UTEST_TRUE("Wrong state produces no success page", Html.empty());
		UTEST_FALSE("No token granted on wrong state", AuthMngr->HasAccessToken());

		// The route stays bound: a spoofed callback must not abort the pending sign-in.
		UTEST_TRUE("Route still bound after wrong state", RouteSpy->IsBound());
		UTEST_TRUE("Still in progress after wrong state", AuthMngr->IsAuthorizationInProgress());

		//---------------------------------------------------------------------------
		// A redirect carrying an error (and no refresh token available) must report it.
		//---------------------------------------------------------------------------
		Html = RouteSpy->Replay({
			{"error", "access_denied"},
			{"error_description", "User+declined+the+request"} });
		UTEST_TRUE("Error page mentions the error",
			Html.find("User declined the request") != std::string::npos);
		UTEST_FALSE("No token granted on error", AuthMngr->HasAccessToken());
		// The handler releases the port, so a new authorization can be initiated.
		UTEST_FALSE("Route unbound after error", RouteSpy->IsBound());
		UTEST_FALSE("Port released after error", AuthMngr->IsAuthorizationInProgress());

		//---------------------------------------------------------------------------
		// Second attempt: this time the redirect carries a valid code + state.
		//---------------------------------------------------------------------------
		// Redirect the token endpoint to the mock server.
		AuthMngr->SetIMSBaseUrlForTesting(Helper.GetServerUrl());

		auto const AuthObserver = std::make_shared<FITwinAuthObserverMock>();
		AuthMngr->AddObserver(AuthObserver.get());
		// NB: no scope-exit removal here - the observer must stay registered until the latent
		// command below has seen the result. It takes ownership of the cleanup.

		UTEST_EQUAL("CheckAuthorization result (2nd attempt)",
			AuthMngr->CheckAuthorization(), AdvViz::SDK::EITwinAuthStatus::InProgress);
		UTEST_EQUAL("Route was bound again", RouteSpy->BindCount, 2);
		UTEST_TRUE("Route bound for 2nd attempt", RouteSpy->IsBound());

		FString State2;
		UTEST_TRUE("2nd AuthorizationURL contains state",
			FParse::Value(*FString(AuthMngr->GetCurrentAuthorizationURL().c_str())
				.Replace(TEXT("&"), TEXT(" ")), TEXT("state="), State2));

		Html = RouteSpy->Replay({
			{"code", ITWINTEST_AUTH_CODE},
			{"state", TCHAR_TO_UTF8(*State2)} });
		UTEST_TRUE("Success page", Html.find("Sign in was successful") != std::string::npos);
		// The exchange is concluded as far as the loopback route is concerned.
		UTEST_FALSE("Route unbound after success", RouteSpy->IsBound());

		// The token request is asynchronous, and its response is processed in the game thread:
		// the remaining checks (and the cleanup) are deferred to a latent command.
		ADD_LATENT_AUTOMATION_COMMAND(FITwinWaitForAuthorizationDone(
			*this, AuthObserver, AuthMngr, AuthTestEnv,
			nullptr, TestConnection, ServiceActor,
			15. /*grace period*/));
	}

	SECTION("InitiateAuthorizationURL")
	{
		// Same remark as in previous section.
		// Use a 3rd environment to avoid conflicts with the latent  command above.
		constexpr AdvViz::SDK::EITwinEnvironment AuthURLTestEnv = AdvViz::SDK::EITwinEnvironment::Dev;

		// Start from a pristine manager.
		AdvViz::SDK::ITwinAuthManager::ResetInstanceForTesting(AuthURLTestEnv);

		FITwinAuthorizationManager::SetupTestMode(AuthURLTestEnv,
			FString::Printf(TEXT("_AuthURL_%d"), FPlatformProcess::GetCurrentProcessId()));
		FITwinAuthorizationManager::DeleteTokenFile(AuthURLTestEnv);

		// Dedicated instance, so that the shared 'WebServices' (Prod) - and the requests still in
		// flight from the previous sections - are left untouched.
		UITwinWebServices* AuthWebServices = NewObject<UITwinWebServices>();

		// Not strictly required (this section is synchronous, so no GC can occur), but it keeps the
		// ownership pattern uniform with the other authorization sections.
		AuthWebServices->AddToRoot();
		ON_SCOPE_EXIT{ AuthWebServices->RemoveFromRoot(); };

		AuthWebServices->SetEnvironment(static_cast<EITwinEnvironment>(AuthURLTestEnv));

		// Create a server connection for the same environment.
		UWorld* const TestWorld = FITwinAPITestHelperBase::GetTestWorld();
		if (!ensure(TestWorld))
		{
			return false;
		}
		AITwinServerConnection* const TestConnection = TestWorld->SpawnActor<AITwinServerConnection>();
		if (!ensure(TestConnection))
		{
			return false;
		}
		TestConnection->Environment = static_cast<EITwinEnvironment>(AuthURLTestEnv);
		UTEST_EQUAL("UrlPrefix", TestConnection->UrlPrefix(), TEXT("dev-"));

		// Partially test the authorization process.
		AuthWebServices->ResetOverrideAccessToken();

		auto& AuthMngr = AdvViz::SDK::ITwinAuthManager::GetInstance(
			static_cast<AdvViz::SDK::EITwinEnvironment>(AuthWebServices->GetEnvironment()));
		if (ensure(AuthMngr))
		{
			UTEST_FALSE("IsAuthorizationInProgress before initiate", AuthWebServices->IsAuthorizationInProgress());
		}

		UTEST_EQUAL("AccessTokenPtr", TestConnection->GetAccessTokenPtr(), AuthMngr->GetAccessToken());

		// By default, we should use the external browser for authorization. But for this partial test, we
		// will override it (no interaction should occur).
		UTEST_TRUE("UseExternalBrowser", AuthWebServices->UseExternalBrowser());
		AuthWebServices->SetUseExternalBrowser(false);

		// Restores the production factory when this section ends.
		FITwinScopedHttpRouterMock const ScopedRouterMock({});

		FString AuthorizationURL = AuthWebServices->InitiateAuthorizationURL();
		UTEST_TRUE("AuthorizationURL is not empty", !AuthorizationURL.IsEmpty());
		UTEST_TRUE("AuthorizationURL content", AuthorizationURL.Contains(TEXT("client_id="))
			&& AuthorizationURL.Contains(TEXT("scope=itwin-platform"))
			&& AuthorizationURL.Contains(TEXT("code_challenge_method=S256"))
			&& AuthorizationURL.Contains(TEXT("code_challenge="))
			&& AuthorizationURL.Contains(TEXT("response_type=code"))
		);

		if (ensure(AuthMngr))
		{
			UTEST_TRUE("IsAuthorizationInProgress after initiate", AuthWebServices->IsAuthorizationInProgress());
		}

		TestConnection->FillAuthorizationURL();
		UTEST_EQUAL("AuthorizationURL matches server connection", AuthorizationURL, TestConnection->AuthorizationURL);

		AuthMngr->Logout();
	}

	SECTION("CancelAuthorization")
	{
		// Dedicated environment: ITwinAuthManager is a per-environment singleton, and this section
		// logs out / resets tokens. Doing that on an environment used by another section would
		// break its in-flight requests (see the hang investigated previously).
		constexpr AdvViz::SDK::EITwinEnvironment CancelEnv = AdvViz::SDK::EITwinEnvironment::Dev;

		// Start from a pristine manager: the previous section used the same environment.
		AdvViz::SDK::ITwinAuthManager::ResetInstanceForTesting(CancelEnv);

		FITwinAuthorizationManager::SetupTestMode(CancelEnv,
			FString::Printf(TEXT("_CancelTest_%d"), FPlatformProcess::GetCurrentProcessId()));
		FITwinAuthorizationManager::DeleteTokenFile(CancelEnv);

		auto const Spy = std::make_shared<FITwinAuthRouteSpy>();
		// Restores the production factory when this section ends.
		FITwinScopedHttpRouterMock const ScopedRouterMock(Spy);

		auto const AuthMngr = AdvViz::SDK::ITwinAuthManager::GetInstance(CancelEnv);
		UTEST_TRUE("AuthManager instance", (bool)AuthMngr);

		auto const AuthObserver = std::make_shared<FITwinAuthObserverMock>();
		AuthMngr->AddObserver(AuthObserver.get());
		AuthMngr->SetIMSBaseUrlForTesting(Helper.GetServerUrl());

		// Disable the external browser, so CheckAuthorization only binds the route.
		FITwinAuthorizationManager::FExternalBrowserDisabler const NoBrowser;

		//---------------------------------------------------------------------------
		// Cancel *before* the redirect: the route must be released, and replaying the
		// redirect afterwards must have no effect.
		//---------------------------------------------------------------------------
		UTEST_TRUE("CheckAuthorization starts the process",
			AuthMngr->CheckAuthorization() == AdvViz::SDK::EITwinAuthStatus::InProgress);
		UTEST_TRUE("Route is bound", Spy->IsBound());
		UTEST_TRUE("IsAuthorizationInProgress", AuthMngr->IsAuthorizationInProgress());

		UTEST_TRUE("CancelAuthorization releases the route", AuthMngr->CancelAuthorization());
		UTEST_FALSE("Route is released", Spy->IsBound());
		UTEST_FALSE("IsAuthorizationInProgress after cancel", AuthMngr->IsAuthorizationInProgress());
		UTEST_FALSE("Observer not notified by a cancellation", AuthObserver->bDone.load());

		// Cancelling twice is harmless, and reports that nothing was left to cancel.
		UTEST_FALSE("Second CancelAuthorization is a no-op", AuthMngr->CancelAuthorization());

		// The stale redirect must not trigger a token exchange any more.
		Spy->Replay({ { "code", ITWINTEST_AUTH_CODE }, { "state", "staleState" } });
		UTEST_FALSE("No token from a stale redirect", AuthMngr->HasAccessToken());

		//---------------------------------------------------------------------------
		// Logout() must now cancel a pending authorization instead of refusing.
		//---------------------------------------------------------------------------
		UTEST_TRUE("CheckAuthorization restarts the process",
			AuthMngr->CheckAuthorization() == AdvViz::SDK::EITwinAuthStatus::InProgress);
		UTEST_TRUE("Route is bound again", Spy->IsBound());
		UTEST_TRUE("Logout cancels the pending authorization", AuthMngr->Logout());
		UTEST_FALSE("Route released by Logout", Spy->IsBound());

		//---------------------------------------------------------------------------
		// Cancel *after* the redirect, while the token exchange is in flight: the epoch
		// guard must discard the response.
		// Unreal dispatches http responses from the game thread tick, and this test body
		// runs on the game thread: no tick can occur between Replay() and Logout(), so the
		// response is guaranteed to still be in flight here.
		//---------------------------------------------------------------------------
		// Same ownership scheme as SECTION("AuthorizationCallback"): raw pointers here, the
		// latent command below takes the strong references that outlive RunTest.
		UWorld* const TestWorld = FITwinAPITestHelperBase::GetTestWorld();
		if (!ensure(TestWorld))
		{
			return false;
		}
		UITwinWebServices* const TestWebServices = NewObject<UITwinWebServices>(TestWorld);
		TestWebServices->AddToRoot();
		TestWebServices->SetEnvironment(static_cast<EITwinEnvironment>(CancelEnv));

		// Create a server connection for the same environment.
		AITwinServerConnection* const TestConnection = TestWorld->SpawnActor<AITwinServerConnection>();
		if (!ensure(TestConnection))
		{
			return false;
		}
		TestConnection->Environment = static_cast<EITwinEnvironment>(CancelEnv);

		AuthObserver->bDone = false;
		UTEST_TRUE("CheckAuthorization for the in-flight case",
			AuthMngr->CheckAuthorization() == AdvViz::SDK::EITwinAuthStatus::InProgress);
		UTEST_TRUE("Route bound for the in-flight case", Spy->IsBound());

		// Capture the genuine state from the authorization URL, so the redirect is accepted.
		FString const AuthURL(AuthMngr->GetCurrentAuthorizationURL().c_str());
		FString State;
		UTEST_TRUE("Extract state from the authorization URL",
			FParse::Value(*AuthURL.Replace(TEXT("&"), TEXT(" ")), TEXT("state="), State));

		// This one *is* accepted: it starts the /connect/token exchange and unbinds the route.
		Spy->Replay({ { "code", ITWINTEST_AUTH_CODE }, { "state", TCHAR_TO_UTF8(*State) } });
		UTEST_FALSE("Route released by the accepted redirect", Spy->IsBound());

		// Cancel while the exchange is running: this bumps the epoch.
		AuthMngr->Logout();

		ADD_LATENT_AUTOMATION_COMMAND(FITwinCheckAuthorizationCancelled(
			*this, AuthObserver, AuthMngr, CancelEnv,
			TestWebServices, TestConnection, nullptr,
			3. /*grace period*/));
	}

	ADD_LATENT_AUTOMATION_COMMAND(FNUTWaitForAuthMockServerResponse(Observer));

	return true;
}

#endif // WITH_TESTS
