/*--------------------------------------------------------------------------------------+
|
|     $Source: ScenePersistenceDS.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ScenePersistenceDS.h"

#include "AsyncHelpers.h"
#include "AsyncHttp.inl"
#include "Config.h"
#include "../Singleton/singleton.h"
#include "Core/Network/HttpGetWithLink.h"
#include "SavableItem.h"
#include "SavableItemManager.h"

namespace AdvViz::SDK {
	namespace 
	{
		static dmat4x3 Identity34 = { 1., 0., 0.,
									  0., 1., 0.,
									  0., 0., 1.,
									  0., 0., 0. };
	}
	struct LinkDS::Impl final : public SavableItemWithID
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
			std::optional < std::string>  name;
			std::optional<SJSonGCS> gcs;
 			std::optional<bool> visibility;
 			std::optional<double> quality;
 			std::optional< std::array<double, 12> > transform;
			std::optional<std::string> id;
		};
		using LinkWithId = Link;

		bool shouldDelete_ = false;
		Link link_;

		void FromLinkWithId(const LinkWithId& value)
		{
			link_ = value;
			if (value.id)
				SetDBIdentifier(*value.id);
		}

		LinkWithId ToLinkWithId() const 
		{
			LinkWithId value = link_;
			if (HasDBIdentifier())
				value.id = GetDBIdentifier();
			return value;
		}
		void SetType(const std::string& value)
		{
			if (link_.type != value)
			{
				link_.type = value;
				InvalidateDB();
			}
		}

		void SetRef(const std::string& value)
		{
			if (link_.ref != value)
			{
				link_.ref = value;
				InvalidateDB();
			}
		}

		void SetName(const std::string& value)
		{
			if (!link_.name.has_value() || link_.name.value() != value)
			{
				link_.name = value;
				InvalidateDB();
			}
		}

		void SetGCS(const std::string& v1, const std::array<float, 3>& v2)
		{
			SJSonGCS value{ .wkt = v1, .center = v2 };
			if (!link_.gcs.has_value() || value.wkt != link_.gcs->wkt || value.center != link_.gcs->center)
			{
				link_.gcs = value;
				InvalidateDB();
			}
		}

		void SetVisibility(bool v)
		{
			if (!link_.visibility.has_value() || link_.visibility.value() != v)
			{
				link_.visibility = v;
				InvalidateDB();
			}
		}

		void SetQuality(double v)
		{
			if (!link_.quality.has_value() || link_.quality.value() != v)
			{
				link_.quality = v;
				InvalidateDB();
			}
		}

		void SetTransform(const dmat4x3& v)
		{
			if (!link_.transform.has_value() || link_.transform.value() != v)
			{
				link_.transform = v;
				InvalidateDB();
			}
		}
	};

	class ScenePersistenceDS::Impl : public std::enable_shared_from_this<ScenePersistenceDS::Impl>
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

			std::optional<std::string> HDRIImage;
			std::optional<double> HDRIZRotation;
			std::optional<double> sunIntensity;
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
		struct SJSonHDRI
		{
			std::string	hdriName;
			double		sunPitch;
			double		sunYaw;
			double		sunIntensity;
			double		rotation;
		};

	public:
		struct SThreadSafeData : public SavableItemWithID
		{
			SJsonScene jsonScene_;
			std::vector<std::shared_ptr<AdvViz::SDK::LinkDS>> links_;
		};
		Tools::RWLockableObject<SThreadSafeData> thdata_;

		std::shared_ptr<Http> http_;
		std::shared_ptr<AdvViz::SDK::ITimeline> timeline_;
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
			const std::string& name,
			const std::string& itwinid,
			std::function<void(bool)>&& onCreationDoneFunc,
			bool keepCurrentValues = false)
		{
			struct SJsonIn
			{
				std::string name;
				std::string itwinid;
			};
			SJsonIn jIn{ name, itwinid };
			struct SJsonOut
			{
				std::string id;
				SJsonScene data;
			};
			std::shared_ptr<AsyncRequestGroupCallback> callbackPtr =
				std::make_shared<AsyncRequestGroupCallback>(
					std::move(onCreationDoneFunc), isThisValid_);

			AsyncPostJsonJBody<SJsonOut>(GetHttp(), callbackPtr,
				[this, itwinid, keepCurrentValues](long httpCode,
												   const Tools::TSharedLockableData<SJsonOut>& joutPtr)
			{
				const bool bSuccess = (httpCode == 200 || httpCode == 201);
				if (bSuccess)
				{
					auto unlockedJout = joutPtr->GetAutoLock();
					auto thdata = thdata_.GetAutoLock();
					SJsonOut const& jOut = unlockedJout.Get();
					if (!keepCurrentValues)
						thdata->jsonScene_ = std::move(jOut.data);

					// Update DB identifier now that the scene has been created.
					thdata->SetDBIdentifier(jOut.id);
					BE_LOGI("ITwinScene", "Created Scene in DS for itwin " << itwinid
						<< " (ID: " << thdata->GetDBIdentifier() << ")");
				}
				else
				{
					BE_LOGW("ITwinScene", "Could not create Scene in DS for itwin " << itwinid
						<< ". Http status: " << httpCode);
				}
				return bSuccess;
			},
				std::string("scenes"),
				jIn);

			callbackPtr->OnFirstLevelRequestsRegistered();
		}

		void AsyncSave(std::shared_ptr<AsyncRequestGroupCallback> callbackPtr)
		{
			auto thdata = thdata_.GetAutoLock();
			if (!thdata->HasDBIdentifier())
			{
				BE_LOGE("ITwinScene", "Cannot save Scene in DS with no ID!"
					<< " (from itwin " << thdata->jsonScene_.itwinid << ")");
				return;
			}
			thdata->OnStartSave();

			struct Sout
			{
			};

			AsyncPutJsonJBody<Sout>(GetHttp(), callbackPtr,
				[this](long httpCode, const Tools::TSharedLockableData<Sout>& /*joutPtr*/)
			{
				auto thdata = thdata_.GetAutoLock();
				const bool bSuccess = (httpCode == 200);
				if (bSuccess)
				{
					BE_LOGI("ITwinScene", "Saved Scene in DS with ID " << thdata->GetDBIdentifier()
						<< " itwin " << thdata->jsonScene_.itwinid);
					thdata->OnSaved();
				}
				else
				{
					BE_LOGW("ITwinScene", "Failed saving scene in DS with ID " << thdata->GetDBIdentifier()
						<< " in Scene API from itwin " << thdata->jsonScene_.itwinid
						<< " Http status: " << httpCode);
				}
				return bSuccess;
			},
				"scenes/" + thdata->GetDBIdentifier(),
				thdata->jsonScene_);
		}

		bool Get(const std::string& id)
		{
			auto thdata = thdata_.GetAutoLock();
			long status = GetHttp()->GetJson(thdata->jsonScene_, "scenes/" + id);
			if (status == 200)
			{
				thdata->SetDBIdentifier(id);
				BE_LOGI("ITwinScene", "Loaded Scene in DS with ID " << id);
				return true;
			}
			else
			{
				BE_LOGW("ITwinScene", "Load Scene in DS failed. Http status: " << status);
				return false;
			}
		}

		void AsyncGet(const std::string& id, std::function<void(expected<void, std::string> const&)> onFinish)
		{
			auto SThis = shared_from_this();
			auto loadedData = MakeSharedLockableDataPtr<>(new SJsonScene);
			GetHttp()->AsyncGetJson(loadedData, 
				[SThis, onFinish, id](const Http::Response& /*r*/, expected<Tools::TSharedLockableDataPtr<SJsonScene>, std::string>& exp)
				{
					expected<void, std::string> exp2;
					if (exp)
					{
						auto dataLock((*exp)->GetAutoLock());
						auto thdata = SThis->thdata_.GetAutoLock();
						thdata->jsonScene_ = dataLock.Get();
						thdata->SetDBIdentifier(id);
					}
					else
					{
						exp2 = make_unexpected(exp.error());
					}
					onFinish(exp2);
				},
				"scenes/" + id);
		}
		
		void AsyncLoadLinks(std::function<void(expected<void, std::string> const&)> onFinish)
		{
			std::shared_ptr<Http> const& http = GetDefaultHttp();
			if (!http)
			{
				onFinish(make_unexpected("No HTTP client available"));
				return;
			}

			std::string id;
			{
				auto thdata = thdata_.GetRAutoLock();
				id = thdata->GetDBIdentifier();
			}

			auto SThis = shared_from_this();
			AsyncHttpGetWithLink<LinkDS::Impl::LinkWithId>(http,
				"scenes/" + id + "/links",
				{} /* extra headers*/,
				[SThis](LinkDS::Impl::LinkWithId const& row) -> expected<void, std::string>
				{
					auto thdata = SThis->thdata_.GetAutoLock();
					std::shared_ptr<AdvViz::SDK::LinkDS> link(AdvViz::SDK::LinkDS::New());
					link->GetImpl().FromLinkWithId(row);
					thdata->links_.push_back(link);
					return {};
				},
				onFinish
				);
		}

		bool Delete()
		{
			auto thdata = thdata_.GetAutoLock();
			if (!thdata->HasDBIdentifier())
			{
				BE_LOGE("ITwinScene", "Cannot delete scene in DS with no ID!"
					<< " (from itwin " << thdata->jsonScene_.itwinid << ")");
				return false;
			}
			std::string s("scenes/" + thdata->GetDBIdentifier());
			auto status = GetHttp()->Delete(s, {});
			if (status.first != 200 && status.first != 204)
			{
				BE_LOGW("ITwinScene", "Delete Scene in DS failed. Http status: " << status.first);
				return false;
			}
			else
			{
				BE_LOGI("ITwinScene", "Deleted Scene in DS with ID " << thdata->GetDBIdentifier());
				thdata->SetDBIdentifier("");
				thdata->jsonScene_ = SJsonScene();
				return true;
			}
		}

		inline void HDRIToJson(ITwinHDRISettings const& hdri, SJSonHDRI& jsonHdri) const
		{
			jsonHdri.hdriName = hdri.hdriName;
			jsonHdri.sunPitch = hdri.sunPitch;
			jsonHdri.sunYaw = hdri.sunYaw;
			jsonHdri.sunIntensity = hdri.sunIntensity;
			jsonHdri.rotation = hdri.rotation;
		}

		std::string ExportHDRIAsJson(ITwinHDRISettings const& hdri) const
		{
			SJSonHDRI jsonHdri;
			HDRIToJson(hdri, jsonHdri);
			return rfl::json::write(jsonHdri, YYJSON_WRITE_PRETTY);
		}

		bool ConvertHDRIJsonFileToKeyValueMap(std::filesystem::path const& jsonPath, KeyValueStringMap& outMap) const
		{
			SJSonHDRI jsonHdri;
			std::ifstream ifs(jsonPath);
			std::string parseError;
			if (!Json::FromStream(jsonHdri, ifs, parseError))
			{
				return false;
			}

			outMap["hdriName"] = jsonHdri.hdriName;
			outMap["sunPitch"] = std::to_string(jsonHdri.sunPitch);
			outMap["sunYaw"] = std::to_string(jsonHdri.sunYaw);
			outMap["sunIntensity"] = std::to_string(jsonHdri.sunIntensity);
			outMap["rotation"] = std::to_string(jsonHdri.rotation);

			return true;
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

	void ScenePersistenceDS::AsyncCreate(const std::string& name, const std::string& itwinid,
		std::function<void(bool)>&& onCreationDoneFunc /*= {}*/)
	{
		GetImpl().AsyncCreate(name, itwinid, std::move(onCreationDoneFunc), false);
	}

	bool ScenePersistenceDS::Get(const std::string& /*itwinid*/, const std::string& id)
	{
		bool res = GetImpl().Get(id);
		LoadLinks();
		return res;
	}

	void ScenePersistenceDS::AsyncGet(const std::string& /*itwinid*/, const std::string& id,
		std::function<void(expected<void, std::string> const&)> onFinish)
	{
		auto implPtr = GetImpl().shared_from_this();
		GetImpl().AsyncGet(id, 
			[implPtr, onFinish](expected<void, std::string> const& exp)
		{
			if (exp)
			{
				implPtr->AsyncLoadLinks(onFinish);
			}
			else
			{
				onFinish(exp);
			}
		});
	}

	bool ScenePersistenceDS::Delete()
	{
		return GetImpl().Delete();
	}

	const std::string& ScenePersistenceDS::GetId() const
	{
		auto thdata = GetImpl().thdata_.GetRAutoLock();
		return thdata->GetDBIdentifier();
	}

	const std::string& ScenePersistenceDS::GetName() const
	{
		auto thdata = GetImpl().thdata_.GetRAutoLock();
		return thdata->jsonScene_.name;
	}

	const std::string& ScenePersistenceDS::GetITwinId() const
	{
		auto thdata = GetImpl().thdata_.GetRAutoLock();
		return thdata->jsonScene_.itwinid;
	}

	ScenePersistenceDS::~ScenePersistenceDS()
	{
	}

	ScenePersistenceDS::ScenePersistenceDS()
		: impl_(new Impl)
	{
	}

	ScenePersistenceDS::Impl& ScenePersistenceDS::GetImpl() const {
		return *impl_;
	}

	void ScenePersistenceDS::SetAtmosphere(const ITwinAtmosphereSettings& atmo)
	{
		auto thdata = GetImpl().thdata_.GetAutoLock();
		auto& jsonatmo = thdata->jsonScene_.environment.atmosphere;
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
		jsonatmo.HDRIImage = atmo.HDRIImage;
		jsonatmo.sunIntensity = atmo.sunIntensity;
		jsonatmo.HDRIZRotation = atmo.HDRIZRotation;
		thdata->InvalidateDB();
	}

	AdvViz::SDK::ITwinAtmosphereSettings ScenePersistenceDS::GetAtmosphere() const
	{
		auto thdata = GetImpl().thdata_.GetRAutoLock();	
		AdvViz::SDK::ITwinAtmosphereSettings atmo;
		const auto& jsonatmo = thdata->jsonScene_.environment.atmosphere;
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
		atmo.HDRIImage = jsonatmo.HDRIImage;
		atmo.sunIntensity = jsonatmo.sunIntensity;
		atmo.HDRIZRotation = jsonatmo.HDRIZRotation;
		return atmo;
	}

	void ScenePersistenceDS::SetSceneSettings(const ITwinSceneSettings& ss)
	{
		auto thdata = GetImpl().thdata_.GetAutoLock();
		auto& jsonss = thdata->jsonScene_.environment.sceneSettings;
		jsonss.displayGoogleTiles = ss.displayGoogleTiles;
		jsonss.qualityGoogleTiles = ss.qualityGoogleTiles;
		jsonss.geoLocation = ss.geoLocation;
		thdata->InvalidateDB();
	}

	AdvViz::SDK::ITwinSceneSettings ScenePersistenceDS::GetSceneSettings() const 
	{
		auto thdata = GetImpl().thdata_.GetRAutoLock();
		const auto& jsonss = thdata->jsonScene_.environment.sceneSettings;
		AdvViz::SDK::ITwinSceneSettings ss;
		ss.displayGoogleTiles = jsonss.displayGoogleTiles;
		ss.qualityGoogleTiles = jsonss.qualityGoogleTiles;
		ss.geoLocation = jsonss.geoLocation;
		return ss;
	}

	bool ScenePersistenceDS::ShouldSave() const
	{
		auto thdata = GetImpl().thdata_.GetRAutoLock();
		if (thdata->ShouldSave())
			return true;
		for (auto link : thdata->links_)
		{
			if (link->GetImpl().ShouldSave())
				return true;
		}
		return false;
	}

	void ScenePersistenceDS::AsyncSave(std::function<void(bool)>&& onDataSavedFunc /*= {}*/)
	{
		std::function<void(bool)> onSceneSavedFunc = [onSaveCallback = std::move(onDataSavedFunc)](bool bSuccess)
		{
			BE_LOGI("ITwinScene", "Save Scene end");
			if (onSaveCallback)
				onSaveCallback(bSuccess);
		};

		std::shared_ptr<AsyncRequestGroupCallback> callbackPtr =
			std::make_shared<AsyncRequestGroupCallback>(
				std::move(onSceneSavedFunc), GetImpl().isThisValid_);

		auto actualSaveFunc = [this, callbackPtr](bool bCreationSuccess)
		{
			if (callbackPtr->IsValid() && bCreationSuccess && ShouldSave())
			{
				GetImpl().AsyncSave(callbackPtr);
				AsyncSaveLinks(callbackPtr);

				// Also save timeline
				if (GetTimeline())
				{
					GetTimeline()->AsyncSave(GetId());
				}

				callbackPtr->OnFirstLevelRequestsRegistered();
			}
		};

		std::string name;
		bool hasDBId(false);
		std::string itwinid;
		{
			auto thdata = GetImpl().thdata_.GetRAutoLock();
			name = thdata->jsonScene_.name;
			hasDBId = thdata->HasDBIdentifier();
			itwinid = thdata->jsonScene_.itwinid;
		}

		if (!hasDBId && !name.empty() && !itwinid.empty())
		{
			GetImpl().AsyncCreate(name, itwinid, std::move(actualSaveFunc), true);
		}
		else
		{
			actualSaveFunc(true);
		}
	}

	void ScenePersistenceDS::SetShouldSave(bool shouldSave) const
	{
		auto thdata = GetImpl().thdata_.GetAutoLock();
		thdata->SetShouldSave(shouldSave);
	}

	void ScenePersistenceDS::PrepareCreation(const std::string& name, const std::string& itwinid)
	{
		auto thdata = GetImpl().thdata_.GetAutoLock();
		thdata->jsonScene_.name = name;
		thdata->jsonScene_.itwinid = itwinid;
	}

	std::vector<std::shared_ptr<AdvViz::SDK::ILink>> ScenePersistenceDS::GetLinks() const
	{
		auto thdata = GetImpl().thdata_.GetRAutoLock();
		std::vector<std::shared_ptr<AdvViz::SDK::ILink>> res;
		for (auto l : thdata->links_)
			res.push_back(l);
		return res;

	}

	void ScenePersistenceDS::AsyncLoadLinks(std::function<void(expected<void, std::string> const&)> onFinish)
	{
		GetImpl().AsyncLoadLinks(onFinish);
	}

	void ScenePersistenceDS::LoadLinks()
	{
		std::shared_ptr<Http> const& http = GetDefaultHttp();
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
					auto thdata = GetImpl().thdata_.GetAutoLock();
					thdata->links_.push_back(link);
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
		auto thdata = GetImpl().thdata_.GetAutoLock();
		auto rv = std::dynamic_pointer_cast<LinkDS>(v);
		thdata->links_.push_back(rv);
		thdata->InvalidateDB();
	}

	void ScenePersistenceDS::AsyncSaveLinks(std::shared_ptr<AsyncRequestGroupCallback> callbackPtr_external)
	{
		// This whole set of requests will count for one
		callbackPtr_external->AddRequestToWait();

		std::function<void(bool)> onLinksSavedFunc =
			[this, callbackPtr_external](bool bSuccess)
		{
			if (!callbackPtr_external->IsValid())
				return;

			// Actually remove links which were deleted on the server
			auto thdata = GetImpl().thdata_.GetAutoLock();
			std::erase_if(thdata->links_, [](const std::shared_ptr<LinkDS>& l)
			{
				return l->ShouldDelete() && !l->HasDBIdentifier();
			});

			callbackPtr_external->OnRequestDone(bSuccess);
		};

		std::shared_ptr<AsyncRequestGroupCallback> callbackPtr =
			std::make_shared<AsyncRequestGroupCallback>(
				std::move(onLinksSavedFunc), GetImpl().isThisValid_);

		auto thdata = GetImpl().thdata_.GetRAutoLock();

		std::shared_ptr<Http> const& http = GetImpl().GetHttp();
		const std::string sceneId = thdata->GetDBIdentifier();
		const std::string url("scenes/" + sceneId + "/links");

		for (auto link : thdata->links_)
		{
			const RefID linkId = link->GetId();
			if (!linkId.HasDBIdentifier() && !link->ShouldDelete())
			{
				link->OnStartSave();

				struct SJsonOut
				{
					std::vector<LinkDS::Impl::LinkWithId> links;
				};
				struct SJsonIn
				{
					std::vector<LinkDS::Impl::Link> links;
				};
				SJsonIn Jin;
				Jin.links.push_back(link->GetImpl().link_);

				AsyncPostJsonJBody<SJsonOut>(http, callbackPtr,
					[this, link](long httpCode, const Tools::TSharedLockableData<SJsonOut>& joutPtr)
				{
					RefID linkId = link->GetId();
					bool bSuccess = (httpCode == 200 || httpCode == 201);
					if (bSuccess)
					{
						auto unlockedJout = joutPtr->GetAutoLock();
						SJsonOut const& jOut = unlockedJout.Get();
						bSuccess &= (jOut.links.size() == 1 && jOut.links[0].id.has_value());
						if (bSuccess)
						{
							linkId.SetDBIdentifier(*jOut.links[0].id);
							if (link->GetSaveStatus() == ESaveStatus::InProgress)
							{
								// Update link ID now that it contains the DB identifier.
								link->SetId(linkId);
								BE_LOGI("ITwinScene", "Add Link for scene " << GetId()
									<< " new link " << (*link));
								link->OnSaved();
							}
						}
						else
						{
							BE_LOGW("ITwinScene", "Add Link for scene " << GetId() << " failed, Unable to get ID. Http status: " << httpCode);
						}
					}
					if (!bSuccess)
					{
						BE_LOGW("ITwinScene", "Add Link for scene " << GetId() << " failed. Http status: " << httpCode
							<< " with link " << (*link));
					}
					return bSuccess;
				},
					url, Jin);
			}
			else if (link->ShouldDelete() && linkId.HasDBIdentifier())
			{
				struct SJsonIn
				{
					std::vector<std::string> ids;
				};
				SJsonIn Jin;
				Jin.ids.push_back(link->GetDBIdentifier());
				AsyncDeleteJsonNoOutput(http, callbackPtr,
					[this, link](long httpCode)
				{
					const bool bSuccess = (httpCode == 200 || httpCode == 204 /* No-Content*/);
					if (bSuccess)
					{
						BE_LOGI("ITwinScene", "Deleted Link with scene ID " << GetId()
							<< " link " << (*link));
						link->SetDBIdentifier("");
					}
					else
					{
						BE_LOGW("ITwinScene", "Delete Link failed. Http status: " << httpCode
							<< " scene id " << GetId()
							<< " link " << (*link));
					}
					return bSuccess;
				},
					url, Jin);
			}
			else if (linkId.HasDBIdentifier() && !link->ShouldDelete())
			{
				link->OnStartSave();

				struct SJsonOut
				{
					int numUpdated = 0;
				};
				struct SJsonIn
				{
					std::vector<LinkDS::Impl::LinkWithId> links;
				};
				SJsonIn Jin;
				Jin.links.push_back(link->GetImpl().ToLinkWithId());
				AsyncPutJsonJBody<SJsonOut>(http, callbackPtr,
					[this, link](long httpCode, const Tools::TSharedLockableData<SJsonOut>& joutPtr)
				{
					bool bSuccess = (httpCode == 200 || httpCode == 201);
					if (bSuccess)
					{
						auto unlockedJout = joutPtr->GetAutoLock();
						SJsonOut const& jOut = unlockedJout.Get();
						bSuccess &= (jOut.numUpdated == 1);

						if (bSuccess)
						{
							BE_LOGI("ITwinScene", "Update Link for scene " << GetId()
								<< " with link " << (*link));
							link->OnSaved();
						}
						else
						{
							BE_ISSUE("Update Link: http code OK but wrong result count", jOut.numUpdated);
						}
					}
					if (!bSuccess)
					{
						BE_LOGW("ITwinScene", "Update Link  for scene " << GetId()
							<< " failed. Http status: " << httpCode
							<< " with link link " << (*link));
					}
					return bSuccess;
				},
					url, Jin);
			}
		}
		callbackPtr->OnFirstLevelRequestsRegistered();
	}

	std::shared_ptr<AdvViz::SDK::ILink> ScenePersistenceDS::MakeLink()
	{
		return 	std::shared_ptr<AdvViz::SDK::ILink>(LinkDS::New());

	}

	void ScenePersistenceDS::SetTimeline(const std::shared_ptr<AdvViz::SDK::ITimeline>& timeline)
	{
		GetImpl().timeline_ = timeline;
	}

	std::string ScenePersistenceDS::ExportHDRIAsJson(ITwinHDRISettings const& hdri) const 
	{
		return GetImpl().ExportHDRIAsJson(hdri);
	}

	bool ScenePersistenceDS::ConvertHDRIJsonFileToKeyValueMap(std::filesystem::path const& jsonPath, KeyValueStringMap& outMap) const {
		return GetImpl().ConvertHDRIJsonFileToKeyValueMap(jsonPath, outMap);
	}

	std::shared_ptr<AdvViz::SDK::ITimeline> ScenePersistenceDS::GetTimeline()
	{
		return GetImpl().timeline_;
	}

	AdvViz::expected<ScenePtrVector, HttpError> GetITwinScenesDS(const std::string& itwinid)
	{
		std::shared_ptr<Http> const& http = GetDefaultHttp();
		if (!http)
		{
			return make_unexpected(HttpError{
				.message = "No Http support to retrieve scenes",
				.httpCode = -2 });
		}

		ScenePtrVector scenes;

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

		if (!ret)
		{
			BE_LOGW("ITwinScene", "Loading scenes failed for iTwin " << itwinid << ". " << ret.error());
			return make_unexpected(HttpError{
				.message = ret.error(),
				.httpCode = -3}
			);
		}

		BE_LOGI("ITwinScene", "Found " << scenes.size() << " Scenes(s) for iTwin " << itwinid);
		return scenes;
	}

	void AsyncGetITwinSceneInfosDS(const std::string& itwinid,
		std::function<void(AdvViz::expected<SceneInfoVec, HttpError>)>&& inCallback)
	{
		std::shared_ptr<Http> const& http = GetDefaultHttp();
		if (!http)
		{
			inCallback(make_unexpected(HttpError{
				.message = "No Http support to retrieve scene infos",
				.httpCode = -2}
			));
			return;
		}

		struct JsonSceneWithId
		{
			std::string name;
			std::string itwinid;
			std::string id;
		};

		std::shared_ptr<std::mutex> scenesMutex = std::make_shared<std::mutex>();
		std::shared_ptr<SceneInfoVec> sceneInfos = std::make_shared<SceneInfoVec>();

		AsyncHttpGetWithLink<JsonSceneWithId>(http,
			"scenes?iTwinId=" + itwinid,
			{} /* extra headers*/,
			[itwinid, sceneInfos, scenesMutex](JsonSceneWithId const& row) -> expected<void, std::string>
		{
			std::lock_guard<std::mutex> lock(*scenesMutex);
			sceneInfos->emplace_back(SceneInfo{
				.id = row.id,
				.iTwinId = row.itwinid,
				.displayName = row.name});
			return {};
		},
			[sceneInfos, callback = std::move(inCallback), itwinid](expected<void, std::string> const& ret)
		{
			if (ret)
			{
				BE_LOGI("ITwinScene", "[DS] Found " << sceneInfos->size() << " Scenes(s) for iTwin " << itwinid);
				callback(*sceneInfos);
			}
			else
			{
				BE_LOGW("ITwinScene", "Fetching scene infos failed for iTwin " << itwinid << ". " << ret.error());
				callback(make_unexpected(HttpError{
					.message = ret.error(),
					.httpCode = -3})
				);
			}
		});
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
		GetImpl().SetType(value);
	}

	void LinkDS::SetRef(const std::string& value)
	{
		GetImpl().SetRef(value);
	}

	void LinkDS::SetName(const std::string& value)
	{
		GetImpl().SetName(value);
	}

	void LinkDS::SetGCS(const std::string& v1, const std::array<float, 3>& v2)
	{
		GetImpl().SetGCS(v1, v2);
	}

	void LinkDS::SetVisibility(bool v)
	{
		GetImpl().SetVisibility(v);
	}

	void LinkDS::SetQuality(double v)
	{
		GetImpl().SetQuality(v);
	}

	void LinkDS::SetTransform(const dmat4x3& v)
	{
		GetImpl().SetTransform(v);
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

	const RefID& LinkDS::GetId() const
	{
		return GetImpl().GetId();
	}
	void LinkDS::SetId(const RefID& id)
	{
		GetImpl().SetId(id);
	}

	ESaveStatus LinkDS::GetSaveStatus() const
	{
		return GetImpl().GetSaveStatus();
	}
	void LinkDS::SetSaveStatus(ESaveStatus status)
	{
		GetImpl().SetSaveStatus(status);
	}

	void LinkDS::Delete(bool value)
	{
		GetImpl().shouldDelete_ = value;
		GetImpl().InvalidateDB();
	}

	bool LinkDS::ShouldDelete() const
	{
		return GetImpl().shouldDelete_;
	}

}
