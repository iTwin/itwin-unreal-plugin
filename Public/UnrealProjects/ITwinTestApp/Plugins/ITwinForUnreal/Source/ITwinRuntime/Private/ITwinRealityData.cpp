/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinRealityData.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinRealityData.h>
#include <ITwinGeolocation.h>
#include <ITwinServerConnection.h>
#include <ITwinCesium3DTileset.h>

#include <Dom/JsonObject.h>
#include <Dom/JsonValue.h>
#include <Engine/World.h>
#include <HttpModule.h>
#include <Interfaces/IHttpResponse.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>

class AITwinRealityData::FImpl
{
public:
	AITwinRealityData& Owner;
	double Latitude = 0;
	double Longitude = 0;
	FImpl(AITwinRealityData& InOwner)
		:Owner(InOwner)
	{
	}
	void OnMetadataRetrieved(const TSharedPtr<FJsonObject>& MetadataJson)
	{
		const auto Request = FHttpModule::Get().CreateRequest();
		Request->SetVerb("GET");
		Request->SetURL("https://"+Owner.ServerConnection->UrlPrefix()+"api.bentley.com/reality-management/reality-data/"+Owner.RealityDataId+"/readaccess?iTwinId="+Owner.ITwinId);
		Request->SetHeader("Accept", "application/vnd.bentley.itwin-platform.v1+json");
		Request->SetHeader("Authorization", "Bearer "+Owner.ServerConnection->AccessToken);
		Request->OnProcessRequestComplete().BindLambda(
			[this, MetadataJson](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
			{
				if (!AITwinServerConnection::CheckRequest(Request, Response, bConnectedSuccessfully))
					{ check(false); return; }
				TSharedPtr<FJsonObject> ResponseJson;
				FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), ResponseJson);
				FActorSpawnParameters SpawnParams;
				SpawnParams.Owner = &Owner;
				const auto Tileset = Owner.GetWorld()->SpawnActor<AITwinCesium3DTileset>(SpawnParams);
#if WITH_EDITOR
				Tileset->SetActorLabel(Owner.GetActorLabel()+TEXT(" tileset"));
#endif
				Tileset->AttachToActor(&Owner, FAttachmentTransformRules::KeepRelativeTransform);
				Tileset->SetCreatePhysicsMeshes(false);
				Tileset->SetTilesetSource(ETilesetSource::FromUrl);
				Tileset->SetUrl(ResponseJson->GetObjectField("_links")->GetObjectField("containerUrl")->GetStringField("href").
					Replace(TEXT("?"), ToCStr("/"+MetadataJson->GetStringField("rootDocument")+"?")));
				const TSharedPtr<FJsonObject>* ExtentJson;
				if (MetadataJson->TryGetObjectField("extent", ExtentJson))
				{
					Owner.bGeolocated = true;
					Latitude = 0.5*((*ExtentJson)->GetObjectField("southWest")->GetNumberField("latitude")+
							(*ExtentJson)->GetObjectField("northEast")->GetNumberField("latitude"));
					Longitude = 0.5*((*ExtentJson)->GetObjectField("southWest")->GetNumberField("longitude")+
							(*ExtentJson)->GetObjectField("northEast")->GetNumberField("longitude"));
					Tileset->SetGeoreference(Owner.Geolocation->LocatedGeoreference.Get());
					if (Tileset->GetGeoreference()->GetOriginPlacement() == EOriginPlacement::TrueOrigin)
					{
						// Common geolocation is not yet inited, use the location of this reality data.
						Tileset->GetGeoreference()->SetOriginPlacement(EOriginPlacement::CartographicOrigin);
						Tileset->GetGeoreference()->SetOriginLatitude(Latitude);
						Tileset->GetGeoreference()->SetOriginLongitude(Longitude);
						Tileset->GetGeoreference()->SetOriginHeight(0);
					}
				}
				else
					Tileset->SetGeoreference(Owner.Geolocation->NonLocatedGeoreference.Get());
			});
		Request->ProcessRequest();
	}
};

AITwinRealityData::AITwinRealityData()
	:Impl(MakePimpl<FImpl>(*this))
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("root")));
}

void AITwinRealityData::UpdateRealityData()
{
	check(!RealityDataId.IsEmpty()); // TODO_AW

	if (!ServerConnection && UITwinWebServices::GetWorkingInstance())
	{
		// Happens when the requests are made from blueprints, independently from ITwinDigitalTwin
		// through OnGetRealityDataComplete callback...
		UITwinWebServices::GetWorkingInstance()->GetServerConnection(ServerConnection);
	}
	if (!Geolocation)
	{
		// Idem: can happen if this was created manually, outside any instance of AITwinDigitalTwin
		Geolocation = MakeShared<FITwinGeolocation>(*this);
	}

	if (!ServerConnection)
	{
		checkf(false, TEXT("no server connection"));
		return;
	}

	if (!Children.IsEmpty())
		return;
	{
		const auto Request = FHttpModule::Get().CreateRequest();
		Request->SetVerb("GET");
		Request->SetURL("https://"+ServerConnection->UrlPrefix()+"api.bentley.com/reality-management/reality-data/"+RealityDataId+"?iTwinId="+ITwinId);
		Request->SetHeader("Accept", "application/vnd.bentley.itwin-platform.v1+json");
		Request->SetHeader("Authorization", "Bearer "+ServerConnection->AccessToken);
		Request->OnProcessRequestComplete().BindLambda(
			[this](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
			{
				if (!AITwinServerConnection::CheckRequest(Request, Response, bConnectedSuccessfully))
					{ check(false); return; }
				TSharedPtr<FJsonObject> ResponseJson;
				FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), ResponseJson);
				Impl->OnMetadataRetrieved(ResponseJson->GetObjectField("realityData"));
			});
		Request->ProcessRequest();
	}
}

void AITwinRealityData::UseAsGeolocation()
{
	check(bGeolocated && Geolocation);
	if (Geolocation)
	{
		Geolocation->LocatedGeoreference->SetOriginPlacement(EOriginPlacement::CartographicOrigin);
		Geolocation->LocatedGeoreference->SetOriginLatitude(Impl->Latitude);
		Geolocation->LocatedGeoreference->SetOriginLongitude(Impl->Longitude);
	}
}

void AITwinRealityData::Destroyed()
{
	const auto ChildrenCopy = Children;
	for (auto& Child: ChildrenCopy)
		GetWorld()->DestroyActor(Child);
}
