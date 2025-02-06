/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinWebServices.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <functional>
#include <mutex>

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include <ITwinServerConnection.h>
#include "ITwinWebServices_Info.h"
#include <MaterialPrediction/ITwinMaterialPredictionStatus.h>

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/ITwinAPI/ITwinAuthObserver.h>
#	include <SDK/Core/ITwinAPI/ITwinAuthStatus.h>
#	include <SDK/Core/ITwinAPI/ITwinRequestTypes.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

namespace SDK::Core
{
	class ITwinAuthManager;
}

#include "ITwinWebServices.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAuthorizationChecked, bool, bSuccess, FString, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGetITwinInfoComplete, bool, bSuccess, FITwinInfo, iTwin);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGetiTwinsComplete, bool, bSuccess, FITwinInfos, iTwins);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGetiTwiniModelsComplete, bool, bSuccess, FIModelInfos, iModels);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGetiModelChangesetsComplete, bool, bSuccess, FChangesetInfos, Changesets);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGetExportsComplete, bool, bSuccess, FITwinExportInfos, Exports);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGetExportInfoComplete, bool, bSuccess, FITwinExportInfo, Export);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnStartExportComplete, bool, bSuccess, FString, ExportId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAddSavedViewComplete, bool, bSuccess, FSavedViewInfo, SavedViewInfo);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnDeleteSavedViewComplete, bool, bSuccess, FString, SavedViewId, FString, Response);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnEditSavedViewComplete, bool, bSuccess, FSavedView, SavedView, FSavedViewInfo, SavedViewInfo);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGetSavedViewsComplete, bool, bSuccess, FSavedViewInfos, SavedViews);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGetSavedViewGroupsComplete, bool, bSuccess, FSavedViewGroupInfos, SavedViewGroups);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAddSavedViewGroupComplete, bool, bSuccess, FSavedViewGroupInfo, SavedViewGroupInfo);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnGetSavedViewComplete, bool, bSuccess, FSavedView, SavedView, FSavedViewInfo, SavedViewInfo);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnGetSavedViewExtensionComplete, bool, bSuccess, FString, SavedViewId, FString, ExtensionData);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnGetSavedViewThumbnailComplete, bool, bSuccess, FString, SavedViewThumbnail, FString, SavedViewId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnUpdateSavedViewThumbnailComplete, bool, bSuccess, FString, SavedViewId, FString, Response);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGetRealityDataComplete, bool, bSuccess, FITwinRealityDataInfos, RealityDataInfos);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGetRealityData3DInfoComplete, bool, bSuccess, FITwinRealityData3DInfo, RealityDataInfo);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnGetElementPropertiesComplete, bool, bSuccess, FElementProperties, ElementProps, FString, ElementId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FOnGetIModelPropertiesComplete, bool, bSuccess, bool, bHasExtents, FProjectExtents, Extents, bool, bHasEcefLocation, FEcefLocation, EcefLocation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQueryIModelComplete, bool, bSuccess, FString, QueryResult);

class FJsonObject;
class FJsonQueriesCache;
class IITwinWebServicesObserver;
using HttpRequestID = FString;

UCLASS(BlueprintType)
class ITWINRUNTIME_API UITwinWebServices : public UObject, public SDK::Core::ITwinAuthObserver
{
	GENERATED_BODY()
public:
	UITwinWebServices();

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	bool CheckAuthorization();

	SDK::Core::EITwinAuthStatus CheckAuthorizationStatus();

	//! Returns the last error encountered, if any.
	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	FString GetLastError() const;

	//! Returns the last error encountered, if any, and resets it.
	//! Returns whether an error message actually existed.
	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	bool ConsumeLastError(FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void GetiTwins();

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void GetITwinInfo(FString iTwinId);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void GetiTwiniModels(FString iTwinId);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void GetiModelChangesets(FString iModelId);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void GetiModelLatestChangeset(FString iModelId);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void GetExports(FString iModelId, FString ChangesetId);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void GetExportInfo(FString ExportId);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void StartExport(FString iModelId, FString ChangesetId);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void GetAllSavedViews(FString iTwinId, FString iModelId, FString GroupId = "", int Top = 100, int Skip = 0);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void GetSavedViewGroups(FString iTwinId, FString iModelId = "");

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void AddSavedViewGroup(FString ITwinId, FString IModelId, FSavedViewGroupInfo SavedViewGroupInfo);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void GetSavedView(FString SavedViewId);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void GetSavedViewExtension(FString SavedViewId, FString ExtensionName);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void GetSavedViewThumbnail(FString SavedViewId);
	
	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void UpdateSavedViewThumbnail(FString SavedViewId, FString ThumbnailURL);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void AddSavedView(FString ITwinId, FSavedView SavedView, FSavedViewInfo SavedViewInfo, FString IModelId = "", FString GroupId = "");
	void OnSavedViewAdded(bool bSuccess, FSavedViewInfo const& SavedViewInfo);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void DeleteSavedView(FString SavedViewId);
	void OnSavedViewDeleted(bool bSuccess, FString const& SavedViewId, FString const& Response) const;

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void EditSavedView(FSavedView SavedView, FSavedViewInfo SavedViewInfo);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void GetRealityData(FString iTwinId);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void GetRealityData3DInfo(FString iTwinId, FString RealityDataId);


	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void GetElementProperties(FString iTwinId, FString iModelId, FString ChangesetId, FString ElementId);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void GetIModelProperties(FString iTwinId, FString iModelId, FString ChangesetId);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void QueryIModel(FString iTwinId, FString iModelId, FString ChangesetId, FString ECSQLQuery, int Offset,
					 int Count);
	SDK::Core::ITwinAPIRequestInfo InfosToQueryIModel(FString iTwinId, FString iModelId,
		FString ChangesetId, FString ECSQLQuery, int Offset, int Count);
	void QueryIModelRows(FString iTwinId, FString iModelId, FString ChangesetId,
		FString ECSQLQuery, int Offset, int Count, std::function<void(HttpRequestID)>&& NotifyRequestID,
		SDK::Core::ITwinAPIRequestInfo const* RequestInfo = nullptr);

	void GetMaterialProperties(
		FString iTwinId, FString iModelId, FString ChangesetId,
		FString MaterialId);
	void GetMaterialListProperties(
		FString iTwinId, FString iModelId, FString ChangesetId,
		TArray<FString> MaterialIds);
	void GetTextureData(
		FString iTwinId, FString iModelId, FString ChangesetId,
		FString TextureId);

	//!------------------------------------------------------------------------------------------------------
	//! WORK IN PROGRESS - UNRELEASED - material predictions using machine learning.
	//!
	//! Change the server to be compatible with ML Material Assignment. Beware it will make all other iTwin
	//! services unavailable from this actor, since the base URL is different...
	void SetupForMaterialMLPrediction();
	EITwinMaterialPredictionStatus GetMaterialMLPrediction(FString iTwinId, FString iModelId,
														   FString ChangesetId);
	//-------------------------------------------------------------------------------------------------------


	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnAuthorizationChecked OnAuthorizationChecked;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnGetiTwinsComplete OnGetiTwinsComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnGetITwinInfoComplete OnGetITwinInfoComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnGetiTwiniModelsComplete OnGetiTwiniModelsComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnGetiModelChangesetsComplete OnGetiModelChangesetsComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnGetExportsComplete OnGetExportsComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnGetExportInfoComplete OnGetExportInfoComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnStartExportComplete OnStartExportComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnAddSavedViewComplete OnAddSavedViewComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnDeleteSavedViewComplete OnDeleteSavedViewComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnEditSavedViewComplete OnEditSavedViewComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnGetSavedViewsComplete OnGetSavedViewsComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnGetSavedViewComplete OnGetSavedViewComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnGetSavedViewExtensionComplete OnGetSavedViewExtensionComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnGetSavedViewGroupsComplete OnGetSavedViewGroupsComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnAddSavedViewGroupComplete OnAddSavedViewGroupComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnGetSavedViewThumbnailComplete OnGetSavedViewThumbnailComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnUpdateSavedViewThumbnailComplete OnUpdateSavedViewThumbnailComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnGetRealityDataComplete OnGetRealityDataComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnGetRealityData3DInfoComplete OnGetRealityData3DInfoComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnGetElementPropertiesComplete OnGetElementPropertiesComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnGetIModelPropertiesComplete OnGetIModelPropertiesComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnQueryIModelComplete OnQueryIModelComplete;

	UFUNCTION(BlueprintGetter, Category = "iTwin Web Services")
	EITwinEnvironment GetEnvironment() const { return Environment; }

	UFUNCTION(BlueprintSetter, Category = "iTwin Web Services")
	void SetEnvironment(EITwinEnvironment InEnvironment);

	bool IsAuthorizationInProgress() const;
	using AuthManagerPtr = std::shared_ptr<SDK::Core::ITwinAuthManager>;
	AuthManagerPtr& GetAuthManager() const;
	void SetServerConnection(TObjectPtr<AITwinServerConnection> const& InConnection);

	//! Initialize the server connection from the level, if all connection actors in the level are using the
	//! same environment. If no server connection actor exists in the world, or if there are several ones not
	//! sharing the same environment, this WebServices actor will not be modified and false is returned.
	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	bool InitServerConnectionFromWorld();

	void GetServerConnection(TObjectPtr<AITwinServerConnection>& OutConnection) const;
	bool HasSameConnection(AITwinServerConnection const* Connection) const;

	void SetObserver(IITwinWebServicesObserver* InObserver);
	bool HasObserver(IITwinWebServicesObserver const* Observer) const;

	static void SetITwinAppIDArray(ITwin::AppIDArray const& iTwinAppIDs);

	//! Can be called to customize the scopes used to request the authorization.
	static void AddScope(FString const& ExtraScope);

	static void SetPreferredEnvironment(EITwinEnvironment Env);


	static bool GetActiveConnection(TObjectPtr<AITwinServerConnection>& OutConnection,
		const UObject* WorldContextObject);

	static void SetLogErrors(bool bInLogErrors);
	static bool ShouldLogErrors() { return bLogErrors; }

#if WITH_TESTS
	void SetTestServerURL(FString const& ServerUrl);
	static void SetupTestMode(EITwinEnvironment Env, FString const& TokenFileSuffix);
#endif


private:
	/// Returns the current instance, ie. the one currently processing a request response callback.
	/// Beware it will return null as soon as we are not currently executing such callback, which can be a
	/// source of confusion and bugs.
	/// \see GetActiveConnection, in which we handle the null case, for example (only working if we do not
	/// mix different environments in a same session...)
	static UITwinWebServices* GetWorkingInstance();

	bool TryGetServerConnection(bool bAllowBroadcastAuthResult);
	void OnAuthDoneImpl(bool bSuccess, std::string const& Error, bool bBroadcastResult = true);

	virtual void OnAuthorizationDone(bool bSuccess, std::string const& Error) override;

	void DoGetiModelChangesets(FString const& iModelId, bool bRestrictToLatest);

	//! Returns the error stored for the given request, if any.
	FString GetRequestError(HttpRequestID const& InRequestId) const;

	template <typename FunctorType>
	void DoRequest(FunctorType&& InFunctor);
	template <typename FunctorType>
	HttpRequestID DoRequestRetID(FunctorType&& InFunctor);

private:
	UPROPERTY()
	TObjectPtr<AITwinServerConnection> ServerConnection;

	UPROPERTY(EditAnywhere, Category = "iTwin Web Services",
		BlueprintGetter = GetEnvironment, BlueprintSetter = SetEnvironment)
	EITwinEnvironment Environment = EITwinEnvironment::Prod;

	class FImpl;
	TPimplPtr<FImpl> Impl;

	static bool bLogErrors;
};
