/*--------------------------------------------------------------------------------------+
|
|     $Source: VizKeyframeTests.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "../Visualization.h"
#include <filesystem>

#include <catch2/catch_all.hpp>
#include <Core/Network/Tests/HttpMock.h>
#include <Core/Network/Tests/AsyncTestHelpers.h>


void SetDefaultConfig();

namespace
{
	using namespace AdvViz::SDK;

	// Payloads shared by the asynchronous tests (same content as the synchronous ones).

	// GET /advviz/v1/animations -> 2 animations
	static const std::string kAnimationListPayload =
		"{\"total_rows\":2,\"rows\":["
		"{\"name\":\"MyAnim2\",\"itwinid\":\"904a89f7-b63c-4ae1-a223-88517bd4bb08\",\"id\":\"67a217484ad6dc296ad8adea\"},"
		"{\"name\":\"MyAnim\",\"itwinid\":\"904a89f7-b63c-4ae1-a223-88517bd4bb08\",\"id\":\"67a217484ad6dc296ad8adeb\"}"
		"],\"_links\":{\"self\":\"http:///advviz/v1/animations?iTwinId=904a89f7-b63c-4ae1-a223-88517bd4bb08\u0026$skip=0\u0026$top=1000\"}}";

	// POST .../query/animationKeyFramesBBox -> 1 keyframe info id
	static const std::string kQueryKeyframesInfosPayload =
		"{\"ids\":[\"67a0e124f9f9158c2e60d000\"]}";

	static const std::string kITwinId = "904a89f7-b63c-4ae1-a223-88517bd4bb08";
	static const std::string kKeyframesInfoId = "67a0e124f9f9158c2e60d000";

	// POST /advviz/v1/animations
	static const std::string kCreateAnimationPayload =
		"{\"name\":\"MyAnim2\",\"itwinid\":\"904a89f7-b63c-4ae1-a223-88517bd4bb08\",\"id\":\"67a0e122f9f9158c2e60d7ba\"}";

	// GET /advviz/v1/animations/animationKeyFramesInfos
	static const std::string kAnimationKeyFramesInfosPayload = kCreateAnimationPayload;

	// POST .../animationKeyFramesInfos
	static const std::string kSaveKeyframeInfoPayload =
		"{\"animationKeyFramesInfos\":[{\"objectId\":\"MyObject1\",\"type\":\"baked\",\"keyframeInterval\":0.033333333333,"
		"\"startTime\":0.5,\"keyframeCount\":35,\"chunckSize\":30,\"states\":[\"walking\",\"standing\"],"
		"\"tags\":[\"female\",\"old\"],\"id\":\"67a0e124f9f9158c2e60d7bc\"}]}";

	// POST .../animationKeyFramesChunks
	static const std::string kSaveChunkPayload = "{\"ids\":[\"67a0e124f9f9158c2e60daaa\"]}";
	static const std::string kSaveChunkPayload2 = "{\"ids\":[\"67a0e124f9f9158c2e60dbbb\"]}";

	static const std::string kSavedKeyframeInfoId = "67a0e124f9f9158c2e60d7bc";
	static const std::string kSavedChunkId = "67a0e124f9f9158c2e60daaa";
	static const std::string kSavedChunkId2 = "67a0e124f9f9158c2e60dbbb";

	// Async callbacks run on a worker thread: always wait with a timeout.
	static constexpr int kAsyncTimeoutSeconds = 10;

	//! Whether the test drives the synchronous or the asynchronous SDK entry points.
	enum class ESaveMode { Sync, Async };

	//! Common setup shared by all keyframe tests: log channels, config, mock server and access token.
	HTTPMock& InitKeyframeTest()
	{
		Tools::CreateAdvVizLogChannels();
		SetDefaultConfig();
		HTTPMock* mock = GetHttpMock();
		REQUIRE(mock != nullptr);
		std::shared_ptr<ThreadSafeAccessToken> p = std::make_shared<ThreadSafeAccessToken>();
		p->Set("abcd");
		GetDefaultHttp()->SetAccessToken(p);
		return *mock;
	}

	//! Registers a one-shot response, and unregisters it at end of scope.
	class [[nodiscard]] ScopedResponse
	{
	public:
		ScopedResponse(HTTPMock& mock, const HTTPMock::RequestKey& key, long code, const std::string& payload)
			: mock_(mock), key_(key)
		{
			mock_.SetResponseFunction(key_, [code, payload] {
				return HTTPMock::Response2(code, std::string(payload));
			});
		}
		~ScopedResponse() { mock_.ResetResponseFunction(key_); }
	private:
		HTTPMock& mock_;
		HTTPMock::RequestKey key_;
	};

	//! Creates the animation used by the save tests (always synchronous, there is no async variant).
	IAnimationKeyframePtr CreateMockedAnimation(HTTPMock& mock)
	{
		ScopedResponse createResp(mock, std::pair("POST", "/advviz/v1/animations"),
			201, kCreateAnimationPayload);
		ScopedResponse infosResp(mock, std::pair("GET", "/advviz/v1/animations/animationKeyFramesInfos"),
			200, kAnimationKeyFramesInfosPayload);

		auto ret = CreateAnimationKeyframe(kITwinId, "MyAnim2");
		REQUIRE((bool)ret == true);
		return ret.value();
	}

	//! Saves the keyframe info either synchronously or asynchronously, and waits for completion.
	void SaveKeyframeInfo(IAnimationKeyframeInfo& info, ESaveMode mode)
	{
		if (mode == ESaveMode::Sync)
		{
			auto ret = info.Save(GetDefaultHttp());
			REQUIRE((bool)ret == true);
		}
		else
		{
			std::atomic_bool bSaved = false;
			IAnimationKeyframeInfo::Id savedId;
			info.AsyncSave(GetDefaultHttp(), [&](const IAnimationKeyframeInfo::Id& id) {
				savedId = id;
				bSaved = true;
			});
			REQUIRE(WaitForAsyncTask(bSaved, kAsyncTimeoutSeconds));
			CHECK(savedId == IAnimationKeyframeInfo::Id(kSavedKeyframeInfoId));
		}
	}

	//! Saves one chunk either synchronously or asynchronously, and waits for completion.
	void SaveKeyframeChunk(IAnimationKeyframeChunk& chunk, const std::string& animationId,
		const std::string& infoId, ESaveMode mode)
	{
		if (mode == ESaveMode::Sync)
		{
			auto ret = chunk.Save(GetDefaultHttp(), animationId, infoId);
			REQUIRE((bool)ret == true);
		}
		else
		{
			std::atomic_bool bSaved = false;
			long httpResult = 0;
			chunk.AsyncSave(GetDefaultHttp(), animationId, infoId, [&](long result) {
				httpResult = result;
				bSaved = true;
			});
			REQUIRE(WaitForAsyncTask(bSaved, kAsyncTimeoutSeconds));
			CHECK(httpResult == 201);
		}
	}
}


TEST_CASE("SaveKeyframeAnimation") {
	using namespace AdvViz::SDK;

	HTTPMock& mock = InitKeyframeTest();
	// run the whole scenario twice: once through the sync API, once through the async one
	const ESaveMode saveMode = GENERATE(ESaveMode::Sync, ESaveMode::Async);
	CAPTURE(saveMode == ESaveMode::Async);

	IAnimationKeyframePtr animation = CreateMockedAnimation(mock);
	auto animKeyFrameLocked = animation->GetAutoLock();
	IAnimationKeyframe* animKeyFrame = animKeyFrameLocked.GetPtr();
	REQUIRE(animKeyFrame != nullptr);
	const std::string animId = (std::string)animKeyFrame->GetId();
	const std::string animUrlPath = "/advviz/v1/animations/" + animId;

	SECTION("Create Animation KeyframeInfo") {
		const std::string objId("MyObject1");
		IAnimationKeyframeInfoPtr keyframeInfoPtr = animKeyFrame->AddAnimationKeyframeInfo(objId);
		REQUIRE(keyframeInfoPtr != nullptr);
		auto keyframeInfoLocked = keyframeInfoPtr->GetAutoLock();
		IAnimationKeyframeInfo* keyframeInfo = keyframeInfoLocked.GetPtr();
		REQUIRE(keyframeInfo != nullptr);
		CHECK(keyframeInfo->GetObjectId() == objId);
		keyframeInfo->SetType("baked");
		keyframeInfo->SetChunkSize(60);
		std::vector<std::string> states = { "hidden", "walking", "standing" };
		keyframeInfo->SetStates(states);
		std::vector<std::string> tags = { "man", "old" };
		keyframeInfo->SetTags(tags);

		{
			ScopedResponse infoResp(mock, std::pair("POST", animUrlPath + "/animationKeyFramesInfos"),
				201, kSaveKeyframeInfoPayload);
			SaveKeyframeInfo(*keyframeInfo, saveMode);
		}
		CHECK(keyframeInfo->GetId() == IAnimationKeyframeInfo::Id(kSavedKeyframeInfoId));

		SECTION("Create Animation KeyframeChunk") {
			std::vector<float> translations = {
				0,0,1,
				0,0,2
			};
			std::vector<float> quaternions = {
				0,0,0, 1,
				0,0,0, 2
			};

			const HTTPMock::RequestKey chunkKey = std::pair("POST", animUrlPath + "/animationKeyFramesChunks");
			IAnimationKeyframeChunkPtr keyframeChunkPtr2;
			{ // note: reduce lock scope because chunks need to be unlocked before keyframeInfo::Save
				ScopedResponse chunkResp(mock, chunkKey, 201, kSaveChunkPayload);

				IAnimationKeyframeChunkPtr keyframeChunkPtr = keyframeInfo->CreateChunk();
				{
					auto keyframeChunkLocked = keyframeChunkPtr->GetAutoLock();
					auto chunk = keyframeChunkLocked.GetPtr();
					REQUIRE(chunk != nullptr);
					chunk->SetTranslations(translations);
					chunk->SetQuaternions(quaternions);
					SaveKeyframeChunk(*chunk, animId, (std::string)keyframeInfo->GetId(), saveMode);
					CHECK(chunk->GetId() == IAnimationKeyframeChunk::Id(kSavedChunkId));
				}

				keyframeChunkPtr2 = keyframeInfo->CreateChunk();
				auto keyframeChunkLocked2 = keyframeChunkPtr2->GetAutoLock();
				auto chunk2 = keyframeChunkLocked2.GetPtr();
				chunk2->SetTranslations(translations);
				chunk2->SetQuaternions(quaternions);
			}

			// the second chunk is saved through the info (sync API only: AsyncSave does not save chunks)
			ScopedResponse chunkResp2(mock, chunkKey, 201, kSaveChunkPayload2);
			auto ret = keyframeInfo->Save(GetDefaultHttp());
			REQUIRE((bool)ret == true);
			auto keyframeChunkLocked2 = keyframeChunkPtr2->GetAutoLock();
			CHECK(keyframeChunkLocked2->GetId() == IAnimationKeyframeChunk::Id(kSavedChunkId2));
		}
	}
}

TEST_CASE("LoadKeyframeAnimation") {
	using namespace AdvViz::SDK;

	Tools::CreateAdvVizLogChannels();

	SetDefaultConfig();
	HTTPMock* mock = GetHttpMock();
	REQUIRE(mock != nullptr);

	std::shared_ptr<ThreadSafeAccessToken> p = std::make_shared<ThreadSafeAccessToken>();
	p->Set("abcd");
	GetDefaultHttp()->SetAccessToken(p);

	using RequestKey = HTTPMock::RequestKey;

	std::shared_ptr<IAnimationKeyframe> animKeyFrame;
	SECTION("LoadAnimationKeyframe") {
		RequestKey respKey2 = std::pair("GET", "/advviz/v1/animations");
		mock->SetResponseFunction(respKey2, [] {
			std::string s = kAnimationListPayload;
			return HTTPMock::Response2(200, s);
		});

		std::vector<IAnimationKeyframePtr> v = GetITwinAnimationKeyframes(kITwinId);
		CHECK(v.size() == 2);
		mock->ResetResponseFunction(respKey2);
		if (!v.empty())
		{
			auto keyFrameLocked = v[0]->GetAutoLock();
			IAnimationKeyframe* animationKeyframe = keyFrameLocked.GetPtr();
			auto respKey3 = std::pair("POST", "/advviz/v1/animations/"+ (std::string)animationKeyframe->GetId() + "/query/animationKeyFramesBBox");
			mock->SetResponseFunction(respKey3, [] {
				std::string s = kQueryKeyframesInfosPayload;
				return HTTPMock::Response2(200, s);
			});

			std::vector<BoundingBox> boundingBoxes = { {{0,0,0},{1,1,1}}, {{2,2,2},{3,3,3}} };
			TimeRange timeRange = { 0.0, 1.0 };
			auto ret = animationKeyframe->QueryKeyframesInfos(boundingBoxes, timeRange);
			CHECK(ret.has_value());
			CHECK(ret.value().size() == 1);
			mock->ResetResponseFunction(respKey3);
			if (ret)
			{
				std::string animUrlPath = "/advviz/v1/animations/67a217484ad6dc296ad8adea/";
				{
					RequestKey respKey = std::pair("GET", animUrlPath + "animationKeyFramesInfos/67a0e124f9f9158c2e60d000");
					mock->SetResponseFunction(respKey, [] {
						std::string s = "{\"objectId\":\"MyObject2\",\"type\":\"baked\",\"keyframeInterval\":0.03333333333333,\"startTime\":0,\"keyframeCount\":10,\"chunckSize\":30,\"states\":[\"walking + luggage\",\"standing\"],\"tags\":[\"male\",\"young\"],\"id\":\"67a0e124f9f9158c2e60d000\"}";
						return HTTPMock::Response2(200, s);
					});

					mock->SetResponseFunction(std::pair("POST", animUrlPath + "query/animationKeyFramesChunks"), [] {
						std::string s = "{\"ids\":[\"aaaabbbb\",\"ccccdddd\"]}";
						return HTTPMock::Response2(200, s);
					});

					REQUIRE((*ret)[0] == "67a0e124f9f9158c2e60d000");
					auto keyframesInfoId = (*ret)[0];
					auto ret2 = animationKeyframe->LoadKeyframesInfo(keyframesInfoId);

					CHECK(ret2.has_value() == true);
					mock->ResetResponseFunction(respKey);

					IAnimationKeyframeInfoPtr keyframeInfoPtr = ret2.value();
					REQUIRE(keyframeInfoPtr != nullptr);
					auto keyframeInfoLocked = keyframeInfoPtr->GetAutoLock();
					IAnimationKeyframeInfo* keyframesInfo = keyframeInfoLocked.GetPtr();
					CHECK(keyframesInfo->GetChunkCount() == 2);
					auto lockChunck0 = keyframesInfo->GetChunk(0)->GetAutoLock();
					auto lockChunck1 = keyframesInfo->GetChunk(1)->GetAutoLock();
					CHECK(lockChunck0->GetId() == "aaaabbbb");
					CHECK(lockChunck1->GetId() == "ccccdddd");

					auto keyframeChunkLocked = keyframesInfo->GetChunk(0)->GetAutoLock();
					IAnimationKeyframeChunk* chuck = keyframeChunkLocked.GetPtr();
					CHECK(chuck->IsFullyLoaded() == false);

					respKey3 = std::pair("GET", animUrlPath + "animationKeyFramesChunks/aaaabbbb");
					mock->SetResponseFunction(respKey3, [] {
						std::string s = "{\"chunkId\":0,\"animationKeyFramesInfoId\":\"67bef7c1f831f091186335d2\",\"translations\":[0,0,1,0.1,0,1,0.2,0,1,0.3,0,1,0.4,0,1,0.5,0,1,0.6,0,1,0.7,0,1,0.8,0,1,0.9,0,1,1,0,1,1.1,0,1,1.2,0,1,1.3,0,1,1.4,0,1,1.5,0,1,1.6,0,1,1.7,0,1,1.8,0,1,1.9,0,1,2,0,1,2.1,0,1,2.2,0,1,2.3,0,1,2.4,0,1,2.5,0,1,2.6,0,1,2.7,0,1,2.8,0,1,2.9,0,1],\"quaternions\":[0,0,2,1,0,0.1,2,1,0,0.2,2,1,0,0.3,2,1,0,0.4,2,1,0,0.5,2,1,0,0.6,2,1,0,0.7,2,1,0,0.8,2,1,0,0.9,2,1,0,1,2,1,0,1.1,2,1,0,1.2,2,1,0,1.3,2,1,0,1.4,2,1,0,1.5,2,1,0,1.6,2,1,0,1.7,2,1,0,1.8,2,1,0,1.9,2,1,0,2,2,1,0,2.1,2,1,0,2.2,2,1,0,2.3,2,1,0,2.4,2,1,0,2.5,2,1,0,2.6,2,1,0,2.7,2,1,0,2.8,2,1,0,2.9,2,1],\"boundingBox\":{\"min\":{\"x\":0,\"y\":0,\"z\":1},\"max\":{\"x\":2.9,\"y\":0,\"z\":1}},\"timeRange\":{\"begin\":0.5,\"end\":1.4999999999999},\"id\":\"aaaabbbb\"}";
						return HTTPMock::Response2(200, s);
					});

					auto ret3 = chuck->Load();
					CHECK(ret3.has_value() == true);
					auto &trans = chuck->GetTranslations();
					CHECK(trans.size() == 90);

					respKey2 = std::pair("POST", animUrlPath + "query/animationKeyFrames");
					mock->SetResponseFunction(respKey2, [] {
						std::string s = "{\"translations\":[0,0,1,0.1,0,1,0.2,0,1,0.3,0,1,0.4,0,1,0.5,0,1,0.6,0,1,0.7,0,1,0.8,0,1,0.9,0,1],\"quaternions\":[0,0,2,1,0,0.1,2,1,0,0.2,2,1,0,0.3,2,1,0,0.4,2,1,0,0.5,2,1,0,0.6,2,1,0,0.7,2,1,0,0.8,2,1,0,0.9,2,1], \"boundingBox\":{\"min\":{\"x\":0,\"y\":0,\"z\":0}, \"max\":{\"x\":10,\"y\":10,\"z\":10}}, \"timeRange\":{\"begin\":0,\"end\":100} }";
						return HTTPMock::Response2(200, s);
					});

					IAnimationKeyframeInfo::TimelineResult result;
					auto ret4 = keyframesInfo->QueryKeyframes(result, 0.0, 1.0);
					CHECK(ret4.has_value() == true);
					REQUIRE(result.translations.size() == 30);
				}
			}
		}
	}
}

TEST_CASE("AsyncLoadKeyframeAnimation") {
	using namespace AdvViz::SDK;

	Tools::CreateAdvVizLogChannels();

	SetDefaultConfig();
	HTTPMock* mock = GetHttpMock();
	REQUIRE(mock != nullptr);

	std::shared_ptr<ThreadSafeAccessToken> p = std::make_shared<ThreadSafeAccessToken>();
	p->Set("abcd");
	GetDefaultHttp()->SetAccessToken(p);

	using RequestKey = HTTPMock::RequestKey;

	SECTION("AsyncGetITwinAnimationKeyframes") {
		RequestKey respKeyList = std::pair("GET", "/advviz/v1/animations");
		mock->SetResponseFunction(respKeyList, [] {
			return HTTPMock::Response2(200, std::string(kAnimationListPayload));
		});

		std::mutex resultMutex;
		std::vector<IAnimationKeyframePtr> animations;
		std::atomic_bool bFinished = false;
		bool bFinishedOk = false;

		AsyncGetITwinAnimationKeyframes(kITwinId,
			[&resultMutex, &animations](IAnimationKeyframePtr& anim) {
				std::lock_guard<std::mutex> lock(resultMutex);
				animations.push_back(anim);
			},
			[&bFinished, &bFinishedOk](AdvViz::expected<void, std::string> const& ret) {
				bFinishedOk = ret.has_value();
				bFinished = true;
			});

		REQUIRE(WaitForAsyncTask(bFinished, kAsyncTimeoutSeconds));
		CHECK(bFinishedOk);

		// reset only once the worker thread is done
		mock->ResetResponseFunction(respKeyList);

		std::lock_guard<std::mutex> lock(resultMutex);
		REQUIRE(animations.size() == 2);

		SECTION("AsyncQueryKeyframesInfos") {
			auto keyFrameLocked = animations[0]->GetAutoLock();
			IAnimationKeyframe* animationKeyframe = keyFrameLocked.GetPtr();
			REQUIRE(animationKeyframe != nullptr);

			RequestKey respKeyBBox = std::pair("POST",
				"/advviz/v1/animations/" + (std::string)animationKeyframe->GetId() + "/query/animationKeyFramesBBox");
			mock->SetResponseFunction(respKeyBBox, [] {
				return HTTPMock::Response2(200, std::string(kQueryKeyframesInfosPayload));
			});

			std::vector<BoundingBox> boundingBoxes = { {{0,0,0},{1,1,1}}, {{2,2,2},{3,3,3}} };
			TimeRange timeRange = { 0.0, 1.0 };

			auto data = MakeSharedLockableData<std::set<IAnimationKeyframeInfo::Id>>();
			std::atomic_bool bQueryDone = false;
			long queryHttpCode = 0;
			std::set<IAnimationKeyframeInfo::Id> receivedIds;

			// note: the set is already locked by the SDK when the callback is invoked, do not re-lock it
			auto ret = animationKeyframe->AsyncQueryKeyframesInfos(data,
				[&](long httpCode, std::set<IAnimationKeyframeInfo::Id>& ids) {
					queryHttpCode = httpCode;
					receivedIds = ids;
					bQueryDone = true;
				},
				boundingBoxes, timeRange);
			REQUIRE(ret.has_value());

			REQUIRE(WaitForAsyncTask(bQueryDone, kAsyncTimeoutSeconds));
			CHECK(queryHttpCode == 200);

			mock->ResetResponseFunction(respKeyBBox);

			REQUIRE(receivedIds.size() == 1);
			CHECK(*receivedIds.begin() == IAnimationKeyframeInfo::Id(kKeyframesInfoId));

			auto dataLocked = data->GetAutoLock();
			CHECK(dataLocked.Get().size() == 1);
		}
	}
}

namespace
{
	// DELETE .../animationKeyFramesChunks and .../animationKeyFramesInfos answer with the deleted ids
	static const std::string kDeleteChunkPayload = "{\"ids\":[\"67a0e124f9f9158c2e60daaa\"]}";
	static const std::string kDeleteInfoPayload = "{\"ids\":[\"67a0e124f9f9158c2e60d7bc\"]}";
	// DELETE /advviz/v1/animations/<id>
	static const std::string kDeleteAnimationPayload = "{\"id\":\"67a0e122f9f9158c2e60d7ba\"}";

	//! Creates a keyframe info with one saved chunk, ready to be deleted.
	//! Returns the info, the animation stays owned by the caller.
	IAnimationKeyframeInfoPtr CreateSavedKeyframeInfo(HTTPMock& mock, IAnimationKeyframe& animKeyFrame,
		const std::string& animUrlPath)
	{
		std::string objId("MyObject1");
		IAnimationKeyframeInfoPtr keyframeInfoPtr = animKeyFrame.AddAnimationKeyframeInfo(objId);
		REQUIRE(keyframeInfoPtr != nullptr);

		auto keyframeInfoLocked = keyframeInfoPtr->GetAutoLock();
		IAnimationKeyframeInfo* keyframeInfo = keyframeInfoLocked.GetPtr();
		REQUIRE(keyframeInfo != nullptr);
		keyframeInfo->SetType("baked");
		keyframeInfo->SetChunkSize(60);

		{
			ScopedResponse infoResp(mock, std::pair("POST", animUrlPath + "/animationKeyFramesInfos"),
				201, kSaveKeyframeInfoPayload);
			auto ret = keyframeInfo->Save(GetDefaultHttp(), false /*bChunks*/);
			REQUIRE((bool)ret == true);
		}

		{ // one saved chunk, so that DeleteChunk has something to send to the server
			ScopedResponse chunkResp(mock, std::pair("POST", animUrlPath + "/animationKeyFramesChunks"),
				201, kSaveChunkPayload);
			IAnimationKeyframeChunkPtr chunkPtr = keyframeInfo->CreateChunk();
			auto chunkLocked = chunkPtr->GetAutoLock();
			auto chunk = chunkLocked.GetPtr();
			REQUIRE(chunk != nullptr);
			chunk->SetTranslations({ 0,0,1, 0,0,2 });
			chunk->SetQuaternions({ 0,0,0,1, 0,0,0,2 });
			auto ret = chunk->Save(GetDefaultHttp(), (std::string)animKeyFrame.GetId(),
				(std::string)keyframeInfo->GetId());
			REQUIRE((bool)ret == true);
		}

		REQUIRE(keyframeInfo->GetChunkCount() == 1);
		return keyframeInfoPtr;
	}
}

TEST_CASE("DeleteKeyframeAnimation") {
	using namespace AdvViz::SDK;

	HTTPMock& mock = InitKeyframeTest();

	IAnimationKeyframePtr animation = CreateMockedAnimation(mock);
	auto animKeyFrameLocked = animation->GetAutoLock();
	IAnimationKeyframe* animKeyFrame = animKeyFrameLocked.GetPtr();
	REQUIRE(animKeyFrame != nullptr);
	const std::string animUrlPath = "/advviz/v1/animations/" + (std::string)animKeyFrame->GetId();

	SECTION("DeleteChunk") {
		IAnimationKeyframeInfoPtr keyframeInfoPtr = CreateSavedKeyframeInfo(mock, *animKeyFrame, animUrlPath);
		auto keyframeInfoLocked = keyframeInfoPtr->GetAutoLock();
		IAnimationKeyframeInfo* keyframeInfo = keyframeInfoLocked.GetPtr();

		SECTION("existing chunk") {
			ScopedResponse delResp(mock, std::pair("DELETE", animUrlPath + "/animationKeyFramesChunks"),
				200, kDeleteChunkPayload);

			auto ret = keyframeInfo->DeleteChunk(0);
			CHECK((bool)ret == true);
			// last chunk of the list: the slot is popped
			CHECK(keyframeInfo->GetChunkCount() == 0);
		}

		SECTION("out of range chunk") {
			// no mock registered: no request must be sent
			auto ret = keyframeInfo->DeleteChunk(42);
			REQUIRE((bool)ret == false);
			CHECK(keyframeInfo->GetChunkCount() == 1);
		}
	}

	SECTION("DeleteKeyframeInfo") {
		SECTION("saved info") {
			IAnimationKeyframeInfoPtr keyframeInfoPtr = CreateSavedKeyframeInfo(mock, *animKeyFrame, animUrlPath);
			auto keyframeInfoLocked = keyframeInfoPtr->GetAutoLock();

			ScopedResponse delResp(mock, std::pair("DELETE", animUrlPath + "/animationKeyFramesInfos"),
				200, kDeleteInfoPayload);

			auto ret = keyframeInfoLocked->Delete();
			CHECK((bool)ret == true);
		}

		SECTION("unsaved info has no id") {
			std::string objId("MyObject2");
			IAnimationKeyframeInfoPtr keyframeInfoPtr = animKeyFrame->AddAnimationKeyframeInfo(objId);
			REQUIRE(keyframeInfoPtr != nullptr);
			auto keyframeInfoLocked = keyframeInfoPtr->GetAutoLock();

			// never saved -> no server id -> must fail without sending any request
			auto ret = keyframeInfoLocked->Delete();
			REQUIRE((bool)ret == false);
		}
	}

	SECTION("DeleteAnimationKeyframe") {
		SECTION("success") {
			ScopedResponse delResp(mock, std::pair("DELETE", animUrlPath), 200, kDeleteAnimationPayload);
			auto ret = animKeyFrame->Delete();
			CHECK((bool)ret == true);
		}

		SECTION("server error is reported") {
			ScopedResponse delResp(mock, std::pair("DELETE", animUrlPath), 404, std::string("{}"));
			auto ret = animKeyFrame->Delete();
			REQUIRE((bool)ret == false);
		}
	}
}

namespace
{
	// PUT answers (update of an existing object)
	static const std::string kNumUpdatedPayload = "{\"numUpdated\":1}";

	// GET animations/<id>/animationKeyFramesInfos (HttpGetWithLink envelope)
	static const std::string kKeyframeInfosListPayload =
		"{\"total_rows\":1,\"rows\":["
		"{\"objectId\":\"MyObject1\",\"type\":\"baked\",\"keyframeInterval\":0.033333333333,"
		"\"startTime\":0.5,\"keyframeCount\":35,\"chunckSize\":30,\"states\":[\"walking\",\"standing\"],"
		"\"tags\":[\"female\",\"old\"],\"id\":\"67a0e124f9f9158c2e60d7bc\"}"
		"],\"_links\":{\"self\":\"http:///advviz/v1/animations/67a0e122f9f9158c2e60d7ba/animationKeyFramesInfos\"}}";

	// POST .../query/animationKeyFrames (10 keyframes)
	static const std::string kQueryKeyframesPayload =
		"{\"translations\":[0,0,1,0.1,0,1,0.2,0,1,0.3,0,1,0.4,0,1,0.5,0,1,0.6,0,1,0.7,0,1,0.8,0,1,0.9,0,1],"
		"\"quaternions\":[0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1],"
		"\"boundingBox\":{\"min\":{\"x\":0,\"y\":0,\"z\":0},\"max\":{\"x\":10,\"y\":10,\"z\":10}},"
		"\"timeRange\":{\"begin\":0,\"end\":100}}";

	//! Builds a TimelineResult with 4 keys on [0,4], with optional scales and stateIds.
	IAnimationKeyframeInfo::TimelineResult MakeTimelineResult()
	{
		IAnimationKeyframeInfo::TimelineResult result;
		result.timeRange = { 0.f, 4.f };
		result.translations = {
			0,0,0,
			1,0,0,
			2,0,0,
			3,0,0 };
		result.quaternions = {
			0,0,0,1,
			0,0,0,1,
			0,0,0,1,
			0,0,0,1 };
		result.scales = std::vector<float>{
			1,1,1,
			2,2,2,
			3,3,3,
			4,4,4 };
		result.stateIds = std::vector<std::int8_t>{ 0, 1, 2, 3 };
		return result;
	}
}

TEST_CASE("KeyframeChunkAccessors") {
	using namespace AdvViz::SDK;

	HTTPMock& mock = InitKeyframeTest();
	IAnimationKeyframePtr animation = CreateMockedAnimation(mock);
	auto animLocked = animation->GetAutoLock();
	IAnimationKeyframeInfoPtr infoPtr = animLocked->AddAnimationKeyframeInfo("MyObject1");
	REQUIRE(infoPtr != nullptr);
	auto infoLocked = infoPtr->GetAutoLock();
	IAnimationKeyframeChunkPtr chunkPtr = infoLocked->CreateChunk();
	REQUIRE(chunkPtr != nullptr);
	auto chunkLocked = chunkPtr->GetAutoLock();
	IAnimationKeyframeChunk* chunk = chunkLocked.GetPtr();
	REQUIRE(chunk != nullptr);

	// a brand new chunk is empty and has no server id
	CHECK(chunk->ShouldSave() == false);
	CHECK(chunk->GetId().IsValid() == false);
	CHECK(chunk->GetQuaternions().empty());
	CHECK(chunk->GetScales().empty());		// no value -> empty dummy
	CHECK(chunk->GetStateIds().empty());	// no value -> empty dummy

	const std::vector<float> quaternions = { 0,0,0,1, 0,0,0,2 };
	chunk->SetQuaternions(quaternions);
	CHECK(chunk->GetQuaternions() == quaternions);

	const std::vector<float> scales = { 1,1,1, 2,2,2 };
	chunk->SetScales(scales);
	CHECK(chunk->GetScales() == scales);

	const std::vector<std::int8_t> stateIds = { 0, 1 };
	chunk->SetStateIds(stateIds);
	CHECK(chunk->GetStateIds() == stateIds);

	BoundingBox bbox;
	bbox.min = { 0.0, 1.0, 2.0 };
	bbox.max = { 3.0, 4.0, 5.0 };
	chunk->SetBoundingBox(bbox);
	CHECK(chunk->GetBoundingBox().min == bbox.min);
	CHECK(chunk->GetBoundingBox().max == bbox.max);

	TimeRange timeRange{ 0.5f, 1.5f };
	chunk->SetTimeRange(timeRange);
	CHECK(chunk->GetTimeRange().begin == Catch::Approx(0.5f));
	CHECK(chunk->GetTimeRange().end == Catch::Approx(1.5f));

	// any setter flags the chunk as dirty
	CHECK(chunk->ShouldSave() == true);
}

TEST_CASE("KeyframeInfoAccessors") {
	using namespace AdvViz::SDK;

	HTTPMock& mock = InitKeyframeTest();
	IAnimationKeyframePtr animation = CreateMockedAnimation(mock);
	auto animLocked = animation->GetAutoLock();
	IAnimationKeyframeInfoPtr infoPtr = animLocked->AddAnimationKeyframeInfo("MyObject1");
	REQUIRE(infoPtr != nullptr);
	auto infoLocked = infoPtr->GetAutoLock();
	IAnimationKeyframeInfo* info = infoLocked.GetPtr();
	REQUIRE(info != nullptr);

	// defaults, before anything is set
	CHECK(info->GetType().empty());
	CHECK(info->GetKeyframeInterval() == Catch::Approx(0.f));
	CHECK(info->GetStartTime() == Catch::Approx(0.0));
	CHECK(info->GetKeyframeCount() == 0);
	CHECK(info->GetChunkSize() == 0);
	CHECK(info->GetStates().empty());
	CHECK(info->GetTags().empty());
	CHECK(info->GetGCS().wkt.empty());
	// never saved -> no server id
	CHECK(info->GetId().IsValid() == false);

	info->SetType("baked");
	CHECK(info->GetType() == "baked");

	info->SetKeyframeInterval(1.f / 30.f);
	CHECK(info->GetKeyframeInterval() == Catch::Approx(1.f / 30.f));

	info->SetStartTime(0.5);
	CHECK(info->GetStartTime() == Catch::Approx(0.5));

	info->SetKeyframeCount(35);
	CHECK(info->GetKeyframeCount() == 35);

	info->SetChunkSize(30);
	CHECK(info->GetChunkSize() == 30);

	const std::vector<std::string> states = { "walking", "standing" };
	info->SetStates(states);
	CHECK(info->GetStates() == states);

	const std::vector<std::string> tags = { "female", "old" };
	info->SetTags(tags);
	CHECK(info->GetTags() == tags);

	GCS gcs;
	gcs.wkt = "EPSG:4326";
	gcs.center = { 48.8, 2.3, 35.0 };
	info->SetGCS(gcs);
	CHECK(info->GetGCS().wkt == gcs.wkt);
	CHECK(info->GetGCS().center == gcs.center);
}

TEST_CASE("UpdateKeyframeAnimation") {
	using namespace AdvViz::SDK;

	HTTPMock& mock = InitKeyframeTest();
	// the update path (server id already known) is exercised with both APIs
	const ESaveMode saveMode = GENERATE(ESaveMode::Sync, ESaveMode::Async);
	CAPTURE(saveMode == ESaveMode::Async);

	IAnimationKeyframePtr animation = CreateMockedAnimation(mock);
	auto animKeyFrameLocked = animation->GetAutoLock();
	IAnimationKeyframe* animKeyFrame = animKeyFrameLocked.GetPtr();
	REQUIRE(animKeyFrame != nullptr);
	const std::string animId = (std::string)animKeyFrame->GetId();
	const std::string animUrlPath = "/advviz/v1/animations/" + animId;

	// first save: POST (creation), gives the keyframe info and the chunk their server id
	IAnimationKeyframeInfoPtr keyframeInfoPtr = CreateSavedKeyframeInfo(mock, *animKeyFrame, animUrlPath);
	auto keyframeInfoLocked = keyframeInfoPtr->GetAutoLock();
	IAnimationKeyframeInfo* keyframeInfo = keyframeInfoLocked.GetPtr();
	REQUIRE(keyframeInfo->GetId() == IAnimationKeyframeInfo::Id(kSavedKeyframeInfoId));

	SECTION("update chunk uses PUT") {
		ScopedResponse chunkResp(mock, std::pair("PUT", animUrlPath + "/animationKeyFramesChunks"),
			200, kNumUpdatedPayload);

		IAnimationKeyframeChunkPtr chunkPtr = keyframeInfo->GetChunk(0);
		REQUIRE(chunkPtr != nullptr);
		auto chunkLocked = chunkPtr->GetAutoLock();
		IAnimationKeyframeChunk* chunk = chunkLocked.GetPtr();
		REQUIRE(chunk->GetId() == IAnimationKeyframeChunk::Id(kSavedChunkId));

		chunk->SetTranslations({ 1,1,1, 2,2,2 }); // make it dirty again
		REQUIRE(chunk->ShouldSave() == true);

		const std::string infoId = (std::string)keyframeInfo->GetId();
		if (saveMode == ESaveMode::Sync)
		{
			auto ret = chunk->Save(GetDefaultHttp(), animId, infoId);
			REQUIRE((bool)ret == true);
		}
		else
		{
			std::atomic_bool bSaved = false;
			long httpResult = 0;
			chunk->AsyncSave(GetDefaultHttp(), animId, infoId, [&](long result) {
				httpResult = result;
				bSaved = true;
			});
			REQUIRE(WaitForAsyncTask(bSaved, kAsyncTimeoutSeconds));
			CHECK(httpResult == 200);
		}
		CHECK(chunk->ShouldSave() == false);
		// the id is left untouched by an update
		CHECK(chunk->GetId() == IAnimationKeyframeChunk::Id(kSavedChunkId));
	}

	SECTION("update keyframe info uses PUT") {
		ScopedResponse infoResp(mock, std::pair("PUT", animUrlPath + "/animationKeyFramesInfos"),
			200, kNumUpdatedPayload);

		keyframeInfo->SetTags({ "male", "young" }); // make it dirty again
		REQUIRE(keyframeInfo->ShouldSave() == true);

		if (saveMode == ESaveMode::Sync)
		{
			auto ret = keyframeInfo->Save(GetDefaultHttp(), false /*bChunks*/);
			REQUIRE((bool)ret == true);
		}
		else
		{
			std::atomic_bool bSaved = false;
			IAnimationKeyframeInfo::Id savedId;
			keyframeInfo->AsyncSave(GetDefaultHttp(), [&](const IAnimationKeyframeInfo::Id& id) {
				savedId = id;
				bSaved = true;
			});
			REQUIRE(WaitForAsyncTask(bSaved, kAsyncTimeoutSeconds));
			CHECK(savedId == IAnimationKeyframeInfo::Id(kSavedKeyframeInfoId));
		}
		CHECK(keyframeInfo->GetId() == IAnimationKeyframeInfo::Id(kSavedKeyframeInfoId));
	}
}

TEST_CASE("SaveAnimationKeyframe") {
	using namespace AdvViz::SDK;

	HTTPMock& mock = InitKeyframeTest();

	IAnimationKeyframePtr animation = CreateMockedAnimation(mock);
	auto animKeyFrameLocked = animation->GetAutoLock();
	IAnimationKeyframe* animKeyFrame = animKeyFrameLocked.GetPtr();
	REQUIRE(animKeyFrame != nullptr);
	const std::string animUrlPath = "/advviz/v1/animations/" + (std::string)animKeyFrame->GetId();

	SECTION("save without infos") {
		// freshly created animations are always dirty
		CHECK(animKeyFrame->ShouldSave() == true);

		// the animation already has a server id -> PUT animations/<id>
		ScopedResponse animResp(mock, std::pair("PUT", animUrlPath), 200, kNumUpdatedPayload);
		auto ret = animKeyFrame->Save(GetDefaultHttp(), false /*bInfos*/);
		REQUIRE((bool)ret == true);
		CHECK(animKeyFrame->ShouldSave() == false);
	}

	SECTION("server error is reported") {
		ScopedResponse animResp(mock, std::pair("PUT", animUrlPath), 500, std::string("{}"));
		auto ret = animKeyFrame->Save(GetDefaultHttp(), false /*bInfos*/);
		REQUIRE((bool)ret == false);
	}

	SECTION("save with infos registers them in the map") {
		ScopedResponse animResp(mock, std::pair("PUT", animUrlPath), 200, kNumUpdatedPayload);
		ScopedResponse infoResp(mock, std::pair("POST", animUrlPath + "/animationKeyFramesInfos"),
			201, kSaveKeyframeInfoPayload);

		IAnimationKeyframeInfoPtr infoPtr = animKeyFrame->AddAnimationKeyframeInfo("MyObject1");
		REQUIRE(infoPtr != nullptr);
		{
			auto infoLocked = infoPtr->GetAutoLock();
			infoLocked->SetType("baked");
		}
		// a dirty info makes the whole animation dirty
		CHECK(animKeyFrame->ShouldSave() == true);

		auto ret = animKeyFrame->Save(GetDefaultHttp());
		REQUIRE((bool)ret == true);

		const IAnimationKeyframeInfo::Id savedId(kSavedKeyframeInfoId);
		auto ids = animKeyFrame->GetAnimationKeyframeInfoIds();
		REQUIRE(ids.size() == 1);
		CHECK(ids[0] == savedId);
		CHECK(animKeyFrame->GetAnimationKeyframeInfo(savedId) != nullptr);
		// unknown id -> null pointer
		CHECK(animKeyFrame->GetAnimationKeyframeInfo(
			IAnimationKeyframeInfo::Id("000000000000000000000000")) == nullptr);
	}

	SECTION("GCS transform accessors") {
		CHECK(animKeyFrame->GetGCSTransform() == nullptr);
		animKeyFrame->SetGCSTransform(std::shared_ptr<Tools::IGCSTransform>());
		CHECK(animKeyFrame->GetGCSTransform() == nullptr);
	}
}

TEST_CASE("LoadAnimationKeyFrameInfos") {
	using namespace AdvViz::SDK;

	HTTPMock& mock = InitKeyframeTest();

	IAnimationKeyframePtr animation = CreateMockedAnimation(mock);
	auto animKeyFrameLocked = animation->GetAutoLock();
	IAnimationKeyframe* animKeyFrame = animKeyFrameLocked.GetPtr();
	REQUIRE(animKeyFrame != nullptr);
	const std::string animUrlPath = "/advviz/v1/animations/" + (std::string)animKeyFrame->GetId();
	const HTTPMock::RequestKey infosKey = std::pair("GET", animUrlPath + "/animationKeyFramesInfos");
	const IAnimationKeyframeInfo::Id expectedId(kSavedKeyframeInfoId);

	SECTION("synchronous") {
		ScopedResponse infosResp(mock, infosKey, 200, kKeyframeInfosListPayload);

		auto ret = animKeyFrame->LoadAnimationKeyFrameInfos();
		REQUIRE((bool)ret == true);
		CHECK(animKeyFrame->ShouldSave() == false);

		auto ids = animKeyFrame->GetAnimationKeyframeInfoIds();
		REQUIRE(ids.size() == 1);
		CHECK(ids[0] == expectedId);

		IAnimationKeyframeInfoPtr infoPtr = animKeyFrame->GetAnimationKeyframeInfo(expectedId);
		REQUIRE(infoPtr != nullptr);
		auto infoLocked = infoPtr->GetAutoLock();
		CHECK(infoLocked->GetObjectId() == "MyObject1");
		CHECK(infoLocked->GetType() == "baked");
		CHECK(infoLocked->GetKeyframeCount() == 35);
	}

	SECTION("synchronous - server error") {
		ScopedResponse infosResp(mock, infosKey, 404, std::string("{}"));
		auto ret = animKeyFrame->LoadAnimationKeyFrameInfos();
		REQUIRE((bool)ret == false);
	}

	SECTION("asynchronous") {
		ScopedResponse infosResp(mock, infosKey, 200, kKeyframeInfosListPayload);

		std::mutex resultMutex;
		std::vector<IAnimationKeyframeInfoPtr> createdInfos;
		std::atomic_bool bFinished = false;
		bool bFinishedOk = false;

		animKeyFrame->AsyncLoadAnimationKeyFrameInfos(
			[&resultMutex, &createdInfos](IAnimationKeyframeInfoPtr& info) {
				std::lock_guard<std::mutex> lock(resultMutex);
				createdInfos.push_back(info);
			},
			[&bFinished, &bFinishedOk](AdvViz::expected<void, std::string> const& ret) {
				bFinishedOk = ret.has_value();
				bFinished = true;
			});

		REQUIRE(WaitForAsyncTask(bFinished, kAsyncTimeoutSeconds));
		CHECK(bFinishedOk);

		std::lock_guard<std::mutex> lock(resultMutex);
		REQUIRE(createdInfos.size() == 1);
		CHECK(animKeyFrame->GetAnimationKeyframeInfo(expectedId) != nullptr);
	}
}

TEST_CASE("AsyncQueryKeyframes") {
	using namespace AdvViz::SDK;

	HTTPMock& mock = InitKeyframeTest();

	IAnimationKeyframePtr animation = CreateMockedAnimation(mock);
	auto animKeyFrameLocked = animation->GetAutoLock();
	IAnimationKeyframe* animKeyFrame = animKeyFrameLocked.GetPtr();
	REQUIRE(animKeyFrame != nullptr);
	const std::string animUrlPath = "/advviz/v1/animations/" + (std::string)animKeyFrame->GetId();

	IAnimationKeyframeInfoPtr keyframeInfoPtr = CreateSavedKeyframeInfo(mock, *animKeyFrame, animUrlPath);
	auto keyframeInfoLocked = keyframeInfoPtr->GetAutoLock();
	IAnimationKeyframeInfo* keyframeInfo = keyframeInfoLocked.GetPtr();

	SECTION("success") {
		ScopedResponse queryResp(mock, std::pair("POST", animUrlPath + "/query/animationKeyFrames"),
			200, kQueryKeyframesPayload);

		auto dataPtr = MakeSharedLockableData<IAnimationKeyframeInfo::TimelineResult>();
		std::atomic_bool bQueryDone = false;
		long httpCode = 0;

		auto ret = keyframeInfo->AsyncQueryKeyframes(dataPtr,
			[&](long code, const TSharedLockableData<IAnimationKeyframeInfo::TimelineResult>&) {
				httpCode = code;
				bQueryDone = true;
			},
			0.0, 1.0);
		REQUIRE((bool)ret == true);

		REQUIRE(WaitForAsyncTask(bQueryDone, kAsyncTimeoutSeconds));
		CHECK(httpCode == 200);

		auto dataLocked = dataPtr->GetAutoLock();
		CHECK(dataLocked->translations.size() == 30);
		CHECK(dataLocked->quaternions.size() == 40);
		CHECK(dataLocked->timeRange.end == Catch::Approx(100.f));
	}

	SECTION("unsaved info has no id") {
		IAnimationKeyframeInfoPtr unsavedPtr = animKeyFrame->AddAnimationKeyframeInfo("MyObject2");
		REQUIRE(unsavedPtr != nullptr);
		auto unsavedLocked = unsavedPtr->GetAutoLock();

		auto dataPtr = MakeSharedLockableData<IAnimationKeyframeInfo::TimelineResult>();
		// no id -> must fail without sending any request
		auto ret = unsavedLocked->AsyncQueryKeyframes(dataPtr,
			[](long, const TSharedLockableData<IAnimationKeyframeInfo::TimelineResult>&) {}, 0.0, 1.0);
		REQUIRE((bool)ret == false);
	}
}

TEST_CASE("GetInterpolatedValue") {
	using namespace AdvViz::SDK;

	HTTPMock& mock = InitKeyframeTest();
	IAnimationKeyframePtr animation = CreateMockedAnimation(mock);
	auto animLocked = animation->GetAutoLock();
	IAnimationKeyframeInfoPtr infoPtr = animLocked->AddAnimationKeyframeInfo("MyObject1");
	REQUIRE(infoPtr != nullptr);
	auto infoLocked = infoPtr->GetAutoLock();
	IAnimationKeyframeInfo* info = infoLocked.GetPtr();

	const IAnimationKeyframeInfo::TimelineResult result = MakeTimelineResult();

	SECTION("on a keyframe") {
		IAnimationKeyframeInfo::TimelineValue value;
		auto ret = info->GetInterpolatedValue(result, 0.0, value);
		REQUIRE((bool)ret == true);
		CHECK(value.translation[0] == Catch::Approx(0.f));
		CHECK(value.quaternion[3] == Catch::Approx(1.f));
	}

	SECTION("between two keyframes") {
		IAnimationKeyframeInfo::TimelineValue value;
		auto ret = info->GetInterpolatedValue(result, 1.5, value);
		REQUIRE((bool)ret == true);
		// linear interpolation between key 1 (x=1) and key 2 (x=2)
		CHECK(value.translation[0] == Catch::Approx(1.5f));
		CHECK(value.translation[1] == Catch::Approx(0.f));
		CHECK(value.quaternion[3] == Catch::Approx(1.f)); // slerp of two identities
	}

	SECTION("scale and state are interpolated when requested") {
		IAnimationKeyframeInfo::TimelineValue value;
		value.scale.emplace();		// opt-in for scale
		value.stateId = 0;			// opt-in for state
		auto ret = info->GetInterpolatedValue(result, 1.5, value);
		REQUIRE((bool)ret == true);
		REQUIRE(value.scale.has_value());
		CHECK(value.scale.value()[0] == Catch::Approx(2.5f));
		REQUIRE(value.stateId.has_value());
		CHECK(value.stateId.value() == 1); // state is taken from the lower key
	}

	SECTION("last keyframe clamps the second index") {
		IAnimationKeyframeInfo::TimelineValue value;
		auto ret = info->GetInterpolatedValue(result, 3.0, value);
		REQUIRE((bool)ret == true);
		CHECK(value.translation[0] == Catch::Approx(3.f));
	}

	SECTION("time out of the result range") {
		IAnimationKeyframeInfo::TimelineValue value;
		auto ret = info->GetInterpolatedValue(result, 10.0, value);
		REQUIRE((bool)ret == false);
	}

	SECTION("time on the upper bound is out of the keyframes") {
		IAnimationKeyframeInfo::TimelineValue value;
		// time == timeRange.end -> key index == nbKeys -> invalid keyframe
		auto ret = info->GetInterpolatedValue(result, 4.0, value);
		REQUIRE((bool)ret == false);
	}
}
