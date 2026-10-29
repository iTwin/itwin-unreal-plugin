/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinWebServices.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <functional>
#include <mutex>

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include <ITwinEnvironmentEnum.h>
#include "ITwinWebServices_Info.h"

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Network/Network.h>
#	include <SDK/Core/ITwinAPI/ITwinAuthObserver.h>
#	include <SDK/Core/ITwinAPI/ITwinAuthStatus.h>
#	include <SDK/Core/ITwinAPI/ITwinRequestTypes.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

namespace AdvViz::SDK
{
	class ITwinAuthManager;

	enum class EITwinAuthOverrideMode : uint8_t;
}

#include "ITwinWebServices.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAuthorizationChecked, bool, bSuccess, FString, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGetITwinInfoComplete, bool, bSuccess, FITwinInfo, iTwin);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGetiTwinsComplete, bool, bSuccess, FITwinInfos, iTwins);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGetiTwiniModelsComplete, bool, bSuccess, FIModelInfos, iModels);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGetIModelProcessingStatusComplete, bool, bSuccess, FIModelProcessingStatus, ProcessingStatus);
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
class ITWINRUNTIME_API UITwinWebServices : public UObject, public AdvViz::SDK::ITwinAuthObserver
{
	GENERATED_BODY()
public:
	// Optional callback called (if any) before the 1st authorization occurs, and returning the
	// environment imposed by the potential override access token.
	using PreAuthFunc = TFunction<EITwinEnvironment()>;

	UITwinWebServices();

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	bool CheckAuthorization();

	AdvViz::SDK::EITwinAuthStatus CheckAuthorizationStatus();

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	static void SetUseExternalBrowser(bool bInUseExternalBrowser);

	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	static bool UseExternalBrowser();

	//! Initiates the authorization process, but instead of opening an external web browser, returns the
	//! URL instead.
	//! \remark If an authorization already exists and is still valid, an empty URL is returned.
	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	FString InitiateAuthorizationURL();

	//! Returns the authorization URL currently valid, if any.
	//! \remark If an authorization already exists and is still valid, an empty URL is returned.
	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	FString GetAuthorizationURL();

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
	void GetIModelProcessingStatus(FString iModelId);

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
	AdvViz::SDK::ITwinAPIRequestInfo InfosToQueryIModel(FString iTwinId, FString iModelId,
		FString ChangesetId, FString ECSQLQuery, int Offset, int Count);
	void QueryIModelRows(FString iTwinId, FString iModelId, FString ChangesetId,
		FString ECSQLQuery, int Offset, int Count, std::function<void(HttpRequestID)>&& NotifyRequestID,
		std::function<void(const AdvViz::expected<AdvViz::SDK::Http::Response, std::string>&)>&& onFinished = {},
		AdvViz::SDK::Http::EAsyncCallbackExecutionMode asyncCBExecMode = AdvViz::SDK::Http::EAsyncCallbackExecutionMode::MainThread,
		AdvViz::SDK::ITwinAPIRequestInfo const* RequestInfo = nullptr,
		AdvViz::SDK::FilterErrorFunc&& FilterError = {});

	void GetMaterialProperties(
		FString iTwinId, FString iModelId, FString ChangesetId,
		FString MaterialId);
	void GetMaterialListProperties(
		FString iTwinId, FString iModelId, FString ChangesetId,
		TArray<FString> MaterialIds);
	void GetTextureData(
		FString iTwinId, FString iModelId, FString ChangesetId,
		FString TextureId);
	void ConvertIModelCoordsToGeoCoords(FString iTwinId, FString iModelId, FString ChangesetId,
		FVector const& IModelSpatialCoords, std::function<void(HttpRequestID)>&& NotifRequestID);

	void GetGoogleCuratedContentAccess();


	void SetCustomServerURL(std::string const& ServerUrl);

	//! Returns the current server URL, or an empty string if none is set.
	FString GetServerURL() const;

	void RunCustomRequest(
		AdvViz::SDK::ITwinAPIRequestInfo const& RequestInfo,
		AdvViz::SDK::CustomRequestCallback&& ResponseCallback,
		AdvViz::SDK::FilterErrorFunc&& FilterError = {});


	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnAuthorizationChecked OnAuthorizationChecked;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnGetiTwinsComplete OnGetiTwinsComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnGetITwinInfoComplete OnGetITwinInfoComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnGetiTwiniModelsComplete OnGetiTwiniModelsComplete;

	UPROPERTY(BlueprintAssignable, Category = "iTwin Web Services")
	FOnGetIModelProcessingStatusComplete OnGetIModelProcessingStatusComplete;

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

	UFUNCTION(BlueprintSetter, Category = "iTwin Web Services")
	bool IsAuthorizationInProgress() const;

	UFUNCTION(BlueprintSetter, Category = "iTwin Web Services")
	bool HasAccessToken() const;

	//! Return the OAuth access token for this environment, if any.
	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	FString GetAccessToken() const;

	using AuthManagerPtr = std::shared_ptr<AdvViz::SDK::ITwinAuthManager>;
	AuthManagerPtr& GetAuthManager() const;

	//! Set the regular access token.
	void SetRegularAccessToken(const FString& InAccessToken);
	//! Set the access token overriding the regular one.
	void SetOverrideAccessToken(const FString& InAccessToken, AdvViz::SDK::EITwinAuthOverrideMode InOverrideMode);
	//! Restore the regular access token, by resetting the override one.
	void ResetOverrideAccessToken();
	//! Returns the access token override mode.
	AdvViz::SDK::EITwinAuthOverrideMode GetOverrideMode() const;

	//! Initialize the environment from the level, if all server connection actors in the level are using the
	//! same environment. If no server connection actor exists in the world, or if there are several ones not
	//! sharing the same environment, this WebServices actor will not be modified and false is returned.
	UFUNCTION(BlueprintCallable, Category = "iTwin Web Services")
	bool InitEnvironmentFromWorld();

	void SetObserver(IITwinWebServicesObserver* InObserver);
	bool HasObserver(IITwinWebServicesObserver const* Observer) const;

	static void SetITwinAppIDArray(ITwin::AppIDArray const& iTwinAppIDs, bool bLogIDs = true);

	//! Can be called to customize the scopes used to request the authorization.
	static void AddScope(FString const& ExtraScope);

	static void SetPreAuthFunc(PreAuthFunc const& InPreAuthCallback);

	static void InitDefaultEnvironment(class UITwinDecorationServiceSettings const* DecoSettings);

	static void SetPreferredEnvironment(EITwinEnvironment Env);

	//! Resets the preferred environment, so that it will be re-initialized from the settings or the world
	//! context object.
	static void ResetPreferredEnvironment();

	//! Returns the default environment, as set in the settings or by the preferred environment.
	//! If none of the atter is set, it will also try to guess the environment from the world context object
	//! (ie. the level), if any - by looking for instances of AITwinServerConnection.
	//! If no environment can be determined, returns the fallback environment (defaulting to Prod).
	static EITwinEnvironment GetDefaultEnvironment(const UWorld* WorldContextObject,
		EITwinEnvironment InFallbackEnvironment = EITwinEnvironment::Prod);

	//! Returns the environment currently used by the active web-services instance, if any.
	//! If no web-services instance is active, looks for the environment for which we currently have an
	//! access token: if such environment exists, and there is no ambiguity (ie. no other environment
	//! with an access token), returns it; otherwise, returns the invalid environment.
	static EITwinEnvironment GetActiveEnvironment();

	static void SetLogErrors(bool bInLogErrors);
	static bool ShouldLogErrors() { return bLogErrors; }

#if WITH_TESTS
	//! Sets a custom URL for tests (generally the URL corrsponding to a mock server).
	void SetTestServerURL(FString const& ServerUrl);
	//! Sets a global factor applied to delay times used in the retry system.
	//! Can be used to avoid losing too much time for requests meant to fail.
	void SetRetryDelayFactor(float InFactor);

	static void SetupTestMode(EITwinEnvironment Env, FString const& TokenFileSuffix);
#endif


private:
	/// Returns the current instance, ie. the one currently processing a request response callback.
	/// Beware it will return null as soon as we are not currently executing such callback, which can be a
	/// source of confusion and bugs.
	/// \see GetActiveConnection, in which we handle the null case, for example (only working if we do not
	/// mix different environments in a same session...)
	static UITwinWebServices* GetWorkingInstance();

	bool TryGetAccessToken(bool bAllowBroadcastAuthResult);
	void OnAuthDoneImpl(bool bSuccess, std::string const& Error, bool bBroadcastResult = true);

	virtual void OnAuthorizationDone(bool bSuccess, std::string const& Error) override;

	void DoGetiModelChangesets(FString const& iModelId, bool bRestrictToLatest);

	//! Returns the error stored for the given request, if any.
	FString GetRequestError(HttpRequestID const& InRequestId) const;

	template <typename FunctorType>
	void DoRequest(FunctorType&& InFunctor);
	template <typename FunctorType>
	HttpRequestID DoRequestRetID(FunctorType&& InFunctor);

	static bool InitDefaultEnvironmentFromDecoSettings(class UITwinDecorationServiceSettings const* DecoSettings);

	static bool InitDefaultEnvironmentFromWorld(const UWorld* WorldContextObject);

private:
	UPROPERTY(EditAnywhere, Category = "iTwin Web Services",
		BlueprintGetter = GetEnvironment, BlueprintSetter = SetEnvironment)
	EITwinEnvironment Environment = EITwinEnvironment::Prod;

	class FImpl;
	TPimplPtr<FImpl> Impl;

	static bool bLogErrors;
	static PreAuthFunc PreAuthCallback;
};
