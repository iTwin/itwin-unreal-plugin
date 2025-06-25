/*--------------------------------------------------------------------------------------+
|
|     $Source: VizKeyframeTests.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "../Visualization.h"
#include <filesystem>

#include <catch2/catch_all.hpp>
#include "Mock.h"

void SetDefaultConfig();

TEST_CASE("SaveKeyframeAnimation"){
	using namespace AdvViz::SDK;
	
	Tools::CreateAdvVizLogChannels();

	SetDefaultConfig();
	HTTPMock *mock = GetHttpMock();
	REQUIRE(mock != nullptr);

	using RequestKey = HTTPMock::RequestKey;
	std::shared_ptr<std::string> p = std::make_shared<std::string>("abcd");
	GetDefaultHttp()->SetAccessToken(p);

	IAnimationKeyframe* animKeyFrame = nullptr;
	SECTION("CreateAnimationKeyframe") {
		RequestKey respKey1 = std::pair("POST", "/advviz/v1/animations");
		mock->responseFct_[respKey1] = [] {
			std::string s = "{\"name\":\"MyAnim2\",\"itwinid\":\"904a89f7-b63c-4ae1-a223-88517bd4bb08\",\"id\":\"67a0e122f9f9158c2e60d7ba\"}";
			return HTTPMock::Response2(201, s);
			};

		RequestKey respKey2 = std::pair("GET", "/advviz/v1/animations/animationKeyFramesInfos");
		mock->responseFct_[respKey2] = [] {
			std::string s = "{\"name\":\"MyAnim2\",\"itwinid\":\"904a89f7-b63c-4ae1-a223-88517bd4bb08\",\"id\":\"67a0e122f9f9158c2e60d7ba\"}";
			return HTTPMock::Response2(200, s);
			};

		auto ret = CreateAnimationKeyframe("904a89f7-b63c-4ae1-a223-88517bd4bb08", "MyAnim2");
		REQUIRE((bool)ret == true);
		mock->responseFct_.erase(respKey1);
		mock->responseFct_.erase(respKey2);

		if (ret)
		{
			auto animKeyFrameLocked = ret.value()->GetAutoLock();
			animKeyFrame = animKeyFrameLocked.GetPtr();
			REQUIRE(animKeyFrame != nullptr);
			std::string animUrlPath = "/advviz/v1/animations/" + (std::string)animKeyFrame->GetId();

			SECTION("Create Animation KeyframeInfo") {
				respKey1 = std::pair("POST", animUrlPath + "/animationKeyFramesInfos");
				mock->responseFct_[respKey1] = [] {
					std::string s = "{\"animationKeyFramesInfos\":[{\"objectId\":\"MyObject1\",\"type\":\"baked\",\"keyframeInterval\":0.033333333333,\"startTime\":0.5,\"keyframeCount\":35,\"chunckSize\":30,\"states\":[\"walking\",\"standing\"],\"tags\":[\"female\",\"old\"],\"id\":\"67a0e124f9f9158c2e60d7bc\"}]}";
					return HTTPMock::Response2(201, s);
					};

				std::string objId("MyObject1");
				IAnimationKeyframeInfoPtr keyframeInfoPtr = animKeyFrame->AddAnimationKeyframeInfo(objId);
				REQUIRE(keyframeInfoPtr != nullptr);
				auto keyframeInfoLocked = keyframeInfoPtr->GetAutoLock();
				IAnimationKeyframeInfo* keyframeInfo = keyframeInfoLocked.GetPtr();
				REQUIRE(keyframeInfo != nullptr);
				keyframeInfo->SetType("baked");
				keyframeInfo->SetChunkSize(60);
				std::vector<std::string> states = { "hidden", "walking", "standing"};
				keyframeInfo->SetStates(states);
				std::vector<std::string> tags = { "man", "old" };
				keyframeInfo->SetTags(tags);
				auto ret2 = keyframeInfo->Save(GetDefaultHttp());
				REQUIRE((bool)ret2 == true);

				mock->responseFct_.erase(respKey1);
				SECTION("Create Animation KeyframeChunk") {
					respKey1 = std::pair("POST", animUrlPath + "/animationKeyFramesChunks");
					mock->responseFct_[respKey1] = [] {
						std::string s = "{\"ids\":[\"67a0e124f9f9158c2e60daaa\"]}";
						return HTTPMock::Response2(201, s);
						};

					std::vector<float> translations = {
						0,0,1,
						0,0,2
					};
					std::vector<float> quaternions = {
						0,0,0, 1,
						0,0,0, 2
					};

					IAnimationKeyframeChunkPtr keyframeChunkPtr2;

					{ // note: reduce lock scope because chucks need to be unlocked before keyframeInfo::Save
						IAnimationKeyframeChunkPtr keyframeChunkPtr = keyframeInfo->CreateChunk();
						auto keyframeChunkLocked = keyframeChunkPtr->GetAutoLock();
						auto chunk = keyframeChunkLocked.GetPtr();
						REQUIRE(chunk != nullptr);
						chunk->SetTranslations(translations);
						chunk->SetQuaternions(quaternions);
						auto ret3 = chunk->Save(GetDefaultHttp(), (std::string)animKeyFrame->GetId(), (std::string)keyframeInfo->GetId());
						REQUIRE((bool)ret3 == true);
						REQUIRE(chunk->GetId() == "67a0e124f9f9158c2e60daaa");

						keyframeChunkPtr2 = keyframeInfo->CreateChunk();
						auto keyframeChunkLocked2 = keyframeChunkPtr2->GetAutoLock();
						auto chunk2 = keyframeChunkLocked2.GetPtr();

						chunk2->SetTranslations(translations);
						chunk2->SetQuaternions(quaternions);
					}

					mock->responseFct_.erase(respKey1);
					mock->responseFct_[respKey1] = [] {
						std::string s = "{\"ids\":[\"67a0e124f9f9158c2e60dbbb\"]}";
						return HTTPMock::Response2(201, s);
						};
					auto ret4 = keyframeInfo->Save(GetDefaultHttp());
					REQUIRE((bool)ret4 == true);
					REQUIRE(keyframeChunkPtr2->GetAutoLock()->GetId() == "67a0e124f9f9158c2e60dbbb");

					mock->responseFct_.erase(respKey1);
				}
			}
		}
	}

}

TEST_CASE("LoadKeyframeAnimation") {
	using namespace AdvViz::SDK;

	Tools::CreateAdvVizLogChannels();

	SetDefaultConfig();
	HTTPMock* mock = GetHttpMock();
	REQUIRE(mock != nullptr);

	std::shared_ptr<std::string> p = std::make_shared<std::string>("abcd");
	GetDefaultHttp()->SetAccessToken(p);

	using RequestKey = HTTPMock::RequestKey;

	std::shared_ptr<IAnimationKeyframe> animKeyFrame;
	SECTION("LoadAnimationKeyframe") {
		RequestKey respKey2 = std::pair("GET", "/advviz/v1/animations");
		mock->responseFct_[respKey2] = [] {
			std::string s = "{\"total_rows\":2,\"rows\":[{\"name\":\"MyAnim2\",\"itwinid\":\"904a89f7-b63c-4ae1-a223-88517bd4bb08\",\"id\":\"67a217484ad6dc296ad8adea\"},{\"name\":\"MyAnim\",\"itwinid\":\"904a89f7-b63c-4ae1-a223-88517bd4bb08\",\"id\":\"67a217484ad6dc296ad8adeb\"}],\"_links\":{\"self\":\"http:///advviz/v1/animations?iTwinId=904a89f7-b63c-4ae1-a223-88517bd4bb08\u0026$skip=0\u0026$top=1000\"}}";
			return HTTPMock::Response2(200, s);
			};

		std::vector<IAnimationKeyframePtr> v = GetITwinAnimationKeyframes("904a89f7-b63c-4ae1-a223-88517bd4bb08");
		CHECK(v.size() == 2);
		mock->responseFct_.erase(respKey2);
		if (!v.empty())
		{
			auto keyFrameLocked = v[0]->GetAutoLock();
			IAnimationKeyframe* animationKeyframe = keyFrameLocked.GetPtr();
			auto respKey3 = std::pair("POST", "/advviz/v1/animations/"+ (std::string)animationKeyframe->GetId() + "/query/animationKeyFramesBBox");
			mock->responseFct_[respKey3] = [] {
				std::string s = "{\"ids\":[\"67a0e124f9f9158c2e60d000\"]}";
				return HTTPMock::Response2(200, s);
				};

			std::vector<BoundingBox> boundingBoxes = { {{0,0,0},{1,1,1}}, {{2,2,2},{3,3,3}} };
			TimeRange timeRange = { 0.0, 1.0 };
			auto ret = animationKeyframe->QueryKeyframesInfos(boundingBoxes, timeRange);
			CHECK(ret.has_value());
			CHECK(ret.value().size() == 1);
			mock->responseFct_.erase(respKey3);
			if (ret)
			{
				std::string animUrlPath = "/advviz/v1/animations/67a217484ad6dc296ad8adea/";
				{
					RequestKey respKey = std::pair("GET", animUrlPath + "animationKeyFramesInfos/67a0e124f9f9158c2e60d000");
					mock->responseFct_[respKey] = [] {
						std::string s = "{\"objectId\":\"MyObject2\",\"type\":\"baked\",\"keyframeInterval\":0.03333333333333,\"startTime\":0,\"keyframeCount\":10,\"chunckSize\":30,\"states\":[\"walking + luggage\",\"standing\"],\"tags\":[\"male\",\"young\"],\"id\":\"67a0e124f9f9158c2e60d000\"}";
						return HTTPMock::Response2(200, s);
						};

					mock->responseFct_[std::pair("POST", animUrlPath + "query/animationKeyFramesChunks")] = [] {
						std::string s = "{\"ids\":[\"aaaabbbb\",\"ccccdddd\"]}";
						return HTTPMock::Response2(200, s);
						};

					REQUIRE((*ret)[0] == "67a0e124f9f9158c2e60d000");
					auto keyframesInfoId = (*ret)[0];
					auto ret2 = animationKeyframe->LoadKeyframesInfo(keyframesInfoId);

					CHECK(ret2.has_value() == true);
					mock->responseFct_.erase(respKey);

					IAnimationKeyframeInfoPtr keyframeInfoPtr = ret2.value();
					REQUIRE(keyframeInfoPtr != nullptr);
					auto keyframeInfoLocked = keyframeInfoPtr->GetAutoLock();
					IAnimationKeyframeInfo* keyframesInfo = keyframeInfoLocked.GetPtr();
					CHECK(keyframesInfo->GetChunkCount() == 2);
					CHECK(keyframesInfo->GetChunk(0)->GetAutoLock()->GetId() == "aaaabbbb");
					CHECK(keyframesInfo->GetChunk(1)->GetAutoLock()->GetId() == "ccccdddd");

					auto keyframeChunkLocked = keyframesInfo->GetChunk(0)->GetAutoLock();
					IAnimationKeyframeChunk* chuck = keyframeChunkLocked.GetPtr();
					CHECK(chuck->IsFullyLoaded() == false);

					respKey3 = std::pair("GET", animUrlPath + "animationKeyFramesChunks/aaaabbbb");
					mock->responseFct_[respKey3] = [] {
						std::string s = "{\"chunkId\":0,\"animationKeyFramesInfoId\":\"67bef7c1f831f091186335d2\",\"translations\":[0,0,1,0.1,0,1,0.2,0,1,0.3,0,1,0.4,0,1,0.5,0,1,0.6,0,1,0.7,0,1,0.8,0,1,0.9,0,1,1,0,1,1.1,0,1,1.2,0,1,1.3,0,1,1.4,0,1,1.5,0,1,1.6,0,1,1.7,0,1,1.8,0,1,1.9,0,1,2,0,1,2.1,0,1,2.2,0,1,2.3,0,1,2.4,0,1,2.5,0,1,2.6,0,1,2.7,0,1,2.8,0,1,2.9,0,1],\"quaternions\":[0,0,2,1,0,0.1,2,1,0,0.2,2,1,0,0.3,2,1,0,0.4,2,1,0,0.5,2,1,0,0.6,2,1,0,0.7,2,1,0,0.8,2,1,0,0.9,2,1,0,1,2,1,0,1.1,2,1,0,1.2,2,1,0,1.3,2,1,0,1.4,2,1,0,1.5,2,1,0,1.6,2,1,0,1.7,2,1,0,1.8,2,1,0,1.9,2,1,0,2,2,1,0,2.1,2,1,0,2.2,2,1,0,2.3,2,1,0,2.4,2,1,0,2.5,2,1,0,2.6,2,1,0,2.7,2,1,0,2.8,2,1,0,2.9,2,1],\"boundingBox\":{\"min\":{\"x\":0,\"y\":0,\"z\":1},\"max\":{\"x\":2.9,\"y\":0,\"z\":1}},\"timeRange\":{\"begin\":0.5,\"end\":1.4999999999999},\"id\":\"aaaabbbb\"}";
						return HTTPMock::Response2(200, s);
						};

					auto ret3 = chuck->Load();
					CHECK(ret3.has_value() == true);
					auto &trans = chuck->GetTranslations();
					CHECK(trans.size() == 90);

					respKey2 = std::pair("POST", animUrlPath + "query/animationKeyFrames");
					mock->responseFct_[respKey2] = [] {
						std::string s = "{\"translations\":[0,0,1,0.1,0,1,0.2,0,1,0.3,0,1,0.4,0,1,0.5,0,1,0.6,0,1,0.7,0,1,0.8,0,1,0.9,0,1],\"quaternions\":[0,0,2,1,0,0.1,2,1,0,0.2,2,1,0,0.3,2,1,0,0.4,2,1,0,0.5,2,1,0,0.6,2,1,0,0.7,2,1,0,0.8,2,1,0,0.9,2,1], \"boundingBox\":{\"min\":{\"x\":0,\"y\":0,\"z\":0}, \"max\":{\"x\":10,\"y\":10,\"z\":10}}, \"timeRange\":{\"begin\":0,\"end\":100} }";
						return HTTPMock::Response2(200, s);
						};

					IAnimationKeyframeInfo::TimelineResult result;
					auto ret4 = keyframesInfo->QueryKeyframes(result, 0.0, 1.0);
					CHECK(ret4.has_value() == true);
					REQUIRE(result.translations.size() == 30);
				}
			}
		}
	}

}

