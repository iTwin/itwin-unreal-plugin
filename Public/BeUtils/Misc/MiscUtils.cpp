/*--------------------------------------------------------------------------------------+
|
|     $Source: MiscUtils.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <BeUtils/Misc/MiscUtils.h>
#include <regex>
#include <boost/algorithm/string/split.hpp>

namespace BeUtils
{

std::string GetRealityDataIdFromUrl(const std::string& url)
{
	const auto formattedUrl = std::regex_replace(std::regex_replace(url, std::regex("~2F"), "/"), std::regex("\\\\"), "/");
	std::vector<std::string> urlParts;
	boost::algorithm::split(urlParts, formattedUrl, [](const auto& c){return c == '/';}, boost::algorithm::token_compress_on);
	std::ranges::transform(urlParts, urlParts.begin(), [](const auto& s){return std::regex_replace(s, std::regex("%2D"), "-");});
	if (!(urlParts.size() >= 6 && urlParts[3] == "Repositories" &&
		std::regex_match(urlParts[4], std::regex("S3MXECPlugin--.*")) && urlParts[5] == "S3MX"))
		return {};
	const auto it = std::ranges::find_if(urlParts, [](const auto& s){return std::regex_match(s,
		std::regex("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));});
	return it == urlParts.end() ? std::string() : *it;
}

bool ContainsUUIDLikeSubstring(const std::string& name)
{
	return std::regex_match(name, std::regex(".*[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-.*"));
}

} // namespace BeUtils
