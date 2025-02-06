/*--------------------------------------------------------------------------------------+
|
|     $Source: MiscUtils.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <string>

namespace BeUtils
{

//! Note: This function has been taken from imodeljs function getRealityDataIdFromUrl(), which can be found here:
//! https://dev.azure.com/bentleycs/iModelTechnologies/_git/imodeljs?path=%2Fclients%2Freality-data%2Fsrc%2FRealityDataClient.ts&_a=contents&version=GBmaster
//! This is the method that determines if the url refers to Reality Data stored on PW Context Share. If not then empty string is returned.
//! @param url A fully formed URL to a reality data or a reality data folder or document of the form:
//!              https://{Host}/{version}/Repositories/S3MXECPlugin--{ProjectId}/S3MX/RealityData/{RealityDataId}
//!              https://{Host}/{version}/Repositories/S3MXECPlugin--{ProjectId}/S3MX/Folder/{RealityDataId}~2F{Folder}
//!              https://{Host}/{version}/Repositories/S3MXECPlugin--{ProjectId}/S3MX/Document/{RealityDataId}~2F{Full Document Path and name}'
//!            Where {Host} represents the Reality Data Service server (ex: connect-realitydataservices.bentley.com). This value is ignored since the
//!            actual host server name depends on the environment or can be changed in the future.
//!            Where {version} is the Bentley Web Service Gateway protocol version. This value is ignored but the version must be supported by Reality Data Service.
//!            Where {Folder} and {Document} are the full folder or document path relative to the Reality Data root.
//!            {RealityDataId} is extracted after validation of the URL and returned.
//!            {ProjectId} is ignored.
//! @returns A string containing the Reality Data Identifier (otherwise named tile id). If the URL is not a reality data service URL then empty string is returned.
std::string GetRealityDataIdFromUrl(const std::string& url);

} // namespace BeUtils
