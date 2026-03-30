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
#include "Core/Visualization/AsyncHttp.inl"

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
		std::shared_ptr< std::atomic_bool > isThisValid_;

		Impl()
		{
			isThisValid_ = std::make_shared<std::atomic_bool>(true);

			SetHttp(GetDefaultHttp());
		}

		~Impl()
		{
			*isThisValid_ = false;
		}

		std::shared_ptr<Http> const& GetHttp() const { return http_; }
		void SetHttp(std::shared_ptr<Http> const& http) { http_ = http; }

		void AsyncCreate(
			const std::string& name, const std::string& itwinid,
			std::function<void(bool)>&& onCreationDoneFunc = {})
		{
			jsonDeco_.name = name;
			jsonDeco_.itwinid = itwinid;
			struct SJsonOut
			{
				std::string id;
				SJsonDeco data;
			};

			std::shared_ptr<AsyncRequestGroupCallback> callbackPtr =
				std::make_shared<AsyncRequestGroupCallback>(
					std::move(onCreationDoneFunc), isThisValid_);

			AsyncPostJsonJBody<SJsonOut>(GetHttp(), callbackPtr,
				[this, itwinid](long httpCode, const Tools::TSharedLockableData<SJsonOut>& joutPtr)
			{
				const bool bSuccess = (httpCode == 200 || httpCode == 201);
				if (bSuccess)
				{
					auto unlockedJout = joutPtr->GetAutoLock();
					SJsonOut const& jOut = unlockedJout.Get();

					jsonDeco_ = std::move(jOut.data);
					id_ = jOut.id;
					BE_LOGI("ITwinDecoration", "Created decoration for iTwin " << itwinid
						<< " (ID: " << id_ << ")");
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Could not create decoration for itwin " << itwinid
						<< ". Http status: " << httpCode);
				}
				return bSuccess;
			},
				std::string("decorations"),
				jsonDeco_);

			callbackPtr->OnFirstLevelRequestsRegistered();
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
			if (status.first != 200 && status.first != 204)
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

	void Decoration::AsyncCreate(const std::string& name, const std::string& itwinid,
		std::function<void(bool)>&& onCreationDoneFunc /*= {}*/)
	{
		GetImpl().AsyncCreate(name, itwinid, std::move(onCreationDoneFunc));
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

	Decoration::Decoration()
		: impl_(new Impl)
	{
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

		std::shared_ptr<Http> const& http = GetDefaultHttp();
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
