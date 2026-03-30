/*--------------------------------------------------------------------------------------+
|
|     $Source: ScenePersistenceAPI.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ScenePersistenceAPI.h"
#include "Config.h"
#include "Core/Singleton/singleton.h"
#include "Core/Network/HttpGetWithLink.h"
#include "AsyncHelpers.h"
#include "AsyncHttp.inl"
#include <regex>
#include <chrono>

namespace AdvViz::SDK {

	static std::string SceneAPIGuidtoDSID(const std::string& guid)
	{
		if (guid.size() < 28)
		{
			BE_ISSUE("Unexpected size from SceneAPI Guid", guid);
			return "";
		}
		return guid.substr(0, 8) + guid.substr(9, 4) + guid.substr(14, 4) + guid.substr(19, 4) + guid.substr(24, 4);
	}


	namespace SceneAPIDetails
	{
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
			std::optional<std::string> HDRIImage;
			std::optional<double> HDRIZRotation;
			std::optional<double> sunIntensity;
		};
		struct SJsonSettings
		{
			SJSonAtmosphere atmosphere;
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
			std::optional<bool> visible; //obsolete
			rfl::Rename<"class", std::optional<std::string>> type;
			std::optional<std::string> repositoryId;
			std::optional<std::string> id;
			std::optional<std::string> name;
			std::optional<double> quality;
			std::optional< std::array<double, 16> > ecefTransform;
			std::optional< std::vector<double> > adjustment;
			std::optional<SJSonAtmosphere> atmosphere;
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
			std::optional<bool> visible;
		};
		using SceneAPILinks = ITwinAPI::SLinks;
		struct SJsonOut
		{
			std::optional<std::vector<JsonObjectWithId>> objects;
			std::optional<SceneAPILinks> _links;
		};
	}



	struct LinkAPI::Impl final : SavableItemWithID
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

		bool shouldDelete_ = false;
		Link link_;
		std::string sublinkId_ = "";
		std::shared_ptr<AdvViz::SDK::LinkAPI> parentLink_;


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

		void Delete(bool value)
		{
			shouldDelete_ = value;
			if (value)
				InvalidateDB();
		}
	};

	class Credential
	{
	public:
		std::shared_ptr<AdvViz::SDK::Http> Http_;
		std::shared_ptr<AdvViz::SDK::Http> const& GetHttp() { return Http_; }


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
	std::string Credential::server = "api.bentley.com";
	static Credential creds;
	static bool enableExportOfResources = true;

	namespace
	{
		static dmat4x3 Identity34 = { 1., 0., 0.,
									  0., 1., 0.,
									  0., 0., 1.,
									  0., 0., 0. };
	}


	class ScenePersistenceAPI::Impl : public std::enable_shared_from_this<ScenePersistenceAPI::Impl>
	{
	public:
		struct SJsonInEmpty {};

		using SJSonAtmosphere = SceneAPIDetails::SJSonAtmosphere;

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
			SJSonAtmosphere jsonAtmo_;
			SJSonSceneSettings jsonSS_;
			LinkAPIPtrVec links_;
		};
		Tools::RWLockableObject<SThreadSafeData> thdata_;


		std::shared_ptr<AdvViz::SDK::ITimeline> timeline_;
		std::shared_ptr<Http> http_;
		std::shared_ptr< std::atomic_bool > isThisValid_;

		Impl()
		{
			isThisValid_ = std::make_shared<std::atomic_bool>(true);
		}

		~Impl()
		{
			*isThisValid_ = false;
		}

		bool AddLink(const std::shared_ptr<AdvViz::SDK::LinkAPI > &newlink)
		{
			auto thdata = thdata_.GetAutoLock();
			std::string id = newlink->GetImpl().GetDBIdentifier();
			if (id.empty())
			{
				thdata->links_.push_back(newlink);
				return true;;
			}
			for (auto& link : thdata->links_)
			{
				if (link->GetImpl().GetDBIdentifier() == id)
				{
					if (link->ShouldSave())
					{
						return true;
					}
					else
					{
						link = newlink;
						return false;
					}
				}
			}

			thdata->links_.push_back(newlink);
			return true;
		}


		std::shared_ptr<Http> const& GetHttp() const
		{
			if (http_)
				return http_;
			if (!creds.Http_)
				creds.SetDefaultHttp(GetDefaultHttp());
			return creds.GetHttp();
		}
		void SetHttp(const std::shared_ptr<Http>& http)
		{
			http_ = http;
		}

		void AsyncCreate(
			const std::string& name,
			const std::string& itwinid,
			std::function<void(bool)>&& onCreationDoneFunc,
			bool keepCurrentValues = false)
		{
			auto thdata = thdata_.GetAutoLock();
			struct SJsonIn
			{
				std::string displayName;
			};
			SJsonIn const jIn{ name };

			struct SJsonOutData
			{
				std::string displayName;
				std::string id;
			};
			struct SJsonOut
			{
				SJsonOutData scene;
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
					{
						thdata->jsonScene_.itwinid = itwinid;
						thdata->jsonScene_.name = jOut.scene.displayName;
					}
					// Update DB identifier now that the scene has been created.
					thdata->SetDBIdentifier(jOut.scene.id);

					BE_LOGI("ITwinScene", "Created scene in Scene API for itwin " << itwinid
						<< " (ID: " << thdata->GetDBIdentifier() << ")");

					//add atmo and scene settings links
					std::shared_ptr<AdvViz::SDK::LinkAPI> link(AdvViz::SDK::LinkAPI::New());
					link->GetImpl().link_.type = "atmosphere";
					thdata->links_.push_back(link);
					std::shared_ptr<AdvViz::SDK::LinkAPI> link2(AdvViz::SDK::LinkAPI::New());
					link2->GetImpl().link_.type = "SceneSettings";
					thdata->links_.push_back(link2);
				}
				else
				{
					BE_LOGW("ITwinScene", "Could not create scene in Scene API for itwin " << itwinid
						<< ". Http status: " << httpCode);
				}
				return bSuccess;
			},
				"scenes?iTwinId="+itwinid,
				jIn);

			callbackPtr->OnFirstLevelRequestsRegistered();
		}

		void AsyncSave(std::shared_ptr<AsyncRequestGroupCallback> callbackPtr)
		{
			auto thdata = thdata_.GetAutoLock();

			if (!thdata->HasDBIdentifier())
			{
				BE_LOGE("ITwinScene", "Cannot save scene with no ID!"
					<< " (from itwin " << thdata->jsonScene_.itwinid << ")");
				return;
			}
			thdata->OnStartSave();

			struct SJsonIn { std::string displayName; };
			SJsonIn jIn;
			jIn.displayName = thdata->jsonScene_.name;
			struct SJsonOutData
			{
				std::string displayName;
				std::string id;
				std::string iTwinId;
			};
			struct SJsonOut
			{
				SJsonOutData scene;
			};
			AsyncPatchJsonJBody<SJsonOut>(GetHttp(), callbackPtr,
				[this](long httpCode, const Tools::TSharedLockableData<SJsonOut>& /*joutPtr*/)
			{
				auto thdata = thdata_.GetAutoLock();
				const bool bSuccess = (httpCode == 200);
				if (bSuccess)
				{
					BE_LOGI("ITwinScene", "Saved scene in Scene API with ID " << thdata->GetDBIdentifier()
						<< " itwin " << thdata->jsonScene_.itwinid);
					thdata->OnSaved();
				}
				else
				{
					BE_LOGW("ITwinScene", "Failed saving scene with ID " << thdata->GetDBIdentifier()
						<< " in Scene API from itwin " << thdata->jsonScene_.itwinid
						<< " Http status: " << httpCode);
				}
				return bSuccess;
			},
				"scenes/" + thdata->GetDBIdentifier() + "?iTwinId=" + thdata->jsonScene_.itwinid,
				jIn);
		}

		bool Get(const std::string& itwinid, const std::string& id)
		{
			auto thdata = thdata_.GetAutoLock();
			struct SJsonOutData
			{
				std::string displayName;
				std::string id;
				std::string iTwinId;
				std::optional<std::string> lastModified;
			};
			struct SJsonOut { SJsonOutData scene; };
			SJsonOut jOut;
			long status = GetHttp()->GetJson(jOut, "scenes/" + id + "?iTwinId=" + itwinid);
			if (status == 200)
			{
				thdata->jsonScene_.itwinid = jOut.scene.iTwinId;
				thdata->jsonScene_.name = jOut.scene.displayName;
				thdata->SetDBIdentifier(jOut.scene.id);
				if (jOut.scene.lastModified)
					thdata->jsonScene_.lastModified = *jOut.scene.lastModified;
				BE_LOGI("ITwinScene", "Loaded Scene in Scene API with ID " << thdata->GetDBIdentifier() << " from itwin "<< itwinid);
				return true;
			}
			else
			{
				BE_LOGW("ITwinScene", "Load scene in Scene API failed. Http status: " << status);
				return false;
			}
		}

		void AsyncGet(const std::string& itwinid, const std::string& id,
			std::function<void(expected<void, std::string> const&)> onFinishCallback)
		{
			auto SThis = shared_from_this();
			struct SJsonOutData {
				std::string displayName;	std::string id; std::string iTwinId; std::optional<std::string> lastModified;
			};
			struct SJsonOut { SJsonOutData scene; };
			typedef Tools::TSharedLockableDataPtr<SJsonOut> TJsonOutPtr;
			TJsonOutPtr jOut = MakeSharedLockableDataPtr<SJsonOut>(new SJsonOut());
			GetHttp()->AsyncGetJson(jOut, 
				[SThis, onFinishCallback](const Http::Response &,expected<TJsonOutPtr, std::string>& exp1) {
					expected<void, std::string> exp;
					if (exp1)
					{
						{
							auto jOut = (*exp1)->GetAutoLock();
							auto thdata = SThis->thdata_.GetAutoLock();
							thdata->jsonScene_.itwinid = jOut->scene.iTwinId;
							thdata->jsonScene_.name = jOut->scene.displayName;
							thdata->SetDBIdentifier(jOut->scene.id);
							if (jOut->scene.lastModified)
								thdata->jsonScene_.lastModified = *jOut->scene.lastModified;
						}
						onFinishCallback(exp);
					}
					else
					{
						exp = make_unexpected(exp1.error());
						onFinishCallback(exp);
					}
				},
				"scenes/" + id + "?iTwinId=" + itwinid);
		}

		/// Load links from server (synchronous version).
		void LoadLinks();

		/// Load links from server (asynchronous version).
		void AsyncLoadLinks(std::function<void(expected<void, std::string> const&)> inCallback);

		bool Delete()
		{
			auto thdata = thdata_.GetAutoLock();
			if (!thdata->HasDBIdentifier())
			{
				BE_LOGE("ITwinScene", "Cannot delete scene with no ID!"
					<< " (from itwin " << thdata->jsonScene_.itwinid << ")");
				return false;
			}
			std::string s("scenes/" + thdata->GetDBIdentifier() + "?iTwinId=" + thdata->jsonScene_.itwinid);
			auto status = GetHttp()->Delete(s, {});
			if (status.first != 200 && status.first != 204)
			{
				BE_LOGW("ITwinScene", "Delete scene in Scene API failed. Http status: " << status.first);
				return false;
			}
			else
			{
				BE_LOGI("ITwinScene", "Deleted scene in Scene API with ID " << thdata->GetDBIdentifier());
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

	private:
		bool StartLoadingLinks(std::function<void(expected<void, std::string> const&)> const& callback);
		expected<void, HttpError> HandleLoadLinksResponse(std::vector<SceneAPIDetails::JsonObjectWithId> const& objects);
		void OnLoadLinksFinished(expected<void, HttpError> const& exp);

		struct SLinkData
		{
			std::string ref;
			std::vector<double> adjusts;
			std::string id;
		};
		/// For multi-page loading of links
		struct SLoadedLinksData
		{
			bool isLoading = false;

			bool atmosphereFound = false;
			bool sceneSettingsFound = false;
			bool timelineFound = false;
			std::vector<SLinkData> sublinks;
			std::vector<std::string> animations;
			std::map<std::string, SceneAPIDetails::SJsonFrameCameraData> cameraDatas;

			std::function<void(expected<void, std::string> const&)> onFinished;

			// Only one request can should be active at the same time: if we request a refresh while the
			// previous request is running, we'll just restart a request when the current one completes.
			bool shouldRelaunchRequest = false;
			std::function<void(expected<void, std::string> const&)> onFinished_NextRequest;

			bool StartLoading(std::function<void(expected<void, std::string> const&)> const& onFinishedCallback)
			{
				if (isLoading && onFinishedCallback)
				{
					// Previous asynchronous request still in progress, and we should not mix them...
					shouldRelaunchRequest = true;
					onFinished_NextRequest = onFinishedCallback;
					return false;
				}
				BE_ASSERT(!isLoading, "multi-page loading of links already in progress!");
				isLoading = true;
				shouldRelaunchRequest = false;
				onFinished = onFinishedCallback;

				atmosphereFound = false;
				sceneSettingsFound = false;
				timelineFound = false;
				sublinks.clear();
				animations.clear();
				cameraDatas.clear();
				return true;
			}
			void OnLoadingFinished() {
				BE_ASSERT(isLoading, "multi-page loading of links not started!");
				isLoading = false;
			}
		};
		Tools::RWLockableObject<SLoadedLinksData> thLoadedLinksData_;
	};

	bool ScenePersistenceAPI::Impl::StartLoadingLinks(std::function<void(expected<void, std::string> const&)> const& callback)
	{
		auto thLoadedLinksData = thLoadedLinksData_.GetAutoLock();
		return thLoadedLinksData->StartLoading(callback);
	}

	expected<void, HttpError> ScenePersistenceAPI::Impl::HandleLoadLinksResponse(
		std::vector<SceneAPIDetails::JsonObjectWithId> const& objects)
	{
		using namespace SceneAPIDetails;

		auto thLoadedLinksData = thLoadedLinksData_.GetAutoLock();
		if (!thLoadedLinksData->isLoading)
		{
			BE_ISSUE("internal error: loading links has not started");
			return make_unexpected(HttpError{
				.message = "Internal error while loading links",
				.httpCode = -501
				});
		}

		for (auto const& row : objects)
		{
			std::shared_ptr<LinkAPI> link(LinkAPI::New());
			link->GetImpl().SetDBIdentifier(row.id);
			if (row.kind == "RepositoryResource")
			{
				std::string& linkType = link->GetImpl().link_.type;
				std::optional<std::string> ttype;
				if (row.data)
				{
					ttype = row.data->type.get();
				}
				if (ttype.has_value())
				{
					if (*ttype == "iModels")
						linkType = "iModel";
					else if (*ttype == "RealityData")
						linkType = "RealityData";
				}
				link->SetType(linkType.empty() ? "iModel" : linkType); // check row class
				if (row.visible)
				{
					link->GetImpl().link_.visibility = *row.visible;
				}
				else if (row.data && row.data->visible)
					link->GetImpl().link_.visibility = *row.data->visible;
				if (row.data && row.data->id)
					link->GetImpl().link_.ref = *row.data->id;

				AddLink(link);
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
				AddLink(link);
			}
			else if (row.kind == "UnrealAtmosphericStyling")
			{
				link->GetImpl().link_.type = "atmosphere";
				if (row.data && row.data->atmosphere)
				{
					auto thdata = thdata_.GetAutoLock();
					thdata->jsonAtmo_ = *row.data->atmosphere;
				}
				AddLink(link);

				thLoadedLinksData->atmosphereFound = true;
			}
			else if (row.kind == "GoogleTilesStyling")
			{
				link->GetImpl().link_.type = "SceneSettings"; // check row class
				if (row.data && row.data->quality)
				{
					auto thdata = thdata_.GetAutoLock();
					thdata->jsonSS_.qualityGoogleTiles = *row.data->quality * 100;
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
						auto thdata = thdata_.GetAutoLock();
						if (sldata[qualityId] < -1e-7)
						{
							thdata->jsonSS_.qualityGoogleTiles = -sldata[qualityId];
							thdata->jsonSS_.displayGoogleTiles = false;
						}
						else
						{
							thdata->jsonSS_.qualityGoogleTiles = sldata[qualityId];
							thdata->jsonSS_.displayGoogleTiles = true;
						}
					}
					if (geolocid >= 0)
					{
						auto thdata = thdata_.GetAutoLock();
						thdata->jsonSS_.geoLocation = std::array<double, 3>();
						(*thdata->jsonSS_.geoLocation)[0] = sldata[geolocid];
						(*thdata->jsonSS_.geoLocation)[1] = sldata[geolocid + 1];
						(*thdata->jsonSS_.geoLocation)[2] = sldata[geolocid + 2];
					}
				}
				AddLink(link);
				thLoadedLinksData->sceneSettingsFound = true;
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
					AddLink(link);
				}
			}
			else if (row.kind == "iModelVisibility")
			{
				if (row.displayName && *row.displayName != "adjustment")
				{
					continue;
				}
				else if (row.data && row.data->id && row.data->adjustment)
				{
					SLinkData sl;
					sl.id = row.id;
					sl.ref = *row.data->id;
					sl.adjusts = *row.data->adjustment;
					thLoadedLinksData->sublinks.push_back(sl);
				}
				else if (row.relatedId && row.data && row.data->adjustment)
				{
					SLinkData sl;
					sl.id = row.id;
					sl.ref = *row.relatedId;
					sl.adjusts = *row.data->adjustment;
					thLoadedLinksData->sublinks.push_back(sl);
				}
			}
			else if (row.kind == "Movie")
			{
				link->GetImpl().link_.type = "timeline";
				if (row.data && row.data->animations)
				{
					thLoadedLinksData->animations = *row.data->animations;
				}
				AddLink(link);
				thLoadedLinksData->timelineFound = true;
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
					thLoadedLinksData->cameraDatas[row.id] = data;
				}
			}
		}
		return {};
	}

	void ScenePersistenceAPI::Impl::OnLoadLinksFinished(expected<void, HttpError> const& exp)
	{
		auto thLoadedLinksData = thLoadedLinksData_.GetAutoLock();
		if (!thLoadedLinksData->isLoading)
		{
			BE_ISSUE("internal error: loading links has not started");
			return;
		}

		if (exp)
		{
			{
				auto thdata = thdata_.GetRAutoLock();
				for (auto sldata : thLoadedLinksData->sublinks)
				{
					for (auto mainlink : thdata->links_)
					{
						if (sldata.ref == mainlink->GetImpl().GetDBIdentifier())
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
				BE_LOGI("ITwinScene", "Found " << thdata->links_.size() << " Link(s) for scene " << thdata->GetDBIdentifier());
			}

			bool atmosphereFound = thLoadedLinksData->atmosphereFound;
			if (!atmosphereFound)
			{
				{
					auto thdata = thdata_.GetRAutoLock();
					for (auto link : thdata->links_)
					{
						if (link->GetImpl().link_.type == "atmosphere")
						{
							atmosphereFound = true;
							break;
						}
					}
				}
				if (!atmosphereFound)
				{
					std::shared_ptr<AdvViz::SDK::LinkAPI> link(AdvViz::SDK::LinkAPI::New());
					link->GetImpl().link_.type = "atmosphere";
					AddLink(link);
					{
						auto thdata = thdata_.GetAutoLock();
						thdata->jsonAtmo_.heliodonDate = "2024-06-16T18:00:00Z";
						thdata->jsonAtmo_.useHeliodon = true;
						thdata->jsonAtmo_.heliodonLongitude = -77.90736389160156;
						thdata->jsonAtmo_.heliodonLatitude = 35.857818603515625;
						thdata->jsonAtmo_.sunPitch = -10;
					}
				}
			}

			bool sceneSettingsFound = thLoadedLinksData->sceneSettingsFound;
			if (!sceneSettingsFound)
			{
				{
					auto thdata = thdata_.GetRAutoLock();
					for (auto link : thdata->links_)
					{
						if (link->GetImpl().link_.type == "SceneSettings")
						{
							sceneSettingsFound = true;
							break;
						}
					}
				}
				if (!sceneSettingsFound)
				{
					std::shared_ptr<AdvViz::SDK::LinkAPI> link(AdvViz::SDK::LinkAPI::New());
					link->GetImpl().link_.type = "SceneSettings";
					AddLink(link);
					{
						auto thdata = thdata_.GetAutoLock();
						thdata->jsonSS_.displayGoogleTiles = true;
						thdata->jsonSS_.qualityGoogleTiles = 0.3;
					}
				}
			}

			bool timelineFound = thLoadedLinksData->timelineFound;
			if (!timelineFound)
			{
				{
					auto thdata = thdata_.GetRAutoLock();
					for (auto link : thdata->links_)
					{
						if (link->GetImpl().link_.type == "timeline")
						{
							timelineFound = true;
							break;
						}
					}
				}
				if (!timelineFound)
				{
					std::shared_ptr<AdvViz::SDK::LinkAPI> link(AdvViz::SDK::LinkAPI::New());
					link->GetImpl().link_.type = "timeline";
					AddLink(link);
				}
			}
			else //build timeline
			{
				auto tl = timeline_;
				if (!tl)
				{
					timeline_ = std::shared_ptr<AdvViz::SDK::ITimeline>(AdvViz::SDK::ITimeline::New());
					tl = timeline_;
				}

				for (auto id : thLoadedLinksData->animations)
				{
					auto scam = thLoadedLinksData->cameraDatas.find(id);
					if (scam != thLoadedLinksData->cameraDatas.end())
					{
						std::string name;
						if (scam->second.name)
							name = *scam->second.name;
						else
							name = "unamed clip " + std::to_string(tl->GetClipCount());
						std::shared_ptr<ITimelineClip> clipp;
						for (size_t i(0); i < tl->GetClipCount(); i++)
						{
							auto clip = tl->GetClipByIndex(i);
							if (!clip)
								continue;
							if ((*clip)->GetDBIdentifier() == scam->first)
							{
								clipp = *clip;
								break;
							}
						}
						if (!clipp)
							clipp = tl->AddClip(name);
						clipp->SetDBIdentifier(scam->first);
						auto maxid = std::min(scam->second.input.size(), scam->second.output.size());
						for (auto i = 0; i < maxid; ++i)
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
								kd.atmo->sunPitch = output.settings->atmosphere.sunPitch;
								kd.atmo->sunAzimuth = output.settings->atmosphere.sunAzimuth;
								if (output.settings->atmosphere.sunIntensity)
									kd.atmo->sunIntensity = *output.settings->atmosphere.sunIntensity;
								if (output.settings->atmosphere.HDRIZRotation)
									kd.atmo->HDRIZRotation = *output.settings->atmosphere.HDRIZRotation;
								kd.atmo->heliodonLatitude = output.settings->atmosphere.heliodonLatitude;
								kd.atmo->heliodonLongitude = output.settings->atmosphere.heliodonLongitude;
								kd.atmo->useHeliodon = output.settings->atmosphere.useHeliodon;
								if (output.settings->atmosphere.HDRIImage)
									kd.atmo->HDRIImage = *output.settings->atmosphere.HDRIImage;
								kd.atmo->exposure = output.settings->atmosphere.exposure;

							}

							if (output.schedule)
							{
								std::chrono::sys_time<std::chrono::milliseconds> datetime{ std::chrono::milliseconds{ output.schedule->timePoint } };
								kd.synchro = ITimelineKeyframe::SynchroData();
								kd.synchro->date = std::format("{:%FT%T}Z", datetime);
								kd.synchro->scheduleId = output.schedule->timelineId;
							}
							else if (output.settings)
							{
								kd.synchro = ITimelineKeyframe::SynchroData();
								kd.synchro->date = kd.atmo->time;
							}
							kd.camera = ITimelineKeyframe::CameraData();
							if (output.camera.ecefTransform && output.camera.ecefTransform->size() >= 12)
							{
								std::memcpy(kd.camera->transform.data(), output.camera.ecefTransform->data(), 12 * sizeof(double));
							}
							else
							{
								kd.camera->isPause = true;
							}
							auto kfResult = clipp->AddKeyframe(kd);
							if (kfResult)
							{
								(*kfResult)->SetShouldSave(false);
							}
						}
						clipp->SetShouldSave(false);
					}
				}
				// A timeline just loaded is up-to-date.
				tl->SetShouldSave(false);
			}

			if (thLoadedLinksData->onFinished)
			{
				thLoadedLinksData->onFinished({});
			}
		}
		else
		{
			BE_LOGW("ITwinScene", "Load Links error: " << exp.error().message
				<< " (status: " << exp.error().httpCode << ")");

			if (thLoadedLinksData->onFinished)
			{
				thLoadedLinksData->onFinished(make_unexpected(exp.error().message));
			}
		}
		thLoadedLinksData->OnLoadingFinished();

		// Start a new request if needed (ie. if we requested a new refresh while another one was running).
		if (thLoadedLinksData->shouldRelaunchRequest)
		{
			AsyncLoadLinks(thLoadedLinksData->onFinished_NextRequest);
			thLoadedLinksData->shouldRelaunchRequest = false;
			thLoadedLinksData->onFinished_NextRequest = {};
		}
	}

	void ScenePersistenceAPI::Impl::LoadLinks()
	{
		using namespace SceneAPIDetails;

		std::shared_ptr<Http> const& http = GetHttp();
		if (!http)
		{
			return;
		}
		auto thdata = thdata_.GetRAutoLock();
		if (!thdata->HasDBIdentifier())
		{
			BE_ISSUE("missing DB identifier - cannot fetch links");
			return;
		}
		// Synchronous, with multi-page support
		StartLoadingLinks({});
		auto exp = ITwinAPI::GetPagedDataVector<"objects", JsonObjectWithId>(http,
			"scenes/" + thdata->GetDBIdentifier() + "/objects?iTwinId=" + thdata->jsonScene_.itwinid,
			{} /*headers*/,
			[this](std::vector<JsonObjectWithId> const& objects) { return HandleLoadLinksResponse(objects); }
		);
		OnLoadLinksFinished(exp);
	}

	void ScenePersistenceAPI::Impl::AsyncLoadLinks(std::function<void(expected<void, std::string> const&)> inCallback)
	{
		using namespace SceneAPIDetails;

		std::shared_ptr<Http> const& http = GetHttp();
		if (!http)
		{
			if (inCallback)
			{
				inCallback(make_unexpected("No Http support to retrieve links"));
			}
			return;
		}
		auto thdata = thdata_.GetRAutoLock();
		if (!thdata->HasDBIdentifier())
		{
			BE_ISSUE("missing DB identifier - cannot fetch links");
			if (inCallback)
			{
				inCallback(make_unexpected("Unknown scene ID - cannot retrieve links"));
			}
			return;
		}
		auto SThis = shared_from_this();

		// Synchronous, with multi-page support
		if (!StartLoadingLinks(inCallback))
		{
			// Another request is in progress => we will start a ne one when it is completed.
			return;
		}
		ITwinAPI::AsyncGetPagedDataVector<"objects", JsonObjectWithId>(http,
			"scenes/" + thdata->GetDBIdentifier() + "/objects?iTwinId=" + thdata->jsonScene_.itwinid,
			{} /*headers*/,
			[SThis](std::vector<SceneAPIDetails::JsonObjectWithId> const& objects) ->expected<void, HttpError>
		{
			return SThis->HandleLoadLinksResponse(objects);
		},
			[SThis](expected<void, HttpError> const& exp)
		{
			SThis->OnLoadLinksFinished(exp);
		});
	}

	static std::string DSIDtoSceneAPIGuid(const std::string& dsi)
	{
		if (dsi.size() < 24)
		{
			BE_ISSUE("Unexpected size from DSId ", dsi);
			return "";
		}
		return dsi.substr(0, 8) + "-" + dsi.substr(8, 4) + "-" + dsi.substr(12, 4) + "-" + dsi.substr(16, 4) + "-" + dsi.substr(20, 4) + "00000000";
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

	void ScenePersistenceAPI::AsyncCreate(const std::string& name, const std::string& itwinid,
		std::function<void(bool)>&& onCreationDoneFunc /*= {}*/)
	{
		GetImpl().AsyncCreate(name, itwinid, std::move(onCreationDoneFunc), false);
	}

	bool ScenePersistenceAPI::Get(const std::string& itwinId, const std::string& id)
	{
		bool res = GetImpl().Get(itwinId, id);
		LoadLinks();
		return res;
	}

	void ScenePersistenceAPI::AsyncGet(const std::string& itwinid, const std::string& id,
		std::function<void(expected<void, std::string> const&)> onFinish)
	{
		auto implPtr = GetImpl().shared_from_this();
		GetImpl().AsyncGet(itwinid, id,
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
	
	bool ScenePersistenceAPI::Delete()
	{
		return GetImpl().Delete();
	}

	const std::string& ScenePersistenceAPI::GetId() const
	{
		auto thdata = GetImpl().thdata_.GetRAutoLock();
		return thdata->GetDBIdentifier();
	}

	const std::string& ScenePersistenceAPI::GetName() const
	{
		auto thdata = GetImpl().thdata_.GetRAutoLock();
		return thdata->jsonScene_.name;
	}

	std::string ScenePersistenceAPI::GetLastModified() const
	{
		auto thdata = GetImpl().thdata_.GetRAutoLock();
		return thdata->jsonScene_.lastModified;
	}


	const std::string& ScenePersistenceAPI::GetITwinId() const
	{
		auto thdata = GetImpl().thdata_.GetRAutoLock();
		return thdata->jsonScene_.itwinid;
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
		auto thdata = GetImpl().thdata_.GetAutoLock();
		auto& jsonatmo = thdata->jsonAtmo_;
		jsonatmo.sunAzimuth = std::fmod(atmo.sunAzimuth,360);
		jsonatmo.sunPitch = std::fmod(atmo.sunPitch, 360);
		if(jsonatmo.sunPitch >180)
			jsonatmo.sunPitch-=360;
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
		jsonatmo.HDRIZRotation = atmo.HDRIZRotation;
		jsonatmo.sunIntensity = atmo.sunIntensity;
		//wait until scene API support those values 
		thdata->InvalidateDB();
	}

	AdvViz::SDK::ITwinAtmosphereSettings ScenePersistenceAPI::GetAtmosphere() const
	{
		auto thdata = GetImpl().thdata_.GetRAutoLock();
		AdvViz::SDK::ITwinAtmosphereSettings atmo;
		const auto& jsonatmo = thdata->jsonAtmo_;
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

	void ScenePersistenceAPI::SetSceneSettings(const ITwinSceneSettings& ss)
	{
		auto thdata = GetImpl().thdata_.GetAutoLock();
		auto& jsonss = thdata->jsonSS_;
		jsonss.displayGoogleTiles = ss.displayGoogleTiles;
		jsonss.qualityGoogleTiles = ss.qualityGoogleTiles;
		jsonss.geoLocation = ss.geoLocation;
		thdata->InvalidateDB();
	}

	AdvViz::SDK::ITwinSceneSettings ScenePersistenceAPI::GetSceneSettings() const
	{
		auto thdata = GetImpl().thdata_.GetRAutoLock();
		const auto& jsonss = thdata->jsonSS_;
		AdvViz::SDK::ITwinSceneSettings ss;
		ss.displayGoogleTiles = jsonss.displayGoogleTiles;
		ss.qualityGoogleTiles = jsonss.qualityGoogleTiles;
		ss.geoLocation = jsonss.geoLocation;
		return ss;
	}

	bool ScenePersistenceAPI::ShouldSave() const
	{
		auto thdata = GetImpl().thdata_.GetRAutoLock();
		if (thdata->ShouldSave())
			return true;
		for (auto link : thdata->links_)
		{
			if (link->ShouldSave())
				return true;
		}
		return false;
	}

	void ScenePersistenceAPI::AsyncSave(std::function<void(bool)>&& onDataSavedFunc /*= {}*/)
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

				callbackPtr->OnFirstLevelRequestsRegistered();
			}
		};

		bool hasDBId(false);
		std::string name;
		std::string itwinid;
		{
			auto thdata = GetImpl().thdata_.GetRAutoLock();
			hasDBId = thdata->HasDBIdentifier();
			name = thdata->jsonScene_.name;
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

	void ScenePersistenceAPI::SetShouldSave(bool shouldSave) const
	{
		auto thdata = GetImpl().thdata_.GetAutoLock();
		thdata->SetShouldSave(shouldSave);
	}

	void ScenePersistenceAPI::PrepareCreation(const std::string& name, const std::string& itwinid)
	{
		auto thdata = GetImpl().thdata_.GetAutoLock();
		thdata->jsonScene_.name = name;
		thdata->jsonScene_.itwinid = itwinid;
		ITwinAtmosphereSettings defaultAtmo;
		defaultAtmo.heliodonDate = "2024-06-16T18:00:00Z";
		defaultAtmo.useHeliodon = true;
		defaultAtmo.heliodonLongitude = -77.90736389160156;
		defaultAtmo.heliodonLatitude= 35.857818603515625;
		SetAtmosphere(defaultAtmo);
		SetSceneSettings(ITwinSceneSettings());
		SetShouldSave(false);
	}

	std::vector<std::shared_ptr<AdvViz::SDK::ILink>> ScenePersistenceAPI::GetLinks() const
	{
		std::vector<std::shared_ptr<AdvViz::SDK::ILink>> res;
		auto thdata = GetImpl().thdata_.GetRAutoLock();
		for (auto l : thdata->links_)
			res.push_back(l);
		return res;
	}


	void ScenePersistenceAPI::LoadLinks()
	{
		GetImpl().LoadLinks();
	}

	void ScenePersistenceAPI::AddLink(std::shared_ptr<ILink> v)
	{
		auto rv = std::dynamic_pointer_cast<LinkAPI>(v);

		GetImpl().AddLink(rv);

		auto thdata = GetImpl().thdata_.GetAutoLock();
		thdata->InvalidateDB();
	}


	void ScenePersistenceAPI::OnLinkSaved(LinkAPIPtr link, std::string const& idOnServer)
	{
		RefID linkId = link->GetId();
		linkId.SetDBIdentifier(idOnServer);
		if (link->GetSaveStatus() == ESaveStatus::InProgress)
		{
			// Update link ID now that it contains the DB identifier.
			link->SetId(linkId);
			BE_LOGI("ITwinScene", "Add Link for scene " << GetId() << " new link " << (*link));
			if (link->GetImpl().parentLink_)
			{
				link->GetImpl().parentLink_->GetImpl().sublinkId_ = linkId.GetDBIdentifier();
			}
			link->OnSaved();
		}
	}

	void ScenePersistenceAPI::AsyncSaveLinksVec(LinkAPIPtrVec links,
		[[maybe_unused]] bool bSubLink,
		std::shared_ptr<AsyncRequestGroupCallback> callbackPtr_global,
		std::function<void(bool)>&& onLinksSavedFunc)
	{
		std::shared_ptr<Http> const& http = GetImpl().GetHttp();

		// This whole set of requests will count for one
		callbackPtr_global->AddRequestToWait();

		std::function<void(bool)> onLinksVecSavedFunc =
			[onLinksSavedFunc = std::move(onLinksSavedFunc),
			 callbackPtr_global](bool bSuccess)
		{
			if (!callbackPtr_global->IsValid())
				return;

			if (onLinksSavedFunc)
				onLinksSavedFunc(bSuccess);

			callbackPtr_global->OnRequestDone(bSuccess);
		};

		// Use a distinct callback to handle the termination of the saving of this individual vector of
		// links.
		std::shared_ptr<AsyncRequestGroupCallback> callbackPtr =
			std::make_shared<AsyncRequestGroupCallback>(
				std::move(onLinksVecSavedFunc), GetImpl().isThisValid_);

		auto thdata = GetImpl().thdata_.GetRAutoLock();
		const std::string sceneId = thdata->GetDBIdentifier();
		const std::string itwinid = thdata->jsonScene_.itwinid;

		for (auto link : links)
		{
			// RealityData and iModel links are saved separately, in Itwin Engage TS. We don't want to save them here as well.
			if (!enableExportOfResources &&(link->GetType() == "RealityData" || link->GetType() == "iModel"))
			{
				continue;
			}
			RefID const linkId = link->GetId();
			BE_ASSERT(bSubLink || !link->GetImpl().parentLink_);
			if (!linkId.HasDBIdentifier() && !link->ShouldDelete())
			{
				// New link - use POST request
				link->OnStartSave();

				struct SJsonO2
				{
					std::string id;
				};

				if (link->GetImpl().parentLink_)
				{
					if (!link->GetImpl().parentLink_->HasDBIdentifier())
					{
						continue; //parent post failed, do not post child
					}
					link->SetRef(link->GetImpl().parentLink_->GetDBIdentifier());
				}
				const Http::BodyParams bodyParams(GenerateBody(link, false));

				struct SJsonOut
				{
					std::vector<SJsonO2> objects;
				};
				AsyncPostJson<SJsonOut>(http, callbackPtr,
					[this, link, callbackPtr, sceneId, itwinid](long httpCode, const Tools::TSharedLockableData<SJsonOut>& joutPtr)
				{
					auto thdata = GetImpl().thdata_.GetRAutoLock();
					auto unlockedJout = joutPtr->GetAutoLock();
					SJsonOut const& jOut = unlockedJout.Get();

					bool bSuccess = (httpCode == 200 || httpCode == 201) && jOut.objects.size() == 1;
					if (bSuccess)
					{
						OnLinkSaved(link, jOut.objects[0].id);
					}
					else
					{
						BE_LOGW("ITwinScene", "Add Link for scene " << sceneId << " failed. Http status: " << httpCode
							<< " with link " << (*link));
					}
					return bSuccess;
				},
					"scenes/" + sceneId + "/objects?iTwinId=" + itwinid,
					bodyParams);
			}
			else if (link->ShouldDelete() && linkId.HasDBIdentifier())
			{
				if (link->GetImpl().parentLink_)
				{
					if (link->GetImpl().parentLink_->HasDBIdentifier())
					{
						continue; //parent delete failed, do not delete child
					}
				}
				Impl::SJsonInEmpty Jin;
				std::string url("scenes/" + sceneId + "/objects/" + linkId.GetDBIdentifier() + "?iTwinId=" + itwinid);
				AsyncDeleteJsonNoOutput(http, callbackPtr,
					[this, link, sceneId](long httpCode)
				{
					RefID linkId = link->GetId();
					const bool bSuccess = (httpCode == 200 || httpCode == 204 /* No-Content*/);
					if (bSuccess)
					{
						BE_LOGI("ITwinScene", "Deleted Link with scene ID " << sceneId << " link " << (*link));
						linkId.SetDBIdentifier(""); // reset DB identifier to notify deletion
						link->SetId(linkId);
					}
					else
					{
						BE_LOGW("ITwinScene", "Delete Link failed. Http status: " << httpCode
							<< " scene id: " << sceneId << " link " << (*link));
					}
					return bSuccess;
				},
					url, Jin);
			}
			else if (linkId.HasDBIdentifier() && !link->ShouldDelete())
			{
				// Update link existing on the server (using PATCH request).
				// Note that we don't test ShouldSave here, to avoid missing the saving of "dummy" links.

				link->OnStartSave();

				struct SJsonO2
				{
					std::string id;
				};
				struct SJsonOut
				{
					SJsonO2 object;
				};
				SJsonOut jOut;
				if (link->GetImpl().parentLink_
					&& link->GetImpl().parentLink_->HasDBIdentifier())
				{
					link->SetRef(link->GetImpl().parentLink_->GetDBIdentifier());
				}
				const Http::BodyParams bodyParams(GenerateBody(link, true));
				AsyncPatchJson<SJsonOut>(http, callbackPtr,
					[this, link, callbackPtr, sceneId, itwinid](long httpCode, const Tools::TSharedLockableData<SJsonOut>& /*joutPtr*/)
				{
					RefID linkId = link->GetId();
					bool bSuccess = (httpCode == 200);
					if (bSuccess)
					{
						BE_LOGI("ITwinScene", "Update Link for scene " << sceneId
							<< " with link " << (*link));
						link->OnSaved();
					}
					else
					{
						BE_LOGW("ITwinScene", "Update Link for scene " << sceneId << " failed. Http status: " << httpCode
							<< " with link " << (*link));
					}
					return bSuccess;
				},
					"scenes/" + sceneId + "/objects/" + linkId.GetDBIdentifier() + "?iTwinId=" + itwinid,
					bodyParams);
			}
			//else nothing to do, id is empty so the link is not on the server
		}

		callbackPtr->OnFirstLevelRequestsRegistered();
	}

	void ScenePersistenceAPI::AsyncSaveLinks(std::shared_ptr<AsyncRequestGroupCallback> callbackPtr)
	{
		auto sublinks = GenerateSubLinks();
		auto prelinks = GeneratePreLinks();

		// The order matters: pre-links must be saved before main links, which themselves must be saved
		// before sub-links!

		// 1. Pre-links (for timeline)
		AsyncSaveLinksVec(prelinks, false, callbackPtr,
			[prelinks, sublinks, this, callbackPtr](bool bSuccess)
		{
			if (!callbackPtr->IsValid())
				return;
			auto tl = GetTimeline();
			for (auto link : prelinks)
			{
				auto clipp = tl->GetClipByRefID(link->GetImpl().GetId());
				if (!clipp)
					continue;
				if (link->HasDBIdentifier())
				{
					clipp->SetDBIdentifier(link->GetDBIdentifier());
					if (!link->ShouldSave())
					{
						clipp->OnSaved();
						// For SceneAPI, keyframes are saved together with clips
						clipp->OnKeyframesSaved();
					}
				}
			}
			if (tl)
			{
				bool bDeleteSuccess = true;
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
					if (deletionFailed)
					{
						bDeleteSuccess = false;
					}
					else
					{
						tl->RemoveObsoleteClip(clipp);
					}
				}
				if (bSuccess && bDeleteSuccess)
				{
					tl->OnSaved();
				}
			}
		

			// 2. Main links (must be saved *after* pre-links).
			auto thdata = GetImpl().thdata_.GetRAutoLock();
			AsyncSaveLinksVec(thdata->links_, false, callbackPtr,
				[sublinks, this, callbackPtr](bool /*bSuccess*/)
			{
				if (!callbackPtr->IsValid())
					return;

				// Actually remove links which were deleted on the server
				auto thdata = GetImpl().thdata_.GetAutoLock();
				std::erase_if(thdata->links_, [](const std::shared_ptr<LinkAPI>& l)
				{
					return l->ShouldDelete() && !l->HasDBIdentifier();
				});

				// 3. Sub-links (must be saved *after* the main links).
				AsyncSaveLinksVec(sublinks, true, callbackPtr, {});
			});
		});
	}

	std::shared_ptr<AdvViz::SDK::ILink> ScenePersistenceAPI::MakeLink()
	{
		return 	std::shared_ptr<AdvViz::SDK::ILink>(LinkAPI::New());
	}

	template <typename T>
	std::string ToPostString(const T& t)
	{
		struct SJSONObjects
		{
			std::vector<T> objects;
		};
		SJSONObjects jobs;
		jobs.objects.push_back(t);
		return Json::ToString(jobs);
	}

	std::string ScenePersistenceAPI::GenerateBody(const std::shared_ptr<LinkAPI>& link, bool forPatch,bool ignoreTimelineID)
	{
		if (link->GetType() == "RealityData" || link->GetType() == "iModel")
		{
			std::string itwinid;
			{
				auto thdata = GetImpl().thdata_.GetRAutoLock();
				itwinid = thdata->jsonScene_.itwinid;
			}

			struct SJsonInData
			{
				std::string id;
				rfl::Rename<"class", std::string> type;
				std::string repositoryId;
				std::string iTwinId;
			};
			if (forPatch)
			{
				struct SJsonIn
				{
					SJsonInData data;
					std::optional<bool> visible;
				};
				SJsonIn Jin;
				if (link->HasVisibility())
				{
					Jin.visible = link->GetVisibility();
				}
				else
				{
					Jin.visible = true;
				}
				Jin.data.id = link->GetRef();
				Jin.data.iTwinId = itwinid;
				
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
					std::optional<bool> visible;
				};

				SJsonIn Jin;
				if (link->HasVisibility())
				{
					Jin.visible = link->GetVisibility();
				}
				else
				{
					Jin.visible = true;
				}
				Jin.data.id = link->GetRef();
				Jin.data.iTwinId = itwinid;
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
				
				return ToPostString(Jin);
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
				return ToPostString(Jin);
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
				{
					auto thdata = GetImpl().thdata_.GetRAutoLock();
					Jin.data.atmosphere = thdata->jsonAtmo_;
				}
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
				{
					auto thdata = GetImpl().thdata_.GetRAutoLock();
					Jin.data.atmosphere = thdata->jsonAtmo_;
				}
				return ToPostString(Jin);
			}
		}
		else if (link->GetType() == "SceneSettings")
		{
			struct SJsonInData
			{
				double quality;
				std::vector<double> adjustment;
			};
			auto thdata = GetImpl().thdata_.GetRAutoLock();
			SJsonInData inData;
			inData.quality = thdata->jsonSS_.qualityGoogleTiles / 100;
			double ql = thdata->jsonSS_.qualityGoogleTiles;
			if (fabs(ql) < 1e-6)
			{
				ql = 1e-6;
			}
			if (!thdata->jsonSS_.displayGoogleTiles)
			{
				ql = -ql;
			}
			inData.adjustment.push_back(ql);

			if (thdata->jsonSS_.geoLocation)
			{
				inData.adjustment.push_back((*thdata->jsonSS_.geoLocation)[0]);
				inData.adjustment.push_back((*thdata->jsonSS_.geoLocation)[1]);
				inData.adjustment.push_back((*thdata->jsonSS_.geoLocation)[2]);
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
				return ToPostString(Jin);
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
					if ((*clipp)->HasDBIdentifier())
						inData.animations.push_back((*clipp)->GetDBIdentifier());
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
				return ToPostString(Jin);
			}
		}
		else if (link->GetType() == "clip")
		{
			auto tl = GetTimeline();
			auto clipp = tl->GetClipByRefID(link->GetImpl().GetId());
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

			for (size_t idx(0); idx < clipp->GetKeyframeCount(); ++idx)
			{
				auto kf = clipp->GetKeyframeByIndex(idx);
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
					o.settings->atmosphere.heliodonLatitude = kdata.atmo->heliodonLatitude.get();
					o.settings->atmosphere.heliodonLongitude = kdata.atmo->heliodonLongitude.get();
					o.settings->atmosphere.sunAzimuth = kdata.atmo->sunAzimuth.get();
					o.settings->atmosphere.sunPitch = kdata.atmo->sunPitch.get();
					if(o.settings->atmosphere.sunPitch>180)
						o.settings->atmosphere.sunPitch -= 360;
					o.settings->atmosphere.HDRIZRotation = kdata.atmo->HDRIZRotation.get();
					o.settings->atmosphere.sunIntensity = kdata.atmo->sunIntensity.get();
					o.settings->atmosphere.HDRIImage = kdata.atmo->HDRIImage.get();
					o.settings->atmosphere.useHeliodon = kdata.atmo->useHeliodon.get();
					o.settings->atmosphere.exposure = kdata.atmo->exposure.get();
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
						if(ignoreTimelineID)
							o.schedule->timelineId ="0x30";
						else
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
				Jin.displayName = clipp->GetName();
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
				Jin.displayName = clipp->GetName();
				return ToPostString(Jin);
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
				return ToPostString(Jin);
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
				//std::optional<std::string> displayName;
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
				return ToPostString(Jin);
			}
		}
		BE_ISSUE("Unknown Link Type", link->GetType());
		return "";
	}

	ScenePersistenceAPI::LinkAPIPtrVec ScenePersistenceAPI::GenerateSubLinks()
	{
		auto thdata = GetImpl().thdata_.GetRAutoLock();
		LinkAPIPtrVec res;
		for (auto link : thdata->links_)
		{
			BE_ASSERT(!link->GetImpl().parentLink_, "main links should not have parents");

			//disable saving of adjustments if they are currently saved in Itwin Engage Typescript.
			if (enableExportOfResources && (link->GetType() == "RealityData" || link->GetType() == "iModel"))
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
					nulink->SetRef(link->GetDBIdentifier());
					nulink->SetShouldSave(link->ShouldSave());
					nulink->Delete(link->ShouldDelete());
					nulink->GetImpl().parentLink_ = link;
					nulink->GetImpl().SetDBIdentifier(link->GetImpl().sublinkId_);
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

	ScenePersistenceAPI::LinkAPIPtrVec ScenePersistenceAPI::GeneratePreLinks()
	{
		LinkAPIPtrVec res;
		auto timeline = GetTimeline();
		if (!timeline)
			return res;
		timeline->OnStartSave();
		for (size_t i(0); i < timeline->GetClipCount(); i++)
		{
			auto clipp = timeline->GetClipByIndex(i);
			if (!clipp)
				continue;
			(*clipp)->OnStartSave();
			// Here keyframes will be saved together with clips:
			(*clipp)->OnStartSaveKeyframes();
			std::shared_ptr<AdvViz::SDK::LinkAPI> nulink(AdvViz::SDK::LinkAPI::New());
			nulink->SetType("clip");
			nulink->SetName((*clipp)->GetName());
			nulink->SetId((*clipp)->GetId());
			nulink->SetShouldSave(true);
			res.push_back(nulink);
		}
		for (auto clipp : timeline->GetObsoleteClips())
		{
			std::shared_ptr<AdvViz::SDK::LinkAPI> nulink(AdvViz::SDK::LinkAPI::New());
			nulink->SetType("clip");
			nulink->SetName(clipp->GetName());
			nulink->SetId(clipp->GetId());
			nulink->Delete(true);
			res.push_back(nulink);
		}
		return res;

	}

	void ScenePersistenceAPI::SetDefaultHttp(std::shared_ptr<Http> http)
	{
		creds.SetDefaultHttp(http);
	}

	std::string ScenePersistenceAPI::ExportHDRIAsJson(ITwinHDRISettings const& hdri) const
	{
		return GetImpl().ExportHDRIAsJson(hdri);
	}

	bool ScenePersistenceAPI::ConvertHDRIJsonFileToKeyValueMap(std::filesystem::path const& jsonPath, KeyValueStringMap& outMap) const {
		return GetImpl().ConvertHDRIJsonFileToKeyValueMap(jsonPath, outMap);
	}

	void ScenePersistenceAPI::AsyncRefreshLinks(std::function<void(expected<void, std::string> const&)> callback)
	{
		auto thdata = GetImpl().thdata_.GetRAutoLock();
		if (thdata->HasDBIdentifier())
		{
			GetImpl().AsyncLoadLinks(callback);
		}
		else if (callback)
		{
			callback(make_unexpected("No ID set"));
		}
	}

	void ScenePersistenceAPI::EnableExportOfResources(bool bEnable)
	{
		enableExportOfResources = bEnable;
	}

	namespace
	{
		struct JsonSceneWithId
		{
			std::string displayName;
			std::string iTwinId;
			std::string id;
		};
	}

	AdvViz::expected<ScenePtrVector, HttpError> GetITwinScenesAPI(
		const std::string& itwinid)
	{
		std::shared_ptr<Http> const& http = creds.GetHttp();
		if (!http)
		{
			return make_unexpected(HttpError{
				.message = "No Http support to retrieve scenes",
				.httpCode = -2});
		}
		ScenePtrVector scenes;
		auto ret = ITwinAPI::GetPagedData<"scenes", JsonSceneWithId>(http,
			"scenes?iTwinId=" + itwinid,
			{} /* extra headers*/,
			[&scenes, itwinid](JsonSceneWithId const& row) -> expected<void, HttpError>
		{
			std::shared_ptr<ScenePersistenceAPI> scene(ScenePersistenceAPI::New());
			if (scene->Get(itwinid, row.id))
			{
				scenes.push_back(scene);
			}
			return {};
		});
		if (!ret)
		{
			return make_unexpected(ret.error());
		}
		return scenes;
	}

	void AsyncGetITwinSceneInfosAPI(const std::string& itwinid,
		std::function<void(expected<SceneInfoVec, HttpError>)>&& inCallback,
		Http::EAsyncCallbackExecutionMode asyncCBExecMode /*= Default*/)
	{
		std::shared_ptr<Http> http = creds.GetHttp();
		if (!http)
		{
			inCallback(make_unexpected(HttpError{
					.message = "No Http support to retrieve scene infos",
					.httpCode = -2
				})
			);
			return;
		}

		std::shared_ptr<std::mutex> scenesMutex = std::make_shared<std::mutex>();
		std::shared_ptr<SceneInfoVec> sceneInfos = std::make_shared<SceneInfoVec>();

		ITwinAPI::AsyncGetPagedDataVector<"scenes", JsonSceneWithId>(http,
			"scenes?iTwinId=" + itwinid,
			{} /*headers*/,
			/* Receive-Vector callback */
			[scenesMutex, sceneInfos](std::vector<JsonSceneWithId> const& rows) -> expected<void, HttpError>
		{
			std::lock_guard<std::mutex> lock(*scenesMutex);

			sceneInfos->reserve(sceneInfos->size() + rows.size());
			for (JsonSceneWithId const& row : rows)
			{
				sceneInfos->emplace_back(SceneInfo{
					.id = row.id,
					.iTwinId = row.iTwinId,
					.displayName = row.displayName
				});
			}
			return {};
		},
			/* OnFinish callback */
			[itwinid, callback = std::move(inCallback), scenesMutex, sceneInfos](expected<void, HttpError> const& exp)
		{
			if (exp)
			{
				std::lock_guard<std::mutex> lock(*scenesMutex);
				BE_LOGI("ITwinScene", "[SceneAPI] Found " << sceneInfos->size() << " Scenes(s) for iTwin " << itwinid);
				callback(*sceneInfos);
			}
			else
			{
				BE_LOGW("ITwinScene", "Fetching scene infos from SceneAPI failed for iTwin " << itwinid
					<< ". " << exp.error().message
					<< " (status: " << exp.error().httpCode << ")");
				callback(make_unexpected(exp.error()));
			}
		},
			asyncCBExecMode);
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
		return GetImpl().link_.visibility.value_or(true);
	}

	double LinkAPI::GetQuality() const
	{
		return GetImpl().link_.quality.value_or(1.0);
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
		GetImpl().SetType(value);
	}

	void LinkAPI::SetRef(const std::string& value)
	{
		GetImpl().SetRef(value);
	}

	void LinkAPI::SetName(const std::string& value)
	{
		GetImpl().SetName(value);
	}

	void LinkAPI::SetGCS(const std::string& v1, const std::array<float, 3>& v2)
	{
		GetImpl().SetGCS(v1, v2);
	}

	void LinkAPI::SetVisibility(bool v)
	{
		GetImpl().SetVisibility(v);
	}

	void LinkAPI::SetQuality(double v)
	{
		GetImpl().SetQuality(v);
	}

	void LinkAPI::SetTransform(const dmat4x3& v)
	{
		GetImpl().SetTransform(v);
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


	const RefID& LinkAPI::GetId() const
	{
		return GetImpl().GetId();
	}
	void LinkAPI::SetId(const RefID& id)
	{
		GetImpl().SetId(id);
	}

	ESaveStatus LinkAPI::GetSaveStatus() const
	{
		return GetImpl().GetSaveStatus();
	}
	void LinkAPI::SetSaveStatus(ESaveStatus status)
	{
		GetImpl().SetSaveStatus(status);
	}

	void LinkAPI::Delete(bool value)
	{
		GetImpl().Delete(value);
	}

	bool LinkAPI::ShouldDelete() const
	{
		return GetImpl().shouldDelete_;
	}

	void SetSceneAPIConfig(const Config::SConfig& c)
	{
		creds.curstomServerConfig = c;
	}

}
