/*--------------------------------------------------------------------------------------+
|
|     $Source: http.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <shared_mutex>
#include <Core/Json/Json.h>
#include <Core/Network/HttpError.h>
#include <Core/Tools/Tools.h>

namespace AdvViz::SDK {

	class ADVVIZ_LINK ThreadSafeAccessToken
	{
	public:
		void Set(const std::string& value) {
			token_.store(std::make_shared<const std::string>(value));
		}

		// we return a shared ptr instead of copying the string on each Get.
		std::shared_ptr<const std::string> Get() const {
			return token_.load();
		}

		bool IsEmpty() const {
			auto tokenPtr = token_.load();
			return !tokenPtr || tokenPtr->empty();
		}

	private:
		std::atomic<std::shared_ptr<const std::string>> token_;
	};

	class ADVVIZ_LINK Http: public Tools::Factory<Http>, public Tools::ExtensionSupport
	{
	public:
		using KeyValueVector = std::vector<std::pair<std::string, std::string>>;
		using Headers = KeyValueVector;

		using RawData = std::vector<uint8_t>;
		using RawDataPtr = std::shared_ptr<RawData>;
		using HeadersPtr = std::unique_ptr<Headers>;

		// using Response = std::pair<long, std::string>;
		//	-> keeping first/second below to avoid having to change all calls...
		struct Response
		{
			long first = 0; // stands for Unknown
			std::string second;
			RawDataPtr rawdata_; // only provided if the request asks it
			HeadersPtr headers_;

			Response(const Response& other) = default;
			Response(Response&& other) = default;
			Response& operator=(const Response& other) = default;
			Response& operator=(Response&& other) = default;

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

		/// In asynchronous mode, we may prefer the callback being executed in the main thread (in Unreal,
		/// typically).
		/// Not all implementations will support this (see Http::SupportsExecuteAsyncCallbackInMainThread).
		enum class EAsyncCallbackExecutionMode : uint8_t
		{
			WorkerThread = 0,
			Default = WorkerThread,
			MainThread,
			GameThread = MainThread, /* convenient alias for Unreal world */
		};

		virtual ~Http();

		void SetAccessToken(std::shared_ptr<ThreadSafeAccessToken> p) { accessToken_ = p; }
		std::shared_ptr<ThreadSafeAccessToken> GetAccessToken() const { return accessToken_; }

		void SetBaseUrl(const char* url);
		const char* GetBaseUrl() const;
		const std::string& GetBaseUrlStr() const { return baseUrl_; }
		virtual std::string EncodeForUrl(const std::string& str) const = 0;


		virtual void SetBasicAuth(const char* login, const char* passwd) = 0;
		virtual bool DecodeBase64(const std::string& src, RawData& buffer) const = 0;

		/// Returns true if this class of implementation supports executing the callbacks of asynchronous
		/// in the main thread (as Unreal can do, typically).
		virtual bool SupportsExecuteAsyncCallbackInMainThread() const = 0;

		using BodyParams = Tools::StringWithEncoding;

		Response PatchJson(const std::string& url, const BodyParams& body, const Headers& h = {});
		Response PostJson(const std::string& url, const BodyParams& body, const Headers& h = {});
		Response PutJson(const std::string& url, const BodyParams& body, const Headers& h = {});
		Response DeleteJson(const std::string& url, const BodyParams& body, const Headers& h = {});


		Response Get(const std::string& url, const Headers& h = {}, bool isFullUrl = false);
		Response Patch(const std::string& url, const BodyParams& body, const Headers& h = {});
		Response Post(const std::string& url, const BodyParams& body, const Headers& h = {});
		Response PostFile(const std::string& url, const std::string& fileParamName, const std::string& filePath,
			const KeyValueVector& extraParams = {}, const Headers& h = {});
		Response Put(const std::string& url, const BodyParams& body, const Headers& h = {});
		Response PutBinaryFile(const std::string& url, const std::string& filePath, const Headers& headers = {});
		Response Delete(const std::string& url, const BodyParams& body, const Headers& h = {});


	protected:

		virtual Response DoGet(const std::string& url, const Headers& h = {}, bool isFullUrl = false) = 0;
		virtual void DoAsyncGet(std::function<void(const Response&)> callback, const std::string& url,
			const Headers& h = {}, bool isFullUrl = false,
			EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default) = 0;

		virtual Response DoPatch(const std::string& url, const BodyParams& body, const Headers& h = {}) = 0;
		virtual void DoAsyncPatch(std::function<void(const Response&)> callback, const std::string& url,
			const BodyParams& body, const Headers& headers, EAsyncCallbackExecutionMode asyncCBExecMode) = 0;

		virtual Response DoPost(const std::string& url, const BodyParams& body, const Headers& h = {}) = 0;
		virtual void DoAsyncPost(std::function<void(const Response&)> callback, const std::string& url,
			const BodyParams& body, const Headers& headers, EAsyncCallbackExecutionMode asyncCBExecMode) = 0;

		virtual Response DoPostFile(const std::string& url,
			const std::string& fileParamName, const std::string& filePath,
			const KeyValueVector& extraParams = {}, const Headers& h = {}) = 0;
		virtual void DoAsyncPostFile(std::function<void(const Response&)> callback, const std::string& url,
			const std::string& fileParamName, const std::string& filePath,
			const KeyValueVector& extraParams = {}, const Headers& h = {},
			EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default) = 0;

		virtual Response DoPut(const std::string& url, const BodyParams& body, const Headers& h = {}) = 0;
		virtual void DoAsyncPut(std::function<void(const Response&)> callback, const std::string& url,
			const BodyParams& body, const Headers& headers, EAsyncCallbackExecutionMode asyncCBExecMode) = 0;
		virtual Response DoPutBinaryFile(const std::string& url, const std::string& filePath, const Headers& headers = {}) = 0;

		virtual Response DoDelete(const std::string& url, const BodyParams& body, const Headers& h = {}) = 0;
		virtual void DoAsyncDelete(std::function<void(const Response&)> callback, const std::string& url,
			const BodyParams& body = {}, const Headers& headers = {},
			EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default) = 0;

		friend class HttpRequest;

	public:
		Response GetJsonStr(const std::string& url, const Headers& h = {}, bool isFullUrl = false);

		/*--------------------------------------------------------------------------*/
		/* GET variants																*/
		/*---------------------------------------------------------------------------*/
		template<typename Type>
		inline long GetJson(Type& t, const std::string& url, const Headers& h = {}, bool isFullUrl = false)
		{
			Response r(GetJsonStr(url, h, isFullUrl));
			if (IsSuccessful(r))
			{
				if (!Json::FromString(t, r.second))
					return 500; // internal error during json parsing
			}
			else
				BE_LOGE("http", "GetJson failed code:" << r.first<<" url:" << url << " body out:" << r.second);
			return r.first;
		}

		template<typename TFunctor>
		inline void AsyncGet(const TFunctor &fct, const std::string& url, const Headers& hi = {}, bool isFullUrl = false,
			EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default)
		{
			Headers h(hi);
			if (accessToken_ && !accessToken_->IsEmpty())
				h.emplace_back("Authorization", std::string("Bearer ") + *accessToken_->Get());
			DoAsyncGet(fct, url, h, isFullUrl, asyncCBExecMode);
		}

		template<typename Type, typename TFunctor>
		inline void AsyncGetJson(const Tools::TSharedLockableDataPtr<Type>& sharedData, const TFunctor &fct,
			const std::string& url, const Headers& hi = {}, bool isFullUrl = false,
			EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default)
		{
			Headers h(hi);
			h.emplace_back("accept", "application/json");
			h.emplace_back("Content-Type", "application/json; charset=UTF-8");
			if (accessToken_ && !accessToken_->IsEmpty())
				h.emplace_back("Authorization", std::string("Bearer ") + *accessToken_->Get());
			AsyncGet([sharedData, fct, url](const Response& r) {
				expected<Tools::TSharedLockableDataPtr<Type>, std::string> exp;
				if (IsSuccessful(r))
				{
					auto dataLock(sharedData->GetAutoLock());
					bool res = Json::FromString(dataLock.Get(), r.second);
					if (res)
						exp = sharedData;
					else
						exp = make_unexpected("json parse error");
				}
				else
				{
					BE_LOGE("http", "AsyncGetJson failed code: " << r.first << " url: " << url << " body out: " << r.second);
					exp = make_unexpected("AsyncGetJson failed");
				}
				fct(r, exp);
			}
			,url
			, h
			, isFullUrl
			, asyncCBExecMode
			);
		}


		/*--------------------------------------------------------------------------*/
		/* PUT variants																*/
		/*---------------------------------------------------------------------------*/

		template<typename Type>
		inline long PutJson(Type& t, const std::string& url, const BodyParams& body, const Headers& h = {})
		{
			Response r(PutJson(url, body, h));
			if (IsSuccessful(r))
				Json::FromString(t, r.second);
			else
				BE_LOGE("http", "PutJson failed code:"<< r.first << " url:"<<url << " body in:" << body.str() << " body out:" << r.second);
			return r.first;
		}

		template<typename Type, typename TypeBody>
		inline long PutJsonJBody(Type& t, const std::string& url, const TypeBody& body, const Headers& h = {})
		{
			const BodyParams bodyParams(Json::ToString(body));
			return PutJson<Type>(t, url, bodyParams, h);
		}

		template<typename TFunctor>
		inline void AsyncPut(const TFunctor& fct, const std::string& url,
			const BodyParams& body,
			const Headers& h = {},
			EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default)
		{
			DoAsyncPut(fct, url, body, h, asyncCBExecMode);
		}

		template<typename Type, typename TFunctor>
		inline void AsyncPutJson(const Tools::TSharedLockableDataPtr<Type>& sharedData, const TFunctor& fct, const std::string& url,
			const BodyParams& body,
			const Headers& h = {},
			EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default)
		{
			AsyncPutJsonImpl(sharedData, fct, url, body, h, asyncCBExecMode);
		}

		template<typename Type, typename TFunctor>
		inline void AsyncPutJson(const Tools::TSharedLockableData<Type>& sharedData, const TFunctor& fct, const std::string& url,
			const BodyParams& body,
			const Headers& h = {},
			EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default)
		{
			AsyncPutJsonImpl(sharedData, fct, url, body, h, asyncCBExecMode);
		}

		template<typename Type, typename TFunctor, typename TypeBody>
		inline void AsyncPutJsonJBody(const Tools::TSharedLockableDataPtr<Type>& sharedData, const TFunctor& fct, const std::string& url,
			const TypeBody& body,
			const Headers& h = {},
			EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default)
		{
			const BodyParams bodyParams(Json::ToString(body));
			AsyncPutJson<Type>(sharedData, fct, url, bodyParams, h, asyncCBExecMode);
		}

		template<typename Type, typename TFunctor, typename TypeBody>
		inline void AsyncPutJsonJBody(const Tools::TSharedLockableData<Type>& sharedData, const TFunctor& fct, const std::string& url,
			const TypeBody& body,
			const Headers& h = {},
			EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default)
		{
			const BodyParams bodyParams(Json::ToString(body));
			AsyncPutJson<Type>(sharedData, fct, url, bodyParams, h, asyncCBExecMode);
		}


		/*--------------------------------------------------------------------------*/
		/* PATCH variants															*/
		/*---------------------------------------------------------------------------*/

		template<typename Type>
		inline long PatchJson(Type& t, const std::string& url, const BodyParams& body, const Headers& h = {})
		{
			Response r(PatchJson(url, body, h));
			if (IsSuccessful(r))
				Json::FromString<Type>(t, r.second);
			else
				BE_LOGE("http", "PatchJson failed code:" << r.first << " url:" << url << " body in:" << body.str() << " body out:" << r.second);
			return r.first;
		}

		template<typename Type, typename TypeBody>
		inline long PatchJsonJBody(Type& t, const std::string& url, const TypeBody& body, const Headers& h = {})
		{
			const BodyParams bodyParams(Json::ToString(body));
			return PatchJson<Type>(t, url, bodyParams, h);
		}

		template<typename TFunctor>
		inline void AsyncPatch(const TFunctor& fct, const std::string& url, const BodyParams& body,
			const Headers& h = {},
			EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default)
		{
			DoAsyncPatch(fct, url, body, h, asyncCBExecMode);
		}

		template<typename Type, typename TFunctor>
		inline void AsyncPatchJson(const Tools::TSharedLockableDataPtr<Type>& sharedData, const TFunctor& fct, const std::string& url,
			const BodyParams& body,
			const Headers& h = {},
			EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default)
		{
			AsyncPatchJsonImpl(sharedData, fct, url, body, h, asyncCBExecMode);
		}

		template<typename Type, typename TFunctor>
		inline void AsyncPatchJson(const Tools::TSharedLockableData<Type>& sharedData, const TFunctor& fct, const std::string& url,
			const BodyParams& body,
			const Headers& h = {},
			EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default)
		{
			AsyncPatchJsonImpl(sharedData, fct, url, body, h, asyncCBExecMode);
		}

		template<typename Type, typename TFunctor, typename TypeBody>
		inline void AsyncPatchJsonJBody(const Tools::TSharedLockableDataPtr<Type>& sharedData, const TFunctor& fct, const std::string& url,
			const TypeBody& body,
			const Headers& h = {},
			EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default)
		{
			const BodyParams bodyParams(Json::ToString(body));
			AsyncPatchJson<Type>(sharedData, fct, url, bodyParams, h, asyncCBExecMode);
		}

		template<typename Type, typename TFunctor, typename TypeBody>
		inline void AsyncPatchJsonJBody(const Tools::TSharedLockableData<Type>& sharedData, const TFunctor& fct, const std::string& url,
			const TypeBody& body,
			const Headers& h = {},
			EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default)
		{
			const BodyParams bodyParams(Json::ToString(body));
			AsyncPatchJson<Type>(sharedData, fct, url, bodyParams, h, asyncCBExecMode);
		}


		/*--------------------------------------------------------------------------*/
		/* POST variants															*/
		/*---------------------------------------------------------------------------*/

		template<typename Type>
		inline long PostJson(Type& t, const std::string& url, const BodyParams& body, const Headers& h = {})
		{
			Response r(PostJson(url, body, h));
			if (IsSuccessful(r))
				Json::FromString<Type>(t, r.second);
			else
				BE_LOGE("http", "PostJson failed code:" << r.first << " url:" << url << " body in:" << body.str() << " body out:" << r.second);
			return r.first;
		}
		
		template<typename Type, typename TypeBody>
		inline long PostJsonJBody(Type& t, const std::string& url, const TypeBody& body, const Headers& h = {})
		{
			const BodyParams bodyParams(Json::ToString(body));
			return PostJson<Type>(t, url, bodyParams, h);
		}
		

		template<typename TFunctor>
		inline void AsyncPost(const TFunctor& fct, const std::string& url, const BodyParams& body,
			const Headers& h = {},
			EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default)
		{
			DoAsyncPost(fct, url, body, h, asyncCBExecMode);
		}

		template<typename Type, typename TFunctor>
		inline void AsyncPostJson(const Tools::TSharedLockableDataPtr<Type>& sharedData, const TFunctor& fct, const std::string& url,
			const BodyParams& body,
			const Headers& h = {},
			EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default)
		{
			AsyncPostJsonImpl(sharedData, fct, url, body, h, asyncCBExecMode);
		}

		template<typename Type, typename TFunctor>
		inline void AsyncPostJson(const Tools::TSharedLockableData<Type>& sharedData, const TFunctor& fct, const std::string& url,
			const BodyParams& body,
			const Headers& h = {},
			EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default)
		{
			AsyncPostJsonImpl(sharedData, fct, url, body, h, asyncCBExecMode);
		}

		template<typename Type, typename TFunctor, typename TypeBody>
		inline void AsyncPostJsonJBody(const Tools::TSharedLockableDataPtr<Type>& sharedData, const TFunctor& fct, const std::string& url,
			const TypeBody& body,
			const Headers& h = {},
			EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default)
		{
			const BodyParams bodyParams(Json::ToString(body));
			AsyncPostJson<Type>(sharedData, fct, url, bodyParams, h, asyncCBExecMode);
		}

		template<typename Type, typename TFunctor, typename TypeBody>
		inline void AsyncPostJsonJBody(const Tools::TSharedLockableData<Type>& sharedData, const TFunctor& fct, const std::string& url,
			const TypeBody& body,
			const Headers& h = {},
			EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default)
		{
			const BodyParams bodyParams(Json::ToString(body));
			AsyncPostJson<Type>(sharedData, fct, url, bodyParams, h, asyncCBExecMode);
		}

		void AsyncPostFile(std::function<void(const Response&)> callback, const std::string& url,
			const std::string& fileParamName, const std::string& filePath,
			const KeyValueVector& extraParams = {}, const Headers& h = {},
			EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default);


		/*--------------------------------------------------------------------------*/
		/* DELETE variants															*/
		/*---------------------------------------------------------------------------*/

		template<typename Type>
		inline long DeleteJson(Type& t, const std::string& url, const BodyParams& body, const Headers& h = {})
		{
			Response r(DeleteJson(url, body, h));
			if (IsSuccessful(r))
				Json::FromString(t, r.second);
			else
				BE_LOGE("http", "DeleteJson failed code:" << r.first << " url:" << url << " body in:" << body.str() << " body out:" << r.second);
			return r.first;
		}

		template<typename Type, typename TypeBody>
		inline long DeleteJsonJBody(Type& t, const std::string& url, const TypeBody& body, const Headers& h = {})
		{
			const BodyParams bodyParams(Json::ToString(body));
			return DeleteJson<Type>(t, url, bodyParams, h);
		}

		template<typename TFunctor>
		inline void AsyncDelete(const TFunctor& fct, const std::string& url, const BodyParams& body, const Headers& h = {},
			EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default)
		{
			DoAsyncDelete(fct, url, body, h, asyncCBExecMode);
		}

		// \param bIsExpectingOutput can be set to false if Type is an "empty" type, to avoid logging an error
		// (our Json library makes an error when trying to parse an empty string as an empty struct...)
		template<typename Type, typename TFunctor>
		inline void AsyncDeleteJson(const Tools::TSharedLockableData<Type>& sharedData, const TFunctor& fct,
			const std::string& url,
			const BodyParams& body,
			const Headers& hi = {},
			EAsyncCallbackExecutionMode asyncCBExecMode = EAsyncCallbackExecutionMode::Default,
			bool bIsExpectingOutput = true)
		{
			Headers h(hi);
			h.emplace_back("accept", "application/json");
			h.emplace_back("Content-Type", "application/json; charset=UTF-8");
			if (accessToken_ && !accessToken_->IsEmpty())
				h.emplace_back("Authorization", std::string("Bearer ") + *accessToken_->Get());
			AsyncDelete([sharedData, fct, url, bIsExpectingOutput](const Response& r) {
				{
					auto dataLock(sharedData->GetAutoLock());
					if (IsSuccessful(r))
					{
						if (bIsExpectingOutput || !r.second.empty())
							Json::FromString(dataLock.Get(), r.second);
					}
					else
					{
						BE_LOGE("http", "AsyncDeleteJson failed code:" << r.first << " url:" << url << " body out:" << r.second);
					}
				}
				fct(r.first, sharedData);
			}
			, url, body, h, asyncCBExecMode);
		}


	protected:
		Http();

		template <typename TSharedLockableType, typename TFunctor>
		inline void AsyncPatchJsonImpl(const TSharedLockableType& sharedData, const TFunctor& fct,
			const std::string& url, const BodyParams& body, const Headers& hi,
			EAsyncCallbackExecutionMode asyncCBExecMode)
		{
			Headers h(hi);
			h.emplace_back("accept", "application/json");
			h.emplace_back("Content-Type", "application/json; charset=UTF-8");
			if (accessToken_ && !accessToken_->IsEmpty())
				h.emplace_back("Authorization", std::string("Bearer ") + *accessToken_->Get());
			AsyncPatch([sharedData, fct, url](const Response& r) {
				{
					auto dataLock(sharedData->GetAutoLock());
					if (IsSuccessful(r))
						Json::FromString(dataLock.Get(), r.second);
					else
						BE_LOGE("http", "AsyncPatchJson failed code:" << r.first << " url:" << url << " body out:" << r.second);
				}
				fct(r.first, sharedData);
			}
			, url, body, h, asyncCBExecMode);
		}

		template <typename TSharedLockableType, typename TFunctor>
		inline void AsyncPostJsonImpl(const TSharedLockableType& sharedData, const TFunctor& fct,
			const std::string& url, const BodyParams& body, const Headers& hi,
			EAsyncCallbackExecutionMode asyncCBExecMode)
		{
			Headers h(hi);
			h.emplace_back("accept", "application/json");
			h.emplace_back("Content-Type", "application/json; charset=UTF-8");
			if (accessToken_ && !accessToken_->IsEmpty())
				h.emplace_back("Authorization", std::string("Bearer ") + *accessToken_->Get());
			AsyncPost([sharedData, fct, url](const Response& r) {
				{
					auto dataLock(sharedData->GetAutoLock());
					if (IsSuccessful(r))
						Json::FromString(dataLock.Get(), r.second);
					else
						BE_LOGE("http", "AsyncPostJson failed code:" << r.first << " url:" << url << " body out:" << r.second);
				}
				fct(r.first, sharedData);
			}
			, url, body, h, asyncCBExecMode);
		}

		template<typename TSharedLockableType, typename TFunctor>
		inline void AsyncPutJsonImpl(const TSharedLockableType& sharedData, const TFunctor& fct,
			const std::string& url, const BodyParams& body, const Headers& hi,
			EAsyncCallbackExecutionMode asyncCBExecMode)
		{
			Headers h(hi);
			h.emplace_back("accept", "application/json");
			h.emplace_back("Content-Type", "application/json; charset=UTF-8");
			if (accessToken_ && !accessToken_->IsEmpty())
				h.emplace_back("Authorization", std::string("Bearer ") + *accessToken_->Get());
			AsyncPut([sharedData, fct, url](const Response& r) {
				{
					auto dataLock(sharedData->GetAutoLock());
					if (IsSuccessful(r))
						Json::FromString(dataLock.Get(), r.second);
					else
						BE_LOGE("http", "AsyncPutJson failed code:" << r.first << " url:" << url << " body out:" << r.second);
				}
				fct(r.first, sharedData);
			}
			, url, body, h, asyncCBExecMode);
		}


	protected:
		std::string baseUrl_; // base URL
		std::shared_ptr<ThreadSafeAccessToken> accessToken_;
	};

	//explicit declaration to avoid a warning
	template<>
	ADVVIZ_LINK Tools::Factory<Http>::Globals& Tools::Factory<Http>::GetGlobals();

}
