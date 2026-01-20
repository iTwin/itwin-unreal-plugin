/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMaterialPredictionStatus.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/ITwinAPI/ITwinMatMLPredictionEnums.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include "ITwinMaterialPredictionStatus.generated.h"

/// Describes the status of the ML-based material prediction process.
UENUM()
enum class EITwinMaterialPredictionStatus : uint8
{
	Unknown,
	NoAuth,
	InProgress,
	Failed,
	Complete,
	Validated, /* validated by a human person */
};

// Should be synchronized with AdvViz::SDK::EITwinMatMLPredictionStatus
static_assert(static_cast<EITwinMaterialPredictionStatus>(AdvViz::SDK::EITwinMatMLPredictionStatus::Complete) == EITwinMaterialPredictionStatus::Complete,
	"EITwinMaterialPredictionStatus enum definition mismatch");
