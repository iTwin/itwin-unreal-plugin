/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDecorationMockServerBase.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#if WITH_TESTS

#include <Tests/ITwinMockServerBase.h>

/// Base class for mock servers used in Decoration tests.
/// Provides with an implementation for decoration, materials, splines, annotations (for an empty
/// decoration). You can override the Process*Request methods to provide your own implementation for each of
/// these categories of requests.
class FITwinDecorationMockServerBase : public FITwinMockServerBase
{
public:
	explicit FITwinDecorationMockServerBase(int port)
		: FITwinMockServerBase(port)
	{}

	virtual Response responseHandler(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) override;

protected:
	//! Process a request for an empty collection (of materials, splines, instances, etc.).
	Response ProcessEmptyCollectionRequest(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) const;

	virtual Response ProcessDecorationRequest(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) const;

	virtual Response ProcessMaterialsRequest(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) const;

	virtual Response ProcessSplinesRequest(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) const;

	virtual Response ProcessInstancesRequest(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) const;

	virtual Response ProcessAnimationsRequest(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) const;

	virtual Response ProcessAnnotationsRequest(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) const;

protected:
	std::string decorationID = "test_decoration_id";
	std::string iTwinID = "test_itwin_id";
};


#endif // WITH_TESTS
