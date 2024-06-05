/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAuthorizationObserver.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

class ITWINRUNTIME_API IITwinAuthorizationObserver
{
public:
	virtual ~IITwinAuthorizationObserver() = default;

	virtual void OnAuthorizationDone(bool bSuccess, FString const& Error) = 0;
};
