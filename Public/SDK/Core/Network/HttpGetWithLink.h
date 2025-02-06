/*--------------------------------------------------------------------------------------+
|
|     $Source: HttpGetWithLink.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#include "Network.h"
#include <fmt/format.h>

namespace SDK::Core
{
	template<typename T, typename TFct, typename SJsonInT>
	inline SDK::expected<void, std::string> HttpGetWithLink(std::shared_ptr<Http>& http, const std::string& url, const Http::Headers &headers, SJsonInT& jIn, const TFct& fct)
	{
		struct SJsonLink
		{
			std::optional<std::string> prev;
			std::optional<std::string> self;
			std::optional<std::string> next;
		};

		struct SJsonOut { int total_rows; std::vector<T> rows; SJsonLink _links; };
		SJsonOut jOut;

		long status = http->GetJsonJBody(jOut, url, jIn, headers);
		bool continueLoading = true;

		while (continueLoading)
		{
			if (status != 200 && status != 201)
			{
				continueLoading = false;
				return SDK::make_unexpected(fmt::format("{} failed with Http status:{}", url, status));
			}

			for (auto& row : jOut.rows)
			{
				auto ret = fct(row);
				if (!ret)
					return ret;
			}

			jOut.rows.clear();
			if (jOut._links.next.has_value() && !jOut._links.next.value().empty())
			{
				status = http->GetJsonJBody(jOut, jOut._links.next.value(), jIn, headers, true);
			}
			else
			{
				continueLoading = false;
			}
		}
		return {};
	}

}
