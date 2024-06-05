/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinWebServicesObserver.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "ITwinAuthorizationObserver.h"

struct FChangesetInfos;
struct FIModelInfos;
struct FITwinInfos;
struct FITwinExportInfos;
struct FITwinExportInfo;
struct FITwinRealityDataInfos;
struct FSavedViewInfos;
struct FSavedView;
struct FSavedViewInfo;

class ITWINRUNTIME_API IITwinWebServicesObserver : public IITwinAuthorizationObserver
{
public:
	virtual ~IITwinWebServicesObserver() = default;

	virtual void OnITwinsRetrieved(bool bSuccess, FITwinInfos const& Infos) = 0;

	virtual void OnIModelsRetrieved(bool bSuccess, FIModelInfos const& Infos) = 0;

	virtual void OnRealityDataRetrieved(bool bSuccess, FITwinRealityDataInfos const& Infos) = 0;

	virtual void OnChangesetsRetrieved(bool bSuccess, FChangesetInfos const& ChangesetInfos) = 0;

	virtual void OnExportInfosRetrieved(bool bSuccess, FITwinExportInfos const& ExportInfos) = 0;
	virtual void OnExportInfoRetrieved(bool bSuccess, FITwinExportInfo const& ExportInfo) = 0;
	virtual void OnExportStarted(bool bSuccess, FString const& ExportId) = 0;

	virtual void OnSavedViewInfosRetrieved(bool bSuccess, FSavedViewInfos const& Infos) = 0;
	virtual void OnSavedViewRetrieved(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo) = 0;
	virtual void OnSavedViewAdded(bool bSuccess, FSavedViewInfo const& SavedViewInfo) = 0;
	virtual void OnSavedViewDeleted(bool bSuccess, FString const& Response) = 0;
	virtual void OnSavedViewEdited(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo) = 0;
};
