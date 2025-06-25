/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinWebServices.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinWebServices/ITwinWebServices.h>


#include <EncryptionContextOpenSSL.h>

#include "ITwinAuthorizationManager.h"

#include <ITwinServerEnvironment.h>
#include <ITwinWebServices/ITwinWebServicesObserver.h>
#include <Decoration/ITwinDecorationServiceSettings.h>
#include <Network/JsonQueriesCache.h>
#include <Network/UEHttpAdapter.h>


#include <Kismet/GameplayStatics.h>

#include <Engine/World.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/ITwinAPI/ITwinRequestTypes.h>
#	include <Core/ITwinAPI/ITwinWebServices.h>
#	include <Core/ITwinAPI/ITwinWebServicesObserver.h>
#include <Compil/AfterNonUnrealIncludes.h>


namespace ITwin
{
	// Filled by InitServerConnectionFromWorld in case we find a custom server connection in world.
	static std::optional<EITwinEnvironment> PreferredEnvironment;

	ITWINRUNTIME_API bool IsMLMaterialPredictionEnabled();
}

namespace
{
	static UITwinWebServices* WorkingInstance = nullptr;

	struct [[nodiscard]] ScopedWorkingWebServices
	{
		UITwinWebServices* PreviousInstance = WorkingInstance;

		ScopedWorkingWebServices(UITwinWebServices* CurrentInstance)
		{
			WorkingInstance = CurrentInstance;
		}

		~ScopedWorkingWebServices()
		{
			WorkingInstance = PreviousInstance;
		}
	};
}

/*static*/
void UITwinWebServices::SetITwinAppIDArray(ITwin::AppIDArray const& ITwinAppIDs)
{
	FITwinAuthorizationManager::SetAppIDArray(ITwinAppIDs);
}

/*static*/
bool UITwinWebServices::bLogErrors = true;

/*static*/
void UITwinWebServices::SetLogErrors(bool bInLogErrors)
{
	bLogErrors = bInLogErrors;
}



/*static*/
void UITwinWebServices::AddScope(FString const& ExtraScope)
{
	FITwinAuthorizationManager::AddScope(TCHAR_TO_UTF8(*ExtraScope));
}

/*static*/
void UITwinWebServices::SetPreferredEnvironment(EITwinEnvironment Env)
{
	if (ensure(Env != EITwinEnvironment::Invalid))
	{
		ITwin::PreferredEnvironment = Env;
	}
}

class UITwinWebServices::FImpl
	: public AdvViz::SDK::ITwinWebServices
	, public AdvViz::SDK::IITwinWebServicesObserver
{
	friend class UITwinWebServices;

	UITwinWebServices& owner_;

	using FMutex = std::recursive_mutex;
	using FLock = std::lock_guard<std::recursive_mutex>;
	mutable FMutex mutex_;

	// Avoid confusion between SDKCore's and iTwinForUnreal's observer
	using Observer_ITwinRuntime = ::IITwinWebServicesObserver;
	using Observer_ITwinSDKCore = AdvViz::SDK::IITwinWebServicesObserver;

	Observer_ITwinRuntime* observer_ = nullptr;

	// Some data (mostly tokens) are unique per environment - thus their management is centralized
	using SharedMngrPtr = FITwinAuthorizationManager::SharedInstance;
	SharedMngrPtr authManager_;

	//std::optional<FJsonQueriesCache> queriesCache_;


public:
	FImpl(UITwinWebServices& Owner);
	~FImpl();

	/// Initialize the manager handling tokens for current Environment, and register itself as observer for
	/// the latter.
	void InitAuthManager(EITwinEnvironment InEnvironment);

	void InitMaterialMLCache(FString const& CacheFolder);

	/// Unregister itself from current manager, and reset it.
	void ResetAuthManager();

	void SetEnvironment(EITwinEnvironment InEnvironment);

	void SetObserver(Observer_ITwinRuntime* InObserver);

	virtual void OnRequestError(std::string const& strError, int retriesLeft, bool bLogError = true) override;

	/// Overridden from AdvViz::SDK::IITwinWebServicesObserver
	/// This will just perform a conversion from SDKCore types to Unreal ones, and call the appropriate
	/// callback on the latter.
	virtual void OnITwinsRetrieved(bool bSuccess, AdvViz::SDK::ITwinInfos const& infos) override;
	virtual void OnITwinInfoRetrieved(bool bSuccess, AdvViz::SDK::ITwinInfo const& info) override;
	virtual void OnIModelsRetrieved(bool bSuccess, AdvViz::SDK::IModelInfos const& infos) override;
	virtual void OnChangesetsRetrieved(bool bSuccess, AdvViz::SDK::ChangesetInfos const& infos) override;
	virtual void OnExportInfosRetrieved(bool bSuccess, AdvViz::SDK::ITwinExportInfos const& infos) override;
	virtual void OnExportInfoRetrieved(bool bSuccess, AdvViz::SDK::ITwinExportInfo const& info) override;
	virtual void OnExportStarted(bool bSuccess, std::string const& InExportId) override;
	virtual void OnSavedViewInfosRetrieved(bool bSuccess, AdvViz::SDK::SavedViewInfos const& infos) override;
	virtual void OnSavedViewRetrieved(bool bSuccess, AdvViz::SDK::SavedView const& savedView, AdvViz::SDK::SavedViewInfo const& info) override;
	virtual void OnSavedViewExtensionRetrieved(bool bSuccess, std::string const& SavedViewId, std::string const& data) override;
	virtual void OnSavedViewThumbnailRetrieved(bool bSuccess,  std::string const& SavedViewId, std::vector<uint8_t> const& RawData) override;
	virtual void OnSavedViewThumbnailUpdated(bool bSuccess, std::string const& SavedViewId, std::string const& Response) override;
	virtual void OnSavedViewGroupInfosRetrieved(bool bSuccess, AdvViz::SDK::SavedViewGroupInfos const& coreInfos) override;
	virtual void OnSavedViewGroupAdded(bool bSuccess, AdvViz::SDK::SavedViewGroupInfo const& coreGroupInfo) override;
	virtual void OnSavedViewAdded(bool bSuccess, AdvViz::SDK::SavedViewInfo const& info) override;
	virtual void OnSavedViewDeleted(bool bSuccess, std::string const& savedViewId, std::string const& response) override;
	virtual void OnSavedViewEdited(bool bSuccess, AdvViz::SDK::SavedView const& savedView, AdvViz::SDK::SavedViewInfo const& info) override;
	virtual void OnRealityDataRetrieved(bool bSuccess, AdvViz::SDK::ITwinRealityDataInfos const& infos) override;
	virtual void OnRealityData3DInfoRetrieved(bool bSuccess, AdvViz::SDK::ITwinRealityData3DInfo const& info) override;
	virtual void OnElementPropertiesRetrieved(bool bSuccess, AdvViz::SDK::ITwinElementProperties const& props, std::string const& ElementId) override;
	virtual void OnIModelPropertiesRetrieved(bool bSuccess, AdvViz::SDK::IModelProperties const& props) override;
	virtual void OnConvertedIModelCoordsToGeoCoords(bool bSuccess,
		AdvViz::SDK::GeoCoordsReply const& GeoCoords, AdvViz::SDK::RequestID const& FromRequestId) override;
	virtual void OnIModelQueried(bool bSuccess, std::string const& QueryResult, AdvViz::SDK::RequestID const&) override;
	virtual void OnMaterialPropertiesRetrieved(bool bSuccess, AdvViz::SDK::ITwinMaterialPropertiesMap const& props) override;
	virtual void OnTextureDataRetrieved(bool bSuccess, std::string const& textureId, AdvViz::SDK::ITwinTextureData const& textureData) override;
	virtual void OnMatMLPredictionRetrieved(bool bSuccess, AdvViz::SDK::ITwinMaterialPrediction const& prediction, std::string const& error = {}) override;
	virtual void OnMatMLPredictionProgress(float fProgressRatio) override;
};


namespace
{
	inline void ToCoreVec3(FVector const& InVec3, std::array<double, 3>& OutVec3)
	{
		OutVec3[0] = InVec3.X;
		OutVec3[1] = InVec3.Y;
		OutVec3[2] = InVec3.Z;
	}
	inline FVector FromCoreVec3(std::array<double, 3> const& InVec3)
	{
		return FVector(InVec3[0], InVec3[1], InVec3[2]);
	}
	inline FPlane FromCoreVec4(std::array<double, 4> const& InVec)
	{
		return FPlane(InVec[0], InVec[1], InVec[2], InVec[3]);
	}
	inline FMatrix FromCoreMatrix3x4(AdvViz::SDK::Matrix3x4 const& InMat)
	{
		return FMatrix(
			FromCoreVec4(InMat[0]),
			FromCoreVec4(InMat[1]),
			FromCoreVec4(InMat[2]),
			FPlane(0, 0, 0, 1)
		);
	}
	inline void ToCoreRotator(FRotator const& InAngles, AdvViz::SDK::Rotator& OutAngles)
	{
		OutAngles.yaw = InAngles.Yaw;
		OutAngles.pitch = InAngles.Pitch;
		OutAngles.roll = InAngles.Roll;
	}
	inline FRotator FromCoreRotator(AdvViz::SDK::Rotator const& InRotator)
	{
		return FRotator(
			InRotator.pitch.value_or(0.),
			InRotator.yaw.value_or(0.),
			InRotator.roll.value_or(0.));
	}
	inline TArray<FString> FromCoreStringVector(std::vector<std::string> const& InVec)
	{
		TArray<FString> Array;
		Array.Reserve(InVec.size());
		for (const auto& hiddenModel : InVec)
			Array.Add(hiddenModel.c_str());
		return Array;
	}
	inline void ToCoreSavedView(FSavedView const& SavedView, AdvViz::SDK::SavedView& coreSV)
	{
		ToCoreVec3(SavedView.Origin, coreSV.origin);
		ToCoreVec3(SavedView.Extents, coreSV.extents);
		ToCoreVec3(SavedView.FrustumOrigin, coreSV.frustumOrigin);
		coreSV.focusDist = SavedView.FocusDist;
		ToCoreRotator(SavedView.Angles, coreSV.angles);
		if (!SavedView.DisplayStyle.RenderTimeline.IsEmpty())
		{
			coreSV.displayStyle.emplace();
			coreSV.displayStyle->renderTimeline = TCHAR_TO_ANSI(*SavedView.DisplayStyle.RenderTimeline);
			coreSV.displayStyle->timePoint = SavedView.DisplayStyle.TimePoint;
		}
	}
	inline void ToCoreSavedViewInfo(FSavedViewInfo const& SavedViewInfo, AdvViz::SDK::SavedViewInfo& coreSV)
	{
		coreSV.id = TCHAR_TO_ANSI(*SavedViewInfo.Id);
		coreSV.displayName = TCHAR_TO_UTF8(*SavedViewInfo.DisplayName);
		coreSV.shared = SavedViewInfo.bShared;
	}
	inline void ToCoreSavedViewGroupInfo(FSavedViewGroupInfo const& SavedViewGroupInfo, AdvViz::SDK::SavedViewGroupInfo& coreSVGroup)
	{
		coreSVGroup.id = TCHAR_TO_ANSI(*SavedViewGroupInfo.Id);
		coreSVGroup.displayName = TCHAR_TO_UTF8(*SavedViewGroupInfo.DisplayName);
		coreSVGroup.shared = SavedViewGroupInfo.bShared;
		coreSVGroup.readOnly = SavedViewGroupInfo.bReadOnly;
	}
}


/*static*/
UITwinWebServices* UITwinWebServices::GetWorkingInstance()
{
	if (WorkingInstance)
	{
		return WorkingInstance;
	}
	auto* CoreWorkingInstance = AdvViz::SDK::ITwinWebServices::GetWorkingInstance();
	if (CoreWorkingInstance)
	{
		UITwinWebServices::FImpl* pImpl = static_cast<UITwinWebServices::FImpl*>(CoreWorkingInstance);
		return &pImpl->owner_;
	}
	return nullptr;
}

/*static*/
bool UITwinWebServices::GetActiveConnection(TObjectPtr<AITwinServerConnection>& OutConnection,
	const UObject* WorldContextObject)
{
	if (GetWorkingInstance())
	{
		GetWorkingInstance()->GetServerConnection(OutConnection);
	}
	else
	{
		// Quick fix for Carrot MVP - GetWorkingInstance only works if it's called in a callback of a http
		// request - however, when it's not the case, it's often very easy to recover a correct instance,
		// because in the packaged game, there is only one unique environment at one time (Prod).
		TArray<AActor*> itwinServerActors;
		UGameplayStatics::GetAllActorsOfClass(
			WorldContextObject, AITwinServerConnection::StaticClass(), itwinServerActors);
		EITwinEnvironment commonEnv = EITwinEnvironment::Invalid;
		AITwinServerConnection* firstValidConnection = nullptr;
		for (AActor* servConn : itwinServerActors)
		{
			AITwinServerConnection* existingConnection = Cast<AITwinServerConnection>(servConn);
			if (existingConnection
				&& existingConnection->IsValidLowLevel()
				// Ignore any ServerConnection which has not been assigned any valid environment.
				&& existingConnection->Environment != EITwinEnvironment::Invalid
				&& existingConnection->HasAccessToken())
			{
				if (!firstValidConnection)
				{
					firstValidConnection = existingConnection;
					commonEnv = existingConnection->Environment;
				}
				if (commonEnv != existingConnection->Environment)
				{
					// distinct environments are present, so we cannot decide which connection to use
					commonEnv = EITwinEnvironment::Invalid;
					break;
				}
			}
		}
		if (firstValidConnection && commonEnv != EITwinEnvironment::Invalid)
		{
			OutConnection = firstValidConnection;
		}
	}
	return OutConnection.Get() != nullptr;
}

UITwinWebServices::FImpl::FImpl(UITwinWebServices& Owner)
	: owner_(Owner)
{
	AdvViz::SDK::ITwinWebServices::SetObserver(this);
}


UITwinWebServices::FImpl::~FImpl()
{
	AdvViz::SDK::ITwinWebServices::SetObserver(nullptr);
	ResetAuthManager();
}

void UITwinWebServices::FImpl::InitAuthManager(EITwinEnvironment InEnvironment)
{
	if (authManager_)
	{
		ResetAuthManager();
	}

	// Initiate the manager handling tokens for current Environment
	authManager_ = FITwinAuthorizationManager::GetInstance(
		static_cast<AdvViz::SDK::EITwinEnvironment>(InEnvironment));
	authManager_->AddObserver(&owner_);
}

void UITwinWebServices::FImpl::ResetAuthManager()
{
	if (authManager_)
	{
		authManager_->RemoveObserver(&owner_);
		authManager_.reset();
	}
}

void UITwinWebServices::FImpl::SetEnvironment(EITwinEnvironment InEnvironment)
{
	// The enum should be exactly identical in SDK Core and here...
	// We could investigate on ways to expose existing enumeration in blueprints without having to duplicate
	// it...
	static_assert(static_cast<EITwinEnvironment>(AdvViz::SDK::EITwinEnvironment::Prod) == EITwinEnvironment::Prod
		&& static_cast<EITwinEnvironment>(AdvViz::SDK::EITwinEnvironment::Invalid) == EITwinEnvironment::Invalid,
		"EITwinEnvironment enum definition mismatch");

	AdvViz::SDK::EITwinEnvironment const coreEnv =
		static_cast<AdvViz::SDK::EITwinEnvironment>(InEnvironment);
	AdvViz::SDK::EITwinEnvironment const oldCoreEnv = GetEnvironment();
	AdvViz::SDK::ITwinWebServices::SetEnvironment(coreEnv);
	if (coreEnv != oldCoreEnv && authManager_)
	{
		// Make sure we point at the right manager
		InitAuthManager(InEnvironment);
	}
}

void UITwinWebServices::FImpl::SetObserver(Observer_ITwinRuntime* InObserver)
{
	observer_ = InObserver;

	if (InObserver == nullptr
		&& IsSetupForForMaterialMLPrediction())
	{
		// Material ML prediction may retry the same request regularly (with a timer), and we should ensure
		// we stop repeating this when the IModel is destroyed.
		AdvViz::SDK::ITwinWebServices::SetObserver(nullptr);
	}
}

void UITwinWebServices::FImpl::OnRequestError(std::string const& strError, int retriesLeft, bool bLogError /*= true*/)
{
	if (UITwinWebServices::ShouldLogErrors() && bLogError)
	{
		if (retriesLeft == 0)
		{
			BE_LOGE("ITwinAPI", "iTwin request failed with: " << strError);
		}
		else
		{
			BE_LOGW("ITwinAPI", "iTwin request failed with: " << strError << " - retries left: "
				<< retriesLeft);
		}
	}
}

void UITwinWebServices::FImpl::OnITwinsRetrieved(bool bSuccess, AdvViz::SDK::ITwinInfos const& coreInfos)
{
	FITwinInfos infos;
	infos.iTwins.Reserve(coreInfos.iTwins.size());
	Algo::Transform(coreInfos.iTwins, infos.iTwins,
		[](AdvViz::SDK::ITwinInfo const& V) -> FITwinInfo { return {
			V.id.c_str(),
			UTF8_TO_TCHAR(V.displayName.c_str()),
			V.status.c_str(),
			V.number.c_str()
		};
	});
	owner_.OnGetiTwinsComplete.Broadcast(bSuccess, infos);
	if (observer_)
	{
		observer_->OnITwinsRetrieved(bSuccess, infos);
	}
}

void UITwinWebServices::FImpl::OnITwinInfoRetrieved(bool bSuccess, AdvViz::SDK::ITwinInfo const& coreInfo)
{
	FITwinInfo const info =
	{
		coreInfo.id.c_str(),
		UTF8_TO_TCHAR(coreInfo.displayName.c_str()),
		coreInfo.status.c_str(),
		coreInfo.number.c_str()
	};
	owner_.OnGetITwinInfoComplete.Broadcast(bSuccess, info);
	if (observer_)
	{
		observer_->OnITwinInfoRetrieved(bSuccess, info);
	}
}

void UITwinWebServices::FImpl::OnIModelsRetrieved(bool bSuccess, AdvViz::SDK::IModelInfos const& coreInfos)
{
	FIModelInfos infos;
	infos.iModels.Reserve(coreInfos.iModels.size());
	Algo::Transform(coreInfos.iModels, infos.iModels,
		[](AdvViz::SDK::IModelInfo const& V) -> FIModelInfo { return {
			V.id.c_str(),
			UTF8_TO_TCHAR(V.displayName.c_str())
		};
	});
	owner_.OnGetiTwiniModelsComplete.Broadcast(bSuccess, infos);
	if (observer_)
	{
		observer_->OnIModelsRetrieved(bSuccess, infos);
	}
}

void UITwinWebServices::FImpl::OnChangesetsRetrieved(bool bSuccess, AdvViz::SDK::ChangesetInfos const& coreInfos)
{
	FChangesetInfos infos;
	infos.Changesets.Reserve(coreInfos.changesets.size());
	Algo::Transform(coreInfos.changesets, infos.Changesets,
		[](AdvViz::SDK::ChangesetInfo const& V) -> FChangesetInfo { return {
			V.id.c_str(),
			UTF8_TO_TCHAR(V.displayName.c_str()),
			UTF8_TO_TCHAR(V.description.value_or("").c_str()),
			V.index
		};
	});
	owner_.OnGetiModelChangesetsComplete.Broadcast(bSuccess, infos);
	if (observer_)
	{
		observer_->OnChangesetsRetrieved(bSuccess, infos);
	}
}

void UITwinWebServices::FImpl::OnExportInfosRetrieved(bool bSuccess, AdvViz::SDK::ITwinExportInfos const& coreInfos)
{
	FITwinExportInfos infos;
	infos.ExportInfos.Reserve(coreInfos.exports.size());
	Algo::Transform(coreInfos.exports, infos.ExportInfos,
		[](AdvViz::SDK::ITwinExportInfo const& V) -> FITwinExportInfo { return {
			V.id.c_str(),
			UTF8_TO_TCHAR(V.displayName.c_str()),
			V.status.c_str(),
			V.iModelId.c_str(),
			V.iTwinId.c_str(),
			V.changesetId.c_str(),
			V.meshUrl.c_str()
		};
	});
	owner_.OnGetExportsComplete.Broadcast(bSuccess, infos);
	if (observer_)
	{
		observer_->OnExportInfosRetrieved(bSuccess, infos);
	}
}

void UITwinWebServices::FImpl::OnExportInfoRetrieved(bool bSuccess, AdvViz::SDK::ITwinExportInfo const& coreInfo)
{
	FITwinExportInfo const info =
	{
		coreInfo.id.c_str(),
		UTF8_TO_TCHAR(coreInfo.displayName.c_str()),
		coreInfo.status.c_str(),
		coreInfo.iModelId.c_str(),
		coreInfo.iTwinId.c_str(),
		coreInfo.changesetId.c_str(),
		coreInfo.meshUrl.c_str()
	};
	owner_.OnGetExportInfoComplete.Broadcast(bSuccess, info);
	if (observer_)
	{
		observer_->OnExportInfoRetrieved(bSuccess, info);
	}
}

void UITwinWebServices::FImpl::OnExportStarted(bool bSuccess, std::string const& exportId)
{
	const FString NewExportId(exportId.c_str());
	owner_.OnStartExportComplete.Broadcast(bSuccess, NewExportId);
	if (observer_)
	{
		observer_->OnExportStarted(bSuccess, NewExportId);
	}
}

void UITwinWebServices::FImpl::OnSavedViewExtensionRetrieved(bool bSuccess, std::string const& id, std::string const& data)
{
	FString ExtensionData = data.c_str();
	FString SavedViewId = id.c_str();
	owner_.OnGetSavedViewExtensionComplete.Broadcast(bSuccess, SavedViewId, ExtensionData);
	if (observer_)
	{
		observer_->OnSavedViewExtensionRetrieved(bSuccess, SavedViewId, ExtensionData);
	}
}

void UITwinWebServices::FImpl::OnSavedViewInfosRetrieved(bool bSuccess, AdvViz::SDK::SavedViewInfos const& coreInfos)
{
	FSavedViewInfos infos;
	infos.SavedViews.Reserve(coreInfos.savedViews.size());
	Algo::Transform(coreInfos.savedViews, infos.SavedViews,
		[](AdvViz::SDK::SavedViewInfo const& V) -> FSavedViewInfo { 
			FSavedViewInfo info = {
				V.id.c_str(),
				UTF8_TO_TCHAR(V.displayName.c_str()),
				V.shared,
				UTF8_TO_TCHAR(V.creationTime.c_str())
			};
			info.Extensions.Reserve(V.extensions.size());
			for (const auto& ext : V.extensions)
				info.Extensions.Add(ext.extensionName.c_str());
			return info;
	});
	infos.GroupId = coreInfos.groupId.value_or("").c_str();
	infos.IModelId = coreInfos.iModelId.value_or("").c_str();
	infos.ITwinId = coreInfos.iTwinId.value_or("").c_str();
	owner_.OnGetSavedViewsComplete.Broadcast(bSuccess, infos);
	if (observer_)
	{
		observer_->OnSavedViewInfosRetrieved(bSuccess, infos);
	}
}

void UITwinWebServices::FImpl::OnSavedViewGroupInfosRetrieved(bool bSuccess, AdvViz::SDK::SavedViewGroupInfos const& coreInfos)
{
	FSavedViewGroupInfos infos;
	infos.SavedViewGroups.Reserve(coreInfos.groups.size());
	Algo::Transform(coreInfos.groups, infos.SavedViewGroups,
		[](AdvViz::SDK::SavedViewGroupInfo const& V) -> FSavedViewGroupInfo { return {
			V.id.c_str(),
			UTF8_TO_TCHAR(V.displayName.c_str()),
			V.shared,
			V.readOnly
		};
		});
	infos.IModelId = coreInfos.iModelId.value_or("").c_str();
	owner_.OnGetSavedViewGroupsComplete.Broadcast(bSuccess, infos);
	if (observer_)
	{
		observer_->OnSavedViewGroupInfosRetrieved(bSuccess, infos);
	}
}

void UITwinWebServices::FImpl::OnSavedViewRetrieved(bool bSuccess, AdvViz::SDK::SavedView const& coreSV, AdvViz::SDK::SavedViewInfo const& coreSVInfo)
{
	FSavedView SavedView = {
		FromCoreVec3(coreSV.origin),
		FromCoreVec3(coreSV.extents),
		FromCoreRotator(coreSV.angles),
		TArray<FString>(),
		TArray<FString>(),
		TArray<FString>(),
		FDisplayStyle()
	};
	if (coreSV.hiddenCategories)
		SavedView.HiddenCategories = FromCoreStringVector(coreSV.hiddenCategories.value());
	if (coreSV.hiddenModels)
		SavedView.HiddenModels = FromCoreStringVector(coreSV.hiddenModels.value());
	if (coreSV.hiddenElements)
		SavedView.HiddenElements = FromCoreStringVector(coreSV.hiddenElements.value());
	if (coreSV.displayStyle)
	{
		SavedView.DisplayStyle.RenderTimeline = coreSV.displayStyle->renderTimeline.value_or("").c_str();
		SavedView.DisplayStyle.TimePoint = coreSV.displayStyle->timePoint.value_or(0.);
	}
	FSavedViewInfo SavedViewInfo = {
		coreSVInfo.id.c_str(),
		UTF8_TO_TCHAR(coreSVInfo.displayName.c_str()),
		coreSVInfo.shared
	};
	SavedViewInfo.Extensions.Reserve(coreSVInfo.extensions.size());
	for (const auto& ext : coreSVInfo.extensions)
		SavedViewInfo.Extensions.Add(ext.extensionName.c_str());
	owner_.OnGetSavedViewComplete.Broadcast(bSuccess, SavedView, SavedViewInfo);
	if (observer_)
	{
		observer_->OnSavedViewRetrieved(bSuccess, SavedView, SavedViewInfo);
	}
}

void UITwinWebServices::FImpl::OnSavedViewThumbnailRetrieved(bool bSuccess, std::string const& SavedViewId,
	std::vector<uint8_t> const& RawData)
{
	if (observer_)
	{
		TArray<uint8> UEBuffer;
		UEBuffer.Append(reinterpret_cast<uint8 const*>(RawData.data()), RawData.size());
		observer_->OnSavedViewThumbnailRetrieved(bSuccess, SavedViewId.c_str(), UEBuffer);
	}
}

void UITwinWebServices::FImpl::OnSavedViewThumbnailUpdated(bool bSuccess, std::string const& SavedViewId, std::string const& Response)
{
	owner_.OnUpdateSavedViewThumbnailComplete.Broadcast(bSuccess, SavedViewId.c_str(), Response.c_str());
	if (observer_)
	{
		observer_->OnSavedViewThumbnailUpdated(bSuccess, SavedViewId.c_str(), Response.c_str());
	}
}

void UITwinWebServices::FImpl::OnSavedViewAdded(bool bSuccess, AdvViz::SDK::SavedViewInfo const& coreSVInfo)
{
	const FSavedViewInfo SavedViewInfo = {
		coreSVInfo.id.c_str(),
		UTF8_TO_TCHAR(coreSVInfo.displayName.c_str()),
		coreSVInfo.shared,
		UTF8_TO_TCHAR(coreSVInfo.creationTime.c_str())
	};
	owner_.OnSavedViewAdded(bSuccess, SavedViewInfo);
}

void UITwinWebServices::FImpl::OnSavedViewGroupAdded(bool bSuccess, AdvViz::SDK::SavedViewGroupInfo const& coreGroupInfo)
{
	const FSavedViewGroupInfo SavedViewGroupInfo = {
		coreGroupInfo.id.c_str(),
		UTF8_TO_TCHAR(coreGroupInfo.displayName.c_str()),
		coreGroupInfo.shared,
		coreGroupInfo.readOnly
	};
	owner_.OnAddSavedViewGroupComplete.Broadcast(bSuccess, SavedViewGroupInfo);
	if (observer_)
	{
		observer_->OnSavedViewGroupAdded(bSuccess, SavedViewGroupInfo);
	}
}

void UITwinWebServices::FImpl::OnSavedViewDeleted(bool bSuccess, std::string const& savedViewId, std::string const& strError)
{
	owner_.OnSavedViewDeleted(bSuccess, FString(savedViewId.c_str()), FString(strError.c_str()));
}

void UITwinWebServices::FImpl::OnSavedViewEdited(bool bSuccess, AdvViz::SDK::SavedView const& coreSV, AdvViz::SDK::SavedViewInfo const& coreSVInfo)
{
	const FSavedView SavedView = {
		FromCoreVec3(coreSV.origin),
		FromCoreVec3(coreSV.extents),
		FromCoreRotator(coreSV.angles)
	};
	const FSavedViewInfo SavedViewInfo = {
		coreSVInfo.id.c_str(),
		UTF8_TO_TCHAR(coreSVInfo.displayName.c_str()),
		coreSVInfo.shared
	};
	owner_.OnEditSavedViewComplete.Broadcast(bSuccess, SavedView, SavedViewInfo);
	if (observer_)
	{
		observer_->OnSavedViewEdited(bSuccess, SavedView, SavedViewInfo);
	}
}

void UITwinWebServices::FImpl::OnRealityDataRetrieved(bool bSuccess, AdvViz::SDK::ITwinRealityDataInfos const& coreInfos)
{
	FITwinRealityDataInfos infos;
	infos.Infos.Reserve(coreInfos.realityData.size());
	Algo::Transform(coreInfos.realityData, infos.Infos,
		[](AdvViz::SDK::ITwinRealityDataInfo const& V) -> FITwinRealityDataInfo { return {
			V.id.c_str(),
			UTF8_TO_TCHAR(V.displayName.c_str())
		};
	});
	owner_.OnGetRealityDataComplete.Broadcast(bSuccess, infos);
	if (observer_)
	{
		observer_->OnRealityDataRetrieved(bSuccess, infos);
	}
}

void UITwinWebServices::FImpl::OnRealityData3DInfoRetrieved(bool bSuccess, AdvViz::SDK::ITwinRealityData3DInfo const& coreInfo)
{
	FITwinRealityData3DInfo info;
	info.Id = coreInfo.id.c_str();
	info.DisplayName = UTF8_TO_TCHAR(coreInfo.displayName.c_str());
	info.bGeolocated = coreInfo.bGeolocated;
	info.ExtentNorthEast.Latitude = coreInfo.extentNorthEast.latitude;
	info.ExtentNorthEast.Longitude = coreInfo.extentNorthEast.longitude;
	info.ExtentSouthWest.Latitude = coreInfo.extentSouthWest.latitude;
	info.ExtentSouthWest.Longitude = coreInfo.extentSouthWest.longitude;
	info.MeshUrl = coreInfo.meshUrl.c_str();
	owner_.OnGetRealityData3DInfoComplete.Broadcast(bSuccess, info);
	if (observer_)
	{
		observer_->OnRealityData3DInfoRetrieved(bSuccess, info);
	}
}

void UITwinWebServices::FImpl::OnElementPropertiesRetrieved(bool bSuccess, AdvViz::SDK::ITwinElementProperties const& coreProps, std::string const& ElementId)
{
	FElementProperties props;
	props.Properties.Reserve(coreProps.properties.size());
	Algo::Transform(coreProps.properties, props.Properties,
		[](AdvViz::SDK::ITwinElementProperty const& coreProp) -> FElementProperty {
		FElementProperty p;
		p.Name = coreProp.name.c_str();
		p.Attributes.Reserve(coreProp.attributes.size());
		Algo::Transform(coreProp.attributes, p.Attributes,
			[](AdvViz::SDK::ITwinElementAttribute const& coreAttr) -> FElementAttribute {
			return {
				coreAttr.name.c_str(),
				coreAttr.value.c_str()
			};
		});
		return p;
	});
	FString Id(ElementId.c_str());
	owner_.OnGetElementPropertiesComplete.Broadcast(bSuccess, props, Id);
	if (observer_)
	{
		observer_->OnElementPropertiesRetrieved(bSuccess, props, Id);
	}
}

void UITwinWebServices::FImpl::OnIModelPropertiesRetrieved(bool bSuccess,
														   AdvViz::SDK::IModelProperties const& coreProps)
{
	FProjectExtents ProjectExtents;
	FEcefLocation EcefLocation;
	const bool bHasExtents = coreProps.projectExtents.has_value();
	const bool bHasEcefLocation = coreProps.ecefLocation.has_value();
	if (bHasExtents)
	{
		ProjectExtents.Low = FromCoreVec3(coreProps.projectExtents->low);
		ProjectExtents.High = FromCoreVec3(coreProps.projectExtents->high);
	}
	if (bHasEcefLocation)
	{
		AdvViz::SDK::EcefLocation const& coreEcef = *coreProps.ecefLocation;
		EcefLocation.bHasCartographicOrigin = coreEcef.cartographicOrigin.has_value();
		if (EcefLocation.bHasCartographicOrigin)
		{
			// See doc for FCartographicProps about this conversion
			EcefLocation.CartographicOrigin.Latitude =
				FMath::RadiansToDegrees(coreEcef.cartographicOrigin->latitude);
			EcefLocation.CartographicOrigin.Longitude =
				FMath::RadiansToDegrees(coreEcef.cartographicOrigin->longitude);
			EcefLocation.CartographicOrigin.Height = coreEcef.cartographicOrigin->height;
		}
		EcefLocation.Orientation = FromCoreRotator(coreEcef.orientation);
		EcefLocation.Origin = FromCoreVec3(coreEcef.origin);
		EcefLocation.bHasTransform = coreEcef.transform.has_value();
		if (EcefLocation.bHasTransform)
		{
			EcefLocation.Transform = FromCoreMatrix3x4(*coreEcef.transform);
		}
		EcefLocation.bHasVectors = coreEcef.xVector && coreEcef.yVector;
		if (EcefLocation.bHasVectors)
		{
			EcefLocation.xVector = FromCoreVec3(*coreEcef.xVector);
			EcefLocation.yVector = FromCoreVec3(*coreEcef.yVector);
		}
		if (coreProps.geographicCoordinateSystem)
		{
			EcefLocation.bHasGeographicCoordinateSystem = true;
			if (coreProps.geographicCoordinateSystem->horizontalCRS
				&& coreProps.geographicCoordinateSystem->horizontalCRS->epsg)
			{
				EcefLocation.GeographicCoordinateSystemEPSG =
					*coreProps.geographicCoordinateSystem->horizontalCRS->epsg;
			}
		}
	}
	if (coreProps.globalOrigin)
		ProjectExtents.GlobalOrigin = FromCoreVec3(*coreProps.globalOrigin);
	owner_.OnGetIModelPropertiesComplete.Broadcast(bSuccess, bHasExtents, ProjectExtents, bHasEcefLocation, EcefLocation);
	if (observer_)
	{
		observer_->OnIModelPropertiesRetrieved(bSuccess, bHasExtents, ProjectExtents, bHasEcefLocation, EcefLocation);
	}
}

void UITwinWebServices::FImpl::OnConvertedIModelCoordsToGeoCoords(bool bSuccess,
	AdvViz::SDK::GeoCoordsReply const& GeoCoords, AdvViz::SDK::RequestID const& FromRequestID)
{
	if (observer_)
	{
		observer_->OnConvertedIModelCoordsToGeoCoords(bSuccess, GeoCoords,
													  HttpRequestID(FromRequestID.c_str()));
	}
}

void UITwinWebServices::FImpl::OnIModelQueried(bool bSuccess, std::string const& QueryResult,
											   AdvViz::SDK::RequestID const& FromRequestID)
{
	owner_.OnQueryIModelComplete.Broadcast(bSuccess, FString(QueryResult.c_str()));
	if (observer_)
	{
		observer_->OnIModelQueried(bSuccess, FString(QueryResult.c_str()),
								   HttpRequestID(FromRequestID.c_str()));
	}
}

void UITwinWebServices::FImpl::OnMaterialPropertiesRetrieved(bool bSuccess, AdvViz::SDK::ITwinMaterialPropertiesMap const& coreProps)
{
	if (observer_)
	{
		observer_->OnMaterialPropertiesRetrieved(bSuccess, coreProps);
	}
}

void UITwinWebServices::FImpl::OnTextureDataRetrieved(bool bSuccess, std::string const& textureId, AdvViz::SDK::ITwinTextureData const& textureData)
{
	if (observer_)
	{
		observer_->OnTextureDataRetrieved(bSuccess, textureId, textureData);
	}
}

void UITwinWebServices::FImpl::InitMaterialMLCache(FString const& CacheFolder)
{
	//if (!queriesCache_)
	//{
	//	queriesCache_.emplace(owner_);
	//}
	//if (queriesCache_->Initialize(CacheFolder, owner_.Environment, TEXT("MaterialMLPrediction")))
	{
		SetMaterialMLPredictionCacheFolder(TCHAR_TO_UTF8(*CacheFolder));
	}
}

void UITwinWebServices::FImpl::OnMatMLPredictionRetrieved(bool bSuccess,
	AdvViz::SDK::ITwinMaterialPrediction const& prediction, std::string const& error /*= {}*/)
{
	if (observer_)
	{
		observer_->OnMatMLPredictionRetrieved(bSuccess, prediction, error);
	}
}

void UITwinWebServices::FImpl::OnMatMLPredictionProgress(float fProgressRatio)
{
	if (observer_)
	{
		observer_->OnMatMLPredictionProgress(fProgressRatio);
	}
}


/// UITwinWebServices
UITwinWebServices::UITwinWebServices()
	: Impl(MakePimpl<UITwinWebServices::FImpl>(*this))
{
	// Adapt Unreal to SDK Core's Http request and authentication management
	static bool bHasInitSDKCore = false;
	if (!bHasInitSDKCore)
	{
		bHasInitSDKCore = true;

		using namespace AdvViz::SDK;

		HttpRequest::SetNewFct([]() {
			HttpRequest* p(static_cast<HttpRequest*>(new FUEHttpRequest));
			return p;
		});

		FITwinAuthorizationManager::OnStartup();
	}

	static bool bHasTestedDecoScope = false;
	if (!bHasTestedDecoScope && !HasAnyFlags(RF_ClassDefaultObject))
	{
		// Append additional iTwin scopes if some were set by the user.
		UITwinDecorationServiceSettings const* DecoSettings = GetDefault<UITwinDecorationServiceSettings>();
		if (DecoSettings && !DecoSettings->AdditionalITwinScope.IsEmpty())
		{
			UITwinWebServices::AddScope(DecoSettings->AdditionalITwinScope);
		}
		// Test whether we should grant access to the Decoration Service in the current application.
		// This is disabled by default (to avoid forcing all users to add a new scope to their iTwin app).
		// Note that in Carrot, we do this without condition (see AMainLevelScript::BeginPlay).
		if (DecoSettings && DecoSettings->bLoadDecorationsInPlugin)
		{
			UITwinWebServices::AddScope(TEXT(ITWIN_DECORATIONS_SCOPE));
		}
		if (DecoSettings
			&& !DecoSettings->CustomEnv.IsEmpty()
			&& !ITwin::PreferredEnvironment)
		{
			if (DecoSettings->CustomEnv == "DEV")
			{
				ITwin::PreferredEnvironment = EITwinEnvironment::Dev;
			}
			else if (DecoSettings->CustomEnv == "QA")
			{
				ITwin::PreferredEnvironment = EITwinEnvironment::QA;
			}
		}
		bHasTestedDecoScope = true;
	}

	if (ITwin::PreferredEnvironment)
	{
		SetEnvironment(*ITwin::PreferredEnvironment);
	}
	else if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		// See if a server connection was instantiated before playing the level: this is an easy trick to
		// test QA or Dev environment in the test app.
		InitServerConnectionFromWorld();
	}
}


bool UITwinWebServices::IsAuthorizationInProgress() const
{
	FImpl::FLock Lock(Impl->mutex_);
	if (ServerConnection && ServerConnection->HasAccessToken())
	{
		// Authorization already proceed.
		return false;
	}
	if (!Impl->authManager_)
	{
		// Never started.
		return false;
	}
	return Impl->authManager_->IsAuthorizationInProgress();
}

UITwinWebServices::AuthManagerPtr& UITwinWebServices::GetAuthManager() const
{
	return FITwinAuthorizationManager::GetInstance(
		static_cast<AdvViz::SDK::EITwinEnvironment>(Environment));
}

void UITwinWebServices::SetServerConnection(TObjectPtr<AITwinServerConnection> const& InConnection)
{
	FImpl::FLock Lock(Impl->mutex_);
	ServerConnection = InConnection;
	if (ServerConnection)
	{
		SetEnvironment(ServerConnection->Environment);
	}
}

void UITwinWebServices::SetEnvironment(EITwinEnvironment InEnvironment)
{
	this->Environment = InEnvironment;
	Impl->SetEnvironment(InEnvironment);
}

bool UITwinWebServices::TryGetServerConnection(bool bAllowBroadcastAuthResult)
{
	if (ServerConnection && ServerConnection->HasAccessToken())
	{
		// We already have an access token for this environment. Bypass the authorization process, but make
		// sure we broadcast the success if needed, as some code logic is placed in the callback (typically
		// in ITwinSelector)
		if (bAllowBroadcastAuthResult)
		{
			OnAuthDoneImpl(true, {}, true /*bAllowBroadcastAuthResult*/);
		}
		return true;
	}

	// Initiate the manager handling tokens for current Environment
	if (!Impl->authManager_)
	{
		Impl->InitAuthManager(Environment);
	}

	// First try to use existing access token for current Environment, if any.
	if (Impl->authManager_->HasAccessToken())
	{
		OnAuthDoneImpl(true, {}, bAllowBroadcastAuthResult);
		return true;
	}

	// No valid server connection
	return false;
}

bool UITwinWebServices::InitServerConnectionFromWorld()
{
	// If the level already contains AITwinServerConnection actors, and they all have the same
	// environment, use the latter.
	TArray<AActor*> itwinServerActors;
	UGameplayStatics::GetAllActorsOfClass(
		GetWorld(), AITwinServerConnection::StaticClass(), itwinServerActors);
	EITwinEnvironment commonEnv = EITwinEnvironment::Invalid;
	AITwinServerConnection* firstValidConnection = nullptr;
	for (AActor* servConn : itwinServerActors)
	{
		AITwinServerConnection* existingConnection = Cast<AITwinServerConnection>(servConn);
		if (existingConnection
			&& existingConnection->IsValidLowLevel()
			// Ignore any ServerConnection which has not been assigned any valid environment.
			&& existingConnection->Environment != EITwinEnvironment::Invalid)
		{
			if (!firstValidConnection)
			{
				firstValidConnection = existingConnection;
				commonEnv = existingConnection->Environment;
			}
			if (commonEnv != existingConnection->Environment)
			{
				// distinct environments are present, so we cannot decide which connection to use
				commonEnv = EITwinEnvironment::Invalid;
				break;
			}
		}
	}
	if (firstValidConnection && commonEnv != EITwinEnvironment::Invalid)
	{
		SetServerConnection(firstValidConnection);
		// Register this environment as the preferred one.
		ITwin::PreferredEnvironment = commonEnv;
		return true;
	}
	else
	{
		return false;
	}
}

AdvViz::SDK::EITwinAuthStatus UITwinWebServices::CheckAuthorizationStatus()
{
	if (TryGetServerConnection(true))
	{
		// We could get a valid server connection. No need to do anything more (note that the token
		// will be automatically refreshed when approaching its expiration: no need to check that).
		return AdvViz::SDK::EITwinAuthStatus::Success;
	}
	return Impl->authManager_->CheckAuthorization();
}

bool UITwinWebServices::CheckAuthorization()
{
	return CheckAuthorizationStatus() == AdvViz::SDK::EITwinAuthStatus::Success;
}

void UITwinWebServices::OnAuthDoneImpl(bool bSuccess, std::string const& Error, bool bBroadcastResult /*= true*/)
{
	ScopedWorkingWebServices WorkingInstanceSetter(this);

	if (bSuccess)
	{
		FImpl::FLock Lock(Impl->mutex_);
		if (!ServerConnection)
		{
			// First see if an existing connection actor for this environment can be reused
			TArray<AActor*> itwinServerActors;
			UGameplayStatics::GetAllActorsOfClass(
				GetWorld(), AITwinServerConnection::StaticClass(), itwinServerActors);
			for (AActor* servConn : itwinServerActors)
			{
				AITwinServerConnection* existingConnection = Cast<AITwinServerConnection>(servConn);
				if (existingConnection
					&& existingConnection->IsValidLowLevelFast(false)
					&& existingConnection->Environment == this->Environment)
				{
					ServerConnection = existingConnection;
					break;
				}
			}
		}
		if (!ServerConnection)
		{
			ServerConnection = GetWorld()->SpawnActor<AITwinServerConnection>();
		}
		ServerConnection->Environment = this->Environment;
		ensureMsgf(ServerConnection->HasAccessToken(), TEXT("Upon success, an access token is expected!"));
	}

	if (bBroadcastResult)
	{
		OnAuthorizationChecked.Broadcast(bSuccess, UTF8_TO_TCHAR(Error.c_str()));
		if (Impl->observer_)
		{
			Impl->observer_->OnAuthorizationDone(bSuccess, Error);
		}
	}
}

void UITwinWebServices::OnAuthorizationDone(bool bSuccess, std::string const& Error)
{
	OnAuthDoneImpl(bSuccess, Error, true);
}

void UITwinWebServices::GetServerConnection(TObjectPtr<AITwinServerConnection>& OutConnection) const
{
	FImpl::FLock Lock(Impl->mutex_);
	if (ServerConnection && ServerConnection->IsValidLowLevelFast(false))
	{
		OutConnection = ServerConnection;
	}
	else
	{
		OutConnection = {};
	}
}

bool UITwinWebServices::HasSameConnection(AITwinServerConnection const* Connection) const
{
	FImpl::FLock Lock(Impl->mutex_);
	return ServerConnection.Get() == Connection;
}

void UITwinWebServices::SetObserver(IITwinWebServicesObserver* InObserver)
{
	Impl->SetObserver(InObserver);
}

bool UITwinWebServices::HasObserver(IITwinWebServicesObserver const* Observer) const
{
	return Impl->observer_ == Observer;
}

FString UITwinWebServices::GetLastError() const
{
	return FString(Impl->GetLastError().c_str());
}

bool UITwinWebServices::ConsumeLastError(FString& OutError)
{
	std::string LastError;
	const bool bHasError = Impl->ConsumeLastError(LastError);
	OutError = LastError.c_str();
	return bHasError;
}

FString UITwinWebServices::GetRequestError(HttpRequestID const& InRequestId) const
{
	return Impl->GetRequestError(TCHAR_TO_ANSI(*InRequestId)).c_str();
}

#if WITH_TESTS
void UITwinWebServices::SetTestServerURL(FString const& ServerUrl)
{
	Impl->SetCustomServerURL(TCHAR_TO_ANSI(*ServerUrl));
}
#endif


template <typename FunctorType>
void UITwinWebServices::DoRequest(FunctorType&& InFunctor)
{
	// We may have no ServerConnection yet (happens if one instantiates UITwinWebServices and uses it at once
	// without calling SetServerConnection or CheckAuthorization...)
	// In such case, if we can find an existing access token, use it instead of failing directly. However we
	// should not broadcast the authorization success in such case, as this is certainly not expected by the
	// client.
	if (!TryGetServerConnection(false))
	{
		return;
	}
	InFunctor();
}

void UITwinWebServices::GetITwinInfo(FString ITwinId)
{
	DoRequest([this, ITwinId]() { Impl->GetITwinInfo(TCHAR_TO_ANSI(*ITwinId)); });
}

void UITwinWebServices::GetiTwins()
{
	DoRequest([this]() { Impl->GetITwins(); });
}

void UITwinWebServices::GetiTwiniModels(FString ITwinId)
{
	DoRequest([this, ITwinId]() { Impl->GetITwinIModels(TCHAR_TO_ANSI(*ITwinId)); });
}

void UITwinWebServices::DoGetiModelChangesets(FString const& IModelId, bool bRestrictToLatest)
{
	DoRequest([this, &IModelId, bRestrictToLatest]() {
		Impl->GetIModelChangesets(TCHAR_TO_ANSI(*IModelId), bRestrictToLatest); }
	);
}

void UITwinWebServices::GetiModelChangesets(FString IModelId)
{
	DoGetiModelChangesets(IModelId, false);
}

void UITwinWebServices::GetiModelLatestChangeset(FString iModelId)
{
	DoGetiModelChangesets(iModelId, true);
}

void UITwinWebServices::GetExports(FString IModelId, FString ChangesetId)
{
	DoRequest([this, IModelId, ChangesetId]() {
		Impl->GetExports(TCHAR_TO_ANSI(*IModelId), TCHAR_TO_ANSI(*ChangesetId)); }
	);
}

void UITwinWebServices::GetExportInfo(FString ExportId)
{
	DoRequest([this, ExportId]() { Impl->GetExportInfo(TCHAR_TO_ANSI(*ExportId)); });
}

void UITwinWebServices::StartExport(FString IModelId, FString ChangesetId)
{
	DoRequest([this, IModelId, ChangesetId]() {
		Impl->StartExport(TCHAR_TO_ANSI(*IModelId), TCHAR_TO_ANSI(*ChangesetId)); }
	);
}


void UITwinWebServices::GetAllSavedViews(FString iTwinId, FString iModelId, FString GroupId /*= ""*/, int Top /*= 100*/, int Skip /*= 0*/)
{
	DoRequest([this, iTwinId, iModelId, GroupId, Top, Skip]() {
		Impl->GetAllSavedViews(TCHAR_TO_ANSI(*iTwinId), TCHAR_TO_ANSI(*iModelId), TCHAR_TO_ANSI(*GroupId), Top, Skip);
	});
}

void UITwinWebServices::GetSavedViewGroups(FString iTwinId, FString iModelId /*= ""*/)
{
	DoRequest([this, iTwinId, iModelId]() {
		Impl->GetSavedViewsGroups(TCHAR_TO_ANSI(*iTwinId), TCHAR_TO_ANSI(*iModelId));
		});
}

void UITwinWebServices::GetSavedView(FString SavedViewId)
{
	DoRequest([this, SavedViewId]() { Impl->GetSavedView(TCHAR_TO_ANSI(*SavedViewId)); });
}

void UITwinWebServices::GetSavedViewExtension(FString SavedViewId, FString ExtensionName)
{
	DoRequest([this, SavedViewId, ExtensionName]() { 
		Impl->GetSavedViewExtension(TCHAR_TO_ANSI(*SavedViewId), TCHAR_TO_ANSI(*ExtensionName)); 
	});
}

void UITwinWebServices::GetSavedViewThumbnail(FString SavedViewId)
{
	DoRequest([this, SavedViewId]() { Impl->GetSavedViewThumbnail(TCHAR_TO_ANSI(*SavedViewId)); });
}

void UITwinWebServices::UpdateSavedViewThumbnail(FString SavedViewId, FString ThumbnailURL)
{
	DoRequest([this, SavedViewId, ThumbnailURL]() { Impl->UpdateSavedViewThumbnail(TCHAR_TO_ANSI(*SavedViewId), TCHAR_TO_ANSI(*ThumbnailURL)); });
}

void UITwinWebServices::AddSavedView(FString ITwinId, FSavedView SavedView, FSavedViewInfo SavedViewInfo, FString IModelId /*= ""*/, FString GroupId /*= ""*/)
{
	AdvViz::SDK::SavedView coreSV;
	AdvViz::SDK::SavedViewInfo coreSVInfo;
	ToCoreSavedView(SavedView, coreSV);
	ToCoreSavedViewInfo(SavedViewInfo, coreSVInfo);

	DoRequest([this, ITwinId, IModelId, &coreSV, &coreSVInfo, GroupId]() {
		Impl->AddSavedView(TCHAR_TO_ANSI(*ITwinId), coreSV, coreSVInfo, 
						   TCHAR_TO_ANSI(*IModelId), TCHAR_TO_ANSI(*GroupId));
	});
}

void UITwinWebServices::OnSavedViewAdded(bool bSuccess, FSavedViewInfo const& SavedViewInfo)
{
	OnAddSavedViewComplete.Broadcast(bSuccess, SavedViewInfo);
	if (Impl->observer_)
	{
		Impl->observer_->OnSavedViewAdded(bSuccess, SavedViewInfo);
	}
}

void UITwinWebServices::AddSavedViewGroup(FString ITwinId, FString IModelId, FSavedViewGroupInfo SavedViewGroupInfo)
{
	AdvViz::SDK::SavedViewGroupInfo coreSVGroupInfo;
	ToCoreSavedViewGroupInfo(SavedViewGroupInfo, coreSVGroupInfo);

	DoRequest([this, ITwinId, IModelId, &coreSVGroupInfo]() {
		Impl->AddSavedViewGroup(TCHAR_TO_ANSI(*ITwinId), TCHAR_TO_ANSI(*IModelId),
		coreSVGroupInfo);
		});
}

void UITwinWebServices::DeleteSavedView(FString SavedViewId)
{
	DoRequest([this, SavedViewId]() { Impl->DeleteSavedView(TCHAR_TO_ANSI(*SavedViewId)); });
}

void UITwinWebServices::OnSavedViewDeleted(bool bSuccess, FString const& SavedViewId, FString const& Response) const
{
	OnDeleteSavedViewComplete.Broadcast(bSuccess, SavedViewId, Response);
	if (Impl->observer_)
	{
		Impl->observer_->OnSavedViewDeleted(bSuccess, SavedViewId, Response);
	}
}

void UITwinWebServices::EditSavedView(FSavedView SavedView, FSavedViewInfo SavedViewInfo)
{
	AdvViz::SDK::SavedView coreSV;
	AdvViz::SDK::SavedViewInfo coreSVInfo;
	ToCoreSavedView(SavedView, coreSV);
	ToCoreSavedViewInfo(SavedViewInfo, coreSVInfo);

	DoRequest([this, &coreSV, &coreSVInfo]() { Impl->EditSavedView(coreSV, coreSVInfo);	});
}


void UITwinWebServices::GetRealityData(FString ITwinId)
{
	DoRequest([this, ITwinId]() { Impl->GetRealityData(TCHAR_TO_ANSI(*ITwinId)); });
}

void UITwinWebServices::GetRealityData3DInfo(FString ITwinId, FString RealityDataId)
{
	DoRequest([this, ITwinId, RealityDataId]() {
		Impl->GetRealityData3DInfo(TCHAR_TO_ANSI(*ITwinId), TCHAR_TO_ANSI(*RealityDataId));
	});
}


void UITwinWebServices::GetElementProperties(FString iTwinId, FString iModelId, FString ChangesetId, FString ElementId)
{
	DoRequest([this, iTwinId, iModelId, ChangesetId, ElementId]() {
		Impl->GetElementProperties(TCHAR_TO_ANSI(*iTwinId), TCHAR_TO_ANSI(*iModelId), TCHAR_TO_ANSI(*ChangesetId), TCHAR_TO_ANSI(*ElementId));
	});
}

void UITwinWebServices::GetIModelProperties(FString iTwinId, FString iModelId, FString ChangesetId)
{
	DoRequest([this, iTwinId, iModelId, ChangesetId]() {
		Impl->GetIModelProperties(TCHAR_TO_ANSI(*iTwinId), TCHAR_TO_ANSI(*iModelId), TCHAR_TO_ANSI(*ChangesetId));
	});
}

void UITwinWebServices::ConvertIModelCoordsToGeoCoords(FString iTwinId, FString iModelId, FString ChangesetId,
	FVector const& IModelSpatialCoords, std::function<void(HttpRequestID)>&& NotifRequestID)
{
	DoRequest([this, iTwinId, iModelId, ChangesetId, IModelSpatialCoords,
				NotifRequestID=std::move(NotifRequestID)] () mutable
		{
			Impl->ConvertIModelCoordsToGeoCoords(
				TCHAR_TO_ANSI(*iTwinId), TCHAR_TO_ANSI(*iModelId), TCHAR_TO_ANSI(*ChangesetId),
				IModelSpatialCoords.X, IModelSpatialCoords.Y, IModelSpatialCoords.Z,
				[NotifRequestID = std::move(NotifRequestID)](AdvViz::SDK::RequestID const& RequestID)
					{ if (NotifRequestID) NotifRequestID(HttpRequestID(RequestID.c_str())); });
		});
}

void UITwinWebServices::QueryIModel(FString iTwinId, FString iModelId, FString ChangesetId,
									FString ECSQLQuery, int Offset, int Count)
{
	QueryIModelRows(iTwinId, iModelId, ChangesetId, ECSQLQuery, Offset, Count, {});
}

AdvViz::SDK::ITwinAPIRequestInfo UITwinWebServices::InfosToQueryIModel(FString iTwinId, FString iModelId,
	FString ChangesetId, FString ECSQLQuery, int Offset, int Count)
{
	return Impl->InfosToQueryIModel(TCHAR_TO_ANSI(*iTwinId), TCHAR_TO_ANSI(*iModelId),
		TCHAR_TO_ANSI(*ChangesetId), TCHAR_TO_ANSI(*ECSQLQuery), Offset, Count);
}

void UITwinWebServices::QueryIModelRows(FString iTwinId, FString iModelId, FString ChangesetId,
	FString ECSQLQuery, int Offset, int Count, std::function<void(HttpRequestID)>&& NotifRequestID,
	AdvViz::SDK::ITwinAPIRequestInfo const* RequestInfo/*=nullptr*/,
	AdvViz::SDK::FilterErrorFunc&& FilterError /*= {}*/)
{
	DoRequest(
		[this, iTwinId, iModelId, ChangesetId, ECSQLQuery, Offset, Count, RequestInfo,
		 NotifRequestID=std::move(NotifRequestID),
		 FilterError=std::move(FilterError)] () mutable
		{
			Impl->QueryIModel(
				TCHAR_TO_ANSI(*iTwinId),
				TCHAR_TO_ANSI(*iModelId),
				TCHAR_TO_ANSI(*ChangesetId),
				TCHAR_TO_ANSI(*ECSQLQuery),
				Offset,
				Count,
				[NotifRequestID = std::move(NotifRequestID)](AdvViz::SDK::RequestID const& RequestID)
					{ if (NotifRequestID) NotifRequestID(HttpRequestID(RequestID.c_str())); },
				RequestInfo,
				[FilterError = std::move(FilterError)](std::string const& StrError, bool& bAllowRetry, bool& bLogError)
					{ if (FilterError) FilterError(std::string(StrError.c_str()), bAllowRetry, bLogError);  }
				);
		});
}

void UITwinWebServices::GetMaterialProperties(
	FString iTwinId, FString iModelId, FString ChangesetId,
	FString MaterialId)
{
	DoRequest([this, iTwinId, iModelId, ChangesetId, MaterialId]() {
		Impl->GetMaterialProperties(
			TCHAR_TO_ANSI(*iTwinId), TCHAR_TO_ANSI(*iModelId), TCHAR_TO_ANSI(*ChangesetId),
			TCHAR_TO_ANSI(*MaterialId));
	});
}

void UITwinWebServices::GetMaterialListProperties(
	FString iTwinId, FString iModelId, FString ChangesetId,
	TArray<FString> MaterialIds)
{
	DoRequest([this, iTwinId, iModelId, ChangesetId, MaterialIds]() {
		std::vector<std::string> coreMatIds;
		coreMatIds.reserve(MaterialIds.Num());
		for (FString const& id : MaterialIds)
			coreMatIds.push_back(TCHAR_TO_ANSI(*id));
		Impl->GetMaterialListProperties(
			TCHAR_TO_ANSI(*iTwinId), TCHAR_TO_ANSI(*iModelId), TCHAR_TO_ANSI(*ChangesetId),
			coreMatIds);
	});
}

void UITwinWebServices::GetTextureData(
	FString iTwinId, FString iModelId, FString ChangesetId,
	FString TextureId)
{
	DoRequest([this, iTwinId, iModelId, ChangesetId, TextureId]() {
		Impl->GetTextureData(
			TCHAR_TO_ANSI(*iTwinId), TCHAR_TO_ANSI(*iModelId), TCHAR_TO_ANSI(*ChangesetId),
			TCHAR_TO_ANSI(*TextureId));
	});
}

bool UITwinWebServices::IsSetupForForMaterialMLPrediction() const
{
	return Impl->IsSetupForForMaterialMLPrediction();
}

void UITwinWebServices::SetupForMaterialMLPrediction()
{
	Impl->SetupForMaterialMLPrediction();
}

EITwinMaterialPredictionStatus UITwinWebServices::GetMaterialMLPrediction(
	FString iTwinId, FString iModelId, FString ChangesetId)
{
	if (!TryGetServerConnection(false))
	{
		return EITwinMaterialPredictionStatus::NoAuth;
	}

	FString const CacheFolder = QueriesCache::GetCacheFolder(
		QueriesCache::ESubtype::MaterialMLPrediction,
		this->Environment, iTwinId, iModelId, ChangesetId);
	Impl->InitMaterialMLCache(CacheFolder);

	return static_cast<EITwinMaterialPredictionStatus>(
		Impl->GetMaterialMLPrediction(
			TCHAR_TO_ANSI(*iTwinId),
			TCHAR_TO_ANSI(*iModelId),
			TCHAR_TO_ANSI(*ChangesetId)));
}

void UITwinWebServices::SetCustomServerURL(std::string const& ServerUrl)
{
	Impl->SetCustomServerURL(ServerUrl);
}

void UITwinWebServices::RunCustomRequest(
	AdvViz::SDK::ITwinAPIRequestInfo const& RequestInfo,
	AdvViz::SDK::CustomRequestCallback&& ResponseCallback,
	AdvViz::SDK::FilterErrorFunc&& FilterError /*= {}*/)
{
	DoRequest([this, &RequestInfo,
			   ResponseCallback = std::move(ResponseCallback),
			   FilterError = std::move(FilterError)]() mutable {
		Impl->RunCustomRequest(RequestInfo, std::move(ResponseCallback), std::move(FilterError));
	});
}
