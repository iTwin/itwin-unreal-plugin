/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinWebServicesObserver.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/ITwinAPI/ITwinAuthObserver.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include "ITwinWebServices_Info.h"

struct FChangesetInfos;
struct FIModelInfos;
struct FITwinInfo;
struct FITwinInfos;
struct FITwinExportInfos;
struct FITwinExportInfo;
struct FITwinRealityDataInfos;
struct FITwinRealityData3DInfo;
struct FSavedViewInfos;
struct FSavedView;
struct FSavedViewInfo;
struct FSavedViewGroupInfo;
struct FSavedViewGroupInfos;
struct FElementProperties;
struct FProjectExtents;
struct FEcefLocation;

namespace AdvViz::SDK {
	struct ITwinMaterialPropertiesMap;
	struct ITwinMaterialPrediction;
	struct ITwinTextureData;
	struct GeoCoordsReply;
}

using HttpRequestID = FString;

class ITWINRUNTIME_API IITwinWebServicesObserver : public AdvViz::SDK::ITwinAuthObserver
{
public:
	virtual ~IITwinWebServicesObserver() = default;

	virtual void OnITwinsRetrieved(bool bSuccess, FITwinInfos const& Infos) = 0;

	virtual void OnITwinInfoRetrieved(bool bSuccess, FITwinInfo const& Info) = 0;

	virtual void OnIModelsRetrieved(bool bSuccess, FIModelInfos const& Infos) = 0;

	virtual void OnRealityDataRetrieved(bool bSuccess, FITwinRealityDataInfos const& Infos) = 0;
	virtual void OnRealityData3DInfoRetrieved(bool bSuccess, FITwinRealityData3DInfo const& Info) = 0;

	virtual void OnChangesetsRetrieved(bool bSuccess, FChangesetInfos const& ChangesetInfos) = 0;

	virtual void OnExportInfosRetrieved(bool bSuccess, FITwinExportInfos const& ExportInfos) = 0;
	virtual void OnExportInfoRetrieved(bool bSuccess, FITwinExportInfo const& ExportInfo) = 0;
	virtual void OnExportStarted(bool bSuccess, FString const& ExportId) = 0;

	virtual void OnSavedViewInfosRetrieved(bool bSuccess, FSavedViewInfos const& Infos) = 0;
	virtual void OnSavedViewRetrieved(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo) = 0;
	virtual void OnSavedViewExtensionRetrieved(bool bSuccess, FString const& SavedViewId, FString const& Data) = 0;
	virtual void OnSavedViewThumbnailRetrieved(bool bSuccess, FString const& SavedViewId, TArray<uint8> const& Buffer) = 0;
	virtual void OnSavedViewThumbnailUpdated(bool bSuccess, FString const& SavedViewId, FString const& Response) = 0;
	virtual void OnSavedViewGroupInfosRetrieved(bool bSuccess, FSavedViewGroupInfos const& Infos) = 0;
	virtual void OnSavedViewGroupAdded(bool bSuccess, FSavedViewGroupInfo const& SavedViewGroupInfo) = 0;
	virtual void OnSavedViewAdded(bool bSuccess, FSavedViewInfo const& SavedViewInfo) = 0;
	virtual void OnSavedViewDeleted(bool bSuccess, FString const& SavedViewId, FString const& Response) = 0;
	virtual void OnSavedViewEdited(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo) = 0;
	virtual void OnElementPropertiesRetrieved(bool bSuccess, FElementProperties const& ElementProps, FString const& ElementId) = 0;
	virtual void OnIModelPropertiesRetrieved(bool bSuccess, bool bHasExtents, FProjectExtents const& Extents, bool bHasEcefLocation, FEcefLocation const& EcefLocation) = 0;
	virtual void OnConvertedIModelCoordsToGeoCoords(bool bSuccess,
		AdvViz::SDK::GeoCoordsReply const& GeoCoords, HttpRequestID const& RequestID) = 0;
	virtual void OnIModelQueried(bool bSuccess, FString const& QueryResult, HttpRequestID const& RequestID) = 0;

	virtual void OnMaterialPropertiesRetrieved(bool bSuccess, AdvViz::SDK::ITwinMaterialPropertiesMap const& props) = 0;
	virtual void OnTextureDataRetrieved(bool bSuccess, std::string const& textureId, AdvViz::SDK::ITwinTextureData const& textureData) = 0;
	virtual void OnMatMLPredictionRetrieved(bool bSuccess, AdvViz::SDK::ITwinMaterialPrediction const& prediction, std::string const& error = {}) = 0;
	virtual void OnMatMLPredictionProgress(float fProgressRatio) = 0;
};


/// Implements all the methods from interface IITwinWebServicesObserver with default implementation
/// doing nothing but triggering an assert (this intermediate class was added to make it easier to
/// add a new abstract method).
class ITWINRUNTIME_API FITwinDefaultWebServicesObserver : public IITwinWebServicesObserver
{
public:
	/// overridden from IITwinWebServicesObserver:
	virtual void OnAuthorizationDone(bool bSuccess, std::string const& Error) override;
	virtual void OnITwinsRetrieved(bool bSuccess, FITwinInfos const& Infos) override;
	virtual void OnITwinInfoRetrieved(bool bSuccess, FITwinInfo const& Info) override;
	virtual void OnIModelsRetrieved(bool bSuccess, FIModelInfos const& Infos) override;
	virtual void OnRealityDataRetrieved(bool bSuccess, FITwinRealityDataInfos const& Infos) override;
	virtual void OnRealityData3DInfoRetrieved(bool bSuccess, FITwinRealityData3DInfo const& Info) override;
	virtual void OnChangesetsRetrieved(bool bSuccess, FChangesetInfos const& ChangesetInfos) override;
	virtual void OnExportInfosRetrieved(bool bSuccess, FITwinExportInfos const& ExportInfos) override;
	virtual void OnExportInfoRetrieved(bool bSuccess, FITwinExportInfo const& ExportInfo) override;
	virtual void OnExportStarted(bool bSuccess, FString const& InExportId) override;
	virtual void OnSavedViewInfosRetrieved(bool bSuccess, FSavedViewInfos const& Infos) override;
	virtual void OnSavedViewRetrieved(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo) override;
	virtual void OnSavedViewExtensionRetrieved(bool bSuccess, FString const& SavedViewId, FString const& Data) override;
	virtual void OnSavedViewThumbnailRetrieved(bool bSuccess, FString const& SavedViewId, TArray<uint8> const& Buffer) override;
	virtual void OnSavedViewThumbnailUpdated(bool bSuccess, FString const& SavedViewId, FString const& Response) override;
	virtual void OnSavedViewGroupInfosRetrieved(bool bSuccess, FSavedViewGroupInfos const& Infos) override;
	virtual void OnSavedViewGroupAdded(bool bSuccess, FSavedViewGroupInfo const& SavedViewGroupInfo) override;
	virtual void OnSavedViewAdded(bool bSuccess, FSavedViewInfo const& SavedViewInfo) override;
	virtual void OnSavedViewDeleted(bool bSuccess, FString const& SavedViewId, FString const& Response) override;
	virtual void OnSavedViewEdited(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo) override;
	virtual void OnElementPropertiesRetrieved(bool bSuccess, FElementProperties const& ElementProps, FString const& ElementId) override;
	virtual void OnIModelPropertiesRetrieved(bool bSuccess, bool bHasExtents, FProjectExtents const& Extents, bool bHasEcefLocation, FEcefLocation const& EcefLocation) override;
	virtual void OnConvertedIModelCoordsToGeoCoords(bool bSuccess,
		AdvViz::SDK::GeoCoordsReply const& GeoCoords, HttpRequestID const& RequestID) override;
	virtual void OnIModelQueried(bool bSuccess, FString const& QueryResult, HttpRequestID const& RequestID) override;

	virtual void OnMaterialPropertiesRetrieved(bool bSuccess, AdvViz::SDK::ITwinMaterialPropertiesMap const& props) override;
	virtual void OnTextureDataRetrieved(bool bSuccess, std::string const& textureId, AdvViz::SDK::ITwinTextureData const& textureData) override;
	virtual void OnMatMLPredictionRetrieved(bool bSuccess, AdvViz::SDK::ITwinMaterialPrediction const& prediction, std::string const& error = {}) override;
	virtual void OnMatMLPredictionProgress(float fProgressRatio) override;

protected:
	virtual const TCHAR* GetObserverName() const = 0;
};
