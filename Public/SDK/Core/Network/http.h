/*--------------------------------------------------------------------------------------+
|
|     $Source: http.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <shared_mutex>
#include <Core/Json/Json.h>
#include <Core/Tools/Tools.h>
#include "Core/Json/Json.h"


namespace AdvViz::SDK {

	class ADVVIZ_LINK Http: public Tools::Factory<Http>, public Tools::ExtensionSupport
	{
	public:
		using KeyValueVector = std::vector<std::pair<std::string, std::string>>;
		using Headers = KeyValueVector;

		using RawData = std::vector<uint8_t>;
		using RawDataPtr = std::shared_ptr<RawData>;

		// using Response = std::pair<long, std::string>;
		//	-> keeping first/second below to avoid having to change all calls...
		struct Response
		{
			long first = 0; // stands for Unknown
			std::string second;
			RawDataPtr rawdata_; // only provided if the request asks it

			Response() = default;
			inline Response(long status_code, std::string&& response_text)
				: first(status_code), second(std::move(response_text)) {
			}
		};

		/// Returns whether the response corresponds to a successful request.
		static inline bool IsSuccessful(long httpCode) {
			// consider 2XX responses as successful.
			// (see https://en.wikipedia.org/wiki/List_of_HTTP_status_codes)
			return httpCode >= 200 && httpCode < 300;
		}

		/// Returns whether the response corresponds to a successful request.
		static inline bool IsSuccessful(Response const& response) {
			return IsSuccessful(response.first);
		}

		/// Returns whether a response was actually made. It can be an error, though.
		static inline bool IsDefined(Response const& response) {
			return response.first > 0;
		}


		void SetBaseUrl(const char* url);
		const char* GetBaseUrl() const;

		virtual void SetBasicAuth(const char* login, const char* passwd) = 0;
		virtual bool DecodeBase64(const std::string& src, RawData& buffer) const = 0;

		Response PatchJson(const std::string& url, const std::string& body, const Headers& h = {});
		Response PostJson(const std::string& url, const std::string& body, const Headers& h = {});
		Response PutJson(const std::string& url, const std::string& body, const Headers& h = {});
		Response DeleteJson(const std::string& url, const std::string& body, const Headers& h = {});





		Response Get(const std::string& url, const Headers& h = {}, bool isFullUrl = false);
		Response Patch(const std::string& url, const std::string& body, const Headers& h = {});
		Response Post(const std::string& url, const std::string& body, const Headers& h = {});
		Response PostFile(const std::string& url, const std::string& fileParamName, const std::string& filePath,
			const KeyValueVector& extraParams = {}, const Headers& h = {});
		Response Put(const std::string& url, const std::string& body, const Headers& h = {});
		Response PutBinaryFile(const std::string& url, const std::string& filePath, const Headers& headers = {}) ;
		Response Delete(const std::string& url, const std::string& body, const Headers& h = {});


		protected:

		virtual Response DoGet(const std::string& url, const Headers& h = {}, bool isFullUrl = false) = 0;
		virtual void DoAsyncGet(std::function<void(const Response&)> callback, const std::string& url, const Headers& h = {}, bool isFullUrl = false) = 0;
		virtual Response DoPatch(const std::string& url, const std::string& body, const Headers& h = {}) = 0;
		virtual Response DoPost(const std::string& url, const std::string& body, const Headers& h = {}) = 0;
		virtual void DoAsyncPost(std::function<void(const Response&)> callback, const std::string& url, const std::string& body = "", const Headers& headers = {}) = 0;
		virtual Response DoPostFile(const std::string& url, const std::string& fileParamName, const std::string& filePath,
			const KeyValueVector& extraParams = {}, const Headers& h = {}) = 0;
		virtual Response DoPut(const std::string& url, const std::string& body, const Headers& h = {}) = 0;
		virtual void DoAsyncPut(std::function<void(const Response&)> callback, const std::string& url, const std::string& body = "", const Headers& headers = {}) = 0;
		virtual Response DoPutBinaryFile(const std::string& url, const std::string& filePath, const Headers& headers = {}) = 0;
		virtual Response DoDelete(const std::string& url, const std::string& body, const Headers& h = {}) = 0;

		friend class HttpRequest;

	public:
		Response GetJsonStr(const std::string& url, const Headers& h = {}, bool isFullUrl = false);

		template<typename Type>
		inline long GetJson(Type& t, const std::string& url, const Headers& h = {}, bool isFullUrl = false)
		{
			Response r(GetJsonStr(url, h, isFullUrl));
			if (IsSuccessful(r))
				Json::FromString(t, r.second);
			else
				BE_LOGE("http", "GetJson failed code:" << r.first<<" url:" << url << " body out:" << r.second);
			return r.first;
		}

		template<typename TFunctor>
		inline void AsyncGet(const TFunctor &fct, const std::string& url, const Headers& h = {}, bool isFullUrl = false)
		{
			if (accessToken_)
				h.emplace_back("Authorization", std::string("Bearer ") + *accessToken_);
			DoAsyncGet(fct, url, h, isFullUrl);
		}

		template<typename Type, typename TFunctor>
		inline void AsyncGetJson(const Tools::TSharedLockableDataPtr<Type>& sharedData, const TFunctor &fct, const std::string& url, const Headers& h = {}, bool isFullUrl = false)
		{
			AsyncGet([sharedData, fct, url](const Response& r) {
				{
					auto dataLock(sharedData.GetAutoLock());
					if (IsSuccessful(r))
						Json::FromString(*dataLock.Get(), r.second);
					else
						BE_LOGE("http", "AsyncGetJson failed code: " << r.first << " url: " << url << " body out: " << r.second);
				}
				fct(r.first, sharedData);
			}
			,url
			, h
			, isFullUrl
			);
		}

		template<typename Type>
		inline long PutJson(Type& t, const std::string& url, const std::string& body, const Headers& h = {})
		{
			Response r(PutJson(url, body, h));
			if (IsSuccessful(r))
				Json::FromString(t, r.second);
			else
				BE_LOGE("http", "PutJson failed code:"<< r.first << " url:"<<url << " body in:" << body << " body out:" << r.second);
			return r.first;
		}

		template<typename Type, typename TypeBody>
		inline long PutJsonJBody(Type& t, const std::string& url, const TypeBody& body, const Headers& h = {})
		{
			std::string bodyStr(Json::ToString(body));
			return PutJson<Type>(t, url, bodyStr, h);
		}

		template<typename TFunctor>
		inline void AsyncPut(const TFunctor& fct, const std::string& url, const std::string& body, const Headers& h = {})
		{
			DoAsyncPut(fct, url, body, h);
		}

		template<typename Type, typename TFunctor>
		inline void AsyncPutJson(const Tools::TSharedLockableDataPtr<Type>& sharedData, const TFunctor& fct, const std::string& url, const std::string& body, const Headers& hi = {})
		{
			Headers h(hi);
			h.emplace_back("accept", "application/json");
			h.emplace_back("Content-Type", "application/json; charset=UTF-8");
			AsyncPut([sharedData, fct, url](const Response& r) {
				{
					auto dataLock(sharedData->GetAutoLock());
					if (IsSuccessful(r))
						Json::FromString(dataLock.Get(), r.second);
					else
						BE_LOGE("http", "AsyncGetJson failed code:" << r.first << " url:" << url << " body out:" << r.second);
				}
				fct(r.first, sharedData);
				}
			, url, body, h);
		}

		template<typename Type, typename TFunctor, typename TypeBody>
		inline void AsyncPutJsonJBody(const Tools::TSharedLockableDataPtr<Type>& sharedData, const TFunctor& fct, const std::string& url, const TypeBody& body, const Headers& h = {})
		{
			std::string bodyStr(Json::ToString(body));
			AsyncPutJson<Type>(sharedData, fct, url, bodyStr, h);
		}

		template<typename Type, typename TFunctor>
		inline void AsyncPutJson(const Tools::TSharedLockableData<Type>& sharedData, const TFunctor& fct, const std::string& url, const std::string& body, const Headers& hi = {})
		{
			Headers h(hi);
			h.emplace_back("accept", "application/json");
			h.emplace_back("Content-Type", "application/json; charset=UTF-8");
			if (accessToken_)
				h.emplace_back("Authorization", std::string("Bearer ") + *accessToken_);
			AsyncPut([sharedData, fct, url](const Response& r) {
				{
					auto dataLock(sharedData->GetAutoLock());
					if (IsSuccessful(r))
						Json::FromString(dataLock.Get(), r.second);
					else
						BE_LOGE("http", "AsyncGetJson failed code:" << r.first << " url:" << url << " body out:" << r.second);
				}
				fct(r.first, sharedData);
				}
			, url, body, h);
		}

		template<typename Type, typename TFunctor, typename TypeBody>
		inline void AsyncPutJsonJBody(const Tools::TSharedLockableData<Type>& sharedData, const TFunctor& fct, const std::string& url, const TypeBody& body, const Headers& h = {})
		{
			std::string bodyStr(Json::ToString(body));
			AsyncPutJson<Type>(sharedData, fct, url, bodyStr, h);
		}



		template<typename Type>
		inline long PatchJson(Type& t, const std::string& url, const std::string& body, const Headers& h = {})
		{
			Response r(PatchJson(url, body, h));
			if (IsSuccessful(r))
				Json::FromString<Type>(t, r.second);
			else
				BE_LOGE("http", "PatchJson failed code:" << r.first << " url:" << url << " body in:" << body << " body out:" << r.second);
			return r.first;
		}

		template<typename Type, typename TypeBody>
		inline long PatchJsonJBody(Type& t, const std::string& url, const TypeBody& body, const Headers& h = {})
		{
			std::string bodyStr(Json::ToString(body));
			return PatchJson<Type>(t, url, bodyStr, h);
		}
		template<typename Type>
		inline long PostJson(Type& t, const std::string& url, const std::string& body, const Headers& h = {})
		{
			Response r(PostJson(url, body, h));
			if (IsSuccessful(r))
				Json::FromString<Type>(t, r.second);
			else
				BE_LOGE("http", "PostJson failed code:" << r.first << " url:" << url << " body in:" << body << " body out:" << r.second);
			return r.first;
		}
		
		template<typename Type, typename TypeBody>
		inline long PostJsonJBody(Type& t, const std::string& url, const TypeBody& body, const Headers& h = {})
		{
			std::string bodyStr(Json::ToString(body));
			return PostJson<Type>(t, url, bodyStr, h);
		}
		

		template<typename TFunctor>
		inline void AsyncPost(const TFunctor& fct, const std::string& url, const std::string& body, const Headers& h = {})
		{
			DoAsyncPost(fct, url, body, h);
		}

		template<typename Type, typename TFunctor>
		inline void AsyncPostJson(const Tools::TSharedLockableDataPtr<Type>& sharedData, const TFunctor& fct, const std::string& url, const std::string& body, const Headers& hi = {})
		{
			Headers h(hi);
			h.emplace_back("accept", "application/json");
			h.emplace_back("Content-Type", "application/json; charset=UTF-8");
			AsyncPost([sharedData, fct, url](const Response& r) {
					{
						auto dataLock(sharedData->GetAutoLock());
						if (IsSuccessful(r))
							Json::FromString(dataLock.Get(), r.second);
						else
							BE_LOGE("http", "AsyncGetJson failed code:" << r.first << " url:" << url << " body out:" << r.second);
					}
					fct(r.first, sharedData);
				}
				,url,body,h);
		}

		template<typename Type, typename TFunctor, typename TypeBody>
		inline void AsyncPostJsonJBody(const Tools::TSharedLockableDataPtr<Type>& sharedData, const TFunctor& fct, const std::string& url, const TypeBody& body, const Headers& h = {})
		{
			std::string bodyStr(Json::ToString(body));
			AsyncPostJson<Type>(sharedData, fct, url, bodyStr, h);
		}

		template<typename Type, typename TFunctor>
		inline void AsyncPostJson(const Tools::TSharedLockableData<Type>& sharedData, const TFunctor& fct, const std::string& url, const std::string& body, const Headers& hi = {})
		{
			Headers h(hi);
			h.emplace_back("accept", "application/json");
			h.emplace_back("Content-Type", "application/json; charset=UTF-8");
			if (accessToken_)
				h.emplace_back("Authorization", std::string("Bearer ") + *accessToken_);
			AsyncPost([sharedData, fct, url](const Response& r) {
					{
						auto dataLock(sharedData->GetAutoLock());
						if (IsSuccessful(r))
							Json::FromString(dataLock.Get(), r.second);
						else
							BE_LOGE("http", "AsyncGetJson failed code:" << r.first << " url:" << url << " body out:" << r.second);
					}
					fct(r.first, sharedData);
				}
			, url, body, h);
		}

		template<typename Type, typename TFunctor, typename TypeBody>
		inline void AsyncPostJsonJBody(const Tools::TSharedLockableData<Type>& sharedData, const TFunctor& fct, const std::string& url, const TypeBody& body, const Headers& h = {})
		{
			std::string bodyStr(Json::ToString(body));
			AsyncPostJson<Type>(sharedData, fct, url, bodyStr, h);
		}

		template<typename Type>
		inline long DeleteJson(Type& t, const std::string& url, const std::string& body, const Headers& h = {})
		{
			Response r(DeleteJson(url, body, h));
			if (IsSuccessful(r))
				Json::FromString(t, r.second);
			else
				BE_LOGE("http", "DeleteJson failed code:" << r.first << " url:" << url << " body in:" << body << " body out:" << r.second);
			return r.first;
		}

		template<typename Type, typename TypeBody>
		inline long DeleteJsonJBody(Type& t, const std::string& url, const TypeBody& body, const Headers& h = {})
		{
			std::string bodyStr(Json::ToString(body));
			return DeleteJson<Type>(t, url, bodyStr, h);
		}



		virtual ~Http();

		void SetAccessToken(std::shared_ptr<std::string> p) { accessToken_ = p; }
		std::shared_ptr<std::string> GetAccessToken() const { return accessToken_; }

		const std::string& GetBaseUrlStr() const { return baseUrl_; }

	protected:
		Http();

	protected:
		std::string baseUrl_; // base URL
		std::shared_ptr<std::string> accessToken_;
	};
	//explicit declaration to avoid a warning
	template<>
	ADVVIZ_LINK Tools::Factory<Http>::Globals& Tools::Factory<Http>::GetGlobals();


	ADVVIZ_LINK std::string EncodeForUrl(std::string const& str);

}
