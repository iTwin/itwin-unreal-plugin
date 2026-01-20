/*--------------------------------------------------------------------------------------+
|
|     $Source: HttpRequest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "HttpRequest.h"

#include <mutex>
#include <stduuid/uuid.h>
#include "../Singleton/singleton.h"

namespace AdvViz::SDK
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

	bool HttpRequest::CheckResponse(Response const& response, std::string& /*requestError*/) const
	{
		return Http::IsSuccessful(response);
	}


	Http::Response HttpRequest::DoProcess(Http& http, std::string const& url, BodyParams const& body,
										  Http::Headers const& headers, bool isFullUrl /*= false*/)
	{
		switch (verb_)
		{
		case EVerb::Delete: return http.Delete(url, body, headers); break;
		case EVerb::Get:	return http.Get(url, headers, isFullUrl); break;
		case EVerb::Patch:	return http.Patch(url, body, headers); break;
		case EVerb::Post:	return http.Post(url, body, headers); break;
		default:
		case EVerb::Put:	return http.Put(url, body, headers); break;
		}
	}

	const char* HttpRequest::GetRequestID() const
	{
		return id_.c_str(); 
	}

}
