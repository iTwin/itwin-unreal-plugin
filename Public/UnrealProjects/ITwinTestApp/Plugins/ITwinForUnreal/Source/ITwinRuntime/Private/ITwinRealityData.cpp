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
#include <ITwinSetupMaterials.h>
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
		// *before* SpawnActor otherwise Cesium will create its own default georef
		auto&& Geoloc = FITwinGeolocation::Get(*Owner.GetWorld());
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
			if (Geoloc->GeoReference->GetOriginPlacement() == EITwinOriginPlacement::TrueOrigin)
			{
				// Common geolocation is not yet inited, use the location of this reality data.
				Geoloc->GeoReference->SetOriginPlacement(EITwinOriginPlacement::CartographicOrigin);
				Geoloc->GeoReference->SetOriginLatitude(Latitude);
				Geoloc->GeoReference->SetOriginLongitude(Longitude);
				Geoloc->GeoReference->SetOriginHeight(0);
			}
			Tileset->SetGeoreference(Geoloc->GeoReference.Get());
		}
		else
			Tileset->SetGeoreference(Geoloc->LocalReference.Get());
		// Make use of our own materials (important for packaged version!)
		ITwin::SetupMaterials(*Tileset);
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
	if (CheckServerConnection() != SDK::Core::EITwinAuthStatus::Success)
	{
		// No authorization yet: postpone the actual update (see OnAuthorizationDone)
		return;
	}
	if (WebServices && !RealityDataId.IsEmpty() && !ITwinId.IsEmpty())
	{
		WebServices->GetRealityData3DInfo(ITwinId, RealityDataId);
	}
}

namespace ITwin
{
	void DestroyTilesetsInActor(AActor& Owner);
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
	ITwin::DestroyTilesetsInActor(*this);
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
	if (ensure(bGeolocated))
	{
		auto&& Geoloc = FITwinGeolocation::Get(*GetWorld());
		Geoloc->GeoReference->SetOriginPlacement(EITwinOriginPlacement::CartographicOrigin);
		Geoloc->GeoReference->SetOriginLatitude(Impl->Latitude);
		Geoloc->GeoReference->SetOriginLongitude(Impl->Longitude);
	}
}

void AITwinRealityData::Destroyed()
{
	const auto ChildrenCopy = Children;
	for (auto& Child: ChildrenCopy)
		GetWorld()->DestroyActor(Child);
}
