/*--------------------------------------------------------------------------------------+
|
|     $Source: CrashInfo.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "../AdvVizLinkType.h"
#include "FactoryClass.h"
#include "Extension.h"
#include <memory>
#include <string>

namespace AdvViz::SDK::Tools
{
	class ICrashInfo : public Tools::Factory<ICrashInfo>
	{
	public:
		virtual void AddInfo(const std::string& key, const std::string& value) = 0;
	};

	class CrashInfo : public ICrashInfo, public Tools::ExtensionSupport, Tools::TypeId<CrashInfo>
	{
	public:
		CrashInfo();
		void AddInfo(const std::string& key, const std::string& value) override;
	};

	typedef std::shared_ptr<ICrashInfo> ICrashInfoPtr;
	ADVVIZ_LINK ICrashInfoPtr GetCrashInfo();
	ADVVIZ_LINK void InitCrashInfo();
}