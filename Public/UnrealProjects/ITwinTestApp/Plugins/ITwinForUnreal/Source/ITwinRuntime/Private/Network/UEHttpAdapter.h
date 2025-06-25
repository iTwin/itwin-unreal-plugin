/*--------------------------------------------------------------------------------------+
|
|     $Source: UEHttpAdapter.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"
#include <Templates/PimplPtr.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/Network/HttpRequest.h>
#include <Compil/AfterNonUnrealIncludes.h>

class FUEHttpRequest : public AdvViz::SDK::HttpRequest
{
public:
	FUEHttpRequest();

	virtual void Process(
		AdvViz::SDK::Http& http,
		std::string const& url,
		std::string const& body,
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
