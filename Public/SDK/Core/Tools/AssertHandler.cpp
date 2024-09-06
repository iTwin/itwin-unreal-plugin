/*--------------------------------------------------------------------------------------+
|
|     $Source: AssertHandler.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "AssertHandler.h"
#include <iostream>

namespace SDK::Core::Tools
{
	std::shared_ptr<IAssertHandler> g_assert;

	template<>
	std::function<std::shared_ptr<IAssertHandler>()> Tools::Factory<IAssertHandler>::newFct_ = []() {
		std::shared_ptr<IAssertHandler> p(static_cast<IAssertHandler*>(new AssertHandler()));
		return p;
		};

    void libassert_default_failure_handler(const libassert::assertion_info& info) {
        libassert::enable_virtual_terminal_processing_if_needed(); // for terminal colors on windows
        std::string message = info.to_string(
            libassert::terminal_width(libassert::stderr_fileno),
            libassert::isatty(libassert::stderr_fileno)
            ? libassert::get_color_scheme()
            : libassert::color_scheme::blank
        );
        std::cerr << message << std::endl;
        switch (info.type) {
        case libassert::assert_type::assertion:
        case libassert::assert_type::debug_assertion:
        case libassert::assert_type::assumption:
            (void)fflush(stderr);
            break;
        case libassert::assert_type::panic:
        case libassert::assert_type::unreachable:
            (void)fflush(stderr);
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
			libassert_default_failure_handler(info);
    }

	void InitAssertHandler() 
	{
		g_assert = IAssertHandler::New();
        libassert::set_failure_handler(FailureHandler);
	}

    void AssertHandler::Handler(const libassert::assertion_info& info) {
        libassert_default_failure_handler(info);
    }

}
