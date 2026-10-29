/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSequencerHelperTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Misc/AutomationTest.h>

#if WITH_TESTS

#include <AnimTimeline/ITwinSequencerHelper.h>

#include <Camera/CameraActor.h>
#include <Channels/MovieSceneDoubleChannel.h>
#include <Channels/MovieSceneBoolChannel.h>
#include <Channels/MovieSceneChannelProxy.h>
#include <GameFramework/Actor.h>
#include <LevelSequence.h>
#include <MovieScene.h>
#include <MovieSceneSection.h>
#include <MovieSceneTrack.h>
#include <Sections/MovieSceneBoolSection.h>
#include <Sections/MovieSceneCameraCutSection.h>
#include <Sections/MovieSceneStringSection.h>
#include <Sections/MovieSceneVectorSection.h>
#include <Tracks/MovieScene3DTransformTrack.h>
#include <Tracks/MovieSceneBoolTrack.h>
#include <Tracks/MovieSceneCameraCutTrack.h>
#include <Tracks/MovieSceneDoubleTrack.h>
#include <Tracks/MovieSceneFloatTrack.h>
#include <Tracks/MovieSceneStringTrack.h>

#include <UObject/Package.h>
#include <UObject/StrongObjectPtr.h>

#include <Tests/TestWorldHelpers.h>

#include "AnimTimeline/ITwinSequencerHelperInternals.h"

#include <string>

#define SEQHELPER_TEST_PREFIX "Bentley.ITwinForUnreal.ITwinRuntime.ITwinSequencerHelper."
#define SEQHELPER_TEST_FLAGS \
	(EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

namespace SequencerHelperTests
{
	using ITwin::SequencerHelper::ComputeEvenlySpacedIndices;

	/// Compact, readable rendering of an index array for failure messages.
	FString ToString(const TArray<int32>& Indices)
	{
		FString Out = TEXT("{");
		for (int32 i = 0; i < Indices.Num(); ++i)
			Out += (i ? TEXT(", ") : TEXT("")) + FString::FromInt(Indices[i]);
		return Out + TEXT("}");
	}

	void CheckIndices(FAutomationTestBase& Test, const TCHAR* What,
		const TArray<int32>& Actual, const TArray<int32>& Expected)
	{
		Test.TestEqual(FString::Printf(TEXT("%s -> %s"), What, *ToString(Expected)),
			ToString(Actual), ToString(Expected));
	}

	/// Creates a transient section of the given type. No world and no asset are needed:
	/// the helpers under test only ever touch the section's channel proxy.
	template<class SectionType>
	SectionType* NewTransientSection()
	{
		return NewObject<SectionType>(GetTransientPackage(), SectionType::StaticClass(),
			NAME_None, RF_Transient);
	}

	/// Guards every section-based test: if a section type did not populate its channel
	/// proxy on construction, the helpers would return false for reasons unrelated to the
	/// behaviour under test, and the assertions below would be vacuous.
	bool VerifyChannelCount(FAutomationTestBase& Test, UMovieSceneSection* pSection, int Expected)
	{
		if (!Test.TestNotNull(TEXT("Section created"), pSection))
			return false;
		return Test.TestEqual(TEXT("Section exposes the expected number of channels"),
			pSection->GetChannelProxy().NumChannels(), Expected);
	}

	TArray<FFrameNumber> Frames(std::initializer_list<int32> Values)
	{
		TArray<FFrameNumber> Out;
		for (int32 Value : Values)
			Out.Add(FFrameNumber(Value));
		return Out;
	}

	TArray<FFrameNumber> TimesOf(const FMovieSceneBoolChannel& Channel)
	{
		return TArray<FFrameNumber>(Channel.GetTimes());
	}

	/// Evaluate() binary-searches the Times array, so ascending order is a hard
	/// precondition, not an implementation detail. Key count alone does not catch a
	/// violation: an appended key keeps the count right and corrupts every later lookup.
	template<class ChannelType>
	void CheckSorted(FAutomationTestBase& Test, const ChannelType* pChannel, const TCHAR* What)
	{
		if (!Test.TestNotNull(What, pChannel))
			return;
		TArrayView<const FFrameNumber> Times = pChannel->GetTimes();
		for (int32 i = 1; i < Times.Num(); ++i)
			Test.TestTrue(
				FString::Printf(TEXT("%s: key times ascending at %d (%d after %d)"),
					What, i, Times[i].Value, Times[i - 1].Value),
				Times[i] > Times[i - 1]);
	}
}

//-----------------------------------------------------------------------------
// ComputeEvenlySpacedIndices - pure index arithmetic, no engine objects at all.
//-----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeqHelperEvenlySpacedIndicesTest,
	SEQHELPER_TEST_PREFIX "EvenlySpacedIndices", SEQHELPER_TEST_FLAGS)

bool FSeqHelperEvenlySpacedIndicesTest::RunTest(const FString& /*Parameters*/)
{
	using namespace SequencerHelperTests;

	// Degenerate inputs yield nothing rather than asserting.
	CheckIndices(*this, TEXT("Count 0"),  ComputeEvenlySpacedIndices(0, 5),  {});
	CheckIndices(*this, TEXT("Count -1"), ComputeEvenlySpacedIndices(-1, 5), {});
	CheckIndices(*this, TEXT("Size 0"),   ComputeEvenlySpacedIndices(3, 0),  {});
	CheckIndices(*this, TEXT("Size -1"),  ComputeEvenlySpacedIndices(3, -1), {});

	// A single index maps to the *first* element, not the last. Pinned deliberately:
	// it is the one case where the "first and last included" rule cannot hold.
	CheckIndices(*this, TEXT("Count 1 of 5"), ComputeEvenlySpacedIndices(1, 5), {0});
	CheckIndices(*this, TEXT("Count 1 of 1"), ComputeEvenlySpacedIndices(1, 1), {0});

	// Count == Size is the identity: every element is picked, in order.
	CheckIndices(*this, TEXT("Count 4 of 4"), ComputeEvenlySpacedIndices(4, 4), {0, 1, 2, 3});

	// Proper subsets stay centred on the exact ratio.
	CheckIndices(*this, TEXT("Count 2 of 5"), ComputeEvenlySpacedIndices(2, 5), {0, 4});
	CheckIndices(*this, TEXT("Count 3 of 9"), ComputeEvenlySpacedIndices(3, 9), {0, 4, 8});
	CheckIndices(*this, TEXT("Count 3 of 5"), ComputeEvenlySpacedIndices(3, 5), {0, 2, 4});

	// Count close to Size makes naive rounding repeat an index; the collision branch
	// must force strict growth while still ending on the last element.
	CheckIndices(*this, TEXT("Count 4 of 5"), ComputeEvenlySpacedIndices(4, 5), {0, 1, 3, 4});

	// Across the whole plausible range, the two invariants callers rely on: the result
	// spans the full array, and no index is ever repeated (two keys sharing a frame
	// would silently collapse into one).
	for (int32 Size = 1; Size <= 24; ++Size)
	{
		for (int32 Count = 1; Count <= Size; ++Count)
		{
			const TArray<int32> Indices = ComputeEvenlySpacedIndices(Count, Size);
			const FString Case = FString::Printf(TEXT("Count %d of %d"), Count, Size);

			if (!TestEqual(Case + TEXT(" returns Count indices"), Indices.Num(), Count))
				continue;
			TestEqual(Case + TEXT(" starts at 0"), Indices[0], 0);
			if (Count > 1)
				TestEqual(Case + TEXT(" ends at Size-1"), Indices.Last(), Size - 1);
			for (int32 i = 1; i < Indices.Num(); ++i)
				TestTrue(FString::Printf(TEXT("%s is strictly increasing at %d (%s)"),
					*Case, i, *ToString(Indices)), Indices[i] > Indices[i - 1]);
			for (int32 Index : Indices)
				TestTrue(Case + TEXT(" stays in range"), Index >= 0 && Index < Size);
		}
	}

	// Count > Size violates the documented contract. It must not read out of bounds;
	// indices clamp to Size-1 and strict growth is necessarily lost. Pinned so that a
	// future change to the clamp is a deliberate decision rather than an accident.
	const TArray<int32> Overflow = ComputeEvenlySpacedIndices(5, 3);
	TestEqual(TEXT("Count > Size still returns Count indices"), Overflow.Num(), 5);
	for (int32 Index : Overflow)
		TestTrue(TEXT("Count > Size keeps indices in range"), Index >= 0 && Index < 3);

	return true;
}

//-----------------------------------------------------------------------------
// SetKeyFrameTimesInChannel - retiming, including the two out-of-sync branches.
//-----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeqHelperSetKeyFrameTimesTest,
	SEQHELPER_TEST_PREFIX "SetKeyFrameTimesInChannel", SEQHELPER_TEST_FLAGS)

bool FSeqHelperSetKeyFrameTimesTest::RunTest(const FString& /*Parameters*/)
{
	using namespace SequencerHelperTests;

	// Null channel and empty time list are no-ops.
	FSequencerHelperTestAccess::SetKeyFrameTimesInChannel(nullptr, Frames({0, 10}));

	// An unanimated channel is left alone (this happens for real, e.g. an atmo track on
	// a clip that carries no atmo data).
	{
		FMovieSceneBoolChannel Channel;
		FSequencerHelperTestAccess::SetKeyFrameTimesInChannel(&Channel, Frames({0, 10, 20}));
		TestEqual(TEXT("Empty channel stays empty"), Channel.GetTimes().Num(), 0);
	}

	// Matching counts: the nominal path, times replaced one for one.
	{
		FMovieSceneBoolChannel Channel;
		Channel.AddKeys(Frames({0, 10, 20}), {true, false, true});
		FSequencerHelperTestAccess::SetKeyFrameTimesInChannel(&Channel, Frames({5, 50, 500}));

		const TArray<FFrameNumber> Times = TimesOf(Channel);
		if (TestEqual(TEXT("Key count unchanged"), Times.Num(), 3))
		{
			TestEqual(TEXT("Key 0 retimed"), Times[0].Value, 5);
			TestEqual(TEXT("Key 1 retimed"), Times[1].Value, 50);
			TestEqual(TEXT("Key 2 retimed"), Times[2].Value, 500);
		}
	}

	// Fewer keys than times: remap onto an evenly spaced subset, keeping first and last
	// so the channel still spans the whole clip.
	{
		FMovieSceneBoolChannel Channel;
		Channel.AddKeys(Frames({0, 1, 2}), {true, false, true});
		AddExpectedError(TEXT("remapping onto a subset"), EAutomationExpectedErrorFlags::Contains, 0);
		FSequencerHelperTestAccess::SetKeyFrameTimesInChannel(&Channel, Frames({10, 20, 30, 40, 50}));

		const TArray<FFrameNumber> Times = TimesOf(Channel);
		if (TestEqual(TEXT("Key count still 3"), Times.Num(), 3))
		{
			// ComputeEvenlySpacedIndices(3, 5) == {0, 2, 4}
			TestEqual(TEXT("First key takes the first new time"), Times[0].Value, 10);
			TestEqual(TEXT("Middle key takes the middle new time"), Times[1].Value, 30);
			TestEqual(TEXT("Last key takes the last new time"), Times[2].Value, 50);
		}
	}

	// More keys than times: two keys cannot share a frame and a partial retime would
	// leave the channel out of order, so it must be left *completely* untouched.
	{
		FMovieSceneBoolChannel Channel;
		Channel.AddKeys(Frames({0, 10, 20, 30}), {true, false, true, false});
		AddExpectedError(TEXT("leaving it unchanged"), EAutomationExpectedErrorFlags::Contains, 0);
		FSequencerHelperTestAccess::SetKeyFrameTimesInChannel(&Channel, Frames({100, 200}));

		const TArray<FFrameNumber> Times = TimesOf(Channel);
		if (TestEqual(TEXT("Key count unchanged"), Times.Num(), 4))
		{
			TestEqual(TEXT("Key 0 untouched"), Times[0].Value, 0);
			TestEqual(TEXT("Key 1 untouched"), Times[1].Value, 10);
			TestEqual(TEXT("Key 2 untouched"), Times[2].Value, 20);
			TestEqual(TEXT("Key 3 untouched"), Times[3].Value, 30);
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
// String channel - the FString and std::string specialisations must agree, and the
// std::string one must survive a UTF-8 round trip.
//-----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeqHelperStringChannelTest,
	SEQHELPER_TEST_PREFIX "StringChannel", SEQHELPER_TEST_FLAGS)

bool FSeqHelperStringChannelTest::RunTest(const FString& /*Parameters*/)
{
	using namespace SequencerHelperTests;

	UMovieSceneStringSection* pSection = NewTransientSection<UMovieSceneStringSection>();
	if (!VerifyChannelCount(*this, pSection, 1))
		return false;

	// Nothing keyed yet: evaluation fails rather than returning a stale value.
	{
		FString Out = TEXT("untouched");
		TestFalse(TEXT("Empty channel evaluates to false"),
			FSequencerHelperTestAccess::GetStringFromChannel(pSection, 0, FFrameNumber(0), Out));
	}

	// A bad channel index must fail cleanly, not crash.
	{
		FString Out;
		TestFalse(TEXT("Out-of-range channel index fails"),
			FSequencerHelperTestAccess::GetStringFromChannel(pSection, 7, FFrameNumber(0), Out));
		TestFalse(TEXT("Null section fails"),
			FSequencerHelperTestAccess::GetStringFromChannel(nullptr, 0, FFrameNumber(0), Out));
		TestFalse(TEXT("Null section fails on add"),
			FSequencerHelperTestAccess::AddStdStringKeyFrameToChannel(
				nullptr, 0, FFrameNumber(0), std::string("x"), 2));
	}

	TestTrue(TEXT("Key added at frame 0"),
		FSequencerHelperTestAccess::AddStdStringKeyFrameToChannel(
			pSection, 0, FFrameNumber(0), std::string("first"), 2));
	TestTrue(TEXT("Key added at frame 100"),
		FSequencerHelperTestAccess::AddStdStringKeyFrameToChannel(
			pSection, 0, FFrameNumber(100), std::string("second"), 2));
	CheckSorted(*this, pSection->GetChannelProxy().GetChannel<FMovieSceneStringChannel>(0),
		TEXT("after the initial keys"));

	// Both specialisations read back the same key.
	{
		FString OutF;
		std::string OutS;
		TestTrue(TEXT("FString read succeeds"),
			FSequencerHelperTestAccess::GetStringFromChannel(pSection, 0, FFrameNumber(0), OutF));
		TestEqual(TEXT("FString value at frame 0"), OutF, FString(TEXT("first")));

		TestTrue(TEXT("std::string read succeeds"),
			FSequencerHelperTestAccess::GetStdStringFromChannel(pSection, 0, FFrameNumber(0), OutS));
		TestEqual(TEXT("std::string value at frame 0"), FString(OutS.c_str()), FString(TEXT("first")));

		FSequencerHelperTestAccess::GetStringFromChannel(pSection, 0, FFrameNumber(100), OutF);
		TestEqual(TEXT("FString value at frame 100"), OutF, FString(TEXT("second")));
	}

	// String channels hold their value until the next key (no interpolation).
	{
		FString Out;
		FSequencerHelperTestAccess::GetStringFromChannel(pSection, 0, FFrameNumber(50), Out);
		TestEqual(TEXT("Value holds between keys"), Out, FString(TEXT("first")));
	}

	// Re-keying the same frame replaces rather than duplicates.
	{
		TestTrue(TEXT("Re-key at frame 0"),
			FSequencerHelperTestAccess::AddStdStringKeyFrameToChannel(
				pSection, 0, FFrameNumber(0), std::string("replaced"), 2));

		const FMovieSceneStringChannel* pChannel =
			pSection->GetChannelProxy().GetChannel<FMovieSceneStringChannel>(0);
		TestEqual(TEXT("Still only two keys"), pChannel->GetTimes().Num(), 2);
		CheckSorted(*this, pChannel, TEXT("after re-keying frame 0"));

		FString Out;
		FSequencerHelperTestAccess::GetStringFromChannel(pSection, 0, FFrameNumber(0), Out);
		TestEqual(TEXT("Value replaced"), Out, FString(TEXT("replaced")));
	}

	// The std::string specialisation converts through UTF-8 (TCHAR_TO_UTF8), so a
	// non-ASCII payload is the case that actually exercises the conversion.
	{
		const std::string NonAscii = "caf\xC3\xA9 \xE2\x80\x94 \xC3\xA9t\xC3\xA9"; // "coffee - summer" in French
		TestTrue(TEXT("Non-ASCII key added"),
			FSequencerHelperTestAccess::AddStdStringKeyFrameToChannel(
				pSection, 0, FFrameNumber(200), NonAscii, 2));
		CheckSorted(*this, pSection->GetChannelProxy().GetChannel<FMovieSceneStringChannel>(0),
			TEXT("after the non-ASCII key"));

		// Round trip through std::string.
		std::string OutS;
		TestTrue(TEXT("Non-ASCII read succeeds"),
			FSequencerHelperTestAccess::GetStdStringFromChannel(pSection, 0, FFrameNumber(200), OutS));
		TestTrue(TEXT("Non-ASCII survives the UTF-8 round trip"), OutS == NonAscii);

		// ...and independently, what the channel actually stores. A round trip alone would
		// still pass if both conversions were wrong in compensating ways; this pins the
		// decoded form, so write and read are each anchored.
		FString OutF;
		TestTrue(TEXT("Non-ASCII FString read succeeds"),
			FSequencerHelperTestAccess::GetStringFromChannel(pSection, 0, FFrameNumber(200), OutF));
		TestEqual(TEXT("Channel stores the correctly decoded string"),
			OutF, FString(TEXT("caf\u00E9 \u2014 \u00E9t\u00E9")));
		TestEqual(TEXT("...of the expected length"), OutF.Len(), 10);
	}

	return true;
}

//-----------------------------------------------------------------------------
// Bool channel - the specialisation ignores iKeyInterp (bools cannot be cubic).
//-----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeqHelperBoolChannelTest,
	SEQHELPER_TEST_PREFIX "BoolChannel", SEQHELPER_TEST_FLAGS)

bool FSeqHelperBoolChannelTest::RunTest(const FString& /*Parameters*/)
{
	using namespace SequencerHelperTests;

	UMovieSceneBoolSection* pSection = NewTransientSection<UMovieSceneBoolSection>();
	if (!VerifyChannelCount(*this, pSection, 1))
		return false;

	TestFalse(TEXT("Null section fails"),
		FSequencerHelperTestAccess::AddBoolKeyFrameToChannel(nullptr, 0, FFrameNumber(0), true, 2));
	TestFalse(TEXT("Out-of-range channel index fails"),
		FSequencerHelperTestAccess::AddBoolKeyFrameToChannel(pSection, 7, FFrameNumber(0), true, 2));

	// Every interpolation value is accepted and ignored: a bool key is always constant.
	// Passing 0 (cubic) must not fall through to AddCubicKey, which bool channels lack.
	for (int iKeyInterp : {0, 1, 2, 99})
	{
		const FFrameNumber Frame(iKeyInterp * 10);
		TestTrue(FString::Printf(TEXT("Key added with interp %d"), iKeyInterp),
			FSequencerHelperTestAccess::AddBoolKeyFrameToChannel(pSection, 0, Frame, true, iKeyInterp));
	}

	const FMovieSceneBoolChannel* pChannel =
		pSection->GetChannelProxy().GetChannel<FMovieSceneBoolChannel>(0);
	TestEqual(TEXT("Four distinct keys"), pChannel->GetTimes().Num(), 4);
	CheckSorted(*this, pChannel, TEXT("after the initial keys"));

	// Re-keying the same frame replaces rather than duplicates, and flips the value.
	TestTrue(TEXT("Re-key at frame 0"),
		FSequencerHelperTestAccess::AddBoolKeyFrameToChannel(pSection, 0, FFrameNumber(0), false, 2));
	TestEqual(TEXT("Still four keys"), pChannel->GetTimes().Num(), 4);
	CheckSorted(*this, pChannel, TEXT("after re-keying frame 0"));

	bool Out = true;
	TestTrue(TEXT("Evaluation succeeds"), pChannel->Evaluate(FFrameNumber(0), Out));
	TestFalse(TEXT("Value was replaced"), Out);

	// Keys written out of order: AddKeys appends, so this is what actually distinguishes
	// a sorted insert from a bulk append.
	UMovieSceneBoolSection* pOutOfOrder = NewTransientSection<UMovieSceneBoolSection>();
	for (int32 Frame : {500, 100, 300})
		FSequencerHelperTestAccess::AddBoolKeyFrameToChannel(
			pOutOfOrder, 0, FFrameNumber(Frame), true, 2);
	CheckSorted(*this, pOutOfOrder->GetChannelProxy().GetChannel<FMovieSceneBoolChannel>(0),
		TEXT("keys written out of order"));

	return true;
}

//-----------------------------------------------------------------------------
// AddKeyFrameToSection<FVector> - keys all three channels, succeeds only if all do.
//-----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeqHelperAddVectorKeyFrameTest,
	SEQHELPER_TEST_PREFIX "AddKeyFrameToSection.Vector", SEQHELPER_TEST_FLAGS)

bool FSeqHelperAddVectorKeyFrameTest::RunTest(const FString& /*Parameters*/)
{
	using namespace SequencerHelperTests;

	UMovieSceneFloatVectorSection* pSection = NewTransientSection<UMovieSceneFloatVectorSection>();
	if (!TestNotNull(TEXT("Section created"), pSection))
		return false;
	// Mirrors AddSectionToTrack: the channel count must be set before the channels exist.
	pSection->SetChannelsUsed(3);
	if (!VerifyChannelCount(*this, pSection, 3))
		return false;

	TestFalse(TEXT("Null section fails"),
		FSequencerHelperTestAccess::AddVectorKeyFrameToSection(
			nullptr, FFrameNumber(0), FVector(1.0, 2.0, 3.0), 1));

	const FVector Value(1.5, -2.5, 3.25);
	TestTrue(TEXT("Vector key added"),
		FSequencerHelperTestAccess::AddVectorKeyFrameToSection(pSection, FFrameNumber(0), Value, 1));

	// Each component must land on its own channel, in X/Y/Z order.
	for (int32 i = 0; i < 3; ++i)
	{
		const FMovieSceneFloatChannel* pChannel =
			pSection->GetChannelProxy().GetChannel<FMovieSceneFloatChannel>(i);
		if (!TestNotNull(FString::Printf(TEXT("Channel %d exists"), i), pChannel))
			continue;
		TestEqual(FString::Printf(TEXT("Channel %d has one key"), i), pChannel->GetTimes().Num(), 1);

		float Out = 0.f;
		TestTrue(FString::Printf(TEXT("Channel %d evaluates"), i),
			pChannel->Evaluate(FFrameNumber(0), Out));
		TestEqual(FString::Printf(TEXT("Channel %d holds component %d"), i, i),
			Out, (float)Value[i], 1.e-4f);
	}

	// Re-keying the same frame replaces on every channel at once.
	TestTrue(TEXT("Vector re-keyed"),
		FSequencerHelperTestAccess::AddVectorKeyFrameToSection(
			pSection, FFrameNumber(0), FVector(9.0, 9.0, 9.0), 1));
	for (int32 i = 0; i < 3; ++i)
	{
		const FMovieSceneFloatChannel* pChannel =
			pSection->GetChannelProxy().GetChannel<FMovieSceneFloatChannel>(i);
		TestEqual(FString::Printf(TEXT("Channel %d still has one key"), i),
			pChannel->GetTimes().Num(), 1);
	}

	// A section with fewer channels than the vector needs must report failure rather
	// than silently keying only the components that happen to exist.
	{
		UMovieSceneFloatVectorSection* pNarrow = NewTransientSection<UMovieSceneFloatVectorSection>();
		pNarrow->SetChannelsUsed(2);
		TestFalse(TEXT("Two-channel section rejects a vector key"),
			FSequencerHelperTestAccess::AddVectorKeyFrameToSection(
				pNarrow, FFrameNumber(0), Value, 1));
	}

	return true;
}


namespace SequencerHelperTests
{
	/// An in-memory ULevelSequence addressable by path.
	///
	/// Every UITwinSequencerHelper entry point resolves its sequence with
	/// StaticLoadObject(..., nullptr, *levelSequencePath). That only avoids touching disk
	/// when the supplied name contains a '.', in which case StaticLoadObjectInternal
	/// short-circuits to StaticFindObject. GetPathName() of a transient object is
	/// "/Engine/Transient.<name>", which satisfies that - so always pass Path(), never a
	/// hand-built string.
	class [[nodiscard]] FScopedTestSequence
	{
	public:
		FScopedTestSequence()
		{
			// Unique name: transient objects outlive a single test until GC runs, and a
			// name clash would silently hand the next test the previous sequence.
			static int32 Counter = 0;
			const FName Name(*FString::Printf(TEXT("SeqHelperTestSeq_%d"), Counter++));

			Sequence.Reset(NewObject<ULevelSequence>(
				GetTransientPackage(), Name, RF_Transient));
			if (Sequence)
			{
				Sequence->Initialize(); // creates the UMovieScene
				// Pin the rates: GetFrameNum/GetTimeFrameNum convert through them, so
				// leaving them at project defaults would couple assertions to config.
				Sequence->GetMovieScene()->SetTickResolutionDirectly(FFrameRate(24000, 1));
				Sequence->GetMovieScene()->SetDisplayRate(FFrameRate(24, 1));
			}
		}

		bool IsValid() const { return Sequence.IsValid() && Sequence->GetMovieScene(); }
		ULevelSequence* Get() const { return Sequence.Get(); }
		UMovieScene* Scene() const { return Sequence->GetMovieScene(); }
		FString Path() const { return Sequence->GetPathName(); }

	private:
		TStrongObjectPtr<ULevelSequence> Sequence;
	};

	/// The public time-based API converts seconds through the movie scene's tick
	/// resolution, so tests must key at exactly the frames those conversions produce.
	/// The fixture pins the resolution, so this stays independent of project config.
	struct FTimeMap
	{
		double TickRes = 0.0;

		explicit FTimeMap(const FScopedTestSequence& Seq)
			: TickRes(Seq.Scene()->GetTickResolution().AsDecimal()) {}

		FFrameNumber operator()(float Time) const
		{
			return FFrameNumber((int32)(Time * TickRes));
		}
	};

	/// Shared setup: a world, a sequence, and an actor already possessed by it.
	/// Returns nullptr (after failing the test) if any step did not take, so that the
	/// assertions which follow are never vacuous.
	struct FFixture
	{
		ITwinTests::FScopedTestWorld World;
		FScopedTestSequence Sequence;
		TStrongObjectPtr<UITwinSequencerHelper> Helper;
		AActor* Actor = nullptr;

		bool Init(FAutomationTestBase& Test)
		{
			if (!Test.TestTrue(TEXT("Level sequence created"), Sequence.IsValid()))
				return false;

			Actor = World.SpawnActorOfClass<AActor>();
			if (!Test.TestNotNull(TEXT("Actor spawned"), Actor))
				return false;

			// The sequence must be reachable through StaticLoadObject, otherwise every
			// helper below bails out with "Level sequence not found" and the test proves
			// nothing. Check it explicitly rather than inferring it from a later failure.
			Helper.Reset(NewObject<UITwinSequencerHelper>(GetTransientPackage()));
			if (!Test.TestTrue(TEXT("Sequence resolves by path"),
				Helper->Initialize(Sequence.Path())))
				return false;

			bool bOk = false;
			FString Msg;
			Helper->GetPActorGuidFromLevelSequence(Actor, bOk, Msg);
			if (!Test.TestFalse(TEXT("Actor is not bound yet"), bOk))
				return false;
			if (!Test.TestTrue(
				FString::Printf(TEXT("Sequence resolves by path (got '%s')"), *Msg),
				!Msg.Contains(TEXT("Level sequence not found"))))
				return false;

			const FGuid Guid = Helper->AddPActorToLevelSequence(
				Actor, bOk, Msg);
			return Test.TestTrue(
				FString::Printf(TEXT("Actor possessed (%s)"), *Msg), bOk && Guid.IsValid());
		}
	};
}

//-----------------------------------------------------------------------------
// Possessable binding - the precondition every track/section helper depends on.
//-----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeqHelperPossessableLifecycleTest,
	SEQHELPER_TEST_PREFIX "PossessableLifecycle", SEQHELPER_TEST_FLAGS)

bool FSeqHelperPossessableLifecycleTest::RunTest(const FString& /*Parameters*/)
{
	using namespace SequencerHelperTests;

	FFixture Fix;
	if (!Fix.Init(*this))
		return false;

	UITwinSequencerHelper& Helper = *Fix.Helper;

	bool bOk = false;
	FString Msg;

	// Binding is idempotent: a second Add reports failure and returns the existing GUID.
	const FGuid First = Helper.GetPActorGuidFromLevelSequence(
		Fix.Actor, bOk, Msg);
	TestTrue(TEXT("Bound actor is found"), bOk && First.IsValid());

	const FGuid Second = Helper.AddPActorToLevelSequence(
		Fix.Actor, bOk, Msg);
	TestEqual(TEXT("Re-adding returns the same GUID"), Second, First);
	TestEqual(TEXT("Possessable count is still 1"), Fix.Sequence.Scene()->GetPossessableCount(), 1);

	// A null actor must fail cleanly rather than crash.
	Helper.GetPActorGuidFromLevelSequence(nullptr, bOk, Msg);
	TestFalse(TEXT("Null actor fails"), bOk);

	Helper.RemovePActorFromLevelSequence(Fix.Actor, bOk, Msg);
	TestTrue(FString::Printf(TEXT("Actor removed (%s)"), *Msg), bOk);
	TestEqual(TEXT("Possessable count back to 0"), Fix.Sequence.Scene()->GetPossessableCount(), 0);

	// Removing twice is a reported failure, not a crash.
	Helper.RemovePActorFromLevelSequence(Fix.Actor, bOk, Msg);
	TestFalse(TEXT("Second removal fails"), bOk);

	return true;
}

//-----------------------------------------------------------------------------
// Track lifecycle, including both bOverwriteExisting branches.
//-----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeqHelperTrackLifecycleTest,
	SEQHELPER_TEST_PREFIX "TrackLifecycle", SEQHELPER_TEST_FLAGS)

bool FSeqHelperTrackLifecycleTest::RunTest(const FString& /*Parameters*/)
{
	using namespace SequencerHelperTests;

	FFixture Fix;
	if (!Fix.Init(*this))
		return false;

	UITwinSequencerHelper& Helper = *Fix.Helper;

	bool bOk = false;
	FString Msg;

	// Nothing added yet.
	TestNull(TEXT("No transform track initially"),
		FSequencerHelperTestAccess::GetTransformTrack(Helper, Fix.Actor, bOk, Msg));
	TestFalse(TEXT("...and it reports failure"), bOk);

	UMovieSceneTrack* pTrack = FSequencerHelperTestAccess::AddTransformTrack(
		Helper, Fix.Actor, /*bOverwriteExisting*/ false, bOk, Msg);
	if (!TestNotNull(FString::Printf(TEXT("Transform track added (%s)"), *Msg), pTrack))
		return false;
	TestTrue(TEXT("Add reports success"), bOk);

	TestEqual(TEXT("Get returns the same track"),
		FSequencerHelperTestAccess::GetTransformTrack(Helper, Fix.Actor, bOk, Msg), pTrack);

	// bOverwriteExisting == false: the existing track is returned untouched.
	UMovieSceneTrack* pSame = FSequencerHelperTestAccess::AddTransformTrack(
		Helper, Fix.Actor, false, bOk, Msg);
	TestEqual(TEXT("Non-overwriting add returns the existing track"), pSame, pTrack);
	TestFalse(TEXT("Non-overwriting add reports failure"), bOk);
	TestTrue(TEXT("...while returning an error message"), Msg.Contains(TEXT("already exists")));

	// bOverwriteExisting == true: the old track is destroyed and replaced.
	UMovieSceneTrack* pFresh = FSequencerHelperTestAccess::AddTransformTrack(
		Helper, Fix.Actor, true, bOk, Msg);
	TestTrue(TEXT("Overwriting add succeeds"), bOk);
	if (TestNotNull(TEXT("Overwriting add returns a track"), pFresh))
		TestNotEqual(TEXT("Overwriting add replaces the track"), pFresh, pTrack);

	// Tracks of different types coexist on the same binding.
	UMovieSceneTrack* pDouble = FSequencerHelperTestAccess::AddDoubleTrack(
		Helper, Fix.Actor, false, bOk, Msg);
	TestNotNull(TEXT("Double track added alongside"), pDouble);
	TestEqual(TEXT("Transform track is unaffected"),
		FSequencerHelperTestAccess::GetTransformTrack(Helper, Fix.Actor, bOk, Msg), pFresh);

	FSequencerHelperTestAccess::RemoveTransformTrack(Helper, Fix.Actor, bOk, Msg);
	TestTrue(FString::Printf(TEXT("Transform track removed (%s)"), *Msg), bOk);
	TestNull(TEXT("Transform track is gone"),
		FSequencerHelperTestAccess::GetTransformTrack(Helper, Fix.Actor, bOk, Msg));

	// Removing a track that is not there is a reported failure.
	FSequencerHelperTestAccess::RemoveTransformTrack(Helper, Fix.Actor, bOk, Msg);
	TestFalse(TEXT("Second removal fails"), bOk);

	// An actor that was never bound cannot have tracks.
	AActor* pUnbound = Fix.World.SpawnActorOfClass<AActor>();
	TestNull(TEXT("Unbound actor has no track"),
		FSequencerHelperTestAccess::GetTransformTrack(Helper, pUnbound, bOk, Msg));
	TestNull(TEXT("Unbound actor cannot receive a track"),
		FSequencerHelperTestAccess::AddTransformTrack(Helper, pUnbound, false, bOk, Msg));
	TestFalse(TEXT("...and it reports failure"), bOk);

	return true;
}

//-----------------------------------------------------------------------------
// Section lifecycle - indices, row assignment and bounds.
//-----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeqHelperSectionLifecycleTest,
	SEQHELPER_TEST_PREFIX "SectionLifecycle", SEQHELPER_TEST_FLAGS)

bool FSeqHelperSectionLifecycleTest::RunTest(const FString& /*Parameters*/)
{
	using namespace SequencerHelperTests;

	FFixture Fix;
	if (!Fix.Init(*this))
		return false;

	UITwinSequencerHelper& Helper = *Fix.Helper;

	bool bOk = false;
	FString Msg;

	// A section cannot exist without its track.
	TestNull(TEXT("No section without a track"),
		FSequencerHelperTestAccess::GetTransformSection(Helper, Fix.Actor, 0, bOk, Msg));
	TestNull(TEXT("Cannot add a section without a track"),
		FSequencerHelperTestAccess::AddTransformSection(
			Helper, Fix.Actor, FFrameNumber(1000), EMovieSceneBlendType::Absolute, bOk, Msg));
	TestFalse(TEXT("...and it reports failure"), bOk);

	UMovieSceneTrack* pTrack = FSequencerHelperTestAccess::AddTransformTrack(
		Helper, Fix.Actor, false, bOk, Msg);
	if (!TestNotNull(TEXT("Transform track added"), pTrack))
		return false;

	TestNull(TEXT("Fresh track has no section at index 0"),
		FSequencerHelperTestAccess::GetTransformSection(Helper, Fix.Actor, 0, bOk, Msg));

	UMovieSceneSection* pSection0 = FSequencerHelperTestAccess::AddTransformSection(
		Helper, Fix.Actor, FFrameNumber(1000), EMovieSceneBlendType::Absolute, bOk, Msg);
	if (!TestNotNull(FString::Printf(TEXT("Section added (%s)"), *Msg), pSection0))
		return false;
	TestTrue(TEXT("Add reports success"), bOk);

	// AddSectionToTrack always starts the range at frame 0, regardless of FrameNum,
	// which only sets the upper bound.
	TestEqual(TEXT("Section starts at frame 0"),
		pSection0->GetRange().GetLowerBoundValue().Value, 0);
	TestEqual(TEXT("Section ends at the requested frame"),
		pSection0->GetRange().GetUpperBoundValue().Value, 1000);
	TestEqual(TEXT("First section is on row 0"), pSection0->GetRowIndex(), 0);

	TestEqual(TEXT("Get returns the section at index 0"),
		FSequencerHelperTestAccess::GetTransformSection(Helper, Fix.Actor, 0, bOk, Msg), pSection0);

	// A second section lands on the next row and the next index.
	UMovieSceneSection* pSection1 = FSequencerHelperTestAccess::AddTransformSection(
		Helper, Fix.Actor, FFrameNumber(2000), EMovieSceneBlendType::Additive, bOk, Msg);
	if (TestNotNull(TEXT("Second section added"), pSection1))
	{
		TestEqual(TEXT("Second section is on row 1"), pSection1->GetRowIndex(), 1);
		TestEqual(TEXT("Second section is at index 1"),
			FSequencerHelperTestAccess::GetTransformSection(Helper, Fix.Actor, 1, bOk, Msg),
			pSection1);
	}
	TestEqual(TEXT("Track holds two sections"), pTrack->GetAllSections().Num(), 2);

	// Out-of-range indices fail cleanly in both directions.
	TestNull(TEXT("Index past the end fails"),
		FSequencerHelperTestAccess::GetTransformSection(Helper, Fix.Actor, 2, bOk, Msg));
	TestFalse(TEXT("...reporting failure"), bOk);
	TestNull(TEXT("Negative index fails"),
		FSequencerHelperTestAccess::GetTransformSection(Helper, Fix.Actor, -1, bOk, Msg));
	TestFalse(TEXT("...reporting failure"), bOk);

	FSequencerHelperTestAccess::RemoveTransformSection(Helper, Fix.Actor, 0, bOk, Msg);
	TestTrue(FString::Printf(TEXT("Section 0 removed (%s)"), *Msg), bOk);
	TestEqual(TEXT("Track holds one section"), pTrack->GetAllSections().Num(), 1);
	// Indices are positional, so what used to be section 1 is now section 0.
	TestEqual(TEXT("Remaining section shifted to index 0"),
		FSequencerHelperTestAccess::GetTransformSection(Helper, Fix.Actor, 0, bOk, Msg), pSection1);

	FSequencerHelperTestAccess::RemoveTransformSection(Helper, Fix.Actor, 5, bOk, Msg);
	TestFalse(TEXT("Removing an out-of-range section fails"), bOk);

	return true;
}

//-----------------------------------------------------------------------------
// AddKeyFrameToActor / AddKeyFrameToTrack - routing and section range maintenance.
//-----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeqHelperAddKeyFrameToActorTest,
	SEQHELPER_TEST_PREFIX "AddKeyFrameToActor", SEQHELPER_TEST_FLAGS)

bool FSeqHelperAddKeyFrameToActorTest::RunTest(const FString& /*Parameters*/)
{
	using namespace SequencerHelperTests;

	FFixture Fix;
	if (!Fix.Init(*this))
		return false;

	UITwinSequencerHelper& Helper = *Fix.Helper;

	bool bOk = false;
	FString Msg;

	const FTransform Value(FRotator(0.0, 90.0, 0.0), FVector(10.0, 20.0, 30.0));

	// Without a track, the call fails before touching anything.
	FSequencerHelperTestAccess::AddTransformKeyFrameToActor(Helper,
		Fix.Actor, 0, FFrameNumber(500), Value, 1, bOk, Msg);
	TestFalse(TEXT("No track -> failure"), bOk);
	TestTrue(TEXT("...with an explanatory message"), Msg.Contains(TEXT("Track")));

	FSequencerHelperTestAccess::AddTransformTrack(Helper, Fix.Actor, false, bOk, Msg);

	// With a track but no section, it still fails - AddKeyFrameToTrack does not create
	// sections on demand.
	FSequencerHelperTestAccess::AddTransformKeyFrameToActor(Helper,
		Fix.Actor, 0, FFrameNumber(500), Value, 1, bOk, Msg);
	TestFalse(TEXT("No section -> failure"), bOk);
	TestTrue(TEXT("...with an explanatory message"), Msg.Contains(TEXT("Section")));

	UMovieSceneSection* pSection = FSequencerHelperTestAccess::AddTransformSection(
		Helper, Fix.Actor, FFrameNumber(1000), EMovieSceneBlendType::Absolute, bOk, Msg);
	if (!TestNotNull(TEXT("Section added"), pSection))
		return false;

	// A key inside the section range leaves that range alone.
	FFrameNumberRange Range = FSequencerHelperTestAccess::AddTransformKeyFrameToActor(
		Helper, Fix.Actor, 0, FFrameNumber(500), Value, 1, bOk, Msg);
	TestTrue(FString::Printf(TEXT("Key added (%s)"), *Msg), bOk);
	TestEqual(TEXT("Range lower bound unchanged"), Range.GetLowerBoundValue().Value, 0);
	TestEqual(TEXT("Range upper bound unchanged"), Range.GetUpperBoundValue().Value, 1000);

	// A transform key writes all nine channels (location, rotation, scale).
	for (int32 i = 0; i < 9; ++i)
	{
		const FMovieSceneDoubleChannel* pChannel =
			pSection->GetChannelProxy().GetChannel<FMovieSceneDoubleChannel>(i);
		if (TestNotNull(FString::Printf(TEXT("Channel %d exists"), i), pChannel))
			TestEqual(FString::Printf(TEXT("Channel %d has one key"), i),
				pChannel->GetTimes().Num(), 1);
	}

	// Re-keying the same frame replaces rather than duplicating.
	FSequencerHelperTestAccess::AddTransformKeyFrameToActor(
		Helper, Fix.Actor, 0, FFrameNumber(500), FTransform(FVector(1.0, 2.0, 3.0)), 1, bOk, Msg);
	{
		const FMovieSceneDoubleChannel* pChannel =
			pSection->GetChannelProxy().GetChannel<FMovieSceneDoubleChannel>(0);
		TestEqual(TEXT("Still one key after re-keying"), pChannel->GetTimes().Num(), 1);
		double Out = 0.0;
		pChannel->Evaluate(FFrameNumber(500), Out);
		TestEqual(TEXT("Value was replaced"), Out, 1.0, 1.e-4);
	}

	// A key past the end extends the section. Note that the current implementation
	// rebuilds the range as [0, iFrameNum] rather than widening the upper bound, so a
	// non-zero lower bound would be discarded here. Pinned deliberately.
	Range = FSequencerHelperTestAccess::AddTransformKeyFrameToActor(
		Helper, Fix.Actor, 0, FFrameNumber(5000), Value, 1, bOk, Msg);
	TestTrue(TEXT("Key past the end succeeds"), bOk);
	TestEqual(TEXT("Range extended to the new key"), Range.GetUpperBoundValue().Value, 5000);
	TestEqual(TEXT("Range lower bound reset to 0"), Range.GetLowerBoundValue().Value, 0);

	// Removing the outer key shrinks the range back to the remaining keys.
	Range = FSequencerHelperTestAccess::RemoveTransformKeyFrameFromActor(
		Helper, Fix.Actor, 0, FFrameNumber(5000), bOk, Msg);
	TestTrue(FString::Printf(TEXT("Key removed (%s)"), *Msg), bOk);
	TestEqual(TEXT("Range shrunk to the surviving key"),
		Range.GetUpperBoundValue().Value, 500);

	// AddKeyFrameToTrack on the wrong track type must fail rather than mis-key: a double
	// value cannot be written to a transform section.
	UMovieSceneTrack* pDoubleTrack =
		FSequencerHelperTestAccess::AddDoubleTrack(Helper, Fix.Actor, false, bOk, Msg);
	if (TestNotNull(TEXT("Double track added"), pDoubleTrack))
	{
		FSequencerHelperTestAccess::AddDoubleKeyFrameToTrack(
			Helper, pDoubleTrack, 0, FFrameNumber(100), 42.0, 1, bOk, Msg);
		TestFalse(TEXT("Double track with no section rejects the key"), bOk);

		FSequencerHelperTestAccess::AddDoubleKeyFrameToTrack(
			Helper, nullptr, 0, FFrameNumber(100), 42.0, 1, bOk, Msg);
		TestFalse(TEXT("Null track rejects the key"), bOk);
	}

	return true;
}


//-----------------------------------------------------------------------------
// RemoveKeyFrame - the live dispatch used by AITwinTimelineActor.
//
// Regression: RemoveKeyFrameFromSection used to bound its loop with the proxy's
// *global* channel count while indexing per channel type. Every section carries a
// ManualWeight float channel, so for every type whose channels are not float the
// last lookup returned null, the function reported failure, and RemoveKeyFrameFromTrack
// returned early - skipping SetRange(ComputeRangeFromKFs()) and leaving a stale range.
//-----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeqHelperRemoveKeyFrameTest,
	SEQHELPER_TEST_PREFIX "RemoveKeyFrame", SEQHELPER_TEST_FLAGS)

bool FSeqHelperRemoveKeyFrameTest::RunTest(const FString& /*Parameters*/)
{
	using namespace SequencerHelperTests;

	FFixture Fix;
	if (!Fix.Init(*this))
		return false;

	UITwinSequencerHelper& Helper = *Fix.Helper;

	bool bOk = false;
	FString Msg;

	// GetTimeFrameNum multiplies by the tick resolution the fixture pinned, so the
	// frame numbers below are the ones the live API will compute from these times.
	const double TickRes = Fix.Sequence.Scene()->GetTickResolution().AsDecimal();
	const auto FrameAt = [TickRes](float Time) { return FFrameNumber((int)(Time * TickRes)); };
	const FFrameNumber Keep = FrameAt(0.5f);
	const FFrameNumber Drop = FrameAt(1.0f);

	// Two tracks of different channel types: FTransform (double channels) is the case
	// that broke, double is the second live type and would have caught it independently.
	UMovieSceneTrack* pTransTrack = FSequencerHelperTestAccess::AddTransformTrack(
		Helper, Fix.Actor, false, bOk, Msg);
	UMovieSceneTrack* pDoubleTrack = FSequencerHelperTestAccess::AddDoubleTrack(
		Helper, Fix.Actor, false, bOk, Msg);
	if (!TestNotNull(TEXT("Transform track added"), pTransTrack) ||
		!TestNotNull(TEXT("Double track added"), pDoubleTrack))
		return false;

	UMovieSceneSection* pTransSection = FSequencerHelperTestAccess::AddTransformSection(
		Helper, Fix.Actor, Drop, EMovieSceneBlendType::Absolute, bOk, Msg);
	UMovieSceneSection* pDoubleSection = FSequencerHelperTestAccess::AddDoubleSection(
		Helper, Fix.Actor, Drop, EMovieSceneBlendType::Absolute, bOk, Msg);
	if (!TestNotNull(TEXT("Transform section added"), pTransSection) ||
		!TestNotNull(TEXT("Double section added"), pDoubleSection))
		return false;

	// Pin the asymmetry that caused the bug, so a revert to NumChannels() fails here
	// rather than via a confusing range assertion further down.

	// The transform section is the case that broke: its 9 double channels sit alongside a
	// ManualWeight *float* channel, so the proxy's global count exceeds its double channel
	// count. Removal must be driven by the latter.
	TestEqual(TEXT("Transform section has 9 double channels"),
		pTransSection->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>().Num(), 9);
	TestTrue(TEXT("...and more channels in total, of mixed type"),
		pTransSection->GetChannelProxy().NumChannels() > 9);

	// The double section has a single channel and no weight channel, so its global and
	// typed counts coincide - which is precisely why double-valued tracks kept working
	// while transforms did not. Pinned so that an engine change adding a weight channel
	// here shows up as a deliberate decision rather than a silent regression.
	TestEqual(TEXT("Double section has 1 double channel"),
		pDoubleSection->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>().Num(), 1);
	TestEqual(TEXT("...and no channels of any other type"),
		pDoubleSection->GetChannelProxy().NumChannels(), 1);

	const FTransform Value(FRotator(0.0, 90.0, 0.0), FVector(10.0, 20.0, 30.0));
	for (FFrameNumber Frame : {Keep, Drop})
	{
		FSequencerHelperTestAccess::AddTransformKeyFrameToActor(
			Helper, Fix.Actor, 0, Frame, Value, 1, bOk, Msg);
		TestTrue(TEXT("Transform key added"), bOk);
		FSequencerHelperTestAccess::AddDoubleKeyFrameToTrack(
			Helper, pDoubleTrack, 0, Frame, 42.0, 1, bOk, Msg);
		TestTrue(TEXT("Double key added"), bOk);
	}

	const auto KeyCount = [](UMovieSceneSection* pSection)
	{
		auto Channels = pSection->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
		return Channels.Num() > 0 ? Channels[0]->GetTimes().Num() : -1;
	};
	TestEqual(TEXT("Transform channel 0 has two keys"), KeyCount(pTransSection), 2);
	TestEqual(TEXT("Double channel 0 has two keys"), KeyCount(pDoubleSection), 2);

	// The live call: AITwinTimelineActor::Impl::RemoveKeyFrame goes through exactly this.
	TArray<TStrongObjectPtr<UMovieSceneTrack> > Tracks;
	Tracks.Add(TStrongObjectPtr<UMovieSceneTrack>(pTransTrack));
	Tracks.Add(TStrongObjectPtr<UMovieSceneTrack>(pDoubleTrack));
	Helper.RemoveKeyFrame(Tracks, 1.0f);

	TestEqual(TEXT("Transform channel 0 lost a key"), KeyCount(pTransSection), 1);
	TestEqual(TEXT("Double channel 0 lost a key"), KeyCount(pDoubleSection), 1);

	// The part the bug actually broke: with the early return, SetRange never ran and
	// both sections kept spanning to the removed key.
	TestEqual(TEXT("Transform section range shrunk"),
		pTransSection->GetRange().GetUpperBoundValue().Value, Keep.Value);
	TestEqual(TEXT("Double section range shrunk"),
		pDoubleSection->GetRange().GetUpperBoundValue().Value, Keep.Value);

	// And the direct per-type entry point reports success for a non-float channel type.
	FSequencerHelperTestAccess::RemoveDoubleKeyFrameFromActor(
		Helper, Fix.Actor, 0, Keep, bOk, Msg);
	TestTrue(FString::Printf(TEXT("Double key removed (%s)"), *Msg), bOk);
	TestEqual(TEXT("Double channel 0 is empty"), KeyCount(pDoubleSection), 0);

	return true;
}

//-----------------------------------------------------------------------------
// ComputeRangeFromKFs - must hull every channel's key span, not just the first
// bound that happens to match.
//-----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeqHelperComputeRangeFromKFsTest,
	SEQHELPER_TEST_PREFIX "ComputeRangeFromKFs", SEQHELPER_TEST_FLAGS)

bool FSeqHelperComputeRangeFromKFsTest::RunTest(const FString& /*Parameters*/)
{
	using namespace SequencerHelperTests;

	FFixture Fix;
	if (!Fix.Init(*this))
		return false;

	UITwinSequencerHelper& Helper = *Fix.Helper;

	bool bOk = false;
	FString Msg;

	FSequencerHelperTestAccess::AddTransformTrack(Helper, Fix.Actor, false, bOk, Msg);
	UMovieSceneSection* pSection = FSequencerHelperTestAccess::AddTransformSection(
		Helper, Fix.Actor, FFrameNumber(1000), EMovieSceneBlendType::Absolute, bOk, Msg);
	if (!TestNotNull(TEXT("Transform section added"), pSection))
		return false;

	// All nine channels keyed identically at 100 and 200.
	const FTransform Value(FVector(1.0, 2.0, 3.0));
	for (int32 Frame : {100, 200})
		FSequencerHelperTestAccess::AddTransformKeyFrameToActor(
			Helper, Fix.Actor, 0, FFrameNumber(Frame), Value, 1, bOk, Msg);

	// Make the *last* double channel span wider at both ends than the running range
	// accumulated from the earlier channels. Ordering is the point: with the old
	// else-if chain, matching the lower bound skipped the upper-bound branch entirely.
	auto Channels = pSection->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
	if (!TestEqual(TEXT("Transform section has 9 double channels"), Channels.Num(), 9))
		return false;
	Channels[8]->AddLinearKey(FFrameNumber(50), 0.0);
	Channels[8]->AddLinearKey(FFrameNumber(500), 0.0);

	// Any removal re-runs ComputeRangeFromKFs and stamps the result onto the section.
	FSequencerHelperTestAccess::RemoveTransformKeyFrameFromActor(
		Helper, Fix.Actor, 0, FFrameNumber(200), bOk, Msg);
	TestTrue(FString::Printf(TEXT("Key removed (%s)"), *Msg), bOk);

	// The section must cover every surviving key on every channel.
	const TRange<FFrameNumber> Range = pSection->GetRange();
	TestEqual(TEXT("Range starts at the earliest key on any channel"),
		Range.GetLowerBoundValue().Value, 50);
	TestEqual(TEXT("Range ends at the latest key on any channel"),
		Range.GetUpperBoundValue().Value, 500);

	return true;
}

//-----------------------------------------------------------------------------
// GetFloat/Double/Bool/StringValueAtTime - the public read path.
//
// All four funnel through GetTrackValueAtTime, which reads *channel 0 of section 0
// only*. The per-type differences are in how the channel evaluates between keys:
// numeric channels interpolate, bool and string channels hold the previous value.
//-----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeqHelperValueAtTimeTest,
	SEQHELPER_TEST_PREFIX "ValueAtTime", SEQHELPER_TEST_FLAGS)

bool FSeqHelperValueAtTimeTest::RunTest(const FString& /*Parameters*/)
{
	using namespace SequencerHelperTests;

	FFixture Fix;
	if (!Fix.Init(*this))
		return false;

	UITwinSequencerHelper& Helper = *Fix.Helper;

	bool bOk = false;
	FString Msg;
	
	const FTimeMap FrameAt(Fix.Sequence);

	// --- double: keys at t=0 -> 10.0 and t=2 -> 20.0.
	UMovieSceneTrack* pDoubleTrack =
		FSequencerHelperTestAccess::AddDoubleTrack(Helper, Fix.Actor, false, bOk, Msg);
	if (!TestNotNull(TEXT("Double track added"), pDoubleTrack))
		return false;

	// A track with no section must fail rather than return a default-constructed value:
	// the caller cannot otherwise distinguish "no data" from "the value is 0".
	double OutDouble = -1.0;
	TestFalse(TEXT("Double track with no section reports no value"),
		Helper.GetDoubleValueAtTime(pDoubleTrack, 0.f, OutDouble));

	FSequencerHelperTestAccess::AddDoubleSection(
		Helper, Fix.Actor, FrameAt(2.f), EMovieSceneBlendType::Absolute, bOk, Msg);
	FSequencerHelperTestAccess::AddDoubleKeyFrameToTrack(
		Helper, pDoubleTrack, 0, FrameAt(0.f), 10.0, 1, bOk, Msg);
	FSequencerHelperTestAccess::AddDoubleKeyFrameToTrack(
		Helper, pDoubleTrack, 0, FrameAt(2.f), 20.0, 1, bOk, Msg);

	TestTrue(TEXT("Double value read at the first key"),
		Helper.GetDoubleValueAtTime(pDoubleTrack, 0.f, OutDouble));
	TestEqual(TEXT("...equals the keyed value"), OutDouble, 10.0, 1.e-4);

	TestTrue(TEXT("Double value read at the last key"),
		Helper.GetDoubleValueAtTime(pDoubleTrack, 2.f, OutDouble));
	TestEqual(TEXT("...equals the keyed value"), OutDouble, 20.0, 1.e-4);

	// Between keys the channel interpolates. The interpolation mode is deliberately not
	// asserted here - only that the result is bracketed by the surrounding keys, which
	// holds for every mode and so will not break if the default interpolation changes.
	TestTrue(TEXT("Double value read between keys"),
		Helper.GetDoubleValueAtTime(pDoubleTrack, 1.f, OutDouble));
	TestTrue(FString::Printf(TEXT("...is bracketed by the keys (got %f)"), OutDouble),
		OutDouble >= 10.0 - 1.e-4 && OutDouble <= 20.0 + 1.e-4);

	// Before the first key the channel holds, rather than failing or extrapolating.
	TestTrue(TEXT("Double value read before the first key"),
		Helper.GetDoubleValueAtTime(pDoubleTrack, -5.f, OutDouble));
	TestEqual(TEXT("...holds the first key"), OutDouble, 10.0, 1.e-4);

	TestFalse(TEXT("Null track reports no value"),
		Helper.GetDoubleValueAtTime(nullptr, 0.f, OutDouble));

	// --- float.
	UMovieSceneTrack* pFloatTrack =
		FSequencerHelperTestAccess::AddFloatTrack(Helper, Fix.Actor, false, bOk, Msg);
	if (TestNotNull(TEXT("Float track added"), pFloatTrack))
	{
		FSequencerHelperTestAccess::AddFloatSection(
			Helper, Fix.Actor, FrameAt(1.f), EMovieSceneBlendType::Absolute, bOk, Msg);
		FSequencerHelperTestAccess::AddFloatKeyFrameToTrack(
			Helper, pFloatTrack, 0, FrameAt(1.f), 2.5f, 1, bOk, Msg);
		TestTrue(TEXT("Float key added"), bOk);

		float OutFloat = 0.f;
		TestTrue(TEXT("Float value read at the key"),
			Helper.GetFloatValueAtTime(pFloatTrack, 1.f, OutFloat));
		TestEqual(TEXT("...equals the keyed value"), OutFloat, 2.5f, 1.e-4f);
	}

	// --- bool: a step channel. The value must hold until the next key, so reading
	// just before the second key still yields the first key's value.
	UMovieSceneTrack* pBoolTrack =
		FSequencerHelperTestAccess::AddBoolTrack(Helper, Fix.Actor, false, bOk, Msg);
	if (TestNotNull(TEXT("Bool track added"), pBoolTrack))
	{
		FSequencerHelperTestAccess::AddBoolSection(
			Helper, Fix.Actor, FrameAt(2.f), EMovieSceneBlendType::Absolute, bOk, Msg);
		FSequencerHelperTestAccess::AddBoolKeyFrameToTrack(
			Helper, pBoolTrack, 0, FrameAt(0.f), true, 1, bOk, Msg);
		FSequencerHelperTestAccess::AddBoolKeyFrameToTrack(
			Helper, pBoolTrack, 0, FrameAt(2.f), false, 1, bOk, Msg);

		bool OutBool = false;
		TestTrue(TEXT("Bool value read at the first key"),
			Helper.GetBoolValueAtTime(pBoolTrack, 0.f, OutBool));
		TestTrue(TEXT("...is the keyed true"), OutBool);

		TestTrue(TEXT("Bool value read between keys"),
			Helper.GetBoolValueAtTime(pBoolTrack, 1.9f, OutBool));
		TestTrue(TEXT("...still holds the first key"), OutBool);

		TestTrue(TEXT("Bool value read at the second key"),
			Helper.GetBoolValueAtTime(pBoolTrack, 2.f, OutBool));
		TestFalse(TEXT("...is the keyed false"), OutBool);
	}

	// --- string, including the UTF-8 round trip. This is the regression guard for the
	// FString(Value.c_str()) bug: the bytes below are not representable in ANSI, so a
	// reverted conversion corrupts them here rather than silently in the field.
	UMovieSceneTrack* pStringTrack =
		FSequencerHelperTestAccess::AddStringTrack(Helper, Fix.Actor, false, bOk, Msg);
	if (TestNotNull(TEXT("String track added"), pStringTrack))
	{
		FSequencerHelperTestAccess::AddStringSection(
			Helper, Fix.Actor, FrameAt(1.f), EMovieSceneBlendType::Absolute, bOk, Msg);

		// "café — été" spelled out as explicit UTF-8 bytes, so the expectation does not
		// depend on this file's own encoding.
		const std::string Utf8 =
			"caf\xC3\xA9 \xE2\x80\x94 \xC3\xA9t\xC3\xA9";
		FSequencerHelperTestAccess::AddStringKeyFrameToTrack(
			Helper, pStringTrack, 0, FrameAt(1.f), Utf8, 1, bOk, Msg);
		TestTrue(TEXT("String key added"), bOk);

		std::string OutString;
		TestTrue(TEXT("String value read at the key"),
			Helper.GetStringValueAtTime(pStringTrack, 1.f, OutString));
		TestTrue(TEXT("...round-trips the UTF-8 bytes unchanged"), OutString == Utf8);
	}

	return true;
}

//-----------------------------------------------------------------------------
// HasTransformKeyFrame and GetDuration / GetStartTime / GetEndTime.
//
// All four read the *first transform section*, and the three time getters derive
// from pSection->GetRange() - the section range, not the span of its keys. Those
// coincide after a removal (which re-stamps the range) but not after an add, so the
// distinction is pinned explicitly below.
//-----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeqHelperTransformTimesTest,
	SEQHELPER_TEST_PREFIX "TransformTimes", SEQHELPER_TEST_FLAGS)

bool FSeqHelperTransformTimesTest::RunTest(const FString& /*Parameters*/)
{
	using namespace SequencerHelperTests;

	FFixture Fix;
	if (!Fix.Init(*this))
		return false;

	UITwinSequencerHelper& Helper = *Fix.Helper;

	bool bOk = false;
	FString Msg;
	const FTimeMap FrameAt(Fix.Sequence);
	const FTransform Value(FRotator(0.0, 90.0, 0.0), FVector(10.0, 20.0, 30.0));

	// With no transform track at all, the getters return 0 instead of asserting.
	TestEqual(TEXT("No track -> zero duration"),
		Helper.GetDuration(Fix.Actor), 0.f);
	TestEqual(TEXT("No track -> zero start"),
		Helper.GetStartTime(Fix.Actor), 0.f);
	TestEqual(TEXT("No track -> zero end"),
		Helper.GetEndTime(Fix.Actor), 0.f);

	TestFalse(TEXT("No track -> HasTransformKeyFrame fails"),
		Helper.HasTransformKeyFrame(Fix.Actor, 0, FrameAt(1.f), bOk, Msg));
	TestFalse(TEXT("...reporting failure"), bOk);

	FSequencerHelperTestAccess::AddTransformTrack(Helper, Fix.Actor, false, bOk, Msg);

	// A track with no section must report failure. This is the case that needs the
	// explicit bOutSuccess = false in the implementation, because the preceding
	// GetTrackFromActorInLevelSequence has already set it to true.
	TestFalse(TEXT("Track but no section -> HasTransformKeyFrame fails"),
		Helper.HasTransformKeyFrame(Fix.Actor, 0, FrameAt(1.f), bOk, Msg));
	TestFalse(TEXT("...reporting failure, not the track lookup's success"), bOk);

	UMovieSceneSection* pSection = FSequencerHelperTestAccess::AddTransformSection(
		Helper, Fix.Actor, FrameAt(2.f), EMovieSceneBlendType::Absolute, bOk, Msg);
	if (!TestNotNull(TEXT("Transform section added"), pSection))
		return false;

	FSequencerHelperTestAccess::AddTransformKeyFrameToActor(
		Helper, Fix.Actor, 0, FrameAt(0.5f), Value, 1, bOk, Msg);
	FSequencerHelperTestAccess::AddTransformKeyFrameToActor(
		Helper, Fix.Actor, 0, FrameAt(1.5f), Value, 1, bOk, Msg);

	TestTrue(TEXT("Key is found at the first keyed frame"),
		Helper.HasTransformKeyFrame(Fix.Actor, 0, FrameAt(0.5f), bOk, Msg));
	TestTrue(TEXT("Key is found at the second keyed frame"),
		Helper.HasTransformKeyFrame(Fix.Actor, 0, FrameAt(1.5f), bOk, Msg));
	TestFalse(TEXT("No key at an unkeyed frame"),
		Helper.HasTransformKeyFrame(Fix.Actor, 0, FrameAt(1.0f), bOk, Msg));
	// Note: the "no key here" answer is itself a successful query.
	TestTrue(TEXT("...which is still a successful lookup"), bOk);

	// An out-of-range section index is a failure, not a false answer.
	Helper.HasTransformKeyFrame(Fix.Actor, 7, FrameAt(0.5f), bOk, Msg);
	TestFalse(TEXT("Bad section index reports failure"), bOk);

	// The section range still spans [0, 2] as created: adding keys inside it does not
	// shrink it to the key span. The time getters therefore report the *section*.
	TestEqual(TEXT("Start time is the section's lower bound"),
		Helper.GetStartTime(Fix.Actor), 0.f, 1.e-3f);
	TestEqual(TEXT("End time is the section's upper bound"),
		Helper.GetEndTime(Fix.Actor), 2.f, 1.e-3f);
	TestEqual(TEXT("Duration is end minus start"),
		Helper.GetDuration(Fix.Actor), 2.f, 1.e-3f);

	// Removing a key re-stamps the range from the surviving keys, so the getters now
	// follow the keys. This is the transition that makes "section range" vs "key span"
	// observable from the public API.
	FSequencerHelperTestAccess::RemoveTransformKeyFrameFromActor(
		Helper, Fix.Actor, 0, FrameAt(1.5f), bOk, Msg);
	TestTrue(FString::Printf(TEXT("Key removed (%s)"), *Msg), bOk);

	TestEqual(TEXT("End time now follows the surviving key"),
		Helper.GetEndTime(Fix.Actor), 0.5f, 1.e-3f);
	TestEqual(TEXT("Duration collapses with a single remaining key"),
		Helper.GetDuration(Fix.Actor), 0.f, 1.e-3f);

	// Removing the last key leaves a section whose range may be empty or unbounded.
	// The getters must still answer rather than trip GetXxxBoundValue's check().
	FSequencerHelperTestAccess::RemoveTransformKeyFrameFromActor(
		Helper,	Fix.Actor, 0, FrameAt(0.5f), bOk, Msg);
	const float EmptyDuration = Helper.GetDuration(Fix.Actor);
	TestTrue(FString::Printf(TEXT("Duration with no keys is non-negative (got %f)"),
		EmptyDuration), EmptyDuration >= 0.f);

	return true;
}

//-----------------------------------------------------------------------------
// AdjustMoviePlaybackRange / GetPlaybackEndTime.
//
// AdjustMoviePlaybackRange copies the camera's first transform section range onto
// the movie scene. It takes an ACameraActor, so this test possesses a camera rather
// than reusing the fixture's plain actor.
//-----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeqHelperPlaybackRangeTest,
	SEQHELPER_TEST_PREFIX "PlaybackRange", SEQHELPER_TEST_FLAGS)

bool FSeqHelperPlaybackRangeTest::RunTest(const FString& /*Parameters*/)
{
	using namespace SequencerHelperTests;

	FFixture Fix;
	if (!Fix.Init(*this))
		return false;

	UITwinSequencerHelper& Helper = *Fix.Helper;

	bool bOk = false;
	FString Msg;

	const FTimeMap FrameAt(Fix.Sequence);

	ACameraActor* pCamera = Fix.World.SpawnActorOfClass<ACameraActor>();
	if (!TestNotNull(TEXT("Camera spawned"), pCamera))
		return false;

	// GetPlaybackEndTime reads the movie scene directly, so it is observable before
	// any track exists - this is the baseline the adjust call must change.
	Fix.Sequence.Scene()->SetPlaybackRange(TRange<FFrameNumber>(FrameAt(0.f), FrameAt(3.f)));
	TestEqual(TEXT("Playback end reflects the movie scene"),
		Helper.GetPlaybackEndTime(), 3.f, 1.e-3f);

	// With no transform section on the camera, the adjust call must leave the range
	// alone rather than crash on a null section.
	Helper.AdjustMoviePlaybackRange(pCamera);
	TestEqual(TEXT("No transform section -> playback range untouched"),
		Helper.GetPlaybackEndTime(), 3.f, 1.e-3f);

	// A null camera is equally tolerated.
	Helper.AdjustMoviePlaybackRange(nullptr);
	TestEqual(TEXT("Null camera -> playback range untouched"),
		Helper.GetPlaybackEndTime(), 3.f, 1.e-3f);

	Helper.AddPActorToLevelSequence(pCamera, bOk, Msg);
	if (!TestTrue(FString::Printf(TEXT("Camera possessed (%s)"), *Msg), bOk))
		return false;

	FSequencerHelperTestAccess::AddTransformTrack(Helper, pCamera, false, bOk, Msg);
	UMovieSceneSection* pSection = FSequencerHelperTestAccess::AddTransformSection(
		Helper, pCamera, FrameAt(5.f), EMovieSceneBlendType::Absolute, bOk, Msg);
	if (!TestNotNull(TEXT("Camera transform section added"), pSection))
		return false;

	Helper.AdjustMoviePlaybackRange(pCamera);
	TestEqual(TEXT("Playback range adopts the section range"),
		Helper.GetPlaybackEndTime(), 5.f, 1.e-3f);
	TestEqual(TEXT("...including its lower bound"),
		Fix.Sequence.Scene()->GetPlaybackRange().GetLowerBoundValue().Value, 0);

	return true;
}

//-----------------------------------------------------------------------------
// HasCameraCutTrack / Add / RemoveCameraCutTrackFromLevelSequence.
//
// The camera cut track is a singleton on the movie scene (AddCameraCutTrack, not
// AddTrack), so "add" is really get-or-create and the bOverwriteExisting branch
// clears animation data rather than replacing the object.
//-----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeqHelperCameraCutTrackTest,
	SEQHELPER_TEST_PREFIX "CameraCutTrack", SEQHELPER_TEST_FLAGS)

bool FSeqHelperCameraCutTrackTest::RunTest(const FString& /*Parameters*/)
{
	using namespace SequencerHelperTests;

	FFixture Fix;
	if (!Fix.Init(*this))
		return false;

	UITwinSequencerHelper& Helper = *Fix.Helper;

	bool bOk = false;
	FString Msg;

	// Nothing yet, and an unresolvable path must answer false rather than crash.
	TestFalse(TEXT("No camera cut track initially"), Helper.HasCameraCutTrack());

	UMovieSceneCameraCutTrack* pTrack =
		Helper.AddCameraCutTrackToLevelSequence(false, bOk, Msg);
	if (!TestNotNull(FString::Printf(TEXT("Camera cut track added (%s)"), *Msg), pTrack))
		return false;
	TestTrue(TEXT("Add reports success"), bOk);
	TestTrue(TEXT("HasCameraCutTrack now agrees"), Helper.HasCameraCutTrack());

	// bOverwriteExisting == false: returns the existing track but reports failure,
	// matching the convention AddTrackToActorInLevelSequence now follows - a non-null
	// return does not by itself mean anything was added.
	UMovieSceneCameraCutTrack* pSame =
		Helper.AddCameraCutTrackToLevelSequence(false, bOk, Msg);
	TestEqual(TEXT("Non-overwriting add returns the existing track"), pSame, pTrack);
	TestFalse(TEXT("Non-overwriting add reports failure"), bOk);
	TestTrue(TEXT("...with an \"already exists\" message"), Msg.Contains(TEXT("already exists")));

	// bOverwriteExisting == true: the same track object is reused, but emptied.
	// Give it a section first, so "emptied" is observable.
	ITwinTests::FScopedTestWorld World;
	ACameraActor* pCamera = World.SpawnActorOfClass<ACameraActor>();
	if (!TestNotNull(TEXT("Camera spawned"), pCamera))
		return false;
	Helper.AddPActorToLevelSequence(pCamera, bOk, Msg);
	Helper.LinkCameraToCameraCutTrack(pCamera, 0.f, 2.f, bOk, Msg);
	if (!TestTrue(FString::Printf(TEXT("Camera linked (%s)"), *Msg), bOk))
		return false;
	TestEqual(TEXT("Track has one cut"), pTrack->GetAllSections().Num(), 1);

	UMovieSceneCameraCutTrack* pOverwritten =
		Helper.AddCameraCutTrackToLevelSequence(true, bOk, Msg);
	TestTrue(TEXT("Overwriting add succeeds"), bOk);
	// RemoveAllAnimationData clears the sections in place; the track is not recreated.
	TestEqual(TEXT("Overwriting add reuses the same track object"), pOverwritten, pTrack);
	TestEqual(TEXT("...with its sections cleared"), pTrack->GetAllSections().Num(), 0);

	Helper.RemoveCameraCutTrackFromLevelSequence(bOk, Msg);
	TestTrue(FString::Printf(TEXT("Camera cut track removed (%s)"), *Msg), bOk);
	TestFalse(TEXT("HasCameraCutTrack is false again"), Helper.HasCameraCutTrack());

	// Removing when there is nothing to remove still reports success: RemoveCameraCutTrack
	// is unconditional and the helper does not probe first. Pinned as observed behaviour -
	// unlike RemoveTrackFromActorInLevelSequence, which reports failure in this case.
	Helper.RemoveCameraCutTrackFromLevelSequence(bOk, Msg);
	TestTrue(TEXT("Second removal still reports success"), bOk);

	return true;
}

//-----------------------------------------------------------------------------
// LinkCameraToCameraCutTrack - four ordered preconditions, then the section.
//
// Also pins the binding kind: the helper creates a *possessable* binding, which
// matters for GetCameraCutBoundCamera below.
//-----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeqHelperLinkCameraTest,
	SEQHELPER_TEST_PREFIX "LinkCameraToCameraCutTrack", SEQHELPER_TEST_FLAGS)

bool FSeqHelperLinkCameraTest::RunTest(const FString& /*Parameters*/)
{
	using namespace SequencerHelperTests;

	FFixture Fix;
	if (!Fix.Init(*this))
		return false;

	UITwinSequencerHelper& Helper = *Fix.Helper;

	bool bOk = false;
	FString Msg;
	const FTimeMap FrameAt(Fix.Sequence);

	ACameraActor* pCamera = Fix.World.SpawnActorOfClass<ACameraActor>();
	if (!TestNotNull(TEXT("Camera spawned"), pCamera))
		return false;

	// Guard 1: null camera, checked before the sequence is even loaded.
	Helper.LinkCameraToCameraCutTrack(nullptr, 0.f, 1.f, bOk, Msg);
	TestFalse(TEXT("Null camera fails"), bOk);
	TestTrue(TEXT("...naming the camera"), Msg.Contains(TEXT("camera actor")));

	// Guard 2: no camera cut track yet. This must fail rather than create one
	// implicitly - the caller is expected to have added the track first.
	Helper.LinkCameraToCameraCutTrack(pCamera, 0.f, 1.f, bOk, Msg);
	TestFalse(TEXT("Missing camera cut track fails"), bOk);
	TestTrue(TEXT("...saying so"), Msg.Contains(TEXT("Camera cut track")));

	UMovieSceneCameraCutTrack* pTrack =
		Helper.AddCameraCutTrackToLevelSequence(false, bOk, Msg);
	if (!TestNotNull(TEXT("Camera cut track added"), pTrack))
		return false;

	// Guard 3: the camera must already be possessed. The helper binds the cut to an

	// existing GUID; it does not possess the actor for you.
	Helper.LinkCameraToCameraCutTrack(pCamera, 0.f, 1.f, bOk, Msg);
	TestFalse(TEXT("Unpossessed camera fails"), bOk);
	TestTrue(TEXT("...saying the actor is not in the sequence"),
		Msg.Contains(TEXT("not found in level sequence")));
	TestEqual(TEXT("No section was created"), pTrack->GetAllSections().Num(), 0);

	const FGuid CameraGuid = Helper.AddPActorToLevelSequence(pCamera, bOk, Msg);
	if (!TestTrue(FString::Printf(TEXT("Camera possessed (%s)"), *Msg), bOk))
		return false;

	// Happy path.
	Helper.LinkCameraToCameraCutTrack(pCamera, 1.f, 3.f, bOk, Msg);
	TestTrue(FString::Printf(TEXT("Camera linked (%s)"), *Msg), bOk);

	if (!TestEqual(TEXT("One cut section was created"), pTrack->GetAllSections().Num(), 1))
		return true;

	UMovieSceneSection* pSection = pTrack->GetAllSections()[0];
	// AddNewCameraCut picks its own end time (FindEndTimeForCameraCut); the helper then
	// overrides the range with [start, end]. Assert the override took effect, since that
	// is the helper's contribution rather than the engine's.
	TestEqual(TEXT("Section starts at the requested time"),
		pSection->GetRange().GetLowerBoundValue().Value, FrameAt(1.f).Value);
	TestEqual(TEXT("Section ends at the requested time"),
		pSection->GetRange().GetUpperBoundValue().Value, FrameAt(3.f).Value);

	// The cut is bound to the camera's possessable GUID.
	UMovieSceneCameraCutSection* pCutSection = Cast<UMovieSceneCameraCutSection>(pSection);
	if (TestNotNull(TEXT("Section is a camera cut section"), pCutSection))
	{
		TestEqual(TEXT("Cut is bound to the camera's GUID"),
			pCutSection->GetCameraBindingID().GetGuid(), CameraGuid);
	}

	// The playback range is reset to [0, end] - note the lower bound is forced to 0
	// regardless of fStartTime, so linking a cut starting at 1s still plays from 0.
	TestEqual(TEXT("Playback range starts at 0"),
		Fix.Sequence.Scene()->GetPlaybackRange().GetLowerBoundValue().Value, 0);
	TestEqual(TEXT("Playback range ends at the cut's end"),
		Helper.GetPlaybackEndTime(), 3.f, 1.e-3f);

	// GetCameraCutBoundCamera resolves through FindSpawnable, but the helper above
	// created a *possessable* binding, so the lookup cannot find it. This is a genuine
	// mismatch between two functions on the same track, not a quirk of the fixture:
	// the two do not compose for any camera linked through LinkCameraToCameraCutTrack.
	// Pinned as observed behaviour - see the note in the review before changing it.
	TestNull(TEXT("GetCameraCutBoundCamera does not resolve a possessable binding"),
		Helper.GetCameraCutBoundCamera(FFrameTime(FrameAt(2.f))));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeqHelperInitializeTest,
	SEQHELPER_TEST_PREFIX "Initialize", SEQHELPER_TEST_FLAGS)

bool FSeqHelperInitializeTest::RunTest(const FString& /*Parameters*/)
{
	TStrongObjectPtr<UITwinSequencerHelper> Helper(
		NewObject<UITwinSequencerHelper>(GetTransientPackage()));

	// An unresolvable path must fail cleanly rather than crash, and must leave the
	// helper unusable rather than half-initialized.
	TestFalse(TEXT("Unknown sequence path fails"),
		Helper->Initialize(TEXT("/Game/DoesNotExist.DoesNotExist")));
	TestFalse(TEXT("Helper stays invalid"), Helper->IsInitialized());

	// Every accessor must tolerate being called before a successful Initialize.
	bool bOk = true;
	FString Msg;
	Helper->GetPActorGuidFromLevelSequence(nullptr, bOk, Msg);
	TestFalse(TEXT("Call before init fails cleanly"), bOk);

	SequencerHelperTests::FScopedTestSequence Seq;
	if (!TestTrue(TEXT("Level sequence created"), Seq.IsValid()))
		return false;
	TestTrue(TEXT("Valid path succeeds"), Helper->Initialize(Seq.Path()));
	TestTrue(TEXT("Idempotent re-init"), Helper->Initialize(Seq.Path()));
	return true;
}

#undef SEQHELPER_TEST_FLAGS
#undef SEQHELPER_TEST_PREFIX

#endif // WITH_TESTS
