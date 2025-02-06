/*--------------------------------------------------------------------------------------+
|
|     $Source: TimelineTest.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

// From vue.git/viewer/Code/Tools/UnitTests/ScheduleTests.cpp

#include <CoreMinimal.h>
#include <Math/Vector.h>
#include <Misc/AutomationTest.h>
#include <Misc/LowLevelTestAdapter.h>

#include <Hashing/UnrealMath.h>
#include <Timeline/Definition.h>

namespace ITwin::Timeline {

ITWIN_TIMELINE_DEFINE_PROPERTY_VALUES(Test_Visibility,
	(float, test_value_)
)
TSharedPtr<FJsonValue> ToJsonValue(Test_Visibility const&) { return MakeShared<FJsonValueNumber>(0.f); }
inline bool NoEffect(Test_Visibility const& Prop) { return Prop.test_value_ == 1.f; }

ITWIN_TIMELINE_DEFINE_PROPERTY_VALUES(Test_Color,
	(bool, test_hasColor_, ITwin::Timeline::Interpolators::BoolOr)
	(FVector, test_value_)
)
TSharedPtr<FJsonValue> ToJsonValue(Test_Color const&) { return MakeShared<FJsonValueNumber>(0.f); }
inline bool NoEffect(Test_Color const& Prop) { return !Prop.test_hasColor_; }

ITWIN_TIMELINE_DEFINE_PROPERTY_VALUES(Test_Transform,
	(FQuat, test_orientation_)
	(FVector, test_position_)
)
TSharedPtr<FJsonValue> ToJsonValue(Test_Transform const&) { return MakeShared<FJsonValueNumber>(0.f); }
inline bool NoEffect(Test_Transform const& Prop) { return false; }

namespace Interpolators
{
	class PlaneEquationBroken
	{
	public:
		FContinue operator ()(FVector4f& Out, const FVector4f& x0, const FVector4f& x1, float u, void*) const
		{
			Out = x0 * (1.f - u) + x1 * u; // not a proper interpolation (ok for unit testing only)
			return Continue;
		}
	};
}

ITWIN_TIMELINE_DEFINE_PROPERTY_VALUES(Test_CuttingPlane,
	(FVector4f, test_planeEquation_, ITwin::Timeline::Interpolators::PlaneEquationBroken)
	(bool, test_fullyVisible_, ITwin::Timeline::Interpolators::BoolAnd)
	(bool, test_fullyHidden_, ITwin::Timeline::Interpolators::BoolAnd)
)
TSharedPtr<FJsonValue> ToJsonValue(Test_CuttingPlane const&) { return MakeShared<FJsonValueNumber>(0.f); }
inline bool NoEffect(Test_CuttingPlane const& Prop) { return Prop.test_fullyVisible_; }

ITWIN_TIMELINE_DEFINE_OBJECT_PROPERTIES(Test_Element,
	(Test_Visibility, test_visibility_)
	(Test_Color, test_color_)
	(Test_Transform, test_transform_)
	(Test_CuttingPlane, test_cuttingPlane_)
)
TSharedPtr<FJsonValue> ToJsonValue(Test_Element const&) { return MakeShared<FJsonValueNumber>(0.f); }

class Test_ElementTimelineEx: public Test_ElementTimeline
{
public:
	using Super = Test_ElementTimeline;
	int32_t test_stuff_;
};

template<class _T>
std::enable_if_t<std::is_arithmetic<_T>::value, bool> AreApproxEqualGeneric(const _T& x, const _T& y)
{
	return std::abs(x-y) < 1e-3;
}

template<class _T>
bool AreApproxEqualGeneric(const std::optional<_T>& x, const std::optional<_T>& y)
{
	return (bool)x == (bool)y && (!x || AreApproxEqualGeneric(*x, *y));
}

bool AreApproxEqualGeneric(const FVector& x, const FVector& y)
{
	return AreApproxEqualGeneric(x.X, y.X) &&
		AreApproxEqualGeneric(x.Y, y.Y) &&
		AreApproxEqualGeneric(x.Z, y.Z);
}

bool AreApproxEqualGeneric(const FVector4f& x, const FVector4f& y)
{
	return AreApproxEqualGeneric(x.X, y.X) &&
		AreApproxEqualGeneric(x.Y, y.Y) &&
		AreApproxEqualGeneric(x.Z, y.Z) &&
		AreApproxEqualGeneric(x.W, y.W);
}

bool AreApproxEqualGeneric(const FQuat& x, const FQuat& y)
{
	return AreApproxEqualGeneric(x.X, y.X) &&
		AreApproxEqualGeneric(x.Y, y.Y) &&
		AreApproxEqualGeneric(x.Z, y.Z) &&
		AreApproxEqualGeneric(x.W, y.W);
}

template<class _Base>
bool AreApproxEqualGeneric(const BoostFusionUtils::SequenceEx<_Base>& x,
						   const BoostFusionUtils::SequenceEx<_Base>& y)
{
	return boost::fusion::all(boost::fusion::zip(x, y),
		[](const auto& memberPair)
		{
			return AreApproxEqualGeneric(boost::fusion::at_c<0>(memberPair),
										 boost::fusion::at_c<1>(memberPair));
		});
}

bool AreApproxEqualGeneric(const Test_Color& x, const Test_Color& y)
{
	return x.test_hasColor_ == y.test_hasColor_
		&& (!x.test_hasColor_ || AreApproxEqualGeneric(x.test_value_, y.test_value_));
}

void AreApproxEqual(FAutomationTestBase& Test, const Test_ElementState& x, const Test_ElementState& y)
{
	Test.TestTrue("AreApproxEqual", AreApproxEqualGeneric(x, y));
}

void VisibilityApproxEqual(FAutomationTestBase& Test, const Test_ElementState& x, float const ref)
{
	Test.TestTrue("AreApproxEqual", AreApproxEqualGeneric(x.test_visibility_->test_value_, ref));
}

} // namespace ITwin::Timeline

using namespace ITwin::Timeline;

BEGIN_DEFINE_SPEC(GetStateAtTimeSpec, "Bentley.ITwinForUnreal.ITwinRuntime.Timeline", \
				  EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
	std::optional<Test_ElementTimeline> ElementTimeline;
END_DEFINE_SPEC(GetStateAtTimeSpec)
void GetStateAtTimeSpec::Define()
{
	Describe("A spec for GetStateAtTime methods", [this]()
		{
			BeforeEach([this]()
				{
					ElementTimeline.emplace();
					// Insert unsorted test keyframes
					{
						ITwin::Timeline::PropertyEntry<Test_Visibility> entry;
						entry.Time = 100.;
						entry.Interpolation = ITwin::Timeline::EInterpolation::Linear;
						entry.test_value_ = 0.6f;
						ElementTimeline->test_visibility_.Values.insert(entry);
					}
					{
						ITwin::Timeline::PropertyEntry<Test_Visibility> entry;
						entry.Time = 200.;
						entry.Interpolation = ITwin::Timeline::EInterpolation::Step;
						entry.test_value_ = 0.2f;
						ElementTimeline->test_visibility_.Values.insert(entry);
					}
					{
						ITwin::Timeline::PropertyEntry<Test_Color> entry;
						entry.Time = 150;
						entry.Interpolation = ITwin::Timeline::EInterpolation::Step;
						entry.test_hasColor_ = true;
						entry.test_value_ = {0.5f, 0.7f, 0.9f};
						ElementTimeline->test_color_.Values.insert(entry);
					}
					{
						ITwin::Timeline::PropertyEntry<Test_Color> entry;
						entry.Time = 190;
						entry.Interpolation = ITwin::Timeline::EInterpolation::Step;
						entry.test_hasColor_ = false;
						ElementTimeline->test_color_.Values.insert(entry);
					}
					{
						ITwin::Timeline::PropertyEntry<Test_Color> entry;
						entry.Time = 200;
						entry.Interpolation = ITwin::Timeline::EInterpolation::Step;
						entry.test_hasColor_ = true;
						entry.test_value_ = {0.15f, 0.37f, 0.43f};
						ElementTimeline->test_color_.Values.insert(entry);
					}
					{
						ITwin::Timeline::PropertyEntry<Test_CuttingPlane> entry;
						entry.Time = 150;
						entry.Interpolation = ITwin::Timeline::EInterpolation::Linear;
						entry.test_planeEquation_ = FVector4f(1, 0, 0, 10);
						ElementTimeline->test_cuttingPlane_.Values.insert(entry);
					}
					{
						ITwin::Timeline::PropertyEntry<Test_CuttingPlane> entry;
						entry.Time = 190;
						entry.Interpolation = ITwin::Timeline::EInterpolation::Linear;
						entry.test_planeEquation_ = FVector4f(1, 0, 0, 20);
						ElementTimeline->test_cuttingPlane_.Values.insert(entry);
					}
				});
			It("checks time < first keyframe, returns all earliest values",
				[this]() {
					AreApproxEqual(*this,
						Test_ElementState{
							Test_Visibility{0.6f},
							Test_Color{true, FVector{0.5f, 0.7f, 0.9f}},
							{},
							Test_CuttingPlane{FVector4f(1,0,0,10), false, false}},
						ElementTimeline->GetStateAtTime(90,
							ITwin::Timeline::StateAtEntryTimeBehavior::UseLeftInterval, nullptr));
				});
			It("checks time == first keyframe returns all earliest values",
				[this]() {
					AreApproxEqual(*this,
						Test_ElementState{
							Test_Visibility{0.6f},
							Test_Color{true, FVector{0.5f, 0.7f, 0.9f}},
							{},
							Test_CuttingPlane{FVector4f(1,0,0,10), false, false}},
						ElementTimeline->GetStateAtTime(100,
							ITwin::Timeline::StateAtEntryTimeBehavior::UseLeftInterval, nullptr));
				});
			It("checks time strictly between keyframes returns the expected values with interpolation",
				[this]() {
					AreApproxEqual(*this,
						Test_ElementState{
							Test_Visibility{0.36f},
							Test_Color{true, FVector{0.5f, 0.7f, 0.9f}},
							{},
							Test_CuttingPlane{FVector4f(1,0,0,12.5f), false, false}},
						ElementTimeline->GetStateAtTime(160,
							ITwin::Timeline::StateAtEntryTimeBehavior::UseLeftInterval, nullptr));
				});
			It("checks time == intermediate keyframe returns the expected values with interpolation",
				[this]() {
					AreApproxEqual(*this,
						Test_ElementState{
							Test_Visibility{0.24f},
							Test_Color{true, FVector{0.5f, 0.7f, 0.9f}},
							{},
							Test_CuttingPlane{FVector4f(1,0,0,20), false, false}},
						ElementTimeline->GetStateAtTime(190,
							ITwin::Timeline::StateAtEntryTimeBehavior::UseLeftInterval, nullptr));
				});
			It("checks time == intermediate keyframe handles UseRightInterval properly",
				[this]() {
					AreApproxEqual(*this,
						Test_ElementState{
							Test_Visibility{0.24f},
							Test_Color{false, {}},
							{},
							Test_CuttingPlane{FVector4f(1,0,0,20), false, false}},
						ElementTimeline->GetStateAtTime(190, 
							ITwin::Timeline::StateAtEntryTimeBehavior::UseRightInterval, nullptr));
				});
			It("checks time == last keyframe returns all latest values",
				[this]() {
					AreApproxEqual(*this,
						Test_ElementState{
							Test_Visibility{0.2f},
							Test_Color{true, FVector{0.15f, 0.37f, 0.43f}},
							{},
							Test_CuttingPlane{FVector4f(1,0,0,20), false, false}},
						ElementTimeline->GetStateAtTime(200,
							ITwin::Timeline::StateAtEntryTimeBehavior::UseLeftInterval, nullptr));
				});
			It("checks time > last keyframe returns all latest values",
				[this]() {
					AreApproxEqual(*this,
						Test_ElementState{
							Test_Visibility{0.2f},
							Test_Color{true, FVector{0.15f, 0.37f, 0.43f}},
							{},
							Test_CuttingPlane{FVector4f(1,0,0,20), false, false}},
						ElementTimeline->GetStateAtTime(210,
							ITwin::Timeline::StateAtEntryTimeBehavior::UseLeftInterval, nullptr));
				});

			Describe("A spec for EInterpolation::Next",
				[this]() {
					// Add a new keyframe after the former last Visibility keyframe, which was set
					// with Next interpolation (see BeforeEach call)
					BeforeEach([this]() {
							ITwin::Timeline::PropertyEntry<Test_Visibility> entry;
							entry.Time = 220.;
							entry.Interpolation = ITwin::Timeline::EInterpolation::Next;
							entry.test_value_ = 0.4f;
							ElementTimeline->test_visibility_.Values.insert(entry);
						});
					// The value 0.2f from keyframe(t=200) is used both at t=200 and t>200, because of the
					// Linear interpolation used for at t=100 and the Step at t=200 (see BeforeEach)
					It("should use value from keyframe(t=200) because of interp=Step", [this]() {
						VisibilityApproxEqual(*this,
							ElementTimeline->GetStateAtTime(200,
								ITwin::Timeline::StateAtEntryTimeBehavior::UseLeftInterval, nullptr),
							0.2f);
						VisibilityApproxEqual(*this,
							ElementTimeline->GetStateAtTime(210,
								ITwin::Timeline::StateAtEntryTimeBehavior::UseLeftInterval, nullptr),
							0.2f);
						// And this is independent of the Use*Interval
						VisibilityApproxEqual(*this,
							ElementTimeline->GetStateAtTime(200,
								ITwin::Timeline::StateAtEntryTimeBehavior::UseRightInterval, nullptr),
							0.2f);
						VisibilityApproxEqual(*this,
							ElementTimeline->GetStateAtTime(219,
								ITwin::Timeline::StateAtEntryTimeBehavior::UseRightInterval, nullptr),
							0.2f);
						});
					It("should always use value from keyframe(t=220) when it is last",
						[this]()
						{
							VisibilityApproxEqual(*this,
								ElementTimeline->GetStateAtTime(220,
									ITwin::Timeline::StateAtEntryTimeBehavior::UseLeftInterval, nullptr),
								// would be 0.2 if not the last keyframe - hack introduced in Schedule.inl to
								// conform to iModel.js behavior :/
								0.4f);
							VisibilityApproxEqual(*this,
								ElementTimeline->GetStateAtTime(220,
									ITwin::Timeline::StateAtEntryTimeBehavior::UseRightInterval, nullptr),
								0.4f);
							// Add a new keyframe at t > 220
							ITwin::Timeline::PropertyEntry<Test_Visibility> entry;
							entry.Time = 240.;
							entry.Interpolation = ITwin::Timeline::EInterpolation::Step;
							entry.test_value_ = 0.5f;
							ElementTimeline->test_visibility_.Values.insert(entry);
							VisibilityApproxEqual(*this,
								ElementTimeline->GetStateAtTime(220,
									ITwin::Timeline::StateAtEntryTimeBehavior::UseLeftInterval, nullptr),
								// so now it's really 0.2
								0.2f);
							VisibilityApproxEqual(*this,
								ElementTimeline->GetStateAtTime(220,
									ITwin::Timeline::StateAtEntryTimeBehavior::UseRightInterval, nullptr),
								// now it's 0.5 because entry0 for RightInterval is 220, which interp is Next
								0.5f);
						});
					It("should use value from Next keyframe(t=240)",
						[this]()
						{
							// t=220 is still the last keyframe, its value should be returned
							VisibilityApproxEqual(*this,
								ElementTimeline->GetStateAtTime(230,
									ITwin::Timeline::StateAtEntryTimeBehavior::UseLeftInterval, nullptr),
								0.4f);
							// Add a new keyframe at t > 220
							ITwin::Timeline::PropertyEntry<Test_Visibility> entry;
							entry.Time = 240.;
							entry.Interpolation = ITwin::Timeline::EInterpolation::Step;
							entry.test_value_ = 0.5f;
							ElementTimeline->test_visibility_.Values.insert(entry);
							// t=220 is no longer the last keyframe, but the interpolation used is that of
							// entry0, which differs with Use*Interval
							// so these first 2 tests are actually the same as the other test above...
							VisibilityApproxEqual(*this,
								ElementTimeline->GetStateAtTime(220,
									ITwin::Timeline::StateAtEntryTimeBehavior::UseLeftInterval, nullptr),
								0.2f);
							VisibilityApproxEqual(*this,
								ElementTimeline->GetStateAtTime(220,
									ITwin::Timeline::StateAtEntryTimeBehavior::UseRightInterval, nullptr),
								0.5f);
							VisibilityApproxEqual(*this,
								ElementTimeline->GetStateAtTime(240,
									ITwin::Timeline::StateAtEntryTimeBehavior::UseLeftInterval, nullptr),
								0.5f);
							VisibilityApproxEqual(*this,
								ElementTimeline->GetStateAtTime(240,
									ITwin::Timeline::StateAtEntryTimeBehavior::UseRightInterval, nullptr),
								0.5f);
							VisibilityApproxEqual(*this,
								ElementTimeline->GetStateAtTime(250,
									ITwin::Timeline::StateAtEntryTimeBehavior::UseLeftInterval, nullptr),
								0.5f);
						});
					It("should not interpolate linearly even when Next is followed by Linear",
						[this]()
						{
							// Add a new keyframe at t > 220 with Linear interpolation: but Next 'cuts' the
							// timeline into independent parts on purpose, so it will behave as Step
							ITwin::Timeline::PropertyEntry<Test_Visibility> entry;
							entry.Time = 240.;
							entry.Interpolation = ITwin::Timeline::EInterpolation::Linear;
							entry.test_value_ = 0.5f;
							ElementTimeline->test_visibility_.Values.insert(entry);
							// Same tests as the above test with Step:
							VisibilityApproxEqual(*this,
								ElementTimeline->GetStateAtTime(220,
									ITwin::Timeline::StateAtEntryTimeBehavior::UseLeftInterval, nullptr),
								0.2f);
							VisibilityApproxEqual(*this,
								ElementTimeline->GetStateAtTime(220,
									ITwin::Timeline::StateAtEntryTimeBehavior::UseRightInterval, nullptr),
								0.5f);
							VisibilityApproxEqual(*this,
								ElementTimeline->GetStateAtTime(240,
									ITwin::Timeline::StateAtEntryTimeBehavior::UseLeftInterval, nullptr),
								0.5f);
							VisibilityApproxEqual(*this,
								ElementTimeline->GetStateAtTime(240,
									ITwin::Timeline::StateAtEntryTimeBehavior::UseRightInterval, nullptr),
								0.5f);
							VisibilityApproxEqual(*this,
								ElementTimeline->GetStateAtTime(250,
									ITwin::Timeline::StateAtEntryTimeBehavior::UseLeftInterval, nullptr),
								0.5f);
							VisibilityApproxEqual(*this,
								ElementTimeline->GetStateAtTime(220,
									ITwin::Timeline::StateAtEntryTimeBehavior::UseRightInterval, nullptr),
								0.5f);
							// Test no interp:
							VisibilityApproxEqual(*this,
								ElementTimeline->GetStateAtTime(230,
									ITwin::Timeline::StateAtEntryTimeBehavior::UseLeftInterval, nullptr),
								0.5f);
						});
				});
		}); // end of Describe call
} // end of Define method

BEGIN_DEFINE_SPEC(MainTimelineAddSpec, "Bentley.ITwinForUnreal.ITwinRuntime.Timeline", \
				  EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
	std::optional<ITwin::Timeline::MainTimelineBase<Test_ElementTimelineEx>> Timeline;
END_DEFINE_SPEC(MainTimelineAddSpec)
void MainTimelineAddSpec::Define()
{
	BeforeEach([this]()
		{
			Timeline.emplace();
			{
				auto ElementTimeline = std::make_shared<Test_ElementTimelineEx>();
				ElementTimeline->test_stuff_ = 12;
				{
					ITwin::Timeline::PropertyEntry<Test_Visibility> entry;
					entry.Time = 100.;
					ElementTimeline->test_visibility_.Values.insert(entry);
				}
				{
					ITwin::Timeline::PropertyEntry<Test_Color> entry;
					entry.Time = 100.;
					ElementTimeline->test_color_.Values.insert(entry);
				}
				{
					ITwin::Timeline::PropertyEntry<Test_Color> entry;
					entry.Time = 200.;
					ElementTimeline->test_color_.Values.insert(entry);
				}
				Timeline->AddTimeline(ElementTimeline);
			}
			{
				auto ElementTimeline = std::make_shared<Test_ElementTimelineEx>();
				ElementTimeline->test_stuff_ = 34;
				{
					ITwin::Timeline::PropertyEntry<Test_Transform> entry;
					entry.Time = 150.;
					ElementTimeline->test_transform_.Values.insert(entry);
				}
				{
					ITwin::Timeline::PropertyEntry<Test_Transform> entry;
					entry.Time = 200.;
					ElementTimeline->test_transform_.Values.insert(entry);
				}
				{
					ITwin::Timeline::PropertyEntry<Test_CuttingPlane> entry;
					entry.Time = 300.;
					ElementTimeline->test_cuttingPlane_.Values.insert(entry);
				}
				Timeline->AddTimeline(ElementTimeline);
			}
		});
	It("should contain the right number of keyframes", [this]() {
			TestTrue("count keyframes", 2 == (int)Timeline->GetContainer().size());
		});
	It("should span the right time range", [this]() {
			TestTrue("get time range", std::make_pair(100., 300.) == Timeline->GetTimeRange());
		});
}

// No I/O (yet?) in ITwinRuntime
//
//IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestReadWrite, "Bentley.ITwinForUnreal.ITwinRuntime.Timeline", \
//	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
//{
//	std::vector<uint8_t> buffer;
//	std::vector<std::shared_ptr<Test_ElementTimelineEx>> allElementTimelines;
//	{
//		ITwin::Timeline::MainTimelineBase<Test_ElementTimelineEx> Timeline;
//		{
//			auto ElementTimeline = std::make_shared<Test_ElementTimelineEx>();
//			ElementTimeline->test_stuff_ = 12;
//			{
//				ITwin::Timeline::PropertyEntry<Test_Visibility> entry;
//				entry.Time = 100.;
//				entry.test_value_ = 0.5f;
//				ElementTimeline->test_visibility_.Values.insert(entry);
//			}
//			{
//				ITwin::Timeline::PropertyEntry<Test_Color> entry;
//				entry.Time = 100.;
//				entry.Interpolation = ITwin::Timeline::EInterpolation::Linear;
//				ElementTimeline->test_color_.Values.insert(entry);
//			}
//			{
//				ITwin::Timeline::PropertyEntry<Test_Color> entry;
//				entry.Time = 200.;
//				entry.test_hasColor_ = true;
//				entry.test_value_ = FVector{0.5f, 0.7f, 0.9f};
//				ElementTimeline->test_color_.Values.insert(entry);
//			}
//			Timeline->AddTimeline(ElementTimeline);
//			allElementTimelines.insert(elementTimeline);
//		}
//		{
//			auto ElementTimeline = std::make_shared<Test_ElementTimelineEx>();
//			ElementTimeline->test_stuff_ = 34;
//			{
//				ITwin::Timeline::PropertyEntry<Test_Transform> entry;
//				entry.Time = 150.;
//				entry.Interpolation = ITwin::Timeline::EInterpolation::Linear;
//				entry.test_orientation_ = {0.1f,0.2f,0.3f,0.4f};
//				entry.test_position_ = {10,20,30};
//				ElementTimeline->test_transform_.Values.insert(entry);
//			}
//			{
//				ITwin::Timeline::PropertyEntry<Test_CuttingPlane> entry;
//				entry.Time = 300.;
//				entry.test_planeEquation_ = {0,1,0,10};
//				entry.test_fullyVisible_ = false;
//				entry.test_fullyHidden_ = false;
//				ElementTimeline->test_cuttingPlane_.Values.insert(entry);
//			}
//			{
//				ITwin::Timeline::PropertyEntry<Test_CuttingPlane> entry;
//				entry.Time = 350.;
//				entry.test_fullyVisible_ = true;
//				ElementTimeline->test_cuttingPlane_.Values.insert(entry);
//			}
//			Timeline->AddTimeline(ElementTimeline);
//			allElementTimelines.insert(elementTimeline);
//		}
//		BEStream stream(ST_MemoryWrite, (HGLOBAL)nullptr, BE_ENDIAN_LITTLE);
//		WriteStreamValue(&stream, Timeline);
//		const auto streamSize = stream.MakeMemoryRead();
//		buffer.assign((const uint8_t*)stream.GetReadMemoryPtr(), (const uint8_t*)stream.GetReadMemoryPtr()+streamSize);
//	}
//	ITwin::Timeline::MainTimelineBase<Test_ElementTimelineEx> Timeline;
//	{
//		BEStream stream(ST_MemoryRead, (HGLOBAL)buffer.data(), BE_ENDIAN_LITTLE);
//		ReadStreamValue(&stream, Timeline);
//	}
//	BOOST_TEST_REQUIRE(2 == (int)Timeline.GetContainer().size());
//	BOOST_TEST(*allElementTimelines[0] == *Timeline.GetContainer()[0]);
//	BOOST_TEST(*allElementTimelines[1] == *Timeline.GetContainer()[1]);
//	BOOST_TEST(std::make_pair(100., 350.) == Timeline.GetTimeRange());
//}
