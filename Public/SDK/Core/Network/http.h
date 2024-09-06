/*--------------------------------------------------------------------------------------+
|
|     $Source: http.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#ifndef SDK_CPPMODULES
	#include <memory>
	#include <string>
	#include <vector>
	#include "Core/Json/Json.h"
	#ifndef MODULE_EXPORT
		#define MODULE_EXPORT
	#endif // !MODULE_EXPORT
#endif

#include <Core/Tools/Tools.h>

MODULE_EXPORT namespace SDK::Core {

	class Http: public Tools::Factory<Http>, public Tools::ExtensionSupport, public std::enable_shared_from_this<Http>
	{
	public:
		using Headers = std::vector<std::pair<std::string, std::string>>;
		using Response = std::pair<long, std::string>;

		void SetBaseUrl(const std::string& url);
		std::string const& GetBaseUrl() const { return baseUrl_; }

		virtual void SetBasicAuth(const std::string& login, const std::string& passwd) = 0;
		virtual Response Get(const std::string& url, const std::string& body, const Headers& h = {}, bool isFullUrl = false) = 0;
		virtual Response Patch(const std::string& url, const std::string& body, const Headers& h = {}) = 0;
		virtual Response Post(const std::string& url, const std::string& body, const Headers& h = {}) = 0;
		virtual Response Put(const std::string& url, const std::string& body, const Headers& h = {}) = 0;
		virtual Response Delete(const std::string& url, const std::string& body, const Headers& h = {}) = 0;

		Response GetJson(const std::string& url, const std::string& body, const Headers& h = {}, bool isFullUrl = false);
		Response PatchJson(const std::string& url, const std::string& body, const Headers& h = {});
		Response PostJson(const std::string& url, const std::string& body, const Headers& h = {});
		Response PutJson(const std::string& url, const std::string& body, const Headers& h = {});
		Response DeleteJson(const std::string& url, const std::string& body, const Headers& h = {});

		template<typename Type>
		long GetJson(Type& t, const std::string& url, const std::string& body, const Headers& h = {}, bool isFullUrl = false)
		{
			Response r = GetJson(url, body, h, isFullUrl);
			if (r.first == 200)
				Json::FromString(t, r.second);
			return r.first;
		}

		template<typename Type, typename TypeBody>
		long GetJsonJBody(Type& t, const std::string& url, const TypeBody& body, const Headers& h = {}, bool isFullUrl = false)
		{
			std::string bodyStr = Json::ToString(body);
			return GetJson<Type>(t, url, bodyStr, h, isFullUrl);
		}

		template<typename Type>
		long PutJson(Type& t, const std::string& url, const std::string& body, const Headers& h = {})
		{
			Response r = PutJson(url, body, h);
			if (r.first == 200)
				Json::FromString(t, r.second);
			return r.first;
		}

		template<typename Type, typename TypeBody>
		long PutJsonJBody(Type& t, const std::string& url, const TypeBody& body, const Headers& h = {})
		{
			std::string bodyStr = Json::ToString(body);
			return PutJson<Type>(t, url, bodyStr, h);
		}

		template<typename Type>
		long PatchJson(Type& t, const std::string& url, const std::string& body, const Headers& h = {})
		{
			Response r = PatchJson(url, body, h);
			if (r.first == 200)
				Json::FromString<Type>(t, r.second);
			return r.first;
		}

		template<typename Type, typename TypeBody>
		long PatchJsonJBody(Type& t, const std::string& url, const TypeBody& body, const Headers& h = {})
		{
			std::string bodyStr = Json::ToString(body);
			return PatchJson<Type>(t, url, bodyStr, h);
		}

		template<typename Type>
		long PostJson(Type& t, const std::string& url, const std::string& body, const Headers& h = {})
		{
			Response r = PostJson(url, body, h);
			if (r.first == 200 || r.first == 201)
				Json::FromString<Type>(t, r.second);
			return r.first;
		}

		template<typename Type, typename TypeBody>
		long PostJsonJBody(Type& t, const std::string& url, const TypeBody& body, const Headers& h = {})
		{
			std::string bodyStr = Json::ToString(body);
			return PostJson<Type>(t, url, bodyStr, h);
		}

		template<typename Type>
		long DeleteJson(Type& t, const std::string& url, const std::string& body, const Headers& h = {})
		{
			std::pair<long, std::string> r = DeleteJson(url, body, h);
			if (r.first == 200)
				Json::FromString(t, r.second);
			return r.first;
		}

		template<typename Type, typename TypeBody>
		long DeleteJsonJBody(Type& t, const std::string& url, const TypeBody& body, const Headers& h = {})
		{
			std::string bodyStr = Json::ToString(body);
			return DeleteJson<Type>(t, url, bodyStr, h);
		}

		virtual ~Http();

	protected:
		Http();

	private:
		std::string baseUrl_; // base URL
	};

}
