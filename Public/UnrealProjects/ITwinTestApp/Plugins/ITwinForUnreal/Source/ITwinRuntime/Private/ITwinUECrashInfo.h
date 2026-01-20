/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinUECrashInfo.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CoreMinimal.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <Core/Tools/CrashInfo.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

/// Helper to redirect ITwin SDKCore CrashInfos to Unreal.
class ITWINRUNTIME_API FITwinUECrashInfo : public AdvViz::SDK::Tools::CrashInfo
{
	using Super = AdvViz::SDK::Tools::CrashInfo;

public:
	FITwinUECrashInfo();
	void AddInfo(const std::string& key, const std::string& value) override;

	static void Init();

private:
};

