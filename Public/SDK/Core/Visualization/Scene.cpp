/*--------------------------------------------------------------------------------------+
|
|     $Source: Scene.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "Scene.h"
#include "Config.h"

namespace SDK::Core {
	
	class DecorationEnvironment::Impl
	{
	public:
		std::string id_;
		Impl(const std::string &id): id_(id){}
	};

	DecorationEnvironment::DecorationEnvironment(const std::string &s):impl_(new Impl(s))
	{}

	DecorationEnvironment::~DecorationEnvironment() 
	{}

	DecorationEnvironment::Impl& DecorationEnvironment::GetImpl()
	{
		return *impl_;
	}

	const std::string& DecorationEnvironment::GetId()
	{
		return GetImpl().id_;
	}
	template<>
	std::function<std::shared_ptr<IDecorationEnvironment>(std::string)> Tools::Factory<IDecorationEnvironment, std::string>::newFct_ = [](std::string id) {
		std::shared_ptr<IDecorationEnvironment> p(static_cast<IDecorationEnvironment*>(new DecorationEnvironment(id)));
		return p;
		};

	class DecorationLayer::Impl
	{
	public:
		std::string id_;
		Impl(const std::string& id) : id_(id) {}
	};

	DecorationLayer::DecorationLayer(const std::string& s) :impl_(new Impl(s))
	{}

	DecorationLayer::~DecorationLayer()
	{}

	DecorationLayer::Impl& DecorationLayer::GetImpl()
	{
		return *impl_;
	}

	const std::string& DecorationLayer::GetId()
	{
		return GetImpl().id_;
	}
	template<>
	std::function<std::shared_ptr<IDecorationLayer>(std::string)> Tools::Factory<IDecorationLayer, std::string>::newFct_ = [](std::string id) {
		std::shared_ptr<IDecorationLayer> p(static_cast<IDecorationLayer*>(new DecorationLayer(id)));
		return p;
		};



	class Scene::Impl
	{
	public:

		struct SJson
		{
			std::string name;
			std::string decorationenvironmentid;
			std::vector<std::string> decorationlayerids;
		};

		std::string id_;
		std::shared_ptr<Http> http_;
		SJson json_;
		std::shared_ptr<IDecorationEnvironment> decEnv_;
		std::vector<std::shared_ptr<IDecorationLayer>> decLayers_;

		std::shared_ptr<Http>& GetHttp() {
			return http_;
		}

		void Create(const std::string& name)
		{
			struct SJsonIn { std::string name; };
			SJsonIn jIn{ name };
			struct SJsonOut { std::string id; SJson data;};
			SJsonOut jOut;
			long status = GetHttp()->PostJsonJBody(jOut, "scene", jIn);
			if (status == 200 || status == 201)
			{
				json_ = std::move(jOut.data);
				id_ = jOut.id;
				decEnv_ = IDecorationEnvironment::New(json_.decorationenvironmentid);
				for (auto& i : json_.decorationlayerids)
					decLayers_.push_back(IDecorationLayer::New(i));
			}
			else
				throw std::string("Create scene failed. http status:" + std::to_string(status));
		}

		void Get(const std::string& id)
		{
			long status = GetHttp()->GetJson(json_, "scene/" + id, "");
			if (status == 200)
			{
				id_ = id;
				decEnv_ = IDecorationEnvironment::New(json_.decorationenvironmentid);
				for (auto& i : json_.decorationlayerids)
					decLayers_.push_back(IDecorationLayer::New(i));
			}
			else
				throw std::string("Load scene failed. http status:" + std::to_string(status));
		}

		void Delete(bool deleteLayers)
		{
			std::string s("scene/" + id_);
			if (deleteLayers)
				s += "?deletelayers=true";
			auto status = GetHttp()->Delete(s, "");
			if (status.first != 200)
			{
				throw std::string("Delete scene failed. http status:" + std::to_string(status.first));
			}
			id_ = "";
			json_ = SJson();
		}

		void SetHttp(const std::shared_ptr<Http>& http)
		{
			http_ = http;
		}

		const std::shared_ptr<IDecorationEnvironment>& GetDecorationEnvironment()
		{
			return decEnv_;
		}

		const std::vector<std::shared_ptr<IDecorationLayer>>& GetDecorationLayers()
		{
			return decLayers_;
		}
	};

	template<>
	std::function<std::shared_ptr<IScene>()> Tools::Factory<IScene>::newFct_ = []() {
		std::shared_ptr<IScene> p(static_cast<IScene*>(new Scene()));
		return p;
		};

	void Scene::SetHttp(std::shared_ptr<Http> http)
	{
		GetImpl().SetHttp(http);
	}

	void Scene::Create(const std::string& name)
	{
		GetImpl().Create(name);
	}

	void Scene::Get(const std::string& id)
	{
		GetImpl().Get(id);
	}

	void Scene::Delete(bool deleteLayers)
	{
		GetImpl().Delete(deleteLayers);
	}

	const std::shared_ptr<IDecorationEnvironment>& Scene::GetDecorationEnvironment()
	{
		return GetImpl().GetDecorationEnvironment();
	}

	const std::vector<std::shared_ptr<IDecorationLayer>>& Scene::GetDecorationLayers() 
	{
		return GetImpl().GetDecorationLayers();
	}

	const std::string& Scene::GetId()
	{
		return GetImpl().id_;
	}

	Scene::~Scene()
	{
	}

	Scene::Scene():impl_(new Impl)
	{
		GetImpl().SetHttp(GetDefaultHttp());
	}

	Scene::Impl& Scene::GetImpl() {
		return *impl_;
	}
}
