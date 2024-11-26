/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinWebServicesObserver.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinWebServicesObserver.h"


//
// #define ITWIN_SDK_WARN(FORMAT, ...) UE_LOG(ITwinSDK, Warning, FORMAT, ##__VA_ARGS__)
//
// TODO_LC: how to deal with errors in the SDK? (knowing that we should not throw exceptions in all cases
// typically in Unreal...)
#define ITWIN_SDK_WARN(FORMAT, ...)

namespace SDK::Core
{
	void ITwinDefaultWebServicesObserver::OnRequestError(std::string const& /*strError*/)
	{

	}
	void ITwinDefaultWebServicesObserver::OnITwinsRetrieved(bool /*bSuccess*/, ITwinInfos const&)
	{
		ITWIN_SDK_WARN(GetObserverName() + " does not handle iTwins");
	}
	void ITwinDefaultWebServicesObserver::OnITwinInfoRetrieved(bool /*bSuccess*/, ITwinInfo const&)
	{
		ITWIN_SDK_WARN(GetObserverName() + " does not handle iTwins");
	}
	void ITwinDefaultWebServicesObserver::OnIModelsRetrieved(bool /*bSuccess*/, IModelInfos const&)
	{
		ITWIN_SDK_WARN(GetObserverName() + " does not handle iModels");
	}
	void ITwinDefaultWebServicesObserver::OnChangesetsRetrieved(bool /*bSuccess*/, ChangesetInfos const&)
	{
		ITWIN_SDK_WARN(GetObserverName() + " does not handle changesets");
	}
	void ITwinDefaultWebServicesObserver::OnExportInfosRetrieved(bool /*bSuccess*/, ITwinExportInfos const&)
	{
		ITWIN_SDK_WARN(GetObserverName() + " does not handle exports");
	}
	void ITwinDefaultWebServicesObserver::OnExportInfoRetrieved(bool /*bSuccess*/, ITwinExportInfo const&)
	{
		ITWIN_SDK_WARN(GetObserverName() + " does not handle exports");
	}
	void ITwinDefaultWebServicesObserver::OnExportStarted(bool /*bSuccess*/, std::string const&)
	{
		ITWIN_SDK_WARN(GetObserverName() + " does not handle exports");
	}
	void ITwinDefaultWebServicesObserver::OnSavedViewInfosRetrieved(bool /*bSuccess*/, SavedViewInfos const&)
	{
		ITWIN_SDK_WARN(GetObserverName() + " does not handle SavedViews");
	}
	void ITwinDefaultWebServicesObserver::OnSavedViewGroupInfosRetrieved(bool /*bSuccess*/, SavedViewGroupInfos const&)
	{
		ITWIN_SDK_WARN(GetObserverName() + " does not handle SavedViews");
	}
	void ITwinDefaultWebServicesObserver::OnSavedViewGroupAdded(bool /*bSuccess*/, SavedViewGroupInfo const&)
	{
		ITWIN_SDK_WARN(GetObserverName() + " does not handle SavedViews");
	}
	void ITwinDefaultWebServicesObserver::OnSavedViewRetrieved(bool /*bSuccess*/, SavedView const&, SavedViewInfo const&)
	{
		ITWIN_SDK_WARN(GetObserverName() + " does not handle SavedViews");
	}
	void ITwinDefaultWebServicesObserver::OnSavedViewExtensionRetrieved(bool /*bSuccess*/, std::string const&, std::string const&)
	{
		ITWIN_SDK_WARN(GetObserverName() + " does not handle SavedViews");
	}
	void ITwinDefaultWebServicesObserver::OnSavedViewThumbnailRetrieved(bool /*bSuccess*/, std::string const&, std::string const&)
	{
		ITWIN_SDK_WARN(GetObserverName() + " does not handle SavedViews");
	}
	void ITwinDefaultWebServicesObserver::OnSavedViewThumbnailUpdated(bool /*bSuccess*/, std::string const&, std::string const&)
	{
		ITWIN_SDK_WARN(GetObserverName() + " does not handle SavedViews");
	}
	void ITwinDefaultWebServicesObserver::OnSavedViewAdded(bool /*bSuccess*/, SavedViewInfo const&)
	{
		ITWIN_SDK_WARN(GetObserverName() + " does not handle SavedViews");
	}
	void ITwinDefaultWebServicesObserver::OnSavedViewDeleted(bool /*bSuccess*/, std::string const&, std::string const&)
	{
		ITWIN_SDK_WARN(GetObserverName() + " does not handle SavedViews");
	}
	void ITwinDefaultWebServicesObserver::OnSavedViewEdited(bool /*bSuccess*/, SavedView const&, SavedViewInfo const&)
	{
		ITWIN_SDK_WARN(GetObserverName() + " does not handle SavedViews");
	}
	void ITwinDefaultWebServicesObserver::OnRealityDataRetrieved(bool /*bSuccess*/, ITwinRealityDataInfos const&)
	{
		ITWIN_SDK_WARN(GetObserverName() + " does not handle RealityData");
	}
	void ITwinDefaultWebServicesObserver::OnRealityData3DInfoRetrieved(bool /*bSuccess*/, ITwinRealityData3DInfo const&)
	{
		ITWIN_SDK_WARN(GetObserverName() + " does not handle RealityData");
	}
	void ITwinDefaultWebServicesObserver::OnElementPropertiesRetrieved(bool /*bSuccess*/, ITwinElementProperties const&)
	{
		ITWIN_SDK_WARN(GetObserverName() + " does not handle Element properties");
	}
	void ITwinDefaultWebServicesObserver::OnIModelPropertiesRetrieved(bool /*bSuccess*/, IModelProperties const&)
	{
		ITWIN_SDK_WARN(GetObserverName() + " does not handle IModel properties");
	}
	void ITwinDefaultWebServicesObserver::OnIModelQueried(bool /*bSuccess*/, std::string const&, RequestID const&)
	{
		ITWIN_SDK_WARN(GetObserverName() + " does not handle querying IModel");
	}
	void ITwinDefaultWebServicesObserver::OnMaterialPropertiesRetrieved(bool /*bSuccess*/, ITwinMaterialPropertiesMap const&)
	{
		ITWIN_SDK_WARN(GetObserverName() + " does not handle material properties");
	}
}
