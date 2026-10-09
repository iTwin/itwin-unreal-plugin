/*--------------------------------------------------------------------------------------+
|
|     $Source: TimelineJsonOut.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "Timeline.h"

#include <Dom/JsonObject.h>
#include <Dom/JsonValue.h>
#include <Math/UnrealMathUtility.h>
#include <Policies/CondensedJsonPrintPolicy.h>
#include <Policies/PrettyJsonPrintPolicy.h>
#include <Serialization/JsonSerializer.h>

#include <set>

namespace ITwin::Timeline {

namespace {
	FString ToString(double Value, int Decimals)
	{
		// Rmd: "%f" prints only 6 decimals by default, %g avoids trailing zeroes but decimals become digits...
		auto Str = FString::Printf(TEXT("%.*f"), (Decimals >= 0 ? Decimals : DBL_DECIMAL_DIG), Value);
		if (Str.Contains(TEXT(".")))
		{
			int32 End = Str.Len();
			while (End > 0 && TEXT('0') == (*Str)[End - 1])
				End--;
			Str.RemoveAt(End, Str.Len() - End);
			if ((*Str)[End - 1] == TEXT('.'))
				Str += TEXT('0');
		}
		return Str;
	}
}

TSharedPtr<FJsonValue> ToJsonValue(PVisibility const& Prop, int Decimals)
{
	return MakeShared<FJsonValueNumberString>(ToString(Prop.Value, Decimals));
}

TSharedPtr<FJsonValue> ToJsonValue(PColor const& Prop, int Decimals)
{
	return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>(
		{ MakeShared<FJsonValueBoolean>(Prop.bHasColor == ITwin::Flag::Present),
		  MakeShared<FJsonValueNumberString>(ToString(Prop.Value.X, Decimals)),
		  MakeShared<FJsonValueNumberString>(ToString(Prop.Value.Y, Decimals)),
		  MakeShared<FJsonValueNumberString>(ToString(Prop.Value.Z, Decimals)) }));
}

TSharedPtr<FJsonValue> ToJsonValue(PClippingPlane const& Prop, int Decimals)
{
	auto&& PlaneDir = Prop.DefrdPlaneEq.PlaneOrientation;
	return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>(
		{ MakeShared<FJsonValueString>(GetGrowthStatusString(Prop.DefrdPlaneEq.GrowthStatus)),
		  MakeShared<FJsonValueNumberString>(ToString(PlaneDir.X, Decimals)),
		  MakeShared<FJsonValueNumberString>(ToString(PlaneDir.Y, Decimals)),
		  MakeShared<FJsonValueNumberString>(ToString(PlaneDir.Z, Decimals)),
		  MakeShared<FJsonValueNumberString>(ToString(Prop.DefrdPlaneEq.PlaneW, Decimals)) }));
}

TSharedPtr<FJsonValue> ToJsonValue(PTransform const& Prop, int Decimals)
{
	if (Prop.bIsTransformed)
	{
		auto JsonObj = MakeShared<FJsonObject>();
		auto&& Translation = Prop.Position;
		JsonObj->SetArrayField(TEXT("translation"), TArray<TSharedPtr<FJsonValue>>(
			{ MakeShared<FJsonValueNumberString>(ToString(Translation.X, Decimals)),
			  MakeShared<FJsonValueNumberString>(ToString(Translation.Y, Decimals)),
			  MakeShared<FJsonValueNumberString>(ToString(Translation.Z, Decimals)) }));
		FVector Orientation = Prop.Rotation.ToRotationVector();
		double LenIsAngle = Orientation.SquaredLength();
		if (LenIsAngle != 0.)
		{
			LenIsAngle = std::sqrt(LenIsAngle);
			Orientation /= LenIsAngle; // yes, see FQuat::ToRotationVector's doc
			JsonObj->SetArrayField(TEXT("rotationAxis"), TArray<TSharedPtr<FJsonValue>>(
				{ MakeShared<FJsonValueNumberString>(ToString(Orientation.X, Decimals)),
				  MakeShared<FJsonValueNumberString>(ToString(Orientation.Y, Decimals)),
				  MakeShared<FJsonValueNumberString>(ToString(Orientation.Z, Decimals)) }));
			JsonObj->SetField(TEXT("rotationAngleDegrees"),
				MakeShared<FJsonValueNumberString>(ToString(FMath::RadiansToDegrees(LenIsAngle), Decimals)));
		}
		if (Prop.DefrdAnchor.IsDeferred() || EAnchorPoint::Static == Prop.DefrdAnchor.AnchorPoint)
		{
			JsonObj->SetStringField(TEXT("anchor"), GetAnchorPointString(Prop.DefrdAnchor.AnchorPoint));
		}
		else
		{
			JsonObj->SetArrayField(TEXT("anchor"), TArray<TSharedPtr<FJsonValue>>(
				{ MakeShared<FJsonValueNumberString>(ToString(Prop.DefrdAnchor.Offset.X, Decimals)),
				  MakeShared<FJsonValueNumberString>(ToString(Prop.DefrdAnchor.Offset.Y, Decimals)),
				  MakeShared<FJsonValueNumberString>(ToString(Prop.DefrdAnchor.Offset.Z, Decimals)) }));
		}
		return MakeShared<FJsonValueObject>(std::move(JsonObj));
	}
	else
	{
		return MakeShared<FJsonValueString>(TEXT("Untransformed"));
	}
}

namespace Detail
{
	std::set<ITwinElementID> GetSortedElements(MainTimeline::ObjectTimeline const& A)
	{
		std::set<ITwinElementID> Sorted;
		for (auto&& Elem : A.GetIModelElements())
			Sorted.insert(Elem);
		return Sorted;
	}
}

void ElementTimelineEx::ToJson(TSharedRef<FJsonObject>& JsonObj) const
{
	// See MainTimeline::ToJsonString
	auto SortedElems = Detail::GetSortedElements(*this);
	TArray<TSharedPtr<FJsonValue>> JsonElems;
	for (auto&& Elem : SortedElems)
	{
		// no uint64 json value :/
		JsonElems.Add(MakeShared<FJsonValueString>(ITwin::ToString(Elem)));
	}
	JsonObj->SetArrayField(TEXT("elementIds"), JsonElems);
	TSharedPtr<FJsonValue> ElemKeyJson;
	std::visit([&ElemKeyJson, &SortedElems](auto&& ElemKey)
		{
			using T = std::decay_t<decltype(ElemKey)>;
			if constexpr (std::is_same_v<T, ITwinElementID>)
			{
				// Don't print the key if it's the same as the single assigned Element
				if (!(SortedElems.size() == 1 && *(SortedElems.begin()) == ElemKey))
				{
					ElemKeyJson = MakeShared<FJsonValueString>(ITwin::ToString(ElemKey));
				}
			}
			else if constexpr (std::is_same_v<T, FGuid>)
			{
				ElemKeyJson = MakeShared<FJsonValueString>(ElemKey.ToString(EGuidFormats::DigitsWithHyphensLower));
			}
			else if constexpr (std::is_same_v<T, size_t>)
			{
				ElemKeyJson = MakeShared<FJsonValueNumber>(ElemKey);
			}
			else static_assert(always_false_v<T>, "non-exhaustive visitor!");
		},
		IModelElementsKey.Key);
	if (ElemKeyJson)
		JsonObj->SetField(TEXT("animationKey"), ElemKeyJson);
	Super::ToJson(JsonObj);
}

template<typename JsonPrintPolicy> FString ElementTimelineEx::ToJsonString() const
{
	auto JsonObj = MakeShared<FJsonObject>();
	ToJson(JsonObj);
	FString JsonString;
	TSharedRef<TJsonWriter<TCHAR, JsonPrintPolicy>> JsonWriter =
		TJsonWriterFactory<TCHAR, JsonPrintPolicy>::Create(&JsonString);
	FJsonSerializer::Serialize(JsonObj, JsonWriter);
	return JsonString;
}

FString ElementTimelineEx::ToCondensedJsonString() const
{
	return ToJsonString<TCondensedJsonPrintPolicy<TCHAR>>();
}

FString ElementTimelineEx::ToPrettyJsonString() const
{
	return ToJsonString<TPrettyJsonPrintPolicy<TCHAR>>();
}

struct TimelineCompareOrderedElementIDs
{
	bool operator()(MainTimeline::ObjectTimelinePtr const& A, MainTimeline::ObjectTimelinePtr const& B) const
	{
		// The FElementsGroup must be sorted, too...
		std::set<ITwinElementID> SortedA = Detail::GetSortedElements(*A);
		std::set<ITwinElementID> SortedB = Detail::GetSortedElements(*B);
		auto ItA = SortedA.begin(), ItB = SortedB.begin();
		for (; ItA != SortedA.end() && ItB != SortedB.end(); ++ItA, ++ItB)
		{
			if ((*ItA) != (*ItB))
			{
				return (*ItA) < (*ItB);
			}
		}
		// Both sets were equal until we reached (above) the end of either or both, so three possibilities:
		if (ItA == SortedA.end() && ItB == SortedB.end()) // A == B => !(A < B)
		{
			// CreateTimelineKeyframesWithTaskDependencies can actually have created split groups that happen to
			// contain the same Elements as existing groups, but we didn't look for them, simply for ease of
			// implementation: to avoid doing such changes now just to please unit tests, we need to include the
			// animation key here (only needed for the 'group' variant) in the comparison to avoid losing timelines
			// when inserting them in the set.
			return A->GetIModelElementsKey() < B->GetIModelElementsKey();
		}
		else if (ItA == SortedA.end()) // A is shorter (ie subset of B) => A < B
			return true;
		else // A superset of B => A > B
			return false;
	}
};

template<typename JsonPrintPolicy> FString MainTimeline::ToJsonString() const
{
	TArray<TSharedPtr<FJsonValue>> TimelinesArray;
	TimelinesArray.Reserve((int32)GetContainer().size());
	// Use a deterministic ordering, since the order of timelines in the container can depend on the order
	// of data received from 4D api. ElementsKeyToTimeline isn't suitable either (even with a fixed hash func)
	// because FIModelElementsKey can be the Elements group index, which also depends on http replies ordering.
	std::set<ObjectTimelinePtr, TimelineCompareOrderedElementIDs> OrderedTimelines;
	for (auto&& ElementTimeline : GetContainer())
	{
		// If it pops, check TimelineCompareOrderedElementIDs :/
		ensure(OrderedTimelines.end() == OrderedTimelines.find(ElementTimeline));
		// CreateTimelineKeyframesWithTaskDependencies can empty some timelines :/ (why?)
		if (!ElementTimeline->GetIModelElements().empty())
			OrderedTimelines.insert(ElementTimeline);
	}
	for (auto&& ElementTimeline : OrderedTimelines)
	{
		auto ElementTimelineJsonObj = MakeShared<FJsonObject>();
		ElementTimeline->ToJson(ElementTimelineJsonObj);
		TimelinesArray.Add(MakeShared<FJsonValueObject>(ElementTimelineJsonObj));
	}
	FString JsonString;
	TSharedRef<TJsonWriter<TCHAR, JsonPrintPolicy>> JsonWriter =
		TJsonWriterFactory<TCHAR, JsonPrintPolicy>::Create(&JsonString);
	FJsonSerializer::Serialize(TimelinesArray, JsonWriter);
	return JsonString;
}

FString MainTimeline::ToCondensedJsonString() const
{
	return ToJsonString<TCondensedJsonPrintPolicy<TCHAR>>();
}

FString MainTimeline::ToPrettyJsonString() const
{
	return ToJsonString<TPrettyJsonPrintPolicy<TCHAR>>();
}

} // ns ITwin::Timeline
