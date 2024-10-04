/*--------------------------------------------------------------------------------------+
|
|     $Source: http.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "http.h"

namespace SDK::Core 
{

	Http::Http()
	{}

	Http::~Http()
	{}

	void Http::SetBaseUrl(const std::string& url)
	{
		baseUrl_ = url;
	}

	Http::Response Http::GetJson(const std::string& url, const std::string& body, const Headers& hi, bool isFullUrl /*= false*/)
	{
		Headers h(hi);
		h.emplace_back("accept", "application/json");
		h.emplace_back("Content-Type", "application/json; charset=UTF-8");
		return Get(url, body, h, isFullUrl);
	}

	Http::Response Http::PutJson(const std::string& url, const std::string& body, const Headers& hi)
	{
		Headers h(hi);
		h.emplace_back("accept", "application/json");
		h.emplace_back("Content-Type", "application/json; charset=UTF-8");
		return Put(url, body, h);
	}

	Http::Response Http::PatchJson(const std::string& url, const std::string& body, const Headers& hi)
	{
		Headers h(hi);
		h.emplace_back("accept", "application/json");
		h.emplace_back("Content-Type", "application/json; charset=UTF-8");
		return Patch(url, body, h);
	}

	Http::Response Http::PostJson(const std::string& url, const std::string& body, const Headers& hi)
	{
		Headers h(hi);
		h.emplace_back("accept", "application/json");
		h.emplace_back("Content-Type", "application/json; charset=UTF-8");
		return Post(url, body, h);
	}

	Http::Response Http::DeleteJson(const std::string& url, const std::string& body, const Headers& hi)
	{
		Headers h(hi);
		h.emplace_back("accept", "application/json");
		h.emplace_back("Content-Type", "application/json; charset=UTF-8");
		return Delete(url, body, h);
	}
}

