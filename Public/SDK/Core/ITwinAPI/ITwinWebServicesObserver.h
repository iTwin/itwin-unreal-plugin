/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinWebServicesObserver.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once


#ifndef SDK_CPPMODULES
#	include <string>
#	include <vector>
#	ifndef MODULE_EXPORT
#		define MODULE_EXPORT
#	endif // !MODULE_EXPORT
#endif

#include "../AdvVizLinkType.h"
#include "ITwinRequestTypes.h"

MODULE_EXPORT namespace AdvViz::SDK
{
	struct ChangesetInfos;
	struct IModelInfos;
	struct ITwinInfo;
	struct ITwinInfos;
	struct ITwinExportInfos;
	struct ITwinExportInfo;
	struct ITwinRealityDataInfos;
	struct ITwinRealityData3DInfo;
	struct SavedViewInfos;
	struct SavedView;
	struct SavedViewInfo;
	struct SavedViewGroupInfos;
	struct SavedViewGroupInfo;
	struct ITwinElementProperties;
	struct IModelProperties;
	struct ITwinMaterialProperties;
	struct ITwinMaterialPropertiesMap;
	struct ITwinTextureData;
	struct ITwinMaterialPrediction;
	struct GeoCoordsReply;
	struct IModelPagedNodesRes;
	struct FilteredNodesRes;

	/// Custom callbacks which can be used to perform some updates once a request is done.
	class IITwinWebServicesObserver
	{
	public:
		virtual ~IITwinWebServicesObserver() = default;

		/// Called upon error - could be used for logging purpose, typically.
		virtual void OnRequestError(std::string const& strError, int retriesLeft, bool bLogError = true) = 0;

		virtual void OnITwinsRetrieved(bool bSuccess, ITwinInfos const& infos) = 0;

		virtual void OnITwinInfoRetrieved(bool bSuccess, ITwinInfo const& info) = 0;

		virtual void OnIModelsRetrieved(bool bSuccess, IModelInfos const& infos) = 0;

		virtual void OnChangesetsRetrieved(bool bSuccess, ChangesetInfos const& infos) = 0;

		virtual void OnExportInfosRetrieved(bool bSuccess, ITwinExportInfos const& infos) = 0;
		virtual void OnExportInfoRetrieved(bool bSuccess, ITwinExportInfo const& info) = 0;
		virtual void OnExportStarted(bool bSuccess, std::string const& ExportId) = 0;

		virtual void OnSavedViewInfosRetrieved(bool bSuccess, SavedViewInfos const& infos) = 0;
		virtual void OnSavedViewRetrieved(bool bSuccess, SavedView const& savedView, SavedViewInfo const& info) = 0;
		virtual void OnSavedViewExtensionRetrieved(bool bSuccess, std::string const& savedViewId, std::string const& data) = 0;
		virtual void OnSavedViewThumbnailRetrieved(bool bSuccess, std::string const& savedViewId, std::vector<uint8_t> const& rawData) = 0;
		virtual void OnSavedViewThumbnailUpdated(bool bSuccess, std::string const& savedViewId, std::string const& Response) = 0;
		virtual void OnSavedViewGroupInfosRetrieved(bool bSuccess, SavedViewGroupInfos const& infos) = 0;
		virtual void OnSavedViewGroupAdded(bool bSuccess, SavedViewGroupInfo const& info) = 0;
		virtual void OnSavedViewAdded(bool bSuccess, SavedViewInfo const& info) = 0;
		virtual void OnSavedViewDeleted(bool bSuccess, std::string const& savedViewId, std::string const& response) = 0;
		virtual void OnSavedViewEdited(bool bSuccess, SavedView const& savedView, SavedViewInfo const& info) = 0;

		virtual void OnRealityDataRetrieved(bool bSuccess, ITwinRealityDataInfos const& infos) = 0;
		virtual void OnRealityData3DInfoRetrieved(bool bSuccess, ITwinRealityData3DInfo const& info) = 0;

		virtual void OnElementPropertiesRetrieved(bool bSuccess, ITwinElementProperties const& props, std::string const& ElementId) = 0;

		virtual void OnIModelPropertiesRetrieved(bool bSuccess, IModelProperties const& props) = 0;
		virtual void OnIModelPagedNodesRetrieved(bool /*bSuccess*/, IModelPagedNodesRes const& nodes) = 0;
		virtual void OnModelFilteredNodesRetrieved(bool /*bSuccess*/, FilteredNodesRes const& nodes, std::string const& Filter) = 0;
		virtual void OnCategoryFilteredNodesRetrieved(bool /*bSuccess*/, FilteredNodesRes const& nodes, std::string const& Filter) = 0;
		virtual void OnIModelCategoryNodesRetrieved(bool /*bSuccess*/, IModelPagedNodesRes const& nodes) = 0;
		virtual void OnConvertedIModelCoordsToGeoCoords(bool bSuccess, GeoCoordsReply const& geoCoords,
														RequestID const& requestId) = 0;
		virtual void OnIModelQueried(bool bSuccess, std::string const& Response, RequestID const&) = 0;

		virtual void OnMaterialPropertiesRetrieved(bool bSuccess, ITwinMaterialPropertiesMap const& props) = 0;

		virtual void OnTextureDataRetrieved(bool bSuccess, std::string const& textureId, ITwinTextureData const& textureData) = 0;

		virtual void OnMatMLPredictionRetrieved(bool bSuccess, ITwinMaterialPrediction const& prediction, std::string const& error = {}) = 0;
		virtual void OnMatMLPredictionProgress(float fProgressRatio) = 0;
	};


	/// Implements all the methods from interface IITwinWebServicesObserver with default implementation
	/// doing nothing but triggering an assert (this intermediate class was added to make it easier to
	/// add a new abstract method).
	class ITwinDefaultWebServicesObserver : public IITwinWebServicesObserver
	{
	public:
		void OnRequestError(std::string const& strError, int retriesLeft, bool bLogError = true) override;
		void OnITwinsRetrieved(bool bSuccess, ITwinInfos const& infos) override;
		void OnITwinInfoRetrieved(bool bSuccess, ITwinInfo const& info) override;
		void OnIModelsRetrieved(bool bSuccess, IModelInfos const& infos) override;
		void OnChangesetsRetrieved(bool bSuccess, ChangesetInfos const& infos) override;
		void OnExportInfosRetrieved(bool bSuccess, ITwinExportInfos const& infos) override;
		void OnExportInfoRetrieved(bool bSuccess, ITwinExportInfo const& info) override;
		void OnExportStarted(bool bSuccess, std::string const& InExportId) override;
		void OnSavedViewInfosRetrieved(bool bSuccess, SavedViewInfos const& infos) override;
		void OnSavedViewRetrieved(bool bSuccess, SavedView const& savedView, SavedViewInfo const& info) override;
		void OnSavedViewExtensionRetrieved(bool bSuccess, std::string const& savedViewId, std::string const& data) override;
		void OnSavedViewThumbnailRetrieved(bool bSuccess, std::string const& savedViewId, std::vector<uint8_t> const& rawData) override;
		void OnSavedViewThumbnailUpdated(bool bSuccess, std::string const& savedViewId, std::string const& Response) override;
		void OnSavedViewGroupInfosRetrieved(bool bSuccess, SavedViewGroupInfos const& infos) override;
		void OnSavedViewGroupAdded(bool bSuccess, SavedViewGroupInfo const& infos) override;
		void OnSavedViewAdded(bool bSuccess, SavedViewInfo const& info) override;
		void OnSavedViewDeleted(bool bSuccess, std::string const& savedViewId, std::string const& response) override;
		void OnSavedViewEdited(bool bSuccess, SavedView const& savedView, SavedViewInfo const& info) override;
		void OnRealityDataRetrieved(bool bSuccess, ITwinRealityDataInfos const& infos) override;
		void OnRealityData3DInfoRetrieved(bool bSuccess, ITwinRealityData3DInfo const& info) override;
		void OnElementPropertiesRetrieved(bool bSuccess, ITwinElementProperties const& props, std::string const& ElementId) override;
		void OnIModelPropertiesRetrieved(bool bSuccess, IModelProperties const& props) override;
		void OnIModelPagedNodesRetrieved(bool /*bSuccess*/, IModelPagedNodesRes const& nodes) override;
		void OnModelFilteredNodesRetrieved(bool /*bSuccess*/, FilteredNodesRes const&, std::string const& Filter) override;
		void OnCategoryFilteredNodesRetrieved(bool /*bSuccess*/, FilteredNodesRes const&, std::string const& Filter) override;
		void OnIModelCategoryNodesRetrieved(bool /*bSuccess*/, IModelPagedNodesRes const& nodes) override;
		void OnConvertedIModelCoordsToGeoCoords(bool bSuccess, GeoCoordsReply const& geoCoords,
												RequestID const& requestId) override;
		void OnIModelQueried(bool bSuccess, std::string const& Response, RequestID const&) override;
		void OnMaterialPropertiesRetrieved(bool bSuccess, ITwinMaterialPropertiesMap const& props) override;
		void OnTextureDataRetrieved(bool bSuccess, std::string const& textureId, ITwinTextureData const& textureData) override;
		void OnMatMLPredictionRetrieved(bool bSuccess, ITwinMaterialPrediction const& prediction, std::string const& error = {}) override;
		void OnMatMLPredictionProgress(float fProgressRatio) override;

	protected:
		virtual std::string GetObserverName() const = 0;
	};

}
