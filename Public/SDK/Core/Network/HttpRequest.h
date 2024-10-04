/*--------------------------------------------------------------------------------------+
|
|     $Source: HttpRequest.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "http.h"

MODULE_EXPORT namespace SDK::Core
{

	class HttpRequest : public Tools::Factory<HttpRequest>
		, public Tools::ExtensionSupport
		, public std::enable_shared_from_this<HttpRequest>
	{
	public:
		enum class EVerb : uint8_t
		{
			Delete,
			Get,
			Patch,
			Post,
			Put
		};

		/// Unique request ID
		using RequestID = uint32_t;
		static const RequestID NO_REQUEST = 0xFFFFFFFF;

		HttpRequest();
		virtual ~HttpRequest();

		void SetVerb(EVerb verb);

		virtual void Process(
			Http& http,
			std::string const& url,
			std::string const& body,
			Http::Headers const& headers);

		EVerb GetVerb() const { return verb_; }
		RequestID GetRequestID() const { return id_; }

		using Response = Http::Response;
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

}
