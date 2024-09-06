/*--------------------------------------------------------------------------------------+
|
|     $Source: HttpRequest.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "HttpRequest.h"

#include <mutex>

namespace SDK::Core
{
	template<>
	std::function<std::shared_ptr<HttpRequest>()> Tools::Factory<HttpRequest>::newFct_ = []() {
		std::shared_ptr<HttpRequest> p(new HttpRequest());
		return p;
	};

	namespace HttpImpl
	{
		static HttpRequest::RequestID nextRequestId_ = 0;
		static HttpRequest::RequestID GetUniqueID()
		{
			static std::mutex mutex;
			std::unique_lock lock(mutex);
			HttpRequest::RequestID nextId = nextRequestId_++;
			return nextId;
		}
	}

	HttpRequest::HttpRequest()
		: id_(HttpImpl::GetUniqueID())
	{

	}

	HttpRequest::~HttpRequest()
	{

	}

	void HttpRequest::SetVerb(EVerb verb)
	{
		verb_ = verb;
		DoSetVerb(verb);
	}

	void HttpRequest::DoSetVerb(EVerb /*verb*/)
	{

	}

	void HttpRequest::SetResponseCallback(ResponseCallback const& callback)
	{
		responseCallback_ = callback;
		DoSetResponseCallback(callback);
	}

	void HttpRequest::DoSetResponseCallback(ResponseCallback const& /*callback*/)
	{

	}

	bool HttpRequest::CheckResponse(Response const& response, std::string& requestError) const
	{
		return response.first >= 200 && response.first < 300;
	}

	void HttpRequest::Process(
		Http& http,
		std::string const& url,
		std::string const& body,
		Http::Headers const& headers)
	{
		Response r;
		switch (verb_)
		{
		case EVerb::Delete: r = http.Delete(url, body, headers); break;
		case EVerb::Get:	r = http.Get(url, body, headers); break;
		case EVerb::Patch:	r = http.Patch(url, body, headers); break;
		case EVerb::Post:	r = http.Post(url, body, headers); break;
		case EVerb::Put:	r = http.Put(url, body, headers); break;
		}
		if (responseCallback_)
		{
			responseCallback_(shared_from_this(), r);
		}
	}
}
