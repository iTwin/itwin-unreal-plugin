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
struct FITwinInfo;
struct FITwinInfos;
struct FITwinExportInfos;
struct FITwinExportInfo;
struct FITwinRealityDataInfos;
struct FITwinRealityData3DInfo;
struct FSavedViewInfos;
struct FSavedView;
struct FSavedViewInfo;

class ITWINRUNTIME_API IITwinWebServicesObserver : public IITwinAuthorizationObserver
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
	virtual void OnSavedViewAdded(bool bSuccess, FSavedViewInfo const& SavedViewInfo) = 0;
	virtual void OnSavedViewDeleted(bool bSuccess, FString const& Response) = 0;
	virtual void OnSavedViewEdited(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo) = 0;
};


/// Implements all the methods from interface IITwinWebServicesObserver with default implementation
/// doing nothing but triggering an assert (this intermediate class was added to make it easier to
/// add a new abstract method).
class ITWINRUNTIME_API FITwinDefaultWebServicesObserver : public IITwinWebServicesObserver
{
public:
	/// overridden from IITwinWebServicesObserver:
	virtual void OnAuthorizationDone(bool bSuccess, FString const& Error) override;
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
	virtual void OnSavedViewAdded(bool bSuccess, FSavedViewInfo const& SavedViewInfo) override;
	virtual void OnSavedViewDeleted(bool bSuccess, FString const& Response) override;
	virtual void OnSavedViewEdited(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo) override;

protected:
	virtual const TCHAR* GetObserverName() const = 0;
};
