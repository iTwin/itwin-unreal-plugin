/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinUELogAdapter.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <CoreMinimal.h>

#if WITH_EDITOR

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <Core/Tools/Log.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

/// Helper to redirect ITwin SDKCore logs to Unreal in Editor mode.
class ITWINRUNTIME_API FITwinUELogAdapter : public AdvViz::SDK::Tools::Log
{
	using Super = AdvViz::SDK::Tools::Log;
	using Level = AdvViz::SDK::Tools::Level;

public:
	FITwinUELogAdapter(std::string name, Level sev);
	virtual void DoLog(const std::string& msg, Level sev, const char* srcPath, const char* func, int line) override;

private:
	std::string const msgPrefix_;
};

#endif // WITH_EDITOR
