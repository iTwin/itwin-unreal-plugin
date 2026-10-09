/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDecorationMockServerBase.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS

#include "ITwinDecorationMockServerBase.h"


httpmock::MockServer::Response FITwinDecorationMockServerBase::responseHandler(
	const std::string& url,
	const std::string& method,
	const std::string& data,
	const std::vector<UrlArg>& urlArguments,
	const std::vector<Header>& headers)
{
	if (url.find("/arg_test") != std::string::npos)
	{
		return ProcessArgTest(urlArguments);
	}
	if (url.find("/materials") != std::string::npos)
	{
		return ProcessMaterialsRequest(url, method, data, urlArguments, headers);
	}
	if (url.find("/spline") != std::string::npos) // for both /splines and /splinepoints
	{
		return ProcessSplinesRequest(url, method, data, urlArguments, headers);
	}
	if (url.find("/instances") != std::string::npos)
	{
		return ProcessInstancesRequest(url, method, data, urlArguments, headers);
	}
	if (url.find("/annotations") != std::string::npos)
	{
		return ProcessAnnotationsRequest(url, method, data, urlArguments, headers);
	}
	if (url.find("/animation") != std::string::npos)
	{
		return ProcessAnimationsRequest(url, method, data, urlArguments, headers);
	}
	if (url.find("decorations/") != std::string::npos)
	{
		return ProcessDecorationRequest(url, method, data, urlArguments, headers);
	}
	return Response(MHD_HTTP_NOT_FOUND,
		std::string("Page not found: ") + url);
}


httpmock::MockServer::Response FITwinDecorationMockServerBase::ProcessEmptyCollectionRequest(
	const std::string& url,
	const std::string& method,
	const std::string& data,
	const std::vector<UrlArg>& urlArguments,
	const std::vector<Header>& headers) const
{
	CHECK_DECORATION_HEADERS();
	if (method == "GET")
	{
		return Response(MHD_HTTP_OK,
			"{\"total_rows\":0,\"rows\":[],\"_links\":{}}"
		);
	}
	return Response(MHD_HTTP_NOT_FOUND, "Page not found.");
}

/// Process decorations/* requests
httpmock::MockServer::Response FITwinDecorationMockServerBase::ProcessDecorationRequest(
	const std::string& url,
	const std::string& method,
	const std::string& data,
	const std::vector<UrlArg>& urlArguments,
	const std::vector<Header>& headers) const
{
	CHECK_DECORATION_HEADERS();

	if (method == "GET")
	{
		if (url.find(this->decorationID) != std::string::npos)
		{
			// Decoration details.
			return Response(MHD_HTTP_OK,
				"{\"name\":\"DecorationName\",\"itwinid\":\"" + this->iTwinID + "\"}"
			);
		}
	}
	return Response(MHD_HTTP_NOT_FOUND, "Page not found.");
}

/// Process materials/* requests
httpmock::MockServer::Response FITwinDecorationMockServerBase::ProcessMaterialsRequest(
	const std::string& url,
	const std::string& method,
	const std::string& data,
	const std::vector<UrlArg>& urlArguments,
	const std::vector<Header>& headers) const
{
	return ProcessEmptyCollectionRequest(url, method, data, urlArguments, headers);
}

/// Process splines/* requests
httpmock::MockServer::Response FITwinDecorationMockServerBase::ProcessSplinesRequest(
	const std::string& url,
	const std::string& method,
	const std::string& data,
	const std::vector<UrlArg>& urlArguments,
	const std::vector<Header>& headers) const
{
	return ProcessEmptyCollectionRequest(url, method, data, urlArguments, headers);
}


/// Process instances/* requests
httpmock::MockServer::Response FITwinDecorationMockServerBase::ProcessInstancesRequest(
	const std::string& url,
	const std::string& method,
	const std::string& data,
	const std::vector<UrlArg>& urlArguments,
	const std::vector<Header>& headers) const
{
	if (method == "POST" && url.ends_with("instancesgroups"))
	{
		return Response(MHD_HTTP_CREATED,
			"{\"id\":\"static_group_id\"}"
		);
	}
	return ProcessEmptyCollectionRequest(url, method, data, urlArguments, headers);
}


/// Process annotations/* requests
httpmock::MockServer::Response FITwinDecorationMockServerBase::ProcessAnnotationsRequest(
	const std::string& url,
	const std::string& method,
	const std::string& data,
	const std::vector<UrlArg>& urlArguments,
	const std::vector<Header>& headers) const
{
	return ProcessEmptyCollectionRequest(url, method, data, urlArguments, headers);
}

/// Process animation* requests
httpmock::MockServer::Response FITwinDecorationMockServerBase::ProcessAnimationsRequest(
	const std::string& url,
	const std::string& method,
	const std::string& data,
	const std::vector<UrlArg>& urlArguments,
	const std::vector<Header>& headers) const
{
	return ProcessEmptyCollectionRequest(url, method, data, urlArguments, headers);
}


#endif // WITH_TESTS
