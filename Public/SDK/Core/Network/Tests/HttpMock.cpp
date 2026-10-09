/*--------------------------------------------------------------------------------------+
|
|     $Source: HttpMock.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "HttpMock.h"

#include <httpmockserver/port_searcher.h>

#include <Core/Tools/Log.h>

namespace AdvViz::SDK
{
	HTTPMock* GetHttpMock()
	{
		static std::unique_ptr<httpmock::MockServer> httpMockM = HTTPMock::MakeServer();
		return static_cast<HTTPMock*>(httpMockM.get());
	}


	/*static*/ std::unique_ptr<httpmock::MockServer> HTTPMock::MakeServer()
	{
		// Ensure the log is initialized before starting the mock server.
		Tools::InitLog("log_Test.txt");
		Tools::CreateAdvVizLogChannels();
		// Start the mock server on a free port (starting from 9200)
		return httpmock::getFirstRunningMockServer<HTTPMock>(9200, 10000);
	}

	HTTPMock::HTTPMock(int port /*= 9200*/)
		: MockServer(port)
	{}


	httpmock::MockServer::Response HTTPMock::responseHandler(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& /*headers*/)
	{
		auto thdata = thdata_.GetRAutoLock();
		RequestKey const requestKey(method, url);
		auto it0 = thdata->responseFctWithArgs_.find(requestKey);
		if (it0 != thdata->responseFctWithArgs_.end())
			return it0->second(urlArguments);
		auto it1 = thdata->responseFct_.find(requestKey);
		if (it1 != thdata->responseFct_.end())
			return it1->second();
		auto it2 = thdata->responseFctWithData_.find(requestKey);
		if (it2 != thdata->responseFctWithData_.end())
			return it2->second(data);
		// Return "URI not found" for the undefined methods
		return Response(404, "Not Found");
	}

	void HTTPMock::ResetResponseFunction(const RequestKey& key)
	{
		auto thdata = thdata_.GetAutoLock();
		thdata->responseFct_.erase(key);
		thdata->responseFctWithData_.erase(key);
		thdata->responseFctWithArgs_.erase(key);
	}

} // namespace AdvViz::SDK
