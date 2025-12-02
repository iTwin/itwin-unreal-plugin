/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDigitalTwin.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinDigitalTwin.h>

#include <ITwinIModel.h>
#include <ITwinRealityData.h>
#include <ITwinSynchro4DAnimator.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <Engine/World.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeUtils/Misc/RWLock.h>
#	include <Core/ITwinAPI/ITwinTypes.h>
#	include <Core/Tools/Log.h>
#include <Compil/AfterNonUnrealIncludes.h>

namespace ITwin
{
	extern void LoadScene(FString const& ITwinID, UWorld* World);
	extern void SaveScene(FString const& ITwinID, UWorld const* World);
}

class AITwinDigitalTwin::FImpl
{
public:
	AITwinDigitalTwin& Owner;

	// Some operations require to first fetch a valid server connection
	enum class EOperationUponAuth : uint8
	{
		None,
		Update,
		LoadDecoration
	};
	EOperationUponAuth PendingOperation = EOperationUponAuth::None;

	// iTwin info
	std::shared_mutex ITwinInfoMutex;
	enum class EITwinInfoRequestStatus : uint8_t
	{
		NotStarted,
		InProgress,
		Done
	};
	EITwinInfoRequestStatus ITwinInfoRequestStatus = EITwinInfoRequestStatus::NotStarted;
	std::optional<AdvViz::SDK::ITwinGeolocationInfo> GeolocInfo;


	FImpl(AITwinDigitalTwin& InOwner)
		: Owner(InOwner)
	{
	}

	void DestroyChildren()
	{
		const auto ChildrenCopy = Owner.Children;
		for (auto& Child: ChildrenCopy)
			Owner.GetWorld()->DestroyActor(Child);
		Owner.Children.Empty();
	}
};

AITwinDigitalTwin::AITwinDigitalTwin()
	:Impl(MakePimpl<FImpl>(*this))
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("root")));
}

const TCHAR* AITwinDigitalTwin::GetObserverName() const
{
	return TEXT("ITwinDigitalTwin");
}

void AITwinDigitalTwin::UpdateOnSuccessfulAuthorization()
{
	switch (Impl->PendingOperation)
	{
	case FImpl::EOperationUponAuth::Update:
		UpdateITwin();
		break;
	case FImpl::EOperationUponAuth::LoadDecoration:
		LoadDecoration();
		break;
	case FImpl::EOperationUponAuth::None:
		break;
	}
	Impl->PendingOperation = FImpl::EOperationUponAuth::None;
}

void AITwinDigitalTwin::OnITwinInfoRetrieved(bool bSuccess, AdvViz::SDK::ITwinInfo const& Info)
{
	BeUtils::WLock WLock(Impl->ITwinInfoMutex);
	if (bSuccess)
	{
		ensureMsgf(ITwinId == ANSI_TO_TCHAR(Info.id.c_str()),
			TEXT("mismatch in iTwin ID (%s vs %s)"), *ITwinId, ANSI_TO_TCHAR(Info.id.c_str()));
#if WITH_EDITOR
		SetActorLabel(UTF8_TO_TCHAR(Info.displayName.value_or(std::u8string()).c_str()));
#endif
		// Store latitude & longitude registered at the iTwin level.
		// TODO_JDE We could/should use it to customize the default geo-location as in iTwin Engage
		if (Info.latitude && Info.longitude)
		{
			Impl->GeolocInfo.emplace(AdvViz::SDK::ITwinGeolocationInfo
				{
					.latitude = *Info.latitude,
					.longitude = *Info.longitude
				});
		}
		else
		{
			Impl->GeolocInfo.reset();
		}
	}
	Impl->ITwinInfoRequestStatus = FImpl::EITwinInfoRequestStatus::Done;
}

void AITwinDigitalTwin::OnIModelsRetrieved(bool bSuccess, FIModelInfos const& IModelInfos)
{
	if (!bSuccess)
		return;

	for (FIModelInfo const& IModelInfo : IModelInfos.iModels)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		const auto IModel = GetWorld()->SpawnActor<AITwinIModel>(SpawnParams);
#if WITH_EDITOR
		IModel->SetActorLabel(IModelInfo.DisplayName);
#endif
		// GCO/Note: I'm using the attachment to list iModels in an iTwin:
		// (see FITwinSynchro4DAnimator::FImpl::TickImpl)
		IModel->AttachToActor(this, FAttachmentTransformRules::KeepRelativeTransform);
		IModel->ServerConnection = ServerConnection;
		IModel->IModelId = IModelInfo.Id;
		IModel->ITwinId = ITwinId;
		IModel->UpdateIModel();
	}
}

void AITwinDigitalTwin::OnRealityDataRetrieved(bool bSuccess, FITwinRealityDataInfos const& RealityDataInfos)
{
	if (!bSuccess)
		return;

	for (FITwinRealityDataInfo const& ReaDataInfo : RealityDataInfos.Infos)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		const auto RealityData = GetWorld()->SpawnActor<AITwinRealityData>(SpawnParams);
#if WITH_EDITOR
		RealityData->SetActorLabel(ReaDataInfo.DisplayName);
#endif
		RealityData->AttachToActor(this, FAttachmentTransformRules::KeepRelativeTransform);
		RealityData->ServerConnection = ServerConnection;
		RealityData->RealityDataId = ReaDataInfo.Id;
		RealityData->ITwinId = ITwinId;
		RealityData->UpdateRealityData();
	}
}

void AITwinDigitalTwin::UpdateITwin()
{
	if (ITwinId.IsEmpty())
	{
		BE_LOGE("ITwinAPI", "ITwinDigitalTwin with no ITwinId cannot be updated");
		return;
	}

	if (!Children.IsEmpty())
		return;

	if (CheckServerConnection() != AdvViz::SDK::EITwinAuthStatus::Success)
	{
		// No authorization yet: postpone the actual update (see OnAuthorizationDone)
		Impl->PendingOperation = FImpl::EOperationUponAuth::Update;
		return;
	}

	// make a request to get the display name
	// (we could possibly use the optional iTwin's geo-location...)
	WebServices->GetITwinInfo(ITwinId);
	{
		BeUtils::WLock WLock(Impl->ITwinInfoMutex);
		Impl->ITwinInfoRequestStatus = FImpl::EITwinInfoRequestStatus::InProgress;
	}

	// fetch iModels
	WebServices->GetiTwiniModels(ITwinId);

	// fetch reality data
	WebServices->GetRealityData(ITwinId);
}

void AITwinDigitalTwin::LoadDecoration()
{
	if (ITwinId.IsEmpty())
	{
		BE_LOGE("ITwinAPI", "ITwinID is required to load decoration");
		return;
	}

	// If no access token has been retrieved yet, make sure we request one.
	if (CheckServerConnection() != AdvViz::SDK::EITwinAuthStatus::Success)
	{
		Impl->PendingOperation = FImpl::EOperationUponAuth::LoadDecoration;
		return;
	}
	ITwin::LoadScene(ITwinId, GetWorld());
}

void AITwinDigitalTwin::SaveDecoration()
{
	ITwin::SaveScene(ITwinId, GetWorld());
}

#if WITH_EDITOR
void AITwinDigitalTwin::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AITwinDigitalTwin, ServerConnection) ||
		PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(AITwinDigitalTwin, ITwinId))
		Impl->DestroyChildren();
}
#endif

void AITwinDigitalTwin::Destroyed()
{
	Super::Destroyed();
	Impl->DestroyChildren();
}
