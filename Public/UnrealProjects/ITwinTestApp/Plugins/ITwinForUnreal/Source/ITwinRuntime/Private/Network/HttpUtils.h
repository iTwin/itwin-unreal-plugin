/*--------------------------------------------------------------------------------------+
|
|     $Source: HttpUtils.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"

#include <ITwinHttpUtils.h>

#include <HttpFwd.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/StrongTypes/TaggedValue.h>
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

	DEFINE_STRONG_BOOL(DeltaQuery);

	FString DescribeTransportFailure(FHttpRequestPtr const& Request,
		ITwinHttp::ConnectionSuccess const bConnectedSuccessfully, FHttpResponsePtr const& Response = {});

	/// Erases a parameter (and its value!) in a copy of the input URL. The copy is only made if the parameter is
	/// found in the input URL.
	/// \param pRedactedUrl Output parameter where to copy the URL and erase the parameter, in case it is found in the
	///		input URL. Untouched if this function returns false.
	/// \return Whether a parameter was found and erased
	bool EraseURLParameter(FString const& Url, FString const& EraseParam, FString* pRedatedUrl);

	/// Erases a parameter (and its value!) in place in the passed URL string.
	/// \param Url URL string modified in place. Untouched if this function returns false.
	/// \return Whether a parameter was found and erased
	bool EraseURLParameter(FString& Url, FString const& EraseParam);
}
