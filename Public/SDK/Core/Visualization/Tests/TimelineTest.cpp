/*--------------------------------------------------------------------------------------+
|
|     $Source: TimelineTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "../Visualization.h"
#include "../Timeline.h"
#include <filesystem>
#include <mutex>

#include <catch2/catch_all.hpp>
#include <Core/Network/Tests/AsyncTestHelpers.h>
#include <Core/Network/Tests/HttpMock.h>

using namespace AdvViz::SDK;

bool WaitForAsyncTask(std::atomic_bool& taskFinished, int maxSeconds);
void SetDefaultConfig();

TEST_CASE("Timeline") {

	SECTION("basic timeline operations") {
		try {
			SetDefaultConfig();

			// Create a timeline
			auto timeline = ITimeline::New();
			REQUIRE(timeline != nullptr);

			// Test initial empty state
			CHECK(timeline->GetClipCount() == 0);

			// Add clips
			auto clip1 = timeline->AddClip("Clip1");
			REQUIRE(clip1 != nullptr);
			CHECK(timeline->GetClipCount() == 1);

			auto clip2 = timeline->AddClip("Clip2");
			REQUIRE(clip2 != nullptr);
			CHECK(timeline->GetClipCount() == 2);

			auto clip3 = timeline->AddClip("Clip3");
			REQUIRE(clip3 != nullptr);
			CHECK(timeline->GetClipCount() == 3);

			// Test clip retrieval by index
			auto retrievedClip1 = timeline->GetClipByIndex(0);
			REQUIRE(retrievedClip1.has_value());
			CHECK(retrievedClip1.value()->GetName() == "Clip1");

			auto retrievedClip2 = timeline->GetClipByIndex(1);
			REQUIRE(retrievedClip2.has_value());
			CHECK(retrievedClip2.value()->GetName() == "Clip2");

			// Test clip retrieval by RefID
			RefID clip1Id = retrievedClip1.value()->GetId();
			auto clipById = timeline->GetClipByRefID(clip1Id);
			REQUIRE(clipById != nullptr);
			CHECK(clipById->GetName() == "Clip1");

			// Test clip removal
			auto removeResult = timeline->RemoveClip(1); // Remove Clip2
			REQUIRE(removeResult.has_value());
			CHECK(timeline->GetClipCount() == 2);

			// After removal, clip at index 1 should now be Clip3
			auto afterRemoval = timeline->GetClipByIndex(1);
			REQUIRE(afterRemoval.has_value());
			CHECK(afterRemoval.value()->GetName() == "Clip3");

			// Test move operation
			timeline->MoveClip(1, 0); // Move Clip3 to position 0
			auto movedClip = timeline->GetClipByIndex(0);
			REQUIRE(movedClip.has_value());
			CHECK(movedClip.value()->GetName() == "Clip3");

			auto secondClip = timeline->GetClipByIndex(1);
			REQUIRE(secondClip.has_value());
			CHECK(secondClip.value()->GetName() == "Clip1");

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}

	SECTION("empty clip operations") {
		try {
			SetDefaultConfig();

			auto timeline = ITimeline::New();
			REQUIRE(timeline != nullptr);

			// Create an empty clip
			auto emptyClip = timeline->AddClip("EmptyClip");
			REQUIRE(emptyClip != nullptr);
			CHECK(emptyClip->GetKeyframeCount() == 0);
			CHECK(emptyClip->GetName() == "EmptyClip");

			// Test empty clip properties
			CHECK(emptyClip->IsEnabled() == true); // Default should be enabled

			emptyClip->SetEnable(false);
			CHECK(emptyClip->IsEnabled() == false);

			emptyClip->SetEnable(true);
			CHECK(emptyClip->IsEnabled() == true);

			// Test name change
			emptyClip->SetName("RenamedClip");
			CHECK(emptyClip->GetName() == "RenamedClip");

			// Verify it's still empty
			CHECK(emptyClip->GetKeyframeCount() == 0);

			// Test getting keyframe from empty clip
			auto keyframeResult = emptyClip->GetKeyframe(0.0);
			CHECK(!keyframeResult.has_value()); // Should return error

			auto keyframeByIndexResult = emptyClip->GetKeyframeByIndex(0);
			CHECK(!keyframeByIndexResult.has_value()); // Should return error

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}

	SECTION("clip with keyframes") {
		try {
			SetDefaultConfig();

			auto timeline = ITimeline::New();
			auto clip = timeline->AddClip("KeyframeClip");
			REQUIRE(clip != nullptr);

			// Create keyframe data with camera
			ITimelineKeyframe::KeyframeData kfData1;
			kfData1.time = 0.0;
			kfData1.camera = ITimelineKeyframe::CameraData();
			kfData1.camera->transform = { 1., 0., 0., 10.,
										  0., 1., 0., 20.,
										  0., 0., 1., 30. };
			kfData1.camera->isPause = false;

			// Add first keyframe
			auto kf1Result = clip->AddKeyframe(kfData1);
			REQUIRE(kf1Result.has_value());
			CHECK(clip->GetKeyframeCount() == 1);

			// Create keyframe with atmosphere data
			ITimelineKeyframe::KeyframeData kfData2;
			kfData2.time = 1.0;
			kfData2.atmo = ITimelineKeyframe::AtmoData();
			kfData2.atmo->time = "2024-06-15T12:00:00";
			kfData2.atmo->cloudCoverage = 0.5f;
			kfData2.atmo->fog = 0.3f;

			// Add second keyframe
			auto kf2Result = clip->AddKeyframe(kfData2);
			REQUIRE(kf2Result.has_value());
			CHECK(clip->GetKeyframeCount() == 2);

			// Create keyframe with synchro data
			ITimelineKeyframe::KeyframeData kfData3;
			kfData3.time = 2.0;
			kfData3.synchro = ITimelineKeyframe::SynchroData();
			kfData3.synchro->date = "2024-07-01";

			// Add third keyframe
			auto kf3Result = clip->AddKeyframe(kfData3);
			REQUIRE(kf3Result.has_value());
			CHECK(clip->GetKeyframeCount() == 3);

			// Create keyframe with all data types
			ITimelineKeyframe::KeyframeData kfData4;
			kfData4.time = 3.0;
			kfData4.camera = ITimelineKeyframe::CameraData();
			kfData4.camera->isPause = true;
			kfData4.atmo = ITimelineKeyframe::AtmoData();
			kfData4.atmo->cloudCoverage = 0.8f;
			kfData4.synchro = ITimelineKeyframe::SynchroData();
			kfData4.synchro->date = "2024-08-01";

			auto kf4Result = clip->AddKeyframe(kfData4);
			REQUIRE(kf4Result.has_value());
			CHECK(clip->GetKeyframeCount() == 4);

			// Retrieve keyframes by time
			auto kfAt0 = clip->GetKeyframe(0.0);
			REQUIRE(kfAt0.has_value());
			CHECK(kfAt0.value()->GetData().time == 0.0);
			CHECK(kfAt0.value()->GetData().camera.has_value());

			auto kfAt1 = clip->GetKeyframe(1.0);
			REQUIRE(kfAt1.has_value());
			CHECK(kfAt1.value()->GetData().time == 1.0);
			CHECK(kfAt1.value()->GetData().atmo.has_value());
			CHECK(kfAt1.value()->GetData().atmo->cloudCoverage == 0.5f);

			// Retrieve keyframes by index
			auto kfIdx0 = clip->GetKeyframeByIndex(0);
			REQUIRE(kfIdx0.has_value());
			CHECK(kfIdx0.value()->GetData().time == 0.0);

			auto kfIdx2 = clip->GetKeyframeByIndex(2);
			REQUIRE(kfIdx2.has_value());
			CHECK(kfIdx2.value()->GetData().time == 2.0);
			CHECK(kfIdx2.value()->GetData().synchro.has_value());

			// Get keyframe index by time
			auto indexResult = clip->GetKeyframeIndex(1.0);
			REQUIRE(indexResult.has_value());
			CHECK(indexResult.value() == 1);

			// Test removing a keyframe
			auto kfToRemove = clip->GetKeyframe(1.0);
			REQUIRE(kfToRemove.has_value());
			auto removeResult = clip->RemoveKeyframe(kfToRemove.value());
			REQUIRE(removeResult.has_value());
			CHECK(clip->GetKeyframeCount() == 3);

			// Verify keyframe at time 1.0 is gone
			auto kfAt1After = clip->GetKeyframe(1.0);
			CHECK(!kfAt1After.has_value());

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}

	SECTION("keyframe data validation") {
		try {
			SetDefaultConfig();

			auto timeline = ITimeline::New();
			auto clip = timeline->AddClip("ValidationClip");

			// Test camera data preservation
			ITimelineKeyframe::KeyframeData kfData;
			kfData.time = 0.0;
			kfData.camera = ITimelineKeyframe::CameraData();
			kfData.camera->transform = { 1., 2., 3., 4.,
										  5., 6., 7., 8.,
										  9., 10., 11., 12. };
			kfData.camera->isPause = true;

			auto kfResult = clip->AddKeyframe(kfData);
			REQUIRE(kfResult.has_value());

			// Retrieve and validate
			auto retrieved = clip->GetKeyframe(0.0);
			REQUIRE(retrieved.has_value());
			const auto& retrievedData = retrieved.value()->GetData();

			CHECK(retrievedData.camera.has_value());
			CHECK(retrievedData.camera->isPause == true);
			// Check transform values
			CHECK(retrievedData.camera->transform[0] == 1.);
			CHECK(retrievedData.camera->transform[3] == 4.);
			CHECK(retrievedData.camera->transform[11] == 12.);

			// Test atmosphere data preservation
			ITimelineKeyframe::KeyframeData atmoData;
			atmoData.time = 1.0;
			atmoData.atmo = ITimelineKeyframe::AtmoData();
			atmoData.atmo->time = "2024-12-25T18:30:00";
			atmoData.atmo->cloudCoverage = 0.75f;
			atmoData.atmo->fog = 0.25f;

			auto atmoResult = clip->AddKeyframe(atmoData);
			REQUIRE(atmoResult.has_value());

			auto atmoRetrieved = clip->GetKeyframe(1.0);
			REQUIRE(atmoRetrieved.has_value());
			const auto& atmoRetrievedData = atmoRetrieved.value()->GetData();

			CHECK(atmoRetrievedData.atmo.has_value());
			CHECK(atmoRetrievedData.atmo->time == "2024-12-25T18:30:00");
			CHECK(atmoRetrievedData.atmo->cloudCoverage == 0.75f);
			CHECK(atmoRetrievedData.atmo->fog == 0.25f);

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}

	SECTION("keyframe ordering") {
		try {
			SetDefaultConfig();

			auto timeline = ITimeline::New();
			auto clip = timeline->AddClip("OrderedClip");

			// Add keyframes out of order
			ITimelineKeyframe::KeyframeData kf3;
			kf3.time = 3.0;
			auto kf3Result = clip->AddKeyframe(kf3);
			REQUIRE(kf3Result.has_value());

			ITimelineKeyframe::KeyframeData kf1;
			kf1.time = 1.0;
			auto kf1Result = clip->AddKeyframe(kf1);
			REQUIRE(kf1Result.has_value());

			ITimelineKeyframe::KeyframeData kf5;
			kf5.time = 5.0;
			auto kf5Result = clip->AddKeyframe(kf5);
			REQUIRE(kf5Result.has_value());

			ITimelineKeyframe::KeyframeData kf0;
			kf0.time = 0.0;
			auto kf0Result = clip->AddKeyframe(kf0);
			REQUIRE(kf0Result.has_value());

			ITimelineKeyframe::KeyframeData kf2;
			kf2.time = 2.0;
			auto kf2Result = clip->AddKeyframe(kf2);
			REQUIRE(kf2Result.has_value());

			CHECK(clip->GetKeyframeCount() == 5);

			// Verify keyframes are sorted by time when retrieved by index
			auto kf_idx0 = clip->GetKeyframeByIndex(0);
			REQUIRE(kf_idx0.has_value());
			CHECK(kf_idx0.value()->GetData().time == 0.0);

			auto kf_idx1 = clip->GetKeyframeByIndex(1);
			REQUIRE(kf_idx1.has_value());
			CHECK(kf_idx1.value()->GetData().time == 1.0);

			auto kf_idx2 = clip->GetKeyframeByIndex(2);
			REQUIRE(kf_idx2.has_value());
			CHECK(kf_idx2.value()->GetData().time == 2.0);

			auto kf_idx3 = clip->GetKeyframeByIndex(3);
			REQUIRE(kf_idx3.has_value());
			CHECK(kf_idx3.value()->GetData().time == 3.0);

			auto kf_idx4 = clip->GetKeyframeByIndex(4);
			REQUIRE(kf_idx4.has_value());
			CHECK(kf_idx4.value()->GetData().time == 5.0);

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}

	SECTION("keyframe update") {
		try {
			SetDefaultConfig();

			auto timeline = ITimeline::New();
			auto clip = timeline->AddClip("UpdateClip");

			// Create initial keyframe
			ITimelineKeyframe::KeyframeData initialData;
			initialData.time = 1.0;
			initialData.camera = ITimelineKeyframe::CameraData();
			initialData.camera->isPause = false;
			initialData.atmo = ITimelineKeyframe::AtmoData();
			initialData.atmo->cloudCoverage = 0.2f;

			auto kfResult = clip->AddKeyframe(initialData);
			REQUIRE(kfResult.has_value());

			// Retrieve the keyframe
			auto kf = clip->GetKeyframe(1.0);
			REQUIRE(kf.has_value());

			// Update the keyframe data
			ITimelineKeyframe::KeyframeData updatedData;
			updatedData.time = 1.0; // Time should remain the same
			updatedData.camera = ITimelineKeyframe::CameraData();
			updatedData.camera->isPause = true; // Changed
			updatedData.atmo = ITimelineKeyframe::AtmoData();
			updatedData.atmo->cloudCoverage = 0.8f; // Changed
			updatedData.atmo->fog = 0.5f; // New field

			kf.value()->Update(updatedData);

			// Retrieve again and verify updates
			auto kfUpdated = clip->GetKeyframe(1.0);
			REQUIRE(kfUpdated.has_value());
			const auto& updatedKfData = kfUpdated.value()->GetData();

			CHECK(updatedKfData.time == 1.0);
			CHECK(updatedKfData.camera.has_value());
			CHECK(updatedKfData.camera->isPause == true);
			CHECK(updatedKfData.atmo.has_value());
			CHECK(updatedKfData.atmo->cloudCoverage == 0.8f);
			CHECK(updatedKfData.atmo->fog == 0.5f);

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}

	SECTION("multiple clips with keyframes") {
		try {
			SetDefaultConfig();

			auto timeline = ITimeline::New();

			// Create first clip with keyframes
			auto clip1 = timeline->AddClip("Clip1");
			ITimelineKeyframe::KeyframeData kf1_1;
			kf1_1.time = 0.0;
			kf1_1.camera = ITimelineKeyframe::CameraData();
			auto kf1_1Result = clip1->AddKeyframe(kf1_1);
			REQUIRE(kf1_1Result.has_value());

			ITimelineKeyframe::KeyframeData kf1_2;
			kf1_2.time = 1.0;
			kf1_2.atmo = ITimelineKeyframe::AtmoData();
			auto kf1_2Result = clip1->AddKeyframe(kf1_2);
			REQUIRE(kf1_2Result.has_value());

			// Create second clip with keyframes
			auto clip2 = timeline->AddClip("Clip2");
			ITimelineKeyframe::KeyframeData kf2_1;
			kf2_1.time = 0.5;
			kf2_1.synchro = ITimelineKeyframe::SynchroData();
			kf2_1.synchro->date = "2024-01-01";
			auto kf2_1Result = clip2->AddKeyframe(kf2_1);
			REQUIRE(kf2_1Result.has_value());

			ITimelineKeyframe::KeyframeData kf2_2;
			kf2_2.time = 2.0;
			auto kf2_2Result = clip2->AddKeyframe(kf2_2);
			REQUIRE(kf2_2Result.has_value());

			ITimelineKeyframe::KeyframeData kf2_3;
			kf2_3.time = 3.0;
			auto kf2_3Result = clip2->AddKeyframe(kf2_3);
			REQUIRE(kf2_3Result.has_value());

			// Verify clip counts
			CHECK(timeline->GetClipCount() == 2);
			CHECK(clip1->GetKeyframeCount() == 2);
			CHECK(clip2->GetKeyframeCount() == 3);

			// Verify clip independence
			auto retrievedClip1 = timeline->GetClipByIndex(0);
			REQUIRE(retrievedClip1.has_value());
			CHECK(retrievedClip1.value()->GetKeyframeCount() == 2);

			auto retrievedClip2 = timeline->GetClipByIndex(1);
			REQUIRE(retrievedClip2.has_value());
			CHECK(retrievedClip2.value()->GetKeyframeCount() == 3);

			// Verify keyframe data in each clip
			auto clip1Kf1 = retrievedClip1.value()->GetKeyframe(0.0);
			REQUIRE(clip1Kf1.has_value());
			CHECK(clip1Kf1.value()->GetData().camera.has_value());

			auto clip2Kf1 = retrievedClip2.value()->GetKeyframe(0.5);
			REQUIRE(clip2Kf1.has_value());
			CHECK(clip2Kf1.value()->GetData().synchro.has_value());

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}

	SECTION("edge cases") {
		try {
			SetDefaultConfig();

			auto timeline = ITimeline::New();

			// Test removing from empty timeline
			auto removeEmpty = timeline->RemoveClip(0);
			CHECK(!removeEmpty.has_value());

			// Test getting clip from out of bounds index
			auto outOfBounds = timeline->GetClipByIndex(999);
			CHECK(!outOfBounds.has_value());

			// Add a clip
			auto clip = timeline->AddClip("TestClip");

			// Test adding keyframe with negative time
			ITimelineKeyframe::KeyframeData negativeTimeKf;
			negativeTimeKf.time = -1.0;
			auto negResult = clip->AddKeyframe(negativeTimeKf);
			REQUIRE(negResult.has_value()); // Should succeed, negative time is valid

			// Test adding keyframe with very large time
			ITimelineKeyframe::KeyframeData largeTimeKf;
			largeTimeKf.time = 999999.999;
			auto largeResult = clip->AddKeyframe(largeTimeKf);
			REQUIRE(largeResult.has_value());

			// Test duplicate time keyframes
			ITimelineKeyframe::KeyframeData dup1;
			dup1.time = 5.0;
			dup1.camera = ITimelineKeyframe::CameraData();
			dup1.camera->isPause = false;
			auto dup1Result = clip->AddKeyframe(dup1);
			REQUIRE(dup1Result.has_value());

			ITimelineKeyframe::KeyframeData dup2;
			dup2.time = 5.0;
			dup2.camera = ITimelineKeyframe::CameraData();
			dup2.camera->isPause = true;
			auto dup2Result = clip->AddKeyframe(dup2);
			REQUIRE(!dup2Result.has_value()); // can't create a duplicate time keyframe, should return error

			auto kfAt5 = clip->GetKeyframe(5.0);
			CHECK(kfAt5.has_value());

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}

	SECTION("snapshot IDs") {
		try {
			SetDefaultConfig();

			auto timeline = ITimeline::New();
			auto clip = timeline->AddClip("SnapshotClip");

			// Set snapshot ID on clip
			clip->SetSnapshotId("snapshot_clip_001");
			CHECK(clip->GetSnapshotId() == "snapshot_clip_001");

			// Add keyframes with snapshot IDs
			ITimelineKeyframe::KeyframeData kf1;
			kf1.time = 0.0;
			auto kfResult1 = clip->AddKeyframe(kf1);
			REQUIRE(kfResult1.has_value());
			kfResult1.value()->SetSnapshotId("snapshot_kf_001");
			CHECK(kfResult1.value()->GetSnapshotId() == "snapshot_kf_001");

			ITimelineKeyframe::KeyframeData kf2;
			kf2.time = 1.0;
			auto kfResult2 = clip->AddKeyframe(kf2);
			REQUIRE(kfResult2.has_value());
			kfResult2.value()->SetSnapshotId("snapshot_kf_002");

			// Get all keyframe snapshot IDs
			std::vector<std::string> snapshotIds;
			clip->GetKeyFrameSnapshotIds(snapshotIds);
			CHECK(snapshotIds.size() == 2);
			CHECK(std::find(snapshotIds.begin(), snapshotIds.end(), "snapshot_kf_001") != snapshotIds.end());
			CHECK(std::find(snapshotIds.begin(), snapshotIds.end(), "snapshot_kf_002") != snapshotIds.end());

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}

	SECTION("save status lifecycle") {
		try {
			SetDefaultConfig();

			auto timeline = ITimeline::New();

			// A brand new timeline has never been saved and has nothing pending.
			CHECK(timeline->GetSaveStatus() == ESaveStatus::NeverSaved);
			CHECK(timeline->HasSomethingToSave() == false);

			// AddClip calls InvalidateDB on the timeline, and SetName on the clip.
			auto clip = timeline->AddClip("SaveClip");
			REQUIRE(clip != nullptr);
			CHECK(timeline->GetSaveStatus() == ESaveStatus::ShouldSave);
			CHECK(clip->GetSaveStatus() == ESaveStatus::ShouldSave);
			CHECK(timeline->HasSomethingToSave() == true);

			// AddKeyframe marks the keyframe dirty and invalidates the clip.
			ITimelineKeyframe::KeyframeData kfData;
			kfData.time = 1.0;
			auto kfResult = clip->AddKeyframe(kfData);
			REQUIRE(kfResult.has_value());
			auto kf = kfResult.value();
			CHECK(kf->GetSaveStatus() == ESaveStatus::ShouldSave);
			CHECK(clip->HasSomethingToSave() == true);

			// Simulate a save round-trip of the keyframes.
			clip->OnStartSaveKeyframes();
			CHECK(kf->GetSaveStatus() == ESaveStatus::InProgress);
			clip->OnKeyframesSaved();
			CHECK(kf->GetSaveStatus() == ESaveStatus::Done);

			// Clip itself is still dirty, so the timeline still has work to do.
			CHECK(clip->HasSomethingToSave() == true);
			clip->SetShouldSave(false);
			CHECK(clip->HasSomethingToSave() == false);

			// Mutating a keyframe re-dirties the clip (and therefore the timeline).
			kf->Update(kfData);
			CHECK(kf->GetSaveStatus() == ESaveStatus::ShouldSave);
			CHECK(clip->HasSomethingToSave() == true);
			CHECK(timeline->HasSomethingToSave() == true);

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}

	SECTION("save status - invalidation during an in-flight save") {
		try {
			SetDefaultConfig();

			auto timeline = ITimeline::New();
			auto clip = timeline->AddClip("RaceClip");

			ITimelineKeyframe::KeyframeData kfData;
			kfData.time = 0.0;
			auto kfResult = clip->AddKeyframe(kfData);
			REQUIRE(kfResult.has_value());
			auto kf = kfResult.value();

			// Start saving...
			clip->OnStartSaveKeyframes();
			CHECK(kf->GetSaveStatus() == ESaveStatus::InProgress);

			// ...the user edits the keyframe while the request is in flight.
			ITimelineKeyframe::KeyframeData edited;
			edited.time = 0.0;
			edited.camera = ITimelineKeyframe::CameraData();
			kf->Update(edited);
			CHECK(kf->GetSaveStatus() == ESaveStatus::ShouldSave);

			// The completion callback must NOT clear the pending edit.
			clip->OnKeyframesSaved();
			CHECK(kf->GetSaveStatus() == ESaveStatus::ShouldSave);
			CHECK(clip->HasSomethingToSave() == true);

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}

	SECTION("SetShouldSaveRecursive") {
		try {
			SetDefaultConfig();

			auto timeline = ITimeline::New();
			auto clip1 = timeline->AddClip("Clip1");
			auto clip2 = timeline->AddClip("Clip2");

			ITimelineKeyframe::KeyframeData kfData;
			kfData.time = 0.0;
			auto kfResult = clip1->AddKeyframe(kfData);
			REQUIRE(kfResult.has_value());
			auto kf = kfResult.value();

			timeline->SetShouldSaveRecursive(true);
			CHECK(timeline->GetSaveStatus() == ESaveStatus::ShouldSave);
			CHECK(clip1->GetSaveStatus() == ESaveStatus::ShouldSave);
			CHECK(clip2->GetSaveStatus() == ESaveStatus::ShouldSave);

			timeline->SetShouldSaveRecursive(false);
			CHECK(timeline->GetSaveStatus() == ESaveStatus::Done);
			CHECK(clip1->GetSaveStatus() == ESaveStatus::Done);
			CHECK(clip2->GetSaveStatus() == ESaveStatus::Done);

			// NOTE: SetShouldSaveRecursive only descends to clips, NOT to keyframes.
			// The keyframe stays dirty, so the timeline still reports pending work.
			CHECK(kf->GetSaveStatus() == ESaveStatus::ShouldSave);
			CHECK(timeline->HasSomethingToSave() == true);

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}

	SECTION("keyframe time rounding (RoundTime, ms precision)") {
		try {
			SetDefaultConfig();

			CHECK(RoundTime(1.00049) == 1.0);
			CHECK(RoundTime(1.0006) == 1.001);
			CHECK(RoundTime(-1.0004) == -1.0);

			auto timeline = ITimeline::New();
			auto clip = timeline->AddClip("RoundingClip");

			// Stored time is rounded to the millisecond on creation.
			ITimelineKeyframe::KeyframeData kfData;
			kfData.time = 1.00049;
			auto kfResult = clip->AddKeyframe(kfData);
			REQUIRE(kfResult.has_value());
			CHECK(kfResult.value()->GetData().time == 1.0);

			// Lookup also rounds, so sub-ms variations resolve to the same keyframe.
			CHECK(clip->GetKeyframe(1.0).has_value());
			CHECK(clip->GetKeyframe(1.00049).has_value());
			CHECK(clip->GetKeyframe(0.99951).has_value());
			CHECK(!clip->GetKeyframe(1.002).has_value());

			// A time that rounds to an existing one is rejected as a duplicate.
			ITimelineKeyframe::KeyframeData nearDup;
			nearDup.time = 1.00041;
			CHECK(!clip->AddKeyframe(nearDup).has_value());
			CHECK(clip->GetKeyframeCount() == 1);

			// A time that rounds to a distinct millisecond is accepted.
			ITimelineKeyframe::KeyframeData distinct;
			distinct.time = 1.0006;
			auto distinctResult = clip->AddKeyframe(distinct);
			REQUIRE(distinctResult.has_value());
			CHECK(distinctResult.value()->GetData().time == 1.001);
			CHECK(clip->GetKeyframeCount() == 2);

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}

	SECTION("Update ignores time and id, and overwrites optionals wholesale") {
		try {
			SetDefaultConfig();

			auto timeline = ITimeline::New();
			auto clip = timeline->AddClip("UpdateSemanticsClip");

			ITimelineKeyframe::KeyframeData initial;
			initial.time = 2.0;
			initial.camera = ITimelineKeyframe::CameraData();
			initial.camera->isPause = true;
			initial.atmo = ITimelineKeyframe::AtmoData();
			initial.atmo->cloudCoverage = 0.4f;
			initial.synchro = ITimelineKeyframe::SynchroData();
			initial.synchro->date = "2024-01-01";

			auto kfResult = clip->AddKeyframe(initial);
			REQUIRE(kfResult.has_value());
			auto kf = kfResult.value();

			// Attempt to change the time through Update: must be ignored.
			ITimelineKeyframe::KeyframeData retimed;
			retimed.time = 42.0;
			retimed.camera = ITimelineKeyframe::CameraData();
			kf->Update(retimed);

			CHECK(kf->GetData().time == 2.0);
			CHECK(clip->GetKeyframe(2.0).has_value());
			CHECK(!clip->GetKeyframe(42.0).has_value());
			CHECK(clip->GetKeyframeCount() == 1);

			// Optionals left unset in the new data are cleared, not merged.
			CHECK(kf->GetData().camera.has_value());
			CHECK(!kf->GetData().atmo.has_value());
			CHECK(!kf->GetData().synchro.has_value());

			// Ordering is still intact after the ignored re-time.
			auto byIndex = clip->GetKeyframeByIndex(0);
			REQUIRE(byIndex.has_value());
			CHECK(byIndex.value()->GetData().time == 2.0);

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}

	SECTION("SetKeyFrameTimes") {
		try {
			SetDefaultConfig();

			auto timeline = ITimeline::New();
			auto clip = std::static_pointer_cast<TimelineClip>(timeline->AddClip("RetimeClip"));
			REQUIRE(clip != nullptr);

			for (double t : { 0.0, 1.0, 2.0, 3.0 })
			{
				ITimelineKeyframe::KeyframeData kfData;
				kfData.time = t;
				REQUIRE(clip->AddKeyframe(kfData).has_value());
			}
			REQUIRE(clip->GetKeyframeCount() == 4);

			// Order-preserving retime (a uniform stretch) must succeed.
			auto res = clip->SetKeyFrameTimes({ 0.0f, 2.0f, 4.0f, 6.0f });
			REQUIRE(res.has_value());
			CHECK(clip->GetKeyframeCount() == 4);
			for (size_t i = 0; i < 4; ++i)
			{
				auto kf = clip->GetKeyframeByIndex(i);
				REQUIRE(kf.has_value());
				CHECK(kf.value()->GetData().time == static_cast<double>(i) * 2.0);
			}

			// Retimed key-frames must remain findable by time, and dirty.
			CHECK(clip->GetKeyframe(4.0).has_value());
			CHECK(!clip->GetKeyframe(1.0).has_value());
			auto idx = clip->GetKeyframeIndex(6.0);
			REQUIRE(idx.has_value());
			CHECK(idx.value() == 3);
			CHECK(clip->HasSomethingToSave() == true);

			// Mismatched size is rejected, clip left untouched.
			auto badSize = clip->SetKeyFrameTimes({ 0.0f, 1.0f });
			REQUIRE(!badSize.has_value());
			CHECK(!badSize.error().empty());
			CHECK(clip->GetKeyframeCount() == 4);
			auto unchanged = clip->GetKeyframeByIndex(1);
			REQUIRE(unchanged.has_value());
			CHECK(unchanged.value()->GetData().time == 2.0);

			// Retiming an empty clip with an empty vector is a valid no-op.
			auto emptyClip = std::static_pointer_cast<TimelineClip>(timeline->AddClip("EmptyRetime"));
			CHECK(emptyClip->SetKeyFrameTimes({}).has_value());

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}

	SECTION("SetKeyFrameTimes rejects non strictly increasing times") {
		try {
			SetDefaultConfig();

			auto timeline = ITimeline::New();

			auto makeClip = [&timeline](const char* name) {
				auto c = std::static_pointer_cast<TimelineClip>(timeline->AddClip(name));
				for (double t : { 0.0, 1.0, 2.0 })
				{
					ITimelineKeyframe::KeyframeData kfData;
					kfData.time = t;
					REQUIRE(c->AddKeyframe(kfData));
				}
				return c;
			};

			auto checkUntouched = [](std::shared_ptr<TimelineClip> const& c) {
				REQUIRE(c->GetKeyframeCount() == 3);
				for (size_t i = 0; i < 3; ++i)
				{
					auto kf = c->GetKeyframeByIndex(i);
					REQUIRE(kf.has_value());
					CHECK(kf.value()->GetData().time == static_cast<double>(i));
				}
			};

			// Duplicate times would put two key-frames at the same time in the ordered set.
			auto dupClip = makeClip("DupClip");
			auto dupRes = dupClip->SetKeyFrameTimes({ 0.0f, 1.0f, 1.0f });
			REQUIRE(!dupRes.has_value());
			CHECK(!dupRes.error().empty());
			checkUntouched(dupClip);

			// Distinct floats that collapse onto the same millisecond are duplicates too.
			auto roundClip = makeClip("RoundClip");
			CHECK(!roundClip->SetKeyFrameTimes({ 0.0f, 1.0f, 1.00041f }).has_value());
			checkUntouched(roundClip);

			// Unsorted input must be rejected outright, not silently applied.
			auto unsortedClip = makeClip("UnsortedClip");
			CHECK(!unsortedClip->SetKeyFrameTimes({ 0.0f, 5.0f, 2.0f }).has_value());
			checkUntouched(unsortedClip);

			// Rejection must be atomic: no key-frame is retimed, not even the leading ones
			// that were individually valid.
			auto atomicClip = makeClip("AtomicClip");
			CHECK(!atomicClip->SetKeyFrameTimes({ 10.0f, 20.0f, 20.0f }).has_value());
			checkUntouched(atomicClip);
			CHECK(!atomicClip->GetKeyframe(10.0).has_value());

			// A failed retime must not have dirtied anything either.
			atomicClip->SetShouldSave(false);
			atomicClip->OnStartSaveKeyframes();
			atomicClip->OnKeyframesSaved();
			REQUIRE(atomicClip->HasSomethingToSave() == false);
			CHECK(!atomicClip->SetKeyFrameTimes({ 0.0f, 0.0f, 0.0f }).has_value());
			CHECK(atomicClip->HasSomethingToSave() == false);

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}

	SECTION("SetKeyFrameTimes rounds times to ms") {
		try {
			SetDefaultConfig();

			auto timeline = ITimeline::New();
			auto clip = std::static_pointer_cast<TimelineClip>(timeline->AddClip("RoundRetimeClip"));
			for (double t : { 0.0, 1.0 })
			{
				ITimelineKeyframe::KeyframeData kfData;
				kfData.time = t;
				REQUIRE(clip->AddKeyframe(kfData).has_value());
			}

			// 0.1f widens to 0.100000001490116..., which GetKeyframe(0.1) would never match
			// unless the stored time is rounded the same way AddKeyframe rounds it.
			REQUIRE(clip->SetKeyFrameTimes({ 0.1f, 0.2f }).has_value());

			auto kf0 = clip->GetKeyframeByIndex(0);
			REQUIRE(kf0.has_value());
			CHECK(kf0.value()->GetData().time == 0.1);

			CHECK(clip->GetKeyframe(0.1).has_value());
			CHECK(clip->GetKeyframe(0.2).has_value());

			auto idx = clip->GetKeyframeIndex(0.2);
			REQUIRE(idx.has_value());
			CHECK(idx.value() == 1);

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}

	SECTION("obsolete clips (sceneAPI)") {
		try {
			SetDefaultConfig();

			auto timeline = ITimeline::New();
			auto clip1 = timeline->AddClip("Clip1");
			auto clip2 = timeline->AddClip("Clip2");
			REQUIRE(timeline->GetClipCount() == 2);
			CHECK(timeline->GetObsoleteClips().empty());

			// Removing a clip queues it for deletion on the server.
			REQUIRE(timeline->RemoveClip(0).has_value());
			CHECK(timeline->GetClipCount() == 1);

			auto obsolete = timeline->GetObsoleteClips();
			REQUIRE(obsolete.size() == 1);
			CHECK(obsolete[0] == clip1);

			// Acknowledging the deletion dequeues it.
			timeline->RemoveObsoleteClip(obsolete[0]);
			CHECK(timeline->GetObsoleteClips().empty());

			// Removing a clip that was never queued is a no-op.
			timeline->RemoveObsoleteClip(clip2);
			CHECK(timeline->GetObsoleteClips().empty());
			CHECK(timeline->GetClipCount() == 1);

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}

	SECTION("clip lookup and move edge cases") {
		try {
			SetDefaultConfig();

			auto timeline = ITimeline::New();
			auto clipA = timeline->AddClip("A");
			auto clipB = timeline->AddClip("B");
			auto clipC = timeline->AddClip("C");

			// Unknown RefID returns null rather than throwing.
			RefID unknownId;
			CHECK(timeline->GetClipByRefID(unknownId) == nullptr);

			// A removed clip is no longer reachable by RefID.
			RefID idA = clipA->GetId();
			REQUIRE(timeline->GetClipByRefID(idA) != nullptr);
			REQUIRE(timeline->RemoveClip(0).has_value());
			CHECK(timeline->GetClipByRefID(idA) == nullptr);

			// Re-populate: A, B, C
			timeline->AddClip("D");
			REQUIRE(timeline->GetClipCount() == 3); // B, C, D

			// Move to the same index is a no-op.
			timeline->MoveClip(1, 1);
			auto same = timeline->GetClipByIndex(1);
			REQUIRE(same.has_value());
			CHECK(same.value()->GetName() == "C");

			// Forward move: B (0) -> 2
			timeline->MoveClip(0, 2);
			auto i0 = timeline->GetClipByIndex(0);
			auto i1 = timeline->GetClipByIndex(1);
			auto i2 = timeline->GetClipByIndex(2);
			REQUIRE(i0.has_value());
			REQUIRE(i1.has_value());
			REQUIRE(i2.has_value());
			CHECK(i0.value()->GetName() == "C");
			CHECK(i1.value()->GetName() == "D");
			CHECK(i2.value()->GetName() == "B");

			// Out-of-bounds moves are ignored and must not reorder or crash.
			timeline->MoveClip(0, 99);
			timeline->MoveClip(99, 0);
			CHECK(timeline->GetClipCount() == 3);
			auto after = timeline->GetClipByIndex(0);
			REQUIRE(after.has_value());
			CHECK(after.value()->GetName() == "C");

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}

	SECTION("keyframe removal edge cases") {
		try {
			SetDefaultConfig();

			auto timeline = ITimeline::New();
			auto clip1 = timeline->AddClip("Clip1");
			auto clip2 = timeline->AddClip("Clip2");

			ITimelineKeyframe::KeyframeData kfData;
			kfData.time = 1.0;
			auto kfResult = clip1->AddKeyframe(kfData);
			REQUIRE(kfResult.has_value());
			auto kf = kfResult.value();

			// Removing a keyframe owned by another clip must fail.
			// NOTE: lookup is by time ordering, so clip2 must not hold time 1.0.
			auto foreign = kf;
			CHECK(!clip2->RemoveKeyframe(foreign).has_value());
			CHECK(clip1->GetKeyframeCount() == 1);

			// First removal succeeds, second fails.
			REQUIRE(clip1->RemoveKeyframe(kf).has_value());
			CHECK(clip1->GetKeyframeCount() == 0);
			CHECK(!clip1->RemoveKeyframe(kf).has_value());

			// Re-adding the same time after removal is allowed.
			auto readded = clip1->AddKeyframe(kfData);
			CHECK(readded.has_value());
			CHECK(clip1->GetKeyframeCount() == 1);

			// Index/time lookups on a non-empty clip, out of range.
			CHECK(!clip1->GetKeyframeByIndex(1).has_value());
			CHECK(!clip1->GetKeyframeIndex(99.0).has_value());

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}

	SECTION("snapshot ids - partially assigned") {
		try {
			SetDefaultConfig();

			auto timeline = ITimeline::New();
			auto clip = timeline->AddClip("PartialSnapshotClip");

			// Defaults are empty strings, not unset.
			CHECK(clip->GetSnapshotId() == "");

			ITimelineKeyframe::KeyframeData kf1;
			kf1.time = 0.0;
			auto r1 = clip->AddKeyframe(kf1);
			REQUIRE(r1.has_value());
			CHECK(r1.value()->GetSnapshotId() == "");

			ITimelineKeyframe::KeyframeData kf2;
			kf2.time = 1.0;
			auto r2 = clip->AddKeyframe(kf2);
			REQUIRE(r2.has_value());
			r2.value()->SetSnapshotId("snapshot_kf_002");

			// One id per keyframe, in time order, empty for the unassigned one.
			std::vector<std::string> snapshotIds;
			clip->GetKeyFrameSnapshotIds(snapshotIds);
			REQUIRE(snapshotIds.size() == 2);
			CHECK(snapshotIds[0] == "");
			CHECK(snapshotIds[1] == "snapshot_kf_002");

			// The output vector is appended to, not cleared.
			clip->GetKeyFrameSnapshotIds(snapshotIds);
			CHECK(snapshotIds.size() == 4);

		}
		catch (std::string& error)
		{
			FAIL("Error: " << error);
		}
	}
}
