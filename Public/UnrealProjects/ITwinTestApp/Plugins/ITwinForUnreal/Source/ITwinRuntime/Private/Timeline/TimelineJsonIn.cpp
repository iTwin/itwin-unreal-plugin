/*--------------------------------------------------------------------------------------+
|
|     $Source: TimelineJsonIn.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "Timeline.h"
#include "SchedulesConstants.h"

#include <Dom/JsonObject.h>
#include <Dom/JsonValue.h>
#include <Math/UnrealMathUtility.h>
#include <Policies/CondensedJsonPrintPolicy.h>
#include <Policies/PrettyJsonPrintPolicy.h>
#include <Serialization/JsonSerializer.h>

namespace ITwin::Timeline {

size_t MainTimeline::FromJsonString(const FString& JsonString)
{
	TArray<TSharedPtr<FJsonValue>> TimelinesArray;
	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);

	size_t ErrorsFound = 0;
	if (!FJsonSerializer::Deserialize(JsonReader, TimelinesArray))
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to deserialize JSON timeline string"));
		++ErrorsFound;
		return ErrorsFound;
	}

	// Clear existing timelines
	GetContainer().clear();
	ElementsKeyToTimeline.clear();

	for (const TSharedPtr<FJsonValue>& TimelineValue : TimelinesArray)
	{
		if (!TimelineValue.IsValid() || TimelineValue->Type != EJson::Object)
		{
			UE_LOG(LogTemp, Warning, TEXT("Timeline entry is not a valid JSON object"));
			++ErrorsFound;
			continue;
		}

		TSharedPtr<FJsonObject> TimelineObj = TimelineValue->AsObject();
		if (!TimelineObj.IsValid())
		{
			++ErrorsFound;
			continue;
		}

		// Extract elementIds array
		if (!TimelineObj->HasField(TEXT("elementIds")))
		{
			UE_LOG(LogTemp, Warning, TEXT("Timeline object missing 'elementIds' field"));
			++ErrorsFound;
			continue;
		}

		TArray<TSharedPtr<FJsonValue>> ElementIdValues = TimelineObj->GetArrayField(TEXT("elementIds"));
		FElementsGroup Elements;

		for (const TSharedPtr<FJsonValue>& ElemValue : ElementIdValues)
		{
			if (ElemValue.IsValid() && ElemValue->Type == EJson::String)
			{
				FString ElemIdString = ElemValue->AsString();
				ITwinElementID ElemID = ITwin::ParseElementID(*ElemIdString);
				Elements.insert(ElemID);
			}
		}

		if (Elements.empty())
		{
			UE_LOG(LogTemp, Warning, TEXT("Timeline has no valid elements"));
			++ErrorsFound;
			continue;
		}

		std::optional<FIModelElementsKey> Key;
		FString KeyString;
		size_t KeyAsIndex;
		if (TimelineObj->TryGetNumberField(TEXT("animationKey"), KeyAsIndex))
		{
			Key.emplace(KeyAsIndex);
		}
		else if (TimelineObj->TryGetStringField(TEXT("animationKey"), KeyString))
		{
			if (KeyString.Len() >= 36)
			{
				FGuid ElementGuid;
				if (!FGuid::ParseExact(KeyString, EGuidFormats::DigitsWithHyphensLower, ElementGuid))
				{
					UE_LOG(LogTemp, Warning, TEXT("Invalid animation key (length suggested an FGuid)"));
					++ErrorsFound;
					continue;
				}
				Key.emplace(std::move(ElementGuid));
			}
			else
			{
				ITwinElementID ElementID = ITwin::ParseElementID(KeyString);
				if (ElementID == ITwin::NOT_ELEMENT)
				{
					UE_LOG(LogTemp, Warning, TEXT("Invalid animation key (length suggested an Element ID)"));
					++ErrorsFound;
					continue;
				}
				Key.emplace(ElementID);
			}
		}
		else if (TimelineObj->HasField(TEXT("animationKey")))
		{
			UE_LOG(LogTemp, Warning, TEXT("Animation key field found but has an unexpected type"));
			++ErrorsFound;
			continue;
		}
		else if (Elements.size() == 1)
		{
			Key.emplace(*Elements.begin());
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Timeline object missing 'animationKey' field but has more than one Element assigned"));
			++ErrorsFound;
			continue;
		}
		auto NewTimeline = std::make_shared<ElementTimelineEx>(*Key, std::move(Elements));
		// Deserialize the timeline properties from the JSON object
		if (!NewTimeline->FromJson(TimelineObj))
		{
			UE_LOG(LogTemp, Warning, TEXT("Error while parsing the timeline properties"));
			++ErrorsFound;
			continue;
		}
		// Add the timeline to the container
		ElementsKeyToTimeline[*Key] = (int)GetContainer().size();
		AddTimeline(NewTimeline);
	}
	return ErrorsFound;
}

bool FromJsonValue(double Time, EInterpolation Interp, TSharedPtr<FJsonValue> const& Value,
				   PropertyEntry<PVisibility>& Entry)
{
	float Alpha;
	if (!Value->TryGetNumber(Alpha))
		return false;
	if (std::abs(Alpha - S4D_FLOAT_ALPHA_DISABLED) < 1e-6) // special value, enforce exact value
		Alpha = S4D_FLOAT_ALPHA_DISABLED;
	Entry.Time = Time;
	Entry.Interpolation = Interp;
	Entry.Value = Alpha;
	return true;
}

bool FromJsonValue(double Time, EInterpolation Interp, TSharedPtr<FJsonValue> const& Value,
				   PropertyEntry<PColor>& Entry)
{
	TArray<TSharedPtr<FJsonValue>> const* pColorArray;
	if (!Value->TryGetArray(pColorArray) || pColorArray->Num() != 4)
		return false;
	auto const& ColorArray = *pColorArray;
	bool bHasColor = ColorArray[0]->AsBool();
	Entry.Time = Time;
	Entry.Interpolation = Interp;
	Entry.bHasColor = bHasColor ? ITwin::Flag::Present : ITwin::Flag::Absent;
	Entry.Value = bHasColor
		? FVector(ColorArray[1]->AsNumber(), ColorArray[2]->AsNumber(), ColorArray[3]->AsNumber())
		: FVector::ZeroVector;
	return true;
}

bool FromJsonValue(double Time, EInterpolation Interp, TSharedPtr<FJsonValue> const& Value,
				   PropertyEntry<PClippingPlane>& Entry)
{
	TArray<TSharedPtr<FJsonValue>> const* pPlaneArray;
	if (!Value->TryGetArray(pPlaneArray) || pPlaneArray->Num() != 5)
		return false;
	auto const& PlaneArray = *pPlaneArray;
	FString GrowthStatusStr = PlaneArray[0]->AsString();
	auto OptGrowthStatus = ParseGrowthStatus(GrowthStatusStr);
	if (!OptGrowthStatus)
		return false;
	FVector3f const PlaneOrientation(PlaneArray[1]->AsNumber(), PlaneArray[2]->AsNumber(), PlaneArray[3]->AsNumber());
	Entry.Time = Time;
	Entry.Interpolation = Interp;
	Entry.DefrdPlaneEq = FDeferredPlaneEquation{
		.PlaneOrientation = PlaneOrientation,
		.TransformKeyframe = nullptr,
		.PlaneW = static_cast<float>(PlaneArray[4]->AsNumber()),
		.GrowthStatus = *OptGrowthStatus
	};
	return true;
}

bool FromJsonValue(double Time, EInterpolation Interp, TSharedPtr<FJsonValue> const& Value,
				   PropertyEntry<PTransform>& Entry)
{
	TSharedPtr<FJsonObject> const* pTransformData;
	FString Untransfo;
	if (Value->TryGetString(Untransfo) && Untransfo == TEXT("Untransformed"))
	{
		Entry.bIsTransformed = ITwin::Flag::Absent;
		Entry.Position = FVector::ZeroVector;
		Entry.Rotation = FQuat::Identity;
		Entry.Scale = FVector::OneVector;
	}
	else if (Value->TryGetObject(pTransformData))
	{
		FJsonObject* TransformData = pTransformData->Get();
		FVector Position = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity;
		FVector Scale = FVector::OneVector;
		FDeferredAnchor Anchor;
		if (TransformData->HasField(TEXT("translation")))
		{
			TArray<TSharedPtr<FJsonValue>> TranslationArray = TransformData->GetArrayField(TEXT("translation"));
			if (TranslationArray.Num() == 3)
			{
				Position = FVector(
					TranslationArray[0]->AsNumber(),
					TranslationArray[1]->AsNumber(),
					TranslationArray[2]->AsNumber()
				);
			}
		}
		if (TransformData->HasField(TEXT("rotationAxis"))
			&& TransformData->HasField(TEXT("rotationAngleDegrees")))
		{
			TArray<TSharedPtr<FJsonValue>> AxisArray = TransformData->GetArrayField(TEXT("rotationAxis"));
			if (AxisArray.Num() == 3)
			{
				FVector Axis(AxisArray[0]->AsNumber(), AxisArray[1]->AsNumber(), AxisArray[2]->AsNumber());
				double AngleDegrees = TransformData->GetNumberField(TEXT("rotationAngleDegrees"));
				double AngleRadians = FMath::DegreesToRadians(AngleDegrees);
				Rotation = FQuat(Axis, AngleRadians);
			}
		}
		if (TransformData->HasField(TEXT("scale")))
		{
			TArray<TSharedPtr<FJsonValue>> ScaleArray = TransformData->GetArrayField(TEXT("scale"));
			if (ScaleArray.Num() != 3)
				return false;
			Scale = FVector(ScaleArray[0]->AsNumber(), ScaleArray[1]->AsNumber(), ScaleArray[2]->AsNumber());
		}
		if (TransformData->HasField(TEXT("anchor")))
		{
			TSharedPtr<FJsonValue> AnchorField = TransformData->TryGetField(TEXT("anchor"));
			if (AnchorField->Type == EJson::String)
			{
				auto OptAnchorPoint = ParseAnchorPoint(AnchorField->AsString());
				if (!OptAnchorPoint)
					return false;
				Anchor.AnchorPoint = *OptAnchorPoint;
				Anchor.bDeferred = true;
			}
			else if (AnchorField->Type == EJson::Array)
			{
				TArray<TSharedPtr<FJsonValue>> AnchorArray = AnchorField->AsArray();
				if (AnchorArray.Num() == 3)
				{
					Anchor.Offset = FVector(
						AnchorArray[0]->AsNumber(), AnchorArray[1]->AsNumber(), AnchorArray[2]->AsNumber());
				}
			}
		}
		Entry.bIsTransformed = ITwin::Flag::Present;
		Entry.Position = Position;
		Entry.Rotation = Rotation;
		Entry.Scale = Scale;
		Entry.DefrdAnchor = Anchor;
	}
	else
	{
		return false;
	}
	Entry.Time = Time;
	Entry.Interpolation = Interp;
	return true;
}

} // ns ITwin::Timeline
