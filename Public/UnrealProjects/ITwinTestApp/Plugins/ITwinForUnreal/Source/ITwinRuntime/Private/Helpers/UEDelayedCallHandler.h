/*--------------------------------------------------------------------------------------+
|
|     $Source: UEDelayedCallHandler.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"
#include <Templates/PimplPtr.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Tools/IDelayedCallHandler.h>
#include <Compil/AfterNonUnrealIncludes.h>

class FUEDelayedCallHandler : public AdvViz::SDK::IDelayedCallHandler
{
public:
	FUEDelayedCallHandler();

	virtual void UniqueDelayedCall(std::string const& UniqueId,
		AdvViz::SDK::DelayedCallFunc&& Func, float DelayInSeconds) override;

private:
	class FImpl;
	TPimplPtr<FImpl> Impl;
};
