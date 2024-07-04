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

	std::pair<long, std::string> Http::GetJson(const std::string& url, const std::string& body, const Headers& hi)
	{
		Headers h(hi);
		h.emplace_back("accept", "application/json");
		h.emplace_back("Content-Type", "application/json; charset=UTF-8");
		return Get(url, body, h);
	}

	std::pair<long, std::string> Http::PutJson(const std::string& url, const std::string& body, const Headers& hi)
	{
		Headers h(hi);
		h.emplace_back("accept", "application/json");
		h.emplace_back("Content-Type", "application/json; charset=UTF-8");
		return Put(url, body, h);
	}

	std::pair<long, std::string> Http::PatchJson(const std::string& url, const std::string& body, const Headers& hi)
	{
		Headers h(hi);
		h.emplace_back("accept", "application/json");
		h.emplace_back("Content-Type", "application/json; charset=UTF-8");
		return Patch(url, body, h);
	}

	std::pair<long, std::string> Http::PostJson(const std::string& url, const std::string& body, const Headers& hi)
	{
		Headers h(hi);
		h.emplace_back("accept", "application/json");
		h.emplace_back("Content-Type", "application/json; charset=UTF-8");
		return Post(url, body, h);
	}

}

