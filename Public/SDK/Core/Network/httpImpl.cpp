/*--------------------------------------------------------------------------------------+
|
|     $Source: httpImpl.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------------+
|
|     $Source: httpImpl.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cpr/cpr.h>
#include "http.h"
#include "httpCprImpl.h"

namespace SDK::Core
{
	std::shared_ptr<Http> Http::New() {
		return std::shared_ptr<Http>(new HttpImpl());
	}

	// downcasting methods
	inline HttpImpl* GetImpl(Http* ptr) { return (HttpImpl*)ptr; }
	inline const HttpImpl* GetImpl(const HttpImpl* ptr) { return (const HttpImpl*)ptr; }

	void Http::SetBaseUrl(const std::string& url)
	{
		GetImpl(this)->SetBaseUrl(url);
	}

	void Http::SetBasicAuth(const std::string& login, const std::string& passwd)
	{
		GetImpl(this)->SetBasicAuth(login, passwd);
	}

	std::pair<long, std::string> Http::Put(const std::string& url, const std::string& body, const Headers& h)
	{
		return GetImpl(this)->Put(url, body, h);
	}

	std::pair<long, std::string> Http::Patch(const std::string& url, const std::string& body, const Headers& h)
	{
		return GetImpl(this)->Patch(url, body, h);
	}

	std::pair<long, std::string> Http::Post(const std::string& url, const std::string& body, const Headers& h)
	{
		return GetImpl(this)->Post(url, body, h);
	}

	std::pair<long, std::string> Http::Get(const std::string& url, const std::string& body, const Headers& h)
	{
		return GetImpl(this)->Get(url, body, h);
	}

	std::pair<long, std::string> Http::Delete(const std::string& url, const std::string& body, const Headers& h)
	{
		return GetImpl(this)->Delete(url, body, h);
	}

	std::pair<long, std::string> Http::GetJson(const std::string& url, const std::string& body, const Headers& hi)
	{
		HttpImpl::Headers h(hi);
		h.emplace_back("accept", "application/json");
		h.emplace_back("Content-Type", "application/json; charset=UTF-8");
		return GetImpl(this)->Get(url, body, h);
	}

	std::pair<long, std::string> Http::PutJson(const std::string& url, const std::string& body, const Headers& hi)
	{
		HttpImpl::Headers h(hi);
		h.emplace_back("accept", "application/json");
		h.emplace_back("Content-Type", "application/json; charset=UTF-8");
		return GetImpl(this)->Put(url, body, h);
	}

	std::pair<long, std::string> Http::PatchJson(const std::string& url, const std::string& body, const Headers& hi)
	{
		HttpImpl::Headers h(hi);
		h.emplace_back("accept", "application/json");
		h.emplace_back("Content-Type", "application/json; charset=UTF-8");
		return GetImpl(this)->Patch(url, body, h);
	}

	std::pair<long, std::string> Http::PostJson(const std::string& url, const std::string& body, const Headers& hi)
	{
		HttpImpl::Headers h(hi);
		h.emplace_back("accept", "application/json");
		h.emplace_back("Content-Type", "application/json; charset=UTF-8");
		return GetImpl(this)->Post(url, body, h);
	}


}

