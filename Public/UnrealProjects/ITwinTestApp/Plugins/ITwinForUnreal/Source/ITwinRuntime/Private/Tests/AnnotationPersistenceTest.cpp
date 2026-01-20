/*--------------------------------------------------------------------------------------+
|
|     $Source: AnnotationPersistenceTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#if WITH_TESTS

#include "WebTestHelpers.h"

#include <Annotations/ITwinAnnotation.h>

#include <Misc/LowLevelTestAdapter.h>

#if WITH_EDITOR
#include <Editor/EditorEngine.h>
#else
#include <Tests/AutomationCommon.h>
#endif

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/ITwinAPI/ITwinAuthManager.h>
#	include <Core/Network/http.h>
#	include <Core/Visualization/AnnotationsManager.h>
#include <Compil/AfterNonUnrealIncludes.h>


#define TEST_DECO_ID "679d2cc2ba6b5b82ce6e1ec5"


/// Mock server implementation for annotation persistence
class FAnnotationPersistenceMockServer : public FITwinMockServerBase
{
public:
	static std::unique_ptr<httpmock::MockServer> MakeServer(
		unsigned startPort, unsigned tryCount = 1000);

	explicit FAnnotationPersistenceMockServer(int port) : FITwinMockServerBase(port) {}

	virtual Response responseHandler(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) override
	{
		if (url.find("/arg_test") != std::string::npos)
		{
			return ProcessArgTest(urlArguments);
		}
		if (url.ends_with("/" TEST_DECO_ID "/annotations"))
		{
			return ProcessAnnotationsTest(url, method, data, urlArguments, headers);
		}
		return Response(cpr::status::HTTP_NOT_FOUND,
			std::string("Page not found: ") + url);
	}

	virtual bool PostCondition() const override
	{
		return bHasAddedAnnotation;
	}

private:
	mutable bool bHasAddedAnnotation = false;


#define CHECK_DECORATION_HEADERS()										\
{																		\
	const int HeaderStatus = CheckRequiredHeaders(headers,				\
		{{ "accept", "application/json" },								\
		 { "Content-Type", "application/json; charset=UTF-8" },			\
		 { "Authorization", "Bearer " ITWINTEST_ACCESS_TOKEN } });		\
	if (HeaderStatus != cpr::status::HTTP_OK)							\
	{																	\
		return Response(HeaderStatus, "Error in headers.");				\
	}																	\
}

	/// Process /annotations requests
	Response ProcessAnnotationsTest(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) const
	{
		CHECK_DECORATION_HEADERS();

		if (method == "GET")
		{
			return Response(cpr::status::HTTP_OK,
				"{\"total_rows\":1,\"rows\":[{\"position\":[0.0,0.0,0.0],\"text\":\"Un \u00E9t\u00E9 h\u00E9lv\u00E8te\",\"colorTheme\":\"Dark\",\"displayMode\":\"Marker and label\",\"name\":\"\",\"id\":\"6908dd0d36638d1a3b3db1be\"}],\"_links\":{\"self\":\"https://itwindecoration-eus.bentley.com/advviz/v1/decorations/679d2cc2ba6b5b82ce6e1ec5/annotations?$skip=0\u0026$top=1000\"}}"
			);
		}
		if (method == "POST")
		{
			if (data != "{\"annotations\":[{\"position\":[0.0,0.0,0.0],\"text\":\"Un \u00E9t\u00E9 h\u00E9lv\u00E8te\",\"name\":\"\",\"colorTheme\":\"Dark\",\"displayMode\":\"Marker and label\"}]}")
			{
				return Response(cpr::status::HTTP_EXPECTATION_FAILED, "Unexpected new annotation");
			}
			bHasAddedAnnotation = true;
			return Response(cpr::status::HTTP_CREATED,
				"{\"ids\":[\"6908dd0d36638d1a3b3db1be\"]}"
			);
		}
		return Response(cpr::status::HTTP_NOT_FOUND, "Page not found.");
	}
};

/*static*/
std::unique_ptr<httpmock::MockServer> FAnnotationPersistenceMockServer::MakeServer(
	unsigned startPort, unsigned tryCount /*= 1000*/)
{
	return httpmock::getFirstRunningMockServer<FAnnotationPersistenceMockServer>(startPort, tryCount);
}



class FAnnotationPersistenceTestHelper : public FITwinAPITestHelperBase
{
public:
	using AnnotationsManager = AdvViz::SDK::AnnotationsManager;

	static FAnnotationPersistenceTestHelper& Instance();
	~FAnnotationPersistenceTestHelper();

	std::shared_ptr<AnnotationsManager> GetAnnotationsMngr() const { return AnnotationsMngr; }
	std::shared_ptr<AdvViz::SDK::Http> GetHttp() const { return Http; }

protected:
	virtual bool DoInit(AdvViz::SDK::EITwinEnvironment) override;
	virtual void DoCleanup() override;

private:
	FAnnotationPersistenceTestHelper() {}

	std::shared_ptr<AnnotationsManager> AnnotationsMngr;
	std::shared_ptr<AdvViz::SDK::Http> Http;
};

/*static*/
FAnnotationPersistenceTestHelper& FAnnotationPersistenceTestHelper::Instance()
{
	static FAnnotationPersistenceTestHelper instance;
	return instance;
}

bool FAnnotationPersistenceTestHelper::DoInit(AdvViz::SDK::EITwinEnvironment Env)
{
	/// Port number server is tried to listen on
	/// Number is being incremented while free port has not been found.
	static constexpr int DEFAULT_SERVER_PORT = 8100;

	if (!InitServer(FAnnotationPersistenceMockServer::MakeServer(DEFAULT_SERVER_PORT)))
	{
		return false;
	}

	AnnotationsMngr = std::make_shared<AnnotationsManager>();

	// Use our local mock server's URL
	Http.reset(AdvViz::SDK::Http::New());
	Http->SetBaseUrl(GetServerUrl().c_str());
	Http->SetAccessToken(AdvViz::SDK::ITwinAuthManager::GetInstance(Env)->GetAccessToken());

	AnnotationsMngr->SetHttp(Http);

	return true;
}

void FAnnotationPersistenceTestHelper::DoCleanup()
{

}

FAnnotationPersistenceTestHelper::~FAnnotationPersistenceTestHelper()
{
	Cleanup();
}

#if WITH_EDITOR
extern UNREALED_API class UEditorEngine* GEditor;
#endif

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FAnnotationPersistenceTest, FAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.AnnotationPersistence", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/// Remark: there is already a test (in SDK/Core/Annotations/Tests/AnnotationTest.cpp covering most of the
/// aspects of annotations persistence. This test was added to test specifically the handling of unicode
/// characters and their conversion from/to FString.
bool FAnnotationPersistenceTest::RunTest(const FString& /*Parameters*/)
{
	FAnnotationPersistenceTestHelper& Helper = FAnnotationPersistenceTestHelper::Instance();
	if (!Helper.Init())
	{
		return false;
	}

	auto pAnnotationsMngr = Helper.GetAnnotationsMngr();
	auto& AnnotationsMngr = *pAnnotationsMngr;

	const std::string url = Helper.GetServerUrl();

#if WITH_EDITOR
	auto* const World = GEditor->GetEditorWorldContext().World();
#else
	auto* const World = AutomationCommon::GetAnyGameWorld();
#endif

	SECTION("Add Annotation with unicode characters and Save")
	{
		UTEST_TRUE(TEXT("Check world"), World != nullptr);

		// Add a new material definition
		const FString TextWithAccents = TEXT("Un \u00E9t\u00E9 h\u00E9lv\u00E8te");
		AITwinAnnotation* Annot = World->SpawnActor<AITwinAnnotation>(AITwinAnnotation::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		UTEST_TRUE(TEXT("Spawn annotation"), Annot != nullptr);

		Annot->SetText(FText::FromString(TextWithAccents));
		AnnotationsMngr.AddAnnotation(Annot->GetAVizAnnotation());
		UTEST_TRUE(TEXT("DB Invalidation"), AnnotationsMngr.HasAnnotationToSave());

		AnnotationsMngr.SaveDataOnServerDS(TEST_DECO_ID);
		UTEST_FALSE(TEXT("DB up-to-date"), AnnotationsMngr.HasAnnotationToSave());

		// create annotations copy by fetching previous annotations from server
		auto AnnotationManager2 = std::make_shared<AdvViz::SDK::AnnotationsManager>();
		AnnotationManager2->SetHttp(Helper.GetHttp());
		AnnotationManager2->LoadDataFromServerDS(TEST_DECO_ID);
		UTEST_FALSE(TEXT("DB up-to-date after loading"), AnnotationManager2->HasAnnotationToSave());

		auto const& LoadedAnnotations = AnnotationManager2->GetAnnotations();
		UTEST_TRUE(TEXT("Load annotations"), LoadedAnnotations.size() == 1);
		AITwinAnnotation* LoadedAnnot = World->SpawnActor<AITwinAnnotation>(AITwinAnnotation::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		UTEST_TRUE(TEXT("Spawn annotation for load"), LoadedAnnot != nullptr);
		LoadedAnnot->LoadAVizAnnotation(LoadedAnnotations[0]);
		UTEST_TRUE(TEXT("Compare text"), LoadedAnnot->GetText().ToString() == TextWithAccents);
	}

	UTEST_TRUE("Post-Condition", Helper.PostCondition());

	return true;
}

#endif // WITH_TESTS
