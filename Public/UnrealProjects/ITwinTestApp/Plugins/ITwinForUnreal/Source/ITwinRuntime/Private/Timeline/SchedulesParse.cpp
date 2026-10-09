/*--------------------------------------------------------------------------------------+
|
|     $Source: SchedulesParse.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "SchedulesParse.h"
#include "SchedulesJsonMacros.h"

#include <Dom/JsonObject.h>
#include <Policies/CondensedJsonPrintPolicy.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/Tools/Log.h>
#include <Compil/AfterNonUnrealIncludes.h>

constexpr bool s_bDebugNoPartialTransparencies = false;
constexpr bool s_bDebugForcePartialTransparencies = false; // will extract EVERYTHING! SLOW!!

/*static*/
bool FSchedulesParse::ParseScheduleStatistics(TSharedPtr<FJsonObject> const& Reply, FITwinScheduleStats& Tmp,
											  FString const& ScheduleId, FString const& ScheduleName)
{
	auto const& JsonObj = Reply->GetObjectField(TEXT("animationStatistics"));
	JSON_GETNUMBER_OR(JsonObj, "animation3dPathAssignmentCount", Tmp.Animation3dPathAssignmentCount, return false);
	JSON_GETNUMBER_OR(JsonObj, "animation3dPathCount", Tmp.Animation3dPathCount, return false);
	JSON_GETNUMBER_OR(JsonObj, "animation3dPathKeyframeCount", Tmp.Animation3dPathKeyframeCount, return false);
	JSON_GETNUMBER_OR(JsonObj, "animation3dTransformCount", Tmp.Animation3dTransformCount, return false);
	JSON_GETNUMBER_OR(JsonObj, "animationBindingCount", Tmp.AnimationBindingCount, return false);
	JSON_GETNUMBER_OR(JsonObj, "appearanceProfileCount", Tmp.AppearanceProfileCount, return false);
	JSON_GETNUMBER_OR(JsonObj, "taskCount", Tmp.TaskCount, return false);

	// Note: the animation binding count does not match the final "bindings" count displayed, by far.
	// I'm sure there's a reason, probably due to grouping and/or leaf vs. non-leaf Element nodes, etc. ;-)
	BE_LOGI("ITwin4DImp", "Statistics for the schedule Id " << TCHAR_TO_UTF8(*ScheduleId) << " named '"
		<< TCHAR_TO_UTF8(*ScheduleName) << "' = Tasks: " << Tmp.TaskCount
		<< ", Appearance profiles: " << Tmp.AppearanceProfileCount
		<< ", Animation Bindings: " << Tmp.AnimationBindingCount
		<< ", Static transforms: " << Tmp.Animation3dTransformCount
		<< ", 3D Path assignments: " << Tmp.Animation3dPathAssignmentCount
		<< ", 3D Paths: " << Tmp.Animation3dPathCount
		<< ", 3D Paths keyframes: " << Tmp.Animation3dPathKeyframeCount);
	return true;
}

/*static*/
bool FSchedulesParse::ParseAnimationBinding(TSharedPtr<FJsonObject> const& BindingObj, ITwinElementID& ElementID,
											FGuid& ElementGuid, FAnimationBinding& Tmp)
{
	FString AnimElemIdStr;
	JSON_GETSTR_OR(BindingObj, "animatedEntityId", AnimElemIdStr, return false)
	if (AnimElemIdStr.Len() >= 36)
	{
		if (!FGuid::ParseExact(AnimElemIdStr, EGuidFormats::DigitsWithHyphensLower, ElementGuid))
		{
			ensure(false);
			return false;
		}
	}
	else
	{
		ElementID = ITwin::ParseElementID(AnimElemIdStr);
		if (ElementID == ITwin::NOT_ELEMENT)
		{
			ensure(false);
			return false;
		}
	}
	JSON_GETDELETEDORFALSE(BindingObj, Tmp)
	JSON_GETSTR_OR(BindingObj, "taskId", Tmp.TaskId, return false)
	JSON_GETSTR_OR(BindingObj, "appearanceProfileId", Tmp.AppearanceProfileId, return false)
	FString AnimatedEntitiesAsGroup;
	// 4D team confirmed to check resourceGroupId first, then resourceId - both can be present
	if (BindingObj->TryGetStringField(TEXT("resourceGroupId"), AnimatedEntitiesAsGroup))
		Tmp.AnimatedEntities = AnimatedEntitiesAsGroup;
	else if (BindingObj->TryGetStringField(TEXT("resourceId"), AnimatedEntitiesAsGroup))
		Tmp.AnimatedEntities = AnimatedEntitiesAsGroup;
	else if (ElementID == ITwin::NOT_ELEMENT)
		Tmp.AnimatedEntities = ElementGuid;
	else
		Tmp.AnimatedEntities = ElementID;
#if SYNCHRO4D_ENABLE_TRANSFORMATIONS()
	BindingObj->TryGetStringField(TEXT("transformId"), Tmp.StaticTransfoAssignmentId);
	BindingObj->TryGetStringField(TEXT("pathAssignmentId"), Tmp.PathTransfoAssignmentId);
#endif // SYNCHRO4D_ENABLE_TRANSFORMATIONS()
	return true;
}

/*static*/
bool FSchedulesParse::ParseTaskDates(TSharedPtr<FJsonObject> const& JsonObj,
	FDateTime& Start, FDateTime& Finish, FString& StartStr, FString& FinishStr,
	bool const bActualOrPlanned)
{
	if (!JsonObj->TryGetStringField(bActualOrPlanned ? TEXT("actualStart") : TEXT("plannedStart"), StartStr)
		|| StartStr.IsEmpty())
	{
		return false;
	}
	if (!JsonObj->TryGetStringField(bActualOrPlanned ? TEXT("actualFinish") : TEXT("plannedFinish"), FinishStr)
		|| FinishStr.IsEmpty())
	{
		return false;
	}
	return FDateTime::ParseIso8601(*StartStr, Start) && FDateTime::ParseIso8601(*FinishStr, Finish);
}

/*static*/
bool FSchedulesParse::ParseTaskDetails(TSharedPtr<FJsonObject> const& JsonObj,
									   FScheduleTask& Task, FString const& ScheduleId, FLock& Lock)
{
	Task.Name = JsonObj->GetStringField(TEXT("name"));
	auto bDeleted = Task.bDeleted;
	JSON_GETDELETEDORFALSE(JsonObj, Task)
	bool bUpdatedTaskAffects4DAnim = (bDeleted != Task.bDeleted);
	// Using  "Best dates" (Actual if any, Planned otw) like Synchro/Pineapple/DesignReview
	FDateTime Start, Finish, ActualStart, ActualFinish; FString StartStr, FinishStr;
	bool bUsesActualDates = true;
	bool bCouldParseDates =
		FSchedulesParse::ParseTaskDates(JsonObj, ActualStart, ActualFinish, StartStr, FinishStr, bUsesActualDates);
	bool const bHasActualDates = bCouldParseDates; // might be invalid (Start > Finish)
	if (!bCouldParseDates || ActualStart > ActualFinish)
	{
		bUsesActualDates = false;
		bCouldParseDates = FSchedulesParse::ParseTaskDates(JsonObj, Start, Finish, StartStr, FinishStr, bUsesActualDates);
		// either the Planned ones, or the Actual ones if !bCouldParseDates
		if ((bHasActualDates && ActualStart > ActualFinish)
			|| (bCouldParseDates && Start > Finish))
		{
			bCouldParseDates |= bHasActualDates;
			if (bCouldParseDates)
				Start = Finish;
			else
			{
				bUsesActualDates = true;
				Start = Finish = ActualFinish;
			}
			BE_LOGW("ITwin4DImp", "Task " << TCHAR_TO_UTF8(*Task.Id) << " named '"
				<< TCHAR_TO_UTF8(*Task.Name) << "' for schedule Id " << TCHAR_TO_UTF8(*ScheduleId)
				<< " had Start > Finish => assigned Finish to Start so now both are: "
				<< TCHAR_TO_UTF8(*StartStr)
				<< " (" << (bUsesActualDates ? "Actual" : "Planned") << " dates)");
		}
	}
	else
	{
		Start = ActualStart;
		Finish = ActualFinish;
	}
	if (ensure(bCouldParseDates))
	{
		bUpdatedTaskAffects4DAnim |=
			(std::abs(Task.TimeRange.first - ITwin::Time::FromDateTime(Start)) > KEYFRAME_TIME_EPSILON)
			|| (std::abs(Task.TimeRange.second - ITwin::Time::FromDateTime(Finish)) > KEYFRAME_TIME_EPSILON);
		Task.TimeRange.first = ITwin::Time::FromDateTime(Start);
		Task.TimeRange.second = ITwin::Time::FromDateTime(Finish);
		BE_LOGV("ITwin4DImp", "Task " << TCHAR_TO_UTF8(*Task.Id)
			<< " named '" << TCHAR_TO_UTF8(*Task.Name)
			<< "' for schedule Id " << TCHAR_TO_UTF8(*ScheduleId)
			<< " spans " << TCHAR_TO_UTF8(*StartStr)
			<< " to " << TCHAR_TO_UTF8(*FinishStr)
			<< " ('" << (bUsesActualDates ? "Actual" : "Planned") << "' dates)");
	}
	else
	{
		// Don't, it's already the default, and if we are updating the task, let's keep a possibly valid former value
		//Task.TimeRange = ITwin::Time::Undefined();
		BE_LOGE("ITwin4DImp", "Task " << TCHAR_TO_UTF8(*Task.Id) << " named '" << TCHAR_TO_UTF8(*Task.Name)
			<< "' for schedule Id " << TCHAR_TO_UTF8(*ScheduleId) << " has invalid date(s)!");
	}
	return bUpdatedTaskAffects4DAnim;
}

/*static*/
EProfileAction FSchedulesParse::ParseProfileAction(FString const& FromStr)
{
	if (ensure(!FromStr.IsEmpty()))
	{
		switch (FromStr.ToLower()[0])
		{
		case 'i':
			return EProfileAction::Install;
		case 'r':
			return EProfileAction::Remove;
		case 't':
			return EProfileAction::Temporary;
		case 'm':
			return EProfileAction::Maintenance;
		case 'n':
			return EProfileAction::Neutral;
		default:
			ensure(false);
			break;
		}
	}
	return EProfileAction::Neutral;
}

/*static*/
bool FSchedulesParse::ParseAppearanceProfileDetails(TSharedPtr<FJsonObject> const& JsonObj,
													FAppearanceProfile& AppearanceProfile, FLock& Lock)
{
	FAppearanceProfile Parsed;
	JSON_GETDELETEDORFALSE(JsonObj, Parsed)
	//JSON_GETSTR_OR(JsonObj, "id", AppearanceProfile.Id, return false); <= already set by caller
	FString ProfileTypeStr;
	JSON_GETSTR_OR(JsonObj, "action", ProfileTypeStr, return false)
	bool bUpdatedAPAffects4DAnim = false;
	Parsed.ProfileType = FSchedulesParse::ParseProfileAction(ProfileTypeStr);
	bool const bNeedStartAppearance = EProfileAction::Install != Parsed.ProfileType
								   && EProfileAction::Temporary != Parsed.ProfileType;
	bool const bNeedFinishAppearance = EProfileAction::Remove != Parsed.ProfileType
									&& EProfileAction::Temporary != Parsed.ProfileType;
	TSharedPtr<FJsonObject> const *StartObj = nullptr, *ActiveObj, *EndObj = nullptr;
	if (bNeedStartAppearance)
		JSON_GETOBJ_OR(JsonObj, "startAppearance", StartObj, return false)
	JSON_GETOBJ_OR(JsonObj, "activeAppearance", ActiveObj, return false)
	if (bNeedFinishAppearance)
		JSON_GETOBJ_OR(JsonObj, "endAppearance", EndObj, return false)

	if ((bNeedStartAppearance && !FSchedulesParse::ParseSimpleAppearance(Parsed.StartAppearance, false, *StartObj))
		|| !FSchedulesParse::ParseActiveAppearance(Parsed.ActiveAppearance, *ActiveObj)
		|| (bNeedFinishAppearance && !FSchedulesParse::ParseSimpleAppearance(Parsed.FinishAppearance, false, *EndObj)))
	{
		BE_LOGE("ITwin4DImp", "Error reading appearance profile " << TCHAR_TO_UTF8(*AppearanceProfile.Id));
		return false;
	}
	Parsed.Id = AppearanceProfile.Id; // copy, otherwise operator== returns false...
	bool const bAffects4DAnim = !(Parsed == AppearanceProfile);
	AppearanceProfile = std::move(Parsed);
	return bAffects4DAnim;
}

/*static*/
/// Success already 'ensure'd, so no obligation to test the result
bool FSchedulesParse::ParseColorFromHexString(FString const& FromStr, FVector& Color)
{
	if (FromStr.Len() < 6)
	{
		ensure(false);
		return false;
	}
	uint64 const Clr = FCString::Strtoui64(*FromStr.Right(6), nullptr, /*base*/16);
	Color.X = ((Clr & 0xFF0000) >> 16) / 255.;
	Color.Y = ((Clr & 0x00FF00) >> 8) / 255.;
	Color.Z = (Clr & 0x0000FF) / 255.;
	return true;
}

/*static*/
/// Success already 'ensure'd, so no obligation to test the result
bool FSchedulesParse::ParseVector(TSharedPtr<FJsonObject> const& JsonObj, FVector& Out)
{
	JSON_GETNUMBER_OR(JsonObj, "x", Out.X, return false)
	JSON_GETNUMBER_OR(JsonObj, "y", Out.Y, return false)
	JSON_GETNUMBER_OR(JsonObj, "z", Out.Z, return false)
	return true;
}

/*static*/
/// Note: direction of growth kept in iTwin axes convention (and growth never uses relative coordinates).
/// Success already 'ensure'd, so no obligation to test the result
bool FSchedulesParse::ParseGrowthSimulationMode(FString const& FromStr, EGrowthSimulationMode& Mode)
{
	if (ensure(FromStr.Len() >= 2))
	{
		auto const Lower = FromStr.ToLower();
		switch (Lower[0])
		{
		case 'b':
			if (Lower[1] == 'o')
				Mode = EGrowthSimulationMode::Bottom2Top;
			else if (Lower[1] == 'a')
				Mode = EGrowthSimulationMode::Back2Front;
			else
			{
				ensure(false);
				return false;
			}
			break;
		case 't': Mode = EGrowthSimulationMode::Top2Bottom; break;
		case 'l': Mode = EGrowthSimulationMode::Left2Right; break;
		case 'r': Mode = EGrowthSimulationMode::Right2Left; break;
		case 'f': Mode = EGrowthSimulationMode::Front2Back; break;
		case 'c': Mode = EGrowthSimulationMode::Custom; break;
		case 'n': Mode = EGrowthSimulationMode::None; break;
		case 'u': Mode = EGrowthSimulationMode::Unknown; break;
		default: return false;
		}
		return true;
	}
	return false;
}

/*static*/
bool FSchedulesParse::ParseSimpleAppearance(FSimpleAppearance& Appearance,
	bool const bBaseOfActiveAppearance, TSharedPtr<FJsonObject> const& JsonObj)
{
	FString ColorStr;
	// Note: cannot take address (or ref) of bitfield, hence the bools:
	// Note2: init the flags to silence C4701...
	bool OrgCol = true, OrgTransp = true;
	JSON_GETBOOL_OR(JsonObj, "useOriginalColor", OrgCol, return false)
	JSON_GETBOOL_OR(JsonObj, "useOriginalTransparency", OrgTransp, return false)
	OrgTransp &= !s_bDebugForcePartialTransparencies;
	OrgTransp |= s_bDebugNoPartialTransparencies;
	Appearance.bUseOriginalColor = OrgCol;
	Appearance.bUseOriginalAlpha = OrgTransp;
	if (!OrgCol)
	{
		JSON_GETSTR_OR(JsonObj, "color", ColorStr, return false)
			if (!ParseColorFromHexString(ColorStr, Appearance.Color))
				return false;
	}
	if (!OrgTransp)
	{
		if (bBaseOfActiveAppearance)
			JSON_GETNUMBER_OR(JsonObj, "startTransparency", Appearance.Alpha, return false)
		else
			JSON_GETNUMBER_OR(JsonObj, "transparency", Appearance.Alpha, return false)
			if constexpr (s_bDebugForcePartialTransparencies)
				Appearance.Alpha = 0.3f;
			else
				Appearance.Alpha = std::clamp(1.f - Appearance.Alpha / 100.f, 0.f, 1.f);
	}
	return true;
}

/*static*/
bool FSchedulesParse::ParseActiveAppearance(FActiveAppearance& Appearance,
	TSharedPtr<FJsonObject> const& JsonObj)
{
	if (!ParseSimpleAppearance(Appearance.Base, true, JsonObj))
		return false;
	JSON_GETNUMBER_OR(JsonObj, "finishTransparency", Appearance.FinishAlpha, return false)
	if constexpr (s_bDebugNoPartialTransparencies)
		Appearance.FinishAlpha = 1.f; // already the default
	else if constexpr (s_bDebugForcePartialTransparencies)
		Appearance.FinishAlpha = 0.3f;
	else
		Appearance.FinishAlpha = std::clamp(1.f - Appearance.FinishAlpha / 100.f, 0.f, 1.f);
	TSharedPtr<FJsonObject> const* GrowthObj;
	JSON_GETOBJ_OR(JsonObj, "growthSimulation", GrowthObj, return false)
		// Note: cannot take address (or ref) of bitfield, hence the bools:
		// Note2: init the flags to silence C4701...
	bool GroPercent = true, GroPause = true, InvertGro = true;
	JSON_GETBOOL_OR(*GrowthObj, "adjustForTaskPercentComplete", GroPercent, return false)
	JSON_GETBOOL_OR(*GrowthObj, "pauseDuringNonWorkingTime", GroPause, return false)
	JSON_GETBOOL_OR(*GrowthObj, "simulateAsRemove", InvertGro, return false)
	Appearance.bGrowthSimulationBasedOnPercentComplete = GroPercent;
	Appearance.bGrowthSimulationPauseDuringNonWorkingTime = GroPause;
	Appearance.bInvertGrowth = InvertGro;
	FString GrowthModeStr;
	JSON_GETSTR_OR(*GrowthObj, "mode", GrowthModeStr, return false)
	if (!ParseGrowthSimulationMode(GrowthModeStr, Appearance.GrowthSimulationMode))
		return false;
	TSharedPtr<FJsonObject> const* GrowthVecObj;
	JSON_GETOBJ_OR(*GrowthObj, "direction", GrowthVecObj, return false)
	if (!ParseVector(*GrowthVecObj, Appearance.GrowthDirectionCustom)) return false;
	return true;
}

bool FSchedulesParse::ParseStaticTransform(FMatrix& Mat, TSharedPtr<FJsonObject> const& JsonObj)
{
	//JSON_GETDELETEDORFALSE(KeyframeObj, StaticAssignment) <= done in ParseStaticTransfoAssignment
	auto&& TransfoArray = JsonObj->GetArrayField(TEXT("transform"));
	if (!ensure(TransfoArray.Num() == 16))
		return false;
	// From https://rodolphe-vaillant.fr/entry/145/unreal-engine-c-tmap-doc-sheet-1:
	// Matrix elements are accessed with: FMatrix::M[rowIndex][columnIndex]
	// Unreal's convention is to use a row-matrix representation:
	//	X.x  X.y  X.z  0.0 // Basis vector X
	//	Y.x  Y.y  Y.z  0.0 // Basis vector Y
	//	Z.x  Z.y  Z.z  0.0 // Basis vector Z
	//	T.x  T.y  T.z  1.0 // Translation vector
	// 4D api gives the matrix row by row, interpreted with the normal math convention,
	// ie. the first axis of the new base is { M[0][0], M[1][0], M[2][0] }
	for (int Col = 0; Col < 4; ++Col)
	{
		for (int Row = 0; Row < 4; ++Row)
		{
			// instead of Mat.M[Row][Col], because of the above
			double& DestVal = Mat.M[Col][Row];
			if (!TransfoArray[4 * Row + Col]->TryGetNumber(DestVal))
			{
				ensure(false); return false;
			}
			// Doing this introduces a Scale.X:=-1, which is then lost when converting to a
			// keyframe. And it didn't even seem it would've worked with this anyway :/
			// See FITwinSynchro4DSchedulesInternals::ComputeTransformFromFinalizedKeyframe and
			// comment in AddStaticTransformToTimeline... -_-
			//if (1 == Col)
			//	DestVal *= -1.;
		}
	}
	return true;
}

/*static*/
/// Note: anchor point semantic kept in iTwin axes convention.
/// Success already 'ensure'd, so no obligation to test the result
bool FSchedulesParse::Parse3DPathAlignment(FString const& FromStr,
										   std::variant<ITwin::Timeline::EAnchorPoint, FVector>& Anchor)
{
	if (ensure(FromStr.Len() >= 4))
	{
		auto const Lower = FromStr.ToLower();
		switch (Lower[0])
		{
		case 'c':
			if (Lower == TEXT("custom"))
				Anchor = FVector::Zero(); // make an FVector variant - all other cases use the enum
			else if (Lower == TEXT("center"))
				Anchor = ITwin::Timeline::EAnchorPoint::Center;
			else
			{
				ensure(false);
				return false;
			}
			break;
		case 'm':
			if (Lower[1] == 'i' && Lower[2] == 'n')
			{
				if (Lower[3] == 'x')
					Anchor = ITwin::Timeline::EAnchorPoint::MinX;
				else if (Lower[3] == 'y')
					Anchor = ITwin::Timeline::EAnchorPoint::MinY;
				else if (Lower[3] == 'z')
					Anchor = ITwin::Timeline::EAnchorPoint::MinZ;
				else
				{
					ensure(false);
					return false;
				}
			}
			else if (Lower[1] == 'a' && Lower[2] == 'x')
			{
				if (Lower[3] == 'x')
					Anchor = ITwin::Timeline::EAnchorPoint::MaxX;
				else if (Lower[3] == 'y')
					Anchor = ITwin::Timeline::EAnchorPoint::MaxY;
				else if (Lower[3] == 'z')
					Anchor = ITwin::Timeline::EAnchorPoint::MaxZ;
				else
				{
					ensure(false);
					return false;
				}
			}
			else
				return false;
			break;
		case 'o':
			Anchor = ITwin::Timeline::EAnchorPoint::Original;
			break;
		default:
			ensure(false);
			return false;
		}
		return true;
	}
	return false;
}

FString ToString(TSharedPtr<FJsonObject> const& JsonObj)
{
	FString JsonString;
	auto JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonString);
	FJsonSerializer::Serialize(JsonObj.ToSharedRef(), JsonWriter);
	return JsonString;
}

/*static*/
bool FSchedulesParse::Parse3DPathAssignment(FPathTransformAssignment& PathAssignment,
											TSharedPtr<FJsonObject> const& JsonObj)
{
	JSON_GETDELETEDORFALSE(JsonObj, PathAssignment)
	JSON_GETSTR_OR(JsonObj, "id", PathAssignment.Id, return false);
	JSON_GETSTR_OR(JsonObj, "pathId", PathAssignment.Animation3DPathId, return false);
	FString Alignment;
	JSON_GETSTR_OR(JsonObj, "alignment", Alignment, return false);
	if (!FSchedulesParse::Parse3DPathAlignment(Alignment, PathAssignment.TransformAnchor))
	{
		BE_LOGE("ITwin4DImp", "Parsing error for 3D path 'alignment', with value: "
			<< TCHAR_TO_UTF8(*Alignment));
		return false;
	}
	if (std::holds_alternative<FVector>(PathAssignment.TransformAnchor))
	{
		TSharedPtr<FJsonObject> const* CenterObj;
		JSON_GETOBJ_OR(JsonObj, "center", CenterObj, return false);
		if (!FSchedulesParse::ParseVector(*CenterObj, std::get<1>(PathAssignment.TransformAnchor)))
		{
			BE_LOGE("ITwin4DImp", "Parsing error for 3D path custom alignment, from: "
				<< TCHAR_TO_UTF8(*ToString(*CenterObj)));
			return false;
		}
	}
	JSON_GETBOOL_OR(JsonObj, "reverseDirection", PathAssignment.b3DPathReverseDirection, return false);
	JSON_GETNUMBER_OR(JsonObj, "motionStart", PathAssignment.MotionStart, PathAssignment.MotionStart = 0.);
	JSON_GETNUMBER_OR(JsonObj, "motionEnd", PathAssignment.MotionEnd, PathAssignment.MotionEnd = 1.);
	if (PathAssignment.MotionStart > PathAssignment.MotionEnd)
		std::swap(PathAssignment.MotionStart, PathAssignment.MotionEnd);
	return true;
}

/*static*/
bool FSchedulesParse::Parse3DPathKeyframe(std::vector<FString>& KeyframePathIds,
	FTransformKey& Keyframe, TSharedPtr<FJsonObject> const& KeyframeObj, bool const bMixedKeyframes)
{
	JSON_GETDELETEDORFALSE(KeyframeObj, Keyframe)
	JSON_GETNUMBER_OR(KeyframeObj, "time", Keyframe.RelativeTime, return false)
	TSharedPtr<FJsonObject> const* PosObj, * RotObj;
	JSON_GETOBJ_OR(KeyframeObj, "position", PosObj, return false)
	FVector Pos, RotAxis;
	if (!ParseVector(*PosObj, Pos))
		return false;
	Keyframe.Transform = FTransform(Pos);
	bool bSkipRotation = false; // support optional rotation
	JSON_GETOBJ_OR(KeyframeObj, "rotation", RotObj, bSkipRotation = true)
	double AngleDegrees = 0.;
	if (!bSkipRotation)
	{
		JSON_GETNUMBER_OR(*RotObj, "angle", AngleDegrees, bSkipRotation = true)
	}
	if (!bSkipRotation && AngleDegrees != 0. && ParseVector(*RotObj, RotAxis))
	{
		Keyframe.Transform.SetRotation(FQuat(RotAxis, FMath::DegreesToRadians(AngleDegrees)));
	}
	// At the end so an invalid keyframe will not be in the Parsed.Keyframes nor KeyframePathIds
	if (bMixedKeyframes)
	{
		KeyframePathIds.emplace_back();
		JSON_GETSTR_OR(KeyframeObj, "pathId", KeyframePathIds.back(), return false);
	}
	return true;
}
