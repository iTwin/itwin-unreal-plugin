/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinScene.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#ifndef SDK_CPPMODULES
	#include <string>
	#ifndef MODULE_EXPORT
		#define MODULE_EXPORT
	#endif // !MODULE_EXPORT
#endif

#include <optional>
#include <array>
#include <core/Json/Json.h>

MODULE_EXPORT namespace AdvViz::SDK
{

	///==================================================================================
	/// SIMPLIFIED VERSION
	///==================================================================================


	struct ITwinAtmosphereSettings
	{
		double sunAzimuth = 0.;
		double sunPitch = 0.;
		double heliodonLongitude = 0.;
		double heliodonLatitude = 0.;
		std::string heliodonDate;
		double weather = 0.;
		double windOrientation = 0.;
		double windForce = 0.;
		double fog = 0.;
		double exposure = 0.;
		bool useHeliodon = false;
		std::optional<std::string> HDRIImage;
		std::optional<double> HDRIZRotation;
		std::optional<double> sunIntensity;
		bool operator==(const ITwinAtmosphereSettings& other) const
		{
			return fabs(this->sunAzimuth-other.sunAzimuth)<1e-5 
				&& fabs(this->sunPitch - other.sunPitch) < 1e-5
				&& fabs(this->heliodonLongitude - other.heliodonLongitude) < 1e-5
				&& fabs(this->heliodonLatitude - other.heliodonLatitude) < 1e-5
				&& this->heliodonDate == other.heliodonDate
				&& fabs(this->weather - other.weather) < 1e-5
				&& fabs(this->windOrientation - other.windOrientation) < 1e-5
				&& fabs(this->windForce - other.windForce) < 1e-5
				&& fabs(this->fog - other.fog) < 1e-5
				&& fabs(this->exposure - other.exposure) < 1e-5
				&& this->useHeliodon == other.useHeliodon
				&& this->HDRIImage == other.HDRIImage
				&& ((!this->HDRIZRotation.has_value() && !other.HDRIZRotation.has_value()) || (this->HDRIZRotation.has_value() && other.HDRIZRotation.has_value() && fabs(this->HDRIZRotation.value() - other.HDRIZRotation.value()) < 1e-5))
				&& ((!this->sunIntensity.has_value() && !other.sunIntensity.has_value()) || (this->sunIntensity.has_value() && other.sunIntensity.has_value() && fabs(this->sunIntensity.value() - other.sunIntensity.value()) < 1e-5))
				;
		}
	};

	struct ITwinSceneSettings
	{
		bool displayGoogleTiles = true;
		double qualityGoogleTiles = 0.30;
		std::optional < std::array<double, 3> > geoLocation;

		bool operator==(const ITwinSceneSettings& other) const
		{
			return fabs(this->qualityGoogleTiles - other.qualityGoogleTiles) < 1e-5
				&& this->geoLocation == other.geoLocation
				&& this->qualityGoogleTiles == other.qualityGoogleTiles
				;

		}
	};

	struct ITwinHDRISettings
	{
		std::string	hdriName;
		double		sunPitch = 0.;
		double		sunYaw = 0.;
		double		sunIntensity = 10.;
		double		rotation = 0.;	// rotation of the hdri around the up vector
		bool operator==(const ITwinHDRISettings& other) const
		{
			return this->hdriName==other.hdriName
				&& fabs(this->sunPitch - other.sunPitch) < 1e-5
				&& fabs(this->sunYaw - other.sunYaw) < 1e-5
				&& fabs(this->sunIntensity - other.sunIntensity) < 1e-5
				&& fabs(this->rotation - other.rotation) < 1e-5
				;
		}
	};

	struct ITwinEnvironment
	{
		ITwinAtmosphereSettings atmosphere;
		ITwinSceneSettings		sceneSettings;
		ITwinHDRISettings		hdri;
	};

	struct ITwinScene
	{
		std::string name;
		ITwinEnvironment environment;
	};
}
