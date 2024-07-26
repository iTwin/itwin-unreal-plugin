/*--------------------------------------------------------------------------------------+
|
|     $Source: httpCprImpl.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
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
			void SetBaseUrl(const std::string& url) override;
			void SetBasicAuth(const std::string& login, const std::string& passwd) override;
			std::pair<long, std::string> Put(const std::string& url, const std::string& body = "", const Headers& headers = {}) override;
			std::pair<long, std::string> Patch(const std::string& url, const std::string& body = "", const Headers& headers = {}) override;
			std::pair<long, std::string> Post(const std::string& url, const std::string& body = "", const Headers& headers = {}) override;
			std::pair<long, std::string> Get(const std::string& url, const std::string& body="", const Headers& headers = {}, bool isFullUrl = false) override;
			std::pair<long, std::string> Delete(const std::string& url, const std::string& body = "", const Headers& headers = {}) override;

		private:
			std::unique_ptr<cpr::Authentication> auth_;
			std::string url_;
		};
	};
}
