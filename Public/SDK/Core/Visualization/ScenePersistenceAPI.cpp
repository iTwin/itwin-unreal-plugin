/*--------------------------------------------------------------------------------------+
|
|     $Source: ScenePersistenceAPI.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ScenePersistenceAPI.h"
#include "Config.h"
#include "Core/Singleton/singleton.h"
#include "Core/Network/HttpGetWithLink.h"
#include <regex>
#include <chrono>

namespace AdvViz::SDK {


	struct LinkAPI::Impl
	{
		struct SJSonGCS
		{
			std::string wkt;
			std::array<float, 3> center = { 0.f, 0.f, 0.f };
		};
		struct Link
		{
			std::string type;
			std::string ref;
			std::optional < std::string > name;
			std::optional<SJSonGCS> gcs;
			std::optional<bool> visibility;
			std::optional<double> quality;
			std::optional< std::array<double, 12> > transform;

		};
		struct LinkWithId
		{
			std::string type;
			std::string ref;
			std::optional < std::string>  name;
			std::optional<SJSonGCS> gcs;
			std::optional<bool> visibility;
			std::optional<double> quality;
			std::optional< std::array<double, 12> > transform;
			std::string id;

		};
		bool shoudSave_ = false;
		bool shouldDelete_ = false;
		Link link_;
		std::string id_;
		void FromLinkWithId(const LinkWithId& value)
		{
			link_.type = value.type;
			link_.ref = value.ref;
			link_.name = value.name;
			link_.gcs = value.gcs;
			link_.visibility = value.visibility;
			link_.quality = value.quality;
			link_.transform = value.transform;
			id_ = value.id;
		}
		LinkWithId ToLinkWithId() const
		{
			LinkWithId value;
			value.type = link_.type;
			value.ref = link_.ref;
			value.name = link_.name;
			value.gcs = link_.gcs;
			value.visibility = link_.visibility;
			value.quality = link_.quality;
			value.transform = link_.transform;
			value.id = id_;
			return value;
		}
		std::string sublinkId_ = "";
		std::shared_ptr<AdvViz::SDK::LinkAPI> parentLink_;
		int idx_ = -1; //clips needs index

	};

	class Credential
	{
	public:
		std::shared_ptr<AdvViz::SDK::Http> Http_;
		std::shared_ptr<AdvViz::SDK::Http>& GetHttp() { return Http_; }
		Credential()
		{
		}
		void SetDefaultHttp(const std::shared_ptr<AdvViz::SDK::Http>& http)
		{
			Http_.reset(AdvViz::SDK::Http::New());
			Http_->SetAccessToken(http->GetAccessToken());
			auto base = http->GetBaseUrlStr();
			if (base.starts_with("https://dev-"))
			{
				envPrefix = "https://dev-";
			}
			else if (base.starts_with("https://qa-"))
			{
				envPrefix = "https://qa-";
			}
			else
			{
				envPrefix = "https://";
			}
			if (curstomServerConfig.server.port != -1 || !curstomServerConfig.server.server.empty())
			{
				std::string baseUrl = curstomServerConfig.server.server;
				if (curstomServerConfig.server.port >= 0)
				{
					baseUrl += ":" + std::to_string(curstomServerConfig.server.port);
				}
				baseUrl += curstomServerConfig.server.urlapiprefix;
				Http_->SetBaseUrl(baseUrl.c_str());
			}
			else
			{
				Http_->SetBaseUrl((envPrefix + server).c_str());
			}
		}

		Config::SConfig curstomServerConfig;
	private:

		std::string envPrefix;
		static std::string server;
	};
	std::string  Credential::server = "itwinscenes-eus.bentley.com/v1";
	static Credential creds;
	namespace
	{
		static dmat4x3 Identity34 = { 1.0,0,0,0,1.0,0,0,0,1.0,0,0,0 };
	}
	class ScenePersistenceAPI::Impl
	{
	public:
		struct SJsonInEmpty {};

		struct SJSonAtmosphere
		{
			double sunAzimuth = 0;
			double sunPitch = 0;
			double heliodonLongitude = 0;
			double heliodonLatitude = 0;
			std::string heliodonDate;
			double weather = 0;
			double windOrientation = 0;
			double windForce = 0;
			double fog = 0;
			double exposure = 0;
			bool useHeliodon = true;
		};
		struct SJSonSceneSettings
		{
			bool displayGoogleTiles = true;
			double qualityGoogleTiles = 0.30;
			std::optional < std::array<double, 3> > geoLocation;
		};
		struct SJSonEnvironment
		{
			SJSonAtmosphere atmosphere;
			SJSonSceneSettings sceneSettings;
		};
		struct SJsonScene
		{
			std::string name;
			std::string itwinid;
			std::string lastModified;
		};
	public:
		std::string id_;
		SJsonScene jsonScene_;
		SJSonAtmosphere jsonAtmo_;
		SJSonSceneSettings jsonSS_;
		bool shoudSave_ = false;
		std::vector<std::shared_ptr<AdvViz::SDK::LinkAPI>> links_;
		std::shared_ptr<AdvViz::SDK::ITimeline> timeline_;
		std::shared_ptr<Http> http_;

		std::shared_ptr<Http>& GetHttp() { 
			if (http_)
				return http_;
			if (!creds.Http_)
				creds.SetDefaultHttp(GetDefaultHttp());
			return creds.GetHttp();
		}
		void SetHttp(const std::shared_ptr<Http>& http) { http_ = http; }

		bool Create(
			const std::string& name, const std::string& itwinid, bool keepCurrentValues = false)
		{
			struct SJsonIn { std::string displayName;};
			SJsonIn jIn{ name };
			struct SJsonOutData { std::string displayName; std::string id;};
			struct SJsonOut { SJsonOutData scene; };
			SJsonOut jOut;

			long status = GetHttp()->PostJsonJBody(jOut, "iTwins/"+ itwinid + "/scenes", jIn);
			if (status == 200 || status == 201)
			{
				if (!keepCurrentValues)
				{
					jsonScene_.itwinid = itwinid;
					jsonScene_.name = jOut.scene.displayName;
				}
				id_ = jOut.scene.id;
				BE_LOGI("ITwinScene", "Created Scene in Scene API for itwin " << itwinid
					<< " (ID: " << id_ << ")");


				//add atmo and scene settings lnks
				std::shared_ptr<AdvViz::SDK::LinkAPI> link(AdvViz::SDK::LinkAPI::New());
				link->GetImpl().link_.type = "atmosphere";
				links_.push_back(link);
				std::shared_ptr<AdvViz::SDK::LinkAPI> link2(AdvViz::SDK::LinkAPI::New());
				link2->GetImpl().link_.type = "SceneSettings";
				links_.push_back(link2);
				return true;
			}
			else
			{
				BE_LOGW("ITwinScene", "Could not create Scene in Scene API for itwin " << itwinid
					<< ". Http status: " << status);
				return false;
			}



		}
		bool Save()
		{
			struct SJsonIn { std::string displayName; };
			SJsonIn jIn;
			jIn.displayName = jsonScene_.name;
			struct SJsonOutData { std::string displayName; std::string id; std::string iTwinId; };
			struct SJsonOut { SJsonOutData scene; };
			SJsonOut jOut;
			long status = GetHttp()->PatchJsonJBody(jOut, "iTwins/" + jsonScene_.itwinid + "/scenes/" + id_, jIn);
			if (status == 200)
			{
				BE_LOGI("ITwinScene", "Save Scene in Scene API with ID " << id_ <<" itwin " << jsonScene_.itwinid);
				return true;
			}
			else
			{
				BE_LOGW("ITwinScene", "Save Scene in Scene API failedwith ID " << id_ << " itwin " << jsonScene_.itwinid<< " Http status : " << status);
				return false;
			}
		}
		bool Get(const std::string& itwinid, const std::string& id)
		{
			struct SJsonOutData {
				std::string displayName;	std::string id; std::string iTwinId; std::optional<std::string> lastModified;
			};
			struct SJsonOut { SJsonOutData scene; };
			SJsonOut jOut;
			long status = GetHttp()->GetJson(jOut, "iTwins/" + itwinid + "/scenes/" + id);
			if (status == 200)
			{
				jsonScene_.itwinid = jOut.scene.iTwinId;
				jsonScene_.name = jOut.scene.displayName;
				id_ = jOut.scene.id;
				if (jOut.scene.lastModified)
					jsonScene_.lastModified = *jOut.scene.lastModified;
				BE_LOGI("ITwinScene", "Loaded Scene in Scene API with ID " << id_ << "from itwin "<< itwinid);
				return true;
			}
			else
			{
				BE_LOGW("ITwinScene", "Load Scene in Scene API failed. Http status: " << status);
				return false;
			}
		}

		bool Delete()
		{
			std::string s("iTwins/" + jsonScene_.itwinid + "/scenes/" + id_);
			auto status = GetHttp()->Delete(s, "");
			if (status.first != 204)
			{
				BE_LOGW("ITwinScene", "Delete Scene in Scene API failed. Http status: " << status.first);
				return false;
			}
			else
			{
				BE_LOGI("ITwinScene", "Deleted Scene in Scene API with ID " << id_);
				id_ = "";
				jsonScene_ = SJsonScene();
				return true;
			}
		}
	};

	static std::string DSIDtoSceneAPIGuid(const std::string& dsi)
	{
		if (dsi.size() < 24)
		{
			BE_ISSUE("Unexpected size from DSId ", dsi);
			return "";
		}
		return dsi.substr(0, 8) + "-" + dsi.substr(8, 4) + "-" + dsi.substr(12, 4) + "-" + dsi.substr(16, 4) + "-" + dsi.substr(20, 4) + "00000000";
	}
	static std::string SceneAPIGuidtoDSID(const std::string& guid)
	{

		if (guid.size() < 28)
		{
			BE_ISSUE("Unexpected size from SceneAPI Guid", guid);
			return "";
		}
		return guid.substr(0, 8) + guid.substr(9, 4) + guid.substr(14, 4) + guid.substr(19, 4) + guid.substr(24, 4);
	}
	template<>
	Tools::Factory<ScenePersistenceAPI>::Globals::Globals()
	{
		newFct_ = []() {return static_cast<ScenePersistenceAPI*>(new ScenePersistenceAPI()); };
	}


	template<>
	Tools::Factory<ScenePersistenceAPI>::Globals& Tools::Factory<ScenePersistenceAPI>::GetGlobals()
	{
		return singleton<Tools::Factory<ScenePersistenceAPI>::Globals>();
	}

	template<>
	Tools::Factory<LinkAPI>::Globals::Globals()
	{
		newFct_ = []() {return new LinkAPI(); };
	}


	template<>
	Tools::Factory<LinkAPI>::Globals& Tools::Factory<LinkAPI>::GetGlobals()
	{
		return singleton<Tools::Factory<LinkAPI>::Globals>();
	}

	void ScenePersistenceAPI::SetHttp(std::shared_ptr<Http> http)
	{
		GetImpl().SetHttp(http);
	}

	bool ScenePersistenceAPI::Create(const std::string& name, const std::string& itwinid)
	{
		return GetImpl().Create(name, itwinid, false);
	}

	bool ScenePersistenceAPI::Get(const std::string& itwinId, const std::string& id)
	{
		bool res = GetImpl().Get(itwinId, id);
		LoadLinks();
		return res;
	}

	bool ScenePersistenceAPI::Delete()
	{
		return GetImpl().Delete();
	}

	const std::string& ScenePersistenceAPI::GetId() const
	{
		return GetImpl().id_;
	}

	const std::string& ScenePersistenceAPI::GetName() const
	{
		return GetImpl().jsonScene_.name;

	}

	std::string ScenePersistenceAPI::GetLastModified() const
	{
		return GetImpl().jsonScene_.lastModified;
	}


	const std::string& ScenePersistenceAPI::GetITwinId() const
	{
		return GetImpl().jsonScene_.itwinid;
	}

	ScenePersistenceAPI::~ScenePersistenceAPI()
	{
	}

	ScenePersistenceAPI::ScenePersistenceAPI() :impl_(new Impl)
	{
	}

	ScenePersistenceAPI::Impl& ScenePersistenceAPI::GetImpl() const {
		return *impl_;
	}

	void ScenePersistenceAPI::SetAtmosphere(const ITwinAtmosphereSettings& atmo)
	{
		auto& jsonatmo = GetImpl().jsonAtmo_;
		jsonatmo.sunAzimuth = atmo.sunAzimuth;
		jsonatmo.sunPitch = atmo.sunPitch;
		jsonatmo.heliodonLongitude = atmo.heliodonLongitude;
		jsonatmo.heliodonLatitude = atmo.heliodonLatitude;
		jsonatmo.heliodonDate = atmo.heliodonDate;
		jsonatmo.weather = atmo.weather;
		jsonatmo.windOrientation = atmo.windOrientation;
		jsonatmo.windForce = atmo.windForce;
		jsonatmo.fog = atmo.fog;
		jsonatmo.exposure = atmo.exposure;
		jsonatmo.useHeliodon = atmo.useHeliodon;
		GetImpl().shoudSave_ = true;
	}

	AdvViz::SDK::ITwinAtmosphereSettings ScenePersistenceAPI::GetAtmosphere() const
	{
		AdvViz::SDK::ITwinAtmosphereSettings atmo;
		const auto& jsonatmo = GetImpl().jsonAtmo_;
		atmo.sunAzimuth = jsonatmo.sunAzimuth;
		atmo.sunPitch = jsonatmo.sunPitch;
		atmo.heliodonLongitude = jsonatmo.heliodonLongitude;
		atmo.heliodonLatitude = jsonatmo.heliodonLatitude;
		atmo.heliodonDate = jsonatmo.heliodonDate;
		atmo.weather = jsonatmo.weather;
		atmo.windOrientation = jsonatmo.windOrientation;
		atmo.windForce = jsonatmo.windForce;
		atmo.fog = jsonatmo.fog;
		atmo.exposure = jsonatmo.exposure;
		atmo.useHeliodon = jsonatmo.useHeliodon;
		return atmo;
	}

	void ScenePersistenceAPI::SetSceneSettings(const ITwinSceneSettings& ss)
	{
		auto& jsonss = GetImpl().jsonSS_;
		jsonss.displayGoogleTiles = ss.displayGoogleTiles;
		jsonss.qualityGoogleTiles = ss.qualityGoogleTiles;
		jsonss.geoLocation = ss.geoLocation;
		GetImpl().shoudSave_ = true;
	}

	AdvViz::SDK::ITwinSceneSettings ScenePersistenceAPI::GetSceneSettings() const
	{
		const auto& jsonss = GetImpl().jsonSS_;
		AdvViz::SDK::ITwinSceneSettings ss;
		ss.displayGoogleTiles = jsonss.displayGoogleTiles;
		ss.qualityGoogleTiles = jsonss.qualityGoogleTiles;
		ss.geoLocation = jsonss.geoLocation;
		return ss;
	}

	bool ScenePersistenceAPI::ShouldSave() const
	{
		if (GetImpl().shoudSave_) return true;
		for (auto link : GetImpl().links_)
		{
			if (link->GetImpl().shoudSave_)
				return true;
		}
		return false;
	}

	bool ScenePersistenceAPI::Save()
	{
		if (GetImpl().id_.empty() && !GetImpl().jsonScene_.name.empty() && !GetImpl().jsonScene_.itwinid.empty())
		{
			if (!GetImpl().Create(GetImpl().jsonScene_.name, GetImpl().jsonScene_.itwinid, true))
				return false;

		}
		if (ShouldSave())
		{
			bool res = GetImpl().Save();
			SaveLinks();
			GetImpl().shoudSave_ = false;
			BE_LOGI("ITwinScene", "Save Scene end");

			return res;
		}
		return true;
	}

	void ScenePersistenceAPI::SetShouldSave(bool shouldSave) const
	{
		GetImpl().shoudSave_ = shouldSave;
	}

	void ScenePersistenceAPI::PrepareCreation(const std::string& name, const std::string& itwinid)
	{
		GetImpl().jsonScene_.name = name;
		GetImpl().jsonScene_.itwinid = itwinid;
		SetAtmosphere(ITwinAtmosphereSettings());
		SetSceneSettings(ITwinSceneSettings());
		GetImpl().shoudSave_ = false;
	}

	std::vector<std::shared_ptr<AdvViz::SDK::ILink>> ScenePersistenceAPI::GetLinks() const
	{
		std::vector<std::shared_ptr<AdvViz::SDK::ILink>> res;
		for (auto l : GetImpl().links_)
			res.push_back(l);
		return res;


	}

	void ScenePersistenceAPI::LoadLinks()
	{
		std::shared_ptr<Http>& http = GetImpl().GetHttp();
		if (!http)
			return;
		struct JsonVector
		{
			double x = 0.0;
			double y = 0.0;
			double z = 0.0;
		};
		struct SJsonCamera
		{
			JsonVector up;
			JsonVector direction;
			JsonVector position;
			bool isOrthographic = false;
			double aspectRatio = 1.0;
			double far = 10000000000;
			double near = 0.1;
			std::optional< std::array<double, 16> > ecefTransform;
		};
		struct SJsonScheduleSimulation
		{
			std::string timelineId;
			int64_t timePoint;
		};
		struct SJsonSettings
		{
			Impl::SJSonAtmosphere atmosphere;
		};
		struct SJsonFrameData
		{
			SJsonCamera camera;
			std::optional <SJsonSettings> settings;
			std::optional<SJsonScheduleSimulation> schedule;
		};
		struct SJsonFrameCameraData
		{
			std::vector<double> input;
			std::vector<SJsonFrameData> output;
			std::optional< std::string> name;
		};
		struct Data {
			std::optional<bool> visible;
			rfl::Rename<"class", std::optional<std::string>> type;
			std::optional<std::string> repositoryId;
			std::optional<std::string> id;
			std::optional<std::string> name;
			std::optional<double> quality;
			std::optional< std::array<double, 16> > ecefTransform;
			std::optional< std::vector<double> > adjustment;
			std::optional<Impl::SJSonAtmosphere> atmosphere;
			std::optional<std::string> decorationId;
			std::optional<std::vector<std::string>> animations;
			std::optional< std::vector<double>> input;
			std::optional< std::vector<SJsonFrameData>> output;
		};
		struct JsonObjectWithId
		{
			std::string id;
			std::string kind;
			std::optional<Data> data;
			std::optional<std::string> displayName;
			std::optional<std::string> relatedId;
		};
		struct SceneAPIUrl
		{
			std::string href;
		};
		struct SceneAPILinks
		{
			std::optional<SceneAPIUrl> prev;
			std::optional<SceneAPIUrl> self;
			std::optional<SceneAPIUrl> next;
		};
		struct SJsonOut
		{
			std::optional<std::vector<JsonObjectWithId>> objects;
			std::optional<SceneAPILinks> _links;
		};

		SJsonOut jOut;
		long status = http->GetJson(jOut, "iTwins/" + GetImpl().jsonScene_.itwinid + "/scenes/" + GetId() + "/objects", { });
		bool continueLoading = true;
		bool atmosphereFound = false;
		bool sceneSettingsFound = false;
		bool timelineFound = false;
		struct SLinkData
		{
			std::string ref;
			std::vector<double> adjusts;
			std::string id;
			std::optional<std::string> displayName; // used for type for decoration & DecorationScene ( for timeline)
		};
		std::vector<SLinkData> sublinks;
		std::vector<std::string> animations;
		std::map<std::string, SJsonFrameCameraData> cameraDatas;

		while (continueLoading)
		{
			if (status != 200 && status != 201)
			{
				continueLoading = false;
			}
			if (jOut.objects)
			{
				for (auto& row : *jOut.objects)
				{
					std::shared_ptr<AdvViz::SDK::LinkAPI> link(AdvViz::SDK::LinkAPI::New());
					link->GetImpl().id_ = row.id;
					if (row.kind == "RepositoryResource")
					{
						std::optional<std::string> ttype = row.data->type.get();
						if (row.data && ttype.has_value())
						{
							if (*ttype == "iModels")
								link->GetImpl().link_.type = "iModel";
							if (*ttype == "RealityData")
								link->GetImpl().link_.type = "RealityData";
						}
						link->SetType("iModel"); // check row class
						if (row.data && row.data->visible)
							link->GetImpl().link_.visibility = *row.data->visible;
						if (row.data && row.data->id)
							link->GetImpl().link_.ref = *row.data->id;
						GetImpl().links_.push_back(link);
					}
					else if (row.kind == "View3d")
					{
						link->GetImpl().link_.type = "camera";
						if (row.data && row.data->name)
							link->GetImpl().link_.ref = *row.data->name;
						else if (row.displayName)
							link->GetImpl().link_.ref = *row.displayName;
						else
							link->GetImpl().link_.ref = "Home Camera";
						if (row.data && row.data->ecefTransform)
							link->GetImpl().link_.transform = Identity34;
						std::memcpy(link->GetImpl().link_.transform->data(), row.data->ecefTransform->data(), 12 * sizeof(double));
						GetImpl().links_.push_back(link);
					}
					else if (row.kind == "UnrealAtmosphericStyling")
					{
						link->GetImpl().link_.type = "atmosphere";
						if (row.data && row.data->atmosphere)
						{
							GetImpl().jsonAtmo_ = *row.data->atmosphere;
						}
						GetImpl().links_.push_back(link);

						atmosphereFound = true;
					}
					else if (row.kind == "GoogleTilesStyling")
					{
						link->GetImpl().link_.type = "SceneSettings"; // check row class
						if (row.data && row.data->quality)
						{
							GetImpl().jsonSS_.qualityGoogleTiles = *row.data->quality * 100;
						}
						if (row.data && row.data->adjustment)
						{
							auto& sldata = *row.data->adjustment;
							int geolocid = -4;
							int qualityId = -1;
							if (sldata.size() == 1)
							{
								qualityId = 0;
							}
							else if (sldata.size() == 3)
							{
								geolocid = 0;
							}
							else if (sldata.size() == 4)
							{
								qualityId = 0;
								geolocid = 1;
							}
							if (qualityId >= 0)
							{
								if (sldata[qualityId] < -1e-7)
								{
									GetImpl().jsonSS_.qualityGoogleTiles = -sldata[qualityId];
									GetImpl().jsonSS_.displayGoogleTiles = false;
								}
								else
								{
									GetImpl().jsonSS_.qualityGoogleTiles = sldata[qualityId];
									GetImpl().jsonSS_.displayGoogleTiles = true;
								}
							}
							if (geolocid >= 0)
							{
								GetImpl().jsonSS_.geoLocation = std::array<double, 3>();
								(*GetImpl().jsonSS_.geoLocation)[0] = sldata[geolocid];
								(*GetImpl().jsonSS_.geoLocation)[1] = sldata[geolocid + 1];
								(*GetImpl().jsonSS_.geoLocation)[2] = sldata[geolocid + 2];
							}
						}
						GetImpl().links_.push_back(link);
						sceneSettingsFound = true;
					}
					else if (row.kind == "MaterialDecoration")
					{
						if (row.displayName)
							link->GetImpl().link_.type = *row.displayName;
						else
							link->GetImpl().link_.type = "decoration";
						if (row.data && row.data->id)
							link->GetImpl().link_.ref = *row.data->id;
						if (row.data && row.data->decorationId)
						{
							link->GetImpl().link_.ref = SceneAPIGuidtoDSID(*row.data->decorationId);
							GetImpl().links_.push_back(link);
						}
					}
					else if (row.kind == "iModelVisibility")
					{
						if (row.data && row.data->id && row.data->adjustment)
						{
							SLinkData sl;
							sl.id = row.id;
							sl.ref = *row.data->id;
							sl.adjusts = *row.data->adjustment;
							sublinks.push_back(sl);
						}
						else if (row.relatedId && row.data && row.data->adjustment)
						{
							SLinkData sl;
							sl.id = row.id;
							sl.ref = *row.relatedId;
							sl.adjusts = *row.data->adjustment;
							sublinks.push_back(sl);
						}
					}
					else if (row.kind == "Movie")
					{
						link->GetImpl().link_.type = "timeline";
						if (row.data && row.data->animations)
						{
							animations = *row.data->animations;
						}
						GetImpl().links_.push_back(link);
						timelineFound = true;
					}
					else if (row.kind == "CameraAnimation")
					{
						if (row.data && row.data->input && row.data->output)
						{
							SJsonFrameCameraData data;
							data.input = *row.data->input;
							data.output = *row.data->output;
							if (row.displayName)
								data.name = *row.displayName;
							cameraDatas[row.id] = data;
						}
					}

				}
				jOut.objects->clear();
			}
			if (jOut._links.has_value() && jOut._links->next.has_value() && !jOut._links->next->href.empty())
			{
				// Quick workaround for a bug in Decoration Service sometimes providing bad URLs with http
				// instead of https protocol! (issue found for instances, which was causing bug #1609088).
				std::string const nextUrl = rfl::internal::strings::replace_all(
					jOut._links->next->href,
					"http://", "https://");
				{
				}
			}
			else
			{
				continueLoading = false;
			}

		}
		for (auto sldata : sublinks)
		{
			for (auto mainlink : GetImpl().links_)
			{
				if (sldata.ref == mainlink->GetImpl().id_)
				{
					mainlink->GetImpl().sublinkId_ = sldata.id;
					int qualityId = -1;
					int geolocid = -4;
					if (sldata.adjusts.size() == 1)
					{
						qualityId = 0;
					}
					else if (sldata.adjusts.size() == 3)
					{
						geolocid = 0;
					}
					else if (sldata.adjusts.size() == 4)
					{
						qualityId = 0;
						geolocid = 1;
					}
					else  if (sldata.adjusts.size() == 13)
					{
						qualityId = 12;
					}
					else if (sldata.adjusts.size() == 5)
					{
						geolocid = 12;
					}
					else  if (sldata.adjusts.size() == 16)
					{
						qualityId = 12;
						geolocid = 13;
					}

					if (qualityId >= 0)
					{
						if (sldata.adjusts[qualityId] < -1e-7)
						{
							mainlink->GetImpl().link_.quality = -sldata.adjusts[qualityId];
							mainlink->GetImpl().link_.visibility = false;
						}
						else
						{
							mainlink->GetImpl().link_.quality = sldata.adjusts[qualityId];
							mainlink->GetImpl().link_.visibility = true;
						}
					}

					if (sldata.adjusts.size() > 11)
					{
						mainlink->GetImpl().link_.transform = std::array<double, 12>();
						for (int i = 0; i < 12; ++i)
						{
							(*mainlink->GetImpl().link_.transform)[i] = sldata.adjusts[i];
						}
					}

					break;
				}
			}
		}
		BE_LOGI("ITwinScene", "Found " << GetImpl().links_.size() << " Link(s) for scene " << GetId());
		if (!atmosphereFound)
		{
			std::shared_ptr<AdvViz::SDK::LinkAPI> link(AdvViz::SDK::LinkAPI::New());
			link->GetImpl().link_.type = "atmosphere";
			GetImpl().links_.push_back(link);
		}
		if (!sceneSettingsFound)
		{
			std::shared_ptr<AdvViz::SDK::LinkAPI> link(AdvViz::SDK::LinkAPI::New());
			link->GetImpl().link_.type = "SceneSettings";
			GetImpl().links_.push_back(link);
		}
		if (!timelineFound)
		{
			std::shared_ptr<AdvViz::SDK::LinkAPI> link(AdvViz::SDK::LinkAPI::New());
			link->GetImpl().link_.type = "timeline";
			GetImpl().links_.push_back(link);
		}
		else //build timeline
		{
			auto tl = GetTimeline();
			if (!tl)
			{
				GetImpl().timeline_ = std::shared_ptr<AdvViz::SDK::ITimeline>(AdvViz::SDK::ITimeline::New());
				tl = GetTimeline();
			}

			for (auto id : animations)
			{
				auto scam = cameraDatas.find(id);
				if (scam != cameraDatas.end())
				{
					
					std::string name = "unamed clip " + std::to_string(tl->GetClipCount());
					if (scam->second.name)
						name = *scam->second.name;
					auto clipp = tl->AddClip(name);
					clipp->SetId(ITimelineClip::Id(scam->first));
					auto maxid = std::min(scam->second.input.size(), scam->second.output.size());
					for (auto i=0; i<maxid;++i)
					{
						auto& output = scam->second.output[i];
						ITimelineKeyframe::KeyframeData kd;
						kd.time = scam->second.input[i];

						if (output.settings)
						{
							kd.atmo = ITimelineKeyframe::AtmoData();
							kd.atmo->time = output.settings->atmosphere.heliodonDate;
							kd.atmo->cloudCoverage = (float)output.settings->atmosphere.weather;
							kd.atmo->fog = (float)output.settings->atmosphere.fog;
						}

						if (output.schedule)
						{
							time_t datetime(output.schedule->timePoint);
							kd.synchro = ITimelineKeyframe::SynchroData();
							kd.synchro->date = std::format("%FT%TZ", datetime);
							kd.synchro->scheduleId = output.schedule->timelineId;
						}
						else if (output.settings)
						{
							kd.synchro = ITimelineKeyframe::SynchroData();
							kd.synchro->date = kd.atmo->time;
						}
						kd.camera = ITimelineKeyframe::CameraData();
						if(output.camera.ecefTransform && output.camera.ecefTransform->size()>= 12 )
						{ 
							std::memcpy(kd.camera->transform.data(), output.camera.ecefTransform->data(), 12 * sizeof(double));
						}
						else
						{
							kd.camera->isPause = true;
						}
						clipp->AddKeyframe(kd);

					}
				}
			}
		}

	}

	void ScenePersistenceAPI::AddLink(std::shared_ptr<ILink> v)
	{
		auto rv = std::dynamic_pointer_cast<LinkAPI>(v);
		GetImpl().links_.push_back(rv);
		GetImpl().shoudSave_ = true;
	}

	void ScenePersistenceAPI::SaveLinks()
	{
		auto sublinks = GenerateSubLinks();
		auto prelinks = GeneratePreLinks();
		std::shared_ptr<Http>& http = GetImpl().GetHttp();
		auto doforLink = [this, &http](std::vector<std::shared_ptr<AdvViz::SDK::LinkAPI>>& links) {
			for (auto link : links)
			{
				if (link->GetImpl().id_.empty() && !link->GetImpl().shouldDelete_)
				{

					struct SJsonO2
					{
						std::string id;
					};
					struct SJsonOut
					{
						SJsonO2 object;
					};
					SJsonOut jOut;
					if (link->GetImpl().parentLink_)
					{
						if (link->GetImpl().parentLink_->GetId().empty())
						{
							continue; //parent post failed, do not post child
						}
						link->SetRef(link->GetImpl().parentLink_->GetId());
					}

					std::string body = GenerateBody(link, false);
					long status = http->PostJson(jOut, "iTwins/" + GetImpl().jsonScene_.itwinid + "/scenes/" + GetId() + "/objects", body);

					if (status == 200 || status == 201)
					{
						link->GetImpl().id_ = jOut.object.id;
						BE_LOGI("ITwinScene", "Add Link for scene " << GetId()\
							<< " new link Id " << link->GetImpl().id_ << " type : " << link->GetType() << " ref : " << link->GetRef());
						link->GetImpl().shoudSave_ = false;
						if (link->GetImpl().parentLink_)
						{
							link->GetImpl().parentLink_->GetImpl().sublinkId_ = jOut.object.id;
						}
					}
					else
					{
						BE_LOGW("ITwinScene", "Add Link for scene " << GetId() << " failed. Http status : " << status  \
							<< " with link Id " << link->GetImpl().id_ << " type : " << link->GetType() << " ref : " << link->GetRef());
					}
				}
				else if (link->GetImpl().shouldDelete_ && !link->GetImpl().id_.empty())
				{
					if (link->GetImpl().parentLink_)
					{
						if (!link->GetImpl().parentLink_->GetId().empty())
						{
							continue; //parent delete failed, do not delete child
						}
					}
					struct SJsonOutEmpty {};
					struct SJsonInEmpty {};
					SJsonOutEmpty jOut;
					SJsonInEmpty Jin;
					std::string url("iTwins/" + GetImpl().jsonScene_.itwinid + "/scenes/" + GetId() + "/objects/" + link->GetImpl().id_);
					auto status = http->DeleteJsonJBody(jOut, url, Jin);
					if (status != 200)
					{
						BE_LOGW("ITwinScene", "Delete Link failed. Http status: " << status << " sceneid " << GetId() << " link ID " << link->GetImpl().id_ \
							<< " type : " << link->GetType() << " ref : " << link->GetRef());
					}
					else
					{
						BE_LOGI("ITwinScene", "Deleted Link with scene ID " << GetId() << " link ID " << link->GetImpl().id_ \
							<< " type : " << link->GetType() << " ref : " << link->GetRef());
						link->GetImpl().id_.clear();
					}
				}
				else if (!link->GetImpl().id_.empty() && !link->GetImpl().shouldDelete_)
				{

					struct SJsonO2
					{
						std::string id;
					};
					struct SJsonOut
					{
						SJsonO2 object;
					};
					SJsonOut jOut;
					if (link->GetImpl().parentLink_)
					{
						link->SetRef(link->GetImpl().parentLink_->GetId());
					}
					std::string body = GenerateBody(link, true);
					auto status = http->PatchJson(jOut, "iTwins/" + GetImpl().jsonScene_.itwinid + "/scenes/" + GetId() + "/objects/" + link->GetId(), body);
					if (status == 200)
					{
						BE_LOGI("ITwinScene", "Update Link for scene " << GetId() << " with link Id " << link->GetImpl().id_ << " type : " << link->GetType() << " ref : " << link->GetRef());
						link->GetImpl().shoudSave_ = false;
					}
					else
					{
						BE_LOGW("ITwinScene", "Update Link  for scene " << GetId() << " failed. Http status : " << status \
							<< " with link Id " << link->GetImpl().id_ << " type : " << link->GetType() << " ref : " << link->GetRef());
					}

				}
				//else nothing to do, id is empty so the link is not on the server
			}

			std::erase_if(links, [](const std::shared_ptr<LinkAPI>& l)
				{
					return l->GetImpl().shouldDelete_ && l->GetImpl().id_.empty();
				});
			};
		//for timeline
		doforLink(prelinks);
		auto tl = GetTimeline();
		for (auto link : prelinks)
		{
			auto clipp = tl->GetClipByIndex(link->GetImpl().idx_);
			if (!clipp)
				continue;
			if (!link->GetId().empty())
			{
				(*clipp)->SetId(ITimelineClip::Id(link->GetId()));
				(*clipp)->SetShouldSave(link->ShouldSave());

			}
		}
		if (tl)
		{
			for (auto clipp : tl->GetObsoleteClips())
			{
				bool deletionFailed = false;
				for (auto link : prelinks)
				{
					if (clipp->GetId() == link->GetId())
					{
						deletionFailed = true;
						break;
					}
				}
				if (!deletionFailed)
				{
					tl->RemoveObsoleteClip(clipp);
				}
			}
		}
		doforLink(GetImpl().links_);
		doforLink(sublinks);
	}

	std::shared_ptr<AdvViz::SDK::ILink> ScenePersistenceAPI::MakeLink()
	{
		return 	std::shared_ptr<AdvViz::SDK::ILink>(LinkAPI::New());
	}

	std::string ScenePersistenceAPI::GenerateBody(const std::shared_ptr<LinkAPI>& link, bool forPatch)
	{

		if (link->GetType() == "RealityData" || link->GetType() == "iModel")
		{
			struct SJsonInData
			{
				std::string id;
				rfl::Rename<"class", std::string> type;
				std::optional<bool> visible;
				std::string repositoryId;
			};
			if (forPatch)
			{
				struct SJsonIn
				{
					SJsonInData data;
					std::string iTwinId;
				};
				SJsonIn Jin;
				if (link->HasVisibility())
				{
					Jin.data.visible = link->GetVisibility();
				}
				else
				{
					Jin.data.visible = true;
				}
				Jin.data.id = link->GetRef();
				Jin.iTwinId = GetImpl().jsonScene_.itwinid;

				if (link->GetType() == "iModel")
				{
					Jin.data.repositoryId = "iModels";
					Jin.data.type = "iModels";
				}
				else if (link->GetType() == "RealityData")
				{
					Jin.data.repositoryId = "RealityData";
					Jin.data.type = "RealityData";
				}

				return Json::ToString(Jin);
			}
			else
			{
				struct SJsonIn
				{
					std::string version = "1.0.0";
					std::string kind = "RepositoryResource";
					SJsonInData data;
					std::string iTwinId;

				};
				SJsonIn Jin;
				if (link->HasVisibility())
				{
					Jin.data.visible = link->GetVisibility();
				}
				else
				{
					Jin.data.visible = true;
				}
				Jin.data.id = link->GetRef();
				Jin.iTwinId = GetImpl().jsonScene_.itwinid;
				if (link->GetType() == "iModel")
				{
					Jin.data.repositoryId = "iModels";
					Jin.data.type = "iModels";
				}
				else if (link->GetType() == "RealityData")
				{
					Jin.data.repositoryId = "RealityData";
					Jin.data.type = "RealityData";
				}
				return Json::ToString(Jin);
			}

		}
		else if (link->GetType() == "camera")
		{
			struct JsonVector
			{
				double x = 0.0;
				double y = 0.0;
				double z = 0.0;
			};
			struct SJsonInData
			{
				JsonVector up;
				JsonVector direction;
				JsonVector position;
				bool isOrthographic = false;
				double aspectRatio = 1.0;
				double far = 10000000000;
				double near = 0.1;
				std::optional< std::array<double, 16> > ecefTransform;
			};
			SJsonInData data;
			if (link->HasTransform())
			{
				data.ecefTransform = std::array<double, 16>();
				std::memcpy(data.ecefTransform->data(), link->GetTransform().data(), 12 * sizeof(double));
				(*data.ecefTransform)[12] = 0.0;
				(*data.ecefTransform)[13] = 0.0;
				(*data.ecefTransform)[14] = 0.0;
				(*data.ecefTransform)[15] = 1.0;
			}
			if (forPatch)
			{
				struct SJsonIn
				{
					std::optional<std::string> displayName;
					SJsonInData data;
				};
				SJsonIn Jin;
				Jin.data = data;
				Jin.displayName = link->GetRef();
				return Json::ToString(Jin);
			}
			else
			{
				struct SJsonIn
				{
					std::string version = "1.0.0";
					std::string kind = "View3d";
					SJsonInData data;
					std::optional<std::string> displayName;
				};
				SJsonIn Jin;
				Jin.data = data;
				Jin.displayName = link->GetRef();
				return Json::ToString(Jin);
			}
		}
		else if (link->GetType() == "atmosphere")
		{
			struct SJsonInData
			{
				Impl::SJSonAtmosphere atmosphere;
			};
			if (forPatch)
			{
				struct SJsonIn
				{
					SJsonInData data;
				};
				SJsonIn Jin;
				Jin.data.atmosphere = GetImpl().jsonAtmo_;
				return Json::ToString(Jin);
			}
			else
			{
				struct SJsonIn
				{
					std::string version = "1.0.0";
					std::string kind = "UnrealAtmosphericStyling";
					SJsonInData data;
				};
				SJsonIn Jin;
				Jin.data.atmosphere = GetImpl().jsonAtmo_;
				return Json::ToString(Jin);
			}
		}
		else if (link->GetType() == "SceneSettings")
		{
			struct SJsonInData
			{
				double quality;
				std::vector<double> adjustment;
			};
			SJsonInData inData;
			inData.quality = GetImpl().jsonSS_.qualityGoogleTiles / 100;
			double ql = GetImpl().jsonSS_.qualityGoogleTiles;
			if (fabs(ql) < 1e-6)
			{
				ql = 1e-6;
			}
			if (!GetImpl().jsonSS_.displayGoogleTiles)
			{
				ql = -ql;
			}
			inData.adjustment.push_back(ql);

			if (GetImpl().jsonSS_.geoLocation)
			{
				inData.adjustment.push_back((*GetImpl().jsonSS_.geoLocation)[0]);
				inData.adjustment.push_back((*GetImpl().jsonSS_.geoLocation)[1]);
				inData.adjustment.push_back((*GetImpl().jsonSS_.geoLocation)[2]);
			}

			if (forPatch)
			{
				struct SJsonIn
				{
					SJsonInData data;
				};
				SJsonIn Jin;
				Jin.data = inData;
				return Json::ToString(Jin);
			}
			else
			{
				struct SJsonIn
				{
					std::string version = "1.0.0";
					std::string kind = "GoogleTilesStyling";
					SJsonInData data;
				};
				SJsonIn Jin;
				Jin.data = inData;
				return Json::ToString(Jin);
			}
		}
		else if (link->GetType() == "timeline")
		{
			struct SJsonInData
			{
				std::vector<std::string> animations;
			};
			SJsonInData inData;

			auto tl = GetTimeline();
			if (tl)
			{
				for (size_t i(0); i < tl->GetClipCount(); i++)
				{
					auto clipp = tl->GetClipByIndex(i);
					if (!clipp)
						continue;
					std::string id = static_cast<const std::string>((*clipp)->GetId());
					if (!id.empty())
						inData.animations.push_back(id);
				}

			}

			if (forPatch)
			{
				struct SJsonIn
				{
					SJsonInData data;
				};
				SJsonIn Jin;
				Jin.data = inData;
				return Json::ToString(Jin);
			}
			else
			{
				struct SJsonIn
				{
					std::string version = "1.0.0";
					std::string kind = "Movie";
					SJsonInData data;
				};
				SJsonIn Jin;
				Jin.data = inData;
				return Json::ToString(Jin);
			}
		}
		else if (link->GetType() == "clip")
		{
			auto tl = GetTimeline();
			auto clipp = tl->GetClipByIndex(link->GetImpl().idx_);
			if (!clipp)
				return "";
			struct JsonVector
			{
				double x = 0.0;
				double y = 0.0;
				double z = 0.0;
			};
			struct SJsonCamera
			{
				JsonVector up;
				JsonVector direction;
				JsonVector position;
				bool isOrthographic = false;
				double aspectRatio = 1.0;
				double far = 10000000000;
				double near = 0.1;
				std::optional< std::array<double, 16> > ecefTransform;
			};
			struct SJsonScheduleSimulation
			{
				std::string timelineId;
				int64_t timePoint;
			};
			struct SJsonSettings
			{
				Impl::SJSonAtmosphere atmosphere;
			};
			struct SJsonFrameData
			{
				SJsonCamera camera;
				std::optional <SJsonSettings> settings;
				std::optional<SJsonScheduleSimulation> schedule;
			};
			struct SJsonInData
			{
				std::vector<double> input;
				std::vector<SJsonFrameData> output;
			};
			SJsonInData inData;

			for (size_t idx(0); idx < (*clipp)->GetKeyframeCount(); ++idx)
			{
				auto kf = (*clipp)->GetKeyframeByIndex(idx);
				if (!*kf) continue;
				auto kdata = (*kf)->GetData();
				if (!kdata.camera)
				{
					continue;
				}
				inData.input.push_back(kdata.time);
				SJsonFrameData o;
				o.camera.ecefTransform = std::array<double, 16>();
				std::memcpy(o.camera.ecefTransform->data(), kdata.camera->transform.data(), 12 * sizeof(double));
				(*o.camera.ecefTransform)[12] = 0.0;
				(*o.camera.ecefTransform)[13] = 0.0;
				(*o.camera.ecefTransform)[14] = 0.0;
				(*o.camera.ecefTransform)[15] = 1.0;
				if (kdata.atmo)
				{
					o.settings = SJsonSettings();
					o.settings->atmosphere.fog = kdata.atmo->fog;
					o.settings->atmosphere.weather = kdata.atmo->cloudCoverage;
					o.settings->atmosphere.heliodonDate = kdata.atmo->time;
				}
				if (kdata.synchro && !kdata.synchro->date.empty() && !kdata.synchro->scheduleId().empty())
				{
					std::chrono::sys_time<std::chrono::milliseconds> tp;
					std::istringstream is{ kdata.synchro->date };
					is >> std::chrono::parse("%FT%TZ", tp);
					if (is.fail())
					{
						BE_ISSUE("unable to parse string");
					}
					else
					{
						o.schedule = SJsonScheduleSimulation();
						o.schedule->timePoint = tp.time_since_epoch().count();
						o.schedule->timelineId = kdata.synchro->scheduleId();
					}

				}

				inData.output.push_back(o);

			}
			if (forPatch)
			{
				struct SJsonIn
				{
					SJsonInData data;
					std::optional<std::string> displayName;
				};
				SJsonIn Jin;
				Jin.data = inData;
				Jin.displayName = (*clipp)->GetName();
				return Json::ToString(Jin);
			}
			else
			{
				struct SJsonIn
				{
					std::string version = "1.0.0";
					std::string kind = "CameraAnimation";
					SJsonInData data;
					std::optional<std::string> displayName;
				};
				SJsonIn Jin;
				Jin.data = inData;
				Jin.displayName = (*clipp)->GetName();
				return Json::ToString(Jin);
			}
		}
		else if (link->GetType() == "decoration" || link->GetType() == "DecorationScene")
		{
			struct SJsonInData
			{
				std::string decorationId;
			};
			if (forPatch)
			{
				struct SJsonIn
				{
					SJsonInData data;
					std::optional<std::string> displayName;
				};
				SJsonIn Jin;
				Jin.data.decorationId = DSIDtoSceneAPIGuid(link->GetRef());
				Jin.displayName = link->GetType();
				return Json::ToString(Jin);
			}
			else
			{
				struct SJsonIn
				{
					std::string version = "1.0.0";
					std::string kind = "MaterialDecoration";
					std::optional<std::string> displayName;
					SJsonInData data;
				};
				SJsonIn Jin;
				Jin.data.decorationId = DSIDtoSceneAPIGuid(link->GetRef());
				Jin.displayName = link->GetType();
				return Json::ToString(Jin);
			}
		}
		else if (link->GetType() == "adjustment")
		{
			struct SJsonInDataList
			{
				std::string shownList = "";
				std::string hiddenList = "";
			};
			struct SJsonInData
			{
				std::vector<double> adjustment;
				SJsonInDataList categories; //temp until it comes optional
				SJsonInDataList models;//temp until it comes optional
				std::optional<std::string> displayName;
			};
			SJsonInData data;
			if (link->HasTransform())
			{
				auto tr = link->GetTransform();
				for (int i = 0; i < 12; ++i)
				{
					data.adjustment.push_back(tr[i]);
				}
			}
			if (link->HasQuality())
			{
				double ql = link->GetQuality();
				if (fabs(ql) < 1e-6)
				{
					ql = 1e-6;
				}
				if (link->HasVisibility() && !link->GetVisibility())
					data.adjustment.push_back(-ql);
				else
					data.adjustment.push_back(ql);
			}


			if (forPatch)
			{
				struct SJsonIn
				{
					std::optional<std::string> displayName;
					//std::string relatedId;
					SJsonInData data;
				};
				SJsonIn Jin;
				Jin.data = data;
				Jin.displayName = link->GetType();
				//Jin.relatedId = link->GetRef();
				return Json::ToString(Jin);
			}
			else
			{
				struct SJsonIn
				{
					std::string version = "1.0.0";
					std::string kind = "iModelVisibility";
					std::string relatedId;
					std::optional<std::string> displayName;
					SJsonInData data;
				};
				SJsonIn Jin;
				Jin.data = data;
				Jin.displayName = link->GetType();
				Jin.relatedId = link->GetRef();
				return Json::ToString(Jin);
			}
		}
		BE_ISSUE("Unknown Link Type", link->GetType());
		return "";
	}

	std::vector<std::shared_ptr<AdvViz::SDK::LinkAPI>> ScenePersistenceAPI::GenerateSubLinks()
	{
		std::vector<std::shared_ptr<AdvViz::SDK::LinkAPI>> res;
		for (auto link : GetImpl().links_)
		{
			if (link->GetType() == "RealityData" || link->GetType() == "iModel")
			{
				if (link->HasTransform() || link->HasQuality())
				{
					std::shared_ptr<AdvViz::SDK::LinkAPI> nulink(AdvViz::SDK::LinkAPI::New());
					if (link->HasTransform())
					{
						nulink->SetTransform(link->GetTransform());
					}
					if (link->HasQuality())
					{
						nulink->SetQuality(link->GetQuality());
					}
					if (link->HasVisibility())
					{
						nulink->SetVisibility(link->GetVisibility());
					}
					nulink->SetType("adjustment");
					nulink->SetRef(link->GetId());
					nulink->SetShouldSave(link->ShouldSave());
					nulink->Delete(link->ShouldDelete());
					nulink->GetImpl().parentLink_ = link;
					nulink->GetImpl().id_ = link->GetImpl().sublinkId_;
					res.push_back(nulink);
				}
			}
		}
		return res;
	}

	void ScenePersistenceAPI::SetTimeline(const std::shared_ptr<AdvViz::SDK::ITimeline>& timeline)
	{
		GetImpl().timeline_ = timeline;
	}

	std::shared_ptr<AdvViz::SDK::ITimeline> ScenePersistenceAPI::GetTimeline()
	{
		return GetImpl().timeline_;
	}

	std::vector<std::shared_ptr<AdvViz::SDK::LinkAPI>> ScenePersistenceAPI::GeneratePreLinks()
	{
		std::vector<std::shared_ptr<AdvViz::SDK::LinkAPI>> res;
		auto timeline = GetTimeline();
		if (!timeline)
			return res;
		for (size_t i(0); i < timeline->GetClipCount(); i++)
		{
			auto clipp = timeline->GetClipByIndex(i);
			if (!clipp)
				continue;

			std::shared_ptr<AdvViz::SDK::LinkAPI> nulink(AdvViz::SDK::LinkAPI::New());
			nulink->SetType("clip");
			nulink->SetName((*clipp)->GetName());
			nulink->GetImpl().idx_ = (int)i;
			nulink->GetImpl().id_ = static_cast<const std::string>((*clipp)->GetId());
			nulink->SetShouldSave(true);
			res.push_back(nulink);
		}
		for (auto clipp : timeline->GetObsoleteClips())
		{
			std::shared_ptr<AdvViz::SDK::LinkAPI> nulink(AdvViz::SDK::LinkAPI::New());
			nulink->SetType("clip");
			nulink->SetName(clipp->GetName());
			nulink->GetImpl().id_ = static_cast<const std::string>(clipp->GetId());
			nulink->Delete(true);
			res.push_back(nulink);
		}
		return res;

	}

	void ScenePersistenceAPI::SetDefaulttHttp(std::shared_ptr<Http> http)
	{
		creds.SetDefaultHttp(http);
	}


	AdvViz::expected<std::vector<std::shared_ptr<IScenePersistence>>, int> GetITwinScenesAPI(
		const std::string& itwinid)
	{
		std::vector<std::shared_ptr<IScenePersistence>> scenes;

		std::shared_ptr<Http> http = creds.GetHttp();
		if (!http)
			return scenes;


		struct JsonSceneWithId
		{
			std::string displayName;
			std::string iTwinId;
			std::string id;
		};
		struct SceneAPIUrl
		{
			std::string href;
		};
		struct SceneAPILinks
		{
			std::optional<SceneAPIUrl> prev;
			std::optional<SceneAPIUrl> self;
			std::optional<SceneAPIUrl> next;
		};
		struct SJsonOut
		{
			std::optional<std::vector<JsonSceneWithId>> scenes;
			std::optional<SceneAPILinks> _links;
		};

		SJsonOut jOut;
		long status = http->GetJson(jOut, "iTwins/" + itwinid + "/scenes", {});
		bool continueLoading = true;
		while (continueLoading)
		{
			if (status != 200 && status != 201)
			{
				continueLoading = false;
				return AdvViz::make_unexpected(status);
			}
			if (jOut.scenes)
			{
				for (auto& row : *jOut.scenes)
				{
					std::shared_ptr<ScenePersistenceAPI> scene(ScenePersistenceAPI::New());
					if (scene->Get(itwinid, row.id))
						scenes.push_back(scene);
				}


				jOut.scenes->clear();
			}
			if (jOut._links.has_value() && jOut._links->next.has_value() && !jOut._links->next->href.empty())
			{
				// Quick workaround for a bug in Decoration Service sometimes providing bad URLs with http
				// instead of https protocol! (issue found for instances, which was causing bug #1609088).
				std::string const nextUrl = rfl::internal::strings::replace_all(
					jOut._links->next->href,
					"http://", "https://");
				{
				}
			}
			else
			{
				continueLoading = false;
			}
		}

		//if (ret)
		//{
		//	BE_LOGI("ITwinScene", "Found " << scenes.size() << " API Scenes(s) for iTwin " << itwinid);
		//		}
		//else
		//{
		//	BE_LOGW("ITwinScene", "Loading scenes failed for iTwin " << itwinid << ". " << ret.error());
		//}

		return scenes;
	}


	LinkAPI::Impl& LinkAPI::GetImpl() const
	{
		return *impl_;
	}

	const std::string& LinkAPI::GetType() const
	{
		return GetImpl().link_.type;
	}

	const std::string& LinkAPI::GetRef() const
	{
		return GetImpl().link_.ref;

	}

	std::string LinkAPI::GetName() const
	{
		if (GetImpl().link_.name.has_value())
			return GetImpl().link_.name.value();
		return "";

	}

	std::pair<std::string, std::array<float, 3>> LinkAPI::GetGCS() const
	{
		std::pair<std::string, std::array<float, 3>> res("", { 0.f, 0.f, 0.f });
		if (HasGCS())
		{
			res = std::make_pair(GetImpl().link_.gcs->wkt, GetImpl().link_.gcs->center);
		}
		return res;
	}


	bool LinkAPI::GetVisibility() const
	{
		if (GetImpl().link_.visibility.has_value())
			return GetImpl().link_.visibility.value();
		return true;
	}

	double LinkAPI::GetQuality() const
	{
		if (GetImpl().link_.quality.has_value())
			return GetImpl().link_.quality.value();
		return 1.0;
	}

	dmat3x4 LinkAPI::GetTransform() const
	{
		if (GetImpl().link_.transform.has_value())
		{
			dmat3x4 mat;
			std::memcpy(&mat[0], GetImpl().link_.transform->data(), 12 * sizeof(double));
			return mat;

		}
		return Identity34;
	}

	void LinkAPI::SetType(const std::string& value)
	{
		GetImpl().shoudSave_ = GetImpl().shoudSave_ || GetImpl().link_.type != value;
		GetImpl().link_.type = value;
	}

	void LinkAPI::SetRef(const std::string& value)
	{
		GetImpl().shoudSave_ = GetImpl().shoudSave_ || GetImpl().link_.ref != value;
		GetImpl().link_.ref = value;
	}

	void LinkAPI::SetName(const std::string& value)
	{
		GetImpl().shoudSave_ = GetImpl().shoudSave_ || !GetImpl().link_.name.has_value() || GetImpl().link_.name != value;
		GetImpl().link_.name = value;
	}

	void LinkAPI::SetGCS(const std::string& v1, const std::array<float, 3>& v2)
	{
		Impl::SJSonGCS value{ .wkt = v1, .center = v2 };

		GetImpl().shoudSave_ = GetImpl().shoudSave_ || !GetImpl().link_.gcs.has_value() || value.wkt != GetImpl().link_.gcs->wkt || value.center != GetImpl().link_.gcs->center;
		GetImpl().link_.gcs = value;

	}

	void LinkAPI::SetVisibility(bool v)
	{
		GetImpl().shoudSave_ = GetImpl().shoudSave_ || !GetImpl().link_.visibility.has_value() || GetImpl().link_.visibility != v;
		GetImpl().link_.visibility = v;
	}

	void LinkAPI::SetQuality(double v)
	{
		GetImpl().shoudSave_ = GetImpl().shoudSave_ || !GetImpl().link_.quality.has_value() || GetImpl().link_.quality != v;
		GetImpl().link_.quality = v;
	}

	void LinkAPI::SetTransform(const dmat4x3& v)
	{
		GetImpl().shoudSave_ = GetImpl().shoudSave_ || !GetImpl().link_.transform.has_value() || v != *GetImpl().link_.transform;
		GetImpl().link_.transform = v;
	}

	bool LinkAPI::HasName() const
	{
		return GetImpl().link_.name.has_value();
	}

	bool LinkAPI::HasGCS() const
	{
		return GetImpl().link_.gcs.has_value();
	}

	bool LinkAPI::HasVisibility() const
	{
		return GetImpl().link_.visibility.has_value();
	}

	bool LinkAPI::HasQuality() const
	{
		return GetImpl().link_.quality.has_value();
	}

	bool LinkAPI::HasTransform() const
	{
		return GetImpl().link_.transform.has_value();

	}


	LinkAPI::LinkAPI() : impl_(new Impl)
	{

	}

	LinkAPI::~LinkAPI() {}

	bool LinkAPI::ShouldSave() const
	{
		return GetImpl().shoudSave_;
	}

	void LinkAPI::SetShouldSave(bool shouldSave)
	{
		GetImpl().shoudSave_ = shouldSave;
	}

	void LinkAPI::Delete(bool value)
	{
		GetImpl().shouldDelete_ = value;
		if (value)
			GetImpl().shoudSave_ = true;
	}

	bool LinkAPI::ShouldDelete()
	{
		return GetImpl().shouldDelete_;
	}

	const std::string& LinkAPI::GetId()
	{
		return GetImpl().id_;
	}

	void SetSceneAPIConfig(const Config::SConfig& c)
	{
		creds.curstomServerConfig = c;
	}

}
