/*--------------------------------------------------------------------------------------+
|
|     $Source: TestMiscUtils.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <catch2/catch.hpp>
#include <BeUtils/Misc/MiscUtils.h>


//! Test taken from imodeljs getRealityDataIdFromUrl() test, which can be found here:
//! https://dev.azure.com/bentleycs/iModelTechnologies/_git/imodeljs?path=/clients/reality-data/src/test/integration/RealityDataClient.test.ts&_a=contents&version=GBmaster
TEST_CASE("TestGetRealityDataIdFromUrl")
{
	CHECK("73226b81-6d95-45d3-9473-20e52703aea5" == BeUtils::GetRealityDataIdFromUrl("http://connect-realitydataservices.bentley.com/v2.4/Repositories/S3MXECPlugin--95b8160c-8df9-437b-a9bf-22ad01fecc6b/S3MX/RealityData/73226b81-6d95-45d3-9473-20e52703aea5"));
	CHECK("73226b81-6d95-45d3-9473-20e52703aea5" == BeUtils::GetRealityDataIdFromUrl("http:\\\\connect-realitydataservices.bentley.com\\v2.4\\Repositories/S3MXECPlugin--95b8160c-8df9-437b-a9bf-22ad01fecc6b\\S3MX\\RealityData\\73226b81-6d95-45d3-9473-20e52703aea5"));
	CHECK("73226b81-6d95-45d3-9473-20e52703aea5" == BeUtils::GetRealityDataIdFromUrl("http:\\connect-realitydataservices.bentley.com\\v2.4\\Repositories/S3MXECPlugin--95b8160c-8df9-437b-a9bf-22ad01fecc6b\\S3MX\\RealityData\\73226b81-6d95-45d3-9473-20e52703aea5"));
}
