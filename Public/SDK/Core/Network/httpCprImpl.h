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

namespace SDK::Core
{
	namespace Impl
	{
		class HttpCpr : public Http
		{
		public:
			HttpCpr() {}
			void SetBasicAuth(const std::string& login, const std::string& passwd) override;
			Response Put(const std::string& url, const std::string& body = "", const Headers& headers = {}) override;
			Response PutBinaryFile(const std::string& url, const std::string& filePath, const Headers& headers = {}) override;
			Response Patch(const std::string& url, const std::string& body = "", const Headers& headers = {}) override;
			Response Post(const std::string& url, const std::string& body = "", const Headers& headers = {}) override;
			Response PostFile(const std::string& url, const std::string& fileParamName, const std::string& filePath,
				const KeyValueVector& extraParams = {}, const Headers& h = {}) override;
			Response Get(const std::string& url, const std::string& body="", const Headers& headers = {}, bool isFullUrl = false) override;
			Response Delete(const std::string& url, const std::string& body = "", const Headers& headers = {}) override;

		private:
			std::unique_ptr<cpr::Authentication> auth_;
		};
	};
}
