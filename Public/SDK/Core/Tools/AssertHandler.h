/*--------------------------------------------------------------------------------------+
|
|     $Source: AssertHandler.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "Assert.h"

#include "FactoryClass.h"
#include "Extension.h"

namespace SDK::Core::Tools
{
	class IAssertHandler : public Tools::Factory<IAssertHandler>, public std::enable_shared_from_this<IAssertHandler>
	{
	public:
		virtual void Handler(const libassert::assertion_info& info) = 0;
	};

	class AssertHandler : public Tools::ExtensionSupport, public IAssertHandler
	{
	public:
		void Handler(const libassert::assertion_info& info) override;
	};

	void InitAssertHandler(std::string const& moduleName);
}
