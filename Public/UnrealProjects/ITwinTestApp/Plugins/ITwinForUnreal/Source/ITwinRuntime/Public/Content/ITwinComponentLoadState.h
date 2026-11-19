/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinComponentLoadState.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CoreMinimal.h>

/// State of a recognized component ID or component-prefixed asset path.
enum class EITwinComponentLoadState : uint8
{
	NotComponent,
	NotRequested,
	InFlight,
	Ready,
	RetryableFailure,
	TerminalFailure
};

enum class EITwinComponentLoadFailure : uint8
{
	Retryable,
	Terminal
};

/// Only describes component attempts; it does not establish actor or instance readiness.
enum class EITwinComponentLoadWait : uint8
{
	NoFailures,
	InFlight,
	Failed
};
