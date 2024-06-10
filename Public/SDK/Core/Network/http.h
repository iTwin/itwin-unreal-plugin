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

MODULE_EXPORT namespace SDK::Core {

	class Http: public std::enable_shared_from_this<Http>
	{
	public:
		static std::shared_ptr<Http> New();

		typedef std::vector<std::pair<std::string, std::string>> Headers;

		void SetBaseUrl(const std::string& url);
		void SetBasicAuth(const std::string& login, const std::string& passwd);
		std::pair<long, std::string> Delete(const std::string& url, const std::string& body, const Headers& h = Headers());
		std::pair<long, std::string> Get(const std::string& url, const std::string& body, const Headers& h = Headers());
		std::pair<long, std::string> Patch(const std::string& url, const std::string& body, const Headers& h = Headers());
		std::pair<long, std::string> Post(const std::string& url, const std::string& body, const Headers& h = Headers());
		std::pair<long, std::string> Put(const std::string& url, const std::string& body, const Headers& h = Headers());

		std::pair<long, std::string> GetJson(const std::string& url, const std::string& body, const Headers& h = Headers());
		std::pair<long, std::string> PatchJson(const std::string& url, const std::string& body, const Headers& h = Headers());
		std::pair<long, std::string> PostJson(const std::string& url, const std::string& body, const Headers& h = Headers());
		std::pair<long, std::string> PutJson(const std::string& url, const std::string& body, const Headers& h = Headers());

		template<typename Type>
		long GetJson(Type& t, const std::string& url, const std::string& body)
		{
			std::pair<long, std::string> r = GetJson(url, body);
			if (r.first == 200)
				Json::FromString(t, r.second);
			return r.first;
		}

		template<typename Type, typename TypeBody>
		long GetJsonJBody(Type& t, const std::string& url, const TypeBody& body)
		{
			std::string bodyStr = Json::ToString(body);
			return GetJson<Type>(t, url, bodyStr);
		}

		template<typename Type>
		long PutJson(Type& t, const std::string& url, const std::string& body)
		{
			std::pair<long, std::string> r = PutJson(url, body);
			if (r.first == 200)
				Json::FromString(t, r.second);
			return r.first;
		}

		template<typename Type, typename TypeBody>
		long PutJsonJBody(Type& t, const std::string& url, const TypeBody& body)
		{
			std::string bodyStr = Json::ToString(body);
			return PutJson<Type>(t, url, bodyStr);
		}

		template<typename Type>
		long PatchJson(Type& t, const std::string& url, const std::string& body)
		{
			std::pair<long, std::string> r = PatchJson(url, body);
			if (r.first == 200)
				Json::FromString<Type>(t, r.second);
			return r.first;
		}

		template<typename Type, typename TypeBody>
		long PatchJsonJBody(Type& t, const std::string& url, const TypeBody& body)
		{
			std::string bodyStr = Json::ToString(body);
			return PatchJson<Type>(t, url, bodyStr);
		}

		template<typename Type>
		long PostJson(Type& t, const std::string& url, const std::string& body)
		{
			std::pair<long, std::string> r = PostJson(url, body);
			if (r.first == 200 || r.first == 201)
				Json::FromString<Type>(t, r.second);
			return r.first;
		}

		template<typename Type, typename TypeBody>
		long PostJsonJBody(Type& t, const std::string& url, const TypeBody& body)
		{
			std::string bodyStr = Json::ToString(body);
			return PostJson<Type>(t, url, bodyStr);
		}

		virtual ~Http();

	protected:
		Http();
	};

}
