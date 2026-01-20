/*--------------------------------------------------------------------------------------+
|
|     $Source: CrashInfo.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#include "FactoryClassInternalHelper.h"
#include "CrashInfo.h"
#include "Assert.h"
#include "Log.h"

namespace AdvViz::SDK::Tools
{
	namespace Internal
	{
		struct CrashInfoGlobals
		{
			ICrashInfoPtr singleton_;
		};

		CrashInfoGlobals& GetCrashInfoGlobals()
		{
			return singleton<CrashInfoGlobals>();
		}
	}

	DEFINEFACTORYGLOBALS(CrashInfo);

	CrashInfo::CrashInfo()
	{
	}

	void CrashInfo::AddInfo(const std::string& key, const std::string& value)
	{ 
		if (IsLogInitialized())
			BE_LOGI("AdvVizSDK", "Adding crash value:" << key << ":" << value);
	}

	void InitCrashInfo()
	{
		if (!Internal::GetCrashInfoGlobals().singleton_)
			Internal::GetCrashInfoGlobals().singleton_.reset(ICrashInfo::New());
	}

	ICrashInfoPtr AdvViz::SDK::Tools::GetCrashInfo()
	{
		if (!Internal::GetCrashInfoGlobals().singleton_)
		{
			if (IsLogInitialized())
				BE_LOGW("AdvVizSDK", "CrashInfo singleton not initialized. Doing it now. Could be with wrong implementation.");
			InitCrashInfo();
		}
		return Internal::GetCrashInfoGlobals().singleton_;
	}
}
