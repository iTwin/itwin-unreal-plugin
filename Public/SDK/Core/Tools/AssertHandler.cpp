/*--------------------------------------------------------------------------------------+
|
|     $Source: AssertHandler.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "AssertHandler.h"
#include "Log.h"
#include <iostream>
#include "../Singleton/singleton.h"

namespace SDK::Core::Tools
{
	std::shared_ptr<IAssertHandler> g_assert;

    template<>
    Tools::Factory<IAssertHandler>::Globals::Globals()
    {
        newFct_ = []() { return static_cast<IAssertHandler*>(new AssertHandler()); };
    }

    template<>
    Tools::Factory<IAssertHandler>::Globals& Tools::Factory<IAssertHandler>::GetGlobals()
    {
        return singleton<Tools::Factory<IAssertHandler>::Globals>();
    }

    void AssertHandlerFct(const libassert::assertion_info& info) {
        switch (info.type) {
        case libassert::assert_type::assertion:
        case libassert::assert_type::debug_assertion:
        case libassert::assert_type::assumption:
            if (BE_GETLOG("BE_ASSERT"))
            {
                BE_LOGD("BE_ASSERT", info.to_string(0, libassert::color_scheme::blank));
            }
            else
            {
                std::cerr << info.to_string() << std::endl;
                (void)fflush(stderr);
            }
            break;
        case libassert::assert_type::panic:
        case libassert::assert_type::unreachable:
            if (BE_GETLOG("BE_ASSERT"))
            {
                BE_LOGD("BE_ASSERT", info.to_string(0, libassert::color_scheme::blank));
            }
            else
            {
                std::cerr << info.to_string() << std::endl;
                (void)fflush(stderr);
            }
             std::abort();
            // Breaking here as debug CRT allows aborts to be ignored, if someone wants to make a
            // debug build of this library
            break;
        default:
            std::cerr << "Critical error: Unknown libassert::assert_type" << std::endl;
            std::abort();
        }
    }

    void FailureHandler(const libassert::assertion_info& info) {
		if (g_assert)
			g_assert->Handler(info);
		else
            AssertHandlerFct(info);
    }

	void InitAssertHandler(std::string const& moduleName)
	{
        InitLog(std::string("log_") + moduleName + ".txt");
        CreateLogChannel("BE_ASSERT", Level::debug);
        g_assert.reset(IAssertHandler::New());
        libassert::set_failure_handler(FailureHandler);
	}

    void AssertHandler::Handler(const libassert::assertion_info& info) {
        AssertHandlerFct(info);
    }

}
