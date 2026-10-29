/*--------------------------------------------------------------------------------------+
|
|     $Source: MaterialPersistencePayloads.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <string_view>

#define TEST_DECO_ID "679d2cc2ba6b5b82ce6e1ec5"
#define TEST_IMODEL_ID "8eb0fcc5-712b-48b6-a74d-5c80e50008b1"

namespace MatPersistenceTestData
{
	inline constexpr std::string_view GetMaterialsRows = "{\"total_rows\":4,\"rows\":[" \
		"{\"roughness\":0.884214,\"roughnessMap\":\"\u003cMatLibrary\u003e/Roof_Tiles/roughness.png\",\"metallic\":0,\"opacity\":1,\"opacityMap\":\"0xbd64766217cf1f3f_hlfoot.png\",\"normal\":1,\"normalMap\":\"\u003cMatLibrary\u003e/Roof_Tiles/normal.png\",\"ao\":1,\"aoMap\":\"\u003cMatLibrary\u003e/Roof_Tiles/AO.png\",\"albedoMap\":\"\u003cMatLibrary\u003e/Roof_Tiles/color.png\",\"color\":\"#FFFFFF\",\"type\":\"PBR\",\"id\":\"83_8eb0fcc5-712b-48b6-a74d-5c80e50008b1\"}," \
		"{\"roughness\":0.8988662958145142,\"metallic\":0,\"opacity\":1,\"normal\":0.2551162838935852,\"normalMap\":\"0xd4919cc328654b18_Normal.png\",\"albedoMap\":\"0x23a8b53872b49b9a_grid.jpeg\",\"color\":\"#CCC3B3\",\"type\":\"Glass\",\"id\":\"328_8eb0fcc5-712b-48b6-a74d-5c80e50008b1\"}," \
		"{\"roughness\":0.13949279487133026,\"metallic\":0.8144928216934204,\"metallicMap\":\"0xade57343837ffcc8_road_4ln2w_height.jpg\",\"opacity\":1,\"normal\":0.7250000238418579,\"normalMap\":\"0xd4919cc328654b18_Normal.png\",\"ao\":1,\"aoMap\":\"0xbceda97d82965da6_road_4ln2w_ambientOcclusion.jpg\",\"albedoMap\":\"0\",\"color\":\"#296E1D\",\"uvScaling\":[0.5,0.5],\"uvOffset\":[0,0],\"uvRotationAngle\":0,\"type\":\"PBR\",\"id\":\"56_8eb0fcc5-712b-48b6-a74d-5c80e50008b1\"}," \
		"{\"roughness\":0.7457427978515625,\"metallic\":0,\"opacity\":1,\"color\":\"#211A0F\",\"type\":\"PBR\",\"id\":\"53_8eb0fcc5-712b-48b6-a74d-5c80e50008b1\"}],\"_links\":{\"self\":\"https://itwindecoration-eus.bentley.com/advviz/v1/decorations/679d2cc2ba6b5b82ce6e1ec5/materials?$skip=0\u0026$top=1000\"}}";

	inline constexpr std::string_view ExpectedPostBody = "{\"materials\":[{\"id\":\"357_8eb0fcc5-712b-48b6-a74d-5c80e50008b1\",\"displayName\":\"Glass #357\",\"type\":\"Glass\"," \
		"\"color\":\"#7CFF7C\",\"colorEncoding\":\"sRGB\",\"albedoMapFactor\":0.0}]}";

	inline constexpr std::string_view PostResponse = "{\"materials\":[{\"roughness\":0.85,\"metallic\":0.75,\"opacity\":1,\"opacityMap\":\"0x34efa79259bb8be0_Vector 1.png\",\"color\":\"#FFFFFF\",\"uvScaling\":[0.5,0.5],\"uvOffset\":[0,0],\"uvRotationAngle\":0,\"type\":\"Glass\",\"id\":\"357_8eb0fcc5-712b-48b6-a74d-5c80e50008b1\"}]}";
}
