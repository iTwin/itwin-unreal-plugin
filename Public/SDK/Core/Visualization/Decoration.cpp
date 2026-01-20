/*--------------------------------------------------------------------------------------+
|
|     $Source: Decoration.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "Decoration.h"
#include "Config.h"
#include "../Singleton/singleton.h"
#include "Core/Network/HttpGetWithLink.h"

namespace AdvViz::SDK {

	class Decoration::Impl
	{
	public:
		struct SJsonDeco
		{
			std::string name;
			std::string itwinid;
			std::optional<GCS> gcs;
		};

		std::string id_;
		std::shared_ptr<Http> http_;
		SJsonDeco jsonDeco_;
		Tools::IGCSTransformPtr gcsTransfrom_;

		std::shared_ptr<Http>& GetHttp() { return http_; }
		void SetHttp(const std::shared_ptr<Http>& http) { http_ = http; }

		void Create(
			const std::string& name, const std::string& itwinid)
		{
			jsonDeco_.name = name;
			jsonDeco_.itwinid = itwinid;
			struct SJsonOut { std::string id; SJsonDeco data; };
			SJsonOut jOut;

			long status = GetHttp()->PostJsonJBody(jOut, std::string("decorations"), jsonDeco_);
			if (status == 200 || status == 201)
			{
				jsonDeco_ = std::move(jOut.data);
				id_ = jOut.id;
				BE_LOGI("ITwinDecoration", "Created decoration for itwin " << itwinid
					<< " (ID: " << id_ << ")");
			}
			else
			{
				BE_LOGW("ITwinDecoration", "Could not create decoration for itwin " << itwinid
					<< ". Http status: " << status);
			}
		}

		void Get(const std::string& id)
		{

			long status = GetHttp()->GetJson(jsonDeco_, "decorations/"+ id);
			if (status == 200)
			{
				id_ = id;
				BE_LOGI("ITwinDecoration", "Loaded decoration with ID " << id_);
			}
			else
			{
				BE_LOGW("ITwinDecoration", "Load decoration failed. Http status: " << status);
			}
		}

		void Delete()
		{
			std::string s("decorations/" + id_);
			Http::Response status = GetHttp()->Delete(s, {});
			if (status.first != 200)
			{
				BE_LOGW("ITwinDecoration", "Delete decoration failed. Http status: " << status.first);
			}
			else
			{
				BE_LOGI("ITwinDecoration", "Deleted decoration with ID " << id_);
			}
			id_ = "";
			jsonDeco_ = SJsonDeco();
		}
	};

	template<>
	Tools::Factory<IDecoration>::Globals::Globals()
	{
		newFct_ = []() {
			IDecoration* p(static_cast<IDecoration*>(new Decoration()));
			return p;
			};
	}

	template<>
	Tools::Factory<IDecoration>::Globals& Tools::Factory<IDecoration>::GetGlobals()
	{
		return singleton<Tools::Factory<IDecoration>::Globals>();
	}

	void Decoration::SetHttp(std::shared_ptr<Http> http)
	{
		GetImpl().SetHttp(http);
	}

	void Decoration::Create(const std::string& name, const std::string& itwinid)
	{
		GetImpl().Create(name, itwinid);
	}

	void Decoration::Get(const std::string& id)
	{
		GetImpl().Get(id);
	}

	void Decoration::Delete()
	{
		GetImpl().Delete();
	}

	const std::string& Decoration::GetId()
	{
		return GetImpl().id_;
	}

	void Decoration::SetGCSTransform(const Tools::IGCSTransformPtr& transform)
	{
		GetImpl().gcsTransfrom_ = transform;
	}

	const Tools::IGCSTransformPtr& Decoration::GetGCSTransform() const
	{
		return GetImpl().gcsTransfrom_;
	}

	const std::optional<GCS>& Decoration::GetGCS() const
	{
		return GetImpl().jsonDeco_.gcs;
	}

	void Decoration::SetGCS(const GCS &gcs)
	{
		GetImpl().jsonDeco_.gcs = gcs;
	}

	Decoration::~Decoration()
	{
	}

	Decoration::Decoration():impl_(new Impl)
	{
		GetImpl().SetHttp(GetDefaultHttp());
	}

	Decoration::Impl& Decoration::GetImpl() {
		return *impl_;
	}

	const Decoration::Impl& Decoration::GetImpl() const {
		return *impl_;
	}

	std::vector<std::shared_ptr<IDecoration>> GetITwinDecorations(
		const std::string& itwinid)
	{
		std::vector<std::shared_ptr<IDecoration>> decorations;

		std::shared_ptr<Http>& http = GetDefaultHttp();
		if (!http)
			return decorations;

		struct SJsonDecoWithId
		{
			std::string id;
			std::string name;
			std::string itwinid;
			std::optional<GCS> gcs;
		};

		auto ret = HttpGetWithLink<SJsonDecoWithId>(http,
			"decorations?iTwinId=" + itwinid,
			{} /* extra headers*/,
			[&decorations](SJsonDecoWithId const& row) -> expected<void, std::string>
		{
				std::shared_ptr<AdvViz::SDK::IDecoration> deco(AdvViz::SDK::IDecoration::New());
					deco->Get(row.id);
					decorations.push_back(deco);

			return {};
		});

		if (ret)
		{
			BE_LOGI("ITwinDecoration", "Found " << decorations.size() << " decoration(s) for iTwin " << itwinid);
		}
		else
		{
			BE_LOGW("ITwinDecoration", "Load decorations failed. " << ret.error());
		}

		return decorations;
	}
}
