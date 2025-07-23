/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinWebServicesObserver.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinWebServices/ITwinWebServicesObserver.h>
#include "Misc/AssertionMacros.h"

void FITwinDefaultWebServicesObserver::OnAuthorizationDone(bool bSuccess, std::string const& Error)
{
	ensureMsgf(false, TEXT("%s does not handle authorization"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnITwinsRetrieved(bool bSuccess, FITwinInfos const& Infos)
{
	ensureMsgf(false, TEXT("%s does not handle iTwins"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnITwinInfoRetrieved(bool bSuccess, FITwinInfo const& Info)
{
	ensureMsgf(false, TEXT("%s does not handle iTwins"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnIModelsRetrieved(bool bSuccess, FIModelInfos const& Infos)
{
	ensureMsgf(false, TEXT("%s does not handle iModels"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnRealityDataRetrieved(bool bSuccess, FITwinRealityDataInfos const& Infos)
{
	ensureMsgf(false, TEXT("%s does not handle RealityData"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnRealityData3DInfoRetrieved(bool bSuccess, FITwinRealityData3DInfo const& Infos)
{
	ensureMsgf(false, TEXT("%s does not handle RealityData"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnChangesetsRetrieved(bool bSuccess, FChangesetInfos const& ChangesetInfos)
{
	ensureMsgf(false, TEXT("%s does not handle changesets"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnExportInfosRetrieved(bool bSuccess, FITwinExportInfos const& ExportInfos)
{
	ensureMsgf(false, TEXT("%s does not handle exports"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnExportInfoRetrieved(bool bSuccess, FITwinExportInfo const& ExportInfo)
{
	ensureMsgf(false, TEXT("%s does not handle exports"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnExportStarted(bool bSuccess, FString const& InExportId)
{
	ensureMsgf(false, TEXT("%s does not handle exports"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnSavedViewInfosRetrieved(bool bSuccess, FSavedViewInfos const& Infos)
{
	ensureMsgf(false, TEXT("%s does not handle SavedViews"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnSavedViewGroupInfosRetrieved(bool bSuccess, FSavedViewGroupInfos const& Infos)
{
	ensureMsgf(false, TEXT("%s does not handle SavedViews"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnSavedViewGroupAdded(bool bSuccess, FSavedViewGroupInfo const& SavedViewGroupInfo)
{
	ensureMsgf(false, TEXT("%s does not handle SavedViews"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnSavedViewRetrieved(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo)
{
	ensureMsgf(false, TEXT("%s does not handle SavedViews"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnSavedViewExtensionRetrieved(bool bSuccess, FString const& SavedViewId, FString const& Data)
{
	ensureMsgf(false, TEXT("%s does not handle SavedViews"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnSavedViewThumbnailRetrieved(bool bSuccess, FString const& SavedViewId, TArray<uint8> const& Buffer)
{
	checkf(false, TEXT("%s does not handle SavedViews"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnSavedViewThumbnailUpdated(bool bSuccess, FString const& SavedViewId, FString const& Response)
{
	checkf(false, TEXT("%s does not handle SavedViews"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnSavedViewAdded(bool bSuccess, FSavedViewInfo const& SavedViewInfo)
{
	ensureMsgf(false, TEXT("%s does not handle SavedViews"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnSavedViewDeleted(bool bSuccess, FString const& SavedViewId, FString const& Response)
{
	ensureMsgf(false, TEXT("%s does not handle SavedViews"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnSavedViewEdited(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo)
{
	ensureMsgf(false, TEXT("%s does not handle SavedViews"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnElementPropertiesRetrieved(bool bSuccess, FElementProperties const& ElementProps, FString const& ElementId)
{
	ensureMsgf(false, TEXT("%s does not handle BIM Info"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnIModelPropertiesRetrieved(bool bSuccess, bool bHasExtents, FProjectExtents const& Extents, bool bHasEcefLocation, FEcefLocation const& EcefLocation)
{
	ensureMsgf(false, TEXT("%s does not handle querying IModel properties"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnIModelPagedNodesRetrieved(bool bSuccess, FIModelPagedNodesRes const& IModelNodes)
{
	ensureMsgf(false, TEXT("%s does not handle models tree"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnIModelCategoryNodesRetrieved(bool bSuccess, FIModelPagedNodesRes const& IModelNodes)
{
	ensureMsgf(false, TEXT("%s does not handle categories tree"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnModelFilteredNodesRetrieved(bool bSuccess, FFilteredNodesRes const& IModelNodes, FString const& Filter)
{
	ensureMsgf(false, TEXT("%s does not handle models search tree"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnCategoryFilteredNodesRetrieved(bool bSuccess, FFilteredNodesRes const& IModelNodes, FString const& Filter)
{
	ensureMsgf(false, TEXT("%s does not handle categories search tree"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnConvertedIModelCoordsToGeoCoords(bool bSuccess,
	AdvViz::SDK::GeoCoordsReply const& GeoCoords, HttpRequestID const& RequestID)
{
	ensureMsgf(false, TEXT("%s does not handle converting IModel coords"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnIModelQueried(bool bSuccess, FString const& QueryResult, HttpRequestID const&)
{
	ensureMsgf(false, TEXT("%s does not handle querying iModels"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnMaterialPropertiesRetrieved(bool bSuccess, AdvViz::SDK::ITwinMaterialPropertiesMap const& )
{
	ensureMsgf(false, TEXT("%s does not handle querying material properties"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnTextureDataRetrieved(bool bSuccess, std::string const& , AdvViz::SDK::ITwinTextureData const& )
{
	ensureMsgf(false, TEXT("%s does not handle querying texture data"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnMatMLPredictionRetrieved(bool bSuccess, AdvViz::SDK::ITwinMaterialPrediction const&, std::string const& )
{
	ensureMsgf(false, TEXT("%s does not handle material predictions"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnMatMLPredictionProgress(float )
{
	ensureMsgf(false, TEXT("%s does not handle material predictions"), GetObserverName());
}
