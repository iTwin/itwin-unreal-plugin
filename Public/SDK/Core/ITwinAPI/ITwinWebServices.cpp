/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinWebServices.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinWebServices.h"
#include <Core/ITwinApi/ITwinMaterialPrediction.h>
#include <Core/ITwinApi/ITwinRequestDump.h>
#include "ITwinAuthManager.h"
#include "ITwinWebServicesObserver.h"

#include <../BeHeaders/Compil/CleanUpGuard.h>

#include <fmt/format.h>

#include <Core/Json/Json.h>
#include <Core/Network/HttpRequest.h>
#include <Core/Tools/DelayedCall.h>


namespace AdvViz::SDK
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
		std::string const& indent /* = {}*/)
	{
		// Try to parse iTwin error
		ITwinError iTwinError;
		std::string parseError;
		if (Json::FromString(iTwinError, jsonContent, parseError, false /*bLogParseError*/))
		{
			return GetErrorDescription(iTwinError, indent);
		}
		else
		{
			return {};
		}
	}

	static std::string BuildUniqueImplName()
	{
		static int WebSrcImplCount = 0;

		const int nextID = WebSrcImplCount++;
		return fmt::format("ws_{}_", nextID);
	}

	class ITwinWebServices::Impl
	{
		friend class ITwinWebServices;

		ITwinWebServices& owner_;

		std::string const uniqueName_ = BuildUniqueImplName();
		using Mutex = std::recursive_mutex;
		using Lock = std::lock_guard<std::recursive_mutex>;
		mutable Mutex mutex_;
		std::shared_ptr< std::atomic_bool > isThisValid_; // same principle as in #FReusableJsonQueries::FImpl

		IITwinWebServicesObserver* observer_ = nullptr;

		struct LastError
		{
			std::string msg_;
			RequestID requestId_ = HttpRequest::NO_REQUEST;
		};
		LastError lastError_;

		std::string customServerURL_;

		bool hasSetupMLMaterialAssignment_ = false;

		enum class EMatMLPredictionStep : uint8_t
		{
			Init = 0,

			RunJob,
			GetJobStatus,
			GetJobResults,

			Done
		};
		struct MaterialMLPredictionInfo
		{
			EMatMLPredictionStep step_ = EMatMLPredictionStep::Init;
			std::string iTwinId_;
			std::string iModelId_;
			std::string changesetId_;

			// Variables filled from the ML service responses
			std::string jobId_;
			std::optional<std::string> jobResultURL_;
			ITwinMaterialPrediction result_;
		};
		std::optional<MaterialMLPredictionInfo> matMLPredictionInfo_ = std::nullopt;
		std::filesystem::path matMLPredictionCacheFolder_;
		bool isResumingMatMLPrediction_ = false;


	public:
		Impl(ITwinWebServices& Owner)
			: owner_(Owner)
		{
			isThisValid_ = std::make_shared<std::atomic_bool>(true);
		}

		~Impl()
		{
			*isThisValid_ = false;
		}

		using ResultCallback =
			std::function<bool(Http::Response const& response, RequestID const&, std::string& strError)>;

		inline std::string GetAPIRootURL(EITwinEnvironment env) const
		{
			// Use custom URL if defined (in unit tests based on mock server typically).
			if (!customServerURL_.empty())
			{
				return customServerURL_;
			}
			// Adapt prefix to current iTwin environment.
			return std::string("https://") + ITwinServerEnvironment::GetUrlPrefix(env) + "api.bentley.com";
		}

		static std::pair<float, int> defaultShouldRetryFunc(int attempt, int httpCode)
		{
			if (202 == httpCode)
			{
				// Allow more attempts, DB is probably running lengthy background processes to reply our
				// query (happens the first time a specific changeset is queried after creation).
				// Here, retry every 20s for 5 minutes:
				return std::make_pair((attempt >= 0 && attempt < 15) ? 20.f : 0.f, std::max(0, 15 - attempt));
			}
			BE_ASSERT(attempt <= 3, "Too many http request attempts?!");
			switch (attempt)
			{
			case 0: return std::make_pair( 5.f, 3); // 1st attempt
			case 1: return std::make_pair(12.f, 2);
			case 2: return std::make_pair(30.f, 1);
			case 3: return std::make_pair(30.f, 0);
			default:
				return std::make_pair(0.f, 0);
			}
		}

		/// \param notifyRequestID Notify caller of request ID identifying the request: several calls can be
		///		made sequentially, because of retries! Also, note that retries are called from unspecified
		///		threads, so caller functor must take care to protect whatever it does against concurrency.
		/// \param shouldRetry User-supplied retry policy: for each attempt (even the 1st one = 0, to
		///		determine the number of retries left!), this is called at least once (but maybe more, in case
		///		of code 202) to determine the number of seconds to wait before retrying, and the number of
		///		attempts left, returned as an std::pair. Normally we only need to know whether or not this is
		///		the last attempt, which failure would mean a hard error instead of a mere warning.
		///		IMPORTANT: this functor is also used to determine what to do in case of http response
		///		code 202 = "Accepted" (retry later, or handle as success by returning zero retries).
		///		The default is to wait and retry several times, because several endpoints (at least
		///		QueryIModel and GetIModelProperties) were witnessed to return 202 just after an iModel
		///		changeset has been created, while the DB is being initialized I guess.
		void ProcessHttpRequest(ITwinAPIRequestInfo const& requestInfo, ResultCallback&& resultCallback,
			std::function<void(RequestID const&)> && notifyRequestID = {},
			FilterErrorFunc&& filterError = {},
			std::function<std::pair<float, int>(int attempt, int httpCode)> && shouldRetry =
				std::bind(&ITwinWebServices::Impl::defaultShouldRetryFunc,
						  std::placeholders::_1, std::placeholders::_2),
			int const attempt = 0);

		EITwinMatMLPredictionStatus ProcessMatMLPrediction(std::string const& iTwinId,
			std::string const& iModelId, std::string const& changesetId);

		void SetLastError(std::string const& strError, RequestID const& requestID,
			int retriesLeft, bool bLogError = true);

	private:
		ITwinAPIRequestInfo BuildMatMLPredictionRequestInfo(EMatMLPredictionStep eStep);
		void ProcessMatMLPredictionStep(EMatMLPredictionStep eStep);
		bool ProcessMatMLPredictionStepWithDelay(EMatMLPredictionStep eStep);
		std::pair<float, int> ShouldRetryMaterialMLStep(EMatMLPredictionStep eStep, int attempt, int httpCode) const;

		struct MatMLPredictionParseResult
		{
			/// Whether we received a valid response.
			bool parsingOK = false;
			/// Response parsing error should be filled *only* in case of communication error, not when the
			/// service fails to compute a prediction for some reason.
			std::string parsingError;
			/// Will be set to false when the response indicates a failed or finished job.
			bool continueJob = false;
			/// Only used at step GetJobStatus, which should be retried as long as the job is not finished.
			bool retryWithDelay = false;
		};
		void ParseMatMLPredictionResponse(EMatMLPredictionStep eStep,
			Http::Response const& response, RequestID const&,
			MatMLPredictionParseResult& parseResult);

		/// Reset all data retrieved from the ML material prediction server (job ID...)
		void ResetMatMLJobData();

		std::filesystem::path GetMatMLInfoPath() const;
		void RemoveMatMLInfoFile();

	}; // class ITwinWebServices::Impl


	ITwinWebServices::ITwinWebServices()
	{
		impl_ = std::make_unique<Impl>(*this);

		http_.reset(Http::New());
		http_->SetBaseUrl(GetAPIRootURL().c_str());
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
			http_->SetBaseUrl(baseUrl_new.c_str());
		}
	}

	void ITwinWebServices::SetEnvironment(EITwinEnvironment env)
	{
		ModifyServerSetting([this, env] { env_ = env; });
	}

	std::string ITwinWebServices::GetAuthToken() const
	{
		std::string authToken;
		auto const& authMngr = ITwinAuthManager::GetInstance(env_);
		if (authMngr)
		{
			auto token =  authMngr->GetAccessToken();
			if (token)
				return *token;
		}
		return authToken;
	}

	void ITwinWebServices::SetObserver(IITwinWebServicesObserver* InObserver)
	{
		impl_->observer_ = InObserver;
	}

	bool ITwinWebServices::HasObserver(IITwinWebServicesObserver const* observer) const
	{
		return impl_->observer_ == observer;
	}

	void ITwinWebServices::Impl::SetLastError(std::string const& strError, RequestID const& requestID,
		int retriesLeft, bool bLogError /*= true*/)
	{
		Lock Lock(mutex_);
		lastError_.msg_ = strError;
		lastError_.requestId_ = requestID;
		if (!strError.empty() && observer_)
		{
			observer_->OnRequestError(strError, retriesLeft, bLogError);
		}
	}

	std::string ITwinWebServices::GetLastError() const
	{
		Impl::Lock Lock(impl_->mutex_);
		return impl_->lastError_.msg_;
	}

	std::string ITwinWebServices::GetRequestError(RequestID const& requestID) const
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

	void ITwinWebServices::SetCustomServerURL(std::string const& serverUrl)
	{
		ModifyServerSetting([this, &serverUrl] { impl_->customServerURL_ = serverUrl; });
	}

	std::string ITwinWebServices::GetAPIRootURL() const
	{
		return impl_->GetAPIRootURL(env_);
	}

	namespace
	{
		//! Set this variable to true in the debugger to dump all requests & responses.
		//! The generated files can then be used in automatic tests, to mock the web services.
		//! See For example IModelRenderTest.cpp. 
		static bool g_ShouldDumpRequests = false;
	} // unnamed namespace

	void ITwinWebServices::Impl::ProcessHttpRequest(ITwinAPIRequestInfo const& requestInfo,
		ResultCallback&& InResultCallback,
		std::function<void(RequestID const&)> && notifyRequestID/*= {}*/,
		FilterErrorFunc&& filterError /*= {}*/,
		std::function<std::pair<float, int>(int attempt, int httpCode)> && shouldRetry/*= {}*/,
		int const attempt/*= 0*/)
	{
		if (requestInfo.badlyFormed)
		{
			// Some mandatory information was missing to build a valid url
			// => do not even try to process any request, but notify the error at once.
			this->SetLastError(
				fmt::format("[{}] insufficient parameters to build a valid request.", requestInfo.ShortName),
				HttpRequest::NO_REQUEST, /*no retry in that case*/0);
			std::string dummyErr;
			InResultCallback({}, HttpRequest::NO_REQUEST, dummyErr);
			return;
		}
		std::pair<float, int> retryInfo = // in case of failure
			shouldRetry ? shouldRetry(attempt, 0/*Unset*/) : std::make_pair(0.f, 0/*no retry on failure!*/);

		std::string const authToken = owner_.GetAuthToken();
		if (authToken.empty())
		{
			return;
		}
		const auto request = std::shared_ptr<HttpRequest>(HttpRequest::New());
		if (!request)
		{
			return;
		}
		if (notifyRequestID)
		{
			notifyRequestID(request->GetRequestID());
		}
		request->SetVerb(requestInfo.Verb);
		if (requestInfo.needRawData)
		{
			request->SetNeedRawData(true);
		}

		Http::Headers headers;
		headers.reserve(requestInfo.CustomHeaders.size() + 5);

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
		headers.emplace_back("X-Correlation-ID", request->GetRequestID());

		// add custom headers, if any
		for (auto const& [key, value] : requestInfo.CustomHeaders) {
			headers.emplace_back(key, value);
		}

		if (requestInfo.discardAllHeaders)
		{
			// Some very specific requests require no header (download of texture from blob storage...)
			headers.clear();
		}

		std::filesystem::path requestDumpPath;
		if (g_ShouldDumpRequests)
		{
			// Dump request to temp folder.
			requestDumpPath = std::filesystem::temp_directory_path()/"iTwinRequestDump"/
				RequestDump::GetRequestHash(requestInfo.UrlSuffix, requestInfo.ContentString);
			std::filesystem::remove_all(requestDumpPath);
			std::filesystem::create_directories(requestDumpPath);
			std::ofstream(requestDumpPath/"request.json") << rfl::json::write(
				RequestDump::Request{requestInfo.UrlSuffix, requestInfo.ContentString}, YYJSON_WRITE_PRETTY);
		}
		using RequestPtr = HttpRequest::RequestPtr;
		using Response = HttpRequest::Response;
		request->SetResponseCallback(
			[this,
			isValidLambda = isThisValid_,
			requestInfoCopy = requestInfo,
			resultCallback = std::move(InResultCallback),
			notifyRequestID = std::move(notifyRequestID),
			filterError = std::move(filterError),
			shouldRetry = std::move(shouldRetry),
			requestDumpPath, attempt, retryInfo/*needs to be mutable*/]
			(RequestPtr const& request, Response const& response) mutable
		{
			if (!requestDumpPath.empty())
			{
				// Dump response to temp folder.
				std::ofstream(requestDumpPath/"response.json") << rfl::json::write(
					RequestDump::Response{response.first, response.second}, YYJSON_WRITE_PRETTY);
				if (!response.second.empty())
					std::ofstream(requestDumpPath/"response.bin").write(
						(const char*)response.second.data(), response.second.size());
			}
			if (!(*isValidLambda))
			{
				// see comments in #ReusableJsonQueries.cpp
				return;
			}
			bool bValidResponse = false;
			std::string requestError;
			Be::CleanUpGuard setErrorGuard([&, this] () mutable
			{
				// In case of early exit, ensure we store the error and notify the caller

				// Some errors are not really relevant, ie. they can happen in a normal cases, typically for
				// generic queries, which may trigger errors in case the data we are looking for is missing.
				// In such case, we would certainly prefer not to retry, and to skip the error completely
				// from logs.
				bool bAllowRetry(retryInfo.second > 0);
				bool bLogError(true);
				if (!bValidResponse && filterError)
				{
					filterError(requestError, bAllowRetry, bLogError);
				}

				this->SetLastError(
					fmt::format("[{}] {}", requestInfoCopy.ShortName, requestError),
					request->GetRequestID(),
					bAllowRetry ? retryInfo.second : 0,
					bLogError);
				if (!bValidResponse)
				{
					if (bAllowRetry)
					{
						// Retry after a delay.
						float const delayInSeconds = std::max(0.1f, retryInfo.first);
						std::string const delayedCallUniqueID = uniqueName_ + requestInfoCopy.ShortName;

						UniqueDelayedCall(delayedCallUniqueID,
							[this, isValidRetryLambda = isValidLambda,
							retry_requestInfo = std::move(requestInfoCopy),
							retry_resultCallback = std::move(resultCallback),
							retry_notifyRequestID = std::move(notifyRequestID),
							retry_filterError = std::move(filterError),
							retry_shouldRetry = std::move(shouldRetry),
							attempt]() mutable
						{
							if (*isValidRetryLambda)
							{
								this->ProcessHttpRequest(
									retry_requestInfo,
									std::move(retry_resultCallback),
									std::move(retry_notifyRequestID),
									std::move(retry_filterError),
									std::move(retry_shouldRetry),
									attempt + 1);
							}
							return DelayedCall::EReturnedValue::Done;
						}, delayInSeconds /* in seconds*/);

					}
					else
					{
						std::string dummyErr;
						resultCallback({}, request->GetRequestID(), dummyErr);
					}
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
				// store error and launch retry (through CleanUpGuard above)
				return;
			}
			// 202 = "Accepted but not immediately processed"! ie response is empty... This seems to happen
			// when querying an iModel (changeset)'s rows for the first time, maybe because of some possibly
			// lengthy init process? Should we then retry "indefinitely" or have some specific user feedback,
			// in case it's really long for BIG iModels?
			if (response.first == 202)
			{
				BE_ASSERT((bool)shouldRetry, "HTTP 202 received: you should handle this case by supplying a non-empty 'shouldRetry' functor!");
				retryInfo =	shouldRetry ? shouldRetry(attempt, 202) : std::make_pair(0.f, 0/*no retry on 202!*/);
				if (retryInfo.second > 0) // caller wants us to retry
				{
					requestError += "Received HTTP code 202: request accepted but answer delayed";
					// store "error" and launch retry (through CleanUpGuard above)
					return;
				}
				// else: handle as a success: resultCallback should handle this case!
			}
			ScopedWorkingWebServices WorkingInstanceSetter(&this->owner_);
			std::string parsingError;
			bValidResponse = resultCallback(response, request->GetRequestID(), parsingError);
			if (!parsingError.empty())
			{
				requestError += parsingError;
			}
			// store error and launch retry (through CleanUpGuard above)
			if (!requestError.empty())
				return;
			setErrorGuard.release();
		});
		request->Process(*owner_.http_, requestInfo.UrlSuffix, requestInfo.ContentString, headers,
			requestInfo.isFullUrl);
	}

	void ITwinWebServices::GetITwinInfo(std::string const& iTwinId)
	{
		ITwinAPIRequestInfo iTwinRequestInfo =
		{
			__func__,
			EVerb::Get,
			std::string("/itwins/") + iTwinId,
			"application/vnd.bentley.itwin-platform.v1+json"
		};
		iTwinRequestInfo.badlyFormed = iTwinId.empty();

		impl_->ProcessHttpRequest(
			iTwinRequestInfo,
			[this](Http::Response const& response, RequestID const&, std::string& strError) -> bool
		{
			struct ITwinInfoHolder
			{
				ITwinInfo iTwin;
			} infoHolder;
			bool const bResult = Http::IsSuccessful(response)
				&& Json::FromString(infoHolder, response.second, strError);
			if (impl_->observer_)
			{
				impl_->observer_->OnITwinInfoRetrieved(bResult, infoHolder.iTwin);
			}
			return bResult;
		});
	}

	void ITwinWebServices::GetITwins()
	{
		static const ITwinAPIRequestInfo requestInfo =
		{
			__func__,
			EVerb::Get,
			"/itwins/recents?subClass=Project&status=Active&$top=1000",
			"application/vnd.bentley.itwin-platform.v1+json"
		};
		impl_->ProcessHttpRequest(
			requestInfo,
			[this](Http::Response const& response, RequestID const&, std::string& strError) -> bool
		{
			ITwinInfos iTwinInfos;
			bool const bResult = Http::IsSuccessful(response)
				&& Json::FromString(iTwinInfos, response.second, strError);
			if (impl_->observer_)
			{
				impl_->observer_->OnITwinsRetrieved(bResult, iTwinInfos);
			}
			return bResult;
		});
	}

	void ITwinWebServices::GetITwinIModels(std::string const& iTwinId)
	{
		ITwinAPIRequestInfo requestInfo =
		{
			"GetIModels",
			EVerb::Get,
			std::string("/imodels/?iTwinId=") + iTwinId + "&$top=100",
			"application/vnd.bentley.itwin-platform.v2+json"
		};
		requestInfo.badlyFormed = iTwinId.empty();

		impl_->ProcessHttpRequest(
			requestInfo,
			[this](Http::Response const& response, RequestID const&, std::string& strError) -> bool
		{
			IModelInfos iModelInfos;
			bool const bResult = Http::IsSuccessful(response)
				&& Json::FromString(iModelInfos, response.second, strError);
			if (impl_->observer_)
			{
				impl_->observer_->OnIModelsRetrieved(bResult, iModelInfos);
			}
			return bResult;
		});
	}

	void ITwinWebServices::GetIModelChangesets(std::string const& iModelId, bool bRestrictToLatest /*= false*/)
	{
		ITwinAPIRequestInfo requestInfo =
		{
			__func__,
			EVerb::Get,
			std::string("/imodels/") + iModelId + "/changesets?"
			+ (bRestrictToLatest ? "$top=1&" : "")
			+ "$orderBy=index+desc",
			"application/vnd.bentley.itwin-platform.v2+json"
		};
		requestInfo.badlyFormed = iModelId.empty();

		impl_->ProcessHttpRequest(
			requestInfo,
			[this](Http::Response const& response, RequestID const&, std::string& strError) -> bool
		{
			ChangesetInfos changesets;
			bool const bResult = Http::IsSuccessful(response)
				&& Json::FromString(changesets, response.second, strError);
			if (impl_->observer_)
			{
				impl_->observer_->OnChangesetsRetrieved(bResult, changesets);
			}
			return bResult;
		});
	}

	namespace Detail
	{
		struct ITwinExportRequest
		{
			std::string iModelId;
			std::optional<std::string> contextId; // aka iTwinId, need one or the other
			std::optional<std::string> iTwinId; // aka contextId
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
			exportInfo.iTwinId = *fullInfo.request.iTwinId;
			exportInfo.changesetId = fullInfo.request.changesetId;
			exportInfo.lastModified = fullInfo.lastModified.value_or("");
			if (fullInfo.status == "Complete" && fullInfo._links)
			{
				exportInfo.meshUrl = Detail::FormatMeshUrl(fullInfo._links->mesh.href);
			}
		}
		// URL parameters:
		//	* exportType=CESIUM to filter out non-cesium exports
		//	* cdn=1 to enable Content Delivery Network (will be the default after YII, says D.Iborra)
		//	* client=Unreal for identification
		// For Mesh Export Service's statistics, these need to be passed as URL parameters
		// (NOT custom headers - at least for client=Unreal, don't know about the others)
		static const std::string getExportsCommonUrlParams("exportType=CESIUM&cdn=1&client=Unreal");
	} // ns Detail

	void ITwinWebServices::GetExports(std::string const& iModelId, std::string const& changesetId)
	{
		// Beware changesetID can be empty (if the iModel has none).
		const ITwinAPIRequestInfo requestInfo =
		{
			.ShortName		= __func__,
			.Verb			= EVerb::Get,
			// $top=1 to get only the latest export for a given iModelId and changesetId
			.UrlSuffix		= std::string("/mesh-export/?$top=1&iModelId=") + iModelId + "&changesetId="
								+ changesetId + "&" + Detail::getExportsCommonUrlParams,
			.AcceptHeader	= "application/vnd.bentley.itwin-platform.v1+json",
			.badlyFormed	= iModelId.empty()
		};
		impl_->ProcessHttpRequest(
			requestInfo,
			[this](Http::Response const& response, RequestID const&, std::string& strError) -> bool
		{
			// could now use ITwinExportInfo (singular) TODO_GCO
			ITwinExportInfos Infos;

			using ExportFullInfo = Detail::ITwinExportFullInfo;
			struct ITwinExportFullInfoHolder
			{
				std::vector<ExportFullInfo> exports;
			} exportsHolder;

			bool const bValidResponse = Http::IsSuccessful(response)
				&& Json::FromString(exportsHolder, response.second, strError);

			// There should be only one now (see $top=1 parameter in URL)
			if (bValidResponse && !exportsHolder.exports.empty())
			{
				auto&& fullInfo = exportsHolder.exports[0];
				if (fullInfo.request.exportType != "CESIUM")
				{
					strError = std::string("entry has wrong exportType instead of CESIUM, got: ")
						+ fullInfo.request.exportType;
				}
				else
				{
					if (!fullInfo.request.iTwinId)
					{
						if (fullInfo.request.contextId)
							std::swap(fullInfo.request.contextId, fullInfo.request.iTwinId);
						else
							strError = std::string("entry has neither iTwinId nor contextId");
					}
					if (fullInfo.request.iTwinId)
					{
						ITwinExportInfo& exportInfo = Infos.exports.emplace_back();
						Detail::SimplifyExportInfo(exportInfo, fullInfo);
					}
				}
			}
			bool const hasError = Infos.exports.empty() && !strError.empty();

			bool const bResult = bValidResponse && !hasError;
			if (impl_->observer_)
			{
				impl_->observer_->OnExportInfosRetrieved(bResult, Infos);
			}
			return bResult;
		});
	}

	void ITwinWebServices::GetExportInfo(std::string const& exportId)
	{
		const ITwinAPIRequestInfo requestInfo =
		{
			.ShortName		= __func__,
			.Verb			= EVerb::Get,
			.UrlSuffix		= std::string("/mesh-export/") + exportId + "?" + Detail::getExportsCommonUrlParams,
			.AcceptHeader	= "application/vnd.bentley.itwin-platform.v1+json",
			.badlyFormed	= exportId.empty()
		};

		impl_->ProcessHttpRequest(
			requestInfo,
			[this](Http::Response const& response, RequestID const&, std::string& strError) -> bool
		{
			ITwinExportInfo exportInfo;

			using ExportFullInfo = Detail::ITwinExportFullInfo;
			struct FullInfoHolder
			{
				rfl::Rename<"export", ExportFullInfo> export_; // "export" is a c++ keyword...
			} exportHolder;

			bool bResult = Http::IsSuccessful(response)
				&& Json::FromString(exportHolder, response.second, strError);

			if (bResult) // validate returned export information
			{
				if (exportHolder.export_.value().request.exportType != "CESIUM")
				{
					strError = std::string("unsupported export type: ")
						+ exportHolder.export_.value().request.exportType;
					bResult = false;
				}
				else if (!exportHolder.export_.value().request.iTwinId)
				{
					if (exportHolder.export_.value().request.contextId)
					{
						std::swap(exportHolder.export_.value().request.iTwinId,
							exportHolder.export_.value().request.contextId);
					}
					else
					{
						strError = std::string("entry has neither iTwinId nor contextId");
						bResult = false;
					}
				}
			}
			if (bResult)
			{
				Detail::SimplifyExportInfo(exportInfo, exportHolder.export_.value());
			}
			if (impl_->observer_)
			{
				impl_->observer_->OnExportInfoRetrieved(bResult, exportInfo);
			}
			return bResult;
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

		const ITwinAPIRequestInfo requestInfo =
		{
			.ShortName		= __func__,
			.Verb			= EVerb::Post,
			.UrlSuffix		= "/mesh-export",
			.AcceptHeader	= "application/vnd.bentley.itwin-platform.v1+json",
			.ContentType	= "application/json",
			.ContentString	= Json::ToString(exportParams),
			.CustomHeaders	= { { "use-new-exporter", "3" } },
			.badlyFormed	= iModelId.empty()
		};
		impl_->ProcessHttpRequest(
			requestInfo,
			[this](Http::Response const& response, RequestID const&, std::string& strError) -> bool
		{
			struct ExportBasicInfo
			{
				std::string id;
			};
			struct StartExportInfoHolder
			{
				rfl::Rename<"export", ExportBasicInfo> export_; // "export" is a c++ keyword...
			} exportHolder;

			bool const bResult = Http::IsSuccessful(response)
				&& Json::FromString(exportHolder, response.second, strError);

			std::string exportId;
			if (bResult)
			{
				exportId = exportHolder.export_.value().id;
			}

			if (impl_->observer_)
			{
				impl_->observer_->OnExportStarted(bResult, exportId);
			}
			return bResult;
		},
		{}, // notifyRequestID
		{}, // filterError
		[](int attempt, int httpCode)
		{
			if (202 == httpCode)
			{
				// Don't retry, this would start a new export! (or not?)
				// (no retry for a 202 means "handle as success").
				return std::make_pair(0.f, 0);
			}
			else return ITwinWebServices::Impl::defaultShouldRetryFunc(attempt, httpCode);
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
			double lens = 90.0;
			double focusDist = 0.0;
			std::array<double, 3> eye = { 0, 0, 0 };
		};
		struct ViewFlags
		{
			int renderMode = 6;
			std::optional<bool> noConstructions = false;
		};
		struct Color
		{
			int red;
			int green;
			int blue;
		};
		struct Sky
		{
			bool display = true;
			std::optional<bool> twoColor = true;
			Color skyColor = {222, 242, 255};
			Color groundColor = {240, 236, 232};
			Color zenithColor = skyColor;
			Color nadirColor = groundColor;
		};
		struct Environment
		{
			Sky sky;
		};
		struct DisplayStyle
		{
			std::optional<std::string> renderTimeline;
			std::optional<double> timePoint;
			//optional below for retro-compatibility with Synchro saved views created inside Carrot,
			//which only used to contain fields renderTimeline and timePoint.
			std::optional<ViewFlags> viewflags;
			std::optional<Environment> environment;
		};
		struct Models
		{
			std::vector<std::string> disabled;
		};
		struct Categories
		{
			std::vector<std::string> disabled;
		};
		struct Itwin3dView
		{
			std::array<double, 3> origin = { 0, 0, 0 };
			std::array<double, 3> extents = { 0, 0, 0 };
			Rotator angles;
			std::optional<CameraInfo> camera;
			//optional here in case users created saved views with the old version 
			//that didn't contain a displayStyle field
			std::optional<DisplayStyle> displayStyle;
			std::optional<Models> models;
			std::optional<Categories> categories;
		};
		struct EmphasizeElementsProps
		{
			std::optional<std::vector<std::string>> neverDrawn;
		};
		struct LegacyView
		{
			std::optional<EmphasizeElementsProps> emphasizeElementsProps;
		};
		struct SavedView3DData
		{
			Itwin3dView itwin3dView;
			std::optional<LegacyView> legacyView;
		};
		struct SavedViewFullInfo
		{
			std::string id;
			std::string displayName;
			bool shared = false;
			SavedView3DData savedViewData;
			std::vector<SavedViewExtensionsInfo> extensions;
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
				if (itwin3dView.categories)
					svData.savedView.hiddenCategories = itwin3dView.categories->disabled;
				if (itwin3dView.models)
					svData.savedView.hiddenModels = itwin3dView.models->disabled;
				if (fullInfo.savedViewData.legacyView)
				{
					LegacyView const& legacyView(fullInfo.savedViewData.legacyView.value());
					if (legacyView.emphasizeElementsProps)
						svData.savedView.hiddenElements = legacyView.emphasizeElementsProps->neverDrawn;
				}
				if (itwin3dView.displayStyle)
				{
					svData.savedView.displayStyle.emplace();
					if (itwin3dView.displayStyle->renderTimeline)
						svData.savedView.displayStyle->renderTimeline = itwin3dView.displayStyle->renderTimeline;
					if (itwin3dView.displayStyle->timePoint)
						svData.savedView.displayStyle->timePoint = itwin3dView.displayStyle->timePoint;
				}
				svData.savedViewInfo.id = std::move(fullInfo.id);
				svData.savedViewInfo.displayName = std::move(fullInfo.displayName);
				svData.savedViewInfo.shared = fullInfo.shared;
				svData.savedViewInfo.extensions = fullInfo.extensions;
			}
		};
	}

	void ITwinWebServices::GetAllSavedViews(std::string const& iTwinId, std::string const& iModelId, std::string const& groupId /*=""*/,
											int Top /*=100*/, int Skip /*=0*/)
	{
		std::string topSkip = std::string("&$top=") + std::to_string(Top) + std::string("&$skip=") + std::to_string(Skip);
		ITwinAPIRequestInfo requestInfo =
		{
			__func__,
			EVerb::Get,
			(!groupId.empty() ? std::string("/savedviews?groupId=") + groupId + topSkip : std::string("/savedviews?iTwinId=") + iTwinId + "&iModelId=" + iModelId + topSkip),
			"application/vnd.bentley.itwin-platform.v1+json",
			"application/json",
		};
		requestInfo.badlyFormed = iTwinId.empty() || iModelId.empty();

		impl_->ProcessHttpRequest(
			requestInfo,
			[this, groupId, iTwinId, iModelId](Http::Response const& response, RequestID const&, std::string& strError) -> bool
		{
			SavedViewInfos infos;
			bool const bResult = Http::IsSuccessful(response)
				&& Json::FromString(infos, response.second, strError);
			infos.groupId = groupId;
			infos.iTwinId = iTwinId;
			infos.iModelId = iModelId;
			if (impl_->observer_)
			{
				impl_->observer_->OnSavedViewInfosRetrieved(bResult, infos);
			}
			return bResult;
		});
	}

	void ITwinWebServices::GetSavedViewsGroups(std::string const& iTwinId, std::string const& iModelId)
	{
		ITwinAPIRequestInfo requestInfo =
		{
			__func__,
			EVerb::Get,
			std::string("/savedviews/groups?iTwinId=") + iTwinId + (iModelId.empty()? "" : ("&iModelId=" + iModelId)),
			"application/vnd.bentley.itwin-platform.v1+json",
			"application/json",
		};
		requestInfo.badlyFormed = iTwinId.empty();

		impl_->ProcessHttpRequest(
			requestInfo,
			[this, iModelId](Http::Response const& response, RequestID const&, std::string& strError) -> bool
		{
			SavedViewGroupInfos svGroupInfos;
			bool const bResult = Http::IsSuccessful(response)
				&& Json::FromString(svGroupInfos, response.second, strError);
			if (bResult)
				svGroupInfos.iModelId = iModelId;
			if (impl_->observer_)
			{
				impl_->observer_->OnSavedViewGroupInfosRetrieved(bResult, svGroupInfos);
			}
			return bResult;
		});
	}

	void ITwinWebServices::GetSavedView(std::string const& savedViewId)
	{
		ITwinAPIRequestInfo requestInfo =
		{
			__func__,
			EVerb::Get,
			std::string("/savedviews/") + savedViewId,
			"application/vnd.bentley.itwin-platform.v1+json",
			"application/json",
		};
		requestInfo.badlyFormed = savedViewId.empty();

		using SavedViewData = Detail::SavedViewData;

		impl_->ProcessHttpRequest(
			requestInfo,
			[this, savedViewId](Http::Response const& response, RequestID const&, std::string& strError) -> bool
		{
			SavedViewData svData;
			Detail::SavedViewFullInfoHolder svInfoHolder;
			bool const bResult = Http::IsSuccessful(response)
				&& Json::FromString(svInfoHolder, response.second, strError);
			if (bResult)
			{
				svInfoHolder.MoveToSavedViewData(svData);
				bool bGetExtension = false;
				std::vector<SavedViewExtensionsInfo> svExtensions = svInfoHolder.savedView.extensions;
				std::string extensionName = "EmphasizeElements";
				auto it = std::find_if(svExtensions.begin(), svExtensions.end(), [&](const SavedViewExtensionsInfo& ext) {
					return ext.extensionName == extensionName;
					});
				bGetExtension = it != svExtensions.end();
				if (bGetExtension)
				{
					//fire 2nd request to get potential hidden elements from EmphasizeElements extension
					ITwinAPIRequestInfo extensionRequestInfo =
					{
						__func__,
						EVerb::Get,
						std::string("/savedviews/") + savedViewId + std::string("/extensions/") + extensionName,
						"application/vnd.bentley.itwin-platform.v1+json",
						"application/json",
					};

					impl_->ProcessHttpRequest(
						extensionRequestInfo,
						[this, savedViewId, svData](Http::Response const& response2, RequestID const&, std::string& strError2) -> bool
						{
							struct ExtensionData
							{
								std::string data;
							};
							struct SavedViewExtension
							{
								ExtensionData extension;
							} extInfoHolder;

							Detail::LegacyView emphasizeElementsPropsHolder;
							bool const bResult2 = Http::IsSuccessful(response2)
								&& Json::FromString(extInfoHolder, response2.second, strError2)
								&& Json::FromString(emphasizeElementsPropsHolder, extInfoHolder.extension.data, strError2);
							//parse the data in extInfoHolder.extension.data and put it in svData.savedView directly
							SavedView savedView = svData.savedView;
							if (bResult2)
							{
								if (emphasizeElementsPropsHolder.emphasizeElementsProps)
								{
									savedView.hiddenElements = emphasizeElementsPropsHolder.emphasizeElementsProps->neverDrawn;
								}
							}
							if (impl_->observer_)
							{
								impl_->observer_->OnSavedViewRetrieved(bResult2, savedView, svData.savedViewInfo);
							}
							return bResult2;
						});
				}
				else
				{
					if (impl_->observer_)
					{
						impl_->observer_->OnSavedViewRetrieved(bResult, svData.savedView, svData.savedViewInfo);
					}
					return bResult;
				}
			}
			if (!bResult && impl_->observer_)
			{
				impl_->observer_->OnSavedViewRetrieved(bResult, svData.savedView, svData.savedViewInfo);
			}
			return bResult;
		});
	}

	void ITwinWebServices::GetSavedViewThumbnail(std::string const& savedViewId)
	{
		const ITwinAPIRequestInfo requestInfo = {
			__func__,
			EVerb::Get,
			std::string("/savedviews/") + savedViewId + std::string("/image"),
			"application/vnd.bentley.itwin-platform.v1+json",
			"application/json",
		};

		impl_->ProcessHttpRequest(
			requestInfo,
			[this, savedViewId](Http::Response const& response, RequestID const&, std::string& strError) -> bool
		{
			struct ThumbnailData
			{
				std::string href;
			} thumbnailInfoHolder;

			bool bResult = Http::IsSuccessful(response)
				&& Json::FromString(thumbnailInfoHolder, response.second, strError)
				&& !thumbnailInfoHolder.href.empty();
			if (bResult)
			{
				std::string const& thumbnailURL = thumbnailInfoHolder.href;

				// We retrieve a Blob URL in case the thumbnail has been updated using the saved views api
				// (typically if the saved view has been created inside AdvViz application or plugin)
				if (thumbnailURL.starts_with("https"))
				{
					// Make a second request to download the thumbnail
					const ITwinAPIRequestInfo thumbnailRequestInfo =
					{
						.ShortName			= "DownloadThumbnail",
						.Verb				= EVerb::Get,
						.UrlSuffix			= thumbnailURL,
						.AcceptHeader		= "application/vnd.bentley.itwin-platform.v1+json",
						.needRawData		= true,
						.discardAllHeaders	= true,
						.isFullUrl			= true
					};

					impl_->ProcessHttpRequest(
						thumbnailRequestInfo,
						[this, savedViewId]
						(Http::Response const& response2, RequestID const&, std::string& /*strError2*/) -> bool
					{
						bool const bResult2 = Http::IsSuccessful(response2)
							&& response2.rawdata_
							&& response2.rawdata_->size() > 0;
						if (impl_->observer_)
						{
							Http::RawData const emptyData;
							impl_->observer_->OnSavedViewThumbnailRetrieved(bResult2, savedViewId,
								response2.rawdata_ ? *response2.rawdata_ : emptyData);
						}
						return bResult2;
					});
				}
				// Otherwise, we retrieve a base64 encoded string in this format:
				//		("data:image/jpeg;base64,/9j/4AA...")
				// (this means the saved view has been created inside Pineapple/Design Review)
				else
				{
					Http::RawData buffer;
					const std::string_view base64Chunk = "base64,";
					auto const startPos = thumbnailURL.find(base64Chunk);
					bResult = (startPos != std::string::npos)
						&& http_->DecodeBase64(thumbnailURL.substr(startPos + base64Chunk.size()), buffer);
					if (!bResult)
					{
						BE_LOGE("ITwinAPI", "[SavedView] Failed decoding thumbnail from " << thumbnailURL);
					}
					if (bResult && impl_->observer_)
					{
						impl_->observer_->OnSavedViewThumbnailRetrieved(true, savedViewId, buffer);
					}
				}
			}
			// If the 1st request fails, directly notify the caller.
			if (!bResult && impl_->observer_)
			{
				impl_->observer_->OnSavedViewThumbnailRetrieved(false, savedViewId, {});
			}
			return bResult;
		});
	}

	void ITwinWebServices::UpdateSavedViewThumbnail(std::string const& savedViewId, std::string const& thumbnailURL)
	{
		const ITwinAPIRequestInfo requestInfo = {
			.ShortName		= __func__,
			.Verb			= EVerb::Put,
			.UrlSuffix		= std::string("/savedviews/") + savedViewId + std::string("/image"),
			.AcceptHeader	= "application/vnd.bentley.itwin-platform.v1+json",

			/*** additional settings for POST ***/
			.ContentType	= "application/json",
			.ContentString	= "{\"image\":\"" + thumbnailURL + "\"}"
		};
		
		impl_->ProcessHttpRequest(
			requestInfo,
			[this, savedViewId](Http::Response const& response, RequestID const& requestId, std::string& strError) -> bool
		{
			std::string outError;
			bool bResult = Http::IsDefined(response);
			if (bResult)
			{
				outError = GetErrorDescriptionFromJson(response.second);
				bResult = outError.empty();
			}

			// Here, the callbacks expects an error message (in case of failure)
			// => if none is provided, and if the last error recorded corresponds to our request, use the
			// latter as response.
			if (!bResult && outError.empty())
			{
				outError = GetRequestError(requestId);
			}
			if (impl_->observer_)
			{
				impl_->observer_->OnSavedViewThumbnailUpdated(bResult, savedViewId, outError);
			}
			strError = outError;
			return bResult;
		});
	}

	void ITwinWebServices::GetSavedViewExtension(std::string const& savedViewId, std::string const& extensionName)
	{
		ITwinAPIRequestInfo requestInfo =
		{
			__func__,
			EVerb::Get,
			std::string("/savedviews/") + savedViewId + std::string("/extensions/") + extensionName,
			"application/vnd.bentley.itwin-platform.v1+json",
			"application/json",
		};
		requestInfo.badlyFormed = savedViewId.empty() || extensionName.empty();

		impl_->ProcessHttpRequest(
			requestInfo,
			[this, savedViewId](Http::Response const& response, RequestID const&, std::string& strError) -> bool
		{
			struct ExtensionData
			{
				std::string data;
			};
			struct SavedViewExtension
			{
				ExtensionData extension;
			} extInfoHolder;

			bool const bResult = Http::IsSuccessful(response)
				&& Json::FromString(extInfoHolder, response.second, strError);
			if (impl_->observer_)
			{
				impl_->observer_->OnSavedViewExtensionRetrieved(bResult,
					savedViewId, extInfoHolder.extension.data);
			}
			return bResult;
		});
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
			std::optional<std::string> iModelId;
			SavedView3DData savedViewData;
			std::optional<std::string> groupId;
			std::string displayName;
			bool shared = true;
			std::vector<std::string> tagIds;
		};

		struct AddSavedViewGroupInfo
		{
			std::string iTwinId;
			std::optional<std::string> iModelId;
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
			itwin3dView.origin = savedView.frustumOrigin;
			itwin3dView.extents = savedView.extents;
			itwin3dView.angles = savedView.angles;
			itwin3dView.camera.emplace();
			itwin3dView.camera->eye = savedView.origin;
			itwin3dView.camera->focusDist = savedView.focusDist;
			itwin3dView.displayStyle.emplace();
			itwin3dView.displayStyle->viewflags.emplace();
			itwin3dView.displayStyle->environment.emplace();
			if (savedView.displayStyle)
			{
				if (!savedView.displayStyle->renderTimeline.value().empty())
				{
					itwin3dView.displayStyle->renderTimeline = savedView.displayStyle->renderTimeline;
					itwin3dView.displayStyle->timePoint = savedView.displayStyle->timePoint;
				}
			}
		}
	}

	void ITwinWebServices::AddSavedView(std::string const& iTwinId, SavedView const& savedView, 
										SavedViewInfo const& savedViewInfo, std::string const& iModelId/*=""*/, 
										std::string const& groupId/*=""*/)
	{
		Detail::AddSavedViewInfo addInfo;
		Detail::FillSavedViewEditInfo(addInfo, savedView, savedViewInfo);
		addInfo.iTwinId = iTwinId;
		if (!iModelId.empty())
			addInfo.iModelId = iModelId;
		if (!groupId.empty())
			addInfo.groupId = groupId;

		ITwinAPIRequestInfo const requestInfo =
		{
			.ShortName		= __func__,
			.Verb			= EVerb::Post,
			.UrlSuffix		= "/savedviews/",
			.AcceptHeader	= "application/vnd.bentley.itwin-platform.v1+json",

			/*** additional settings for POST ***/
			.ContentType	= "application/json",
			.ContentString	= Json::ToString(addInfo),

			.badlyFormed	= iTwinId.empty()
		};

		impl_->ProcessHttpRequest(
			requestInfo,
			[this](Http::Response const& response, RequestID const&, std::string& strError) -> bool
		{
			struct SavedViewInfoHolder
			{
				SavedViewInfo savedView;
			} svHolder;

			bool const bResult = Http::IsSuccessful(response)
				&& Json::FromString(svHolder, response.second, strError);
			this->OnSavedViewAdded(bResult, svHolder.savedView);
			return bResult;
		});
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
		if (!iModelId.empty())
			addInfo.iModelId = iModelId;
		addInfo.displayName = savedViewGroupInfo.displayName;
		addInfo.shared = savedViewGroupInfo.shared;

		ITwinAPIRequestInfo const requestInfo =
		{
			.ShortName		= __func__,
			.Verb			= EVerb::Post,
			.UrlSuffix		= "/savedviews/groups",
			.AcceptHeader	= "application/vnd.bentley.itwin-platform.v1+json",

			/*** additional settings for POST ***/
			.ContentType	= "application/json",
			.ContentString	= Json::ToString(addInfo),

			.badlyFormed	= iTwinId.empty()
		};

		impl_->ProcessHttpRequest(
			requestInfo,
			[this](Http::Response const& response, RequestID const&, std::string& strError) -> bool
		{
			struct GroupInfoHolder
			{
				SavedViewGroupInfo group;
			} groupHolder;
			bool const bResult = Http::IsSuccessful(response)
				&& Json::FromString(groupHolder, response.second, strError);
			if (impl_->observer_)
			{
				impl_->observer_->OnSavedViewGroupAdded(bResult, groupHolder.group);
			}
			return bResult;
		});
	}

	void ITwinWebServices::DeleteSavedView(std::string const& savedViewId)
	{
		ITwinAPIRequestInfo requestInfo = {
			__func__,
			EVerb::Delete,
			std::string("/savedviews/") + savedViewId,
			"application/vnd.bentley.itwin-platform.v1+json"
		};
		requestInfo.badlyFormed = savedViewId.empty();

		impl_->ProcessHttpRequest(
			requestInfo,
			[this, savedViewId](Http::Response const& response, RequestID const& requestId, std::string& strError) -> bool
		{
			std::string outError;
			bool bResult = Http::IsDefined(response);
			if (bResult)
			{
				outError = GetErrorDescriptionFromJson(response.second);
				bResult = outError.empty();
			}
			// Here the callback expects an error message (in case of failure)
			// => if none if provided, and if the last error recorded corresponds to our request, use the
			// latter as response.
			if (!bResult && outError.empty())
			{
				outError = GetRequestError(requestId);
			}
			this->OnSavedViewDeleted(bResult, savedViewId, outError);
			strError = outError;
			return bResult;
		});
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

		ITwinAPIRequestInfo const requestInfo =
		{
			.ShortName		= __func__,
			.Verb			= EVerb::Patch,
			.UrlSuffix		= std::string("/savedviews/") + savedViewInfo.id,
			.AcceptHeader	= "application/vnd.bentley.itwin-platform.v1+json",

			/*** additional settings for PATCH ***/
			.ContentType	= "application/json",
			.ContentString	= Json::ToString(editInfo),

			.badlyFormed	= savedViewInfo.id.empty()
		};

		impl_->ProcessHttpRequest(
			requestInfo,
			[this](Http::Response const& response, RequestID const&, std::string& strError) -> bool
		{
			Detail::SavedViewData editSVData;
			Detail::SavedViewFullInfoHolder svInfoHolder;
			bool const bResult = Http::IsSuccessful(response)
				&& Json::FromString(svInfoHolder, response.second, strError);
			if (bResult)
			{
				svInfoHolder.MoveToSavedViewData(editSVData);
			}
			if (impl_->observer_)
			{
				impl_->observer_->OnSavedViewEdited(bResult, editSVData.savedView, editSVData.savedViewInfo);
			}
			return bResult;
		});
	}


	void ITwinWebServices::GetRealityData(std::string const& iTwinId)
	{
		const ITwinAPIRequestInfo requestInfo =
		{
			.ShortName		= __func__,
			.Verb			= EVerb::Get,
			.UrlSuffix		= std::string("/reality-management/reality-data/?iTwinId=") + iTwinId + "&types=Cesium3DTiles&$top=100",
			.AcceptHeader	= "application/vnd.bentley.itwin-platform.v1+json",

			.CustomHeaders	=
			{
				{ "Prefer",	"return=minimal" },
				{ "types",	"Cesium3DTiles" },
			},

			.badlyFormed	= iTwinId.empty()
		};

		impl_->ProcessHttpRequest(
			requestInfo,
			[this](Http::Response const& response, RequestID const&, std::string& strError) -> bool
		{
			ITwinRealityDataInfos realityData;
			bool const bResult = Http::IsSuccessful(response)
				&& Json::FromString(realityData, response.second, strError);
			if (impl_->observer_)
			{
				impl_->observer_->OnRealityDataRetrieved(bResult, realityData);
			}
			return bResult;
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
			EVerb::Get,
			std::string("/reality-management/reality-data/") + realityDataId + "?iTwinId=" + iTwinId,
			"application/vnd.bentley.itwin-platform.v1+json"
		};
		requestInfo.badlyFormed = iTwinId.empty() || realityDataId.empty();

		impl_->ProcessHttpRequest(
			requestInfo,
			[this, iTwinId, realityDataId](Http::Response const& response1, RequestID const&, std::string& strError1) -> bool
		{
			ITwinRealityData3DInfo realityData3DInfo;
			realityData3DInfo.id = realityDataId;

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

			bool const bResult1 = response1.first >= 0
				&& Json::FromString(infoHolder, response1.second, strError1);
			if (!bResult1)
			{
				// the 1st request has failed
				if (impl_->observer_)
				{
					impl_->observer_->OnRealityData3DInfoRetrieved(false, realityData3DInfo);
				}
				return false;
			}

			realityData3DInfo.displayName = infoHolder.realityData.displayName;

			// Make a second request to retrieve mesh URL
			const ITwinAPIRequestInfo realDataRequestInfo =
			{
				"GetRealityData3DInfo-part2",
				EVerb::Get,
				std::string("/reality-management/reality-data/") + realityDataId + "/readaccess?iTwinId=" + iTwinId,
				"application/vnd.bentley.itwin-platform.v1+json"
			};

			impl_->ProcessHttpRequest(
				realDataRequestInfo,
				[this, realityData3DInfo, detailedInfo = infoHolder.realityData]
				(Http::Response const& response2, RequestID const&, std::string& strError2) -> bool
			{
				ITwinRealityData3DInfo finalRealityData3DInfo;
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

				bool const bResult2 = response2.first >= 0
					&& Json::FromString(linkInfo, response2.second, strError2);
				if (bResult2)
				{
					finalRealityData3DInfo.meshUrl = Detail::FormatRealityDataUrl(
						linkInfo._links.containerUrl.href,
						detailedInfo.rootDocument);
				}
				// This is for the 2nd request: broadcast final result
				if (impl_->observer_)
				{
					impl_->observer_->OnRealityData3DInfoRetrieved(bResult2, finalRealityData3DInfo);
				}
				return bResult2;
			});

			return true;
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
			AdvViz::SDK::ITwinElementProperties& outProps_;
			mutable PropParserData helper_;
			mutable std::stringstream error_;

			ElementPropertiesVisitor(AdvViz::SDK::ITwinElementProperties& elementProps)
				: outProps_(elementProps)
			{

			}

			// implement the 6 possible cases of the variant
			// bool, int, double, std::string, Object, Array
			// some are not used at all, since the response only deals with string primitives

			void operator()(const bool& /*boolValue*/) const
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
			void operator()(const std::nullopt_t&) const
			{
				error_ << "unhandled std::nullopt_t" << std::endl;
			}

			void operator()(const std::string& strValue) const
			{
				if (helper_.currentType_ == "primitive" ||
					helper_.arrayType_ == "primitive")
				{
					AdvViz::SDK::ITwinElementAttribute& attr = outProps_.properties.back().attributes.emplace_back();
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
							AdvViz::SDK::ITwinElementProperty& newProp = outProps_.properties.emplace_back();
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

	namespace
	{
	//! When sending an "iModel RPC" request for an iModel without any changeset
	//! (ie. iModel having just a baseline file), we should pass "0" in the URL.
	std::string GetIModelRpcUrlChangeset(const std::string& rawChangesetId)
	{
		if (rawChangesetId.empty())
			return "0";
		return rawChangesetId;
	}
	} // unnamed namespace

	void ITwinWebServices::GetElementProperties(
		std::string const& iTwinId, std::string const& iModelId,
		std::string const& changesetId, std::string const& elementId)
	{
		std::string const key = fmt::format("{}:{}", iModelId, changesetId);

		ITwinAPIRequestInfo const requestInfo =
		{
			.ShortName		= __func__,
			.Verb			= EVerb::Post,
			.UrlSuffix		= std::string("/imodel/rpc/v4/mode/1/context/") + iTwinId
								+ "/imodel/" + iModelId
								+ "/changeset/" + GetIModelRpcUrlChangeset(changesetId)
								+ "/PresentationRpcInterface-4.1.0-getElementProperties",
			.AcceptHeader	= "application/vnd.bentley.itwin-platform.v1+json",

			/*** additional settings for POST ***/
			.ContentType	= "text/plain",
			.ContentString	= "[{\"key\":\"" + key
								+ "\",\"iTwinId\":\"" + iTwinId
								+ "\",\"iModelId\":\"" + iModelId
								+ "\",\"changeset\":{\"id\":\"" + changesetId
								+ "\"}},{\"elementId\":\"" + elementId
								+ "\"}]",

			.badlyFormed	= iTwinId.empty() || iModelId.empty() || elementId.empty()
		};

		impl_->ProcessHttpRequest(
			requestInfo,
			[this, elementId](Http::Response const& response, RequestID const&, std::string& strError) -> bool
		{
			ITwinElementProperties elementProps;

			struct ItemsHolder { rfl::Generic items; };
			struct ResultHolder { ItemsHolder result; };
			ResultHolder res;
			bool bResult = Http::IsSuccessful(response)
				&& Json::FromString(res, response.second, strError);
			if (bResult)
			{
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
				bResult &= strError.empty();
			}
			if (impl_->observer_)
			{
				impl_->observer_->OnElementPropertiesRetrieved(bResult, elementProps, elementId);
			}
			return bResult;
		});
	}

	void ITwinWebServices::GetPagedNodes(std::string const& iTwinId, std::string const& iModelId,
		std::string const& changesetId, std::string const& parentKey /*= ""*/, int offset /*= 0*/, int count /*= 1000*/)
	{
		std::string const key = fmt::format("{}:{}", iModelId, changesetId);

		ITwinAPIRequestInfo const requestInfo =
		{
			.ShortName = __func__,
			.Verb = EVerb::Post,
			.UrlSuffix = std::string("/imodel/rpc/v4/mode/1/context/") + iTwinId
								+ "/imodel/" + iModelId
								+ "/changeset/" + GetIModelRpcUrlChangeset(changesetId)
								+ "/PresentationRpcInterface-4.1.0-getPagedNodes",
			.AcceptHeader = "application/vnd.bentley.itwin-platform.v1+json",

			/*** additional settings for POST ***/
			.ContentType = "text/plain",
			.ContentString = "[{\"key\":\"" + key
								+ "\",\"iTwinId\":\"" + iTwinId
								+ "\",\"iModelId\":\"" + iModelId
								+ "\",\"changeset\":{\"id\":\"" + changesetId
								+ "\"}},{" + (!parentKey.empty()? "\"parentKey\":" + parentKey + "," : "")
								+ "\"clientId\":\"" + ITwinAuthManager::GetAppID(EITwinEnvironment::Prod)
								+ "\",\"locale\":\"en\",\"unitSystem\":\"metric\",\"rulesetOrId\":{\"id\":\"tree-widget-react/ModelsTree\","
								+ "\"requiredSchemas\":[{\"name\":\"BisCore\"}],\"rules\":[{\"ruleType\":\"RootNodes\",\"autoExpand\":true,"
								+ "\"specifications\":[{\"specType\":\"InstanceNodesOfSpecificClasses\",\"classes\":[{\"schemaName\":\"BisCore\","
								+ "\"classNames\":[\"Subject\"]}],\"instanceFilter\":\"this.Parent = NULL\",\"groupByClass\":false,\"groupByLabel\":false}],"
								+ "\"customizationRules\":[{\"ruleType\":\"ExtendedData\",\"items\":{\"isSubject\":\"true\",\"icon\":\"\\\"icon-imodel-hollow-2\\\"\"}}]},"
								+ "{\"ruleType\":\"ChildNodes\",\"condition\":\"ParentNode.IsOfClass(\\\"Subject\\\", \\\"BisCore\\\")\",\"specifications\":"
								+ "[{\"specType\":\"RelatedInstanceNodes\",\"relationshipPaths\":[{\"relationship\":{\"schemaName\":\"BisCore\",\"className\":"
								+ "\"SubjectOwnsSubjects\"},\"direction\":\"Forward\",\"targetClass\":{\"schemaName\":\"BisCore\",\"className\":\"Subject\"}}],"
								+ "\"instanceFilter\":\"json_extract(this.JsonProperties, \\\"$.Subject.Job.Bridge\\\") <> NULL OR ifnull(json_extract(this.JsonProperties,"
								+ " \\\"$.Subject.Model.Type\\\"), \\\"\\\") = \\\"Hierarchy\\\"\",\"hideNodesInHierarchy\":true,\"groupByClass\":false,\"groupByLabel\":false},"
								+ "{\"specType\":\"RelatedInstanceNodes\",\"relationshipPaths\":[{\"relationship\":{\"schemaName\":\"BisCore\",\"className\":"
								+ "\"SubjectOwnsSubjects\"},\"direction\":\"Forward\",\"targetClass\":{\"schemaName\":\"BisCore\",\"className\":\"Subject\"}}],"
								+ "\"instanceFilter\":\"json_extract(this.JsonProperties, \\\"$.Subject.Job.Bridge\\\") = NULL AND ifnull(json_extract(this.JsonProperties,"
								+ " \\\"$.Subject.Model.Type\\\"), \\\"\\\") <> \\\"Hierarchy\\\"\",\"hideIfNoChildren\":true,\"groupByClass\":false,\"groupByLabel\":false}],"
								+ "\"customizationRules\":[{\"ruleType\":\"ExtendedData\",\"items\":{\"isSubject\":\"true\",\"icon\":\"\\\"icon-folder\\\"\"}}]},"
								+ "{\"ruleType\":\"ChildNodes\",\"condition\":\"ParentNode.IsOfClass(\\\"Subject\\\", \\\"BisCore\\\")\",\"specifications\":"
								+ "[{\"specType\":\"InstanceNodesOfSpecificClasses\",\"classes\":{\"schemaName\":\"BisCore\",\"classNames\":[\"GeometricModel3d\"],"
								+ "\"arePolymorphic\":true},\"relatedInstances\":[{\"relationshipPath\":{\"relationship\":{\"schemaName\":\"BisCore\",\"className\":"
								+ "\"ModelModelsElement\"},\"direction\":\"Forward\",\"targetClass\":{\"schemaName\":\"BisCore\",\"className\":\"InformationPartitionElement\"}},"
								+ "\"alias\":\"partition\",\"isRequired\":true}],\"instanceFilter\":\"(parent.ECInstanceId = partition.Parent.Id OR "
								+ "json_extract(parent.JsonProperties, \\\"$.Subject.Model.TargetPartition\\\") = printf(\\\"0x%x\\\", partition.ECInstanceId)) "
								+ "AND NOT this.IsPrivate AND json_extract(partition.JsonProperties, \\\"$.PhysicalPartition.Model.Content\\\") = NULL AND "
								+ "json_extract(partition.JsonProperties, \\\"$.GraphicalPartition3d.Model.Content\\\") = NULL AND "
								+ "this.HasRelatedInstance(\\\"BisCore:ModelContainsElements\\\", \\\"Forward\\\", \\\"BisCore:GeometricElement3d\\\")\","
								+ "\"hasChildren\":\"Always\",\"groupByClass\":false,\"groupByLabel\":false},{\"specType\":\"InstanceNodesOfSpecificClasses\","
								+ "\"classes\":{\"schemaName\":\"BisCore\",\"classNames\":[\"GeometricModel3d\"],\"arePolymorphic\":true},\"relatedInstances\":"
								+ "[{\"relationshipPath\":{\"relationship\":{\"schemaName\":\"BisCore\",\"className\":\"ModelModelsElement\"},\"direction\":\"Forward\","
								+ "\"targetClass\":{\"schemaName\":\"BisCore\",\"className\":\"InformationPartitionElement\"}},\"alias\":\"partition\",\"isRequired\":true}],"
								+ "\"instanceFilter\":\"(parent.ECInstanceId = partition.Parent.Id OR json_extract(parent.JsonProperties, \\\"$.Subject.Model.TargetPartition\\\")"
								+ " = printf(\\\"0x%x\\\", partition.ECInstanceId)) AND NOT this.IsPrivate AND (json_extract(partition.JsonProperties,"
								+ " \\\"$.PhysicalPartition.Model.Content\\\") <> NULL OR json_extract(partition.JsonProperties, \\\"$.GraphicalPartition3d.Model.Content\\\")"
								+ " <> NULL) AND this.HasRelatedInstance(\\\"BisCore:ModelContainsElements\\\", \\\"Forward\\\", \\\"BisCore:GeometricElement3d\\\")\","
								+ "\"hasChildren\":\"Always\",\"hideNodesInHierarchy\":true,\"groupByClass\":false,\"groupByLabel\":false}],\"customizationRules\":"
								+ "[{\"ruleType\":\"ExtendedData\",\"items\":{\"isModel\":\"true\",\"icon\":\"\\\"icon-model\\\"\"}}]},{\"ruleType\":\"ChildNodes\","
								+ "\"condition\":\"ParentNode.IsOfClass(\\\"ISubModeledElement\\\", \\\"BisCore\\\")\",\"specifications\":[{\"specType\":\"RelatedInstanceNodes\","
								+ "\"relationshipPaths\":[{\"relationship\":{\"schemaName\":\"BisCore\",\"className\":\"ModelModelsElement\"},\"direction\":\"Backward\"}],"
								+ "\"instanceFilter\":\"NOT this.IsPrivate AND this.HasRelatedInstance(\\\"BisCore:ModelContainsElements\\\", \\\"Forward\\\", "
								+ "\\\"BisCore:GeometricElement3d\\\")\",\"hideNodesInHierarchy\":true,\"groupByClass\":false,\"groupByLabel\":false}],\"customizationRules\":"
								+ "[{\"ruleType\":\"ExtendedData\",\"items\":{\"isModel\":\"true\",\"icon\":\"\\\"icon-model\\\"\"}}]},{\"ruleType\":\"ChildNodes\",\"condition\":"
								+ "\"ParentNode.IsOfClass(\\\"GeometricModel3d\\\", \\\"BisCore\\\")\",\"specifications\":[{\"specType\":\"RelatedInstanceNodes\","
								+ "\"relationshipPaths\":[[{\"relationship\":{\"schemaName\":\"BisCore\",\"className\":\"ModelContainsElements\"},\"direction\":\"Forward\","
								+ "\"targetClass\":{\"schemaName\":\"BisCore\",\"className\":\"GeometricElement3d\"}},{\"relationship\":{\"schemaName\":\"BisCore\",\"className\":"
								+ "\"GeometricElement3dIsInCategory\"},\"direction\":\"Forward\"}]],\"instanceFilter\":\"NOT this.IsPrivate\",\"suppressSimilarAncestorsCheck\":"
								+ "true,\"hideIfNoChildren\":true,\"groupByClass\":false,\"groupByLabel\":false}],\"customizationRules\":[{\"ruleType\":\"ExtendedData\",\"items\""
								+ ":{\"isCategory\":\"true\",\"modelId\":\"ParentNode.InstanceId\",\"icon\":\"\\\"icon-layers\\\"\"}}]},{\"ruleType\":\"ChildNodes\",\"condition\":"
								+ "\"ParentNode.IsOfClass(\\\"SpatialCategory\\\", \\\"BisCore\\\")\",\"specifications\":[{\"specType\":\"RelatedInstanceNodes\","
								+ "\"relationshipPaths\":[{\"relationship\":{\"schemaName\":\"BisCore\",\"className\":\"GeometricElement3dIsInCategory\"},\"direction\":"
								+ "\"Backward\",\"targetClass\":{\"schemaName\":\"BisCore\",\"className\":\"GeometricElement3d\"}}],\"instanceFilter\":\"this.Model.Id = "
								+ "parent.parent.ECInstanceId ANDALSO this.Parent = NULL\",\"groupByClass\":false,\"groupByLabel\":false}],\"customizationRules\":[{\"ruleType\":"
								+ "\"ExtendedData\",\"items\":{\"modelId\":\"this.Model.Id\",\"categoryId\":\"this.Category.Id\",\"icon\":\"\\\"icon-item\\\"\",\"groupIcon\":"
								+ "\"\\\"icon-ec-class\\\"\"}}]},{\"ruleType\":\"ChildNodes\",\"condition\":\"ParentNode.IsOfClass(\\\"GeometricElement3d\\\", \\\"BisCore\\\")\","
								+ "\"specifications\":[{\"specType\":\"RelatedInstanceNodes\",\"relationshipPaths\":[{\"relationship\":{\"schemaName\":\"BisCore\",\"className\":"
								+ "\"ElementOwnsChildElements\"},\"direction\":\"Forward\",\"targetClass\":{\"schemaName\":\"BisCore\",\"className\":\"GeometricElement3d\"}}],"
								+ "\"groupByClass\":false,\"groupByLabel\":false}],\"customizationRules\":[{\"ruleType\":\"ExtendedData\",\"items\":{\"modelId\":\"this.Model.Id\","
								+ "\"categoryId\":\"this.Category.Id\",\"icon\":\"\\\"icon-item\\\"\",\"groupIcon\":\"\\\"icon-ec-class\\\"\"}}]},{\"ruleType\":\"Grouping\","
								+ "\"class\":{\"schemaName\":\"BisCore\",\"className\":\"Subject\"},\"groups\":[{\"specType\":\"SameLabelInstance\",\"applicationStage\":"
								+ "\"PostProcess\"}]},{\"ruleType\":\"Grouping\",\"class\":{\"schemaName\":\"BisCore\",\"className\":\"SpatialCategory\"},\"groups\":[{\"specType\":"
								+ "\"SameLabelInstance\",\"applicationStage\":\"PostProcess\"}]}]},\"paging\":{\"start\":" + std::to_string(offset) + ",\"size\":" 
								+ std::to_string(count) + "},\"rulesetVariables\":[]}]",
			.badlyFormed = iTwinId.empty() || iModelId.empty()
		};

		impl_->ProcessHttpRequest(
			requestInfo,
			[this](Http::Response const& response, RequestID const&, std::string& strError) -> bool
			{
				IModelPagedNodesRes iModelProps;
				bool const bResult = Http::IsSuccessful(response)
					&& Json::FromString(iModelProps, response.second, strError);
				if (impl_->observer_)
				{
					impl_->observer_->OnIModelPagedNodesRetrieved(bResult, iModelProps);
				}
				return bResult;
			});
	}

	void ITwinWebServices::GetModelFilteredNodePaths(std::string const& iTwinId, std::string const& iModelId,
		std::string const& changesetId, std::string const& filter)
	{
		std::string const key = fmt::format("{}:{}", iModelId, changesetId);

		ITwinAPIRequestInfo const requestInfo =
		{
			.ShortName = __func__,
			.Verb = EVerb::Post,
			.UrlSuffix = std::string("/imodel/rpc/v4/mode/1/context/") + iTwinId
								+ "/imodel/" + iModelId
								+ "/changeset/" + GetIModelRpcUrlChangeset(changesetId)
								+ "/PresentationRpcInterface-4.1.0-getFilteredNodePaths",
			.AcceptHeader = "application/vnd.bentley.itwin-platform.v1+json",

			/*** additional settings for POST ***/
			.ContentType = "text/plain",
			.ContentString = "[{\"key\":\"" + key
								+ "\",\"iTwinId\":\"" + iTwinId
								+ "\",\"iModelId\":\"" + iModelId
								+ "\",\"changeset\":{\"id\":\"" + changesetId
								+ "\"}},{"
								+ "\"clientId\":\"" + ITwinAuthManager::GetAppID(EITwinEnvironment::Prod)
								+ "\",\"locale\":\"en\",\"unitSystem\":\"metric\",\"rulesetOrId\":{\"id\":\"tree-widget-react/ModelsTreeSearch\",\"rules\":[{\"ruleType\":"
								+ "\"RootNodes\",\"specifications\":[{\"specType\":\"InstanceNodesOfSpecificClasses\",\"classes\":[{\"schemaName\":\"BisCore\",\"classNames\":"
								+ "[\"Subject\"]}],\"instanceFilter\":\"this.Parent = NULL\",\"groupByClass\":false,\"groupByLabel\":false}],\"customizationRules\":[{\"ruleType\":"
								+ "\"ExtendedData\",\"items\":{\"isSubject\":\"true\",\"icon\":\"\\\"icon-imodel-hollow-2\\\"\"}}]},{\"ruleType\":\"ChildNodes\",\"condition\":"
								+ "\"ParentNode.IsOfClass(\\\"Subject\\\", \\\"BisCore\\\")\",\"specifications\":[{\"specType\":\"RelatedInstanceNodes\",\"relationshipPaths\":"
								+ "[{\"relationship\":{\"schemaName\":\"BisCore\",\"className\":\"SubjectOwnsSubjects\"},\"direction\":\"Forward\",\"targetClass\":{\"schemaName\":"
								+ "\"BisCore\",\"className\":\"Subject\"}}],\"instanceFilter\":\"json_extract(this.JsonProperties, \\\"$.Subject.Job.Bridge\\\") <> NULL OR ifnull("
								+ "json_extract(this.JsonProperties, \\\"$.Subject.Model.Type\\\"), \\\"\\\") = \\\"Hierarchy\\\"\",\"hideNodesInHierarchy\":true,\"groupByClass\":"
								+ "false,\"groupByLabel\":false},{\"specType\":\"RelatedInstanceNodes\",\"relationshipPaths\":[{\"relationship\":{\"schemaName\":\"BisCore\","
								+ "\"className\":\"SubjectOwnsSubjects\"},\"direction\":\"Forward\",\"targetClass\":{\"schemaName\":\"BisCore\",\"className\":\"Subject\"}}],"
								+ "\"instanceFilter\":\"json_extract(this.JsonProperties, \\\"$.Subject.Job.Bridge\\\") = NULL AND ifnull(json_extract(this.JsonProperties, "
								+ "\\\"$.Subject.Model.Type\\\"), \\\"\\\") <> \\\"Hierarchy\\\"\",\"groupByClass\":false,\"groupByLabel\":false,\"hideExpression\":"
								+ "\"NOT ThisNode.HasChildren ANDALSO NOT ThisNode.ChildrenArtifacts.AnyMatches(x => x.isContentModel)\"}],\"customizationRules\":[{\"ruleType\":"
								+ "\"ExtendedData\",\"items\":{\"isSubject\":\"true\",\"icon\":\"\\\"icon-folder\\\"\"}},{\"ruleType\":\"Grouping\",\"class\":{\"schemaName\":"
								+ "\"BisCore\",\"className\":\"Subject\"},\"groups\":[{\"specType\":\"SameLabelInstance\",\"applicationStage\":\"PostProcess\"}]}]},{\"ruleType\":"
								+ "\"ChildNodes\",\"condition\":\"ParentNode.IsOfClass(\\\"Subject\\\", \\\"BisCore\\\")\",\"specifications\":[{\"specType\":"
								+ "\"InstanceNodesOfSpecificClasses\",\"classes\":{\"schemaName\":\"BisCore\",\"classNames\":[\"GeometricModel3d\"],\"arePolymorphic\":true},"
								+ "\"relatedInstances\":[{\"relationshipPath\":{\"relationship\":{\"schemaName\":\"BisCore\",\"className\":\"ModelModelsElement\"},\"direction\":"
								+ "\"Forward\",\"targetClass\":{\"schemaName\":\"BisCore\",\"className\":\"InformationPartitionElement\"}},\"alias\":\"partition\",\"isRequired\":"
								+ "true}],\"instanceFilter\":\"(parent.ECInstanceId = partition.Parent.Id OR json_extract(parent.JsonProperties, "
								+ "\\\"$.Subject.Model.TargetPartition\\\") = printf(\\\"0x%x\\\", partition.ECInstanceId)) AND NOT this.IsPrivate AND "
								+ "json_extract(partition.JsonProperties, \\\"$.PhysicalPartition.Model.Content\\\") = NULL AND json_extract(partition.JsonProperties, "
								+ "\\\"$.GraphicalPartition3d.Model.Content\\\") = NULL AND this.HasRelatedInstance(\\\"BisCore:ModelContainsElements\\\", \\\"Forward\\\", "
								+ "\\\"BisCore:GeometricElement3d\\\")\",\"hasChildren\":\"Unknown\",\"groupByClass\":false,\"groupByLabel\":false}],\"customizationRules\":"
								+ "[{\"ruleType\":\"ExtendedData\",\"items\":{\"isModel\":\"true\",\"icon\":\"\\\"icon-model\\\"\"}}]},{\"ruleType\":\"ChildNodes\",\"condition\":"
								+ "\"ParentNode.IsOfClass(\\\"Subject\\\", \\\"BisCore\\\")\",\"specifications\":[{\"specType\":\"InstanceNodesOfSpecificClasses\",\"classes\":"
								+ "{\"schemaName\":\"BisCore\",\"classNames\":[\"GeometricModel3d\"],\"arePolymorphic\":true},\"relatedInstances\":[{\"relationshipPath\":"
								+ "{\"relationship\":{\"schemaName\":\"BisCore\",\"className\":\"ModelModelsElement\"},\"direction\":\"Forward\",\"targetClass\":{\"schemaName\":"
								+ "\"BisCore\",\"className\":\"InformationPartitionElement\"}},\"alias\":\"partition\",\"isRequired\":true}],\"instanceFilter\":"
								+ "\"(parent.ECInstanceId = partition.Parent.Id OR json_extract(parent.JsonProperties, \\\"$.Subject.Model.TargetPartition\\\") = "
								+ "printf(\\\"0x%x\\\", partition.ECInstanceId)) AND NOT this.IsPrivate AND (json_extract(partition.JsonProperties, "
								+ "\\\"$.PhysicalPartition.Model.Content\\\") <> NULL OR json_extract(partition.JsonProperties, \\\"$.GraphicalPartition3d.Model.Content\\\") "
								+ "<> NULL) AND this.HasRelatedInstance(\\\"BisCore:ModelContainsElements\\\", \\\"Forward\\\", \\\"BisCore:GeometricElement3d\\\")\","
								+ "\"hasChildren\":\"Unknown\",\"hideNodesInHierarchy\":true,\"groupByClass\":false,\"groupByLabel\":false}],\"customizationRules\":[{\"ruleType\":"
								+ "\"NodeArtifacts\",\"items\":{\"isContentModel\":\"true\"}},{\"ruleType\":\"ExtendedData\",\"items\":{\"isModel\":\"true\",\"icon\":"
								+ "\"\\\"icon-model\\\"\"}}]},{\"ruleType\":\"ChildNodes\",\"condition\":\"ParentNode.IsOfClass(\\\"GeometricModel3d\\\", \\\"BisCore\\\")\","
								+ "\"specifications\":[{\"specType\":\"RelatedInstanceNodes\",\"relationshipPaths\":[{\"relationship\":{\"schemaName\":\"BisCore\",\"className\":"
								+ "\"ModelOwnsSubModel\"},\"direction\":\"Forward\",\"targetClass\":{\"schemaName\":\"BisCore\",\"className\":\"GeometricModel3d\"}}],"
								+ "\"instanceFilter\":\"NOT this.IsPrivate AND this.HasRelatedInstance(\\\"BisCore:ModelContainsElements\\\", \\\"Forward\\\", "
								+ "\\\"BisCore:GeometricElement3d\\\")\",\"groupByClass\":false,\"groupByLabel\":false}],\"customizationRules\":[{\"ruleType\":\"ExtendedData\","
								+ "\"items\":{\"isModel\":\"true\",\"icon\":\"\\\"icon-model\\\"\"}}]}]},\"filterText\":\"" + filter + "\",\"rulesetVariables\":[]}]",
			.badlyFormed = iTwinId.empty() || iModelId.empty()
		};

		impl_->ProcessHttpRequest(
			requestInfo,
			[this, filter](Http::Response const& response, RequestID const&, std::string& strError) -> bool
			{
				FilteredNodesRes Nodes;
				bool const bResult = Http::IsSuccessful(response)
					&& Json::FromString(Nodes, response.second, strError);
				if (impl_->observer_)
				{
					impl_->observer_->OnModelFilteredNodesRetrieved(bResult, Nodes, filter);
				}
				return bResult;
			});
	}

	void ITwinWebServices::GetCategoryFilteredNodePaths(std::string const& iTwinId, std::string const& iModelId, std::string const& changesetId, std::string const& filter)
	{
		std::string const key = fmt::format("{}:{}", iModelId, changesetId);

		ITwinAPIRequestInfo const requestInfo =
		{
			.ShortName = __func__,
			.Verb = EVerb::Post,
			.UrlSuffix = std::string("/imodel/rpc/v4/mode/1/context/") + iTwinId
								+ "/imodel/" + iModelId
								+ "/changeset/" + GetIModelRpcUrlChangeset(changesetId)
								+ "/PresentationRpcInterface-4.1.0-getFilteredNodePaths",
			.AcceptHeader = "application/vnd.bentley.itwin-platform.v1+json",

			/*** additional settings for POST ***/
			.ContentType = "text/plain",
			.ContentString = "[{\"key\":\"" + key
								+ "\",\"iTwinId\":\"" + iTwinId
								+ "\",\"iModelId\":\"" + iModelId
								+ "\",\"changeset\":{\"id\":\"" + changesetId
								+ "\"}},{"
								+ "\"clientId\":\"" + ITwinAuthManager::GetAppID(EITwinEnvironment::Prod)
								+ "\",\"locale\":\"en\",\"unitSystem\":\"metric\",\"rulesetOrId\":{\"id\":\"tree-widget-react/CategoriesTree\",\"rules\":[{\"ruleType\":"
								+ "\"RootNodes\",\"subConditions\":[{\"condition\":\"GetVariableStringValue(\\\"ViewType\\\") = \\\"3d\\\"\",\"specifications\":[{\"specType\":"
								+ "\"InstanceNodesOfSpecificClasses\",\"classes\":[{\"schemaName\":\"BisCore\",\"classNames\":[\"SpatialCategory\"],\"arePolymorphic\":true}],"
								+ "\"relatedInstances\":[{\"relationshipPath\":{\"relationship\":{\"schemaName\":\"BisCore\",\"className\":\"ModelContainsElements\"},"
								+ "\"direction\":\"Backward\"},\"isRequired\":true,\"alias\":\"model\"}],\"instanceFilter\":\"NOT this.IsPrivate AND (NOT model.IsPrivate OR "
								+ "model.IsOfClass(\\\"DictionaryModel\\\", \\\"BisCore\\\")) AND this.HasRelatedInstance(\\\"BisCore:GeometricElement3dIsInCategory\\\", "
								+ "\\\"Backward\\\", \\\"BisCore:Element\\\")\",\"groupByClass\":false,\"groupByLabel\":false}]},{\"condition\":\"GetVariableStringValue"
								+ "(\\\"ViewType\\\") = \\\"2d\\\"\",\"specifications\":[{\"specType\":\"InstanceNodesOfSpecificClasses\",\"classes\":[{\"schemaName\":\"BisCore\","
								+ "\"classNames\":[\"DrawingCategory\"],\"arePolymorphic\":true}],\"relatedInstances\":[{\"relationshipPath\":{\"relationship\":{\"schemaName\":"
								+ "\"BisCore\",\"className\":\"ModelContainsElements\"},\"direction\":\"Backward\"},\"isRequired\":true,\"alias\":\"model\"}],\"instanceFilter\":"
								+ "\"NOT this.IsPrivate AND (NOT model.IsPrivate OR model.IsOfClass(\\\"DictionaryModel\\\", \\\"BisCore\\\")) AND this.HasRelatedInstance"
								+ "(\\\"BisCore:GeometricElement2dIsInCategory\\\", \\\"Backward\\\", \\\"BisCore:Element\\\")\",\"groupByClass\":false,\"groupByLabel\":false}]}]},"
								+ "{\"ruleType\":\"ChildNodes\",\"condition\":\"ParentNode.IsOfClass(\\\"Category\\\", \\\"BisCore\\\") ANDALSO ParentNode.ECInstance."
								+ "GetRelatedInstancesCount(\\\"BisCore:CategoryOwnsSubCategories\\\", \\\"Forward\\\", \\\"BisCore:SubCategory\\\") > 1\",\"specifications\":"
								+ "[{\"specType\":\"RelatedInstanceNodes\",\"relationshipPaths\":[{\"relationship\":{\"schemaName\":\"BisCore\",\"className\":"
								+ "\"CategoryOwnsSubCategories\"},\"direction\":\"Forward\"}],\"instanceFilter\":\"NOT this.IsPrivate\",\"groupByClass\":false,\"groupByLabel\":"
								+ "false}]},{\"ruleType\":\"LabelOverride\",\"condition\":\"this.IsOfClass(\\\"Category\\\", \\\"BisCore\\\")\",\"description\":"
								+ "\"this.Description\"}],\"default\":{\"$schema\":\"../../../../node_modules/@itwin/presentation-common/Ruleset.schema.json\",\"id\":"
								+ "\"tree-widget-react/CategoriesTree\",\"rules\":[{\"ruleType\":\"RootNodes\",\"subConditions\":[{\"condition\":\"GetVariableStringValue"
								+ "(\\\"ViewType\\\") = \\\"3d\\\"\",\"specifications\":[{\"specType\":\"InstanceNodesOfSpecificClasses\",\"classes\":[{\"schemaName\":\"BisCore\","
								+ "\"classNames\":[\"SpatialCategory\"],\"arePolymorphic\":true}],\"relatedInstances\":[{\"relationshipPath\":{\"relationship\":{\"schemaName\":"
								+ "\"BisCore\",\"className\":\"ModelContainsElements\"},\"direction\":\"Backward\"},\"isRequired\":true,\"alias\":\"model\"}],\"instanceFilter\":"
								+ "\"NOT this.IsPrivate AND (NOT model.IsPrivate OR model.IsOfClass(\\\"DictionaryModel\\\", \\\"BisCore\\\")) AND this.HasRelatedInstance"
								+ "(\\\"BisCore:GeometricElement3dIsInCategory\\\", \\\"Backward\\\", \\\"BisCore:Element\\\")\",\"groupByClass\":false,\"groupByLabel\":false}]},"
								+ "{\"condition\":\"GetVariableStringValue(\\\"ViewType\\\") = \\\"2d\\\"\",\"specifications\":[{\"specType\":\"InstanceNodesOfSpecificClasses\","
								+ "\"classes\":[{\"schemaName\":\"BisCore\",\"classNames\":[\"DrawingCategory\"],\"arePolymorphic\":true}],\"relatedInstances\":"
								+ "[{\"relationshipPath\":{\"relationship\":{\"schemaName\":\"BisCore\",\"className\":\"ModelContainsElements\"},\"direction\":\"Backward\"},"
								+ "\"isRequired\":true,\"alias\":\"model\"}],\"instanceFilter\":\"NOT this.IsPrivate AND (NOT model.IsPrivate OR model.IsOfClass"
								+ "(\\\"DictionaryModel\\\", \\\"BisCore\\\")) AND this.HasRelatedInstance(\\\"BisCore:GeometricElement2dIsInCategory\\\", \\\"Backward\\\", "
								+ "\\\"BisCore:Element\\\")\",\"groupByClass\":false,\"groupByLabel\":false}]}]},{\"ruleType\":\"ChildNodes\",\"condition\":\"ParentNode.IsOfClass"
								+ "(\\\"Category\\\", \\\"BisCore\\\") ANDALSO ParentNode.ECInstance.GetRelatedInstancesCount(\\\"BisCore:CategoryOwnsSubCategories\\\", "
								+ "\\\"Forward\\\", \\\"BisCore:SubCategory\\\") > 1\",\"specifications\":[{\"specType\":\"RelatedInstanceNodes\",\"relationshipPaths\":"
								+ "[{\"relationship\":{\"schemaName\":\"BisCore\",\"className\":\"CategoryOwnsSubCategories\"},\"direction\":\"Forward\"}],\"instanceFilter\":"
								+ "\"NOT this.IsPrivate\",\"groupByClass\":false,\"groupByLabel\":false}]},{\"ruleType\":\"LabelOverride\",\"condition\":\"this.IsOfClass"
								+ "(\\\"Category\\\", \\\"BisCore\\\")\",\"description\":\"this.Description\"}]}},\"filterText\":\"" + filter 
								+ "\",\"rulesetVariables\":[{\"id\":\"ViewType\",\"type\":\"string\",\"value\":\"3d\"}]}]",
			.badlyFormed = iTwinId.empty() || iModelId.empty()
		};

		impl_->ProcessHttpRequest(
			requestInfo,
			[this, filter](Http::Response const& response, RequestID const&, std::string& strError) -> bool
			{
				FilteredNodesRes Nodes;
				bool const bResult = Http::IsSuccessful(response)
					&& Json::FromString(Nodes, response.second, strError);
				if (impl_->observer_)
				{
					impl_->observer_->OnCategoryFilteredNodesRetrieved(bResult, Nodes, filter);
				}
				return bResult;
			});
	}

	void ITwinWebServices::GetCategoryNodes(std::string const& iTwinId, std::string const& iModelId, std::string const& changesetId, std::string const& parentKey /*= ""*/, int offset /*= 0*/, int count /*= 1000*/)
	{
		std::string const key = fmt::format("{}:{}", iModelId, changesetId);

		ITwinAPIRequestInfo const requestInfo =
		{
			.ShortName = __func__,
			.Verb = EVerb::Post,
			.UrlSuffix = std::string("/imodel/rpc/v4/mode/1/context/") + iTwinId
								+ "/imodel/" + iModelId
								+ "/changeset/" + GetIModelRpcUrlChangeset(changesetId)
								+ "/PresentationRpcInterface-4.1.0-getPagedNodes",
			.AcceptHeader = "application/vnd.bentley.itwin-platform.v1+json",

			/*** additional settings for POST ***/
			.ContentType = "text/plain",
			.ContentString = "[{\"key\":\"" + key
								+ "\",\"iTwinId\":\"" + iTwinId
								+ "\",\"iModelId\":\"" + iModelId
								+ "\",\"changeset\":{\"id\":\"" + changesetId
								+ "\"}},{" + (!parentKey.empty() ? "\"parentKey\":" + parentKey + "," : "")
								+ "\"clientId\":\"" + ITwinAuthManager::GetAppID(EITwinEnvironment::Prod)
								+ "\",\"locale\":\"en\",\"unitSystem\":\"metric\",\"rulesetOrId\":{\"id\":\"tree-widget-react/CategoriesTree\",\"rules\":[{\"ruleType\":"
								+ "\"RootNodes\",\"subConditions\":[{\"condition\":\"GetVariableStringValue(\\\"ViewType\\\") = \\\"3d\\\"\",\"specifications\":[{\"specType\":"
								+ "\"InstanceNodesOfSpecificClasses\",\"classes\":[{\"schemaName\":\"BisCore\",\"classNames\":[\"SpatialCategory\"],\"arePolymorphic\":true}],"
								+ "\"relatedInstances\":[{\"relationshipPath\":{\"relationship\":{\"schemaName\":\"BisCore\",\"className\":\"ModelContainsElements\"},"
								+ "\"direction\":\"Backward\"},\"isRequired\":true,\"alias\":\"model\"}],\"instanceFilter\":\"NOT this.IsPrivate AND (NOT model.IsPrivate OR "
								+ "model.IsOfClass(\\\"DictionaryModel\\\", \\\"BisCore\\\")) AND this.HasRelatedInstance(\\\"BisCore:GeometricElement3dIsInCategory\\\", "
								+ "\\\"Backward\\\", \\\"BisCore:Element\\\")\",\"groupByClass\":false,\"groupByLabel\":false}]},{\"condition\":\"GetVariableStringValue"
								+ "(\\\"ViewType\\\") = \\\"2d\\\"\",\"specifications\":[{\"specType\":\"InstanceNodesOfSpecificClasses\",\"classes\":[{\"schemaName\":\"BisCore\","
								+ "\"classNames\":[\"DrawingCategory\"],\"arePolymorphic\":true}],\"relatedInstances\":[{\"relationshipPath\":{\"relationship\":{\"schemaName\":"
								+ "\"BisCore\",\"className\":\"ModelContainsElements\"},\"direction\":\"Backward\"},\"isRequired\":true,\"alias\":\"model\"}],\"instanceFilter\":"
								+ "\"NOT this.IsPrivate AND (NOT model.IsPrivate OR model.IsOfClass(\\\"DictionaryModel\\\", \\\"BisCore\\\")) AND this.HasRelatedInstance"
								+ "(\\\"BisCore:GeometricElement2dIsInCategory\\\", \\\"Backward\\\", \\\"BisCore:Element\\\")\",\"groupByClass\":false,\"groupByLabel\":false}]}]},"
								+ "{\"ruleType\":\"ChildNodes\",\"condition\":\"ParentNode.IsOfClass(\\\"Category\\\", \\\"BisCore\\\") ANDALSO ParentNode.ECInstance."
								+ "GetRelatedInstancesCount(\\\"BisCore:CategoryOwnsSubCategories\\\", \\\"Forward\\\", \\\"BisCore:SubCategory\\\") > 1\",\"specifications\":"
								+ "[{\"specType\":\"RelatedInstanceNodes\",\"relationshipPaths\":[{\"relationship\":{\"schemaName\":\"BisCore\",\"className\":"
								+ "\"CategoryOwnsSubCategories\"},\"direction\":\"Forward\"}],\"instanceFilter\":\"NOT this.IsPrivate\",\"groupByClass\":false,\"groupByLabel\":"
								+ "false}]},{\"ruleType\":\"LabelOverride\",\"condition\":\"this.IsOfClass(\\\"Category\\\", \\\"BisCore\\\")\",\"description\":"
								+ "\"this.Description\"}],\"default\":{\"$schema\":\"../../../../node_modules/@itwin/presentation-common/Ruleset.schema.json\",\"id\":"
								+ "\"tree-widget-react/CategoriesTree\",\"rules\":[{\"ruleType\":\"RootNodes\",\"subConditions\":[{\"condition\":\"GetVariableStringValue"
								+ "(\\\"ViewType\\\") = \\\"3d\\\"\",\"specifications\":[{\"specType\":\"InstanceNodesOfSpecificClasses\",\"classes\":[{\"schemaName\":\"BisCore\","
								+ "\"classNames\":[\"SpatialCategory\"],\"arePolymorphic\":true}],\"relatedInstances\":[{\"relationshipPath\":{\"relationship\":{\"schemaName\":"
								+ "\"BisCore\",\"className\":\"ModelContainsElements\"},\"direction\":\"Backward\"},\"isRequired\":true,\"alias\":\"model\"}],\"instanceFilter\":"
								+ "\"NOT this.IsPrivate AND (NOT model.IsPrivate OR model.IsOfClass(\\\"DictionaryModel\\\", \\\"BisCore\\\")) AND this.HasRelatedInstance"
								+ "(\\\"BisCore:GeometricElement3dIsInCategory\\\", \\\"Backward\\\", \\\"BisCore:Element\\\")\",\"groupByClass\":false,\"groupByLabel\":false}]},"
								+ "{\"condition\":\"GetVariableStringValue(\\\"ViewType\\\") = \\\"2d\\\"\",\"specifications\":[{\"specType\":\"InstanceNodesOfSpecificClasses\","
								+ "\"classes\":[{\"schemaName\":\"BisCore\",\"classNames\":[\"DrawingCategory\"],\"arePolymorphic\":true}],\"relatedInstances\":"
								+ "[{\"relationshipPath\":{\"relationship\":{\"schemaName\":\"BisCore\",\"className\":\"ModelContainsElements\"},\"direction\":\"Backward\"},"
								+ "\"isRequired\":true,\"alias\":\"model\"}],\"instanceFilter\":\"NOT this.IsPrivate AND (NOT model.IsPrivate OR model.IsOfClass"
								+ "(\\\"DictionaryModel\\\", \\\"BisCore\\\")) AND this.HasRelatedInstance(\\\"BisCore:GeometricElement2dIsInCategory\\\", \\\"Backward\\\", "
								+ "\\\"BisCore:Element\\\")\",\"groupByClass\":false,\"groupByLabel\":false}]}]},{\"ruleType\":\"ChildNodes\",\"condition\":\"ParentNode.IsOfClass"
								+ "(\\\"Category\\\", \\\"BisCore\\\") ANDALSO ParentNode.ECInstance.GetRelatedInstancesCount(\\\"BisCore:CategoryOwnsSubCategories\\\", "
								+ "\\\"Forward\\\", \\\"BisCore:SubCategory\\\") > 1\",\"specifications\":[{\"specType\":\"RelatedInstanceNodes\",\"relationshipPaths\":"
								+ "[{\"relationship\":{\"schemaName\":\"BisCore\",\"className\":\"CategoryOwnsSubCategories\"},\"direction\":\"Forward\"}],\"instanceFilter\":"
								+ "\"NOT this.IsPrivate\",\"groupByClass\":false,\"groupByLabel\":false}]},{\"ruleType\":\"LabelOverride\",\"condition\":\"this.IsOfClass"
								+ "(\\\"Category\\\", \\\"BisCore\\\")\",\"description\":\"this.Description\"}]}},\"paging\":{\"start\":" + std::to_string(offset) + ",\"size\":" 
								+ std::to_string(count) + "},\"rulesetVariables\":[{\"id\":\"ViewType\",\"type\":\"string\",\"value\":\"3d\"}]}]",
			.badlyFormed = iTwinId.empty() || iModelId.empty()
		};

		impl_->ProcessHttpRequest(
			requestInfo,
			[this](Http::Response const& response, RequestID const&, std::string& strError) -> bool
			{
				IModelPagedNodesRes iModelProps;
				bool const bResult = Http::IsSuccessful(response)
					&& Json::FromString(iModelProps, response.second, strError);
				if (impl_->observer_)
				{
					impl_->observer_->OnIModelCategoryNodesRetrieved(bResult, iModelProps);
				}
				return bResult;
			});
	}

	void ITwinWebServices::GetIModelProperties(std::string const& iTwinId, std::string const& iModelId, std::string const& changesetId)
	{
		ITwinAPIRequestInfo const requestInfo =
		{
			.ShortName		= __func__,
			.Verb			= EVerb::Post,
			.UrlSuffix		= std::string("/imodel/rpc/v4/mode/1/context/") + iTwinId
								+ "/imodel/" + iModelId
								+ "/changeset/" + GetIModelRpcUrlChangeset(changesetId)
								+ "/IModelReadRpcInterface-3.6.0-getConnectionProps",
			.AcceptHeader	= "application/vnd.bentley.itwin-platform.v1+json",

			/*** additional settings for POST ***/
			.ContentType	= "text/plain",
			.ContentString	= "[{\"iTwinId\":\"" + iTwinId
								+ "\",\"iModelId\":\"" + iModelId
								+ "\",\"changeset\":{\"id\":\"" + changesetId
								+ "\"}}]",
			.badlyFormed	= iTwinId.empty() || iModelId.empty()
		};

		impl_->ProcessHttpRequest(
			requestInfo,
			[this](Http::Response const& response, RequestID const&, std::string& strError) -> bool
		{
			IModelProperties iModelProps;
			bool const bResult = Http::IsSuccessful(response)
				&& Json::FromString(iModelProps, response.second, strError);
			if (impl_->observer_)
			{
				impl_->observer_->OnIModelPropertiesRetrieved(bResult, iModelProps);
			}
			return bResult;
		});
	}

	void ITwinWebServices::ConvertIModelCoordsToGeoCoords(std::string const& iTwinId,
		std::string const& iModelId, std::string const& changesetId, double const x, double const y,
		double const z, std::function<void(RequestID const&)>&& notifyRequestID)
	{
		ITwinAPIRequestInfo const requestInfo =
		{
			.ShortName		= __func__,
			.Verb			= EVerb::Post,
			.UrlSuffix		= std::string("/imodel/rpc/v4/mode/1/context/") + iTwinId
								+ "/imodel/" + iModelId
								+ "/changeset/" + GetIModelRpcUrlChangeset(changesetId)
								+ "/IModelReadRpcInterface-3.6.0-getGeoCoordinatesFromIModelCoordinates",
			.AcceptHeader	= "application/vnd.bentley.itwin-platform.v1+json",

			/*** additional settings for POST ***/
			.ContentType	= "text/plain",
			.ContentString	= "[{\"iTwinId\":\"" + iTwinId
								+ "\",\"iModelId\":\"" + iModelId
								+ "\",\"changeset\":{\"id\":\"" + changesetId
								+ "\"}}, {\"target\": \"WGS84\", \"iModelCoords\": [{"
								+ fmt::format("\"x\": {}, \"y\": {}, \"z\": {}", x, y, z)
								+ "}]}]",
			.badlyFormed	= iTwinId.empty() || iModelId.empty()
		};

		impl_->ProcessHttpRequest(
			requestInfo,
			[this](Http::Response const& response, RequestID const& requestId, std::string& strError) -> bool
			{
				GeoCoordsReply geoCoords;
				bool const bResult = Http::IsSuccessful(response)
					&& Json::FromString(geoCoords, response.second, strError);
				if (impl_->observer_)
				{
					impl_->observer_->OnConvertedIModelCoordsToGeoCoords(bResult, geoCoords, requestId);
				}
				return bResult;
			},
			std::move(notifyRequestID));
	}

	ITwinAPIRequestInfo ITwinWebServices::InfosToQueryIModel(
		std::string const& iTwinId, std::string const& iModelId, std::string const& changesetId,
		std::string const& ECSQLQuery, int offset, int count)
	{
		return ITwinAPIRequestInfo{
			.ShortName		= __func__,
			.Verb			= EVerb::Post,
			.UrlSuffix		= std::string("/imodel/rpc/v4/mode/1/context/") + iTwinId
								+ "/imodel/" + iModelId
								+ "/changeset/" + GetIModelRpcUrlChangeset(changesetId)
								+ "/IModelReadRpcInterface-3.7.0-queryRows",
			.AcceptHeader	= "application/vnd.bentley.itwin-platform.v1+json",

			/*** additional settings for POST ***/
			.ContentType	= "text/plain; charset=utf-8",
			.ContentString	= "[{\"iTwinId\":\"" + iTwinId
								+ "\",\"iModelId\":\"" + iModelId
								+ "\",\"changeset\":{\"id\":\"" + changesetId
								+ "\"}},{\"limit\":{\"offset\":" + std::to_string(offset)
								+ ",\"count\":" + std::to_string(count)
								+ "},\"rowFormat\":1,\"convertClassIdsToClassNames\":true,\"kind\":1,\"valueFormat\":0," \
									"\"query\":\"" + ECSQLQuery
								+ "\"}]",

			.badlyFormed	= iTwinId.empty() || iModelId.empty() || ECSQLQuery.empty()
		};
	}

	void ITwinWebServices::QueryIModel(
		std::string const& iTwinId, std::string const& iModelId, std::string const& changesetId,
		std::string const& ECSQLQuery, int offset, int count,
		std::function<void(RequestID const&)>&& notifyRequestID,
		ITwinAPIRequestInfo const* requestInfo,
		FilterErrorFunc&& filterError /*= {}*/)
	{
		std::optional<ITwinAPIRequestInfo> optRequestInfo;
		if (!requestInfo)
		{
			optRequestInfo.emplace(InfosToQueryIModel(iTwinId, iModelId, changesetId, ECSQLQuery, offset,
													  count));
			requestInfo = &(*optRequestInfo);
		}

		impl_->ProcessHttpRequest(
			*requestInfo,
			[this](Http::Response const& response, RequestID const& requestId, std::string& strError) -> bool
			{
				struct DataHolder { rfl::Generic data; };
				DataHolder res;
				bool bResult = Http::IsSuccessful(response)
					&& Json::FromString(res, response.second, strError, false /*bLogParseError*/);

				// Following a recent update, rfl::Generic has become more permissive, and manages to
				// parse a text not even containing 'data'... Check that the result is really relevant
				// here (it should not be a basic type).
				bResult = bResult && (res.data.to_array()
								|| res.data.to_object()
								|| res.data.to_string());

				// Sometimes, we receive 200 but the response contains an error => instead of logging the
				// error, try to parse the specific error in such case:
				if (!bResult && Http::IsSuccessful(response)
					&& !response.second.empty())
				{
					struct IModelQueryError {
						int kind = -1;
						std::string error;
						int status = -1;
					};
					IModelQueryError queryError;
					std::string parseError2;
					if (Json::FromString(queryError, response.second, parseError2))
					{
						// This could give a more detailed error.
						// (even though it's not really useful to us - for example, we can get
						//	"ECClass 'bis.ExternalSourceAspect' does not exist or could not be loaded.")
						strError = queryError.error;
					}
				}
				if (impl_->observer_)
				{
					impl_->observer_->OnIModelQueried(bResult, response.second, requestId);
				}
				return bResult;
			},
			std::move(notifyRequestID),
			std::move(filterError)
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
			AdvViz::SDK::AttributeMap& outAttributes_;
			mutable MaterialPropParserData helper_;
			mutable std::stringstream error_;


			AttributesVisitor(AdvViz::SDK::AttributeMap& outAttrs)
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
			// some are not used at all, since the response only deals with string primitives

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
			void operator()(const std::nullopt_t&) const
			{
				error_ << "unhandled std::nullopt_t" << std::endl;
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

			AdvViz::SDK::ITwinMaterialProperties& outProps_;
			mutable bool bIsParsingMap_ = false;

			MaterialPropertiesVisitor(AdvViz::SDK::ITwinMaterialProperties& elementProps)
				: AttributesVisitor(elementProps.attributes)
				, outProps_(elementProps)
			{

			}

			void operator()(const bool& boolValue) const { Super::operator()(boolValue); }
			void operator()(const int& nValue) const { Super::operator()(nValue); }
			void operator()(const double& dValue) const { Super::operator()(dValue); }
			void operator()(const std::string& strValue) const { Super::operator()(strValue); }
			void operator()(const rfl::Generic::Array& rflArray) const { Super::operator()(rflArray); }
			void operator()(const std::nullopt_t& rflNullopt) const { Super::operator()(rflNullopt); }

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
		std::string const& iTwinId, std::string const& iModelId, std::string const& changesetId,
		std::vector<std::string> const& materialIds)
	{
		ITwinAPIRequestInfo const requestInfo =
		{
			.ShortName		= __func__,
			.Verb			= EVerb::Post,
			.UrlSuffix		= std::string("/imodel/rpc/v4/mode/1/context/") + iTwinId
								+ "/imodel/" + iModelId
								+ "/changeset/" + GetIModelRpcUrlChangeset(changesetId)
								+ "/IModelReadRpcInterface-3.6.0-getElementProps",
			.AcceptHeader	= "application/vnd.bentley.itwin-platform.v1+json",

			/*** additional settings for POST ***/
			.ContentType	= "text/plain",
			.ContentString	= "[{\"iTwinId\":\"" + iTwinId
								+ "\",\"iModelId\":\"" + iModelId
								+ "\",\"changeset\":{\"id\":\"" + changesetId
								+ "\"}}," + Json::ToString(materialIds)
								+ "]",

			.badlyFormed	= iTwinId.empty() || iModelId.empty() || materialIds.empty()
		};

		impl_->ProcessHttpRequest(
			requestInfo,
			[this](Http::Response const& response, RequestID const&, std::string& strError) -> bool
		{
			ITwinMaterialPropertiesMap itwinMaterials;

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
			bool bResult = Http::IsSuccessful(response)
				&& Json::FromString(infos, response.second, strError);
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
			bResult &= strError.empty();

			if (impl_->observer_)
			{
				impl_->observer_->OnMaterialPropertiesRetrieved(bResult, itwinMaterials);
			}

			return bResult;
		});
	}

	void ITwinWebServices::GetMaterialProperties(
		std::string const& iTwinId, std::string const& iModelId, std::string const& changesetId,
		std::string const& materialId)
	{
		GetMaterialListProperties(iTwinId, iModelId, changesetId, { materialId });
	}

	static bool ParseTextureResponse(ITwinTextureData& itwinTexture, Http::Response const& response,
									 std::string& strError)
	{
		if (!Http::IsSuccessful(response))
		{
			// Failed request.
			return false;
		}
		if (!response.rawdata_)
		{
			strError = "internal error (missing binary data)";
			return false;
		}
		struct BytesInfo
		{
			std::optional<bool> isBinary;
			std::optional<int> index;
			uint32_t size = 0;
			std::optional<uint32_t> chunks;
		};
		struct TexDataJsonPart
		{
			int width = 0;
			int height = 0;
			int format = -1;
			std::optional<int> transparency;
			BytesInfo bytes;
		};

		/* The response does not start with the JSON part directly :

----------------------------058561453697718044834493
Content-Disposition: form-data; name="objects"

{"width":215,"height":346,"format":2,"transparency":2,"bytes":{"isBinary":true,"index":0,"size":30455,"chunks":1}}
----------------------------058561453697718044834493
Content-Disposition: form-data; name="data-0"
Content-Type: application/octet-stream

		*/

		auto const extractJson = [](std::string const& r) -> std::string
		{
			auto startPos = r.find('{');
			if (startPos == std::string::npos)
				return {};
			int openedBrackets = 1;
			auto curPos = startPos;
			while (openedBrackets > 0)
			{
				auto nextPos = r.find_first_of("{}", curPos + 1);
				if (nextPos == std::string::npos)
					break;
				if (r.at(nextPos) == '{')
					openedBrackets++;
				else
					openedBrackets--;
				curPos = nextPos;
			}
			if (openedBrackets != 0)
			{
				// BE_ISSUE("mismatch in delimiters");
				return {};
			}
			return r.substr(startPos, curPos + 1 - startPos);
		};
		TexDataJsonPart texDataJson;
		if (!Json::FromString(texDataJson, extractJson(response.second), strError))
		{
			return false;
		}
		if (texDataJson.bytes.size == 0)
		{
			strError = "null texture size";
			return false;
		}
		itwinTexture.width = texDataJson.width;
		itwinTexture.height = texDataJson.height;
		if (texDataJson.format >= 0 && texDataJson.format <= 3)
		{
			itwinTexture.format = static_cast<ImageSourceFormat>(texDataJson.format);
		}
		if (texDataJson.transparency)
		{
			itwinTexture.transparency = static_cast<TextureTransparency>(*texDataJson.transparency);
		}

		// Extract the binary part from the response's raw data
		auto const& rawdata(*response.rawdata_);
		const std::string_view octetStream("octet-stream");
		size_t startBinaryPos = response.second.find(octetStream);
		if (startBinaryPos == std::string::npos)
		{
			strError = "could not find octet-stream chunk";
			return false;
		}
		startBinaryPos += octetStream.size();
		startBinaryPos = response.second.find_first_not_of("\r\n", startBinaryPos);
		if (startBinaryPos == std::string::npos)
		{
			strError = "could not recover binary data start";
			return false;
		}
		if (startBinaryPos + texDataJson.bytes.size > rawdata.size())
		{
			strError = "mismatch string content vs raw data";
			return false;
		}
		itwinTexture.bytes.resize(texDataJson.bytes.size);
		std::copy(
			rawdata.begin() + startBinaryPos,
			rawdata.begin() + startBinaryPos + texDataJson.bytes.size,
			itwinTexture.bytes.data());
		return true;
	}


	void ITwinWebServices::GetTextureData(
		std::string const& iTwinId, std::string const& iModelId, std::string const& changesetId,
		std::string const& textureId)
	{
		ITwinAPIRequestInfo const requestInfo =
		{
			.ShortName		= __func__,
			.Verb			= EVerb::Post,
			.UrlSuffix		= std::string("/imodel/rpc/v4/mode/1/context/") + iTwinId
								+ "/imodel/" + iModelId
								+ "/changeset/" + GetIModelRpcUrlChangeset(changesetId)
								+ "/IModelReadRpcInterface-3.6.0-queryTextureData",
			.AcceptHeader	= "application/vnd.bentley.itwin-platform.v1+json",

			/*** additional settings for POST ***/
			.ContentType	= "text/plain",
			.ContentString	= "[{\"iTwinId\":\"" + iTwinId
								+ "\",\"iModelId\":\"" + iModelId
								+ "\",\"changeset\":{\"id\":\"" + changesetId
								+ "\"}},{\"name\":\"" + textureId
								+ "\"}]",

			.badlyFormed	= iTwinId.empty() || iModelId.empty() || textureId.empty(),

			/* Here we need the* full* retrieved response, not just a string */
			.needRawData = true
		};

		impl_->ProcessHttpRequest(
			requestInfo,
			[this, textureId](Http::Response const& response, RequestID const&, std::string& strError) -> bool
		{
			ITwinTextureData textureData;
			bool const bResult = ParseTextureResponse(textureData, response, strError);
			if (impl_->observer_)
			{
				impl_->observer_->OnTextureDataRetrieved(bResult, textureId, textureData);
			}
			return bResult;
		});
	}

	bool ITwinWebServices::IsSetupForForMaterialMLPrediction() const
	{
		return impl_->hasSetupMLMaterialAssignment_;
	}

	void ITwinWebServices::SetupForMaterialMLPrediction()
	{
		impl_->hasSetupMLMaterialAssignment_ = true;
	}

	void ITwinWebServices::SetMaterialMLPredictionCacheFolder(std::filesystem::path const& cacheFolder)
	{
		std::filesystem::path actualCacheFolder(cacheFolder);
		if (!cacheFolder.empty())
		{
			// Create cache folder if needed.
			std::error_code ec;
			if (!std::filesystem::is_directory(cacheFolder, ec)
				&& !std::filesystem::create_directories(cacheFolder, ec))
			{
				actualCacheFolder.clear();
			}
		}
		impl_->matMLPredictionCacheFolder_ = actualCacheFolder;
	}

	ITwinAPIRequestInfo ITwinWebServices::Impl::BuildMatMLPredictionRequestInfo(EMatMLPredictionStep eStep)
	{
		BE_ASSERT(matMLPredictionInfo_
			&& !matMLPredictionInfo_->iTwinId_.empty()
			&& !matMLPredictionInfo_->iModelId_.empty());

		// Post or Get
		bool const usePost = (eStep == EMatMLPredictionStep::RunJob);
		ITwinAPIRequestInfo requestInfo = {
			fmt::format("MatMLPrediction_{}", static_cast<uint8_t>(eStep)),
			usePost ? EVerb::Post : EVerb::Get,
			"/material-assignment/jobs",
			"application/vnd.bentley.itwin-platform.v1+json"
		};

		if (eStep >= EMatMLPredictionStep::GetJobStatus)
		{
			requestInfo.UrlSuffix += fmt::format("/{}", matMLPredictionInfo_->jobId_);
			requestInfo.badlyFormed = matMLPredictionInfo_->jobId_.empty();
		}

		switch (eStep)
		{
		default:
		case EMatMLPredictionStep::Init:
		case EMatMLPredictionStep::Done:
			BE_ISSUE("no request for this step");
			requestInfo.badlyFormed = true;
			break;

		case EMatMLPredictionStep::RunJob:
		{
			requestInfo.ContentType = "application/json";
			requestInfo.ContentString = std::string("{"
				"\"iModelId\": \"") + matMLPredictionInfo_->iModelId_ + "\","
				"\"changesetId\": \"" + matMLPredictionInfo_->changesetId_ + "\" }";
			requestInfo.badlyFormed |= matMLPredictionInfo_->iModelId_.empty();
			break;
		}

		case EMatMLPredictionStep::GetJobStatus:
			break;

		case EMatMLPredictionStep::GetJobResults:
			requestInfo.UrlSuffix += "/materials";

			if (matMLPredictionInfo_->jobResultURL_)
			{
				BE_ASSERT(matMLPredictionInfo_->jobResultURL_->starts_with("https://"));
				requestInfo.UrlSuffix = *matMLPredictionInfo_->jobResultURL_;
				requestInfo.isFullUrl = true;
			}
			break;
		}
		BE_ASSERT(!requestInfo.badlyFormed);
		return requestInfo;
	}

	namespace Detail
	{
		struct InferenceInfo
		{
			std::string id;
			std::string status;
			std::optional<int> totalSteps;
			std::optional<int> completedSteps;
		};
		struct InferenceInfoHolder
		{
			InferenceInfo inference;
		};

		struct JobLink
		{
			std::string href;
		};
		struct JobStatusLinks
		{
			JobLink materials;
			JobLink iTwin;
			JobLink iModel;
		};

		struct JobInfo
		{
			std::string jobId;
			std::string status;
			JobStatusLinks _links;
		};
		struct JobInfoHolder
		{
			JobInfo job;
		};

		struct Result
		{
			std::string id;
			std::string name; // will always be "results.json" in our case
			uint64_t size = 0;
		};
		struct ResultVec
		{
			std::vector<Result> results;
		};

		struct InferenceElementInfo
		{
			std::string id;
			float confidence = 0.f;
		};
		struct InferenceMaterialEntry
		{
			std::string material; // name of the material - eg. "Wood"
			std::vector<InferenceElementInfo> elements;
		};
		struct JobResultsHolder
		{
			std::vector<InferenceMaterialEntry> materials;
		};

		void TranslateTo(std::vector<InferenceMaterialEntry> const& MLOutput, ITwinMaterialPrediction& predictions)
		{
			auto& dstData(predictions.data);
			dstData.clear();
			dstData.reserve(MLOutput.size());
			for (auto const& e : MLOutput)
			{
				auto& dstEntry = dstData.emplace_back();
				dstEntry.material = e.material;
				dstEntry.elements.reserve(e.elements.size());
				std::transform(
					e.elements.begin(), e.elements.end(),
					std::back_inserter(dstEntry.elements),
					[](InferenceElementInfo const& eltInfo) noexcept
				{
					return std::stoull(eltInfo.id, nullptr, /*base*/16);
				});
			}
		}
	}


	void ITwinWebServices::Impl::ParseMatMLPredictionResponse(EMatMLPredictionStep eStep,
		Http::Response const& response, RequestID const&,
		MatMLPredictionParseResult& parseResult)
	{
		std::string& parsingError(parseResult.parsingError);
		// Distinguish 2 kinds of errors: parsing vs failed job
		bool& responseOK(parseResult.parsingOK);
		bool& continueJob(parseResult.continueJob);

		responseOK = continueJob = false;
		parseResult.retryWithDelay = false; // specific to GetJobStatus

		if (!Http::IsSuccessful(response))
		{
			parsingError = fmt::format("Error response code: {}", response.first);
			return;
		}

		// Most of responses will consist in a description of current run.
		Detail::JobInfoHolder body;
		switch (eStep)
		{
		case EMatMLPredictionStep::RunJob:
		{
			responseOK = Json::FromString(body, response.second, parsingError);

			continueJob = responseOK && !body.job.jobId.empty();
			matMLPredictionInfo_->jobId_ = body.job.jobId;
			if (continueJob && !matMLPredictionCacheFolder_.empty())
			{
				// Save current job info, in order to be able to resume in a future session, in case the
				// user quits Carrot before the job terminates.
				std::ofstream(GetMatMLInfoPath()) << rfl::json::write(
					*matMLPredictionInfo_, YYJSON_WRITE_PRETTY);
			}
			break;
		}

		case EMatMLPredictionStep::GetJobStatus:
		{
			responseOK = Json::FromString(body, response.second, parsingError);
			if (responseOK)
			{
				continueJob = true;
				std::string const& status = body.job.status;
				if (status == "Succeeded"
					|| status == "Finished"
					|| status == "Completed")
				{
					// Successful job. Store the result url, if any (note that this url is easy to
					// reconstruct from the job ID, but make sure our code works if a custom url is given.
					auto const& resultURL = body.job._links.materials.href;
					matMLPredictionInfo_->jobResultURL_.reset();
					if (!resultURL.empty())
					{
						BE_ASSERT(resultURL.starts_with("https://"));
						matMLPredictionInfo_->jobResultURL_ = resultURL;
					}
				}
				else if (status == "Failed")
				{
					// The inference has failed => abort
					BE_LOGE("ITwinAPI", "[ML Material Prediction] A problem has occurred during the inference - abort job");

					// Make sure the failed run will not be tested again in the future
					RemoveMatMLInfoFile();

					continueJob = false;
				}
				else
				{
					// Status can be "InProgress", "Queued"... => repeat request after a delay.
					parseResult.retryWithDelay = true;
				}
			}
			break;
		}

		case EMatMLPredictionStep::GetJobResults:
		{
			Detail::JobResultsHolder result;
			auto& resultMaterials(result.materials);
			responseOK = Json::FromString(result, response.second, parsingError);
			continueJob = responseOK;
			if (continueJob)
			{
				// Translate it in a format that is easier to handle by glTF Tuner
				Detail::TranslateTo(resultMaterials, matMLPredictionInfo_->result_);

				// Cache this result
				if (!matMLPredictionCacheFolder_.empty())
				{
					std::ofstream(matMLPredictionCacheFolder_ / "results.json") << rfl::json::write(
						matMLPredictionInfo_->result_, YYJSON_WRITE_PRETTY);
				}
			}
			break;
		}

		default:
		case EMatMLPredictionStep::Init:
		case EMatMLPredictionStep::Done:
			BE_ISSUE("no response expected for this step");
			break;
		}
	}

	bool ITwinWebServices::Impl::ProcessMatMLPredictionStepWithDelay(EMatMLPredictionStep eStep)
	{
		if (!observer_)
		{
			// This service helper is now orphan (the level may have been exited...)
			return false;
		}

		// Repeat the same step after a delay
		return UniqueDelayedCall(uniqueName_ + "MatMLPredictionPipeline",
			[this, eStep, isValidLambda = isThisValid_]()
			{
				if (*isValidLambda)
				{
					ProcessMatMLPredictionStep(eStep);
				}
				return DelayedCall::EReturnedValue::Done;
			}, 10.0 /* in seconds*/);
	}

	std::pair<float, int> ITwinWebServices::Impl::ShouldRetryMaterialMLStep(
		EMatMLPredictionStep eStep, int attempt, int httpCode) const
	{
		if (!observer_)
		{
			// Do not retry if we are orphan (UE exiting...)
			return std::make_pair(0.f, 0);
		}
		if (isResumingMatMLPrediction_)
		{
			// ...nor if we have resumed a previous job: in such case, the job we are requesting
			// may have been destroyed on the server, typically if it was started a long time ago...
			// In such case, we will restart from scratch.
			return std::make_pair(0.f, 0);
		}

		// Some Material Prediction steps should *not* be retried
		switch (eStep)
		{
		case EMatMLPredictionStep::GetJobResults:
			return defaultShouldRetryFunc(attempt, httpCode);

		default:
		case EMatMLPredictionStep::Init:
		case EMatMLPredictionStep::Done:
			BE_ISSUE("invalid ML step");
			[[fallthrough]];
		case EMatMLPredictionStep::RunJob:
		case EMatMLPredictionStep::GetJobStatus:
			return std::make_pair(0.f, 0);
		}
	}

	void ITwinWebServices::Impl::ResetMatMLJobData()
	{
		matMLPredictionInfo_->jobId_ = {};
		matMLPredictionInfo_->result_ = {};
	}

	std::filesystem::path ITwinWebServices::Impl::GetMatMLInfoPath() const
	{
		return matMLPredictionCacheFolder_ / "info_v1.json";
	}

	void ITwinWebServices::Impl::RemoveMatMLInfoFile()
	{
		// Make sure the failed run will not be tested again in the future
		if (!matMLPredictionCacheFolder_.empty())
		{
			std::error_code ec;
			auto const matMLInfoFile = GetMatMLInfoPath();
			if (std::filesystem::exists(matMLInfoFile, ec))
			{
				std::filesystem::remove(matMLInfoFile, ec);
			}
		}
	}

	void ITwinWebServices::Impl::ProcessMatMLPredictionStep(EMatMLPredictionStep eStep)
	{
		if (!hasSetupMLMaterialAssignment_)
		{
			BE_ISSUE("SetupForMaterialMLPrediction not called!");
			return;
		}
		if (!matMLPredictionInfo_)
		{
			BE_ISSUE("MaterialMLPredictionInfo not initialized!");
			return;
		}
		if (!observer_)
		{
			// This service helper is now orphan (the level may have been exited...)
			return;
		}
		matMLPredictionInfo_->step_ = eStep;

		ProcessHttpRequest(
			BuildMatMLPredictionRequestInfo(eStep),
			[this, eStep]
			(Http::Response const& response, RequestID const& requestId, std::string& parsingError) -> bool
		{
			MatMLPredictionParseResult parseResult;
			ParseMatMLPredictionResponse(eStep, response, requestId, parseResult);
			parsingError = parseResult.parsingError;

			if (parseResult.continueJob)
			{
				if (parseResult.retryWithDelay)
				{
					// Repeat the same step after a delay.
					ProcessMatMLPredictionStepWithDelay(eStep);
					return true;
				}

				EMatMLPredictionStep const nextStep = static_cast<EMatMLPredictionStep>(
					uint8_t(eStep) + 1);
				if (nextStep == EMatMLPredictionStep::Done)
				{
					// We are done - broadcast the result
					matMLPredictionInfo_->step_ = EMatMLPredictionStep::Done;

					if (observer_)
					{
						observer_->OnMatMLPredictionRetrieved(true, matMLPredictionInfo_->result_);
					}
				}
				else
				{
					// Launch next request
					ProcessMatMLPredictionStep(nextStep);
				}
			}
			else if (isResumingMatMLPrediction_)
			{
				// Restart from the beginning
				isResumingMatMLPrediction_ = false;
				ResetMatMLJobData();
				RemoveMatMLInfoFile();
				ProcessMatMLPredictionStep(EMatMLPredictionStep::RunJob);
			}
			else
			{
				// Notify error and abort
				matMLPredictionInfo_->step_ = EMatMLPredictionStep::Done;
				if (observer_)
				{
					observer_->OnMatMLPredictionRetrieved(false, {}, owner_.GetRequestError(requestId));
				}
			}
			return parseResult.parsingOK;
		},
			{} /*notifyRequestID*/,
			{} /*filterError*/,

			std::bind(&ITwinWebServices::Impl::ShouldRetryMaterialMLStep,
				this, eStep, std::placeholders::_1, std::placeholders::_2));
	}


	EITwinMatMLPredictionStatus ITwinWebServices::Impl::ProcessMatMLPrediction(
		std::string const& iTwinId, std::string const& iModelId, std::string const& changesetId)
	{
		if (!hasSetupMLMaterialAssignment_)
		{
			BE_ISSUE("SetupForMaterialMLPrediction not called!");
			return EITwinMatMLPredictionStatus::Failed;
		}
		if (iTwinId.empty() || iModelId.empty())
		{
			BE_ISSUE("iTwin ID and iModel ID are required");
			return EITwinMatMLPredictionStatus::Failed;
		}
		if (matMLPredictionInfo_
			&& matMLPredictionInfo_->step_ != EMatMLPredictionStep::Init
			&& matMLPredictionInfo_->step_ != EMatMLPredictionStep::Done)
		{
			return EITwinMatMLPredictionStatus::InProgress;
		}

		EMatMLPredictionStep initialStep = EMatMLPredictionStep::RunJob;
		isResumingMatMLPrediction_ = false;

		// Before starting a new run (which is heavy in resources), see if we have already cached some
		// results, or at least created a run which is still in progress.
		std::error_code ec;
		if (!matMLPredictionCacheFolder_.empty()
			&& std::filesystem::is_directory(matMLPredictionCacheFolder_, ec))
		{
			// See if we have cached a previous result
			auto const matMLResultFile = matMLPredictionCacheFolder_ / "results.json";
			if (std::filesystem::exists(matMLResultFile, ec))
			{
				ITwinMaterialPrediction reloadedResult;
				std::ifstream ifs(matMLResultFile);
				std::string parseError;
				if (Json::FromStream(reloadedResult, ifs, parseError))
				{
					if (observer_)
					{
						observer_->OnMatMLPredictionRetrieved(true, reloadedResult);
					}
					return EITwinMatMLPredictionStatus::Complete;
				}
				else
				{
					std::filesystem::remove(matMLResultFile, ec);
				}
			}

			// See if a job was already created
			auto const matMLInfoFile = GetMatMLInfoPath();
			if (std::filesystem::exists(matMLInfoFile, ec))
			{
				MaterialMLPredictionInfo reloadedInfo;
				std::ifstream ifs(matMLInfoFile);
				std::string parseError;
				if (Json::FromStream(reloadedInfo, ifs, parseError)
					&& !reloadedInfo.jobId_.empty())
				{
					matMLPredictionInfo_ = reloadedInfo;
					initialStep = EMatMLPredictionStep::GetJobStatus;
					isResumingMatMLPrediction_ = true;
				}
				else
				{
					std::filesystem::remove(matMLInfoFile, ec);
				}
			}
		}

		if (!matMLPredictionInfo_)
			matMLPredictionInfo_.emplace();
		matMLPredictionInfo_->iTwinId_ = iTwinId;
		matMLPredictionInfo_->iModelId_ = iModelId;
		matMLPredictionInfo_->changesetId_ = changesetId;

		// Start the process by first step...
		ProcessMatMLPredictionStep(initialStep);

		return EITwinMatMLPredictionStatus::InProgress;
	}

	EITwinMatMLPredictionStatus ITwinWebServices::GetMaterialMLPrediction(
		std::string const& iTwinId, std::string const& iModelId, std::string const& changesetId)
	{
		return impl_->ProcessMatMLPrediction(iTwinId, iModelId, changesetId);
	}

	void ITwinWebServices::RunCustomRequest(
		ITwinAPIRequestInfo const& requestInfo,
		CustomRequestCallback&& responseCallback,
		FilterErrorFunc&& filterError /*= {}*/)
	{
		impl_->ProcessHttpRequest(
			requestInfo,
			[this, responseCallback = std::move(responseCallback)](Http::Response const& response, RequestID const& reqID, std::string& strError) -> bool
		{
			bool const bResult = Http::IsSuccessful(response)
				&& responseCallback(response.first, response.second, reqID, strError);
			return bResult;
		},
			{} /*notifyRequestID*/,
			std::move(filterError));
	}
}
