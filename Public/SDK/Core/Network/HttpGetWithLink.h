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

namespace AdvViz::SDK
{
	template<typename T, typename VecTFct>
	inline AdvViz::expected<void, std::string> HttpGetWithLink_ByBatch(std::shared_ptr<Http>& http,
		const std::string& url, const Http::Headers& headers,
		const VecTFct& fct)
	{
		struct SJsonLink
		{
			std::optional<std::string> prev;
			std::optional<std::string> self;
			std::optional<std::string> next;
		};

		struct SJsonOut
		{
			int total_rows = 0;
			std::optional<std::vector<T>> rows;
			SJsonLink _links;
		};
		SJsonOut jOut;

		long status = http->GetJson(jOut, url, headers);
		bool continueLoading = true;
		if (jOut.total_rows > 0 && !jOut.rows.has_value() && (status == 200 || status == 201))
		{
			BE_ISSUE("unexpected Json parsed value");
		}
		while (continueLoading)
		{
			if (status != 200 && status != 201)
			{
				continueLoading = false;
				return AdvViz::make_unexpected(fmt::format("{} failed with Http status:{}", url, status));
			}
			if (jOut.rows)
			{
				auto ret = fct(*jOut.rows);
					if (!ret)
						return ret;

				jOut.rows->clear();
			}
			if (jOut._links.next.has_value() && !jOut._links.next.value().empty())
			{
				
				// Quick workaround for a bug in Decoration Service sometimes providing bad URLs with http
				// instead of https protocol! (issue found for instances, which was causing bug #1609088).
				if (!jOut._links.next.value().starts_with("http://localhost") &&
					!jOut._links.next.value().starts_with("http://127.0.0.1")
					) // don't change http when using localhost
				{
					std::string const nextUrl = rfl::internal::strings::replace_all(
						jOut._links.next.value(),
						"http://", "https://");
					status = http->GetJson(jOut, nextUrl, headers, true);
				}
				else
				{
					status = http->GetJson(jOut, jOut._links.next.value(), headers, true);
				}
			}
			else
			{
				continueLoading = false;
			}
		}
		return {};
	}


	template<typename T, typename TFct>
	inline AdvViz::expected<void, std::string> HttpGetWithLink(std::shared_ptr<Http>& http,
		const std::string& url, const Http::Headers& headers,
		const TFct& fct)
	{
		return HttpGetWithLink_ByBatch<T>(http, url, headers,
			[&fct](std::vector<T>& rows) -> expected<void, std::string>
		{
			for (auto& row : rows)
			{
				auto ret = fct(row);
				if (!ret)
					return ret;
}
			return {};
		});
	}

}
