/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinUECrashInfo.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinUECrashInfo.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"

FITwinUECrashInfo::FITwinUECrashInfo()
{
}

void FITwinUECrashInfo::AddInfo(const std::string& key, const std::string& value)
{
	Super::AddInfo(key, value);
	FGenericCrashContext::SetGameData(UTF8_TO_TCHAR(key.c_str()), UTF8_TO_TCHAR(value.c_str()));
}

/*static*/ void FITwinUECrashInfo::Init()
{
	using namespace AdvViz::SDK::Tools;
	ICrashInfo::SetNewFct([]() {
		return static_cast<ICrashInfo*>(new FITwinUECrashInfo());
		});
	InitCrashInfo();
}