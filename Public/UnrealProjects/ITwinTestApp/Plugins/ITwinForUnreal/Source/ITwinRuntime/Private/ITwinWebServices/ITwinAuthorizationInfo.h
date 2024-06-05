/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAuthorizationInfo.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"

struct FITwinAuthorizationInfo
{
	FString AuthorizationCode;
	FString CodeVerifier;
	FString RefreshToken;
	int ExpiresIn = 0; // expressed in seconds
	double CreationTime = 0.; // in seconds

	double GetExpirationTime() const { return CreationTime + ExpiresIn; }
};
