/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMaterialPredictionStatus.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/ITwinAPI/ITwinMatMLPredictionEnums.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include "ITwinMaterialPredictionStatus.generated.h"

/// Describes the status of the ML-base material prediction process.
UENUM()
enum class EITwinMaterialPredictionStatus : uint8
{
	Unknown,
	NoAuth,
	InProgress,
	Failed,
	Complete,
};

// Should be synchronized with SDK::Core::EITwinMatMLPredictionStatus
static_assert(static_cast<EITwinMaterialPredictionStatus>(SDK::Core::EITwinMatMLPredictionStatus::Complete) == EITwinMaterialPredictionStatus::Complete,
	"EITwinMaterialPredictionStatus enum definition mismatch");
