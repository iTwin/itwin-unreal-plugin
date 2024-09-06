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

	struct SJSonGCS
	{
		std::string wkt;
		std::array<float, 3> center = {0.f, 0.f, 0.f};
	};

	class Decoration::Impl
	{
	public:
		struct SJsonDeco
		{
			std::string name;
			std::string itwinid;
			std::optional<SJSonGCS> gcs;
		};

		std::string id_;
		std::shared_ptr<Http> http_;
		SJsonDeco jsonDeco_;

		std::shared_ptr<Http>& GetHttp() { return http_; }
		void SetHttp(const std::shared_ptr<Http>& http) { http_ = http; }

		void Create(
			const std::string& name, const std::string& itwinid, const std::string& accessToken)
		{
			struct SJsonIn { std::string name; std::string itwinid; };
			SJsonIn jIn{ name, itwinid };
			struct SJsonOut { std::string id; SJsonDeco data; };
			SJsonOut jOut;

			Http::Headers headers;
			headers.emplace_back("Authorization", std::string("Bearer ") + accessToken);

			long status = GetHttp()->PostJsonJBody(jOut, std::string(""), jIn, headers);
			if (status == 200 || status == 201)
			{
				jsonDeco_ = std::move(jOut.data);
				id_ = jOut.id;
			}
			else
				throw std::string("Create decoration failed. http status:" + std::to_string(status));
		}

		void Get(const std::string& id, const std::string& accessToken)
		{
			Http::Headers headers;
			headers.emplace_back("Authorization", std::string("Bearer ") + accessToken);

			long status = GetHttp()->GetJson(jsonDeco_, id, "", headers);
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
			jsonDeco_ = SJsonDeco();
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

	void Decoration::Create(const std::string& name, const std::string& itwinid, const std::string& accessToken)
	{
		GetImpl().Create(name, itwinid, accessToken);
	}

	void Decoration::Get(const std::string& id, const std::string& accessToken)
	{
		GetImpl().Get(id, accessToken);
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

	std::vector<std::shared_ptr<IDecoration>> GetITwinDecorations(
		const std::string& itwinid, const std::string& accessToken)
	{
		std::vector<std::shared_ptr<IDecoration>> decorations;

		std::shared_ptr<Http>& http = GetDefaultHttp();
		if (!http)
			return decorations;

		Http::Headers headers;
		headers.emplace_back("Authorization", std::string("Bearer ") + accessToken);

		struct SJsonInEmpty	{};

		struct SJsonLink
		{
			std::optional<std::string> prev;
			std::optional<std::string> self;
			std::optional<std::string> next;
		};

		struct SJsonDecoWithId
		{
			std::string id;
			std::string name;
			std::string itwinid;
			std::optional<SJSonGCS> gcs;
		};

		struct SJsonOut
		{ 
			int total_rows;
			std::vector<SJsonDecoWithId> rows;
			SJsonLink _links;
		};

		SJsonInEmpty jIn;
		SJsonOut jOut;
		long status = http->GetJsonJBody(jOut, "?iTwinId=" + itwinid, jIn, headers);

		if (status == 200 || status == 201)
		{
			for (auto& row : jOut.rows)
			{
				std::shared_ptr<SDK::Core::IDecoration> deco = SDK::Core::IDecoration::New();
				deco->Get(row.id, accessToken);
				decorations.push_back(deco);
			}
		}
		else
			throw std::string("Load decorations failed. http status:" + std::to_string(status));


		return decorations;
	}
}
