/*--------------------------------------------------------------------------------------+
|
|     $Source: SchedulesParse.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "SchedulesStructs.h"
#include <Network/HttpUtils.h>

class FJsonObject;
using FLock = ITwinHttp::FLock;

class FSchedulesParse
{
public:
	static bool ParseScheduleStatistics(TSharedPtr<FJsonObject> const& Reply, FITwinScheduleStats& Tmp,
										FString const& ScheduleId, FString const& ScheduleName);
	static bool ParseAnimationBinding(TSharedPtr<FJsonObject> const& BindingObj, ITwinElementID& ElementID,
									  FGuid& ElementGuid, FAnimationBinding& Tmp);
	/// \return When updating a known task, returns whether a parsed property differs from the previous value in a way
	///		that will affect the 4D animation. Indeed we don't want to rebuild all the timelines when merely changing
	///		non-anim related properties, which tasks have a lot of :/ When parsing the task for the first time, returns
	///		true for consistency.
	static bool ParseTaskDetails(TSharedPtr<FJsonObject> const& JsonObj, FScheduleTask& Task,
								 FString const& ScheduleId, FLock& Lock);
	static bool ParseTaskDates(TSharedPtr<FJsonObject> const& JsonObj, FDateTime& Start, FDateTime& Finish,
							   FString& StartStr, FString& FinishStr, bool const bActualOrPlanned);
	/// \return When updating a known appearance profile, returns whether a parsed property differs from the previous
	///		value in a way that will affect the 4D animation. Indeed we don't want to rebuild all the timelines when
	///		merely changing non-anim related properties, eg when renaming the apperance profile :/ When parsing the
	///		appearance profile for the first time, returns true for consistency.
	static bool ParseAppearanceProfileDetails(TSharedPtr<FJsonObject> const& JsonObj,
											  FAppearanceProfile& AppearanceProfile, FLock& Lock);
	static EProfileAction ParseProfileAction(FString const& FromStr);
	static bool ParseStaticTransform(FMatrix& Mat, TSharedPtr<FJsonObject> const& JsonObj);
	static bool Parse3DPathAlignment(FString const& Alignment,
									 std::variant<ITwin::Timeline::EAnchorPoint, FVector>& Anchor);
	static bool Parse3DPathAssignment(FPathTransformAssignment& PathAssignment, TSharedPtr<FJsonObject> const& JsonObj);
	/// \return Parsing success (unlike ParseAppearanceProfileDetails!)
	static bool Parse3DPathKeyframe(std::vector<FString>& KeyframePathIds, FTransformKey& Keyframe,
									TSharedPtr<FJsonObject> const& KeyframeObj, bool const bMixedKeyframes);
	/// \return Parsing success (unlike ParseAppearanceProfileDetails!)
	static bool ParseSimpleAppearance(FSimpleAppearance& Appearance, bool const bBaseOfActiveAppearance,
									  TSharedPtr<FJsonObject> const& JsonObj);
	/// \return Parsing success (unlike ParseAppearanceProfileDetails!)
	static bool ParseActiveAppearance(FActiveAppearance& Appearance, TSharedPtr<FJsonObject> const& JsonObj);
	/// \return Parsing success (unlike ParseAppearanceProfileDetails!)
	static bool ParseColorFromHexString(FString const& FromStr, FVector& Color);
	/// \return Parsing success (unlike ParseAppearanceProfileDetails!)
	static bool ParseVector(TSharedPtr<FJsonObject> const& JsonObj, FVector& Out);
	/// \return Parsing success (unlike ParseAppearanceProfileDetails!)
	static bool ParseGrowthSimulationMode(FString const& FromStr, EGrowthSimulationMode& Mode);
};
