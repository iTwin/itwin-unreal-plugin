/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinWebServices.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <mutex>

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include <ITwinServerConnection.h>
#include "ITwinAuthorizationObserver.h"
#include "ITwinWebServices_Info.h"
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
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnGetSavedViewComplete, bool, bSuccess, FSavedView, SavedView, FSavedViewInfo, SavedViewInfo);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGetRealityDataComplete, bool, bSuccess, FITwinRealityDataInfos, RealityDataInfos);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGetRealityData3DInfoComplete, bool, bSuccess, FITwinRealityData3DInfo, RealityDataInfo);

class FJsonObject;
class IITwinWebServicesObserver;

UCLASS(BlueprintType)
class ITWINRUNTIME_API UITwinWebServices : public UObject, public IITwinAuthorizationObserver
{
	GENERATED_BODY()
public:
	UITwinWebServices();

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void CheckAuthorization();

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
	void GetExports(FString iModelId, FString iChangesetId);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void GetExportInfo(FString ExportId);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void StartExport(FString iModelId, FString iChangesetId);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void GetRealityData(FString iTwinId);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void GetRealityData3DInfo(FString iTwinId, FString RealityDataId);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void AddSavedView(FString ITwinId, FString IModelId, FSavedView SavedView, FSavedViewInfo SavedViewInfo);
	void OnSavedViewAdded(bool bSuccess, FSavedViewInfo const& SavedViewInfo);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void DeleteSavedView(FString SavedViewId);
	void OnSavedViewDeleted(bool bSuccess, FString const& SavedViewId, FString const& Response) const;

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void EditSavedView(FSavedView SavedView, FSavedViewInfo SavedViewInfo);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void GetAllSavedViews(FString iTwinId, FString iModelId);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	void GetSavedView(FString SavedViewId);

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
	FOnGetRealityDataComplete OnGetRealityDataComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnGetRealityData3DInfoComplete OnGetRealityData3DInfoComplete;

	UPROPERTY(EditAnywhere, Category = "iTwin Web Services")
	EITwinEnvironment Environment = EITwinEnvironment::Prod;


	bool IsAuthorizationInProgress() const;
	void SetServerConnection(TObjectPtr<AITwinServerConnection> const& InConnection);

	void GetServerConnection(TObjectPtr<AITwinServerConnection>& OutConnection) const;
	bool HasSameConnection(AITwinServerConnection const* Connection) const;

	void SetObserver(IITwinWebServicesObserver* InObserver);
	bool HasObserver(IITwinWebServicesObserver const* Observer) const;

	static void SetITwinAppIDArray(ITwin::AppIDArray const& iTwinAppIDs);

	static UITwinWebServices* GetWorkingInstance();

	static bool SaveToken(FString const& InInfo, EITwinEnvironment Env);
	static bool LoadToken(FString& OutInfo, EITwinEnvironment Env);
	static void DeleteTokenFile(EITwinEnvironment Env);

	static bool GetErrorDescription(FJsonObject const& responseJson, FString& OutError,
		FString const& Indent = {});

	static void SetLogErrors(bool bInLogErrors);
	static bool ShouldLogErrors() { return bLogErrors; }

#if WITH_TESTS
	void SetTestServerURL(FString const& ServerUrl);
	static void SetupTestMode(EITwinEnvironment Env, FString const& TokenFileSuffix);
#endif


private:
	FString GetAPIRootURL() const;
	bool TryGetServerConnection(bool bAllowBroadcastAuthResult);
	void OnAuthDoneImpl(bool bSuccess, FString const& Error, bool bBroadcastResult = true);

	virtual void OnAuthorizationDone(bool bSuccess, FString const& Error) override;
	FString GetAuthToken() const;

	void DoGetiModelChangesets(FString const& iModelId, bool bRestrictToLatest);

	/// This Request ID is relative to each instance of UITwinWebServices, it is *not* a global unique
	/// identifier for requests (hence it should be kept it private...)
	/// For now its only purpose is to test if the last error message was created for current request or not.
	using RequestID = uint32;

	void SetLastError(FString const& InError, RequestID InRequestId);

	//! Returns the error stored for the given request, if any.
	FString GetRequestError(RequestID InRequestId) const;

	struct FITwinAPIRequestInfo;

	template <typename ResultDataType, class FunctorType, class DelegateAsFunctor>
	RequestID TProcessHttpRequest(
		FITwinAPIRequestInfo const& RequestInfo,
		FunctorType&& InFunctor,
		DelegateAsFunctor&& InResultFunctor);

private:
	UPROPERTY()
	TObjectPtr<AITwinServerConnection> ServerConnection;

	class FImpl;
	TPimplPtr<FImpl> Impl;

	static bool bLogErrors;
};
