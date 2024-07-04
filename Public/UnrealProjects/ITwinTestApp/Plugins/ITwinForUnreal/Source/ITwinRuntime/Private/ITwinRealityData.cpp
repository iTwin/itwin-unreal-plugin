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
		: Owner(InOwner)
	{
	}

	void OnRealityData3DInfoRetrieved(FITwinRealityData3DInfo const& Info)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = &Owner;
		const auto Tileset = Owner.GetWorld()->SpawnActor<AITwinCesium3DTileset>(SpawnParams);
#if WITH_EDITOR
		Tileset->SetActorLabel(Owner.GetActorLabel() + TEXT(" tileset"));
#endif
		Tileset->AttachToActor(&Owner, FAttachmentTransformRules::KeepRelativeTransform);
		Tileset->SetCreatePhysicsMeshes(false);
		Tileset->SetTilesetSource(EITwinTilesetSource::FromUrl);
		Tileset->SetUrl(Info.MeshUrl);

		if (Info.bGeolocated)
		{
			Owner.bGeolocated = true;
			Latitude = 0.5 * (Info.ExtentNorthEast.Latitude + Info.ExtentSouthWest.Latitude);
			Longitude = 0.5 * (Info.ExtentNorthEast.Longitude + Info.ExtentSouthWest.Longitude);
			Tileset->SetGeoreference(Owner.Geolocation->LocatedGeoreference);
			if (Tileset->GetGeoreference()->GetOriginPlacement() == EITwinOriginPlacement::TrueOrigin)
			{
				// Common geolocation is not yet inited, use the location of this reality data.
				Tileset->GetGeoreference()->SetOriginPlacement(EITwinOriginPlacement::CartographicOrigin);
				Tileset->GetGeoreference()->SetOriginLatitude(Latitude);
				Tileset->GetGeoreference()->SetOriginLongitude(Longitude);
				Tileset->GetGeoreference()->SetOriginHeight(0);
			}
		}
		else
			Tileset->SetGeoreference(Owner.Geolocation->NonLocatedGeoreference);
	}
};

AITwinRealityData::AITwinRealityData()
	:Impl(MakePimpl<FImpl>(*this))
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("root")));
}

void AITwinRealityData::UpdateOnSuccessfulAuthorization()
{
	UpdateRealityData();
}

const TCHAR* AITwinRealityData::GetObserverName() const
{
	return TEXT("ITwinRealityData");
}

void AITwinRealityData::OnRealityData3DInfoRetrieved(bool bSuccess, FITwinRealityData3DInfo const& Info)
{
	if (bSuccess)
	{
		Impl->OnRealityData3DInfoRetrieved(Info);

#if WITH_EDITOR
		if (!Info.DisplayName.IsEmpty())
		{
			SetActorLabel(Info.DisplayName);
		}
#endif
	}
}

void AITwinRealityData::UpdateRealityData()
{
	if (HasTileset())
		return;

	if (CheckServerConnection() != AITwinServiceActor::EConnectionStatus::Connected)
	{
		// No authorization yet: postpone the actual update (see OnAuthorizationDone)
		return;
	}

	if (!Geolocation)
	{
		// Idem: can happen if this was created manually, outside any instance of AITwinDigitalTwin
		Geolocation = MakeShared<FITwinGeolocation>(*this);
	}

	if (WebServices && !RealityDataId.IsEmpty() && !ITwinId.IsEmpty())
	{
		WebServices->GetRealityData3DInfo(ITwinId, RealityDataId);
	}
}

namespace ITwin
{
	void DestroyTilesetsInActor(AActor& Owner, bool bDestroyCesiumGeoref);
}

bool AITwinRealityData::HasTileset() const
{
	for (auto const& Child : Children)
	{
		if (Cast<AITwinCesium3DTileset const>(Child.Get()))
		{
			return true;
		}
	}
	return false;
}

void AITwinRealityData::DestroyTileset()
{
	// see comment in AITwinIModel::DestroyTileset()
	bool bDestroyCesiumGeoref = !Geolocation;
	ITwin::DestroyTilesetsInActor(*this, bDestroyCesiumGeoref);
}

void AITwinRealityData::Reset()
{
	DestroyTileset();
}

void AITwinRealityData::OnLoadingUIEvent()
{
	DestroyTileset();

	if (!RealityDataId.IsEmpty() && !ITwinId.IsEmpty())
	{
		UpdateRealityData();
	}
}

#if WITH_EDITOR
void AITwinRealityData::PostEditChangeProperty(FPropertyChangedEvent& e)
{
	Super::PostEditChangeProperty(e);

	FName const PropertyName = (e.Property != nullptr) ? e.Property->GetFName() : NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(AITwinRealityData, RealityDataId)
		||
		PropertyName == GET_MEMBER_NAME_CHECKED(AITwinRealityData, ITwinId))
	{
		OnLoadingUIEvent();
	}
}
#endif //WITH_EDITOR

void AITwinRealityData::PostLoad()
{
	Super::PostLoad();

	if (!RealityDataId.IsEmpty() && !ITwinId.IsEmpty())
	{
		OnLoadingUIEvent();
	}
}

void AITwinRealityData::UseAsGeolocation()
{
	if (ensure(bGeolocated && Geolocation))
	{
		Geolocation->LocatedGeoreference->SetOriginPlacement(EITwinOriginPlacement::CartographicOrigin);
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
