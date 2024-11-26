/*--------------------------------------------------------------------------------------+
|
|     $Source: ScenePersistence.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ScenePersistence.h"
#include "Config.h"

namespace SDK::Core {


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
		bool displayGoogleTiles;
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
	class ScenePersistence::Impl
	{
	public:

		std::string id_;
		std::shared_ptr<Http> http_;
		SJsonScene jsonScene_;
		bool shoudSave_ = false;

		std::shared_ptr<Http>& GetHttp() { return http_; }
		void SetHttp(const std::shared_ptr<Http>& http) { http_ = http; }

		void Create(
			const std::string& name, const std::string& itwinid, const std::string& accessToken)
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
		void Get(const std::string& id, const std::string& accessToken)
		{
			Http::Headers headers;
			headers.emplace_back("Authorization", std::string("Bearer ") + accessToken);

			long status = GetHttp()->GetJson(jsonScene_, "scenes/" + id, "", headers);
			if (status == 200)
			{
				id_ = id;
				BE_LOGI("ITwinScene", "Loaded Scene with ID " << id_);
			}
			else
			{
				BE_LOGW("ITwinScene", "Load Scene failed. Http status: " << status);
			}
		}

		void Delete()
		{
			std::string s("scenes/" + id_);
			auto status = GetHttp()->Delete(s, "");
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
	std::function<std::shared_ptr<IScenePersistence>()> Tools::Factory<IScenePersistence>::newFct_ = []() {
		std::shared_ptr<IScenePersistence> p(static_cast<IScenePersistence*>(new ScenePersistence()));
		return p;
		};

	void ScenePersistence::SetHttp(std::shared_ptr<Http> http)
	{
		GetImpl().SetHttp(http);
	}

	void ScenePersistence::Create(const std::string& name, const std::string& itwinid, const std::string& accessToken)
	{
		GetImpl().Create(name, itwinid, accessToken);
	}

	void ScenePersistence::Get(const std::string& id, const std::string& accessToken)
	{
		GetImpl().Get(id, accessToken);
	}

	void ScenePersistence::Delete()
	{
		GetImpl().Delete();
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
		GetImpl().shoudSave_ = true;
	}

	SDK::Core::ITwinSceneSettings ScenePersistence::GetSceneSettings() const 
	{
		const auto& jsonss = GetImpl().jsonScene_.environment.sceneSettings;
		SDK::Core::ITwinSceneSettings ss;
		ss.displayGoogleTiles = jsonss.displayGoogleTiles;
		return ss;
	}

	bool ScenePersistence::ShoudlSave() const
	{
		return GetImpl().shoudSave_;
	}

	void ScenePersistence::Save(const std::string& accessToken)
	{
		if (ShoudlSave())
		{
			GetImpl().Save(accessToken);
			GetImpl().shoudSave_ = false;
		}
	}

	void ScenePersistence::SetShoudlSave(bool shouldSave) const
	{
		GetImpl().shoudSave_ = shouldSave;
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

		struct SJsonInEmpty	{};

		struct SJsonLink
		{
			std::optional<std::string> prev;
			std::optional<std::string> self;
			std::optional<std::string> next;
		};

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
				std::shared_ptr<SDK::Core::IScenePersistence> scene = SDK::Core::IScenePersistence::New();
				scene->Get(row.id, accessToken);
				scenes.push_back(scene);
			}
		}
		else
		{
			BE_LOGW("ITwinScene", "Load Scenes failed. Http status: " << status);
		}

		return scenes;
	}
}
