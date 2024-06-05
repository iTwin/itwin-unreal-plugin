/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDigitalTwin.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinDigitalTwin.h>
#include <ITwinElementID.h>
#include <ITwinGeolocation.h>
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
	TSharedPtr<FITwinGeolocation> Geolocation;

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

	void OnClickedElement(FHitResult const& HitResult, std::unordered_set<ITwinElementID>& DejaVu);
};

AITwinDigitalTwin::AITwinDigitalTwin()
	:Impl(MakePimpl<FImpl>(*this))
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("root")));
}

void AITwinDigitalTwin::OnAuthorizationDone(bool bSuccess, FString const& Error)
{
	if (bSuccess)
	{
		UpdateWebServices();
		if (ServerConnection && !ServerConnection->AccessToken.IsEmpty())
		{
			UpdateITwin();
		}
	}
	else
	{
		UE_LOG(LrtuITwin, Error, TEXT("AITwinDigitalTwin Authorization failure (%s)"), *Error);
	}
}

void AITwinDigitalTwin::OnITwinsRetrieved(bool bSuccess, FITwinInfos const& Infos)
{
	checkf(false, TEXT("an iTwin cannot handle other iTwins"));
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
		IModel->Geolocation = Impl->Geolocation;
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
		RealityData->Geolocation = Impl->Geolocation;
		RealityData->RealityDataId = ReaDataInfo.Id;
		RealityData->ITwinId = ITwinId;
		RealityData->UpdateRealityData();
	}
}

void AITwinDigitalTwin::OnChangesetsRetrieved(bool bSuccess, FChangesetInfos const& ChangesetInfos)
{
	checkf(false, TEXT("an iTwin cannot handle changesets"));
}
void AITwinDigitalTwin::OnExportInfosRetrieved(bool bSuccess, FITwinExportInfos const& ExportInfos)
{
	checkf(false, TEXT("an iTwin cannot handle exports"));
}
void AITwinDigitalTwin::OnExportInfoRetrieved(bool bSuccess, FITwinExportInfo const& ExportInfo)
{
	checkf(false, TEXT("an iTwin cannot handle exports"));
}
void AITwinDigitalTwin::OnExportStarted(bool bSuccess, FString const& ExportId)
{
	checkf(false, TEXT("an iTwin cannot handle exports"));
}
void AITwinDigitalTwin::OnSavedViewInfosRetrieved(bool bSuccess, FSavedViewInfos const& Infos)
{
	checkf(false, TEXT("an iTwin cannot handle SavedViews"));
}
void AITwinDigitalTwin::OnSavedViewRetrieved(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo)
{
	checkf(false, TEXT("an iTwin cannot handle SavedViews"));
}
void AITwinDigitalTwin::OnSavedViewAdded(bool bSuccess, FSavedViewInfo const& SavedViewInfo)
{
	checkf(false, TEXT("an iTwin cannot handle SavedViews"));
}
void AITwinDigitalTwin::OnSavedViewDeleted(bool bSuccess, FString const& Response)
{
	checkf(false, TEXT("an iTwin cannot handle SavedViews"));
}
void AITwinDigitalTwin::OnSavedViewEdited(bool bSuccess, FSavedView const& SavedView, FSavedViewInfo const& SavedViewInfo)
{
	checkf(false, TEXT("an iTwin cannot handle SavedViews"));
}


namespace ITwin
{
	void UpdateWebServices(AActor* Owner, IITwinWebServicesObserver* InObserver,
		TObjectPtr<AITwinServerConnection>& ServerConnection,
		TObjectPtr<UITwinWebServices>& WebServices);
}

void AITwinDigitalTwin::UpdateWebServices()
{
	// use same code as for AITwinIModel...
	ITwin::UpdateWebServices(this, this, ServerConnection, WebServices);
}

void AITwinDigitalTwin::UpdateITwin()
{
	check(!ITwinId.IsEmpty());
	if (!Children.IsEmpty())
		return;

	UpdateWebServices();
	if (!ServerConnection)
	{
		WebServices->CheckAuthorization();
		// postpone the actual update (see OnAuthorizationDone)
		return;
	}

	Impl->Geolocation = MakeShared<FITwinGeolocation>(*this);
	{
		const auto Request = FHttpModule::Get().CreateRequest();
		Request->SetVerb("GET");
		Request->SetURL("https://"+ServerConnection->UrlPrefix()+"api.bentley.com/itwins/"+ITwinId);
		Request->SetHeader("Prefer", "return=representation");
		Request->SetHeader("Accept", "application/vnd.bentley.itwin-platform.v1+json");
		Request->SetHeader("Authorization", "Bearer "+ServerConnection->AccessToken);
		Request->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
			{
				if (!AITwinServerConnection::CheckRequest(Request, Response, bConnectedSuccessfully))
					{ return; }
				TSharedPtr<FJsonObject> ResponseJson;
				FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), ResponseJson);
#if WITH_EDITOR
				SetActorLabel(ResponseJson->GetObjectField("iTwin")->GetStringField("displayName"));
#endif
			});
		Request->ProcessRequest();
	}

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
	if (WebServices)
	{
		WebServices->SetObserver(nullptr);
	}
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

void AITwinDigitalTwin::IdentifyElementsUnderCursor(uint32 const* pMaxUniqueElementsHit)
{
	FVector2D MousePosition;
	ITwin::VisitElementsUnderCursor(GetWorld(), MousePosition,
		[this](FHitResult const& HitResult, std::unordered_set<ITwinElementID>& DejaVu)
	{
		Impl->OnClickedElement(HitResult, DejaVu);
	}, pMaxUniqueElementsHit);
}

void AITwinDigitalTwin::FImpl::OnClickedElement(FHitResult const& HitResult,
												std::unordered_set<ITwinElementID>& DejaVu)
{
	TMap<FString, FITwinCesiumMetadataValue> const Table =
		UITwinCesiumMetadataPickingBlueprintLibrary::GetPropertyTableValuesFromHit(HitResult);
	TMap<FString, FString> const AsStrings =
		UITwinCesiumMetadataValueBlueprintLibrary::GetValuesAsStrings(Table);
	FITwinCesiumMetadataValue const* const ElementIdFound = Table.Find(ITwinCesium::Metada::ELEMENT_NAME);
	ITwinElementID const ElementID = (ElementIdFound != nullptr)
		? ITwinElementID(UITwinCesiumMetadataValueBlueprintLibrary::GetUnsignedInteger64(
						 *ElementIdFound, ITwin::NOT_ELEMENT.value()))
		: ITwin::NOT_ELEMENT;
	if (ElementID != ITwin::NOT_ELEMENT)
	{
		if (DejaVu.end() != DejaVu.find(ElementID)) // TODO_GCO: could be different iModels...
		{
			return;
		}
		DejaVu.insert(ElementID);
		UE_LOG(LrtuITwin, Display,
			   TEXT("ElementID 0x%I64x clicked, with additional metadata:"), ElementID.value());
		for (TTuple<FString, FString> const& Entry : AsStrings)
		{
			if (ITwinCesium::Metada::ELEMENT_NAME != Entry.Get<0>())
				UE_LOG(LrtuITwin, Display, TEXT("%s = %s"), *Entry.Get<0>(), *Entry.Get<1>());
		}
		for (auto&& Child : Owner.Children)
		{
			AITwinIModel* AsIModel = Cast<AITwinIModel>(Child.Get());
			if (!AsIModel)
				continue;
			FITwinIModelInternals& IModelInternals = GetInternals(*AsIModel);
			if (IModelInternals.HasElementWithID(ElementID))
			{
				IModelInternals.OnClickedElement(ElementID, HitResult);
			}
		}
	}
	else
	{
		UE_LOG(LrtuITwin, Display, TEXT("'%s' not found in metadata, instead got:"),
			   *ITwinCesium::Metada::ELEMENT_NAME);
		for (TTuple<FString, FString> const& Entry : AsStrings)
		{
			UE_LOG(LrtuITwin, Display, TEXT("%s = %s"), *Entry.Get<0>(), *Entry.Get<1>());
		}
	}
}
