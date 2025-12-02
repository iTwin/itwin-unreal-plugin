/*--------------------------------------------------------------------------------------+
|
|     $Source: http.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "http.h"
#include <string.h>

namespace AdvViz::SDK 
{

	Http::Http()
	{}

	Http::~Http()
	{}


	void Http::SetBaseUrl(const char* url)
	{
		baseUrl_ = url;
	}

	Http::Response Http::GetJsonStr(const std::string& url, const Headers& hi, bool isFullUrl /*= false*/)
	{
		Headers h(hi);
		h.emplace_back("accept", "application/json");
		h.emplace_back("Content-Type", "application/json; charset=UTF-8");
		return Get(url, h, isFullUrl);
	}

	Http::Response Http::PutJson(const std::string& url, const BodyParams& body, const Headers& hi)
	{
		Headers h(hi);
		h.emplace_back("accept", "application/json");
		h.emplace_back("Content-Type", "application/json; charset=UTF-8");
		return Put(url, body, h);
	}

	Http::Response Http::PatchJson(const std::string& url, const BodyParams& body, const Headers& hi)
	{
		Headers h(hi);
		h.emplace_back("accept", "application/json");
		h.emplace_back("Content-Type", "application/json; charset=UTF-8");
		return Patch(url, body, h);
	}

	Http::Response Http::PostJson(const std::string& url, const BodyParams& body, const Headers& hi)
	{
		Headers h(hi);
		h.emplace_back("accept", "application/json");
		h.emplace_back("Content-Type", "application/json; charset=UTF-8");
		return Post(url, body, h);
	}

	Http::Response Http::DeleteJson(const std::string& url, const BodyParams& body, const Headers& hi)
	{
		Headers h(hi);
		h.emplace_back("accept", "application/json");
		h.emplace_back("Content-Type", "application/json; charset=UTF-8");
		return Delete(url, body, h);
	}

	const char* Http::GetBaseUrl() const
	{
		return baseUrl_.c_str();
	}

	AdvViz::SDK::Http::Response Http::Get(const std::string& url, const Headers& hi /*= {}*/, bool isFullUrl /*= false*/)
	{
		Headers h(hi);
		if (accessToken_)
			h.emplace_back("Authorization", std::string("Bearer ") + *accessToken_);
		return DoGet(url, h, isFullUrl);

	}

	AdvViz::SDK::Http::Response Http::Patch(const std::string& url, const BodyParams& body, const Headers& hi /*= {}*/)
	{
		Headers h(hi);
		if (accessToken_)
			h.emplace_back("Authorization", std::string("Bearer ") + *accessToken_);
		return DoPatch(url, body, h);
	}

	AdvViz::SDK::Http::Response Http::Post(const std::string& url, const BodyParams& body, const Headers& hi /*= {}*/)
	{
		Headers h(hi);
		if (accessToken_)
			h.emplace_back("Authorization", std::string("Bearer ") + *accessToken_);
		return DoPost(url, body, h);
	}

	AdvViz::SDK::Http::Response Http::PostFile(const std::string& url, const std::string& fileParamName, const std::string& filePath, const KeyValueVector& extraParams /*= {}*/, const Headers& hi /*= {}*/)
	{
		Headers h(hi);
		h.emplace_back("Authorization", std::string("Bearer ") + *accessToken_);
		return DoPostFile(url, fileParamName, filePath, extraParams, h);
	}

	AdvViz::SDK::Http::Response Http::Put(const std::string& url, const BodyParams& body, const Headers& hi /*= {}*/)
	{
		Headers h(hi);
		if (accessToken_)
			h.emplace_back("Authorization", std::string("Bearer ") + *accessToken_);
		return DoPut(url, body, h);
	}

	AdvViz::SDK::Http::Response AdvViz::SDK::Http::PutBinaryFile(const std::string& url, const std::string& filePath, const Headers& hi /*= {}*/)
	{
		Headers h(hi);
		if (accessToken_)
			h.emplace_back("Authorization", std::string("Bearer ") + *accessToken_);
		return DoPutBinaryFile(url, filePath, h);
	}

	AdvViz::SDK::Http::Response Http::Delete(const std::string& url, const BodyParams& body, const Headers& hi /*= {}*/)
	{
		Headers h(hi);
		if (accessToken_)
			h.emplace_back("Authorization", std::string("Bearer ") + *accessToken_);
		return DoDelete(url, body, h);
	}

	void Http::SetExecuteAsyncCallbackInGameThread(bool)
	{

	}

}

