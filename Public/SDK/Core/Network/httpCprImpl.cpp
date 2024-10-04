/*--------------------------------------------------------------------------------------+
|
|     $Source: httpCprImpl.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cpr/cpr.h>
#include "http.h"
#include "httpCprImpl.h"

namespace SDK::Core
{

	template<>
	std::function<std::shared_ptr<Http>()> Tools::Factory<Http>::newFct_ = []() {
		std::shared_ptr<Http> p(static_cast<Http*>(new Impl::HttpCpr()));
		return p;
	};

}

namespace SDK::Core::Impl
{

	void HttpCpr::SetBasicAuth(const std::string& login, const std::string& passwd)
	{
		auth_ = std::make_unique<cpr::Authentication>(login, passwd, cpr::AuthMode::BASIC);
	}

	Http::Response HttpCpr::Put(const std::string& url,
											  const std::string& body /*= "" */,
											  const Headers& headers /*= {} */)
	{
		cpr::Header h;
		for (auto& i : headers)
			h[i.first] = i.second;
		cpr::Response r;
		if (auth_)
			r = cpr::Put(cpr::Url{ GetBaseUrl() + '/' + url }
				, cpr::Body{ body }
				, h
				, *auth_
			);
		else
			r = cpr::Put(cpr::Url{ GetBaseUrl() + '/' + url }
				, cpr::Body{ body }
				, h
		);
		return { r.status_code, r.text };
	}

	Http::Response HttpCpr::Patch(const std::string& url,
												const std::string& body /*= "" */,
												const Headers& headers /*= {} */)
	{
		cpr::Header h;
		for (auto& i : headers)
			h[i.first] = i.second;
		cpr::Response r;
		if (auth_)
			r = cpr::Patch(cpr::Url{ GetBaseUrl() + '/' + url }
				, cpr::Body{ body }
				, h
				, *auth_
			);
		else
			r = cpr::Patch(cpr::Url{ GetBaseUrl() + '/' + url }
				, cpr::Body{ body }
				, h
		);
		return { r.status_code, r.text };
	}

	Http::Response HttpCpr::Post(const std::string& url,
											   const std::string& body /*= "" */,
											   const Headers& headers /*= {} */)
	{
		cpr::Header h;
		for (auto& i : headers)
			h[i.first] = i.second;
		cpr::Response r;
		if (auth_)
			r = cpr::Post(cpr::Url{ GetBaseUrl() + '/' + url }
				, cpr::Body{ body }
				, h
				, *auth_
			);
		else
			r = cpr::Post(cpr::Url{ GetBaseUrl() + '/' + url }
				, cpr::Body{ body }
				, h
		);
		return { r.status_code, r.text };
	}

	Http::Response HttpCpr::Get(const std::string& url,
											  const std::string& body /*= "" */,
											  const Headers& headers /*= {} */,
											  bool isFullUrl /*= false*/)
	{
		cpr::Header h;
		for (auto& i : headers)
			h[i.first] = i.second;
		cpr::Response r;
		if (auth_)
			r = cpr::Get(cpr::Url{ isFullUrl ? url : (GetBaseUrl() + '/' + url) }
				, cpr::Body{ body }
				, h
				, *auth_
			);
		else
			r = cpr::Get(cpr::Url{ isFullUrl ? url : (GetBaseUrl() + '/' + url) }
				, cpr::Body{ body }
				, h
		);
		return { r.status_code, r.text };
	}

	Http::Response HttpCpr::Delete(const std::string& url,
												 const std::string& body /*= "" */,
												 const Headers& headers /*= {} */)
	{
		cpr::Header h;
		for (auto& i : headers)
			h[i.first] = i.second;
		cpr::Response r;
		if (auth_)
			r = cpr::Delete(cpr::Url{ GetBaseUrl() + '/' + url }
				, cpr::Body{ body }
				, h
				, *auth_
			);
		else
			r = cpr::Delete(cpr::Url{ GetBaseUrl() + '/' + url }
				, cpr::Body{ body }
				, h
		);
		return { r.status_code, r.text };
	}

}

