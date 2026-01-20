/*--------------------------------------------------------------------------------------+
|
|     $Source: Mock.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <httpmockserver/mock_server.h>
#include <httpmockserver/port_searcher.h>

class HTTPMock : public httpmock::MockServer {
public:
	static std::unique_ptr<httpmock::MockServer> MakeServer()
	{
		return httpmock::getFirstRunningMockServer<HTTPMock>(9200, 10000);
	}

	explicit HTTPMock(int port = 9200) : MockServer(port) {}

	std::string GetUrl()
	{
		return "http://localhost:" + std::to_string(getPort());
	}

	typedef Response Response2;

	using RequestKey = std::pair<std::string, std::string>; // pair { method, url } identifying a request
	std::map<RequestKey, std::function<Response2()>> responseFct_;
	std::map<RequestKey, std::function<Response2(const std::string&)>> responseFctWithData_;

private:

	/// Handler called by MockServer on HTTP request.
	Response responseHandler(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& /*urlArguments*/,
		const std::vector<Header>& /*headers*/)
	{
		RequestKey const requestKey(method, url);
		auto it = responseFct_.find(requestKey);
		if (it != responseFct_.end())
			return it->second();
		auto it2 = responseFctWithData_.find(requestKey);
		if (it2 != responseFctWithData_.end())
			return it2->second(data);
		// Return "URI not found" for the undefined methods
		return Response(404, "Not Found");
	}

	/// Return true if \p url starts with \p str.
	bool matchesURL(const std::string& url, const std::string& str) const {
		return url.substr(0, str.size()) == str;
	}

};

HTTPMock* GetHttpMock();

