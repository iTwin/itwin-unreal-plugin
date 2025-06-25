/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinRequestTypes.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#ifndef SDK_CPPMODULES
#	include <functional>
#	include <map>
#	include <string>
#	ifndef MODULE_EXPORT
#		define MODULE_EXPORT
#	endif // !MODULE_EXPORT
#endif

MODULE_EXPORT namespace AdvViz::SDK
{
	using RequestID = std::string;

	enum class EVerb : uint8_t
	{
		Delete,
		Get,
		Patch,
		Post,
		Put
	};

	struct ITwinAPIRequestInfo
	{
		const std::string ShortName; // short name used in errors, identifying the request easily
		const EVerb Verb = EVerb::Get;
		std::string UrlSuffix;
		const std::string AcceptHeader;

		std::string ContentType;
		std::string ContentString;

		std::map<std::string, std::string> CustomHeaders;

		// In some cases, we can determine in advance that the request is ill-formed (typically if a
		// mandatory ID is missing...).
		// In such case, we will not even try to run the http request.
		bool badlyFormed = false;

		// Specific to requests fetching binary data (such as GetTextureData)
		bool needRawData = false;

		bool discardAllHeaders = false;
		bool isFullUrl = false; // If true, UrlSuffix should contain a full URL, including the protocol

		bool HasCustomHeader(std::string const& headerKey) const
		{
			return CustomHeaders.find(headerKey) != CustomHeaders.end();
		}
	};

	// Some errors can be filtered to avoid retrying the corresponding requests, and/or discard them from the
	// logs.
	using FilterErrorFunc =	std::function<void(std::string const&, bool& bAllowRetry, bool& bLogError)>;

	using CustomRequestCallback =
		std::function<bool(long status, std::string const& response, RequestID const&, std::string& strError)>;
}
