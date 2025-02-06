/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinServerEnvironment.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "ITwinServerConnection.h"

namespace ITwinServerEnvironment
{
	inline FText ToName(EITwinEnvironment value)
	{
		switch (value)
		{
		case EITwinEnvironment::Prod: return FText::FromString("Prod");
		case EITwinEnvironment::QA: return FText::FromString("QA");
		case EITwinEnvironment::Dev: return FText::FromString("Dev");
		}
		check(false);
		return FText::FromString("<Invalid>");
	}

	inline FString GetUrlPrefix(EITwinEnvironment value)
	{
		switch (value)
		{
		case EITwinEnvironment::Prod: return "";
		case EITwinEnvironment::QA: return "qa-";
		case EITwinEnvironment::Dev: return "dev-";
		}
		check(false);
		return "";
	}
}
