/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinHttpUtils.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include <HttpFwd.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/StrongTypes/TaggedValue.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

namespace ITwinHttp
{
	DEFINE_STRONG_BOOL(ConnectionSuccess);
	DEFINE_STRONG_BOOL(RetryQuery);

	bool ITWINRUNTIME_API CheckRequest(FHttpRequestPtr const& CompletedRequest, FHttpResponsePtr const& Response,
		ConnectionSuccess const bConnectedSuccessfully, FString* pstrError = nullptr,
		RetryQuery bWillRetry = RetryQuery(false));
}
