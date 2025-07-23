/*--------------------------------------------------------------------------------------+
|
|     $Source: ScenePersistenceDS.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ScenePersistenceDS.h"
#include "Config.h"
#include "../Singleton/singleton.h"
#include "Core/Network/HttpGetWithLink.h"

namespace AdvViz::SDK {
	namespace 
	{
		static dmat4x3 Identity34 = { 1.0,0,0,0,1.0,0,0,0,1.0,0,0,0 };
	}
	struct LinkDS::Impl
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

	};
	class ScenePersistenceDS::Impl
	{
	public:
		struct SJsonInEmpty {};

		struct SJSonAtmosphere
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
			SJSonEnvironment environment;
		};
	public:
		std::string id_;
		std::shared_ptr<Http> http_;
		SJsonScene jsonScene_;
		bool shoudSave_ = false;
		std::vector<std::shared_ptr<AdvViz::SDK::LinkDS>> links_;
		std::shared_ptr<AdvViz::SDK::ITimeline> timeline_;

		std::shared_ptr<Http>& GetHttp() { return http_; }
		void SetHttp(const std::shared_ptr<Http>& http) { http_ = http; }

		bool Create(
			const std::string& name, const std::string& itwinid, bool keepCurrentValues = false)
		{
			struct SJsonIn { std::string name; std::string itwinid; };
			SJsonIn jIn{ name, itwinid };
			struct SJsonOut { std::string id; SJsonScene data; };
			SJsonOut jOut;

			long status = GetHttp()->PostJsonJBody(jOut, std::string("scenes"), jIn);
			if (status == 200 || status == 201)
			{
				if(!keepCurrentValues)
					jsonScene_ = std::move(jOut.data);
				id_ = jOut.id;
				BE_LOGI("ITwinScene", "Created Scene in DS for itwin " << itwinid
					<< " (ID: " << id_ << ")");
				return true;
			}
			else
			{
				BE_LOGW("ITwinScene", "Could not create Scene in DS for itwin " << itwinid
					<< ". Http status: " << status);
				return false;
			}
		}
		bool Save()
		{
			struct Sout
			{
			};
			Sout jout;
			long status = GetHttp()->PutJsonJBody(jout, "scenes/" + id_, jsonScene_);
			if (status == 200)
			{
				BE_LOGI("ITwinScene", "Save Scene in DS with ID " << id_ << " itwin " << jsonScene_.itwinid);
				return true;
			}
			else
			{
				BE_LOGW("ITwinScene", "Save Scene in DS failedwith ID " << id_ << " itwin " << jsonScene_.itwinid << " Http status : " << status);
				return false;
			}
		}
		bool Get(const std::string& id)
		{
			long status = GetHttp()->GetJson(jsonScene_, "scenes/" + id);
			if (status == 200)
			{
				id_ = id;
				BE_LOGI("ITwinScene", "Loaded Scene in DS with ID " << id_);
				return true;
			}
			else
			{
				BE_LOGW("ITwinScene", "Load Scene in DS failed. Http status: " << status);
				return false;
			}
		}

		bool Delete()
		{
			std::string s("scenes/" + id_);
			auto status = GetHttp()->Delete(s, "");
			if (status.first != 200)
			{
				BE_LOGW("ITwinScene", "Delete Scene in DS failed. Http status: " << status.first);
				return false;
			}
			else
			{
				BE_LOGI("ITwinScene", "Deleted Scene in DS with ID " << id_);
				id_ = "";
				jsonScene_ = SJsonScene();
				return true;
			}
		}
	};

	template<>
	Tools::Factory<ScenePersistenceDS>::Globals::Globals()
	{
		newFct_ = []() {return static_cast<ScenePersistenceDS*>(new ScenePersistenceDS());};
	}


	template<>
	Tools::Factory<ScenePersistenceDS>::Globals& Tools::Factory<ScenePersistenceDS>::GetGlobals()
	{
		return singleton<Tools::Factory<ScenePersistenceDS>::Globals>();
	}

	template<>
	Tools::Factory<LinkDS>::Globals::Globals()
	{
		newFct_ = []() {return new LinkDS();};
	}


	template<>
	Tools::Factory<LinkDS>::Globals& Tools::Factory<LinkDS>::GetGlobals()
	{
		return singleton<Tools::Factory<LinkDS>::Globals>();
	}

	void ScenePersistenceDS::SetHttp(std::shared_ptr<Http> http)
	{
		GetImpl().SetHttp(http);
	}

	bool ScenePersistenceDS::Create(const std::string& name, const std::string& itwinid)
	{
		return GetImpl().Create(name, itwinid, false);
	}

	bool ScenePersistenceDS::Get(const std::string& /*itwinid*/, const std::string& id)
	{
		bool res = GetImpl().Get(id);
		LoadLinks();
		return res;
	}

	bool ScenePersistenceDS::Delete()
	{
		return GetImpl().Delete();
	}

	const std::string& ScenePersistenceDS::GetId() const
	{
		return GetImpl().id_;
	}

	const std::string& ScenePersistenceDS::GetName() const
	{
		return GetImpl().jsonScene_.name;

	}

	const std::string& ScenePersistenceDS::GetITwinId() const
	{
		return GetImpl().jsonScene_.itwinid;
	}

	ScenePersistenceDS::~ScenePersistenceDS()
	{
	}

	ScenePersistenceDS::ScenePersistenceDS():impl_(new Impl)
	{
		GetImpl().SetHttp(GetDefaultHttp());
	}

	ScenePersistenceDS::Impl& ScenePersistenceDS::GetImpl() const {
		return *impl_;
	}

	void ScenePersistenceDS::SetAtmosphere(const ITwinAtmosphereSettings& atmo)
	{
		auto& jsonatmo = GetImpl().jsonScene_.environment.atmosphere;
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

	AdvViz::SDK::ITwinAtmosphereSettings ScenePersistenceDS::GetAtmosphere() const
	{
		AdvViz::SDK::ITwinAtmosphereSettings atmo;
		const auto& jsonatmo = GetImpl().jsonScene_.environment.atmosphere;
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

	void ScenePersistenceDS::SetSceneSettings(const ITwinSceneSettings& ss)
	{
		auto& jsonss = GetImpl().jsonScene_.environment.sceneSettings;
		jsonss.displayGoogleTiles = ss.displayGoogleTiles;
		jsonss.qualityGoogleTiles = ss.qualityGoogleTiles;
		jsonss.geoLocation = ss.geoLocation;
		GetImpl().shoudSave_ = true;
	}

	AdvViz::SDK::ITwinSceneSettings ScenePersistenceDS::GetSceneSettings() const 
	{
		const auto& jsonss = GetImpl().jsonScene_.environment.sceneSettings;
		AdvViz::SDK::ITwinSceneSettings ss;
		ss.displayGoogleTiles = jsonss.displayGoogleTiles;
		ss.qualityGoogleTiles = jsonss.qualityGoogleTiles;
		ss.geoLocation = jsonss.geoLocation;
		return ss;
	}

	bool ScenePersistenceDS::ShouldSave() const
	{
		if (GetImpl().shoudSave_) return true;
		for (auto link : GetImpl().links_)
		{
			if (link->GetImpl().shoudSave_)
				return true;
		}
		return false;
	}

	bool ScenePersistenceDS::Save()
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

	void ScenePersistenceDS::SetShouldSave(bool shouldSave) const
	{
		GetImpl().shoudSave_ = shouldSave;
	}

	void ScenePersistenceDS::PrepareCreation(const std::string& name, const std::string& itwinid)
	{
		GetImpl().jsonScene_.name = name;
		GetImpl().jsonScene_.itwinid = itwinid;
	}

	std::vector<std::shared_ptr<AdvViz::SDK::ILink>> ScenePersistenceDS::GetLinks() const
	{
		std::vector<std::shared_ptr<AdvViz::SDK::ILink>> res;
		for (auto l : GetImpl().links_)
			res.push_back(l);
		return res;

	}

	void ScenePersistenceDS::LoadLinks()
	{
		std::shared_ptr<Http>& http = GetDefaultHttp();
		if (!http)
			return;


		int nbLinks = 0;
		auto ret = HttpGetWithLink<LinkDS::Impl::LinkWithId>(http,
			"scenes/" + GetId() + "/links",
			{} /* extra headers*/,

			[this, &nbLinks](LinkDS::Impl::LinkWithId const& row) -> expected<void, std::string>
		{
					std::shared_ptr<AdvViz::SDK::LinkDS> link(AdvViz::SDK::LinkDS::New());
					link->GetImpl().FromLinkWithId(row);
					GetImpl().links_.push_back(link);
			nbLinks++;

			return {};
		});

		if (ret)
		{
			BE_LOGI("ITwinScene", "Found " << nbLinks << " Link(s) for scene " << GetId());
				}
		else
		{
			BE_LOGW("ITwinScene", "Load scene links failed. " << ret.error());
			}
		}

	void ScenePersistenceDS::AddLink(std::shared_ptr<ILink> v)
	{
		auto rv = std::dynamic_pointer_cast<LinkDS>(v);
		GetImpl().links_.push_back(rv);
		GetImpl().shoudSave_ = true;
	}

	void ScenePersistenceDS::SaveLinks()
	{
		for (auto link : GetImpl().links_)
		{
			if (link->GetImpl().id_.empty() && !link->GetImpl().shouldDelete_)
			{
				std::shared_ptr<Http>& http = GetDefaultHttp();

				struct SJsonOut
				{
					std::vector<LinkDS::Impl::LinkWithId> links;
				};
				SJsonOut jOut;
				struct SJsonIn
				{
					std::vector<LinkDS::Impl::Link> links;
				};
				SJsonIn Jin;
				Jin.links.push_back(link->GetImpl().link_);

				long status = http->PostJsonJBody(jOut, "scenes/" + GetId() + "/links", Jin);
				if (status == 200 || status == 201)
				{
					if (jOut.links.size() == 1)
					{
						link->GetImpl().id_ = jOut.links[0].id;
						BE_LOGI("ITwinScene", "Add Link for scene " << GetId()\
							<< " new link Id " << link->GetImpl().id_ << " type : " << link->GetType() << " ref : " << link->GetRef());
						link->GetImpl().shoudSave_ = false;
					}
					else
					{
						BE_LOGW("ITwinScene", "Add Link  for scene " << GetId() << " failed, Unable to get ID .Http status : " << status);

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
				std::shared_ptr<Http>& http = GetDefaultHttp();

				struct SJsonOutEmpty {};

				struct SJsonIn
				{
					std::vector<std::string> ids;
				};
				SJsonOutEmpty jOut;
				SJsonIn Jin;
				Jin.ids.push_back(link->GetImpl().id_);

				std::string url("scenes/" + GetId() + "/links");
				auto status = http->DeleteJsonJBody(jOut, url,Jin);
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
			else if(!link->GetImpl().id_.empty() && !link->GetImpl().shouldDelete_)
			{
				std::shared_ptr<Http>& http = GetDefaultHttp();

				struct SJsonOut
				{
					int numUpdated;
				};
				SJsonOut jOut;
				struct SJsonIn
				{
					std::vector<LinkDS::Impl::LinkWithId> links;
				};
				SJsonIn Jin;
				Jin.links.push_back(link->GetImpl().ToLinkWithId());
				long status = http->PutJsonJBody(jOut, "scenes/" + GetId() + "/links", Jin);
				if (status == 200 && jOut.numUpdated == 1)
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

		std::erase_if(GetImpl().links_, [](const std::shared_ptr<LinkDS>& l)
			{
				return l->GetImpl().shouldDelete_ && l->GetImpl().id_.empty(); 
			});
	}

	std::shared_ptr<AdvViz::SDK::ILink> ScenePersistenceDS::MakeLink()
	{
		return 	std::shared_ptr<AdvViz::SDK::ILink>(LinkDS::New());

	}

	void ScenePersistenceDS::SetTimeline(const std::shared_ptr<AdvViz::SDK::ITimeline>& timeline)
	{
		GetImpl().timeline_ = timeline;
	}

	std::shared_ptr<AdvViz::SDK::ITimeline> ScenePersistenceDS::GetTimeline()
	{
		return GetImpl().timeline_;
	}

	std::vector<std::shared_ptr<IScenePersistence>> GetITwinScenesDS(
		const std::string& itwinid)
	{
		std::vector<std::shared_ptr<IScenePersistence>> scenes;

		std::shared_ptr<Http>& http = GetDefaultHttp();
		if (!http)
			return scenes;


		struct JsonSceneWithId
		{
			std::string name;
			std::string itwinid;
			std::string id;
		};

		auto ret = HttpGetWithLink<JsonSceneWithId>(http,
			"scenes?iTwinId=" + itwinid,
			{} /* extra headers*/,
			[itwinid, &scenes](JsonSceneWithId const& row) -> expected<void, std::string>
		{
					std::shared_ptr<ScenePersistenceDS> scene(ScenePersistenceDS::New());
					if (scene->Get(itwinid, row.id))
						scenes.push_back(scene);
			return {};
		});

		if (ret)
		{
			BE_LOGI("ITwinScene", "Found " << scenes.size() << " Scenes(s) for iTwin " << itwinid);
				}
		else
		{
			BE_LOGW("ITwinScene", "Loading scenes failed for iTwin " << itwinid << ". " << ret.error());
		}

		return scenes;
	}


	LinkDS::Impl& LinkDS::GetImpl() const
	{
		return *impl_;
	}

	const std::string& LinkDS::GetType() const
	{
		return GetImpl().link_.type;
	}

	const std::string& LinkDS::GetRef() const
	{
		return GetImpl().link_.ref;

	}

	std::string LinkDS::GetName() const
	{
		if(GetImpl().link_.name.has_value())
			return GetImpl().link_.name.value();
		return "";

	}

	std::pair<std::string, std::array<float, 3>> LinkDS::GetGCS() const
	{
		std::pair<std::string, std::array<float, 3>> res("", { 0.f, 0.f, 0.f });
		if (HasGCS())
		{
			res = std::make_pair(GetImpl().link_.gcs->wkt, GetImpl().link_.gcs->center);
		}
		return res;
	}


	bool LinkDS::GetVisibility() const
	{
		if (GetImpl().link_.visibility.has_value())
			return GetImpl().link_.visibility.value();
		return true;
	}

	double LinkDS::GetQuality() const
	{
		if (GetImpl().link_.quality.has_value())
			return GetImpl().link_.quality.value();
		return 1.0;
	}

	dmat3x4 LinkDS::GetTransform() const
	{
		if (GetImpl().link_.transform.has_value())
		{
			dmat3x4 mat;
			std::memcpy(&mat[0], GetImpl().link_.transform->data(), 12 * sizeof(double));
			return mat;

		}
		return Identity34;
	}

	void LinkDS::SetType(const std::string& value)
	{
		GetImpl().shoudSave_ = GetImpl().shoudSave_ || GetImpl().link_.type != value;
		GetImpl().link_.type = value;
	}

	void LinkDS::SetRef(const std::string& value)
	{
		GetImpl().shoudSave_ = GetImpl().shoudSave_ || GetImpl().link_.ref != value;
		GetImpl().link_.ref = value;
	}

	void LinkDS::SetName(const std::string& value)
	{
		GetImpl().shoudSave_ = GetImpl().shoudSave_ || !GetImpl().link_.name.has_value() || GetImpl().link_.name != value;
		GetImpl().link_.name = value;
	}

	void LinkDS::SetGCS(const std::string& v1, const std::array<float, 3>& v2)
	{
		Impl::SJSonGCS value{ .wkt = v1, .center = v2 };

		GetImpl().shoudSave_ = GetImpl().shoudSave_ || !GetImpl().link_.gcs.has_value() || value.wkt != GetImpl().link_.gcs->wkt || value.center != GetImpl().link_.gcs->center;
		GetImpl().link_.gcs = value;

	}

	void LinkDS::SetVisibility(bool v)
	{
		GetImpl().shoudSave_ = GetImpl().shoudSave_ || !GetImpl().link_.visibility.has_value() || GetImpl().link_.visibility != v;
		GetImpl().link_.visibility = v;
	}

	void LinkDS::SetQuality(double v)
	{
		GetImpl().shoudSave_ = GetImpl().shoudSave_ || !GetImpl().link_.quality.has_value() || GetImpl().link_.quality != v;
		GetImpl().link_.quality = v;
	}

	void LinkDS::SetTransform(const dmat4x3& v)
	{
		GetImpl().shoudSave_ = GetImpl().shoudSave_ || !GetImpl().link_.transform.has_value() || v != *GetImpl().link_.transform;
		GetImpl().link_.transform = v;
	}

	bool LinkDS::HasName() const
	{
		return GetImpl().link_.name.has_value();
	}

	bool LinkDS::HasGCS() const
	{
		return GetImpl().link_.gcs.has_value();
	}

	bool LinkDS::HasVisibility() const
	{
		return GetImpl().link_.visibility.has_value();
	}

	bool LinkDS::HasQuality() const
	{
		return GetImpl().link_.quality.has_value();
	}

	bool LinkDS::HasTransform() const
	{
		return GetImpl().link_.transform.has_value();

	}


	LinkDS::LinkDS(): impl_(new Impl)
	{

	}

	LinkDS::~LinkDS(){}

	bool LinkDS::ShouldSave() const
	{
		return GetImpl().shoudSave_;
	}

	void LinkDS::SetShouldSave(bool shouldSave)
	{
		GetImpl().shoudSave_ = shouldSave;
	}

	void LinkDS::Delete(bool value)
	{
		GetImpl().shouldDelete_ = value;
		GetImpl().shoudSave_ = true;
	}

	bool LinkDS::ShouldDelete()
	{
		return GetImpl().shouldDelete_;
	}

	const std::string & LinkDS::GetId()
	{
		return GetImpl().id_;
	}

}
