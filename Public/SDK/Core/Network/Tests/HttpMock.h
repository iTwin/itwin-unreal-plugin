/*--------------------------------------------------------------------------------------+
|
|     $Source: HttpMock.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <httpmockserver/mock_server.h>
#include <Core/Tools/LockableObject.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace AdvViz::SDK
{

//! A mock HTTP server for testing purposes. It allows you to define custom responses for specific HTTP
//! requests, just by filling one of the 3 available SetResponseFunction variants. The server will then
//! respond with the defined response when it receives a matching request.
//! The interest of this is that you can setup the responses very near to the requests you want to test.
class HTTPMock : public httpmock::MockServer {
public:
	static std::unique_ptr<httpmock::MockServer> MakeServer();

	explicit HTTPMock(int port = 9200);

	std::string GetUrl() const
	{
		return "http://localhost:" + std::to_string(getPort());
	}

	typedef Response Response2;

	using RequestKey = std::pair<std::string, std::string>; // pair { method, url } identifying a request


	template <typename Func>
	void SetResponseFunction(const RequestKey& key, Func&& f)
	{
		auto thdata = thdata_.GetAutoLock();
		thdata->responseFct_[key] = std::forward<Func>(f);
	}
	
	template <typename Func>
	void SetResponseFunctionWithData(const RequestKey& key, Func&& f)
	{
		auto thdata = thdata_.GetAutoLock();
		thdata->responseFctWithData_[key] = std::forward<Func>(f);
	}

	template <typename Func>
	void SetResponseFunctionWithArgs(const RequestKey& key, Func&& f)
	{
		auto thdata = thdata_.GetAutoLock();
		thdata->responseFctWithArgs_[key] = std::forward<Func>(f);
	}

	void ResetResponseFunction(const RequestKey& key);

private:

	/// Handler called by MockServer on HTTP request.
	Response responseHandler(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& /*headers*/) override;

	/// Return true if \p url starts with \p str.
	bool matchesURL(const std::string& url, const std::string& str) const {
		return url.substr(0, str.size()) == str;
	}

private:
	struct SThreadSafeData
	{
		std::map<RequestKey, std::function<Response2()>> responseFct_;
		std::map<RequestKey, std::function<Response2(const std::string&)>> responseFctWithData_;
		std::map<RequestKey, std::function<Response2(const std::vector<UrlArg>&)>> responseFctWithArgs_;
	};
	Tools::RWLockableObject<SThreadSafeData> thdata_;
};

HTTPMock* GetHttpMock();

} // namespace AdvViz::SDK
