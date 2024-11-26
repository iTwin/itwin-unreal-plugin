/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinRequestTypes.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#ifndef SDK_CPPMODULES
	#include <map>
	#include <string>
	#ifndef MODULE_EXPORT
		#define MODULE_EXPORT
	#endif // !MODULE_EXPORT
#endif

MODULE_EXPORT namespace SDK::Core
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

		const std::string ContentType;
		const std::string ContentString;

		std::map<std::string, std::string> CustomHeaders;

		// In some cases, we can determine in advance that the request is ill-formed (typically if a
		// mandatory ID is missing...).
		// In such case, we will not even try to run the http request.
		bool badlyFormed = false;

		// Specific to requests fetching binary data (such as GetTextureData)
		bool needRawData = false;

		bool HasCustomHeader(std::string const& headerKey) const
		{
			return CustomHeaders.find(headerKey) != CustomHeaders.end();
		}
	};
}
