/*--------------------------------------------------------------------------------------+
|
|     $Source: UEHttpAdapter.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"
#include <Templates/PimplPtr.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/Network/HttpRequest.h>
#include <Compil/AfterNonUnrealIncludes.h>

class FUEHttpRequest : public SDK::Core::HttpRequest
{
public:
	FUEHttpRequest();

	virtual void Process(
		SDK::Core::Http& http,
		std::string const& url,
		std::string const& body,
		SDK::Core::Http::Headers const& headers) override;

	virtual bool CheckResponse(
		SDK::Core::Http::Response const& response,
		std::string& requestError) const override;

protected:
	virtual void DoSetVerb(EVerb verb) override;
	virtual void DoSetResponseCallback(ResponseCallback const& callback) override;

private:
	class FImpl;
	TPimplPtr<FImpl> Impl;
};
