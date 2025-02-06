/*--------------------------------------------------------------------------------------+
|
|     $Source: HttpRequest.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "HttpRequest.h"

#include <mutex>
#include <stduuid/uuid.h>
#include "../Singleton/singleton.h"

namespace SDK::Core
{
	template<>
	Tools::Factory<HttpRequest>::Globals::Globals()
	{
		newFct_ = []() {return new HttpRequest(); };
	}

	template<>
	Tools::Factory<HttpRequest>::Globals& Tools::Factory<HttpRequest>::GetGlobals()
	{
		return singleton<Tools::Factory<HttpRequest>::Globals>();
	}

	/*static*/ const RequestID HttpRequest::NO_REQUEST = "NONE";

	namespace HttpImpl
	{
		static uuids::uuid_random_generator CreateGenerator()
		{
			// from https://github.com/mariusbancila/stduuid
			std::random_device rd;
			auto seed_data = std::array<int, std::mt19937::state_size> {};
			std::generate(std::begin(seed_data), std::end(seed_data), std::ref(rd));
			std::seed_seq seq(std::begin(seed_data), std::end(seed_data));
			static thread_local std::mt19937 generator(seq);
			return uuids::uuid_random_generator{generator};
		}

		static RequestID GetUniqueID()
		{
			static thread_local uuids::uuid_random_generator generator = CreateGenerator();
			return uuids::to_string(generator());
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

	void HttpRequest::SetNeedRawData(bool b)
	{
		needRawData_ = b;
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
