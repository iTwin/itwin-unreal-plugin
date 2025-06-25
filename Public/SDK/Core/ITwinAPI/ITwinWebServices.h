/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinWebServices.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#ifndef SDK_CPPMODULES
#	include <functional>
#	include <mutex>
#	ifndef MODULE_EXPORT
#		define MODULE_EXPORT
#	endif
#endif

#include "../AdvVizLinkType.h"
#include "ITwinEnvironment.h"
#include "ITwinMatMLPredictionEnums.h"
#include "ITwinTypes.h"

#include <filesystem>
#include <functional>
#include <optional>

MODULE_EXPORT namespace AdvViz::SDK
{
	class IITwinWebServicesObserver;
	class Http;

	class ITwinWebServices
	{
	public:
		ITwinWebServices();
		~ITwinWebServices();

		EITwinEnvironment GetEnvironment() const { return env_; }
		void SetEnvironment(EITwinEnvironment env);

		//! Returns the authorization token, if any.
		std::string GetAuthToken() const;

		//! Change the server URL - only used for unit testing
		void SetCustomServerURL(std::string const& serverUrl);

		void SetObserver(IITwinWebServicesObserver* observer);
		bool HasObserver(IITwinWebServicesObserver const* observer) const;

		//! Returns the last error encountered, if any.
		std::string GetLastError() const;

		//! Returns the last error encountered, if any, and resets it.
		//! Returns whether an error message actually existed.
		bool ConsumeLastError(std::string& outError);


		void GetITwins();

		void GetITwinInfo(std::string const& iTwinId);

		void GetITwinIModels(std::string const& iTwinId);

		void GetIModelChangesets(std::string const& iModelId, bool bOnlyLatest = false);


		void GetExports(std::string const& iModelId, std::string const& changesetId);
		void GetExportInfo(std::string const& exportId);
		void StartExport(std::string const& iModelId, std::string const& changesetId);

		void GetRealityData(std::string const& iTwinId);
		void GetRealityData3DInfo(std::string const& iTwinId, std::string const& realityDataId);


		void GetAllSavedViews(std::string const& iTwinId, std::string const& iModelId, std::string const& groupId = "",
							  int Top = 100, int Skip = 0);
		void GetSavedView(std::string const& savedViewId);
		
		void GetSavedViewExtension(std::string const& savedViewId, std::string const& extensionName);

		void GetSavedViewThumbnail(std::string const& SavedViewId);
		void UpdateSavedViewThumbnail(std::string const& SavedViewId, std::string const& ThumbnailURL);

		void GetSavedViewsGroups(std::string const& iTwinId, std::string const& iModelId);
		void AddSavedViewGroup(std::string const& iTwinId, std::string const& iModelId,
			SavedViewGroupInfo const& savedViewGroupInfo);

		void AddSavedView(std::string const& iTwinId, SavedView const& savedView, 
			SavedViewInfo const& savedViewInfo, std::string const& iModelId = "", std::string const& groupId = "");
		void OnSavedViewAdded(bool bSuccess, SavedViewInfo const& savedViewInfo);

		void DeleteSavedView(std::string const& savedViewId);
		void OnSavedViewDeleted(bool bSuccess, std::string const& savedViewId, std::string const& response) const;

		void EditSavedView(SavedView const& savedView, SavedViewInfo const& savedViewInfo);

		void GetElementProperties(
			std::string const& iTwinId, std::string const& iModelId, std::string const& changesetId,
			std::string const& elementId);

		void GetIModelProperties(
			std::string const& iTwinId, std::string const& iModelId, std::string const& changesetId);

		/// Convert from iModel spatial coordinates to WGS84 longitude, latitude and height (above ellipsoid),
		/// either using the iModel's Geographic Coordinate System if any, or using the ECEF location
		/// otherwise (but in that case using UITwinUtilityLibrary::GetEcefToIModelTransform should be strictly
		/// equivalent and much faster than a server round-trip...)
		void ConvertIModelCoordsToGeoCoords(std::string const& iTwinId,
			std::string const& iModelId, std::string const& changesetId, double const x, double const y,
			double const z, std::function<void(RequestID const&)>&& notifyRequestID);

		ITwinAPIRequestInfo InfosToQueryIModel(
			std::string const& iTwinId, std::string const& iModelId, std::string const& changesetId,
			std::string const& ECSQLQuery, int offset, int count);
		void QueryIModel(
			std::string const& iTwinId, std::string const& iModelId, std::string const& changesetId,
			std::string const& ECSQLQuery, int offset, int count,
			std::function<void(RequestID const&)>&& notifRequestID,
			ITwinAPIRequestInfo const* requestInfo,
			FilterErrorFunc&& filterError = {});

		void GetMaterialListProperties(
			std::string const& iTwinId, std::string const& iModelId, std::string const& changesetId,
			std::vector<std::string> const& materialIds);
		void GetMaterialProperties(
			std::string const& iTwinId, std::string const& iModelId, std::string const& changesetId,
			std::string const& materialId);
		void GetTextureData(
			std::string const& iTwinId, std::string const& iModelId, std::string const& changesetId,
			std::string const& textureId);

		bool IsSetupForForMaterialMLPrediction() const;
		void SetMaterialMLPredictionCacheFolder(std::filesystem::path const& cacheFolder);
		void SetupForMaterialMLPrediction();
		EITwinMatMLPredictionStatus GetMaterialMLPrediction(
			std::string const& iTwinId, std::string const& iModelId, std::string const& changesetId);

		void RunCustomRequest(
			ITwinAPIRequestInfo const& requestInfo,
			CustomRequestCallback&& responseCallback,
			FilterErrorFunc&& filterError = {});

		static ITwinWebServices* GetWorkingInstance();

		static std::string GetErrorDescription(ITwinError const& iTwinError,
			std::string const& indent = {});

		static std::string GetErrorDescriptionFromJson(std::string const& jsonContent,
			std::string const& indent = {});


	protected:
		void SetLastError(std::string const& error, RequestID const& requestId, int retriesLeft);

		//! Returns the error stored for the given request, if any.
		std::string GetRequestError(RequestID const& requestId) const;


	private:
		std::string GetAPIRootURL() const;

		/// Modify a setting which may have an impact on the end server, and make the relevant updates if
		/// needed.
		template <typename Func>
		void ModifyServerSetting(Func const& functor);

	private:
		class Impl;
		std::unique_ptr<Impl> impl_;

		std::shared_ptr<Http> http_;

		EITwinEnvironment env_ = EITwinEnvironment::Prod;
	};
}
