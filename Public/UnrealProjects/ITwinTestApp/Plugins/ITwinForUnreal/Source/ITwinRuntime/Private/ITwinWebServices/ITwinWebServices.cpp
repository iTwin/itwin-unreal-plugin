/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinWebServices.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinWebServices/ITwinWebServices.h>


#include <EncryptionContextOpenSSL.h>

#include "ITwinAuthorizationManager.h"

#include <ITwinServerEnvironment.h>
#include <ITwinWebServices/ITwinWebServicesObserver.h>
#include <Network/UEHttpAdapter.h>

#include <Kismet/GameplayStatics.h>

#include <Serialization/ArchiveProxy.h>
#include <Serialization/MemoryReader.h>
#include <Serialization/MemoryWriter.h>

#include <Misc/FileHelper.h>
#include <Misc/Paths.h>
#include <PlatformCryptoTypes.h>
#include <HAL/FileManager.h>

#include <Engine/World.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Compil/CleanUpGuard.h>
#	include <Core/ITwinAPI/ITwinWebServices.h>
#	include <Core/ITwinAPI/ITwinWebServicesObserver.h>
#include <Compil/AfterNonUnrealIncludes.h>


namespace ITwin
{
	extern FString GetITwinAppId(EITwinEnvironment Env);
}

namespace
{
	inline FString GetITwinAPIRootUrl(EITwinEnvironment Env)
	{
		return TEXT("https://") + ITwinServerEnvironment::GetUrlPrefix(Env) + TEXT("api.bentley.com");
	}

	static UITwinWebServices* WorkingInstance = nullptr;

	struct [[nodiscard]] ScopedWorkingWebServices
	{
		UITwinWebServices* PreviousInstance = WorkingInstance;

		ScopedWorkingWebServices(UITwinWebServices* CurrentInstance)
		{
			check(!WorkingInstance);
			WorkingInstance = CurrentInstance;
		}

		~ScopedWorkingWebServices()
		{
			WorkingInstance = PreviousInstance;
		}
	};

	// only used for unit tests
	static FString TokenFileSuffix;


	/// Return an AES256 (symmetric) key
	TArray<uint8> GetKey(EITwinEnvironment Env)
	{
		// This handler uses AES256, which has 32-byte keys.
		static const int32 KeySizeInBytes = 32;

		// Build a deterministic key from the application ID and user/computer data. The goal of this
		// encryption is just to secure the token for an external individual not having access to the code of
		// the plugin...
		FString KeyRoot = FString(FPlatformProcess::ComputerName()).Replace(TEXT(" "), TEXT("")).Left(10);
		KeyRoot += FString(FPlatformProcess::UserName()).Replace(TEXT(" "), TEXT("")).Left(10);
		KeyRoot += ITwin::GetITwinAppId(Env).Reverse().Replace(TEXT("-"), TEXT("A"));
		while (KeyRoot.Len() < KeySizeInBytes)
		{
			KeyRoot.Append(*KeyRoot.Reverse());
		}
		TArray<uint8> Key;
		Key.Reset(KeySizeInBytes);
		Key.Append(
				   TArrayView<const uint8>((const uint8*)StringCast<ANSICHAR>(*KeyRoot).Get(), KeySizeInBytes).GetData(), KeySizeInBytes);
		return Key;
	}

	FString GetTokenFilename(EITwinEnvironment Env, FString const& FileSuffix, bool bCreateDir)
	{
		FString OutDir = FPlatformProcess::UserSettingsDir();
		if (OutDir.IsEmpty())
		{
			return {};
		}
		FString const TokenDir = FPaths::Combine(OutDir, TEXT("Bentley"), TEXT("Cache"));
		if (bCreateDir && !IFileManager::Get().DirectoryExists(*TokenDir))
		{
			IFileManager::Get().MakeDirectory(*TokenDir, true);
		}
		return FPaths::Combine(TokenDir,
			ITwinServerEnvironment::GetUrlPrefix(Env) + TEXT("AdvVizCnx") + FileSuffix + TokenFileSuffix + TEXT(".dat"));
	}
}

/*static*/
void UITwinWebServices::SetITwinAppIDArray(ITwin::AppIDArray const& ITwinAppIDs)
{
	FITwinAuthorizationManager::SetITwinAppIDArray(ITwinAppIDs);
}

/*static*/
bool UITwinWebServices::bLogErrors = true;

/*static*/
void UITwinWebServices::SetLogErrors(bool bInLogErrors)
{
	bLogErrors = bInLogErrors;
}


#if WITH_TESTS

/*static*/
void UITwinWebServices::SetupTestMode(EITwinEnvironment Env, FString const& InTokenFileSuffix)
{
	// for unit tests, allow running without iTwin App ID, and use a special suffix for filenames to avoid
	// any conflict with the normal run.
	if (!FITwinAuthorizationManager::HasITwinAppID(Env))
	{
		UITwinWebServices::SetITwinAppIDArray({ TEXT("ThisIsADummyAppIDForTesting") });
	}
	checkf(!InTokenFileSuffix.IsEmpty(), TEXT("a unique suffix is required to avoid conflicts"));
	TokenFileSuffix = InTokenFileSuffix;
}
#endif //WITH_TESTS

/*static*/
bool UITwinWebServices::SaveToken(FString const& Token, EITwinEnvironment Env,
	TArray<uint8> const& Key, FString const& FileSuffix)
{
	const bool bIsDeletingToken = Token.IsEmpty();
	FString OutputFileName = GetTokenFilename(Env, FileSuffix, !bIsDeletingToken);
	if (OutputFileName.IsEmpty())
	{
		return false;
	}
	if (bIsDeletingToken)
	{
		// just remove the file, if it exists: this will discard any old refresh token
		if (IFileManager::Get().FileExists(*OutputFileName))
		{
			IFileManager::Get().Delete(*OutputFileName);
		}
		return true;
	}

	if (Key.Num() != 32)
	{
		ensureMsgf(false, TEXT("wrong key"));
		return false;
	}

	EPlatformCryptoResult EncryptResult = EPlatformCryptoResult::Failure;
	TArray<uint8> OutCiphertext = FEncryptionContextOpenSSL().Encrypt_AES_256_ECB(
		TArrayView<const uint8>(
			(const uint8*)StringCast<ANSICHAR>(*Token).Get(), Token.Len()),
		Key, EncryptResult);
	if (EncryptResult != EPlatformCryptoResult::Success)
	{
		return false;
	}
	TArray<uint8> rawData;
	FMemoryWriter memWriter(rawData, true);
	FArchiveProxy archive(memWriter);
	memWriter.SetIsSaving(true);

	uint32 TokenLen = Token.Len();
	archive << TokenLen;
	archive << OutCiphertext;

	if (rawData.Num())
	{
		return FFileHelper::SaveArrayToFile(rawData, *OutputFileName);
	}
	else
	{
		return false;
	}
}

/*static*/
bool UITwinWebServices::SaveToken(FString const& Token, EITwinEnvironment Env)
{
	return SaveToken(Token, Env, GetKey(Env), {});
}

/*static*/
bool UITwinWebServices::LoadToken(FString& OutToken, EITwinEnvironment Env,
	TArray<uint8> const& Key, FString const& FileSuffix)
{
	if (Key.Num() != 32)
	{
		ensureMsgf(false, TEXT("wrong key"));
		return false;
	}
	FString TokenFileName = GetTokenFilename(Env, FileSuffix, false);
	if (!FPaths::FileExists(TokenFileName))
	{
		return false;
	}

	TArray<uint8> rawData;
	if (!FFileHelper::LoadFileToArray(rawData, *TokenFileName))
	{
		return false;
	}

	FMemoryReader memReader(rawData, true);
	FArchiveProxy archive(memReader);
	memReader.SetIsLoading(true);

	uint32 TokenLen = 0;
	TArray<uint8> Ciphertext;
	archive << TokenLen;
	archive << Ciphertext;

	if (TokenLen == 0)
	{
		return false;
	}

	EPlatformCryptoResult EncryptResult = EPlatformCryptoResult::Failure;
	TArray<uint8> Plaintext = FEncryptionContextOpenSSL().Decrypt_AES_256_ECB(
		Ciphertext, Key, EncryptResult);
	if (EncryptResult != EPlatformCryptoResult::Success)
	{
		return false;
	}
	if (Plaintext.Num() < (int32)TokenLen)
	{
		return false;
	}
	OutToken.Reset(TokenLen);
	for (uint32 i = 0; i < TokenLen; ++i)
	{
		OutToken.AppendChar(static_cast<TCHAR>(Plaintext[i]));
	}
	return true;
}

/*static*/
bool UITwinWebServices::LoadToken(FString& OutToken, EITwinEnvironment Env)
{
	return LoadToken(OutToken, Env, GetKey(Env), {});
}

/*static*/
void UITwinWebServices::DeleteTokenFile(EITwinEnvironment Env, FString const& FileSuffix /*= {}*/)
{
	SaveToken({}, Env, {}, FileSuffix);
}

/*static*/
void UITwinWebServices::AddScopes(FString const& ExtraScopes)
{
	FITwinAuthorizationManager::AddScopes(ExtraScopes);
}


class UITwinWebServices::FImpl
	: public SDK::Core::ITwinWebServices
	, public SDK::Core::IITwinWebServicesObserver
{
	friend class UITwinWebServices;

	UITwinWebServices& owner_;

	using FMutex = std::recursive_mutex;
	using FLock = std::lock_guard<std::recursive_mutex>;
	mutable FMutex mutex_;

	::IITwinWebServicesObserver* observer_ = nullptr;

	// Some data (mostly tokens) are unique per environment - thus their management is centralized
	using SharedMngrPtr = FITwinAuthorizationManager::SharedInstance;
	SharedMngrPtr authManager_;


public:
	FImpl(UITwinWebServices& Owner);
	~FImpl();

	/// Initialize the manager handling tokens for current Environment, and register itself as observer for
	/// the latter.
	void InitAuthManager(EITwinEnvironment InEnvironment);

	/// Unregister itself from current manager, and reset it.
	void ResetAuthManager();

	void SetEnvironment(EITwinEnvironment InEnvironment);

	virtual void OnRequestError(std::string const& strError) override;

	/// Overridden from SDK::Core::IITwinWebServicesObserver
	/// This will just perform a conversion from SDKCore types to Unreal ones, and call the appropriate
	/// callback on the latter.
	virtual void OnITwinsRetrieved(bool bSuccess, SDK::Core::ITwinInfos const& infos) override;
	virtual void OnITwinInfoRetrieved(bool bSuccess, SDK::Core::ITwinInfo const& info) override;
	virtual void OnIModelsRetrieved(bool bSuccess, SDK::Core::IModelInfos const& infos) override;
	virtual void OnChangesetsRetrieved(bool bSuccess, SDK::Core::ChangesetInfos const& infos) override;
	virtual void OnExportInfosRetrieved(bool bSuccess, SDK::Core::ITwinExportInfos const& infos) override;
	virtual void OnExportInfoRetrieved(bool bSuccess, SDK::Core::ITwinExportInfo const& info) override;
	virtual void OnExportStarted(bool bSuccess, std::string const& InExportId) override;
	virtual void OnSavedViewInfosRetrieved(bool bSuccess, SDK::Core::SavedViewInfos const& infos) override;
	virtual void OnSavedViewRetrieved(bool bSuccess, SDK::Core::SavedView const& savedView, SDK::Core::SavedViewInfo const& info) override;
	virtual void OnSavedViewThumbnailRetrieved(bool bSuccess, std::string const& SavedViewThumbnail, std::string const& SavedViewId) override;
	virtual void OnSavedViewThumbnailUpdated(bool bSuccess, std::string const& SavedViewId, std::string const& Response) override;
	virtual void OnSavedViewGroupInfosRetrieved(bool bSuccess, SDK::Core::SavedViewGroupInfos const& coreInfos) override;
	virtual void OnSavedViewGroupAdded(bool bSuccess, SDK::Core::SavedViewGroupInfo const& coreGroupInfo) override;
	virtual void OnSavedViewAdded(bool bSuccess, SDK::Core::SavedViewInfo const& info) override;
	virtual void OnSavedViewDeleted(bool bSuccess, std::string const& savedViewId, std::string const& response) override;
	virtual void OnSavedViewEdited(bool bSuccess, SDK::Core::SavedView const& savedView, SDK::Core::SavedViewInfo const& info) override;
	virtual void OnRealityDataRetrieved(bool bSuccess, SDK::Core::ITwinRealityDataInfos const& infos) override;
	virtual void OnRealityData3DInfoRetrieved(bool bSuccess, SDK::Core::ITwinRealityData3DInfo const& info) override;
	virtual void OnElementPropertiesRetrieved(bool bSuccess, SDK::Core::ITwinElementProperties const& props) override;
	virtual void OnIModelPropertiesRetrieved(bool bSuccess, SDK::Core::IModelProperties const& props) override;
	virtual void OnIModelQueried(bool bSuccess, std::string const& QueryResult) override;
	virtual void OnMaterialPropertiesRetrieved(bool bSuccess, SDK::Core::ITwinMaterialPropertiesMap const& props) override;
	virtual void OnTextureDataRetrieved(bool bSuccess, std::string const& textureId, SDK::Core::ITwinTextureData const& textureData) override;
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
	inline FMatrix FromCoreMatrix3x4(SDK::Core::Matrix3x4 const& InMat)
	{
		return FMatrix(
			FromCoreVec4(InMat[0]),
			FromCoreVec4(InMat[1]),
			FromCoreVec4(InMat[2]),
			FPlane(0, 0, 0, 1)
		);
	}
	inline void ToCoreRotator(FRotator const& InAngles, SDK::Core::Rotator& OutAngles)
	{
		OutAngles.yaw = InAngles.Yaw;
		OutAngles.pitch = InAngles.Pitch;
		OutAngles.roll = InAngles.Roll;
	}
	inline FRotator FromCoreRotator(SDK::Core::Rotator const& InRotator)
	{
		return FRotator(
			InRotator.pitch.value_or(0.),
			InRotator.yaw.value_or(0.),
			InRotator.roll.value_or(0.));
	}
	inline void ToCoreSavedView(FSavedView const& SavedView, SDK::Core::SavedView& coreSV)
	{
		ToCoreVec3(SavedView.Origin, coreSV.origin);
		ToCoreVec3(SavedView.Extents, coreSV.extents);
		ToCoreRotator(SavedView.Angles, coreSV.angles);
		if (!SavedView.DisplayStyle.RenderTimeline.IsEmpty())
		{
			coreSV.displayStyle.emplace();
			coreSV.displayStyle->renderTimeline = TCHAR_TO_ANSI(*SavedView.DisplayStyle.RenderTimeline);
			coreSV.displayStyle->timePoint = SavedView.DisplayStyle.TimePoint;
		}
	}
	inline void ToCoreSavedViewInfo(FSavedViewInfo const& SavedViewInfo, SDK::Core::SavedViewInfo& coreSV)
	{
		coreSV.id = TCHAR_TO_ANSI(*SavedViewInfo.Id);
		coreSV.displayName = TCHAR_TO_ANSI(*SavedViewInfo.DisplayName);
		coreSV.shared = SavedViewInfo.bShared;
	}
	inline void ToCoreSavedViewGroupInfo(FSavedViewGroupInfo const& SavedViewGroupInfo, SDK::Core::SavedViewGroupInfo& coreSVGroup)
	{
		coreSVGroup.id = TCHAR_TO_ANSI(*SavedViewGroupInfo.Id);
		coreSVGroup.displayName = TCHAR_TO_ANSI(*SavedViewGroupInfo.DisplayName);
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
	auto* CoreWorkingInstance = SDK::Core::ITwinWebServices::GetWorkingInstance();
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
	SDK::Core::ITwinWebServices::SetObserver(this);
}


UITwinWebServices::FImpl::~FImpl()
{
	SDK::Core::ITwinWebServices::SetObserver(nullptr);
	ResetAuthManager();
}

void UITwinWebServices::FImpl::InitAuthManager(EITwinEnvironment InEnvironment)
{
	if (authManager_)
	{
		ResetAuthManager();
	}

	// Initiate the manager handling tokens for current Environment
	authManager_ = FITwinAuthorizationManager::GetInstance(InEnvironment);
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
	static_assert(static_cast<EITwinEnvironment>(SDK::Core::EITwinEnvironment::Prod) == EITwinEnvironment::Prod
		&& static_cast<EITwinEnvironment>(SDK::Core::EITwinEnvironment::Invalid) == EITwinEnvironment::Invalid,
		"EITwinEnvironment enum definition mismatch");

	SDK::Core::EITwinEnvironment const coreEnv =
		static_cast<SDK::Core::EITwinEnvironment>(InEnvironment);
	SDK::Core::EITwinEnvironment const oldCoreEnv = GetEnvironment();
	SDK::Core::ITwinWebServices::SetEnvironment(coreEnv);
	if (coreEnv != oldCoreEnv && authManager_)
	{
		// Make sure we point at the right manager
		InitAuthManager(InEnvironment);
	}
}

void UITwinWebServices::FImpl::OnRequestError(std::string const& strError)
{
	if (UITwinWebServices::ShouldLogErrors())
	{
		BE_LOGE("ITwinAPI", "iTwin request failed with: " << strError);
	}
}

void UITwinWebServices::FImpl::OnITwinsRetrieved(bool bSuccess, SDK::Core::ITwinInfos const& coreInfos)
{
	FITwinInfos infos;
	infos.iTwins.Reserve(coreInfos.iTwins.size());
	Algo::Transform(coreInfos.iTwins, infos.iTwins,
		[](SDK::Core::ITwinInfo const& V) -> FITwinInfo { return {
			V.id.c_str(),
			V.displayName.c_str(),
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

void UITwinWebServices::FImpl::OnITwinInfoRetrieved(bool bSuccess, SDK::Core::ITwinInfo const& coreInfo)
{
	FITwinInfo const info =
	{
		coreInfo.id.c_str(),
		coreInfo.displayName.c_str(),
		coreInfo.status.c_str(),
		coreInfo.number.c_str()
	};
	owner_.OnGetITwinInfoComplete.Broadcast(bSuccess, info);
	if (observer_)
	{
		observer_->OnITwinInfoRetrieved(bSuccess, info);
	}
}

void UITwinWebServices::FImpl::OnIModelsRetrieved(bool bSuccess, SDK::Core::IModelInfos const& coreInfos)
{
	FIModelInfos infos;
	infos.iModels.Reserve(coreInfos.iModels.size());
	Algo::Transform(coreInfos.iModels, infos.iModels,
		[](SDK::Core::IModelInfo const& V) -> FIModelInfo { return {
			V.id.c_str(),
			V.displayName.c_str()
		};
	});
	owner_.OnGetiTwiniModelsComplete.Broadcast(bSuccess, infos);
	if (observer_)
	{
		observer_->OnIModelsRetrieved(bSuccess, infos);
	}
}

void UITwinWebServices::FImpl::OnChangesetsRetrieved(bool bSuccess, SDK::Core::ChangesetInfos const& coreInfos)
{
	FChangesetInfos infos;
	infos.Changesets.Reserve(coreInfos.changesets.size());
	Algo::Transform(coreInfos.changesets, infos.Changesets,
		[](SDK::Core::ChangesetInfo const& V) -> FChangesetInfo { return {
			V.id.c_str(),
			V.displayName.c_str(),
			V.description.value_or("").c_str(),
			V.index
		};
	});
	owner_.OnGetiModelChangesetsComplete.Broadcast(bSuccess, infos);
	if (observer_)
	{
		observer_->OnChangesetsRetrieved(bSuccess, infos);
	}
}

void UITwinWebServices::FImpl::OnExportInfosRetrieved(bool bSuccess, SDK::Core::ITwinExportInfos const& coreInfos)
{
	FITwinExportInfos infos;
	infos.ExportInfos.Reserve(coreInfos.exports.size());
	Algo::Transform(coreInfos.exports, infos.ExportInfos,
		[](SDK::Core::ITwinExportInfo const& V) -> FITwinExportInfo { return {
			V.id.c_str(),
			V.displayName.c_str(),
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

void UITwinWebServices::FImpl::OnExportInfoRetrieved(bool bSuccess, SDK::Core::ITwinExportInfo const& coreInfo)
{
	FITwinExportInfo const info =
	{
		coreInfo.id.c_str(),
		coreInfo.displayName.c_str(),
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

void UITwinWebServices::FImpl::OnSavedViewInfosRetrieved(bool bSuccess, SDK::Core::SavedViewInfos const& coreInfos)
{
	FSavedViewInfos infos;
	infos.SavedViews.Reserve(coreInfos.savedViews.size());
	Algo::Transform(coreInfos.savedViews, infos.SavedViews,
		[](SDK::Core::SavedViewInfo const& V) -> FSavedViewInfo { return {
			V.id.c_str(),
			V.displayName.c_str(),
			V.shared
		};
	});
	infos.GroupId = coreInfos.groupId.value_or("").c_str();
	owner_.OnGetSavedViewsComplete.Broadcast(bSuccess, infos);
	if (observer_)
	{
		observer_->OnSavedViewInfosRetrieved(bSuccess, infos);
	}
}

void UITwinWebServices::FImpl::OnSavedViewGroupInfosRetrieved(bool bSuccess, SDK::Core::SavedViewGroupInfos const& coreInfos)
{
	FSavedViewGroupInfos infos;
	infos.SavedViewGroups.Reserve(coreInfos.groups.size());
	Algo::Transform(coreInfos.groups, infos.SavedViewGroups,
		[](SDK::Core::SavedViewGroupInfo const& V) -> FSavedViewGroupInfo { return {
			V.id.c_str(),
			V.displayName.c_str(),
			V.shared,
			V.readOnly
		};
		});
	owner_.OnGetSavedViewGroupsComplete.Broadcast(bSuccess, infos);
	if (observer_)
	{
		observer_->OnSavedViewGroupInfosRetrieved(bSuccess, infos);
	}
}

void UITwinWebServices::FImpl::OnSavedViewRetrieved(bool bSuccess, SDK::Core::SavedView const& coreSV, SDK::Core::SavedViewInfo const& coreSVInfo)
{
	FSavedView SavedView = {
		FromCoreVec3(coreSV.origin),
		FromCoreVec3(coreSV.extents),
		FromCoreRotator(coreSV.angles),
		FDisplayStyle()
	};
	if (coreSV.displayStyle)
	{
		SavedView.DisplayStyle.RenderTimeline = coreSV.displayStyle->renderTimeline.value_or("").c_str();
		SavedView.DisplayStyle.TimePoint = coreSV.displayStyle->timePoint.value_or(0.);
	}
	const FSavedViewInfo SavedViewInfo = {
		coreSVInfo.id.c_str(),
		coreSVInfo.displayName.c_str(),
		coreSVInfo.shared
	};
	owner_.OnGetSavedViewComplete.Broadcast(bSuccess, SavedView, SavedViewInfo);
	if (observer_)
	{
		observer_->OnSavedViewRetrieved(bSuccess, SavedView, SavedViewInfo);
	}
}

void UITwinWebServices::FImpl::OnSavedViewThumbnailRetrieved(bool bSuccess, std::string const& SavedViewThumbnail, std::string const& SavedViewId)
{
	owner_.OnGetSavedViewThumbnailComplete.Broadcast(bSuccess, SavedViewThumbnail.c_str(), SavedViewId.c_str());
	if (observer_)
	{
		observer_->OnSavedViewThumbnailRetrieved(bSuccess, SavedViewThumbnail.c_str(), SavedViewId.c_str());
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

void UITwinWebServices::FImpl::OnSavedViewAdded(bool bSuccess, SDK::Core::SavedViewInfo const& coreSVInfo)
{
	const FSavedViewInfo SavedViewInfo = {
		coreSVInfo.id.c_str(),
		coreSVInfo.displayName.c_str(),
		coreSVInfo.shared
	};
	owner_.OnSavedViewAdded(bSuccess, SavedViewInfo);
}

void UITwinWebServices::FImpl::OnSavedViewGroupAdded(bool bSuccess, SDK::Core::SavedViewGroupInfo const& coreGroupInfo)
{
	const FSavedViewGroupInfo SavedViewGroupInfo = {
		coreGroupInfo.id.c_str(),
		coreGroupInfo.displayName.c_str(),
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

void UITwinWebServices::FImpl::OnSavedViewEdited(bool bSuccess, SDK::Core::SavedView const& coreSV, SDK::Core::SavedViewInfo const& coreSVInfo)
{
	const FSavedView SavedView = {
		FromCoreVec3(coreSV.origin),
		FromCoreVec3(coreSV.extents),
		FromCoreRotator(coreSV.angles)
	};
	const FSavedViewInfo SavedViewInfo = {
		coreSVInfo.id.c_str(),
		coreSVInfo.displayName.c_str(),
		coreSVInfo.shared
	};
	owner_.OnEditSavedViewComplete.Broadcast(bSuccess, SavedView, SavedViewInfo);
	if (observer_)
	{
		observer_->OnSavedViewEdited(bSuccess, SavedView, SavedViewInfo);
	}
}

void UITwinWebServices::FImpl::OnRealityDataRetrieved(bool bSuccess, SDK::Core::ITwinRealityDataInfos const& coreInfos)
{
	FITwinRealityDataInfos infos;
	infos.Infos.Reserve(coreInfos.realityData.size());
	Algo::Transform(coreInfos.realityData, infos.Infos,
		[](SDK::Core::ITwinRealityDataInfo const& V) -> FITwinRealityDataInfo { return {
			V.id.c_str(),
			V.displayName.c_str()
		};
	});
	owner_.OnGetRealityDataComplete.Broadcast(bSuccess, infos);
	if (observer_)
	{
		observer_->OnRealityDataRetrieved(bSuccess, infos);
	}
}

void UITwinWebServices::FImpl::OnRealityData3DInfoRetrieved(bool bSuccess, SDK::Core::ITwinRealityData3DInfo const& coreInfo)
{
	FITwinRealityData3DInfo info;
	info.Id = coreInfo.id.c_str();
	info.DisplayName = coreInfo.displayName.c_str();
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

void UITwinWebServices::FImpl::OnElementPropertiesRetrieved(bool bSuccess, SDK::Core::ITwinElementProperties const& coreProps)
{
	FElementProperties props;
	props.Properties.Reserve(coreProps.properties.size());
	Algo::Transform(coreProps.properties, props.Properties,
		[](SDK::Core::ITwinElementProperty const& coreProp) -> FElementProperty {
		FElementProperty p;
		p.Name = coreProp.name.c_str();
		p.Attributes.Reserve(coreProp.attributes.size());
		Algo::Transform(coreProp.attributes, p.Attributes,
			[](SDK::Core::ITwinElementAttribute const& coreAttr) -> FElementAttribute {
			return {
				coreAttr.name.c_str(),
				coreAttr.value.c_str()
			};
		});
		return p;
	});
	owner_.OnGetElementPropertiesComplete.Broadcast(bSuccess, props);
	if (observer_)
	{
		observer_->OnElementPropertiesRetrieved(bSuccess, props);
	}
}

void UITwinWebServices::FImpl::OnIModelPropertiesRetrieved(bool bSuccess, SDK::Core::IModelProperties const& coreProps)
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
		SDK::Core::EcefLocation const& coreEcef = *coreProps.ecefLocation;
		EcefLocation.bHasCartographicOrigin = coreEcef.cartographicOrigin.has_value();
		if (EcefLocation.bHasCartographicOrigin)
		{
			EcefLocation.CartographicOrigin.Latitude = coreEcef.cartographicOrigin->latitude;
			EcefLocation.CartographicOrigin.Longitude = coreEcef.cartographicOrigin->longitude;
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
	}
	
	owner_.OnGetIModelPropertiesComplete.Broadcast(bSuccess, bHasExtents, ProjectExtents, bHasEcefLocation, EcefLocation);
	if (observer_)
	{
		observer_->OnIModelPropertiesRetrieved(bSuccess, bHasExtents, ProjectExtents, bHasEcefLocation, EcefLocation);
	}
}

void UITwinWebServices::FImpl::OnIModelQueried(bool bSuccess, std::string const& QueryResult)
{
	owner_.OnQueryIModelComplete.Broadcast(bSuccess, FString(QueryResult.c_str()));
	if (observer_)
	{
		observer_->OnIModelQueried(bSuccess, FString(QueryResult.c_str()));
	}
}

void UITwinWebServices::FImpl::OnMaterialPropertiesRetrieved(bool bSuccess, SDK::Core::ITwinMaterialPropertiesMap const& coreProps)
{
	// TODO_JDE convert ITwinMaterialProperties into something we can manipulate through blueprints...
	if (observer_)
	{
		observer_->OnMaterialPropertiesRetrieved(bSuccess, coreProps);
	}
}

void UITwinWebServices::FImpl::OnTextureDataRetrieved(bool bSuccess, std::string const& textureId, SDK::Core::ITwinTextureData const& textureData)
{
	if (observer_)
	{
		observer_->OnTextureDataRetrieved(bSuccess, textureId, textureData);
	}
}

/// UITwinWebServices
UITwinWebServices::UITwinWebServices()
	: Impl(MakePimpl<UITwinWebServices::FImpl>(*this))
{
	// Adapt Unreal to SDK Core's Http request
	static bool bHasInitHttp = false;
	if (!bHasInitHttp)
	{
		bHasInitHttp = true;

		using namespace SDK::Core;
		HttpRequest::SetNewFct([]() {
			std::shared_ptr<HttpRequest> p(static_cast<HttpRequest*>(new FUEHttpRequest));
			return p;
		});
	}

#if 0 // IS_BE_DEV()
	FString bentleyEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("BENTLEY_ENV"));
	if (bentleyEnv == "DEV")
	{
		SetEnvironment(EITwinEnvironment::Dev);
	}
	else if (bentleyEnv == "QA")
	{
		SetEnvironment(EITwinEnvironment::QA);
	}
#endif
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
		return true;
	}
	else
	{
		return false;
	}
}

bool UITwinWebServices::CheckAuthorization()
{
	if (TryGetServerConnection(true))
	{
		// We could get a valid server connection. No need to do anything more (note that the token
		// will be automatically refreshed when approaching its expiration: no need to check that).
		return true;
	}
	Impl->authManager_->CheckAuthorization();
	return false;
}

void UITwinWebServices::OnAuthDoneImpl(bool bSuccess, FString const& Error, bool bBroadcastResult /*= true*/)
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
		OnAuthorizationChecked.Broadcast(bSuccess, Error);
		if (Impl->observer_)
		{
			Impl->observer_->OnAuthorizationDone(bSuccess, Error);
		}
	}
}

void UITwinWebServices::OnAuthorizationDone(bool bSuccess, FString const& Error)
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
	Impl->observer_ = InObserver;
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


void UITwinWebServices::GetAllSavedViews(FString iTwinId, FString iModelId, FString GroupId /*= ""*/)
{
	DoRequest([this, iTwinId, iModelId, GroupId]() {
		Impl->GetAllSavedViews(TCHAR_TO_ANSI(*iTwinId), TCHAR_TO_ANSI(*iModelId), TCHAR_TO_ANSI(*GroupId));
	});
}

void UITwinWebServices::GetSavedViewGroups(FString iTwinId, FString iModelId)
{
	DoRequest([this, iTwinId, iModelId]() {
		Impl->GetSavedViewsGroups(TCHAR_TO_ANSI(*iTwinId), TCHAR_TO_ANSI(*iModelId));
		});
}

void UITwinWebServices::GetSavedView(FString SavedViewId)
{
	DoRequest([this, SavedViewId]() { Impl->GetSavedView(TCHAR_TO_ANSI(*SavedViewId)); });
}

void UITwinWebServices::GetSavedViewThumbnail(FString SavedViewId)
{
	DoRequest([this, SavedViewId]() { Impl->GetSavedViewThumbnail(TCHAR_TO_ANSI(*SavedViewId)); });
}

void UITwinWebServices::UpdateSavedViewThumbnail(FString SavedViewId, FString ThumbnailURL)
{
	DoRequest([this, SavedViewId, ThumbnailURL]() { Impl->UpdateSavedViewThumbnail(TCHAR_TO_ANSI(*SavedViewId), TCHAR_TO_ANSI(*ThumbnailURL)); });
}

void UITwinWebServices::AddSavedView(FString ITwinId, FString IModelId, FSavedView SavedView, FSavedViewInfo SavedViewInfo, FString GroupId /*= ""*/)
{
	SDK::Core::SavedView coreSV;
	SDK::Core::SavedViewInfo coreSVInfo;
	ToCoreSavedView(SavedView, coreSV);
	ToCoreSavedViewInfo(SavedViewInfo, coreSVInfo);

	DoRequest([this, ITwinId, IModelId, &coreSV, &coreSVInfo, GroupId]() {
		Impl->AddSavedView(TCHAR_TO_ANSI(*ITwinId), TCHAR_TO_ANSI(*IModelId),
			coreSV, coreSVInfo, TCHAR_TO_ANSI(*GroupId));
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
	SDK::Core::SavedViewGroupInfo coreSVGroupInfo;
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
	SDK::Core::SavedView coreSV;
	SDK::Core::SavedViewInfo coreSVInfo;
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


void UITwinWebServices::GetElementProperties(FString iTwinId, FString iModelId, FString iChangesetId, FString ElementId)
{
	DoRequest([this, iTwinId, iModelId, iChangesetId, ElementId]() {
		Impl->GetElementProperties(TCHAR_TO_ANSI(*iTwinId), TCHAR_TO_ANSI(*iModelId), TCHAR_TO_ANSI(*iChangesetId), TCHAR_TO_ANSI(*ElementId));
	});
}

void UITwinWebServices::GetIModelProperties(FString iTwinId, FString iModelId, FString iChangesetId)
{
	DoRequest([this, iTwinId, iModelId, iChangesetId]() {
		Impl->GetIModelProperties(TCHAR_TO_ANSI(*iTwinId), TCHAR_TO_ANSI(*iModelId), TCHAR_TO_ANSI(*iChangesetId));
	});
}

void UITwinWebServices::QueryIModel(FString iTwinId, FString iModelId, FString iChangesetId,
									FString ECSQLQuery, int Offset, int Count)
{
	DoRequest([this, iTwinId, iModelId, iChangesetId, ECSQLQuery, Offset, Count]() {
		Impl->QueryIModel(TCHAR_TO_ANSI(*iTwinId), TCHAR_TO_ANSI(*iModelId), TCHAR_TO_ANSI(*iChangesetId),
						  TCHAR_TO_ANSI(*ECSQLQuery), Offset, Count);
	});
}

void UITwinWebServices::GetMaterialProperties(
	FString iTwinId, FString iModelId, FString iChangesetId,
	FString MaterialId)
{
	DoRequest([this, iTwinId, iModelId, iChangesetId, MaterialId]() {
		Impl->GetMaterialProperties(
			TCHAR_TO_ANSI(*iTwinId), TCHAR_TO_ANSI(*iModelId), TCHAR_TO_ANSI(*iChangesetId),
			TCHAR_TO_ANSI(*MaterialId));
	});
}

void UITwinWebServices::GetMaterialListProperties(
	FString iTwinId, FString iModelId, FString iChangesetId,
	TArray<FString> MaterialIds)
{
	DoRequest([this, iTwinId, iModelId, iChangesetId, MaterialIds]() {
		std::vector<std::string> coreMatIds;
		coreMatIds.reserve(MaterialIds.Num());
		for (FString const& id : MaterialIds)
			coreMatIds.push_back(TCHAR_TO_ANSI(*id));
		Impl->GetMaterialListProperties(
			TCHAR_TO_ANSI(*iTwinId), TCHAR_TO_ANSI(*iModelId), TCHAR_TO_ANSI(*iChangesetId),
			coreMatIds);
	});
}

void UITwinWebServices::GetTextureData(
	FString iTwinId, FString iModelId, FString iChangesetId,
	FString TextureId)
{
	DoRequest([this, iTwinId, iModelId, iChangesetId, TextureId]() {
		Impl->GetTextureData(
			TCHAR_TO_ANSI(*iTwinId), TCHAR_TO_ANSI(*iModelId), TCHAR_TO_ANSI(*iChangesetId),
			TCHAR_TO_ANSI(*TextureId));
	});
}
