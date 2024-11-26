/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinScene.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#ifndef SDK_CPPMODULES
	#include <string>
	#ifndef MODULE_EXPORT
		#define MODULE_EXPORT
	#endif // !MODULE_EXPORT
#endif

MODULE_EXPORT namespace SDK::Core
{

	///==================================================================================
	/// SIMPLIFIED VERSION
	///==================================================================================


	struct ITwinAtmosphereSettings
	{
		double sunAzimuth;
		double sunPitch;
		double heliodonLongitude;
		double heliodonLatitude;
		std::string heliodonDate;
		double weather;
		double windOrientation;
		double windForce;
		double fog;
		double exposure;
		bool useHeliodon;
	};

	struct ITwinSceneSettings
	{
		bool displayGoogleTiles;
	};
	struct ITwinEnvironment
	{
		ITwinAtmosphereSettings atmosphere;
		ITwinSceneSettings sceneSettings;
	};
	struct ITwinScene
	{
		std::string name;
		ITwinEnvironment environment;
	};

}
