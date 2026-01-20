/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinRequestDump.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <sstream>
#include <Core/Tools/Hash.h>

namespace AdvViz::SDK::RequestDump
{

struct Request
{
	std::string urlSuffix;
	std::string body;
};

struct Response
{
	long status;
	std::string body;
};

//! Returns a stringized hash of the given request.
//! Can be used as a folder name where to write or read the request & response content.
inline std::string GetRequestHash(const std::string& urlSuffix, const std::string& body)
{
	return (std::stringstream() << std::hex << Tools::GenHash((urlSuffix+";"+body).c_str())).str();
}

} // namespace AdvViz::SDK::RequestDump
