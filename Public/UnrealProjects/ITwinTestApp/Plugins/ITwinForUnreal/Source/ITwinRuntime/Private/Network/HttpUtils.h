/*--------------------------------------------------------------------------------------+
|
|     $Source: HttpUtils.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/ITwinAPI/ITwinRequestTypes.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <mutex>

namespace ITwinHttp
{
	using FMutex = std::recursive_mutex;
	using FLock = std::lock_guard<FMutex>;

	using EVerb = AdvViz::SDK::EVerb;

	inline FString GetVerbString(EVerb eVerb)
	{
		FString Verb;
		switch (eVerb)
		{
		case EVerb::Delete:	Verb = TEXT("DELETE"); break;
		case EVerb::Get:	Verb = TEXT("GET"); break;
		case EVerb::Patch:	Verb = TEXT("PATCH"); break;
		case EVerb::Post:	Verb = TEXT("POST"); break;
		case EVerb::Put:	Verb = TEXT("PUT"); break;
		}
		return Verb;
	}
}
