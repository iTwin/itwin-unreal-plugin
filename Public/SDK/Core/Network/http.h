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

		virtual void SetBaseUrl(const std::string& url) = 0;
		virtual void SetBasicAuth(const std::string& login, const std::string& passwd) = 0;
		virtual std::pair<long, std::string> Delete(const std::string& url, const std::string& body, const Headers& h = {}) = 0;
		virtual std::pair<long, std::string> Get(const std::string& url, const std::string& body, const Headers& h = {}) = 0;
		virtual std::pair<long, std::string> Patch(const std::string& url, const std::string& body, const Headers& h = {}) = 0;
		virtual std::pair<long, std::string> Post(const std::string& url, const std::string& body, const Headers& h = {}) = 0;
		virtual std::pair<long, std::string> Put(const std::string& url, const std::string& body, const Headers& h = {}) = 0;

		std::pair<long, std::string> GetJson(const std::string& url, const std::string& body, const Headers& h = {});
		std::pair<long, std::string> PatchJson(const std::string& url, const std::string& body, const Headers& h = {});
		std::pair<long, std::string> PostJson(const std::string& url, const std::string& body, const Headers& h = {});
		std::pair<long, std::string> PutJson(const std::string& url, const std::string& body, const Headers& h = {});

		template<typename Type>
		long GetJson(Type& t, const std::string& url, const std::string& body, const Headers& h = {})
		{
			std::pair<long, std::string> r = GetJson(url, body, h);
			if (r.first == 200)
				Json::FromString(t, r.second);
			return r.first;
		}

		template<typename Type, typename TypeBody>
		long GetJsonJBody(Type& t, const std::string& url, const TypeBody& body, const Headers& h = {})
		{
			std::string bodyStr = Json::ToString(body);
			return GetJson<Type>(t, url, bodyStr, h);
		}

		template<typename Type>
		long PutJson(Type& t, const std::string& url, const std::string& body, const Headers& h = {})
		{
			std::pair<long, std::string> r = PutJson(url, body, h);
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
			std::pair<long, std::string> r = PatchJson(url, body, h);
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
			std::pair<long, std::string> r = PostJson(url, body, h);
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

		virtual ~Http();

	protected:
		Http();
	};

}
