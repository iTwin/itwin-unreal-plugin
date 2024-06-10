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

			void SetBaseUrl(const std::string& url) { url_ = url; }
			void SetBasicAuth(const std::string& login, const std::string& passwd) {
				auth_ = std::make_unique<cpr::Authentication>(login, passwd, cpr::AuthMode::BASIC);
			}

			std::pair<long, std::string> Put(const std::string &url, const std::string& body="", const Headers& headers = Headers()) {
				cpr::Header h;
				for (auto& i : headers)
					h[i.first] = i.second;
				cpr::Response r;
				if (auth_)
					r = cpr::Put(cpr::Url{ url_ + '/' + url }
						, cpr::Body{ body }
						, *auth_
					);
				else
					r = cpr::Put(cpr::Url{ url_ + '/' + url }
						, cpr::Body{ body }
					);
				return std::make_pair(r.status_code, r.text);
			}

			std::pair<long, std::string> Patch(const std::string& url, const std::string& body = "", const Headers& headers = Headers()) {
				cpr::Header h;
				for (auto& i : headers)
					h[i.first] = i.second;
				cpr::Response r;
				if (auth_)
					r = cpr::Patch(cpr::Url{ url_ + '/' + url }
						, cpr::Body{ body }
						, *auth_
					);
				else
					r = cpr::Patch(cpr::Url{ url_ + '/' + url }
						, cpr::Body{ body }
				);
				return std::make_pair(r.status_code, r.text);
			}

			std::pair<long, std::string> Post(const std::string& url, const std::string& body = "", const Headers& headers = Headers()) {
				cpr::Header h;
				for (auto& i : headers)
					h[i.first] = i.second;
				cpr::Response r;
				if (auth_)
					r = cpr::Post(cpr::Url{ url_ + '/' + url }
						, cpr::Body{ body }
						, *auth_
					);
				else
					r = cpr::Post(cpr::Url{ url_ + '/' + url }
						, cpr::Body{ body }
				);
				return std::make_pair(r.status_code, r.text);
			}

			std::pair<long, std::string> Get(const std::string& url, const std::string& body="", const Headers& headers = Headers()) {
				cpr::Header h;
				for (auto& i : headers)
					h[i.first] = i.second;
				cpr::Response r;
				if (auth_)
					r = cpr::Get(cpr::Url{ url_ + '/' + url }
						, cpr::Body{ body }
						, h
						, *auth_
					);
				else
					r = cpr::Get(cpr::Url{ url_ + '/' + url }
						, h
						, cpr::Body{ body }
				);
				return std::make_pair(r.status_code, r.text);
			}

			std::pair<long, std::string> Delete(const std::string& url, const std::string& body = "", const Headers& headers = Headers()) {
				cpr::Header h;
				for (auto& i : headers)
					h[i.first] = i.second;
				cpr::Response r;
				if (auth_)
					r = cpr::Delete(cpr::Url{ url_ + '/' + url }
						, cpr::Body{ body }
						, h
						, *auth_
					);
				else
					r = cpr::Delete(cpr::Url{ url_ + '/' + url }
						, h
						, cpr::Body{ body }
				);
				return std::make_pair(r.status_code, r.text);
			}

			std::unique_ptr<cpr::Authentication> auth_;
			std::string url_;
		};
	};

	typedef Impl::HttpCpr HttpImpl;
}
