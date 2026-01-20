/*--------------------------------------------------------------------------------------+
|
|     $Source: httpCprImpl.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cpr/cpr.h>
#include "http.h"
#include "httpCprImpl.h"
#include "../Singleton/singleton.h"

namespace AdvViz::SDK
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
	 std::string EncodeForUrl(const std::string & str)
	{
		const auto encoded_value = curl_easy_escape(nullptr, str.c_str(), static_cast<int>(str.length()));
		std::string result(encoded_value);
		curl_free(encoded_value);
		return result;
	}

}

extern "C" {
	CURLcode Curl_base64_decode(const char* src, unsigned char** outptr, size_t* outlen);
}

namespace AdvViz::SDK::Impl
{

	void HttpCpr::SetBasicAuth(const char* login, const char* passwd)
	{
		auth_ = std::make_unique<cpr::Authentication>(login, passwd, cpr::AuthMode::BASIC);
	}

	bool HttpCpr::DecodeBase64(const std::string& src, RawData& buffer) const
	{
		unsigned char* buf = nullptr;
		size_t bufLen = 0;
		CURLcode const res = Curl_base64_decode(src.c_str(), &buf, &bufLen);
		bool const bSuccess = res == CURLE_OK && buf && bufLen > 0;
		if (bSuccess)
		{
			buffer.clear();
			buffer.reserve(bufLen);
			std::transform(
				buf, buf + bufLen,
				std::back_inserter(buffer),
				[](unsigned char c) noexcept { return static_cast<uint8_t>(c); });
		}
		free(buf);
		return bSuccess;
	}

	Http::Response HttpCpr::DoPut(const std::string& url,
								  const BodyParams& body /*= {} */,
								  const Headers& headers /*= {} */)
	{
		cpr::Header h;
		for (auto& i : headers)
			h[i.first] = i.second;
		cpr::Response r;
		if (auth_)
			r = cpr::Put(cpr::Url{ GetBaseUrlStr() + '/' + url }
				, cpr::Body{ body.str() }
				, h
				, *auth_
			);
		else
			r = cpr::Put(cpr::Url{ GetBaseUrlStr() + '/' + url }
				, cpr::Body{ body.str() }
				, h
		);
		return Response(r.status_code, std::move(r.text));
	}

	Http::Response HttpCpr::DoPutBinaryFile(const std::string& url,
		const std::string& filePath, const Headers& headers /*= {}*/)
	{
		cpr::Header h;
		for (auto& i : headers)
			h[i.first] = i.second;
		cpr::Response r;
		if (auth_)
			r = cpr::Put(cpr::Url{ GetBaseUrlStr() + '/' + url }
				, cpr::Body(cpr::File(filePath))
				, h
				, *auth_
			);
		else
			r = cpr::Put(cpr::Url{ GetBaseUrlStr() + '/' + url }
				, cpr::Body(cpr::File(filePath))
				, h
			);
		return Response(r.status_code, std::move(r.text));
	}

	Http::Response HttpCpr::DoPatch(const std::string& url,
									const BodyParams& body /*= {} */,
									const Headers& headers /*= {} */)
	{
		cpr::Header h;
		for (auto& i : headers)
			h[i.first] = i.second;
		cpr::Response r;
		if (auth_)
			r = cpr::Patch(cpr::Url{ GetBaseUrlStr() + '/' + url }
				, cpr::Body{ body.str() }
				, h
				, *auth_
			);
		else
			r = cpr::Patch(cpr::Url{ GetBaseUrlStr() + '/' + url }
				, cpr::Body{ body.str() }
				, h
		);
		return Response(r.status_code, std::move(r.text));
	}

	Http::Response HttpCpr::DoPost(const std::string& url,
								   const BodyParams& body /*= {} */,
								   const Headers& headers /*= {} */)
	{
		cpr::Header h;
		for (auto& i : headers)
			h[i.first] = i.second;
		cpr::Response r;
		if (auth_)
			r = cpr::Post(cpr::Url{ GetBaseUrlStr() + '/' + url }
				, cpr::Body{ body.str() }
				, h
				, *auth_
			);
		else
			r = cpr::Post(cpr::Url{ GetBaseUrlStr() + '/' + url }
				, cpr::Body{ body.str() }
				, h
		);
		return Response(r.status_code, std::move(r.text));
	}

	void HttpCpr::DoAsyncPost(std::function<void(const Response&)> callback, const std::string& url, const BodyParams& body, const Headers& headers)
	{
		cpr::Header h;
		for (auto& i : headers)
			h[i.first] = i.second;
		cpr::Response r;
		cpr::PostCallback([callback](cpr::Response r) {
			Response resp(r.status_code, std::move(r.text));
			callback(resp);
			}
			, cpr::Url{ GetBaseUrlStr() + '/' + url}
			, cpr::Body{ body.str() }
		, h);
	}

	void HttpCpr::DoAsyncPut(std::function<void(const Response&)> callback, const std::string& url, const BodyParams& body, const Headers& headers)
	{
		cpr::Header h;
		for (auto& i : headers)
			h[i.first] = i.second;
		cpr::Response r;
		cpr::PutCallback([callback](cpr::Response r) {
			Response resp(r.status_code, std::move(r.text));
			callback(resp);
			}
			, cpr::Url{ GetBaseUrlStr() + '/' + url }
			, cpr::Body{ body.str() }
		, h);
	}

	Http::Response HttpCpr::DoPostFile(const std::string& url,
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
			r = cpr::Post(cpr::Url{ GetBaseUrlStr() + '/' + url }
				, multipart
				, h
				, *auth_
			);
		else
			r = cpr::Post(cpr::Url{ GetBaseUrlStr() + '/' + url }
				, multipart
				, h
			);
		return Response(r.status_code, std::move(r.text));
	}

	Http::Response HttpCpr::DoGet(const std::string& url,
								  const Headers& headers /*= {} */,
								  bool isFullUrl /*= false*/)
	{
		cpr::Header h;
		for (auto& i : headers)
			h[i.first] = i.second;
		cpr::Response r;
		if (auth_)
			r = cpr::Get(cpr::Url{ isFullUrl ? url : (GetBaseUrlStr() + '/' + url) }
				, h
				, *auth_
			);
		else
			r = cpr::Get(cpr::Url{ isFullUrl ? url : (GetBaseUrlStr() + '/' + url) }
				, h
			);
		return Response(r.status_code, std::move(r.text));
	}


	void HttpCpr::DoAsyncGet(std::function<void(const Response&)> callback, const std::string& url, const Headers& headers, bool isFullUrl)
	{
		cpr::Header h;
		for (auto& i : headers)
			h[i.first] = i.second;
		cpr::Response r;
		cpr::GetCallback([callback](cpr::Response r) {
				Response resp(r.status_code, std::move(r.text));
				callback(resp);
			}, 
			cpr::Url{ isFullUrl ? url : (GetBaseUrlStr() + '/' + url) }
			, h);
	}

	Http::Response HttpCpr::DoDelete(const std::string& url,
									const BodyParams& body /*= {} */,
									const Headers& headers /*= {} */)
	{

		cpr::Header h;
		for (auto& i : headers)
			h[i.first] = i.second;
		cpr::Response r;
		if (auth_)
			r = cpr::Delete(cpr::Url{ GetBaseUrlStr() + '/' + url }
				, cpr::Body{ body.str() }
				, h
				, *auth_
			);
		else
			r = cpr::Delete(cpr::Url{ GetBaseUrlStr() + '/' + url }
				, cpr::Body{ body.str() }
				, h
		);
		return Response(r.status_code, std::move(r.text));
	}

	std::string HttpCpr::GetBaseUrlStr() const
	{
		return baseUrl_;
	}

}

