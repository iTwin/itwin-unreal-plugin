/*--------------------------------------------------------------------------------------+
|
|     $Source: HttpRequest.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "http.h"
#include <Core/ITwinAPI/ITwinRequestTypes.h>

namespace AdvViz::SDK
{
	class ADVVIZ_LINK HttpRequest : public Tools::Factory<HttpRequest>
		, public Tools::ExtensionSupport
		, public std::enable_shared_from_this<HttpRequest>
	{
	public:
		static const RequestID NO_REQUEST;

		HttpRequest();
		virtual ~HttpRequest();

		void SetVerb(EVerb verb);

		using BodyParams = Http::BodyParams;

		inline virtual void Process(Http& http, std::string const& url, BodyParams const& body, Http::Headers const& headers, bool isFullUrl = false)
		{
			Response r = DoProcess(http, url, body, headers, isFullUrl);
			if (responseCallback_)
			{
				responseCallback_(shared_from_this(), r);
			}
		}

		Http::Response DoProcess(Http& http, std::string const& url, BodyParams const& body, Http::Headers const& headers, bool isFullUrl = false);

		EVerb GetVerb() const { return verb_; }
		const char* GetRequestID() const;

		using Response = Http::Response;
		using ResponseRaw = Http::Response;
		using RequestPtr = std::shared_ptr<HttpRequest>;
		using ResponseCallback = std::function<void(RequestPtr const& request, Response const& response)>;

		void SetResponseCallback(ResponseCallback const& callback);

		virtual bool CheckResponse(Response const& response, std::string& requestError) const;

		bool NeedRawData() const { return needRawData_; }
		void SetNeedRawData(bool b);

	protected:
		virtual void DoSetVerb(EVerb verb);
		virtual void DoSetResponseCallback(ResponseCallback const& callback);

	private:
		EVerb verb_ = EVerb::Get;

		/// Unique request ID
		RequestID id_ = NO_REQUEST;

		ResponseCallback responseCallback_;

		// In some cases (download of binary data...), we need the full response and not just its conversion
		// to a string (which can be truncated...)
		bool needRawData_ = false;
	};
	template<>
	ADVVIZ_LINK Tools::Factory<HttpRequest>::Globals::Globals();
}
