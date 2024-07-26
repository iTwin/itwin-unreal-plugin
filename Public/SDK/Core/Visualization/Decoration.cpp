/*--------------------------------------------------------------------------------------+
|
|     $Source: Decoration.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "Decoration.h"
#include "Config.h"

namespace SDK::Core {

	class Decoration::Impl
	{
	public:

		struct SJSonGCS
		{
			std::string wkt;
			float center[3] = {0.f, 0.f, 0.f};
		};

		struct SJson
		{
			std::string name;
			SJSonGCS gcs;
		};

		std::string id_;
		std::shared_ptr<Http> http_;
		SJson json_;

		std::shared_ptr<Http>& GetHttp() { return http_; }
		void SetHttp(const std::shared_ptr<Http>& http) { http_ = http; }

		void Create(const std::string& name)
		{
			struct SJsonIn { std::string name; };
			SJsonIn jIn{ name };
			struct SJsonOut { std::string id; SJson data;};
			SJsonOut jOut;
			long status = GetHttp()->PostJsonJBody(jOut, "decorations", jIn);
			if (status == 200 || status == 201)
			{
				json_ = std::move(jOut.data);
				id_ = jOut.id;
			}
			else
				throw std::string("Create decoration failed. http status:" + std::to_string(status));
		}

		void Get(const std::string& id)
		{
			long status = GetHttp()->GetJson(json_, "decorations/" + id, "");
			if (status == 200)
			{
				id_ = id;
			}
			else
				throw std::string("Load decoration failed. http status:" + std::to_string(status));
		}

		void Delete()
		{
			std::string s("decorations/" + id_);
			auto status = GetHttp()->Delete(s, "");
			if (status.first != 200)
			{
				throw std::string("Delete decoration failed. http status:" + std::to_string(status.first));
			}
			id_ = "";
			json_ = SJson();
		}
	};

	template<>
	std::function<std::shared_ptr<IDecoration>()> Tools::Factory<IDecoration>::newFct_ = []() {
		std::shared_ptr<IDecoration> p(static_cast<IDecoration*>(new Decoration()));
		return p;
		};

	void Decoration::SetHttp(std::shared_ptr<Http> http)
	{
		GetImpl().SetHttp(http);
	}

	void Decoration::Create(const std::string& name)
	{
		GetImpl().Create(name);
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
}
