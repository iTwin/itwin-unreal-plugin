/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDigitalTwin.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinDigitalTwin.h>
#include <ITwinElementID.h>
#include <ITwinIModel.h>
#include <ITwinIModelInternals.h>
#include <ITwinMetadataConstants.h>
#include <ITwinRealityData.h>
#include <ITwinSynchro4DAnimator.h>
#include <ITwinServerConnection.h>
#include <ITwinCesiumMetadataPickingBlueprintLibrary.h>
#include <ITwinCesiumMetadataValue.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <Camera/CameraTypes.h>
#include <Dom/JsonObject.h>
#include <Dom/JsonValue.h>
#include <Engine/GameViewportClient.h>
#include <Engine/LocalPlayer.h>
#include <Engine/World.h>
#include <GameFramework/PlayerController.h>
#include <HttpModule.h>
#include <Interfaces/IHttpResponse.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>
#include <unordered_set>

class AITwinDigitalTwin::FImpl
{
public:
	AITwinDigitalTwin& Owner;

	FImpl(AITwinDigitalTwin& InOwner)
		:Owner(InOwner)
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
	UpdateITwin();
}

void AITwinDigitalTwin::OnITwinInfoRetrieved(bool bSuccess, FITwinInfo const& Info)
{
	if (bSuccess)
	{
		checkf(ITwinId == Info.Id, TEXT("mismatch in iTwin ID (%s vs %s)"), *ITwinId, *Info.Id);
#if WITH_EDITOR
		SetActorLabel(Info.DisplayName);
#endif
	}
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
		UE_LOG(LogTemp, Error, TEXT("ITwinDigitalTwin with no ITwinId cannot be updated"));
		return;
	}

	if (!Children.IsEmpty())
		return;

	if (CheckServerConnection() != AITwinServiceActor::EConnectionStatus::Connected)
	{
		// No authorization yet: postpone the actual update (see OnAuthorizationDone)
		return;
	}

#if WITH_EDITOR
	// make a request to get the display name
	WebServices->GetITwinInfo(ITwinId);
#endif

	// fetch iModels
	WebServices->GetiTwiniModels(ITwinId);

	// fetch reality data
	WebServices->GetRealityData(ITwinId);
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

namespace ITwin
{
	ITwinElementID VisitElementsUnderCursor(UWorld const* World,
		FVector2D& MousePosition,
		std::function<void(FHitResult const& , std::unordered_set<ITwinElementID>& )> const& HitResultHandler,
		uint32 const* pMaxUniqueElementsHit)
	{
		if (!World)
			return NOT_ELEMENT;
		APlayerController* PlayerController = World->GetFirstPlayerController();
		if (!PlayerController)
			return NOT_ELEMENT;
		ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
		if (!LocalPlayer || !LocalPlayer->ViewportClient)
			return NOT_ELEMENT;
		if (!LocalPlayer->ViewportClient->GetMousePosition(MousePosition))
			return NOT_ELEMENT;
		FVector WorldLoc, WorldDir;
		if (!PlayerController->DeprojectScreenPositionToWorld(MousePosition.X, MousePosition.Y,
			WorldLoc, WorldDir))
			return NOT_ELEMENT;
		TArray<FHitResult> AllHits;
		FComponentQueryParams queryParams;
		queryParams.bReturnFaceIndex = true;
		ITwinElementID firstEltID = NOT_ELEMENT;

		FVector::FReal const TraceExtent = 1000 * 100 /*ie 1km*/;
		FVector const TraceStart = WorldLoc;
		FVector const TraceEnd = WorldLoc + WorldDir * TraceExtent;

		bool bHasFrontHits = World->LineTraceMultiByObjectType(AllHits, TraceStart, TraceEnd,
			FCollisionObjectQueryParams::AllObjects, queryParams);

		// our materials are double-sided, and LineTraceMultiByObjectType misses back hits, so let's launch
		// a second "ray" to get potential back hits (I did not find any option to do it in one call)
		TArray<FHitResult> BackHits;
		bool bHasBackHits = World->LineTraceMultiByObjectType(BackHits, TraceEnd, TraceStart,
			FCollisionObjectQueryParams::AllObjects, queryParams);

		// merge both arrays
		if (bHasBackHits)
		{
			if (bHasFrontHits)
			{
				for (FHitResult& BackHit : BackHits)
				{
					float const DistFromStart = TraceExtent - BackHit.Distance;
					BackHit.Distance = DistFromStart;
					auto Index = AllHits.FindLastByPredicate(
						[DistFromStart](FHitResult const& HitRes)
					{
						return HitRes.Distance > DistFromStart;
					});
					if (Index == INDEX_NONE)
					{
						AllHits.Push(BackHit);
					}
					else
					{
						AllHits.Insert(BackHit, Index);
					}
				}
			}
			else
			{
				AllHits = BackHits;
			}
		}

		if (bHasBackHits || bHasFrontHits)
		{
			std::unordered_set<ITwinElementID> DejaVu;
			for (auto&& HitResult : AllHits)
			{
				HitResultHandler(HitResult, DejaVu);

				if (firstEltID == NOT_ELEMENT && DejaVu.size() == 1)
				{
					firstEltID = *DejaVu.cbegin();
				}
				if (!pMaxUniqueElementsHit || (uint32)DejaVu.size() >= (*pMaxUniqueElementsHit))
				{
					break; // avoid overflowing the logs, stop now
				}
			}
		}
		return firstEltID;
	}
}
