/*--------------------------------------------------------------------------------------+
|
|     $Source: httpCprImpl.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cpr/cpr.h>
#include "http.h"
#include "httpCprImpl.h"
#include "../Singleton/singleton.h"

namespace SDK::Core
{
	template<>
	Tools::Factory<Http>::Globals::Globals()
	{
		newFct_ = []() { return static_cast<Http*>(new Impl::HttpCpr()); };
	}

	template<>
	Tools::Factory<Http>::Globals& Tools::Factory<Http>::GetGlobals()
	{
		return singleton<Tools::Factory<Http>::Globals>();
	}
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

	Http::Response HttpCpr::PutBinaryFile(const std::string& url,
		const std::string& filePath, const Headers& headers /*= {}*/)
	{
		cpr::Header h;
		for (auto& i : headers)
			h[i.first] = i.second;
		cpr::Response r;
		if (auth_)
			r = cpr::Put(cpr::Url{ GetBaseUrl() + '/' + url }
				, cpr::Body(cpr::File(filePath))
				, h
				, *auth_
			);
		else
			r = cpr::Put(cpr::Url{ GetBaseUrl() + '/' + url }
				, cpr::Body(cpr::File(filePath))
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


	Http::Response HttpCpr::PostFile(const std::string& url,
		const std::string& fileParamName, const std::string& filePath,
		const KeyValueVector& extraParams /*= {}*/, const Headers& headers /*= {}*/)
	{
		cpr::Header h;
		for (auto& i : headers)
			h[i.first] = i.second;
		cpr::Multipart multipart({});
		multipart.parts.reserve(1 + extraParams.size());
		for (auto& i : extraParams)
			multipart.parts.emplace_back(i.first, i.second);
		multipart.parts.emplace_back(fileParamName, cpr::File{ filePath });
		cpr::Response r;
		if (auth_)
			r = cpr::Post(cpr::Url{ GetBaseUrl() + '/' + url }
				, multipart
				, h
				, *auth_
			);
		else
			r = cpr::Post(cpr::Url{ GetBaseUrl() + '/' + url }
				, multipart
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

