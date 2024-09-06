/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinWebServices.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinWebServices.h"

#include "ITwinWebServicesObserver.h"

#include <../BeHeaders/Compil/CleanUpGuard.h>

#include <fmt/format.h>

#include <Core/Json/Json.h>
#include <Core/Network/HttpRequest.h>

namespace SDK::Core
{
	inline std::string GetITwinAPIRootUrl(EITwinEnvironment Env)
	{
		return std::string("https://") + ITwinServerEnvironment::GetUrlPrefix(Env) + "api.bentley.com";
	}

	static ITwinWebServices* WorkingInstance = nullptr;

	struct [[nodiscard]] ScopedWorkingWebServices
	{
		ITwinWebServices* PreviousInstance = WorkingInstance;

		ScopedWorkingWebServices(ITwinWebServices* CurrentInstance)
		{
			WorkingInstance = CurrentInstance;
		}

		~ScopedWorkingWebServices()
		{
			WorkingInstance = PreviousInstance;
		}
	};


	/*static*/
	ITwinWebServices* ITwinWebServices::GetWorkingInstance()
	{
		return WorkingInstance;
	}

	/*static*/
	std::string ITwinWebServices::GetErrorDescription(
		ITwinError const& iTwinError,
		std::string const& indent)
	{
		ITwinErrorData const& errorData(iTwinError.error);
		if (errorData.code.empty() && errorData.message.empty())
		{
			return {};
		}

		// see https://developer.bentley.com/apis/issues-v1/operations/get-workflow/
		// (search "error-response" section)

		std::string const newLine = std::string("\n") + indent;
		std::string outError = newLine + fmt::format("Error [{}]: {}", errorData.code, errorData.message);

		if (errorData.details)
		{
			for (ITwinErrorDetails const& detailVal : *errorData.details)
			{
				std::string strDetail;
				if (!detailVal.code.empty())
				{
					strDetail += fmt::format("[{}] ", detailVal.code);
				}
				strDetail += detailVal.message;
				if (detailVal.target)
				{
					strDetail += fmt::format(" (target: {})", *detailVal.target);
				}
				if (!strDetail.empty())
				{
					outError += newLine + fmt::format("Details: {}", strDetail);
				}
			}
		}
		return outError;
	}

	/*static*/
	std::string ITwinWebServices::GetErrorDescriptionFromJson(
		std::string const& jsonContent,
		std::string const& indent)
	{
		// Try to parse iTwin error
		ITwinError iTwinError;
		if (Json::FromString(iTwinError, jsonContent))
		{
			return GetErrorDescription(iTwinError, indent);
		}
		else
		{
			return {};
		}
	}

	class ITwinWebServices::Impl
	{
		friend class ITwinWebServices;

		ITwinWebServices& owner_;

		using Mutex = std::recursive_mutex;
		using Lock = std::lock_guard<std::recursive_mutex>;
		mutable Mutex mutex_;
		std::shared_ptr< std::atomic_bool > isThisValid_; // same principle as in #FReusableJsonQueries::FImpl

		std::string authToken_;
		IITwinWebServicesObserver* observer_ = nullptr;

		struct LastError
		{
			std::string msg_;
			RequestID requestId_ = HttpRequest::NO_REQUEST;
		};
		LastError lastError_;

		std::string customServerURL_;

	public:
		Impl(ITwinWebServices& Owner)
			: owner_(Owner)
		{
			isThisValid_ = std::make_shared< std::atomic_bool >(true);
		}

		~Impl()
		{
			*isThisValid_ = false;
		}
	};


	ITwinWebServices::ITwinWebServices()
	{
		impl_ = std::make_unique<Impl>(*this);

		http_ = Http::New();
		http_->SetBaseUrl(GetAPIRootURL());
	}

	ITwinWebServices::~ITwinWebServices()
	{

	}

	template <typename Func>
	void ITwinWebServices::ModifyServerSetting(Func const& functor)
	{
		std::string const baseUrl_old = GetAPIRootURL();

		functor();

		std::string const baseUrl_new = GetAPIRootURL();

		// Update base URL if needed
		if (baseUrl_new != baseUrl_old)
		{
			http_->SetBaseUrl(baseUrl_new);
		}
	}

	void ITwinWebServices::SetEnvironment(EITwinEnvironment env)
	{
		ModifyServerSetting([this, env] { env_ = env; });
	}

	std::string ITwinWebServices::GetAuthToken() const
	{
		std::string authToken;
		Impl::Lock Lock(impl_->mutex_);
		authToken = impl_->authToken_;
		return authToken;
	}

	void ITwinWebServices::SetAuthToken(std::string const& token)
	{
		Impl::Lock Lock(impl_->mutex_);
		impl_->authToken_ = token;
	}

	void ITwinWebServices::SetObserver(IITwinWebServicesObserver* InObserver)
	{
		impl_->observer_ = InObserver;
	}

	bool ITwinWebServices::HasObserver(IITwinWebServicesObserver const* observer) const
	{
		return impl_->observer_ == observer;
	}

	void ITwinWebServices::SetLastError(std::string const& strError, RequestID requestID)
	{
		Impl::Lock Lock(impl_->mutex_);
		impl_->lastError_.msg_ = strError;
		impl_->lastError_.requestId_ = requestID;

		if (!strError.empty() && impl_->observer_)
		{
			impl_->observer_->OnRequestError(strError);
		}
	}

	std::string ITwinWebServices::GetLastError() const
	{
		Impl::Lock Lock(impl_->mutex_);
		return impl_->lastError_.msg_;
	}

	std::string ITwinWebServices::GetRequestError(RequestID requestID) const
	{
		Impl::Lock Lock(impl_->mutex_);
		if (impl_->lastError_.requestId_ == requestID)
			return impl_->lastError_.msg_;
		else
			return {};
	}

	bool ITwinWebServices::ConsumeLastError(std::string& outError)
	{
		Impl::Lock Lock(impl_->mutex_);
		outError = impl_->lastError_.msg_;
		impl_->lastError_ = {};
		return !outError.empty();
	}

	struct ITwinWebServices::ITwinAPIRequestInfo
	{
		const std::string ShortName; // short name used in errors, identifying the request easily
		const HttpRequest::EVerb Verb = HttpRequest::EVerb::Get;
		std::string UrlSuffix;
		const std::string AcceptHeader;

		const std::string ContentType;
		const std::string ContentString;

		std::map<std::string, std::string> CustomHeaders;

		// In some cases, we can determine in advance that the request is ill-formed (typically if a
		// mandatory ID is missing...).
		// In such case, we will not even try to run the http request.
		bool badlyFormed = false;

		bool HasCustomHeader(std::string const& headerKey) const
		{
			return CustomHeaders.find(headerKey) != CustomHeaders.end();
		}
	};

	void ITwinWebServices::SetCustomServerURL(std::string const& serverUrl)
	{
		ModifyServerSetting([this, &serverUrl] { impl_->customServerURL_ = serverUrl; });
	}

	std::string ITwinWebServices::GetAPIRootURL() const
	{
		if (!impl_->customServerURL_.empty())
		{
			// automation test is running: use mock server URL instead.
			return impl_->customServerURL_;
		}
		return GetITwinAPIRootUrl(this->env_);
	}

	template <typename ResultDataType, class FunctorType, class DelegateAsFunctor>
	ITwinWebServices::RequestID ITwinWebServices::TProcessHttpRequest(
		ITwinAPIRequestInfo const& requestInfo,
		FunctorType&& InFunctor,
		DelegateAsFunctor&& InResultFunctor)
	{
		if (requestInfo.badlyFormed)
		{
			// Some mandatory information was missing to build a valid url
			// => do not even try to process any request, but notify the error at once.
			this->SetLastError(
				fmt::format("[{}] insufficient parameters to build a valid request.", requestInfo.ShortName),
				HttpRequest::NO_REQUEST);
			InResultFunctor(false, {}, HttpRequest::NO_REQUEST);
			return HttpRequest::NO_REQUEST;
		}

		std::string const authToken = GetAuthToken();
		if (authToken.empty())
		{
			return HttpRequest::NO_REQUEST;
		}

		const auto request = HttpRequest::New();
		if (!request)
		{
			return HttpRequest::NO_REQUEST;
		}
		request->SetVerb(requestInfo.Verb);

		Http::Headers headers;
		headers.reserve(requestInfo.CustomHeaders.size() + 4);

		// Fill headers
		if (!requestInfo.HasCustomHeader("Prefer"))
		{
			headers.emplace_back("Prefer", "return=representation");
		}
		headers.emplace_back("Accept", requestInfo.AcceptHeader);
		if (!requestInfo.ContentType.empty())
		{
			// for "POST" requests typically
			headers.emplace_back("Content-Type", requestInfo.ContentType);
		}
		
		headers.emplace_back("Authorization", std::string("Bearer ") + authToken);

		// add custom headers, if any
		for (auto const& [key, value] : requestInfo.CustomHeaders) {
			headers.emplace_back(key, value);
		}

		using RequestPtr = HttpRequest::RequestPtr;
		using Response = HttpRequest::Response;
		request->SetResponseCallback(
			[this,
			IsValidLambda = this->impl_->isThisValid_,
			requestShortName = requestInfo.ShortName,
			ResponseProcessor = std::move(InFunctor),
			ResultCallback = std::move(InResultFunctor)]
			(RequestPtr const& request, Response const& response)
		{
			if (!(*IsValidLambda))
			{
				// see comments in #ReusableJsonQueries.cpp
				return;
			}
			bool bValidResponse = false;
			std::string requestError;
			Be::CleanUpGuard setErrorGuard([&]
			{
				// In case of early exit, ensure we store the error and notify the caller
				this->SetLastError(
					fmt::format("[{}] {}", requestShortName, requestError),
					request->GetRequestID());
				if (!bValidResponse)
				{
					ResultCallback(false, {}, request->GetRequestID());
				}
			});

			if (!request->CheckResponse(response, requestError))
			{
				if (!response.second.empty())
				{
					// Try to parse iTwin error
					requestError += GetErrorDescriptionFromJson(response.second,
						requestError.empty() ? "" : "\t");
				}
				return;
			}
			ResultDataType resultData;
			std::string parsingError;
			bValidResponse = ResponseProcessor(resultData, response, parsingError);
			if (!parsingError.empty())
			{
				requestError += parsingError;
			}

			// store error, if any
			if (!requestError.empty())
			{
				this->SetLastError(
					fmt::format("[{}] {}", requestShortName, requestError),
					request->GetRequestID());
			}

			ScopedWorkingWebServices WorkingInstanceSetter(this);
			ResultCallback(bValidResponse, resultData, request->GetRequestID());
			setErrorGuard.release();
		});
		request->Process(*http_, requestInfo.UrlSuffix, requestInfo.ContentString, headers);

		return request->GetRequestID();
	}

	void ITwinWebServices::GetITwinInfo(std::string const& iTwinId)
	{
		ITwinAPIRequestInfo iTwinRequestInfo =
		{
			__func__,
			HttpRequest::EVerb::Get,
			std::string("/itwins/") + iTwinId,
			"application/vnd.bentley.itwin-platform.v1+json"
		};
		iTwinRequestInfo.badlyFormed = iTwinId.empty();

		TProcessHttpRequest<ITwinInfo>(
			iTwinRequestInfo,
			[](ITwinInfo& iTwinInfo, Http::Response const& response, std::string& strError) -> bool
		{
			struct ITwinInfoHolder
			{
				ITwinInfo iTwin;
			} infoHolder;
			if (!Json::FromString(infoHolder, response.second, strError))
			{
				return false;
			}
			iTwinInfo = std::move(infoHolder.iTwin);
			return true;
		},
			[this](bool bResult, ITwinInfo const& resultData, HttpRequest::RequestID)
		{
			if (impl_->observer_)
			{
				impl_->observer_->OnITwinInfoRetrieved(bResult, resultData);
			}
		});
	}

	void ITwinWebServices::GetITwins()
	{
		static const ITwinAPIRequestInfo requestInfo =
		{
			__func__,
			HttpRequest::EVerb::Get,
			"/itwins/recents?subClass=Project&status=Active&$top=1000",
			"application/vnd.bentley.itwin-platform.v1+json"
		};
		TProcessHttpRequest<ITwinInfos>(
			requestInfo,
			[](ITwinInfos& iTwinInfos, Http::Response const& response, std::string& strError) -> bool
		{
			return Json::FromString(iTwinInfos, response.second, strError);
		},
			[this](bool bResult, ITwinInfos const& resultData, HttpRequest::RequestID)
		{
			if (impl_->observer_)
			{
				impl_->observer_->OnITwinsRetrieved(bResult, resultData);
			}
		});
	}

	void ITwinWebServices::GetITwinIModels(std::string const& iTwinId)
	{
		ITwinAPIRequestInfo requestInfo =
		{
			"GetIModels",
			HttpRequest::EVerb::Get,
			std::string("/imodels/?iTwinId=") + iTwinId + "&$top=100",
			"application/vnd.bentley.itwin-platform.v2+json"
		};
		requestInfo.badlyFormed = iTwinId.empty();

		TProcessHttpRequest<IModelInfos>(
			requestInfo,
			[](IModelInfos& iModelInfos, Http::Response const& response, std::string& strError) -> bool
		{
			return Json::FromString(iModelInfos, response.second, strError);
		},
			[this](bool bResult, IModelInfos const& resultData, HttpRequest::RequestID)
		{
			if (impl_->observer_)
			{
				impl_->observer_->OnIModelsRetrieved(bResult, resultData);
			}
		});
	}

	void ITwinWebServices::GetIModelChangesets(std::string const& iModelId, bool bRestrictToLatest /*= false*/)
	{
		ITwinAPIRequestInfo requestInfo =
		{
			__func__,
			HttpRequest::EVerb::Get,
			std::string("/imodels/") + iModelId + "/changesets?"
			+ (bRestrictToLatest ? "$top=1&" : "")
			+ "$orderBy=index+desc",
			"application/vnd.bentley.itwin-platform.v2+json"
		};
		requestInfo.badlyFormed = iModelId.empty();

		TProcessHttpRequest<ChangesetInfos>(
			requestInfo,
			[](ChangesetInfos& Infos, Http::Response const& response, std::string& strError) -> bool
		{
			return Json::FromString(Infos, response.second, strError);
		},
			[this](bool bResult, ChangesetInfos const& resultData, HttpRequest::RequestID)
		{
			if (impl_->observer_)
			{
				impl_->observer_->OnChangesetsRetrieved(bResult, resultData);
			}
		});
	}

	namespace Detail
	{
		struct ITwinExportRequest
		{
			std::string iModelId;
			std::string contextId; // aka iTwinID
			std::string changesetId;
			std::string exportType;
		};
		struct ITwinUrl
		{
			std::string href;
		};
		struct ITwinLinks
		{
			ITwinUrl mesh;
		};
		struct ITwinExportFullInfo
		{
			std::string id;
			std::string displayName;
			std::string status;
			ITwinExportRequest request;
			std::optional<ITwinLinks> _links;
			std::optional<std::string> lastModified;
		};

		[[nodiscard]] std::string FormatMeshUrl(std::string const& inputUrl)
		{
			std::string meshUrl = inputUrl;
			size_t const pos = meshUrl.find("?");
			if (pos != std::string::npos)
			{
				meshUrl.replace(pos, 1, "/tileset.json?");
			}
			return meshUrl;
		}

		[[nodiscard]] std::string FormatRealityDataUrl(std::string const& inputUrl,
			std::optional<std::string> const& rootDocument)
		{
			std::string finalUrl = inputUrl;
			if (rootDocument)
			{
				size_t const pos = finalUrl.find("?");
				if (pos != std::string::npos)
				{
					std::string const replacement = std::string("/") + *rootDocument + "?";
					finalUrl.replace(pos, 1, replacement);
				}
			}
			return finalUrl;
		}

		void SimplifyExportInfo(ITwinExportInfo& exportInfo, ITwinExportFullInfo const& fullInfo)
		{
			exportInfo.id = fullInfo.id;
			exportInfo.displayName = fullInfo.displayName;
			exportInfo.status = fullInfo.status;
			exportInfo.iModelId = fullInfo.request.iModelId;
			exportInfo.iTwinId = fullInfo.request.contextId;
			exportInfo.changesetId = fullInfo.request.changesetId;
			exportInfo.lastModified = fullInfo.lastModified.value_or("");
			if (fullInfo.status == "Complete" && fullInfo._links)
			{
				exportInfo.meshUrl = Detail::FormatMeshUrl(fullInfo._links->mesh.href);
			}
		}
	}

	void ITwinWebServices::GetExports(std::string const& iModelId, std::string const& changesetId)
	{
		// Beware changesetID can be empty (if the iModel has none).
		const ITwinAPIRequestInfo requestInfo =
		{
			__func__,
			HttpRequest::EVerb::Get,
			std::string("/mesh-export/?iModelId=") + iModelId + "&changesetId=" + changesetId,
			"application/vnd.bentley.itwin-platform.v1+json",
			{},
			{},
			{
				// The following headers have been added following suggestion by Daniel Iborra.
				// This header is supposed to filter exports, but it is not implemented yet on server.
				// Therefore we need to keep our own filter on the response for now.
				{ "exportType", "CESIUM" },
				{ "cdn", "1" }, // Activates CDN, improves performance
				{ "client", "Unreal" }, // For stats
				// (end of headers suggested by Daniel Iborra)
			},
			iModelId.empty()
		};
		TProcessHttpRequest<ITwinExportInfos>(
			requestInfo,
			[](ITwinExportInfos& Infos, Http::Response const& response, std::string& strError) -> bool
		{
			using ExportFullInfo = Detail::ITwinExportFullInfo;
			struct ITwinExportFullInfoHolder
			{
				std::vector<ExportFullInfo> exports;
			} exportsHolder;

			if (!Json::FromString(exportsHolder, response.second, strError))
			{
				return false;
			}

			// sort by decreasing modification time
			std::vector<ExportFullInfo>& exports(exportsHolder.exports);
			std::stable_sort(exports.begin(), exports.end(),
				[](ExportFullInfo const& A, ExportFullInfo const& B) {
				return A.lastModified > B.lastModified;
			});
			// only keep Cesium exports
			Infos.exports.reserve(exports.size());
			for (ExportFullInfo const& fullInfo : exports)
			{
				if (fullInfo.request.exportType == "CESIUM")
				{
					ITwinExportInfo& exportInfo = Infos.exports.emplace_back();
					Detail::SimplifyExportInfo(exportInfo, fullInfo);
				}
			}
			return true;
		},
			[this](bool bResult, ITwinExportInfos const& resultData, HttpRequest::RequestID)
		{
			if (impl_->observer_)
			{
				impl_->observer_->OnExportInfosRetrieved(bResult, resultData);
			}
		});
	}

	void ITwinWebServices::GetExportInfo(std::string const& exportId)
	{
		ITwinAPIRequestInfo requestInfo =
		{
			__func__,
			HttpRequest::EVerb::Get,
			std::string("/mesh-export/") + exportId,
			"application/vnd.bentley.itwin-platform.v1+json"
		};
		requestInfo.badlyFormed = exportId.empty();

		TProcessHttpRequest<ITwinExportInfo>(
			requestInfo,
			[](ITwinExportInfo& Export, Http::Response const& response, std::string& strError) -> bool
		{
			using ExportFullInfo = Detail::ITwinExportFullInfo;
			struct FullInfoHolder
			{
				rfl::Rename<"export", ExportFullInfo> export_; // "export" is a c++ keyword...
			} exportHolder;

			if (!Json::FromString(exportHolder, response.second, strError))
			{
				return false;
			}
			if (exportHolder.export_.value().request.exportType != "CESIUM")
			{
				strError = std::string("unsupported export type: ") + exportHolder.export_.value().request.exportType;
				return false;
			}
			Detail::SimplifyExportInfo(Export, exportHolder.export_.value());
			return true;
		},
			[this](bool bResult, ITwinExportInfo const& resultData, HttpRequest::RequestID)
		{
			if (impl_->observer_)
			{
				impl_->observer_->OnExportInfoRetrieved(bResult, resultData);
			}
		});
	}

	void ITwinWebServices::StartExport(std::string const& iModelId, std::string const& changesetId)
	{
		struct ExportParams
		{
			std::string iModelId;
			std::string changesetId;
			std::string exportType;
		};
		const ExportParams exportParams = { iModelId, changesetId, "CESIUM" };
		const std::string exportParams_Json = Json::ToString(exportParams);

		const ITwinAPIRequestInfo requestInfo =
		{
			__func__,
			HttpRequest::EVerb::Post,
			"/mesh-export",
			"application/vnd.bentley.itwin-platform.v1+json",

			/*** additional settings for POST ***/
			"application/json",
			exportParams_Json,

			{ { "use-new-exporter", "3" } },

			iModelId.empty()
		};
		TProcessHttpRequest<std::string>(
			requestInfo,
			[iModelId](std::string& outExportId, Http::Response const& response, std::string& strError) -> bool
		{
			struct ExportBasicInfo
			{
				std::string id;
			};
			struct StartExportInfoHolder
			{
				rfl::Rename<"export", ExportBasicInfo> export_; // "export" is a c++ keyword...
			} exportHolder;

			if (!Json::FromString(exportHolder, response.second, strError))
			{
				return false;
			}
			outExportId = exportHolder.export_.value().id;
			return true;
		},
			[this](bool bResult, std::string const& exportId, HttpRequest::RequestID)
		{
			if (impl_->observer_)
			{
				impl_->observer_->OnExportStarted(bResult, exportId);
			}
		});
	}

	namespace Detail
	{
		struct SavedViewData
		{
			SavedView savedView;
			SavedViewInfo savedViewInfo;
		};


		struct CameraInfo
		{
			double lens = 0.0;
			double focusDist = 0.0;
			std::array<double, 3> eye = { 0, 0, 0 };
		};
		struct Itwin3dView
		{
			std::array<double, 3> origin = { 0, 0, 0 };
			std::array<double, 3> extents = { 0, 0, 0 };
			Rotator angles;
			std::optional<CameraInfo> camera;
		};
		struct SavedView3DData
		{
			Itwin3dView itwin3dView;
		};
		struct SavedViewFullInfo
		{
			std::string id;
			std::string displayName;
			bool shared = false;
			SavedView3DData savedViewData;
		};

		struct SavedViewFullInfoHolder
		{
			SavedViewFullInfo savedView;

			void MoveToSavedViewData(SavedViewData& svData)
			{
				SavedViewFullInfo& fullInfo(this->savedView);
				Itwin3dView const& itwin3dView(fullInfo.savedViewData.itwin3dView);
				if (itwin3dView.camera)
					svData.savedView.origin = itwin3dView.camera->eye;
				else
					svData.savedView.origin = itwin3dView.origin;
				svData.savedView.extents = itwin3dView.extents;
				svData.savedView.angles = itwin3dView.angles;
				svData.savedViewInfo.id = std::move(fullInfo.id);
				svData.savedViewInfo.displayName = std::move(fullInfo.displayName);
				svData.savedViewInfo.shared = fullInfo.shared;
			}
		};
	}

	void ITwinWebServices::GetAllSavedViews(std::string const& iTwinId, std::string const& iModelId, std::string const& groupId /*=""*/)
	{
		ITwinAPIRequestInfo requestInfo =
		{
			__func__,
			HttpRequest::EVerb::Get,
			(!groupId.empty() ? std::string("/savedviews?groupId=") + groupId : std::string("/savedviews?iTwinId=") + iTwinId + "&iModelId=" + iModelId),
			"application/vnd.bentley.itwin-platform.v1+json",
			"application/json",
		};
		requestInfo.badlyFormed = iTwinId.empty() || iModelId.empty();

		TProcessHttpRequest<SavedViewInfos>(
			requestInfo,
			[groupId](SavedViewInfos& infos, Http::Response const& response, std::string& strError) -> bool
		{
			if (!Json::FromString(infos, response.second, strError))
				return false;
			infos.groupId = groupId;
			return true;
		},
			[this](bool bResult, SavedViewInfos const& resultData, HttpRequest::RequestID)
		{
			if (impl_->observer_)
			{
				impl_->observer_->OnSavedViewInfosRetrieved(bResult, resultData);
			}
		});
	}

	void ITwinWebServices::GetSavedViewsGroups(std::string const& iTwinId, std::string const& iModelId)
	{
		ITwinAPIRequestInfo groupsRequestInfo = 
		{
			__func__,
			HttpRequest::EVerb::Get,
			std::string("/savedviews/groups?iTwinId=") + iTwinId + "&iModelId=" + iModelId,
			"application/vnd.bentley.itwin-platform.v1+json",
			"application/json",
		};
		groupsRequestInfo.badlyFormed = iTwinId.empty() || iModelId.empty();

		TProcessHttpRequest<SavedViewGroupInfos>(
			groupsRequestInfo,
			[](SavedViewGroupInfos& Infos, Http::Response const& response, std::string& strError) -> bool
			{
				return Json::FromString(Infos, response.second, strError);
			},
			[this](bool bResult, SavedViewGroupInfos const& ResultData, HttpRequest::RequestID)
			{
				if (impl_->observer_)
				{
					impl_->observer_->OnSavedViewGroupInfosRetrieved(bResult, ResultData);
				}
			});
	}

	void ITwinWebServices::GetSavedView(std::string const& savedViewId)
	{
		ITwinAPIRequestInfo requestInfo =
		{
			__func__,
			HttpRequest::EVerb::Get,
			std::string("/savedviews/") + savedViewId,
			"application/vnd.bentley.itwin-platform.v1+json",
			"application/json",
		};
		requestInfo.badlyFormed = savedViewId.empty();

		using SavedViewData = Detail::SavedViewData;

		TProcessHttpRequest<SavedViewData>(
			requestInfo,
			[](SavedViewData& SVData, Http::Response const& response, std::string& strError) -> bool
		{
			Detail::SavedViewFullInfoHolder svInfoHolder;
			if (!Json::FromString(svInfoHolder, response.second, strError))
			{
				return false;
			}
			svInfoHolder.MoveToSavedViewData(SVData);
			return true;
		},
			[this](bool bResult, SavedViewData const& SVData, HttpRequest::RequestID)
		{
			if (impl_->observer_)
			{
				impl_->observer_->OnSavedViewRetrieved(bResult, SVData.savedView, SVData.savedViewInfo);
			}
		});
	}

	void ITwinWebServices::GetSavedViewThumbnail(std::string const& SavedViewId)
	{
		const ITwinAPIRequestInfo savedViewsRequestInfo = {
			__func__,
			HttpRequest::EVerb::Get,
			std::string("/savedviews/") + SavedViewId + std::string("/image"),
			"application/vnd.bentley.itwin-platform.v1+json",
			"application/json",
		};
		struct ResData
		{
			std::string ThumbnailURL;
			std::string SavedViewId;
		};
		TProcessHttpRequest<ResData>(
			savedViewsRequestInfo,
			[SavedViewId](ResData& SVData, Http::Response const& response, std::string& strError) -> bool
			{
				struct ThumbnailData
				{
					std::string href;
				} thumbnailInfoHolder;
				if (!Json::FromString(thumbnailInfoHolder, response.second, strError))
				{
					return false;
				}
				SVData.ThumbnailURL = thumbnailInfoHolder.href;
				SVData.SavedViewId = SavedViewId;
				return true;
			},
			[this](bool bResult, ResData const& SVData, HttpRequest::RequestID)
			{
				if (impl_->observer_)
				{
					impl_->observer_->OnSavedViewThumbnailRetrieved(bResult, SVData.ThumbnailURL, SVData.SavedViewId);
				}
			});
	}

	void ITwinWebServices::UpdateSavedViewThumbnail(std::string const& SavedViewId, std::string const& ThumbnailURL)
	{
		const ITwinAPIRequestInfo savedViewsRequestInfo = {
			__func__,
			HttpRequest::EVerb::Put,
			std::string("/savedviews/") + SavedViewId + std::string("/image"),
			"application/vnd.bentley.itwin-platform.v1+json",

			/*** additional settings for POST ***/
			"application/json",
			"{\"image\":\"" + ThumbnailURL
			+ "\"}"
		};
		
		RequestID const requestID = TProcessHttpRequest<std::string>(
			savedViewsRequestInfo,
			[](std::string& ErrorCode, Http::Response const& response, std::string& /*strError*/) -> bool
			{
				ITwinError iTwinError;
				if (Json::FromString(iTwinError, response.second))
				{
					ErrorCode = GetErrorDescription(iTwinError);
					return false;
				}
				ErrorCode = {};
				return true;
			},
			[this, SavedViewId](bool bResult, std::string const& strResponse, HttpRequest::RequestID requestId)
			{
				// in UpdateSavedViewThumbnail, the callbacks expect an error message (in case of failure)
				// => if none is provided, and if the last error recorded corresponds to our request, use the
				// latter as response.
				std::string OutResponse = strResponse;
				if (!bResult && OutResponse.empty())
				{
					OutResponse = GetRequestError(requestId);
				}
				if (impl_->observer_)
				{
					impl_->observer_->OnSavedViewThumbnailUpdated(bResult, SavedViewId, OutResponse);
				}
			}
		);
	}


	namespace Detail
	{
		// for Json to string conversion...

		struct SavedViewEditInfo
		{
			SavedView3DData savedViewData;
			std::string displayName;
			bool shared = true;
			std::vector<std::string> tagIds;
		};

		struct AddSavedViewInfo
		{
			std::string iTwinId;
			std::string iModelId;
			SavedView3DData savedViewData;
			std::optional<std::string> groupId;
			std::string displayName;
			bool shared = true;
			std::vector<std::string> tagIds;
		};

		struct AddSavedViewGroupInfo
		{
			std::string iTwinId;
			std::string iModelId;
			std::string displayName;
			bool shared = true;
		};

		template <typename SVEditInfo>
		void FillSavedViewEditInfo(SVEditInfo& outInfo,
			SavedView const& savedView, SavedViewInfo const& savedViewInfo)
		{
			outInfo.displayName = savedViewInfo.displayName;
			outInfo.shared = savedViewInfo.shared;

			Itwin3dView& itwin3dView(outInfo.savedViewData.itwin3dView);
			itwin3dView.origin = savedView.origin;
			itwin3dView.extents = savedView.extents;
			itwin3dView.angles = savedView.angles;
			itwin3dView.camera.emplace();
			itwin3dView.camera->eye = savedView.origin;
		}
	}

	void ITwinWebServices::AddSavedView(std::string const& iTwinId, std::string const& iModelId,
		SavedView const& savedView, SavedViewInfo const& savedViewInfo, std::string const& groupId/*=""*/)
	{
		Detail::AddSavedViewInfo addInfo;
		Detail::FillSavedViewEditInfo(addInfo, savedView, savedViewInfo);
		addInfo.iTwinId = iTwinId;
		addInfo.iModelId = iModelId;
		if (!groupId.empty())
			addInfo.groupId = groupId;
		const std::string addSavedView_Json = Json::ToString(addInfo);

		ITwinAPIRequestInfo requestInfo =
		{
			__func__,
			HttpRequest::EVerb::Post,
			"/savedviews/",
			"application/vnd.bentley.itwin-platform.v1+json",

			/*** additional settings for POST ***/
			"application/json",
			addSavedView_Json
		};
		requestInfo.badlyFormed = iTwinId.empty() || iModelId.empty();

		TProcessHttpRequest<SavedViewInfo>(
			requestInfo,
			[](SavedViewInfo& info, Http::Response const& response, std::string& strError) -> bool
		{
			struct SavedViewInfoHolder
			{
				SavedViewInfo savedView;
			} svHolder;
			if (!Json::FromString(svHolder, response.second, strError))
			{
				return false;
			}
			info = std::move(svHolder.savedView);
			return true;
		},
			[this](bool bResult, SavedViewInfo const& resultData, HttpRequest::RequestID)
		{
			this->OnSavedViewAdded(bResult, resultData);
		}
		);
	}

	void ITwinWebServices::OnSavedViewAdded(bool bSuccess, SavedViewInfo const& savedViewInfo)
	{
		if (impl_->observer_)
		{
			impl_->observer_->OnSavedViewAdded(bSuccess, savedViewInfo);
		}
	}

	void ITwinWebServices::AddSavedViewGroup(std::string const& iTwinId, std::string const& iModelId,
		SavedViewGroupInfo const& savedViewGroupInfo)
	{
		Detail::AddSavedViewGroupInfo addInfo;
		addInfo.iTwinId = iTwinId;
		addInfo.iModelId = iModelId;
		addInfo.displayName = savedViewGroupInfo.displayName;
		const std::string addGroup_Json = Json::ToString(addInfo);

		ITwinAPIRequestInfo requestInfo =
		{
			__func__,
			HttpRequest::EVerb::Post,
			"/savedviews/groups",
			"application/vnd.bentley.itwin-platform.v1+json",

			/*** additional settings for POST ***/
			"application/json",
			addGroup_Json
		};
		requestInfo.badlyFormed = iTwinId.empty() || iModelId.empty();

		TProcessHttpRequest<SavedViewGroupInfo>(
			requestInfo,
			[](SavedViewGroupInfo& info, Http::Response const& response, std::string& strError) -> bool
			{
				struct GroupInfoHolder
				{
					SavedViewGroupInfo group;
				} groupHolder;
				if (!Json::FromString(groupHolder, response.second, strError))
				{
					return false;
				}
				info = std::move(groupHolder.group);
				return true;
			},
			[this](bool bResult, SavedViewGroupInfo const& resultData, HttpRequest::RequestID)
			{
				if (impl_->observer_)
				{
					impl_->observer_->OnSavedViewGroupAdded(bResult, resultData);
				}
			}
		);
	}

	void ITwinWebServices::DeleteSavedView(std::string const& savedViewId)
	{
		ITwinAPIRequestInfo requestInfo = {
			__func__,
			HttpRequest::EVerb::Delete,
			std::string("/savedviews/") + savedViewId,
			"application/vnd.bentley.itwin-platform.v1+json"
		};
		requestInfo.badlyFormed = savedViewId.empty();

		TProcessHttpRequest<std::string>(
			requestInfo,
			[](std::string& outError, Http::Response const& response, std::string& /*strError*/) -> bool
		{
			ITwinError iTwinError;
			if (Json::FromString(iTwinError, response.second))
			{
				outError = GetErrorDescription(iTwinError);
				return false;
			}
			outError = {};
			return true;
		},
			[this, savedViewId](bool bResult, std::string const& strResponse, HttpRequest::RequestID requestId)
		{
			// in DeleteSavedView, the callbacks expect an error message (in case of failure)
			// => if none if provided, and if the last error recorded corresponds to our request, use the
			// latter as response.
			std::string outResponse = strResponse;
			if (!bResult && outResponse.empty())
			{
				outResponse = GetRequestError(requestId);
			}
			this->OnSavedViewDeleted(bResult, savedViewId, outResponse);
		}
		);
	}

	void ITwinWebServices::OnSavedViewDeleted(bool bSuccess, std::string const& savedViewId, std::string const& Response) const
	{
		if (impl_->observer_)
		{
			impl_->observer_->OnSavedViewDeleted(bSuccess, savedViewId, Response);
		}
	}

	void ITwinWebServices::EditSavedView(SavedView const& savedView, SavedViewInfo const& savedViewInfo)
	{
		Detail::SavedViewEditInfo editInfo;
		Detail::FillSavedViewEditInfo(editInfo, savedView, savedViewInfo);
		const std::string editSavedView_Json = Json::ToString(editInfo);

		ITwinAPIRequestInfo requestInfo =
		{
			__func__,
			HttpRequest::EVerb::Patch,
			std::string("/savedviews/") + savedViewInfo.id,
			"application/vnd.bentley.itwin-platform.v1+json",

			/*** additional settings for PATCH ***/
			"application/json",
			editSavedView_Json
		};
		requestInfo.badlyFormed = savedViewInfo.id.empty();

		using EditSavedViewData = Detail::SavedViewData;

		TProcessHttpRequest<EditSavedViewData>(
			requestInfo,
			[](EditSavedViewData& editSVData, Http::Response const& response, std::string& strError) -> bool
		{
			Detail::SavedViewFullInfoHolder svInfoHolder;
			if (!Json::FromString(svInfoHolder, response.second, strError))
			{
				return false;
			}
			svInfoHolder.MoveToSavedViewData(editSVData);
			return true;
		},
			[this](bool bResult, EditSavedViewData const& editSVData, HttpRequest::RequestID)
		{
			if (impl_->observer_)
			{
				impl_->observer_->OnSavedViewEdited(bResult, editSVData.savedView, editSVData.savedViewInfo);
			}
		});
	}


	void ITwinWebServices::GetRealityData(std::string const& iTwinId)
	{
		const ITwinAPIRequestInfo requestInfo =
		{
			__func__,
			HttpRequest::EVerb::Get,
			std::string("/reality-management/reality-data/?iTwinId=") + iTwinId + "&types=Cesium3DTiles&$top=100",
			"application/vnd.bentley.itwin-platform.v1+json",
			"",
			"",
			/* custom headers */
			{
				{ "Prefer",	"return=minimal" },
				{ "types",	"Cesium3DTiles" },
			},
			iTwinId.empty()
		};
		TProcessHttpRequest<ITwinRealityDataInfos>(
			requestInfo,
			[iTwinId](ITwinRealityDataInfos& RealityData, Http::Response const& response, std::string& strError) -> bool
		{
			return Json::FromString(RealityData, response.second, strError);
		},
			[this](bool bResult, ITwinRealityDataInfos const& resultData, HttpRequest::RequestID)
		{
			if (impl_->observer_)
			{
				impl_->observer_->OnRealityDataRetrieved(bResult, resultData);
			}
		});
	}

	namespace Detail
	{
		struct RealityDataGeoLocation
		{
			ITwinGeolocationInfo northEast;
			ITwinGeolocationInfo southWest;
		};
	}

	void ITwinWebServices::GetRealityData3DInfo(std::string const& iTwinId, std::string const& realityDataId)
	{
		// two distinct requests are needed here!
		// (code initially written in ITwinRealityData.cpp)
		ITwinAPIRequestInfo requestInfo =
		{
			__func__,
			HttpRequest::EVerb::Get,
			std::string("/reality-management/reality-data/") + realityDataId + "?iTwinId=" + iTwinId,
			"application/vnd.bentley.itwin-platform.v1+json"
		};
		requestInfo.badlyFormed = iTwinId.empty() || realityDataId.empty();

		TProcessHttpRequest<ITwinRealityData3DInfo>(
			requestInfo,
			[this, iTwinId, realityDataId](ITwinRealityData3DInfo& realityData3DInfo, Http::Response const& response, std::string& strError) -> bool
		{
			struct DetailedRealityDataInfo
			{
				std::string id;
				std::string displayName;
				std::optional<std::string> rootDocument;

				std::optional<Detail::RealityDataGeoLocation> extent;
			};

			struct ITwinRealityDataInfoHolder
			{
				DetailedRealityDataInfo realityData;
			} infoHolder;
			if (!Json::FromString(infoHolder, response.second, strError))
			{
				return false;
			}
			realityData3DInfo.id = realityDataId;
			realityData3DInfo.displayName = infoHolder.realityData.displayName;

			// Make a second request to retrieve mesh URL
			const ITwinAPIRequestInfo realDataRequestInfo =
			{
				"GetRealityData3DInfo-part2",
				HttpRequest::EVerb::Get,
				std::string("/reality-management/reality-data/") + realityDataId + "/readaccess?iTwinId=" + iTwinId,
				"application/vnd.bentley.itwin-platform.v1+json"
			};
			TProcessHttpRequest<ITwinRealityData3DInfo>(
				realDataRequestInfo,
				[this, realityData3DInfo, detailedInfo = infoHolder.realityData]
				(ITwinRealityData3DInfo& finalRealityData3DInfo, Http::Response const& response, std::string& strError) -> bool
			{
				finalRealityData3DInfo.id = detailedInfo.id;
				finalRealityData3DInfo.displayName = detailedInfo.displayName;
				if (detailedInfo.extent)
				{
					finalRealityData3DInfo.bGeolocated = true;
					finalRealityData3DInfo.extentNorthEast = detailedInfo.extent->northEast;
					finalRealityData3DInfo.extentSouthWest = detailedInfo.extent->southWest;
				}

				struct RealDataLinks
				{
					Detail::ITwinUrl containerUrl;
				};
				struct RealDataLinkInfo
				{
					RealDataLinks _links;
				} linkInfo;

				if (!Json::FromString(linkInfo, response.second, strError))
				{
					return false;
				}
				finalRealityData3DInfo.meshUrl = Detail::FormatRealityDataUrl(
					linkInfo._links.containerUrl.href,
					detailedInfo.rootDocument);
				return true;
			},
				[this](bool bResult, ITwinRealityData3DInfo const& FinalResultData, HttpRequest::RequestID)
			{
				// This is for the 2nd request: broadcast final result
				if (impl_->observer_)
				{
					impl_->observer_->OnRealityData3DInfoRetrieved(bResult, FinalResultData);
				}
			});

			return true;
		},
			[this](bool bResult, ITwinRealityData3DInfo const& PartialResultData, HttpRequest::RequestID)
		{
			// result of the 1st request: only broadcast it in case of failure
			if (!bResult)
			{
				// the 1st request has failed
				if (impl_->observer_)
				{
					impl_->observer_->OnRealityData3DInfoRetrieved(false, PartialResultData);
				}
			}
		});
	}

	namespace
	{
		struct PropParserData
		{
			std::string currentKey_;
			std::string currentType_;
			std::string arrayName_;
			std::string arrayType_;

			void SetCurrentType(std::string const& strType)
			{
				currentType_ = strType;
				arrayType_.clear();
			}

			void SetCurrentKey(std::string const& strKey)
			{
				currentKey_ = strKey;
				if (currentKey_ == "@Presentation:selectedItems.categoryLabel@")
				{
					currentKey_ = "Selected Item";
				}
			}
		};

		struct ElementPropertiesVisitor
		{
			SDK::Core::ITwinElementProperties& outProps_;
			mutable PropParserData helper_;
			mutable std::stringstream error_;

			ElementPropertiesVisitor(SDK::Core::ITwinElementProperties& elementProps)
				: outProps_(elementProps)
			{

			}

			// implement the 6 possible cases of the variant
			// bool, int, double, std::string, Object, Array
			// some are not used at all, since we the response only deals with strings primitives

			void operator()(const bool& boolValue) const
			{
				error_ << "unhandled boolean" << std::endl;
			}
			void operator()(const int& nValue) const
			{
				error_ << "unhandled integer: " << nValue << std::endl;
			}
			void operator()(const double& dValue) const
			{
				error_ << "unhandled double: " << dValue << std::endl;
			}

			void operator()(const std::string& strValue) const
			{
				if (helper_.currentType_ == "primitive" ||
					helper_.arrayType_ == "primitive")
				{
					SDK::Core::ITwinElementAttribute& attr = outProps_.properties.back().attributes.emplace_back();
					attr.name = helper_.currentKey_;
					attr.value = strValue;
				}
				else
				{
					error_ << "unhandled string: " << strValue << std::endl;
				}
			}

			void operator()(const rfl::Generic::Object& rflObject) const
			{
				for (auto const& data : rflObject)
				{
					bool bVisitValue = false;

					if (data.first == "type")
					{
						helper_.SetCurrentType(data.second.to_string().value());

						if (helper_.currentType_ == "category")
						{
							// starting a new property
							SDK::Core::ITwinElementProperty& newProp = outProps_.properties.emplace_back();
							newProp.name = helper_.currentKey_;
						}
					}
					else if (data.first == "valueType")
					{
						if (helper_.currentType_ == "array")
						{
							helper_.arrayType_ = data.second.to_string().value();
						}
						else
						{
							error_ << "unexpected key: 'valueType'" << std::endl;
						}
					}
					else if (data.first == "value"
						|| data.first == "values"
						|| data.first == "items")
					{
						bVisitValue = true;
					}
					else
					{
						helper_.SetCurrentKey(data.first);
						bVisitValue = true;
					}
					if (bVisitValue)
					{
						std::visit(*this, data.second.get());
					}
				}
			}

			void operator()(const rfl::Generic::Array& rflArray) const
			{
				if (rflArray.empty())
					return;
				// only consider 1st item for now (we only handle single element selection for now...)
				const rfl::Generic& obj = rflArray[0];
				if (helper_.arrayType_ == "primitive"
					|| helper_.arrayType_ == "struct")
				{
					std::visit(*this, obj.get());
				}
				else if (!helper_.arrayType_.empty())
				{
					error_ << "unhandled array type: " << helper_.arrayType_ << std::endl;
				}
				else
				{
					error_ << "unexpected array (unknown array type)" << std::endl;
				}
			}

			std::string GetError() const { return error_.str(); }
		};
	}

	void ITwinWebServices::GetElementProperties(
		std::string const& iTwinId, std::string const& iModelId,
		std::string const& iChangesetId, std::string const& elementId)
	{
		std::string const key = fmt::format("{}:{}", iModelId, iChangesetId);

		ITwinAPIRequestInfo requestInfo = {
			__func__,
			HttpRequest::EVerb::Post,
			std::string("/imodel/rpc/v4/mode/1/context/") + iTwinId + "/imodel/" + iModelId
			+ "/changeset/" + iChangesetId + "/PresentationRpcInterface-4.1.0-getElementProperties",
			"application/vnd.bentley.itwin-platform.v1+json",

			/*** additional settings for POST ***/
			"text/plain",
			"[{\"key\":\"" + key
			+ "\",\"iTwinId\":\"" + iTwinId
			+ "\",\"iModelId\":\"" + iModelId
			+ "\",\"changeset\":{\"id\":\"" + iChangesetId
			+ "\"}},{\"elementId\":\"" + elementId
			+ "\"}]"
		};
		requestInfo.badlyFormed = iTwinId.empty() || iModelId.empty() || iChangesetId.empty() || elementId.empty();

		TProcessHttpRequest<ITwinElementProperties>(
			requestInfo,
			[this](ITwinElementProperties& elementProps, Http::Response const& response, std::string& strError) -> bool
		{
			struct ItemsHolder { rfl::Generic items; };
			struct ResultHolder { ItemsHolder result; };
			ResultHolder res;
			if (!Json::FromString(res, response.second, strError))
			{
				return false;
			}
			ElementPropertiesVisitor propBuilder(elementProps);
			try
			{
				std::visit(propBuilder, res.result.items.get());
			}
			catch (std::exception const& e)
			{
				strError = std::string("exception while parsing element properties: ") + e.what() + "\n";
			}
			strError += propBuilder.GetError();
			return strError.empty();
		},
			[this](bool bResult, ITwinElementProperties const& resultData, HttpRequest::RequestID)
		{
			if (impl_->observer_)
			{
				impl_->observer_->OnElementPropertiesRetrieved(bResult, resultData);
			}
		}
		);
	}

	void ITwinWebServices::GetIModelProperties(std::string const& iTwinId, std::string const& iModelId, std::string const& iChangesetId)
	{
		ITwinAPIRequestInfo requestInfo = {
			__func__,
			HttpRequest::EVerb::Post,
			std::string("/imodel/rpc/v4/mode/1/context/") + iTwinId + "/imodel/" + iModelId
			+ "/changeset/" + iChangesetId + "/IModelReadRpcInterface-3.6.0-getConnectionProps",
			"application/vnd.bentley.itwin-platform.v1+json",

			/*** additional settings for POST ***/
			"text/plain",
			"[{\"iTwinId\":\"" + iTwinId
			+ "\",\"iModelId\":\"" + iModelId
			+ "\",\"changeset\":{\"id\":\"" + iChangesetId
			+ "\"}}]"
		};
		requestInfo.badlyFormed = iTwinId.empty() || iModelId.empty() || iChangesetId.empty();

		TProcessHttpRequest<IModelProperties>(
			requestInfo,
			[this](IModelProperties& iModelProps, Http::Response const& response, std::string& strError) -> bool
		{
			return Json::FromString(iModelProps, response.second, strError);
		},
			[this](bool bResult, IModelProperties const& resultData, HttpRequest::RequestID)
		{
			if (impl_->observer_)
			{
				impl_->observer_->OnIModelPropertiesRetrieved(bResult, resultData);
			}
		});
	}

	void ITwinWebServices::QueryIModel(
		std::string const& iTwinId, std::string const& iModelId, std::string const& iChangesetId,
		std::string const& ECSQLQuery, int offset, int count)
	{
		ITwinAPIRequestInfo requestInfo = {
			__func__,
			HttpRequest::EVerb::Post,
			std::string("/imodel/rpc/v4/mode/1/context/") + iTwinId + "/imodel/" + iModelId 
			+ "/changeset/" + iChangesetId + "/IModelReadRpcInterface-3.6.0-queryRows",
			"application/vnd.bentley.itwin-platform.v1+json",

			/*** additional settings for POST ***/
			"text/plain",
			"[{\"iTwinId\":\"" + iTwinId
			+ "\",\"iModelId\":\"" + iModelId
			+ "\",\"changeset\":{\"id\":\"" + iChangesetId
			+ "\"}},{\"limit\":{\"offset\":" + std::to_string(offset)
			+ ",\"count\":" + std::to_string(count)
			+ "},\"rowFormat\":1,\"convertClassIdsToClassNames\":true,\"kind\":1,\"valueFormat\":0,\"query\":\"" + ECSQLQuery
			+ "\"}]"
		};
		requestInfo.badlyFormed = iTwinId.empty() || iModelId.empty() || iChangesetId.empty()
			|| ECSQLQuery.empty();

		TProcessHttpRequest<std::string>(
			requestInfo,
			[](std::string &Infos, Http::Response const& response, std::string& strError) -> bool
			{
				struct DataHolder { rfl::Generic data; };
				DataHolder res;
				if (!Json::FromString(res, response.second, strError))
				{
					return false;
				}
				Infos = response.second;
				return true;
			},
			[this](bool bResult, std::string const& ResultData, HttpRequest::RequestID)
			{
				if (impl_->observer_)
				{
					impl_->observer_->OnIModelQueried(bResult, ResultData);
				}
			}
		);
	}


	namespace
	{
		struct MaterialPropParserData : public PropParserData
		{
			using Vec2 = std::array<double, 2>;
			using Vec3 = std::array<double, 3>;

			// additional stuff to parse a VEC3 (such as a color) or VEC2
			size_t currentVecSize_ = 0;
			std::optional<size_t> currentCoordIndex_;
			Vec3 currentVec3_;

			void StartParseVec(size_t nbElements, std::stringstream& error)
			{
				currentCoordIndex_.emplace(0);
				if (nbElements > 3)
				{
					error << "unsupported vector size: " << nbElements << std::endl;
				}
				currentVecSize_ = nbElements;
				currentVec3_.fill(0.0);
			}
			void EndParseVec()
			{
				currentCoordIndex_.reset();
			}

			enum class EVecParsingState
			{
				Error,
				InProgress,
				Done
			};
			EVecParsingState AddVecComponent(double const& dValue, std::stringstream& error)
			{
				if (*currentCoordIndex_ >= 3)
				{
					error << "unsupported vector type (more than 3 components)" << std::endl;
					return EVecParsingState::Error;
				}
				currentVec3_[*currentCoordIndex_] = dValue;
				(*currentCoordIndex_)++;
				if (*currentCoordIndex_ == currentVecSize_)
				{
					EndParseVec();
					return EVecParsingState::Done;
				}
				return EVecParsingState::InProgress;
			}

			std::optional<ITwinMaterialAttributeValue> MakeVecAttribute(std::stringstream& error)
			{
				switch (currentVecSize_)
				{
				case 1:
					return currentVec3_[0];
				case 2:
					return Vec2{ currentVec3_[0], currentVec3_[1] };
				case 3:
					return currentVec3_;

				case 0:
				default:
					error << "cannot make a vector with " << currentVecSize_ << " components" << std::endl;
					break;
				}
				return {};
			}
		};


		struct AttributesVisitor
		{
			SDK::Core::AttributeMap& outAttributes_;
			mutable MaterialPropParserData helper_;
			mutable std::stringstream error_;


			AttributesVisitor(SDK::Core::AttributeMap& outAttrs)
				: outAttributes_(outAttrs)
			{

			}

			template <typename T>
			void InsertValue(T const& val) const
			{
				if (helper_.currentKey_.empty())
				{
					error_ << "unknown key for new attribute" << std::endl;
					return;
				}
				outAttributes_.emplace(helper_.currentKey_, val);
				helper_.currentKey_.clear();
			}

			// implement the 6 possible cases of the variant
			// bool, int, double, std::string, Object, Array
			// some are not used at all, since we the response only deals with strings primitives

			void operator()(const bool& boolValue) const
			{
				InsertValue(boolValue);
			}

			inline void OnFloatingValue(const double& dValue) const
			{
				if (helper_.currentCoordIndex_)
				{
					helper_.AddVecComponent(dValue, error_);
				}
				else
				{
					InsertValue(dValue);
				}
			}

			void operator()(const int& nValue) const
			{
				OnFloatingValue(static_cast<double>(nValue));
			}
			void operator()(const double& dValue) const
			{
				OnFloatingValue(dValue);
			}

			void operator()(const std::string& strValue) const
			{
				InsertValue(strValue);
			}

			void operator()(const rfl::Generic::Object& rflObject) const
			{
				for (auto const& data : rflObject)
				{
					helper_.SetCurrentKey(data.first);
					std::visit(*this, data.second.get());
				}
			}

			void operator()(const rfl::Generic::Array& rflArray) const
			{
				// Used to parse colors, typically...
				if (rflArray.empty())
					return;

				helper_.StartParseVec(rflArray.size(), error_);
				for (rfl::Generic const& obj : rflArray)
				{
					std::visit(*this, obj.get());
				}
				helper_.EndParseVec();

				auto const vec = helper_.MakeVecAttribute(error_);
				if (vec)
				{
					InsertValue(*vec);
				}
			}

			std::string GetError() const { return error_.str(); }
		};


		struct MaterialPropertiesVisitor : public AttributesVisitor
		{
			using Super = AttributesVisitor;

			SDK::Core::ITwinMaterialProperties& outProps_;
			mutable bool bIsParsingMap_ = false;

			MaterialPropertiesVisitor(SDK::Core::ITwinMaterialProperties& elementProps)
				: AttributesVisitor(elementProps.attributes)
				, outProps_(elementProps)
			{

			}

			void operator()(const bool& boolValue) const { Super::operator()(boolValue); }
			void operator()(const int& nValue) const { Super::operator()(nValue); }
			void operator()(const double& dValue) const { Super::operator()(dValue); }
			void operator()(const std::string& strValue) const { Super::operator()(strValue); }
			void operator()(const rfl::Generic::Array& rflArray) const { Super::operator()(rflArray); }

			void operator()(const rfl::Generic::Object& rflObject) const
			{
				if (bIsParsingMap_)
				{
					// "Map" property will contain one Json object per channel
					// ("Bump", "Displacement" or any other channel).
					for (auto const& data : rflObject)
					{
						auto itMap = outProps_.maps.emplace(data.first, AttributeMap());
						AttributesVisitor mapParser(itMap.first->second);
						std::visit(mapParser, data.second.get());
					}
				}
				else
				{
					for (auto const& data : rflObject)
					{
						bool const bParsingMap_Old = bIsParsingMap_;
						helper_.SetCurrentKey(data.first);
						if (data.first == "Map")
						{
							// Make a particular case for "Map" property: store texture maps properties
							// in a dedicated map.
							bIsParsingMap_ = true;
						}
						std::visit(*this, data.second.get());

						bIsParsingMap_ = bParsingMap_Old;
					}
				}
			}
		};
	}

	void ITwinWebServices::GetMaterialListProperties(
		std::string const& iTwinId, std::string const& iModelId, std::string const& iChangesetId,
		std::vector<std::string> const& materialIds)
	{
		std::string const jsonMatIDs = Json::ToString(materialIds);
		ITwinAPIRequestInfo requestInfo = {
			__func__,
			HttpRequest::EVerb::Post,
			std::string("/imodel/rpc/v4/mode/1/context/") + iTwinId + "/imodel/" + iModelId
			+ "/changeset/" + iChangesetId + "/IModelReadRpcInterface-3.6.0-getElementProps",
			"application/vnd.bentley.itwin-platform.v1+json",

			/*** additional settings for POST ***/
			"text/plain",
			"[{\"iTwinId\":\"" + iTwinId
			+ "\",\"iModelId\":\"" + iModelId
			+ "\",\"changeset\":{\"id\":\"" + iChangesetId
			+ "\"}}," + jsonMatIDs
			+ "]"
		};
		requestInfo.badlyFormed = iTwinId.empty() || iModelId.empty() || iChangesetId.empty() || materialIds.empty();

		TProcessHttpRequest<ITwinMaterialPropertiesMap>(
			requestInfo,
			[this](ITwinMaterialPropertiesMap& itwinMaterials, Http::Response const& response, std::string& strError) -> bool
		{
			struct MaterialAssets
			{
				rfl::Generic renderMaterial;
			};
			struct MaterialJsonProperties
			{
				MaterialAssets materialAssets;
			};
			struct CodeProps
			{
				std::string scope;
				std::string spec;
				std::optional<std::string> value;
			};
			struct MaterialInfo
			{
				std::string id;
				std::optional<std::string> classFullName;
				CodeProps code;
				std::optional<std::string> userLabel;
				MaterialJsonProperties jsonProperties;
			};
			std::vector<MaterialInfo> infos;
			if (!Json::FromString(infos, response.second, strError))
			{
				return false;
			}
			for (MaterialInfo const& info : infos)
			{
				auto itMatProps = itwinMaterials.data_.emplace(info.id, ITwinMaterialProperties());
				ITwinMaterialProperties& itwinMaterial = itMatProps.first->second;
				itwinMaterial.id = info.id;
				if (info.code.value)
					itwinMaterial.name = *info.code.value;
				else if (info.userLabel)
					itwinMaterial.name = *info.userLabel;
				else
					itwinMaterial.name = fmt::format("Material_{}", info.id);
				MaterialPropertiesVisitor propBuilder(itwinMaterial);
				try
				{
					std::visit(propBuilder, info.jsonProperties.materialAssets.renderMaterial.get());
				}
				catch (std::exception const& e)
				{
					strError = std::string("exception while parsing material properties: ") + e.what() + "\n";
				}
				strError += propBuilder.GetError();
			}
			return strError.empty();
		},
			[this](bool bResult, ITwinMaterialPropertiesMap const& resultData, HttpRequest::RequestID)
		{
			if (impl_->observer_)
			{
				impl_->observer_->OnMaterialPropertiesRetrieved(bResult, resultData);
			}
		}
		);
	}

	void ITwinWebServices::GetMaterialProperties(
		std::string const& iTwinId, std::string const& iModelId, std::string const& iChangesetId,
		std::string const& materialId)
	{
		GetMaterialListProperties(iTwinId, iModelId, iChangesetId, { materialId });
	}
}
