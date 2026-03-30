/*--------------------------------------------------------------------------------------+
|
|     $Source: UEHttpAdapter.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"
#include <Interfaces/IHttpRequest.h>
#include <Templates/PimplPtr.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/Network/HttpRequest.h>
#include <Compil/AfterNonUnrealIncludes.h>

AdvViz::SDK::Http::Response ConvertUnrealHttpResponse(
	AdvViz::SDK::HttpRequest::RequestPtr const& SDKRequestPtr,
	FHttpResponsePtr UEResponse, bool connectedSuccessfully);

class FUEHttpRequest : public AdvViz::SDK::HttpRequest
{
public:
	FUEHttpRequest();

	static const long HTTP_CONNECT_ERR = -2;
	static const long HTTP_INVALID_UE_RESPONSE = -3;

	virtual void Process(
		AdvViz::SDK::Http& http,
		std::string const& url,
		AdvViz::SDK::Http::BodyParams const& body,
		AdvViz::SDK::Http::Headers const& headers,
		bool isFullUrl = false) override;

	virtual bool CheckResponse(
		AdvViz::SDK::Http::Response const& response,
		std::string& requestError) const override;

protected:
	virtual void DoSetVerb(AdvViz::SDK::EVerb verb) override;
	virtual void DoSetResponseCallback(ResponseCallback const& callback) override;

private:
	class FImpl;
	TPimplPtr<FImpl> Impl;
};
