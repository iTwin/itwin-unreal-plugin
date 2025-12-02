/*--------------------------------------------------------------------------------------+
|
|     $Source: httpCprImpl.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <cpr/cpr.h>

#include <string>
#include "http.h"

namespace AdvViz::SDK
{
	namespace Impl
	{
		class HttpCpr : public Http, Tools::TypeId<HttpCpr>
		{
		public:
			HttpCpr() {}
			void SetBasicAuth(const char* login, const char* passwd) override;
			bool DecodeBase64(const std::string& src, RawData& buffer) const override;
			Http::Response DoPut(const std::string& url, const BodyParams& body = {}, const Headers& headers = {}) override;
			Http::Response DoPutBinaryFile(const std::string& url, const std::string& filePath, const Headers& headers = {}) override;
			Http::Response DoPatch(const std::string& url, const BodyParams& body = {}, const Headers& headers = {}) override;
			Http::Response DoPost(const std::string& url, const BodyParams& body = {}, const Headers& headers = {}) override;
			void DoAsyncPost(std::function<void(const Response&)> callback, const std::string& url, const BodyParams& body = {}, const Headers& headers = {}) override;
			void DoAsyncPut(std::function<void(const Response&)> callback, const std::string& url, const BodyParams& body = {}, const Headers& headers = {});
			Http::Response DoPostFile(const std::string& url, const std::string& fileParamName, const std::string& filePath,
				const KeyValueVector& extraParams = {}, const Headers& h = {}) override;
			Http::Response DoGet(const std::string& url, const Headers& headers = {}, bool isFullUrl = false) override;
			void DoAsyncGet(std::function<void(const Response&)> callback, const std::string& url, const Headers& headers = {}, bool isFullUrl = false) override;
			Http::Response DoDelete(const std::string& url, const BodyParams& body = {}, const Headers& headers = {}) override;

			using Tools::TypeId<HttpCpr>::GetTypeId;
			std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
			bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || Http::IsTypeOf(i); }

		private:
			std::unique_ptr<cpr::Authentication> auth_;

			std::string GetBaseUrlStr() const;

		};
	};
}
