/*--------------------------------------------------------------------------------------+
|
|     $Source: SchedulesStructsJson.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "SchedulesGeneration.h"
#include "SchedulesStructs.h"

#include <Dom/JsonObject.h>
#include <Dom/JsonValue.h>
#include <HAL/PlatformFileManager.h>
#include <Misc/FileHelper.h>
#include <Misc/Paths.h>
#include <Policies/CondensedJsonPrintPolicy.h>
#include <Policies/PrettyJsonPrintPolicy.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>

#include <cfloat>
#include <set>

namespace
{
	FString ToJsonNumberString(double Value)
	{
		auto Str = FString::Printf(TEXT("%.*f"), DBL_DECIMAL_DIG, Value);
		if (Str.Contains(TEXT(".")))
		{
			int32 End = Str.Len();
			while (End > 0 && TEXT('0') == (*Str)[End - 1])
				--End;
			Str.RemoveAt(End, Str.Len() - End);
			if (!Str.IsEmpty() && (*Str)[Str.Len() - 1] == TEXT('.'))
				Str += TEXT('0');
		}
		return Str;
	}

	TSharedPtr<FJsonValue> ToJsonNumberValue(double Value)
	{
		return MakeShared<FJsonValueNumberString>(ToJsonNumberString(Value));
	}

	void SetNumberStringField(TSharedPtr<FJsonObject> const& Obj, const TCHAR* FieldName, double Value)
	{
		Obj->SetField(FieldName, ToJsonNumberValue(Value));
	}

	TSharedPtr<FJsonValue> ToJsonValue(FVector const& Vec)
	{
		return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
				ToJsonNumberValue(Vec.X),
				ToJsonNumberValue(Vec.Y),
				ToJsonNumberValue(Vec.Z)
			});
	}

	bool FromJsonValue(TSharedPtr<FJsonValue> const& JsonValue, FVector& Vec)
	{
		if (!JsonValue.IsValid() || JsonValue->Type != EJson::Array)
			return false;
		auto const& Arr = JsonValue->AsArray();
		if (Arr.Num() != 3)
			return false;
		Vec.X = Arr[0]->AsNumber();
		Vec.Y = Arr[1]->AsNumber();
		Vec.Z = Arr[2]->AsNumber();
		return true;
	}

	TSharedPtr<FJsonValue> ToJsonValue(FQuat const& Quat)
	{
		return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
				ToJsonNumberValue(Quat.X),
				ToJsonNumberValue(Quat.Y),
				ToJsonNumberValue(Quat.Z),
				ToJsonNumberValue(Quat.W)
			});
	}

	bool FromJsonValue(TSharedPtr<FJsonValue> const& JsonValue, FQuat& Quat)
	{
		if (!JsonValue.IsValid() || JsonValue->Type != EJson::Array)
			return false;
		auto const& Arr = JsonValue->AsArray();
		if (Arr.Num() != 4)
			return false;
		Quat.X = Arr[0]->AsNumber();
		Quat.Y = Arr[1]->AsNumber();
		Quat.Z = Arr[2]->AsNumber();
		Quat.W = Arr[3]->AsNumber();
		return true;
	}

	TSharedPtr<FJsonObject> ToJsonObject(FITwinScheduleStats const& Stats)
	{
		auto Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("animation3dPathAssignmentCount"), (double)Stats.Animation3dPathAssignmentCount);
		Obj->SetNumberField(TEXT("animation3dPathCount"), (double)Stats.Animation3dPathCount);
		Obj->SetNumberField(TEXT("animation3dPathKeyframeCount"), (double)Stats.Animation3dPathKeyframeCount);
		Obj->SetNumberField(TEXT("animation3dTransformCount"), (double)Stats.Animation3dTransformCount);
		Obj->SetNumberField(TEXT("animationBindingCount"), (double)Stats.AnimationBindingCount);
		Obj->SetNumberField(TEXT("appearanceProfileCount"), (double)Stats.AppearanceProfileCount);
		Obj->SetNumberField(TEXT("taskCount"), (double)Stats.TaskCount);
		return Obj;
	}

	bool FromJsonObject(TSharedPtr<FJsonObject> const& Obj, FITwinScheduleStats& Stats)
	{
		if (!Obj.IsValid())
			return false;

		double Val = 0.;
		if (!Obj->TryGetNumberField(TEXT("animation3dPathAssignmentCount"), Val)) return false;
		Stats.Animation3dPathAssignmentCount = (size_t)Val;
		if (!Obj->TryGetNumberField(TEXT("animation3dPathCount"), Val)) return false;
		Stats.Animation3dPathCount = (size_t)Val;
		if (!Obj->TryGetNumberField(TEXT("animation3dPathKeyframeCount"), Val)) return false;
		Stats.Animation3dPathKeyframeCount = (size_t)Val;
		if (!Obj->TryGetNumberField(TEXT("animation3dTransformCount"), Val)) return false;
		Stats.Animation3dTransformCount = (size_t)Val;
		if (!Obj->TryGetNumberField(TEXT("animationBindingCount"), Val)) return false;
		Stats.AnimationBindingCount = (size_t)Val;
		if (!Obj->TryGetNumberField(TEXT("appearanceProfileCount"), Val)) return false;
		Stats.AppearanceProfileCount = (size_t)Val;
		if (!Obj->TryGetNumberField(TEXT("taskCount"), Val)) return false;
		Stats.TaskCount = (size_t)Val;
		return true;
	}

	TSharedPtr<FJsonObject> ToJsonObject(FSimpleAppearance const& Appearance)
	{
		auto Obj = MakeShared<FJsonObject>();
		if (Appearance.bUseOriginalColor)
			Obj->SetBoolField(TEXT("useOriginalColor"), Appearance.bUseOriginalColor);
		else
			Obj->SetField(TEXT("color"), ToJsonValue(Appearance.Color));
		if (Appearance.bUseOriginalAlpha)
			Obj->SetBoolField(TEXT("useOriginalAlpha"), Appearance.bUseOriginalAlpha);
		else
			Obj->SetNumberField(TEXT("alpha"), Appearance.Alpha);
		return Obj;
	}

	bool FromJsonObject(TSharedPtr<FJsonObject> const& Obj, FSimpleAppearance& Appearance)
	{
		if (!Obj.IsValid())
			return false;
		TSharedPtr<FJsonValue> ColorValue;
		// useOriginalColor can only be true or absent (see ToJsonObject)
		bool BoolVal = false;// Appearance.bUseOriginalColor being a bitfield cannot be passed to TryGetBoolField
		if (!Obj->TryGetBoolField(TEXT("useOriginalColor"), BoolVal)
			&& !(ColorValue = Obj->TryGetField(TEXT("color"))))
		{
			return false;
		}
		Appearance.bUseOriginalColor = BoolVal;
		if (!Appearance.bUseOriginalColor && !FromJsonValue(ColorValue, Appearance.Color))
			return false;
		// useOriginalAlpha can only be true or absent (see ToJsonObject)
		BoolVal = false;
		if (!Obj->TryGetBoolField(TEXT("useOriginalAlpha"), BoolVal)
			&& !Obj->TryGetNumberField(TEXT("alpha"), Appearance.Alpha))
		{
			return false;
		}
		Appearance.bUseOriginalAlpha = BoolVal;
		return true;
	}

	TSharedPtr<FJsonObject> ToJsonObject(FActiveAppearance const& Appearance)
	{
		auto Obj = MakeShared<FJsonObject>();
		Obj->SetObjectField(TEXT("base"), ToJsonObject(Appearance.Base));
		Obj->SetField(TEXT("growthDirectionCustom"), ToJsonValue(Appearance.GrowthDirectionCustom));
		if (!Appearance.Base.bUseOriginalAlpha && Appearance.FinishAlpha != Appearance.Base.Alpha) // reduce verbosity
			Obj->SetNumberField(TEXT("finishAlpha"), Appearance.FinishAlpha);
		Obj->SetNumberField(TEXT("growthSimulationMode"), (double)Appearance.GrowthSimulationMode);
		// Not yet supported:
		//Obj->SetBoolField(TEXT("growthSimulationBasedOnPercentComplete"),
		//				  Appearance.bGrowthSimulationBasedOnPercentComplete);
		//Obj->SetBoolField(TEXT("growthSimulationPauseDuringNonWorkingTime"),
		//				  Appearance.bGrowthSimulationPauseDuringNonWorkingTime);
		if (Appearance.bInvertGrowth)
			Obj->SetBoolField(TEXT("invertGrowth"), Appearance.bInvertGrowth);
		return Obj;
	}

	bool FromJsonObject(TSharedPtr<FJsonObject> const& Obj, FActiveAppearance& Appearance)
	{
		if (!Obj.IsValid())
			return false;
		if (!FromJsonObject(Obj->GetObjectField(TEXT("base")), Appearance.Base))
			return false;
		if (!FromJsonValue(Obj->TryGetField(TEXT("growthDirectionCustom")), Appearance.GrowthDirectionCustom))
			return false;
		double NumVal = 0.;
		if (!Appearance.Base.bUseOriginalAlpha)
			if (Obj->TryGetNumberField(TEXT("finishAlpha"), NumVal))
				Appearance.FinishAlpha = (float)NumVal;
			else
				Appearance.FinishAlpha = Appearance.Base.Alpha;
		if (!Obj->TryGetNumberField(TEXT("growthSimulationMode"), NumVal))
			return false;
		Appearance.GrowthSimulationMode = (EGrowthSimulationMode)(uint8)NumVal;

		bool BoolVal = false;
		// Not yet supported:
		//if (!Obj->TryGetBoolField(TEXT("growthSimulationBasedOnPercentComplete"), BoolVal))
		//	return false;
		//Appearance.bGrowthSimulationBasedOnPercentComplete = BoolVal;
		//BoolVal = false;
		//if (!Obj->TryGetBoolField(TEXT("growthSimulationPauseDuringNonWorkingTime"), BoolVal))
		//	return false;
		//Appearance.bGrowthSimulationPauseDuringNonWorkingTime = BoolVal;
		//BoolVal = false; // reset! since the last one below is optional
		Obj->TryGetBoolField(TEXT("invertGrowth"), BoolVal); // Appearance.bInvertGrowth is a bitfield...
		Appearance.bInvertGrowth = BoolVal;
		return true;
	}

	TSharedPtr<FJsonObject> ToJsonObject(FTransform const& Transform)
	{
		auto Obj = MakeShared<FJsonObject>();
		// optional, to reduce verbosity: Equals uses a tolerance, particularly useful for Scale which is never
		// set but never exactly 1. either due do conversions between the various representations...
		if (!Transform.GetTranslation().Equals(FVector::ZeroVector))
			Obj->SetField(TEXT("translation"), ToJsonValue(Transform.GetTranslation()));
		if (!Transform.GetRotation().Equals(FQuat::Identity))
			Obj->SetField(TEXT("rotation"), ToJsonValue(Transform.GetRotation()));
		if (!Transform.GetScale3D().Equals(FVector(1, 1, 1)))
			Obj->SetField(TEXT("scale"), ToJsonValue(Transform.GetScale3D()));
		return Obj;
	}

	bool FromJsonObject(TSharedPtr<FJsonObject> const& Obj, FTransform& Transform)
	{
		if (!Obj.IsValid())
			return false;
		FVector Translation, Scale;
		FQuat Rotation;
		bool const bHasTranslation = FromJsonValue(Obj->TryGetField(TEXT("translation")), Translation);
		bool const bHasRotation = FromJsonValue(Obj->TryGetField(TEXT("rotation")), Rotation);
		bool const bHasScale = FromJsonValue(Obj->TryGetField(TEXT("scale")), Scale);
		if (bHasTranslation && !bHasRotation && !bHasScale)
			Transform = FTransform(Translation);
		else if (bHasRotation && !bHasTranslation && !bHasScale)
			Transform = FTransform(Rotation);
		else
		{
			if (!bHasRotation)
				Rotation = FQuat::Identity;
			if (!bHasTranslation)
				Translation = FVector::ZeroVector;
			Transform = FTransform(Rotation, Translation, bHasScale ? Scale : FVector::OneVector);
		}
		return true;
	}

	TSharedPtr<FJsonObject> ToJsonObject(FPathTransformAssignment const& Assignment)
	{
		auto Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("animation3DPathId"), Assignment.Animation3DPathId);

		auto AnchorObj = MakeShared<FJsonObject>();
		if (std::holds_alternative<ITwin::Timeline::EAnchorPoint>(Assignment.TransformAnchor))
		{
			AnchorObj->SetStringField(TEXT("type"), TEXT("anchorPoint"));
			AnchorObj->SetStringField(TEXT("value"),
				ITwin::Timeline::GetAnchorPointString(std::get<ITwin::Timeline::EAnchorPoint>(
					Assignment.TransformAnchor)));
		}
		else
		{
			AnchorObj->SetStringField(TEXT("type"), TEXT("offset"));
			AnchorObj->SetField(TEXT("value"), ToJsonValue(std::get<FVector>(Assignment.TransformAnchor)));
		}
		Obj->SetObjectField(TEXT("transformAnchor"), AnchorObj);

		if (Assignment.b3DPathReverseDirection)
			Obj->SetBoolField(TEXT("3DPathReverseDirection"), Assignment.b3DPathReverseDirection);
		if (Assignment.MotionStart != 0.)
			SetNumberStringField(Obj, TEXT("motionStart"), Assignment.MotionStart);
		if (Assignment.MotionEnd != 1.)
			SetNumberStringField(Obj, TEXT("motionEnd"), Assignment.MotionEnd);
		return Obj;
	}

	bool FromJsonObject(TSharedPtr<FJsonObject> const& Obj, FPathTransformAssignment& Assignment)
	{
		if (!Obj.IsValid())
			return false;
		if (!Obj->TryGetStringField(TEXT("animation3DPathId"), Assignment.Animation3DPathId))
			return false;

		auto AnchorObj = Obj->GetObjectField(TEXT("transformAnchor"));
		FString AnchorType;
		if (!AnchorObj->TryGetStringField(TEXT("type"), AnchorType))
			return false;
		if (AnchorType == TEXT("anchorPoint"))
		{
			FString AnchorString;
			if (!AnchorObj->TryGetStringField(TEXT("value"), AnchorString))
				return false;
			auto AnchorPoint = ITwin::Timeline::ParseAnchorPoint(AnchorString);
			if (!AnchorPoint)
				return false;
			Assignment.TransformAnchor = *AnchorPoint;
		}
		else
		{
			FVector Offset;
			if (!FromJsonValue(AnchorObj->TryGetField(TEXT("value")), Offset))
				return false;
			Assignment.TransformAnchor = Offset;
		}
		// Optional fields with default values, so that missing fields in the JSON are not an error
		Obj->TryGetBoolField(TEXT("3DPathReverseDirection"), Assignment.b3DPathReverseDirection);
		Obj->TryGetNumberField(TEXT("motionStart"), Assignment.MotionStart);
		Obj->TryGetNumberField(TEXT("motionEnd"), Assignment.MotionEnd);
		return true;
	}

	TSharedPtr<FJsonObject> ToJsonObject(FTransformKey const& Keyframe)
	{
		auto Obj = MakeShared<FJsonObject>();
		Obj->SetObjectField(TEXT("transform"), ToJsonObject(Keyframe.Transform));
		SetNumberStringField(Obj, TEXT("relativeTime"), Keyframe.RelativeTime);
		return Obj;
	}

	bool FromJsonObject(TSharedPtr<FJsonObject> const& Obj, FTransformKey& Keyframe)
	{
		if (!Obj.IsValid())
			return false;
		if (!FromJsonObject(Obj->GetObjectField(TEXT("transform")), Keyframe.Transform))
			return false;
		return Obj->TryGetNumberField(TEXT("relativeTime"), Keyframe.RelativeTime);
	}
}

TSharedPtr<FJsonObject> FITwinSchedule::ToJson() const
{
	auto Root = MakeShared<FJsonObject>();

	Root->SetStringField(TEXT("id"), Id);
	Root->SetStringField(TEXT("name"), Name);
	Root->SetNumberField(TEXT("jsonCacheVersion"), JsonCacheVersion);
	Root->SetStringField(TEXT("generation"), ::ToString(Generation));

	Root->SetStringField(TEXT("bindingsDeltaToken"), BindingsDeltaToken);
	Root->SetStringField(TEXT("appearanceProfilesDeltaToken"), AppearanceProfilesDeltaToken);
	Root->SetStringField(TEXT("tasksDeltaToken"), TasksDeltaToken);
	Root->SetStringField(TEXT("staticTransfosDeltaToken"), StaticTransfosDeltaToken);
	Root->SetStringField(TEXT("anim3DPathsAssignmentsDeltaToken"), Anim3DPathsAssignmentsDeltaToken);
	Root->SetStringField(TEXT("anim3DPathsDeltaToken"), Anim3DPathsDeltaToken);
	Root->SetStringField(TEXT("anim3DPathKeyframesDeltaToken"), Anim3DPathKeyframesDeltaToken);

	if (StatisticsTotal)
		Root->SetObjectField(TEXT("statisticsTotal"), ToJsonObject(*StatisticsTotal));

	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Reserve(AnimationBindings.size());
		for (auto const& Binding : AnimationBindings)
		{
			if (Binding.bDeleted)
				continue;
			auto Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("taskId"), Binding.TaskId);
			Obj->SetStringField(TEXT("appearanceProfileId"), Binding.AppearanceProfileId);
			Obj->SetStringField(TEXT("staticTransfoAssignmentId"), Binding.StaticTransfoAssignmentId);
			Obj->SetStringField(TEXT("pathTransfoAssignmentId"), Binding.PathTransfoAssignmentId);

			auto AnimatedObj = MakeShared<FJsonObject>();
			std::visit([&AnimatedObj](auto const& Value)
				{
					using T = std::decay_t<decltype(Value)>;
					if constexpr (std::is_same_v<T, ITwinElementID>)
					{
						AnimatedObj->SetStringField(TEXT("type"), TEXT("element"));
						AnimatedObj->SetStringField(TEXT("value"), ITwin::ToString(Value));
					}
					else if constexpr (std::is_same_v<T, FGuid>)
					{
						AnimatedObj->SetStringField(TEXT("type"), TEXT("guid"));
						AnimatedObj->SetStringField(TEXT("value"),
							Value.ToString(EGuidFormats::DigitsWithHyphensLower));
					}
					else if constexpr (std::is_same_v<T, FString>)
					{
						AnimatedObj->SetStringField(TEXT("type"), TEXT("group"));
						AnimatedObj->SetStringField(TEXT("value"), Value);
					}
					else static_assert(always_false_v<T>, "non-exhaustive visitor!");
				},
				Binding.AnimatedEntities);
			Obj->SetObjectField(TEXT("animatedEntities"), AnimatedObj);
			Arr.Add(MakeShared<FJsonValueObject>(Obj));
		}
		Root->SetArrayField(TEXT("animationBindings"), Arr);
	}

	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Reserve(Tasks.size());
		for (auto const& Task : Tasks)
		{
			if (Task.bDeleted)
				continue;
			auto Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("id"), Task.Id);
			Obj->SetStringField(TEXT("name"), Task.Name);
			SetNumberStringField(Obj, TEXT("start"), Task.TimeRange.first);
			SetNumberStringField(Obj, TEXT("finish"), Task.TimeRange.second);
			Arr.Add(MakeShared<FJsonValueObject>(Obj));
		}
		Root->SetArrayField(TEXT("tasks"), Arr);
	}

	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Reserve(AppearanceProfiles.size());
		for (size_t Index = 0; Index < AppearanceProfiles.size(); ++Index)
		{
			auto const& Profile = AppearanceProfiles[Index];
			if (Profile.bDeleted)
				continue;
			auto Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("id"), Profile.Id);
			Obj->SetNumberField(TEXT("profileType"), (double)Profile.ProfileType);
			Obj->SetObjectField(TEXT("startAppearance"), ToJsonObject(Profile.StartAppearance));
			Obj->SetObjectField(TEXT("activeAppearance"), ToJsonObject(Profile.ActiveAppearance));
			Obj->SetObjectField(TEXT("finishAppearance"), ToJsonObject(Profile.FinishAppearance));
			Arr.Add(MakeShared<FJsonValueObject>(Obj));
		}
		Root->SetArrayField(TEXT("appearanceProfiles"), Arr);
	}

	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Reserve(StaticTransfoAssignments.size());
		for (size_t Index = 0; Index < StaticTransfoAssignments.size(); ++Index)
		{
			auto const& Assignment = StaticTransfoAssignments[Index];
			if (Assignment.bDeleted)
				continue;
			auto Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("id"), Assignment.Id);
			Obj->SetObjectField(TEXT("transform"), ToJsonObject(Assignment.Transform));
			Arr.Add(MakeShared<FJsonValueObject>(Obj));
		}
		Root->SetArrayField(TEXT("staticTransfoAssignments"), Arr);
	}

	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Reserve(PathTransfoAssignments.size());
		for (size_t Index = 0; Index < PathTransfoAssignments.size(); ++Index)
		{
			auto const& Assignment = PathTransfoAssignments[Index];
			if (Assignment.bDeleted)
				continue;
			auto Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("id"), Assignment.Id);
			Obj->SetObjectField(TEXT("pathAssignment"), ToJsonObject(Assignment));
			Arr.Add(MakeShared<FJsonValueObject>(Obj));
		}
		Root->SetArrayField(TEXT("pathTransfoAssignments"), Arr);
	}

	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Reserve(Animation3DPaths.size());
		for (auto const& Path : Animation3DPaths)
		{
			if (Path.bDeleted)
				continue;
			auto Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("id"), Path.Id);
			Obj->SetStringField(TEXT("name"), Path.Name);
			Obj->SetField(TEXT("color"), ToJsonValue(Path.Color));

			TArray<TSharedPtr<FJsonValue>> Keyframes;
			Keyframes.Reserve(Path.Keyframes.size());
			for (auto const& Keyframe : Path.Keyframes)
			{
				if (Keyframe.bDeleted)
					continue;
				Keyframes.Add(MakeShared<FJsonValueObject>(ToJsonObject(Keyframe)));
			}
			Obj->SetArrayField(TEXT("keyframes"), Keyframes);

			Arr.Add(MakeShared<FJsonValueObject>(Obj));
		}
		Root->SetArrayField(TEXT("animation3DPaths"), Arr);
	}

	{
		TArray<TSharedPtr<FJsonValue>> Groups;
		Groups.Reserve(ElemIDGroups.size());
		std::set<size_t> GroupsWritten;
		for (auto const& Binding : AnimationBindings)
		{
			if (!std::holds_alternative<FString>(Binding.AnimatedEntities))
				continue;
			if (GroupsWritten.contains(Binding.GroupInVec))
				continue;
			GroupsWritten.insert(Binding.GroupInVec);
			auto GroupObj = MakeShared<FJsonObject>();
			GroupObj->SetStringField(TEXT("id"), std::get<FString>(Binding.AnimatedEntities));
			TArray<TSharedPtr<FJsonValue>> Items;
			if (Generation == EITwinSchedulesGeneration::Legacy
				// Bypass the Generation test to allow pseudo-NextGen test data generated from a Legacy schedule's
				// requests cache to work
				|| FedGUIDGroups.empty())
			{
				Items.Reserve(ElemIDGroups[Binding.GroupInVec].size());
				for (auto const& ElemID : ElemIDGroups[Binding.GroupInVec])
					Items.Add(MakeShared<FJsonValueString>(ITwin::ToString(ElemID)));
			}
			else
			{
				Items.Reserve(FedGUIDGroups[Binding.GroupInVec].size());
				for (auto const& Guid : FedGUIDGroups[Binding.GroupInVec])
					Items.Add(MakeShared<FJsonValueString>(Guid.ToString(EGuidFormats::DigitsWithHyphensLower)));
			}
			GroupObj->SetArrayField(TEXT("items"), Items);
			Groups.Add(MakeShared<FJsonValueObject>(GroupObj));
		}
		if (Generation == EITwinSchedulesGeneration::Legacy || FedGUIDGroups.empty()) // see comment above
			Root->SetArrayField(TEXT("elemIDGroups"), Groups);
		else
			Root->SetArrayField(TEXT("fedGUIDGroups"), Groups);
	}

	return Root;
}

bool FITwinSchedule::FromJson(TSharedPtr<FJsonObject> const& Root)
{
	if (!Root.IsValid())
		return false;

	if (!Root->TryGetStringField(TEXT("id"), Id))
		return false;
	if (!Root->TryGetStringField(TEXT("name"), Name))
		return false;
	if (!Root->TryGetNumberField(TEXT("jsonCacheVersion"), JsonCacheVersion))
		return false;

	FString GenerationStr;
	if (!Root->TryGetStringField(TEXT("generation"), GenerationStr) || !FromString(GenerationStr, Generation))
		return false;

	Root->TryGetStringField(TEXT("bindingsDeltaToken"), BindingsDeltaToken);
	Root->TryGetStringField(TEXT("appearanceProfilesDeltaToken"), AppearanceProfilesDeltaToken);
	Root->TryGetStringField(TEXT("tasksDeltaToken"), TasksDeltaToken);
	Root->TryGetStringField(TEXT("staticTransfosDeltaToken"), StaticTransfosDeltaToken);
	Root->TryGetStringField(TEXT("anim3DPathsAssignmentsDeltaToken"), Anim3DPathsAssignmentsDeltaToken);
	Root->TryGetStringField(TEXT("anim3DPathsDeltaToken"), Anim3DPathsDeltaToken);
	Root->TryGetStringField(TEXT("anim3DPathKeyframesDeltaToken"), Anim3DPathKeyframesDeltaToken);

	StatisticsTotal.reset();
	if (Root->HasTypedField<EJson::Object>(TEXT("statisticsTotal")))
	{
		FITwinScheduleStats Stats;
		if (!FromJsonObject(Root->GetObjectField(TEXT("statisticsTotal")), Stats))
			return false;
		StatisticsTotal.emplace(Stats);
	}

	AnimationBindings.clear();
	Tasks.clear();
	AppearanceProfiles.clear();
	StaticTransfoAssignments.clear();
	PathTransfoAssignments.clear();
	Animation3DPaths.clear();
	ElemIDGroups.clear();
	FedGUIDGroups.clear();

	TArray<TSharedPtr<FJsonValue>> const* Arr = nullptr;

	if (Root->TryGetArrayField(TEXT("tasks"), Arr) && Arr)
	{
		Tasks.reserve(Arr->Num());
		for (auto const& Value : *Arr)
		{
			auto Obj = Value->AsObject();
			FScheduleTask Task{ FString{}, EDeletedProp(false) };
			if (!Obj.IsValid())
				return false;
			if (!Obj->TryGetStringField(TEXT("id"), Task.Id))
				return false;
			if (!Obj->TryGetStringField(TEXT("name"), Task.Name))
				return false;
			if (!Obj->TryGetNumberField(TEXT("start"), Task.TimeRange.first))
				return false;
			if (!Obj->TryGetNumberField(TEXT("finish"), Task.TimeRange.second))
				return false;
			Tasks.emplace_back(std::move(Task));
		}
	}

	if (Root->TryGetArrayField(TEXT("appearanceProfiles"), Arr) && Arr)
	{
		AppearanceProfiles.reserve(Arr->Num());
		for (auto const& Value : *Arr)
		{
			auto Obj = Value->AsObject();
			FAppearanceProfile Profile{ FString{}, EDeletedProp(false) };
			if (!Obj.IsValid())
				return false;
			Obj->TryGetStringField(TEXT("id"), Profile.Id);
			double NumVal = 0.;
			if (!Obj->TryGetNumberField(TEXT("profileType"), NumVal))
				return false;
			Profile.ProfileType = (EProfileAction)(uint8)NumVal;
			if (!FromJsonObject(Obj->GetObjectField(TEXT("startAppearance")), Profile.StartAppearance))
				return false;
			if (!FromJsonObject(Obj->GetObjectField(TEXT("activeAppearance")), Profile.ActiveAppearance))
				return false;
			if (!FromJsonObject(Obj->GetObjectField(TEXT("finishAppearance")), Profile.FinishAppearance))
				return false;
			AppearanceProfiles.emplace_back(std::move(Profile));
		}
	}

	if (Root->TryGetArrayField(TEXT("staticTransfoAssignments"), Arr) && Arr)
	{
		StaticTransfoAssignments.reserve(Arr->Num());
		for (auto const& Value : *Arr)
		{
			auto Obj = Value->AsObject();
			if (!Obj.IsValid())
				return false;
			FStaticTransformAssignment Assignment{ FString{}, EDeletedProp(false) };
			Obj->TryGetStringField(TEXT("id"), Assignment.Id);
			if (!FromJsonObject(Obj->GetObjectField(TEXT("transform")), Assignment.Transform))
				return false;
			StaticTransfoAssignments.emplace_back(std::move(Assignment));
		}
	}

	if (Root->TryGetArrayField(TEXT("pathTransfoAssignments"), Arr) && Arr)
	{
		PathTransfoAssignments.reserve(Arr->Num());
		for (auto const& Value : *Arr)
		{
			auto Obj = Value->AsObject();
			if (!Obj.IsValid())
				return false;
			FPathTransformAssignment Assignment{ FString{}, EDeletedProp(false) };
			Obj->TryGetStringField(TEXT("id"), Assignment.Id);
			if (!FromJsonObject(Obj->GetObjectField(TEXT("pathAssignment")), Assignment))
				return false;
			PathTransfoAssignments.emplace_back(std::move(Assignment));
		}
	}

	if (Root->TryGetArrayField(TEXT("animation3DPaths"), Arr) && Arr)
	{
		Animation3DPaths.reserve(Arr->Num());
		for (auto const& Value : *Arr)
		{
			auto Obj = Value->AsObject();
			FAnimation3DPath Path{ FString{}, EDeletedProp(false) };
			if (!Obj.IsValid())
				return false;
			if (!Obj->TryGetStringField(TEXT("id"), Path.Id))
				return false;
			if (!Obj->TryGetStringField(TEXT("name"), Path.Name))
				return false;
			if (!FromJsonValue(Obj->TryGetField(TEXT("color")), Path.Color))
				return false;

			TArray<TSharedPtr<FJsonValue>> const* Keyframes = nullptr;
			if (Obj->TryGetArrayField(TEXT("keyframes"), Keyframes) && Keyframes)
			{
				Path.Keyframes.reserve(Keyframes->Num());
				for (auto const& KeyframeValue : *Keyframes)
				{
					FTransformKey Keyframe{ FString{}, EDeletedProp(false) };
					if (!FromJsonObject(KeyframeValue->AsObject(), Keyframe))
						return false;
					Path.Keyframes.emplace_back(std::move(Keyframe));
				}
			}
			Animation3DPaths.emplace_back(std::move(Path));
		}
	}

	// Don't test the generation to allow pseudo-NextGen test data generated
	// from a Legacy schedule's requests cache to work
	if (Root->TryGetArrayField(TEXT("elemIDGroups"), Arr) && Arr)
	{
		ElemIDGroups.reserve(Arr->Num());
		for (auto const& Value : *Arr)
		{
			auto Obj = Value->AsObject();
			TArray<TSharedPtr<FJsonValue>> const* Items = nullptr;
			if (!Obj.IsValid() || !Obj->TryGetArrayField(TEXT("items"), Items) || !Items)
				return false;
			FElementsGroup Group;
			for (auto const& Item : *Items)
				Group.insert(ITwin::ParseElementID(Item->AsString()));
			KnownGroups[Obj->GetStringField(TEXT("id"))] = ElemIDGroups.size();
			ElemIDGroups.emplace_back(std::move(Group));
		}
	}
	else if (Generation == EITwinSchedulesGeneration::NextGen
		&& Root->TryGetArrayField(TEXT("fedGUIDGroups"), Arr) && Arr)
	{
		FedGUIDGroups.reserve(Arr->Num());
		for (auto const& Value : *Arr)
		{
			auto Obj = Value->AsObject();
			TArray<TSharedPtr<FJsonValue>> const* Items = nullptr;
			if (!Obj.IsValid() || !Obj->TryGetArrayField(TEXT("items"), Items) || !Items)
				return false;
			std::unordered_set<FGuid> Group;
			for (auto const& Item : *Items)
			{
				FGuid Guid;
				if (!FGuid::ParseExact(Item->AsString(), EGuidFormats::DigitsWithHyphensLower, Guid))
					return false;
				Group.insert(Guid);
			}
			KnownGroups[Obj->GetStringField(TEXT("id"))] = FedGUIDGroups.size();
			FedGUIDGroups.emplace_back(std::move(Group));
		}
	}

	if (Root->TryGetArrayField(TEXT("animationBindings"), Arr) && Arr)
	{
		AnimationBindings.reserve(Arr->Num());
		for (auto const& Value : *Arr)
		{
			auto Obj = Value->AsObject();
			FAnimationBinding Binding{ FString{}, EDeletedProp(false) };
			if (!Obj.IsValid())
				return false;

			if (!Obj->TryGetStringField(TEXT("taskId"), Binding.TaskId))
				return false;
			if (!Obj->TryGetStringField(TEXT("appearanceProfileId"), Binding.AppearanceProfileId))
				return false;
			Obj->TryGetStringField(TEXT("staticTransfoAssignmentId"), Binding.StaticTransfoAssignmentId);
			Obj->TryGetStringField(TEXT("pathTransfoAssignmentId"), Binding.PathTransfoAssignmentId);

			auto AnimatedObj = Obj->GetObjectField(TEXT("animatedEntities"));
			FString Type, ValueStr;
			if (!AnimatedObj->TryGetStringField(TEXT("type"), Type)
				|| !AnimatedObj->TryGetStringField(TEXT("value"), ValueStr))
			{
				return false;
			}
			if (Type == TEXT("element"))
			{
				Binding.AnimatedEntities = ITwin::ParseElementID(ValueStr);
			}
			else if (Type == TEXT("guid"))
			{
				FGuid Guid;
				if (!FGuid::ParseExact(ValueStr, EGuidFormats::DigitsWithHyphensLower, Guid))
					return false;
				Binding.AnimatedEntities = Guid;
			}
			else if (Type == TEXT("group"))
			{
				Binding.AnimatedEntities = ValueStr;
				auto KnownGroup = KnownGroups.find(ValueStr);
				if (KnownGroups.end() == KnownGroup)
					return false;
				Binding.GroupInVec = KnownGroup->second;
			}
			else
			{
				return false;
			}
			AnimationBindings.emplace_back(std::move(Binding));
		}
	}

	RebuildKnownProperties();
	StatisticsCurrent = StatisticsTotal ? *StatisticsTotal : FITwinScheduleStats{};
	return true;
}

void FITwinSchedule::RebuildKnownProperties()
{
	KnownAnimationBindings.clear();
	KnownTasks.clear();
	KnownAppearanceProfiles.clear();
	KnownStaticTransfoAssignments.clear();
	KnownPathTransfoAssignments.clear();
	KnownAnimation3DPaths.clear();

	for (size_t Index = 0; Index < Tasks.size(); ++Index)
	{
		if (!Tasks[Index].Id.IsEmpty())
			KnownTasks[Tasks[Index].Id] = Index;
	}

	for (size_t Index = 0; Index < AppearanceProfiles.size(); ++Index)
	{
		if (!AppearanceProfiles[Index].Id.IsEmpty())
			KnownAppearanceProfiles[AppearanceProfiles[Index].Id] = Index;
	}

	for (size_t Index = 0; Index < Animation3DPaths.size(); ++Index)
	{
		if (!Animation3DPaths[Index].Id.IsEmpty())
			KnownAnimation3DPaths[Animation3DPaths[Index].Id] = Index;
	}

	for (size_t Index = 0; Index < StaticTransfoAssignments.size(); ++Index)
	{
		auto& Assignment = StaticTransfoAssignments[Index];
		if (!Assignment.Id.IsEmpty())
			KnownStaticTransfoAssignments[Assignment.Id] = Index;
	}

	for (size_t Index = 0; Index < PathTransfoAssignments.size(); ++Index)
	{
		auto& Assignment = PathTransfoAssignments[Index];
		if (!Assignment.Id.IsEmpty())
			KnownPathTransfoAssignments[Assignment.Id] = Index;
		auto FoundPath = KnownAnimation3DPaths.find(Assignment.Animation3DPathId);
		Assignment.Animation3DPathInVec =
			FoundPath == KnownAnimation3DPaths.end() ? ITwin::INVALID_IDX : FoundPath->second;
	}

	for (size_t Index = 0; Index < AnimationBindings.size(); ++Index)
	{
		auto& Binding = AnimationBindings[Index];
		KnownAnimationBindings[Binding] = Index;

		auto FoundTask = KnownTasks.find(Binding.TaskId);
		Binding.TaskInVec = FoundTask == KnownTasks.end() ? ITwin::INVALID_IDX : FoundTask->second;

		auto FoundAppearance = KnownAppearanceProfiles.find(Binding.AppearanceProfileId);
		Binding.AppearanceProfileInVec =
			FoundAppearance == KnownAppearanceProfiles.end() ? ITwin::INVALID_IDX : FoundAppearance->second;

		if (!Binding.StaticTransfoAssignmentId.IsEmpty())
		{
			auto FoundTransfo = KnownStaticTransfoAssignments.find(Binding.StaticTransfoAssignmentId);
			Binding.StaticTransfoAssignmentInVec =
				FoundTransfo == KnownStaticTransfoAssignments.end() ? ITwin::INVALID_IDX : FoundTransfo->second;
		}
		else
		{
			Binding.StaticTransfoAssignmentInVec = ITwin::INVALID_IDX;
		}

		if (!Binding.PathTransfoAssignmentId.IsEmpty())
		{
			auto FoundTransfo = KnownPathTransfoAssignments.find(Binding.PathTransfoAssignmentId);
			Binding.PathTransfoAssignmentInVec =
				FoundTransfo == KnownPathTransfoAssignments.end() ? ITwin::INVALID_IDX : FoundTransfo->second;
		}
		else
		{
			Binding.PathTransfoAssignmentInVec = ITwin::INVALID_IDX;
		}
	}
}

template<typename JsonPrintPolicy>
FString ToJsonString(FITwinSchedule const& Schedule)
{
	FString JsonString;
	auto JsonWriter = TJsonWriterFactory<TCHAR, JsonPrintPolicy>::Create(&JsonString);
	FJsonSerializer::Serialize(Schedule.ToJson().ToSharedRef(), JsonWriter);
	return JsonString;
}

FString FITwinSchedule::ToCondensedJsonString() const
{
	return ::ToJsonString<TCondensedJsonPrintPolicy<TCHAR>>(*this);
}

FString FITwinSchedule::ToPrettyJsonString() const
{
	return ::ToJsonString<TPrettyJsonPrintPolicy<TCHAR>>(*this);
}

bool FITwinSchedule::FromJsonString(FString const& JsonString)
{
	TSharedPtr<FJsonObject> Root;
	auto JsonReader = TJsonReaderFactory<>::Create(JsonString);
	return FJsonSerializer::Deserialize(JsonReader, Root) && FromJson(Root);
}

bool FITwinSchedule::ReadFromJson(FString const& Path, FSchedLock&)
{
	FString JsonString;
	IPlatformFile& FileManager = FPlatformFileManager::Get().GetPlatformFile();
	if (!FileManager.FileExists(*Path) || !FFileHelper::LoadFileToString(JsonString, *Path))
		return false;
	return FromJsonString(JsonString);
}

bool FITwinSchedule::SaveToJson(FString const& Path, bool bPretty, FSchedLock&) const
{
	FString TimelineAsJson = bPretty ? ToPrettyJsonString() : ToCondensedJsonString();
	IPlatformFile& FileManager = FPlatformFileManager::Get().GetPlatformFile();
	if (FileManager.FileExists(*Path))
		FileManager.DeleteFile(*Path);
	return FFileHelper::SaveStringToFile(TimelineAsJson, *Path, FFileHelper::EEncodingOptions::ForceUTF8);
}