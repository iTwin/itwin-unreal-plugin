/*--------------------------------------------------------------------------------------+
|
|     $Source: MockServerHeaderCheck.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <Core/Tools/Log.h>

//! Header-only helpers shared by the SDK mock server (HTTPMock) and the Unreal
//! one (FITwinMockServerBase). Header-only on purpose: the two live in
//! different link units (CMake NetworkMockServer vs. UBT ITwinRuntime), so any
//! out-of-line definition would fail to link on one side or the other.
namespace AdvViz::SDK::MockServerUtils
{
	// Numerically identical to MHD_HTTP_OK / _BAD_REQUEST / _UNAUTHORIZED,
	// declared here so this header does not depend on microhttpd.h.
	inline constexpr int HttpOk = 200;
	inline constexpr int HttpBadRequest = 400;
	inline constexpr int HttpUnauthorized = 401;

	//! \p HeaderType only needs public .key and .value strings.
	template <typename HeaderType>
	std::string HeadersToString(const std::vector<HeaderType>& headers)
	{
		std::string str("{ ");
		for (const HeaderType& header : headers)
			str += "{" + header.key + " : " + header.value + "}, ";
		str += "}";
		return str;
	}

	//! Check that \p headers contains all \p requiredHeaders with the expected
	//! values. A required value ending with '*' is matched as a prefix (useful
	//! for "multipart/form-data; boundary=*" or a Content-Length magnitude).
	//! \return HttpOk on success, HttpUnauthorized if the Authorization header
	//!         mismatched, HttpBadRequest for any other mismatch or omission.
	template <typename HeaderType>
	int CheckRequiredHeaders(const std::vector<HeaderType>& headers,
		const std::map<std::string, std::string>& requiredHeaders)
	{
		std::set<std::string> matchedHeaders;
		std::optional<int> headerError;
		std::string errorInfo;
		for (const HeaderType& header : headers)
		{
			auto itReq = requiredHeaders.find(header.key);
			if (itReq == requiredHeaders.end())
				continue;
			std::string_view const requiredValue(itReq->second);
			bool const bMatchingValue = (header.value == requiredValue)
				|| (requiredValue.ends_with("*")
					&& header.value.starts_with(
						requiredValue.substr(0, requiredValue.length() - 1)));
			if (bMatchingValue)
			{
				matchedHeaders.insert(header.key);
			}
			else
			{
				errorInfo = std::string(" - value differs for ") + header.key
					+ ": was expecting '" + itReq->second
					+ "' and found '" + header.value + "'";
				headerError = (header.key == "Authorization")
					? HttpUnauthorized : HttpBadRequest;
				break;
			}
		}

		if (!headerError && matchedHeaders.size() != requiredHeaders.size())
		{
			errorInfo = " - missing header(s): [";
			int missingKeyIndex(0);
			for (auto const& [key, _] : requiredHeaders)
			{
				if (!matchedHeaders.contains(key))
				{
					if (missingKeyIndex > 0)
						errorInfo += ", ";
					errorInfo += key;
					missingKeyIndex++;
				}
			}
			errorInfo += "]";
			headerError = HttpBadRequest;
		}
		if (headerError)
		{
			BE_LOGE("ITwinAPI", "Not the expected headers (" << *headerError
				<< errorInfo << ") -> " << HeadersToString(headers));
			return *headerError;
		}
		return HttpOk;
	}

	//! Shared test token, used by both the SDK and Unreal mock servers.
	#define ITWINTEST_ACCESS_TOKEN "ThisIsATestITwinAccessToken"

} // namespace AdvViz::SDK::MockServerUtils
