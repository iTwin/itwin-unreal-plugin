/*--------------------------------------------------------------------------------------+
|
|     $Source: AssertHandler.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "Assert.h"

#include "FactoryClass.h"
#include "Extension.h"

namespace AdvViz::SDK::Tools
{
	class IAssertHandler : public Tools::Factory<IAssertHandler>, public Tools::ExtensionSupport
	{
	public:
		virtual void Handler(const libassert::assertion_info& info) = 0;
	};

	class AssertHandler : public IAssertHandler, public Tools::TypeId<AssertHandler>
	{
	public:
		void Handler(const libassert::assertion_info& info) override;

		using Tools::TypeId<AssertHandler>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || IAssertHandler::IsTypeOf(i); }
	};

	ADVVIZ_LINK void InitAssertHandler(std::string const& moduleName);
}
