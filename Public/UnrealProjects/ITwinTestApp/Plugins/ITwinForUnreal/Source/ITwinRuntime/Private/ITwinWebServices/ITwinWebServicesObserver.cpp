/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinWebServicesObserver.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinWebServices/ITwinWebServicesObserver.h>


void FITwinDefaultWebServicesObserver::OnAuthorizationDone(bool bSuccess, FString const& Error)
{
	checkf(false, TEXT("%s does not handle authorization"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnITwinsRetrieved(bool bSuccess, FITwinInfos const& Infos)
{
	checkf(false, TEXT("%s does not handle iTwins"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnITwinInfoRetrieved(bool bSuccess, FITwinInfo const& Info)
{
	checkf(false, TEXT("%s does not handle iTwins"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnIModelsRetrieved(bool bSuccess, FIModelInfos const& Infos)
{
	checkf(false, TEXT("%s does not handle iModels"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnRealityDataRetrieved(bool bSuccess, FITwinRealityDataInfos const& Infos)
{
	checkf(false, TEXT("%s does not handle RealityData"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnRealityData3DInfoRetrieved(bool bSuccess, FITwinRealityData3DInfo const& Infos)
{
	checkf(false, TEXT("%s does not handle RealityData"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnChangesetsRetrieved(bool bSuccess, FChangesetInfos const& ChangesetInfos)
{
	checkf(false, TEXT("%s does not handle changesets"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnExportInfosRetrieved(bool bSuccess, FITwinExportInfos const& ExportInfos)
{
	checkf(false, TEXT("%s does not handle exports"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnExportInfoRetrieved(bool bSuccess, FITwinExportInfo const& ExportInfo)
{
	checkf(false, TEXT("%s does not handle exports"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnExportStarted(bool bSuccess, FString const& InExportId)
{
	checkf(false, TEXT("%s does not handle exports"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnSavedViewInfosRetrieved(bool bSuccess, FSavedViewInfos const& Infos)
{
	checkf(false, TEXT("%s does not handle SavedViews"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnSavedViewRetrieved(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo)
{
	checkf(false, TEXT("%s does not handle SavedViews"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnSavedViewAdded(bool bSuccess, FSavedViewInfo const& SavedViewInfo)
{
	checkf(false, TEXT("%s does not handle SavedViews"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnSavedViewDeleted(bool bSuccess, FString const& SavedViewId, FString const& Response)
{
	checkf(false, TEXT("%s does not handle SavedViews"), GetObserverName());
}
void FITwinDefaultWebServicesObserver::OnSavedViewEdited(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo)
{
	checkf(false, TEXT("%s does not handle SavedViews"), GetObserverName());
}
