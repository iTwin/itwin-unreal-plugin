/*--------------------------------------------------------------------------------------+
|
|     $Source: CutoutPersistenceTest.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#if WITH_TESTS

#include <Tests/CutoutPersistenceTestHelper.h>
#include <Tests/ITwinAutomationTestBaseNoLogs.h>
#include <Tests/ITwinDecorationMockServerBase.h>
#include <Tests/WebTestHelpers.h>

#include <Clipping/ITwinClippingTool.h>
#include <Decoration/ITwinDecorationHelper.h>
#include <Helpers/WorldSingleton.h>
#include <ITwinServerConnection.h>
#include <Population/ITwinPopulationTool.h>
#include <Spline/ITwinSplineTool.h>


#include <Misc/LowLevelTestAdapter.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/ITwinAPI/ITwinAuthManager.h>
#	include <Core/Network/http.h>
#	include <Core/Tools/Tools.h>
#	include <Core/Visualization/AsyncHelpers.h>
#	include <Core/Visualization/Config.h>
#	include <Core/Visualization/ScenePersistenceAPI.h>
#	include <Core/Visualization/Visualization.h>
#include <Compil/AfterNonUnrealIncludes.h>



/// Mock server implementation for annotation persistence
class FCutoutPersistenceMockServer : public FITwinDecorationMockServerBase
{
public:
	static std::unique_ptr<httpmock::MockServer> MakeServer(
		unsigned startPort, unsigned tryCount = 1000);

	explicit FCutoutPersistenceMockServer(int port) : FITwinDecorationMockServerBase(port)
	{
		this->iTwinID = "cutout_test__itwin_id";
		this->decorationID = "6a2d23816c579332387a5b7c";
	}

	virtual Response responseHandler(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) override
	{
		if (url.find("cutout_test__scene_id") != std::string::npos)
		{
			return ProcessSceneRequest(url, method, data, urlArguments, headers);
		}
		return FITwinDecorationMockServerBase::responseHandler(url, method, data, urlArguments, headers);
	}

	virtual bool PostCondition() const override
	{
		return AddedCutouts == 1 && ModifiedCutouts == 2 && DeletedCutouts == 1;
	}

private:
	mutable int32 AddedCutouts = 0;
	mutable int32 ModifiedCutouts = 0;
	mutable int32 DeletedCutouts = 0;

	/// Process Scene API requests
	Response ProcessSceneRequest(
		const std::string& url,
		const std::string& method,
		const std::string& data,
		const std::vector<UrlArg>& urlArguments,
		const std::vector<Header>& headers) const
	{
		CHECK_DECORATION_HEADERS();

		if (method == "GET")
		{
			if (url.find("/objects") != std::string::npos)
			{
				// Scene links.
				return Response(MHD_HTTP_OK,
"{\"objects\":[" \
"{\"id\":\"camera_animation_id\",\"sceneId\":\"cutout_test__scene_id\",\"kind\":\"CameraAnimation\",\"version\":\"1.0.0\",\"data\":{\"input\":[],\"output\":[]},\"createdById\":\"cutout_test__creator_id\",\"lastModifiedById\":\"cutout_test__creator_id\",\"creationTime\":\"2026-06-13T09:27:38.573Z\",\"lastModified\":\"2026-06-13T09:27:38.573Z\",\"displayName\":\"Clip_1\"}," \
"{\"id\":\"cutout_id_001\",\"sceneId\":\"cutout_test__scene_id\",\"kind\":\"Cutout\",\"version\":\"2.0.0\",\"data\":{\"cutout\":{\"box\":{\"center\":{\"x\":0,\"y\":0,\"z\":0},\"halfExtents\":{\"x\":0.5,\"y\":0.5,\"z\":0.5}},\"enabled\":true,\"inverse\":false,\"cutoutType\":\"box\",\"transformFromClip\":[21.546635371170886,-10.746192115285494,9.392337924943506,4201781.654019838,22.652753643196903,9.368234986148734,-8.18798210624115,173892.12495287642,0,6.580846473545899,7.529439533698276,4779453.3974415315,0,0,0,1]},\"appliesTo\":[\"ff4fc2bf-74b2-4a3d-8549-1758cdfd4fb7\"]},\"createdById\":\"cutout_test__creator_id\",\"lastModifiedById\":\"cutout_test__creator_id\",\"creationTime\":\"2026-06-13T10:22:14.442Z\",\"lastModified\":\"2026-06-13T10:22:14.442Z\"}," \
"{\"id\":\"cutout_id_002\",\"sceneId\":\"cutout_test__scene_id\",\"kind\":\"Cutout\",\"version\":\"2.0.0\",\"data\":{\"cutout\":{\"box\":{\"center\":{\"x\":0,\"y\":0,\"z\":0},\"halfExtents\":{\"x\":0.5,\"y\":0.5,\"z\":0.5}},\"enabled\":true,\"inverse\":true,\"cutoutType\":\"box\",\"transformFromClip\":[-50.95719498110071,77.6757552178583,-67.8898082770676,4201983.766883773,-103.16273075370599,-38.36791206744541,33.534147431039486,173913.50880535744,-2.871741874120912e-7,-75.7203084503998,-86.63497922363611,4779274.944489712,0,0,0,1]},\"appliesTo\":[\"ff4fc2bf-74b2-4a3d-8549-1758cdfd4fb7\",\"3cb0c638-b93c-408b-8577-f747ee014f9a\"]},\"createdById\":\"cutout_test__creator_id\",\"lastModifiedById\":\"cutout_test__creator_id\",\"creationTime\":\"2026-06-16T15:02:08.045Z\",\"lastModified\":\"2026-06-16T15:02:08.045Z\"}," \
"{\"id\":\"cutout_id_003\",\"sceneId\":\"cutout_test__scene_id\",\"kind\":\"Cutout\",\"version\":\"2.0.0\",\"data\":{\"cutout\":{\"enabled\":true,\"inverse\":false,\"polygons\":[{\"positions\":[{\"x\":4201741.494479375,\"y\":173974.76499202973,\"z\":4779308.591424717},{\"x\":4201723.4145958,\"y\":173829.37696000715,\"z\":4779296.421915682},{\"x\":4201721.468280662,\"y\":173806.44111783704,\"z\":4779300.055841675},{\"x\":4201765.365047752,\"y\":173736.34794266536,\"z\":4779265.7001373535},{\"x\":4201806.622992235,\"y\":173858.2160864912,\"z\":4779215.778250661},{\"x\":4201803.892565623,\"y\":173977.4322554489,\"z\":4779244.135353529}]}],\"cutoutType\":\"polygonSet\",\"transformFromClip\":[-0.06012948373319449,-0.7515691976894163,0.6569080500880361,4202019.134823243,0.998190584447361,-0.0452417221547711,0.03960736922727588,174106.46325707724,-0.00004802722941682955,0.6581010011092195,0.7529296580906,4779155.03804391,0,0,0,1]},\"appliesTo\":[\"3cb0c638-b93c-408b-8577-f747ee014f9a\"]},\"createdById\":\"cutout_test__creator_id\",\"lastModifiedById\":\"cutout_test__creator_id\",\"creationTime\":\"2026-06-13T10:22:14.480Z\",\"lastModified\":\"2026-06-13T10:22:14.480Z\"}," \
"{\"id\":\"cutout_id_004\",\"sceneId\":\"cutout_test__scene_id\",\"kind\":\"Cutout\",\"version\":\"2.0.0\",\"data\":{\"cutout\":{\"enabled\":true,\"inverse\":false,\"polygons\":[{\"positions\":[{\"x\":4201741.494479375,\"y\":173974.76499202973,\"z\":4779308.591424717},{\"x\":4201734.822342576,\"y\":173898.2326255052,\"z\":4779315.563704691},{\"x\":4201748.441610004,\"y\":173860.61409067886,\"z\":4779307.455226256},{\"x\":4201761.404663791,\"y\":173859.0740487716,\"z\":4779296.808729281},{\"x\":4201769.253193984,\"y\":173807.63715538787,\"z\":4779295.726632206},{\"x\":4201816.690466359,\"y\":173788.2103319259,\"z\":4779250.068623566},{\"x\":4201822.495093874,\"y\":173847.97702837214,\"z\":4779237.060980866},{\"x\":4201784.136396374,\"y\":173891.60466183093,\"z\":4779270.239658831},{\"x\":4201790.731344797,\"y\":173969.85047088325,\"z\":4779259.116086721}]}],\"cutoutType\":\"polygonSet\",\"transformFromClip\":[-0.04136994761033972,-0.7522807731997089,0.6575425200766609,4201879.812607157,0.9991438971752955,-0.031139842087777665,0.027235692980180963,174160.75069950253,-0.000013117932573818573,0.6581063352595813,0.752924997140567,4779337.861173446,0,0,0,1]},\"appliesTo\":[\"ff4fc2bf-74b2-4a3d-8549-1758cdfd4fb7\"]},\"createdById\":\"cutout_test__creator_id\",\"lastModifiedById\":\"cutout_test__creator_id\",\"creationTime\":\"2026-06-13T10:22:14.532Z\",\"lastModified\":\"2026-06-16T15:02:06.733Z\"}," \
"{\"id\":\"cutout_id_005\",\"sceneId\":\"cutout_test__scene_id\",\"kind\":\"Cutout\",\"version\":\"2.0.0\",\"data\":{\"cutout\":{\"box\":{\"center\":{\"x\":0,\"y\":0,\"z\":0},\"halfExtents\":{\"x\":0.5,\"y\":0.5,\"z\":0.5}},\"enabled\":true,\"inverse\":false,\"cutoutType\":\"box\",\"transformFromClip\":[32.07465464587452,-12.341032231405185,10.786252824859133,4201839.383098672,33.31984020588655,10.302297776399126,-9.004367415104973,174018.4988427608,0,45.02550393320842,51.51568429717785,4779420.168909018,0,0,0,1]},\"appliesTo\":[\"ad358f03-5488-44e4-bc1f-42a610b99694\"]},\"createdById\":\"cutout_test__creator_id\",\"lastModifiedById\":\"cutout_test__creator_id\",\"creationTime\":\"2026-06-13T10:22:14.545Z\",\"lastModified\":\"2026-06-13T10:22:14.545Z\"}," \
"{\"id\":\"cutout_id_006\",\"sceneId\":\"cutout_test__scene_id\",\"kind\":\"Cutout\",\"version\":\"2.0.0\",\"data\":{\"cutout\":{\"plane\":{\"normal\":{\"x\":0.5612089228842849,\"y\":-0.6844755700846543,\"z\":-0.4653361568074723},\"distance\":14831.499723099172},\"enabled\":true,\"cutoutType\":\"plane\"},\"appliesTo\":[\"ff4fc2bf-74b2-4a3d-8549-1758cdfd4fb7\"]},\"createdById\":\"cutout_test__creator_id\",\"lastModifiedById\":\"cutout_test__creator_id\",\"creationTime\":\"2026-06-13T10:22:14.405Z\",\"lastModified\":\"2026-06-13T10:22:14.405Z\"}," \
"{\"id\":\"cutout_id_007\",\"sceneId\":\"cutout_test__scene_id\",\"kind\":\"Cutout\",\"version\":\"2.0.0\",\"data\":{\"cutout\":{\"plane\":{\"normal\":{\"x\":-0.6792765925214784,\"y\":0.727213499200694,\"z\":-0.09871087798561892},\"distance\":-3199530.1187569047},\"enabled\":false,\"cutoutType\":\"plane\"},\"appliesTo\":[\"ff4fc2bf-74b2-4a3d-8549-1758cdfd4fb7\"]},\"createdById\":\"cutout_test__creator_id\",\"lastModifiedById\":\"cutout_test__creator_id\",\"creationTime\":\"2026-06-16T15:02:07.463Z\",\"lastModified\":\"2026-06-16T15:02:07.463Z\"}," \
"{\"id\":\"google_styling_id\",\"sceneId\":\"cutout_test__scene_id\",\"kind\":\"GoogleTilesStyling\",\"version\":\"1.0.0\",\"data\":{\"quality\":0.0030000001192092896,\"adjustment\":[0.30000001192092896,48.84603980855607,2.371000031296311,33.54302597045898]},\"createdById\":\"cutout_test__creator_id\",\"lastModifiedById\":\"cutout_test__creator_id\",\"creationTime\":\"2026-06-13T09:27:39.038Z\",\"lastModified\":\"2026-06-13T09:27:39.038Z\"}," \
"{\"id\":\"imodel_viz_001\",\"sceneId\":\"cutout_test__scene_id\",\"kind\":\"iModelVisibility\",\"version\":\"1.0.0\",\"data\":{\"models\":{\"shownList\":\"\",\"hiddenList\":\"\"},\"adjustment\":[1,0,0,0,0,1,0,0,0,0,1,12000],\"categories\":{\"shownList\":\"\",\"hiddenList\":\"\"}},\"createdById\":\"cutout_test__creator_id\",\"lastModifiedById\":\"cutout_test__creator_id\",\"creationTime\":\"2026-06-13T09:27:38.222Z\",\"lastModified\":\"2026-06-13T09:27:38.222Z\",\"displayName\":\"adjustment\",\"relatedId\":\"repository_imodel_001\"}," \
"{\"id\":\"imodel_viz_002\",\"sceneId\":\"cutout_test__scene_id\",\"kind\":\"iModelVisibility\",\"version\":\"1.0.0\",\"data\":{\"models\":{\"shownList\":\"\",\"hiddenList\":\"\"},\"adjustment\":[1,0,0,0,0,1,0,0,0,0,1,15000],\"categories\":{\"shownList\":\"\",\"hiddenList\":\"\"}},\"createdById\":\"cutout_test__creator_id\",\"lastModifiedById\":\"cutout_test__creator_id\",\"creationTime\":\"2026-06-13T09:27:38.206Z\",\"lastModified\":\"2026-06-13T09:27:38.206Z\",\"displayName\":\"adjustment\",\"relatedId\":\"repository_imodel_002\"}," \
"{\"id\":\"material_deco_id\",\"sceneId\":\"cutout_test__scene_id\",\"kind\":\"MaterialDecoration\",\"version\":\"1.0.0\",\"data\":{\"decorationId\":\"6a2d2381-6c57-9332-387a-5b7c00000000\"},\"createdById\":\"cutout_test__creator_id\",\"lastModifiedById\":\"cutout_test__creator_id\",\"creationTime\":\"2026-06-13T09:31:46.823Z\",\"lastModified\":\"2026-06-13T09:31:46.823Z\",\"displayName\":\"decoration\"}," \
"{\"id\":\"movie_id\",\"sceneId\":\"cutout_test__scene_id\",\"kind\":\"Movie\",\"version\":\"1.0.0\",\"data\":{\"animations\":[\"camera_animation_id\"]},\"createdById\":\"cutout_test__creator_id\",\"lastModifiedById\":\"cutout_test__creator_id\",\"creationTime\":\"2026-06-13T09:27:38.991Z\",\"lastModified\":\"2026-06-13T09:27:38.991Z\"}," \
"{\"id\":\"repository_imodel_002\",\"sceneId\":\"cutout_test__scene_id\",\"kind\":\"RepositoryResource\",\"version\":\"1.0.0\",\"data\":{\"id\":\"ad358f03-5488-44e4-bc1f-42a610b99694\",\"class\":\"iModels\",\"iTwinId\":\"cutout_test__itwin_id\",\"repositoryId\":\"imodels\"},\"createdById\":\"cutout_test__creator_id\",\"lastModifiedById\":\"cutout_test__creator_id\",\"creationTime\":\"2026-06-13T09:27:38.191Z\",\"lastModified\":\"2026-06-13T09:27:38.191Z\",\"displayName\":\"MetroStation\",\"order\":2,\"visible\":true}," \
"{\"id\":\"repository_imodel_001\",\"sceneId\":\"cutout_test__scene_id\",\"kind\":\"RepositoryResource\",\"version\":\"1.0.0\",\"data\":{\"id\":\"ff4fc2bf-74b2-4a3d-8549-1758cdfd4fb7\",\"class\":\"iModels\",\"iTwinId\":\"cutout_test__itwin_id\",\"repositoryId\":\"imodels\"},\"createdById\":\"cutout_test__creator_id\",\"lastModifiedById\":\"cutout_test__creator_id\",\"creationTime\":\"2026-06-13T09:27:38.176Z\",\"lastModified\":\"2026-06-13T09:27:38.176Z\",\"displayName\":\"Building-Julot\",\"order\":1,\"visible\":true}," \
"{\"id\":\"atmo_styling_id\",\"sceneId\":\"cutout_test__scene_id\",\"kind\":\"UnrealAtmosphericStyling\",\"version\":\"1.0.0\",\"data\":{\"atmosphere\":{\"fog\":0,\"weather\":0,\"exposure\":0,\"sunPitch\":-10,\"windForce\":0,\"sunAzimuth\":0,\"useHeliodon\":true,\"heliodonDate\":\"2024-06-16T18:00:00.000Z\",\"windOrientation\":0,\"heliodonLatitude\":48.846038818359375,\"heliodonLongitude\":2.371000051498413}},\"createdById\":\"cutout_test__creator_id\",\"lastModifiedById\":\"cutout_test__creator_id\",\"creationTime\":\"2026-06-13T09:27:39.024Z\",\"lastModified\":\"2026-06-13T09:27:39.024Z\"}," \
"{\"id\":\"view_3d_id_001\",\"sceneId\":\"cutout_test__scene_id\",\"kind\":\"View3d\",\"version\":\"1.0.0\",\"data\":{\"up\":{\"x\":0,\"y\":0,\"z\":0},\"far\":10000000000,\"near\":0.1,\"position\":{\"x\":0,\"y\":0,\"z\":0},\"direction\":{\"x\":0,\"y\":0,\"z\":0},\"aspectRatio\":1,\"ecefTransform\":[0.8660254037844386,0,0.5000000000000001,-40000,0,1,0,-10000,-0.5000000000000001,0,0.8660254037844386,35000,0,0,0,1],\"isOrthographic\":false},\"createdById\":\"cutout_test__creator_id\",\"lastModifiedById\":\"cutout_test__creator_id\",\"creationTime\":\"2026-06-13T09:27:39.059Z\",\"lastModified\":\"2026-06-13T09:27:39.059Z\",\"displayName\":\"Home Camera\"}," \
"{\"id\":\"view_3d_id_002\",\"sceneId\":\"cutout_test__scene_id\",\"kind\":\"View3d\",\"version\":\"1.0.0\",\"data\":{\"up\":{\"x\":0,\"y\":0,\"z\":0},\"far\":10000000000,\"near\":0.1,\"position\":{\"x\":0,\"y\":0,\"z\":0},\"direction\":{\"x\":0,\"y\":0,\"z\":0},\"aspectRatio\":1,\"ecefTransform\":[-0.26676208256468925,0.7144726481711545,-0.6468128217042756,8535.137309663434,-0.2724084452816592,-0.6996633726409414,-0.6605034473217619,13662.230726899099,-0.9244628874351736,-5.162537064506978e-15,0.38127204166450646,35478.02541327747,0,0,0,1],\"isOrthographic\":false},\"createdById\":\"cutout_test__creator_id\",\"lastModifiedById\":\"cutout_test__creator_id\",\"creationTime\":\"2026-06-13T09:27:39.206Z\",\"lastModified\":\"2026-06-16T15:02:08.019Z\",\"displayName\":\"Main Camera\"}" \
"],\"sceneContext\":{\"displayName\":\"Cutout 4.0 (SceneAPI)\",\"lastModified\":\"2026-06-16T15:02:08.045Z\",\"isPartial\":false}," \
"\"_links\":{\"self\":{\"href\":\"https://api.bentley.com/scenes/cutout_test__scene_id/objects?iTwinId=cutout_test__itwin_id&$skip=0&$top=100\"}}}"
				);
			}
			else
			{
				// Scene.
				return Response(MHD_HTTP_OK,
"{\"scene\":{\"id\":\"cutout_test__scene_id\",\"displayName\":\"Cutout 4.0 (SceneAPI)\",\"iTwinId\":\"cutout_test__itwin_id\",\"tags\":[],\"createdById\":\"cutout_test__creator_id\",\"lastModifiedById\":\"cutout_test__creator_id\",\"creationTime\":\"2026-06-13T09:18:57.633Z\",\"lastModified\":\"2026-06-16T15:02:08.045Z\"," \
"\"sceneData\":{\"objects\":{\"href\":\"https://api.bentley.com/scenes/cutout_test__scene_id/objects?iTwinId=cutout_test__itwin_id\"}}}}"
					);

			}
		}

		if (method == "PATCH")
		{
			if (url == "/scenes/cutout_test__scene_id")
			{
				return Response(MHD_HTTP_OK,
					"{\"scene\":{\"id\":\"cutout_test__scene_id\",\"displayName\":\"Cutout 4.0 (SceneAPI)\",\"iTwinId\":\"cutout_test__itwin_id\"}}"
				);
			}
			else if (url.starts_with("/scenes/cutout_test__scene_id/objects/"))
			{
				// When saving the scene, a lot of unchanged links are sent back to the server, but we only
				// check the ones related to cutouts here.
				if (url == "/scenes/cutout_test__scene_id"
					|| url.find("google_styling_id") != std::string::npos
					|| url.find("atmo_styling_id") != std::string::npos
					|| url.find("imodel_viz_") != std::string::npos
					|| url.find("repository_") != std::string::npos
					|| url.find("view_3d_id_") != std::string::npos
					|| url.find("material_deco_id") != std::string::npos
					|| url.find("camera_animation_id") != std::string::npos
					|| url.find("movie_id") != std::string::npos)
				{
					return Response(MHD_HTTP_OK,
						"{\"object\":{\"id\":\"dummy_id\"}}"
					);
				}
				else if (url.find("cutout_id_002") != std::string::npos)
				{
					if (data == "{\"data\":{\"cutout\":{\"cutoutType\":\"box\",\"enabled\":true,\"inverse\":false," \
								"\"transformFromClip\":[-50.95719498110071,77.6757552178583,-67.8898082770676,4201983.766883773,-103.16273075370599,-38.36791206744541,33.534147431039486,173913.50880535744,-2.871741874120912e-7,-75.7203084503998,-86.63497922363611,4779274.944489712,0.0,0.0,0.0,1.0]," \
								"\"box\":{\"center\":{\"x\":0.0,\"y\":0.0,\"z\":0.0},\"halfExtents\":{\"x\":0.5,\"y\":0.5,\"z\":0.5}}},\"appliesTo\":[\"ff4fc2bf-74b2-4a3d-8549-1758cdfd4fb7\",\"3cb0c638-b93c-408b-8577-f747ee014f9a\"]}}")
					{
						ModifiedCutouts++;
						return Response(MHD_HTTP_OK,
							"{\"object\":{\"id\":\"cutout_id_002\"}}"
						);
					}
					else
					{
						return Response(MHD_HTTP_BAD_REQUEST, "Invalid data for cutout_id_002.");
					}
				}
				else if (url.find("cutout_id_007") != std::string::npos)
				{
					if (data == "{\"data\":{\"cutout\":{\"cutoutType\":\"plane\",\"enabled\":true,\"plane\":{\"normal\":{\"x\":-0.6792765925214784,\"y\":0.727213499200694,\"z\":-0.09871087798561892},\"distance\":-3199530.1187569047}},\"appliesTo\":[\"ff4fc2bf-74b2-4a3d-8549-1758cdfd4fb7\"]}}")
					{
						ModifiedCutouts++;
						return Response(MHD_HTTP_OK,
							"{\"object\":{\"id\":\"cutout_id_007\"}}"
						);
					}
					else
					{
						return Response(MHD_HTTP_BAD_REQUEST, "Invalid data for cutout_id_007.");
					}
				}
			}
		}

		if (method == "DELETE")
		{
			if (url == "/scenes/cutout_test__scene_id/objects/cutout_id_001" && data == "{}")
			{
				DeletedCutouts++;
				return Response(MHD_HTTP_OK, "{}");
			}
		}

		if (method == "POST")
		{
			if (url == "/scenes/cutout_test__scene_id/objects"
				&& data == "{\"objects\":[{\"version\":\"2.0.0\",\"kind\":\"Cutout\",\"data\":{\"cutout\":{\"cutoutType\":\"box\",\"enabled\":false,\"inverse\":false," \
				"\"transformFromClip\":[10.0,0.0,0.0,0.0,0.0,10.0,0.0,0.0,0.0,0.0,10.0,0.0,0.0,0.0,0.0,1.0],\"box\":{\"center\":{\"x\":0.0,\"y\":0.0,\"z\":0.0}," \
				"\"halfExtents\":{\"x\":0.5,\"y\":0.5,\"z\":0.5}}},\"appliesTo\":[]}}]}")
			{
				AddedCutouts++;
				return Response(MHD_HTTP_CREATED,
					"{\"objects\":[{\"id\":\"cutout_id_008\"}]}"
				);
			}
		}
		return Response(MHD_HTTP_NOT_FOUND, "Page not found.");
	}

};

/*static*/
std::unique_ptr<httpmock::MockServer> FCutoutPersistenceMockServer::MakeServer(
	unsigned startPort, unsigned tryCount /*= 1000*/)
{
	return httpmock::getFirstRunningMockServer<FCutoutPersistenceMockServer>(startPort, tryCount);
}


class UCutoutPersistenceTestHelper::FImpl : public FITwinAPITestHelperBase
{
public:
	FImpl() {}
	~FImpl();

	AITwinClippingTool* GetCutoutsMngr() const { return ClippingTool; }
	AITwinDecorationHelper* GetDecorationHelper() const { return DecoHelper; }
	AITwinPopulationTool* GetPopulationTool() const { return PopulationTool; }

	std::shared_ptr<AdvViz::SDK::Http> GetHttp() const { return Http; }

	void SetDecorationLoadedHandle(FDelegateHandle Handle) { OnDecorationLoadedHandle = Handle; }
	void ResetDecorationLoadedHandle();

protected:
	virtual bool DoInit(AdvViz::SDK::EITwinEnvironment) override;
	virtual void DoCleanup() override;

private:
	TObjectPtr<AITwinClippingTool> ClippingTool;
	TObjectPtr<AITwinPopulationTool> PopulationTool;
	TObjectPtr<AITwinSplineTool> SplineTool;

	TObjectPtr<AITwinServerConnection> ServerConnection;

	TObjectPtr<AITwinDecorationHelper> DecoHelper;

	FDelegateHandle OnDecorationLoadedHandle;

	std::shared_ptr<AdvViz::SDK::Http> Http;
};

namespace ITwin::UnitTests
{
	void InitLogs()
	{
		using namespace AdvViz::SDK;
		Tools::InitLog("log_iTwinRuntime_Test.txt");
		CreateAdvVizLogChannels();
	}
}


bool UCutoutPersistenceTestHelper::FImpl::DoInit(AdvViz::SDK::EITwinEnvironment Env)
{
	/// Port number server is tried to listen on
	/// Number is being incremented while free port has not been found.
	static constexpr int DEFAULT_SERVER_PORT = 8110;

	if (!InitServer(FCutoutPersistenceMockServer::MakeServer(DEFAULT_SERVER_PORT)))
	{
		return false;
	}

	ensure(IsInGameThread());
	AdvViz::SDK::InitMainThreadId();

	// Use our local mock server's URL
	Http.reset(AdvViz::SDK::Http::New());
	Http->SetBaseUrl(GetServerUrl().c_str());
	Http->SetAccessToken(AdvViz::SDK::ITwinAuthManager::GetInstance(Env)->GetAccessToken());
	AdvViz::SDK::SetSupportAsyncCallbacksInMainThread(Http->SupportsExecuteAsyncCallbackInMainThread());

	ITwin::UnitTests::InitLogs();

	auto World = FITwinAPITestHelperBase::GetTestWorld();
	ensure(World);

	// Also initialize Scene API configuration to point to our mock server.
	DecoHelper = TWorldSingleton<AITwinDecorationHelper>().Get(World);
	if (ensure(DecoHelper))
	{
		DecoHelper->SetMockServerPort(GetMockServerPort());
	}

	PopulationTool = TWorldSingleton<AITwinPopulationTool>().Get(World);
	ClippingTool = TWorldSingleton<AITwinClippingTool>().Get(World);
	SplineTool = TWorldSingleton<AITwinSplineTool>().Get(World);

	if (ensure(ClippingTool && DecoHelper && PopulationTool && SplineTool))
	{
		ClippingTool->ConnectPersistenceManager(DecoHelper);
		PopulationTool->SetDecorationHelper(DecoHelper);

		// Connect Clipping Tool (cutout) to the Population & Spline Tools.
		ClippingTool->ConnectPopulationTool(PopulationTool);
		ClippingTool->ConnectSplineTool(SplineTool);
	}

	ServerConnection = World->SpawnActor<AITwinServerConnection>();
	ServerConnection->Environment = static_cast<EITwinEnvironment>(Env);

	AdvViz::SDK::GetDefaultHttp()->SetAccessToken(
		AdvViz::SDK::ITwinAuthManager::GetInstance(Env)->GetAccessToken());

	// Do not reset the default configuration: we have just customized it...
	constexpr bool bResetConfig = false;
	DecoHelper->InitDecorationService(bResetConfig);

	if (SplineTool)
	{
		DecoHelper->ConnectSplineToolToSplinesManager(SplineTool.Get());
	}

	return true;
}

void UCutoutPersistenceTestHelper::FImpl::ResetDecorationLoadedHandle()
{
	if (OnDecorationLoadedHandle.IsValid() && ensure(DecoHelper))
	{
		DecoHelper->OnDecorationLoaded.Remove(OnDecorationLoadedHandle);
		OnDecorationLoadedHandle.Reset();
	}
}

void UCutoutPersistenceTestHelper::FImpl::DoCleanup()
{
	ResetDecorationLoadedHandle();
}

UCutoutPersistenceTestHelper::FImpl::~FImpl()
{
	Cleanup();
}



UCutoutPersistenceTestHelper::UCutoutPersistenceTestHelper()
	: Super()
	, Impl(MakePimpl<FImpl>())
{

}

void UCutoutPersistenceTestHelper::OnReset()
{
	// Important: Reset the handle to ensure the TStrongObjectPtr it contains will be reset, and thus
	// will not prevent ClippingTool and DecoHelper from being destroyed (which blocks the destruction
	// of the world by the GC).
	Impl->ResetDecorationLoadedHandle();
}

bool UCutoutPersistenceTestHelper::Init()
{
	return Impl->Init();
}

bool UCutoutPersistenceTestHelper::PostCondition() const
{
	return Impl->PostCondition();
}

void UCutoutPersistenceTestHelper::OnSceneSaved(bool bSuccess)
{
	Impl->GetAsyncCallback()->OnRequestDone();
}


DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FNUTWaitForAsyncCutoutSaving, FITwinIOAsyncCallbackPtr, CutoutAsyncCallback);

bool FNUTWaitForAsyncCutoutSaving::Update()
{
	if (!CutoutAsyncCallback || CutoutAsyncCallback->IsDone())
	{
		UCutoutPersistenceTestHelper::ResetInstance();
		return true;
	}
	else
	{
		return false;
	}
}


IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FCutoutPersistenceTest, FITwinAutomationTestBaseNoLogs, \
	"Bentley.ITwinForUnreal.ITwinRuntime.CutoutPersistence", \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)


bool FCutoutPersistenceTest::RunTest(const FString& /*Parameters*/)
{
	UCutoutPersistenceTestHelper& HelperObject = UCutoutPersistenceTestHelper::Instance();

	auto& Helper = HelperObject.GetImpl();

	if (!Helper.Init())
	{
		return false;
	}

	auto* ClippingTool = Helper.GetCutoutsMngr();
	auto* DecoHelper = Helper.GetDecorationHelper();
	auto* PopulationTool = Helper.GetPopulationTool();
	FITwinIOAsyncCallbackPtr CutoutAsyncCallback = Helper.GetAsyncCallback();

	const std::string url = Helper.GetServerUrl();

	UWorld* const World = FITwinAPITestHelperBase::GetTestWorld();

	SECTION("Load cutouts from Scene API")
	{
		UTEST_TRUE(TEXT("Check world"), World != nullptr);

		DecoHelper->SetLoadedITwinId(TEXT("cutout_test__itwin_id"));
		DecoHelper->SetLoadedSceneId(TEXT("cutout_test__scene_id"));

		const auto DelegateHandle = DecoHelper->OnDecorationLoaded.AddLambda(
			[this, CutoutAsyncCallback,
			 ClippingTool = TStrongObjectPtr<AITwinClippingTool>(ClippingTool),
			 PopulationTool = TStrongObjectPtr<AITwinPopulationTool>(PopulationTool),
			 DecoHelper = TStrongObjectPtr<AITwinDecorationHelper>(DecoHelper)]()
		{
			auto const ValidateLoadedCutouts = [&]() -> bool
			{
				UTEST_TRUE(TEXT("Num cubes"), ClippingTool->NumEffects(EITwinClippingPrimitiveType::Box) == 3);
				UTEST_TRUE(TEXT("Num planes"), ClippingTool->NumEffects(EITwinClippingPrimitiveType::Plane) == 2);

				UTEST_FALSE(TEXT("Inversion-cube0"), ClippingTool->GetInvertEffect(EITwinClippingPrimitiveType::Box, 0));
				UTEST_TRUE(TEXT("Inversion-cube1"), ClippingTool->GetInvertEffect(EITwinClippingPrimitiveType::Box, 1));
				UTEST_FALSE(TEXT("Inversion-cube2"), ClippingTool->GetInvertEffect(EITwinClippingPrimitiveType::Box, 2));

				UTEST_TRUE(TEXT("Inversion-plane0"), ClippingTool->IsEffectEnabled(EITwinClippingPrimitiveType::Plane, 0));
				UTEST_FALSE(TEXT("Inversion-plane1"), ClippingTool->IsEffectEnabled(EITwinClippingPrimitiveType::Plane, 1));

				//UTEST_TRUE(TEXT("Influence-cube1"), ClippingTool->DoesEffectInfluenceModel(EITwinClippingPrimitiveType::Box, 1,
				//	std::make_pair(EITwinModelType::SomeModelType, FString(TEXT("ff4fc2bf-74b2-4a3d-8549-1758cdfd4fb7")))));

				FTransform Transform;
				double Latitude(0.), Longitude(0.), Elevation(0.);
				UTEST_TRUE(TEXT("GetEffectTransform"), ClippingTool->GetEffectTransform(EITwinClippingPrimitiveType::Box, 2,
					Transform, Latitude, Longitude, Elevation));
				UTEST_TRUE(TEXT("Latitude"), std::fabs(Latitude - 48.84603011) < 1e-6);
				UTEST_TRUE(TEXT("Longitude"), std::fabs(Longitude + 2.37154018) < 1e-6);

				FQuat BoxRotation = Transform.GetRotation();
				BoxRotation.Normalize();
				FVector const EulerAngles = BoxRotation.Euler();
				UTEST_TRUE(TEXT("Rot-x"), std::fabs(EulerAngles.X + 40.475035459) < 1e-6);
				UTEST_TRUE(TEXT("Rot-y"), std::fabs(EulerAngles.Y - 5.537319167) < 1e-6);
				UTEST_TRUE(TEXT("Rot-z"), std::fabs(EulerAngles.Z - 35.566517618) < 1e-6);

				// Cutout polygons cannot be instantiated in the test, because:
				// - it would require we load the linked Model(s) they are attached to (which is far more
				// complex than the scope of this test).
				// - in NullRHI mode (which is used to run our tests), the spline components cannot be
				// created because they require a valid RHI device.
				// So we will just check that the 2 cutout polygons have been loaded in the AdvViz spline
				// manager.
				//UTEST_TRUE(TEXT("Num polygons"), ClippingTool->NumEffects(EITwinClippingPrimitiveType::Polygon) == 2);
				int32 NumPolygons = 0;
				DecoHelper->VisitSplinesWaitingForLinkedModels([&NumPolygons, this](AdvViz::SDK::ISpline const& Spline)
				{
					if (Spline.GetUsage() == AdvViz::SDK::ESplineUsage::MapCutout)
					{
						++NumPolygons;
					}
				});
				UTEST_TRUE(TEXT("Num polygons"), NumPolygons == 2);

				return true;
			};

			auto const TestClippingTool = [&]() -> bool
			{
				// Make a few tests not directly related to persistence, but which require some effects to be
				// relevant.
				auto const InitialSelection = ClippingTool->GetSelectedEffect();
				UTEST_FALSE(TEXT("Has initial selection"), InitialSelection.has_value());

				ClippingTool->SelectEffect(EITwinClippingPrimitiveType::Box, 1);
				auto const SelectedEffect = ClippingTool->GetSelectedEffect();
				UTEST_TRUE(TEXT("Has selected effect"), SelectedEffect.has_value()
					&& SelectedEffect->first == EITwinClippingPrimitiveType::Box
					&& SelectedEffect->second == 1);

				ClippingTool->DeSelectAll();
				auto const FinalSelection = ClippingTool->GetSelectedEffect();
				UTEST_FALSE(TEXT("DeSelectAll"), FinalSelection.has_value());


				AdvViz::SDK::RefID const BoxId_1 = ClippingTool->GetEffectId(EITwinClippingPrimitiveType::Box, 1);
				AdvViz::SDK::RefID const BoxId_2 = ClippingTool->GetEffectId(EITwinClippingPrimitiveType::Box, 2);

				auto const bRemoved = ClippingTool->RemoveEffect(EITwinClippingPrimitiveType::Box, 0, true);
				UTEST_TRUE(TEXT("RemoveEffect"), bRemoved);

				UTEST_TRUE(TEXT("Num cubes"), ClippingTool->NumEffects(EITwinClippingPrimitiveType::Box) == 2);
				UTEST_TRUE(TEXT("Invariant Box ID"), BoxId_1 == ClippingTool->GetEffectId(EITwinClippingPrimitiveType::Box, 0));
				UTEST_TRUE(TEXT("Invariant Box ID"), BoxId_2 == ClippingTool->GetEffectId(EITwinClippingPrimitiveType::Box, 1));
				UTEST_TRUE(TEXT("GetEffectIndex"), 0 == ClippingTool->GetEffectIndex(EITwinClippingPrimitiveType::Box, BoxId_1));
				UTEST_TRUE(TEXT("GetEffectIndex"), 1 == ClippingTool->GetEffectIndex(EITwinClippingPrimitiveType::Box, BoxId_2));
				return true;
			};

			if (ValidateLoadedCutouts())
			{
				// Perform a few tests on clipping tool.
				TestClippingTool();

				// Change a few settings to check that they are saved back to the Scene API.
				ClippingTool->SetInvertEffect(EITwinClippingPrimitiveType::Box, 0, false);
				ClippingTool->EnableEffect(EITwinClippingPrimitiveType::Plane, 1, true);

				// Add a new cutout box, which will be saved to the Scene API.
				auto const AddNewCutoutCube = [&]() -> bool
				{
					UTEST_TRUE(TEXT("Start creating cube"), ClippingTool->StartInteractiveEffectCreation(EITwinClippingPrimitiveType::Box));
					PopulationTool->ValidateInteractiveCreation(true);
					UTEST_TRUE(TEXT("Num cubes after creation"), ClippingTool->NumEffects(EITwinClippingPrimitiveType::Box) == 3);
					return true;
				};
				AddNewCutoutCube();

				DecoHelper->OnSceneSaved.AddDynamic(&UCutoutPersistenceTestHelper::Instance(),
					&UCutoutPersistenceTestHelper::OnSceneSaved);

				CutoutAsyncCallback->OnRequestStarted();
				DecoHelper->SaveScene(false);
			}

			CutoutAsyncCallback->OnRequestDone();
		});

		Helper.SetDecorationLoadedHandle(DelegateHandle);

		CutoutAsyncCallback->OnRequestStarted();
		DecoHelper->LoadScene();
	}

	ADD_LATENT_AUTOMATION_COMMAND(FNUTWaitForAsyncCutoutSaving(CutoutAsyncCallback));

	return true;
}

#endif // WITH_TESTS
