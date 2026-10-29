/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinEnvironmentEnum.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <CoreMinimal.h>
#include <Engine/Blueprint.h>
#include <array>
#include <string>

#include "ITwinEnvironmentEnum.generated.h"

UENUM()
enum class EITwinEnvironment : uint8
{
	Prod,
	QA,
	Dev,
	Invalid,
};


namespace ITwin
{
	// App ID can be depend on the chosen environment, so we store all possible values
	static constexpr size_t ENV_COUNT = static_cast<size_t>(EITwinEnvironment::Invalid) + 1;
	using AppIDArray = std::array<std::string, ENV_COUNT>;
}
