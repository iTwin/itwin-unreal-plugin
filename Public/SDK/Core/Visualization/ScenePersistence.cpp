/*--------------------------------------------------------------------------------------+
|
|     $Source: ScenePersistence.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ScenePersistence.h"
#include "Config.h"
#include "../Singleton/singleton.h"

namespace SDK::Core {

	static dmat4x3 Identity34 = { 1.0,0,0,0,1.0,0,0,0,1.0,0,0,0 };
	struct SJsonInEmpty {};

	struct SJsonLink
	{
		std::optional<std::string> prev;
		std::optional<std::string> self;
		std::optional<std::string> next;
	};
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
	struct Link::Impl
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
	class ScenePersistence::Impl
	{
	public:

		std::string id_;
		std::shared_ptr<Http> http_;
		SJsonScene jsonScene_;
		bool shoudSave_ = false;
		std::vector<std::shared_ptr<SDK::Core::Link>> links_;

		std::shared_ptr<Http>& GetHttp() { return http_; }
		void SetHttp(const std::shared_ptr<Http>& http) { http_ = http; }

		void Create(
			const std::string& name, const std::string& itwinid, const std::string& accessToken, bool keepCurrentValues = false)
		{
			struct SJsonIn { std::string name; std::string itwinid; };
			SJsonIn jIn{ name, itwinid };
			struct SJsonOut { std::string id; SJsonScene data; };
			SJsonOut jOut;

			Http::Headers headers;
			headers.emplace_back("Authorization", std::string("Bearer ") + accessToken);

			long status = GetHttp()->PostJsonJBody(jOut, std::string("scenes"), jIn, headers);
			if (status == 200 || status == 201)
			{
				if(!keepCurrentValues)
					jsonScene_ = std::move(jOut.data);
				id_ = jOut.id;
				BE_LOGI("ITwinScene", "Created Scene for itwin " << itwinid
					<< " (ID: " << id_ << ")");
			}
			else
			{
				BE_LOGW("ITwinScene", "Could not create Scene for itwin " << itwinid
					<< ". Http status: " << status);
			}
		}
		void Save(const std::string& accessToken)
		{
			Http::Headers headers;
			headers.emplace_back("Authorization", std::string("Bearer ") + accessToken);

			SJsonScene jOut;

			long status = GetHttp()->PutJsonJBody(jOut, "scenes/" + id_, jsonScene_, headers);
			if (status == 200)
			{
				BE_LOGI("ITwinScene", "Save Scene with ID " << id_);
			}
			else
			{
				BE_LOGW("ITwinScene", "Save Scene failed. Http status: " << status);
			}
		}
		bool Get(const std::string& id, const std::string& accessToken)
		{
			Http::Headers headers;
			headers.emplace_back("Authorization", std::string("Bearer ") + accessToken);

			long status = GetHttp()->GetJson(jsonScene_, "scenes/" + id, "", headers);
			if (status == 200)
			{
				id_ = id;
				BE_LOGI("ITwinScene", "Loaded Scene with ID " << id_);
				return true;
			}
			else
			{
				BE_LOGW("ITwinScene", "Load Scene failed. Http status: " << status);
				return false;
			}
		}

		void Delete(const std::string& accessToken)
		{
			Http::Headers headers;
			headers.emplace_back("Authorization", std::string("Bearer ") + accessToken);
			std::string s("scenes/" + id_);
			auto status = GetHttp()->Delete(s, "", headers);
			if (status.first != 200)
			{
				BE_LOGW("ITwinScene", "Delete Scene failed. Http status: " << status.first);
			}
			else
			{
				BE_LOGI("ITwinScene", "Deleted Scene with ID " << id_);
			}
			id_ = "";
			jsonScene_ = SJsonScene();
		}
	};

	template<>
	Tools::Factory<IScenePersistence>::Globals::Globals()
	{
		newFct_ = []() {return static_cast<IScenePersistence*>(new ScenePersistence());};
	}


	template<>
	Tools::Factory<IScenePersistence>::Globals& Tools::Factory<IScenePersistence>::GetGlobals()
	{
		return singleton<Tools::Factory<IScenePersistence>::Globals>();
	}

	template<>
	Tools::Factory<Link>::Globals::Globals()
	{
		newFct_ = []() {return new Link();};
	}


	template<>
	Tools::Factory<Link>::Globals& Tools::Factory<Link>::GetGlobals()
	{
		return singleton<Tools::Factory<Link>::Globals>();
	}

	void ScenePersistence::SetHttp(std::shared_ptr<Http> http)
	{
		GetImpl().SetHttp(http);
	}

	void ScenePersistence::Create(const std::string& name, const std::string& itwinid, const std::string& accessToken)
	{
		GetImpl().Create(name, itwinid, accessToken, false);
	}

	bool ScenePersistence::Get(const std::string& id, const std::string& accessToken)
	{
		bool res = GetImpl().Get(id, accessToken);
		LoadLinks(accessToken);
		return res;
	}

	void ScenePersistence::Delete( const std::string& accessToken)
	{
		GetImpl().Delete(accessToken);
	}

	const std::string& ScenePersistence::GetId() const
	{
		return GetImpl().id_;
	}

	const std::string& ScenePersistence::GetName() const
	{
		return GetImpl().jsonScene_.name;

	}

	const std::string& ScenePersistence::GetITwinId() const
	{
		return GetImpl().jsonScene_.itwinid;
	}

	ScenePersistence::~ScenePersistence()
	{
	}

	ScenePersistence::ScenePersistence():impl_(new Impl)
	{
		GetImpl().SetHttp(GetDefaultHttp());
	}

	ScenePersistence::Impl& ScenePersistence::GetImpl() const {
		return *impl_;
	}

	void ScenePersistence::SetAtmosphere(const ITwinAtmosphereSettings& atmo)
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

	SDK::Core::ITwinAtmosphereSettings ScenePersistence::GetAtmosphere() const
	{
		SDK::Core::ITwinAtmosphereSettings atmo;
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

	void ScenePersistence::SetSceneSettings(const ITwinSceneSettings& ss)
	{
		auto& jsonss = GetImpl().jsonScene_.environment.sceneSettings;
		jsonss.displayGoogleTiles = ss.displayGoogleTiles;
		jsonss.qualityGoogleTiles = ss.qualityGoogleTiles;
		jsonss.geoLocation = ss.geoLocation;
		GetImpl().shoudSave_ = true;
	}

	SDK::Core::ITwinSceneSettings ScenePersistence::GetSceneSettings() const 
	{
		const auto& jsonss = GetImpl().jsonScene_.environment.sceneSettings;
		SDK::Core::ITwinSceneSettings ss;
		ss.displayGoogleTiles = jsonss.displayGoogleTiles;
		ss.qualityGoogleTiles = jsonss.qualityGoogleTiles;
		ss.geoLocation = jsonss.geoLocation;
		return ss;
	}

	bool ScenePersistence::ShouldSave() const
	{
		if (GetImpl().shoudSave_) return true;
		for (auto link : GetImpl().links_)
		{
			if (link->GetImpl().shoudSave_)
				return true;
		}
		return false;
	}

	void ScenePersistence::Save(const std::string& accessToken)
	{
		if (GetImpl().id_.empty() && !GetImpl().jsonScene_.name.empty() && !GetImpl().jsonScene_.itwinid.empty())
		{
			GetImpl().Create(GetImpl().jsonScene_.name, GetImpl().jsonScene_.itwinid, accessToken, true);
		}
		if (ShouldSave())
		{
			GetImpl().Save(accessToken);
			SaveLinks(accessToken);
			GetImpl().shoudSave_ = false;

		}
	}

	void ScenePersistence::SetShoudlSave(bool shouldSave) const
	{
		GetImpl().shoudSave_ = shouldSave;
	}

	void ScenePersistence::PrepareCreation(const std::string& name, const std::string& itwinid)
	{
		GetImpl().jsonScene_.name = name;
		GetImpl().jsonScene_.itwinid = itwinid;
	}

	std::vector<std::shared_ptr<SDK::Core::Link>> ScenePersistence::GetLinks() const
	{
		return GetImpl().links_;

	}

	void ScenePersistence::LoadLinks(const std::string& accessToken)
	{
		std::vector<std::shared_ptr<SDK::Core::IScenePersistence>> links;
		std::shared_ptr<Http>& http = GetDefaultHttp();
		if (!http)
			return;

		Http::Headers headers;
		headers.emplace_back("Authorization", std::string("Bearer ") + accessToken);
		struct SJsonInEmpty {};
		SJsonInEmpty jIn;

		struct SJsonOut { int total_rows = 0; std::vector<Link::Impl::LinkWithId> rows; SJsonLink _links; };
		SJsonOut jOut;

		long status = http->GetJsonJBody(jOut, "scenes/" + GetId() + "/links", jIn, headers);
		if (status == 200 || status == 201)
		{
			BE_LOGI("ITwinScene", "Found " << jOut.rows.size() << " Link for scene" << GetId());
			for (auto& row : jOut.rows)
			{
				std::shared_ptr<SDK::Core::Link> link(SDK::Core::Link::New());

				link->GetImpl().FromLinkWithId(row);
				GetImpl().links_.push_back(link);
			}
		}
	}

	void ScenePersistence::AddLink(std::shared_ptr<Link> v)
	{
		GetImpl().links_.push_back(v);
		GetImpl().shoudSave_ = true;
	}

	void ScenePersistence::SetLinks(const std::vector<std::shared_ptr<Link>>& value)
	{
		GetImpl().links_ = value;
		GetImpl().shoudSave_ = true;

	}

	void ScenePersistence::SaveLinks(const std::string& accessToken)
	{
		for (auto link : GetLinks())
		{
			if (link->GetImpl().id_.empty())
			{
				std::shared_ptr<Http>& http = GetDefaultHttp();

				Http::Headers headers;
				headers.emplace_back("Authorization", std::string("Bearer ") + accessToken);

				struct SJsonOut
				{
					std::vector<Link::Impl::LinkWithId> links;
				};
				SJsonOut jOut;
				struct SJsonIn
				{
					std::vector<Link::Impl::Link> links;
				};
				SJsonIn Jin;
				Jin.links.push_back(link->GetImpl().link_);

				long status = http->PostJsonJBody(jOut, "scenes/" + GetId() + "/links", Jin, headers);
				if (status == 200 || status == 201)
				{
					if (jOut.links.size() == 1)
					{
						link->GetImpl().id_ = jOut.links[0].id;
						BE_LOGI("ITwinScene", "Add Link for scene " << GetId() << " new link Id " << link->GetImpl().id_);
						link->GetImpl().shoudSave_ = false;
					}
					else
					{
						BE_LOGW("ITwinScene", "Add Link  for scene " << GetId() << " failed, Unable to get ID .Http status : " << status);

					}

				}
				else
				{
					BE_LOGW("ITwinScene", "Add Link  for scene " << GetId() << " failed.Http status : " << status);
				}
			}
			else
			{
				std::shared_ptr<Http>& http = GetDefaultHttp();

				Http::Headers headers;
				headers.emplace_back("Authorization", std::string("Bearer ") + accessToken);
				struct SJsonOut
				{
					int numUpdated;
				};
				SJsonOut jOut;
				struct SJsonIn
				{
					std::vector<Link::Impl::LinkWithId> links;
				};
				SJsonIn Jin;
				Jin.links.push_back(link->GetImpl().ToLinkWithId());
				long status = http->PutJsonJBody(jOut, "scenes/" + GetId() + "/links", Jin, headers);
				if (status == 200 && jOut.numUpdated == 1)
				{
					BE_LOGI("ITwinScene", "Update Link for scene " << GetId() << " new link Id " << link->GetImpl().id_);
					link->GetImpl().shoudSave_ = false;
				}
				else
				{
					BE_LOGW("ITwinScene", "Update Link  for scene " << GetId() << " failed.Http status : " << status << " #updated "<< jOut.numUpdated);
				}

			}
		}
	}

	std::vector<std::shared_ptr<IScenePersistence>> GetITwinScenes(
		const std::string& itwinid, const std::string& accessToken)
	{
		std::vector<std::shared_ptr<IScenePersistence>> scenes;

		std::shared_ptr<Http>& http = GetDefaultHttp();
		if (!http)
			return scenes;

		Http::Headers headers;
		headers.emplace_back("Authorization", std::string("Bearer ") + accessToken);

		struct JsonSceneWithId
		{
			std::string name;
			std::string itwinid;
			SJSonEnvironment environment;
			std::string id;
		};

		struct SJsonOut
		{ 
			int total_rows;
			std::vector<JsonSceneWithId> rows;
			SJsonLink _links;
		};

		SJsonInEmpty jIn;
		SJsonOut jOut;
		long status = http->GetJsonJBody(jOut, "scenes?iTwinId=" + itwinid, jIn, headers);

		if (status == 200 || status == 201)
		{
			BE_LOGI("ITwinScene", "Found " << jOut.rows.size() << " Scenes(s) for iTwin " << itwinid);
			for (auto& row : jOut.rows)
			{
				std::shared_ptr<SDK::Core::IScenePersistence> scene(SDK::Core::IScenePersistence::New());
				if(scene->Get(row.id, accessToken))
					scenes.push_back(scene);
			}
		}
		else
		{
			BE_LOGW("ITwinScene", "Load Scenes failed. Http status: " << status);
		}

		return scenes;
	}


	Link::Impl& Link::GetImpl() const
	{
		return *impl_;
	}

	const std::string& Link::GetType() const
	{
		return GetImpl().link_.type;
	}

	const std::string& Link::GetRef() const
	{
		return GetImpl().link_.ref;

	}

	std::string Link::GetName() const
	{
		if(GetImpl().link_.name.has_value())
			return GetImpl().link_.name.value();
		return "";

	}

	std::pair<std::string, std::array<float, 3>> Link::GetGCS() const
	{
		std::pair<std::string, std::array<float, 3>> res("", { 0.f, 0.f, 0.f });
		if (HasGCS())
		{
			res = std::make_pair(GetImpl().link_.gcs->wkt, GetImpl().link_.gcs->center);
		}
		return res;
	}


	bool Link::GetVisibility() const
	{
		if (GetImpl().link_.visibility.has_value())
			return GetImpl().link_.visibility.value();
		return true;
	}

	double Link::GetQuality() const
	{
		if (GetImpl().link_.quality.has_value())
			return GetImpl().link_.quality.value();
		return 1.0;
	}

	dmat3x4 Link::GetTransform() const
	{
		if (GetImpl().link_.transform.has_value())
		{
			dmat3x4 mat;
			std::memcpy(&mat[0], GetImpl().link_.transform->data(), 12 * sizeof(double));
			return mat;

		}
		return Identity34;
	}

	void Link::SetType(const std::string& value)
	{
		GetImpl().shoudSave_ = GetImpl().shoudSave_ || GetImpl().link_.type != value;
		GetImpl().link_.type = value;
	}

	void Link::SetRef(const std::string& value)
	{
		GetImpl().shoudSave_ = GetImpl().shoudSave_ || GetImpl().link_.ref != value;
		GetImpl().link_.ref = value;
	}

	void Link::SetName(const std::string& value)
	{
		GetImpl().shoudSave_ = GetImpl().shoudSave_ || !GetImpl().link_.name.has_value() || GetImpl().link_.name != value;
		GetImpl().link_.name = value;
	}

	void Link::SetGCS(const std::string& v1, const std::array<float, 3>& v2)
	{
		Impl::SJSonGCS value{ .wkt = v1, .center = v2 };

		GetImpl().shoudSave_ = GetImpl().shoudSave_ || !GetImpl().link_.gcs.has_value() || value.wkt != GetImpl().link_.gcs->wkt || value.center != GetImpl().link_.gcs->center;
		GetImpl().link_.gcs = value;

	}

	void Link::SetVisibility(bool v)
	{
		GetImpl().shoudSave_ = GetImpl().shoudSave_ || !GetImpl().link_.visibility.has_value() || GetImpl().link_.visibility != v;
		GetImpl().link_.visibility = v;
	}

	void Link::SetQuality(double v)
	{
		GetImpl().shoudSave_ = GetImpl().shoudSave_ || !GetImpl().link_.quality.has_value() || GetImpl().link_.quality != v;
		GetImpl().link_.quality = v;
	}

	void Link::SetTransform(const dmat4x3& v)
	{
		GetImpl().shoudSave_ = GetImpl().shoudSave_ || !GetImpl().link_.transform.has_value() || v != *GetImpl().link_.transform;
		GetImpl().link_.transform = v;
	}

	bool Link::HasName() const
	{
		return GetImpl().link_.name.has_value();
	}

	bool Link::HasGCS() const
	{
		return GetImpl().link_.gcs.has_value();
	}

	bool Link::HasVisibility() const
	{
		return GetImpl().link_.visibility.has_value();
	}

	bool Link::HasQuality() const
	{
		return GetImpl().link_.quality.has_value();
	}

	bool Link::HasTransform() const
	{
		return GetImpl().link_.transform.has_value();

	}


	Link::Link(): impl_(new Impl)
	{

	}

	Link::~Link(){}

	bool Link::ShouldSave() const
	{
		return GetImpl().shoudSave_;
	}

	void Link::SetShouldSave(bool shouldSave)
	{
		GetImpl().shoudSave_ = shouldSave;
	}

}
