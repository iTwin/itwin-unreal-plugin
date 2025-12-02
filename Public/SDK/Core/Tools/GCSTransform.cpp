/*--------------------------------------------------------------------------------------+
|
|     $Source: GCSTransform.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "GCSTransform.h"
#include "Core/Tools/FactoryClassInternalHelper.h"
#include "Internal_mathConv.inl"
#include <glm/vec3.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace AdvViz::SDK::Tools
{
	static_assert(sizeof(dmat4x4) == sizeof(glm::dmat4x4));
	static_assert(sizeof(double3) == sizeof(glm::dvec3));

	class GCSTransform::Impl
	{
	};

	double3 GCSTransform::PositionFromClient(const double3& v)
	{
		return v;
	}
	
	double3 GCSTransform::PositionToClient(const double3& v)
	{
		return v;
	}

	dmat4x4 GCSTransform::MatrixFromClient(const dmat4x4& m)
	{
		return m;
	}

	dmat4x4 GCSTransform::MatrixToClient(const dmat4x4& m)
	{
		return m;
	}

	GCSTransform::Impl& GCSTransform::GetImpl()
	{
		return *impl_;
	}

	const GCSTransform::Impl& GCSTransform::GetImpl() const
	{
		return *impl_;
	};


	GCSTransform::GCSTransform() :impl_(new Impl())
	{
	}

	GCSTransform::~GCSTransform()
	{
	}

	// geodetic to ECEF transform
	// https://en.wikipedia.org/wiki/Geographic_coordinate_conversion#From_geodetic_to_ECEF_coordinates
	// https://gssc.esa.int/navipedia/index.php/Ellipsoidal_and_Cartesian_Coordinates_Conversion
	/*static*/ double3 GCSTransform::WGS84GeodeticToECEF(const double3& latLonHeightRad)
	{
		const double lat = latLonHeightRad[0];
		const double lon = latLonHeightRad[1];
		const double height = latLonHeightRad[2];
		const double a = 6378137.0; // WGS-84 Earth semimajor axis (m), equatorial radius
		// f = 1 / 298.257223563; // WGS-84 flattening factor
		// b = a * (1 - f);
		const double b = 6356752.314245; // WGS-84 Earth semiminor axis (m), polar radius
		const double e2 = 1 - (b * b) / (a * a); // first eccentricity squared
		const double sinLat = sin(lat);
		const double cosLat = cos(lat);
		const double sinLon = sin(lon);
		const double cosLon = cos(lon);
		const double N = a / sqrt(1 - e2 * sinLat * sinLat); // radius of curvature in the prime vertical
		const double x = (N + height) * cosLat * cosLon;
		const double y = (N + height) * cosLat * sinLon;
		const double z = (N * (1 - e2) + height) * sinLat;
		double3 ret = { x, y, z };
		return ret;
	}

    // Returns a 4x4 transformation matrix that converts ECEF (Earth-Centered, Earth-Fixed) coordinates to ENU (East North Up) coordinates at the given geodetic location.
    // The input is a double3: [latitude (rad), longitude (rad), height (meters)].
    // The output matrix transforms ECEF points to the local ENU frame centered at the input location.
	// https://en.wikipedia.org/wiki/Geographic_coordinate_conversion#From_ECEF_to_ENU
    /*static*/ dmat4x4 GCSTransform::WGS84ECEFToENUMatrix(const double3& latLonHeightRad)
    {
        const double lat = latLonHeightRad[0];
        const double lon = latLonHeightRad[1];
        const double height = latLonHeightRad[2];
        const double sinLat = sin(lat);
        const double cosLat = cos(lat);
        const double sinLon = sin(lon);
        const double cosLon = cos(lon);

        // Compute ECEF origin for the ENU frame
        const double a = 6378137.0; // WGS-84 Earth semimajor axis (m), equatorial radius
		// f = 1 / 298.257223563; // WGS-84 flattening factor
		// b = a * (1 - f);
        const double b = 6356752.314245; // WGS-84 Earth semiminor axis (m), polar radius
        const double e2 = 1.0 - (b * b) / (a * a); // first eccentricity squared
        const double N = a / sqrt(1.0 - e2 * sinLat * sinLat); // radius of curvature in the prime vertical
        const double x0 = (N + height) * cosLat * cosLon;
        const double y0 = (N + height) * cosLat * sinLon;
        const double z0 = (N * (1.0 - e2) + height) * sinLat;

        // Rotation matrix from ECEF to ENU
        glm::dmat4x4 enuMatrix(
            -sinLon,	-sinLat * cosLon,	cosLat * cosLon,      0.0,
			cosLon,    -sinLat * sinLon,	cosLat * sinLon, 0.0,
			0.0,		cosLat,				sinLat,          0.0,
             0.0,        0.0,               0.0,             1.0
        );


        // Translation to local origin
        enuMatrix[3] = -enuMatrix * glm::dvec4(x0,y0,z0,1.0);
		enuMatrix[3][3] = 1.0;

        return internal::toSDK(enuMatrix);
    }

	// ENU To ECEF transform
	// Returns a 4x4 transformation matrix that converts ENU (East North Up) coordinates to ECEF (Earth-Centered, Earth-Fixed) coordinates at the given geodetic location.
	// The input is a double3: [latitude (rad), longitude (rad), height (meters)].
	// The output matrix transforms local ENU points to ECEF frame centered at the input location.
	// https://en.wikipedia.org/wiki/Geographic_coordinate_conversion#From_ENU_to_ECEF
	/*static*/ dmat4x4 GCSTransform::WGS84ENUToECEFMatrix(const double3& latLonHeightRad)
	{
		const double lat = latLonHeightRad[0];
		const double lon = latLonHeightRad[1];
		const double height = latLonHeightRad[2];
		const double sinLat = sin(lat);
		const double cosLat = cos(lat);
		const double sinLon = sin(lon);
		const double cosLon = cos(lon);

		// Compute ECEF origin for the ENU frame
		const double a = 6378137.0; // WGS-84 Earth semimajor axis (m), equatorial radius
		// f = 1 / 298.257223563; // WGS-84 flattening factor
		// b = a * (1 - f);
		const double b = 6356752.314245; // WGS-84 Earth semiminor axis (m), polar radius
		const double e2 = 1.0 - (b * b) / (a * a); // first eccentricity squared
		const double N = a / sqrt(1.0 - e2 * sinLat * sinLat); // radius of curvature in the prime vertical
		const double x0 = (N + height) * cosLat * cosLon;
		const double y0 = (N + height) * cosLat * sinLon;
		const double z0 = (N * (1.0 - e2) + height) * sinLat;

		// Rotation matrix from ENU to ECEF
		glm::dmat4x4 enuMatrix(
			-sinLon, cosLon, 0.0, 0.0,
			-sinLat * cosLon,  -sinLat * sinLon, cosLat,  0.0,
			cosLat * cosLon, cosLat * sinLon, sinLat, 0.0,
			0.0, 0.0, 0.0, 1.0
		);

		// Translation to local origin
		enuMatrix[3] = glm::dvec4(x0, y0, z0, 1.0);

		return internal::toSDK(enuMatrix);
	}

	/*static*/ double3 GCSTransform::East(const dmat4x4& enuToEcef)
	{
		return internal::toSDK(glm::dvec3(internal::toGlm(enuToEcef)[0]));
	}
	/*static*/ double3 GCSTransform::North(const dmat4x4& enuToEcef)
	{
		return internal::toSDK(glm::dvec3(internal::toGlm(enuToEcef)[1]));
	}
	/*static*/ double3 GCSTransform::Up(const dmat4x4& enuToEcef)
	{
		return internal::toSDK(glm::dvec3(internal::toGlm(enuToEcef)[2]));
	}

	const GCS& GCSTransform::GetECEFWGS84WKT()
	{
		static GCS wgs84;
		wgs84.wkt =
            "GEOCCS[\"WGS 84 (G2296)\","
            "DATUM[\"World_Geodetic_System_1984_G2296\","
            "SPHEROID[\"WGS 84\", 6378137, 298.257223563,"
            "AUTHORITY[\"EPSG\", \"7030\"]],"
            "AUTHORITY[\"EPSG\", \"1383\"]],"
            "PRIMEM[\"Greenwich\", 0,"
            "AUTHORITY[\"EPSG\", \"8901\"]],"
            "UNIT[\"metre\", 1,"
            "AUTHORITY[\"EPSG\", \"9001\"]],"
            "AXIS[\"Geocentric X\", OTHER],"
            "AXIS[\"Geocentric Y\", OTHER],"
            "AXIS[\"Geocentric Z\", NORTH],"
            "AUTHORITY[\"EPSG\", \"10604\"]]";

		return wgs84;
	}

	DEFINEFACTORYGLOBALS(GCSTransform);
}