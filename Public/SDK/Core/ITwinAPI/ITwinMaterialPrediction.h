/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMaterialPrediction.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#ifndef SDK_CPPMODULES
	#include <string>
	#include <vector>
	#ifndef MODULE_EXPORT
		#define MODULE_EXPORT
	#endif // !MODULE_EXPORT
#endif

MODULE_EXPORT namespace AdvViz::SDK
{
	struct MLInferenceMaterialEntry
	{
		std::string material; // name of the material - eg. "Wood"
		std::vector<uint64_t> elements;
	};

	struct ITwinMaterialPrediction
	{
		std::vector<MLInferenceMaterialEntry> data;
	};

}
