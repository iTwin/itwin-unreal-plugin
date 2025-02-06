/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinRealityData.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinRealityData.h>
#include <ITwinGeolocation.h>
#include <ITwinServerConnection.h>
#include <ITwinSetupMaterials.h>
#include <ITwinCesium3DTileset.h>
#include <ITwinIModel.h>

#include <Dom/JsonObject.h>
#include <Dom/JsonValue.h>
#include <Engine/World.h>
#include <HttpModule.h>
#include <Interfaces/IHttpResponse.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>
#include "Decoration/ITwinDecorationHelper.h"

#include <EngineUtils.h> // for TActorIterator<>

class AITwinRealityData::FImpl
{
public:
	AITwinRealityData& Owner;
	double Latitude = 0;
	double Longitude = 0;
	AITwinDecorationHelper* DecorationPersistenceMgr = nullptr;
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

	void DestroyTileset();
	void OnLoadingUIEvent();
	void FindPersistenceMgr()
	{
		if (DecorationPersistenceMgr)
			return;
		//Look if a helper already exists:
		for (TActorIterator<AITwinDecorationHelper> DecoIter(Owner.GetWorld()); DecoIter; ++DecoIter)
		{
			DecorationPersistenceMgr = *DecoIter;
		}
		if (DecorationPersistenceMgr)
		{
			DecorationPersistenceMgr->OnSceneLoaded.AddDynamic(&Owner, &AITwinRealityData::OnSceneLoaded);
		}
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

bool AITwinRealityData::HasRealityDataIdentifiers() const
{
	return !RealityDataId.IsEmpty() && !ITwinId.IsEmpty();
}

void AITwinRealityData::OnSceneLoaded(bool success)
{

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
	if (WebServices && HasRealityDataIdentifiers())
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
	return GetTileset() != nullptr;
}

const AITwinCesium3DTileset* AITwinRealityData::GetTileset() const
{
	for (auto& Child : Children)
	{
		if (auto* Tileset = Cast<AITwinCesium3DTileset>(Child.Get()))
		{
			return Tileset;
		}
	}
	return nullptr;
}

AITwinCesium3DTileset* AITwinRealityData::GetMutableTileset()
{
	return const_cast<AITwinCesium3DTileset*>(GetTileset());
}

void AITwinRealityData::HideTileset(bool bHide)
{
	for (auto& Child : Children)
	{
		if (auto* Tileset = Cast<AITwinCesium3DTileset>(Child.Get()))
		{
			Tileset->SetActorHiddenInGame(bHide);
		}
	}
	if (!Impl->DecorationPersistenceMgr)
		Impl->FindPersistenceMgr();
	if (Impl->DecorationPersistenceMgr)
	{
		auto ss = Impl->DecorationPersistenceMgr->GetSceneInfo(EITwinModelType::RealityData, RealityDataId);
		if (!ss.Visibility.has_value() || *ss.Visibility != !bHide)
		{
			ss.Quality = !bHide;
			Impl->DecorationPersistenceMgr->SetSceneInfo(EITwinModelType::RealityData, RealityDataId, ss);
		}
	}
}
bool AITwinRealityData::IsTilesetHidden()
{
	auto tileset = GetTileset();
	if (!tileset)
		return false;
	return tileset->IsHidden();
}
void AITwinRealityData::SetMaximumScreenSpaceError(double InMaximumScreenSpaceError)
{
	for (auto& Child : Children)
	{
		if (auto* Tileset = Cast<AITwinCesium3DTileset>(Child.Get()))
		{
			Tileset->SetMaximumScreenSpaceError(InMaximumScreenSpaceError);
		}
	}
}

void AITwinRealityData::SetTilesetQuality(float Value)
{
	SetMaximumScreenSpaceError(ITwin::ToScreenSpaceError(Value));
	if (!Impl->DecorationPersistenceMgr)
		Impl->FindPersistenceMgr();
	if (Impl->DecorationPersistenceMgr)
	{
		auto ss = Impl->DecorationPersistenceMgr->GetSceneInfo(EITwinModelType::RealityData, RealityDataId);
		if (!ss.Quality.has_value() || fabs(*ss.Quality - Value) > 1e-5)
		{
			ss.Quality = Value;
			Impl->DecorationPersistenceMgr->SetSceneInfo(EITwinModelType::RealityData, RealityDataId, ss);
		}
	}
}

float AITwinRealityData::GetTilesetQuality() const
{
	AITwinCesium3DTileset const* Tileset = GetTileset();
	if (Tileset)
	{
		return ITwin::GetTilesetQuality(*Tileset);
	}
	else
	{
		return 0.f;
	}
}

std::optional<FCartographicProps> AITwinRealityData::GetNativeGeoreference() const
{
	if (bGeolocated)
	{
		FCartographicProps Props;
		Props.Latitude = Impl->Latitude;
		Props.Longitude = Impl->Longitude;
		return std::optional<FCartographicProps>(Props);
	}
	return std::optional<FCartographicProps>();
}

void AITwinRealityData::FImpl::DestroyTileset()
{
	ITwin::DestroyTilesetsInActor(Owner);
}

void AITwinRealityData::Reset()
{
	Impl->DestroyTileset();
}

void AITwinRealityData::FImpl::OnLoadingUIEvent()
{
	DestroyTileset();

	if (Owner.HasRealityDataIdentifiers())
	{
		Owner.UpdateRealityData();
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
		Impl->OnLoadingUIEvent();
	}
}
#endif //WITH_EDITOR

void AITwinRealityData::PostLoad()
{
	Super::PostLoad();

	if (HasRealityDataIdentifiers())
	{
		Impl->OnLoadingUIEvent();
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

void AITwinRealityData::SetOffset(const FVector& Pos, const FVector& Rot)
{
	SetActorLocationAndRotation(Pos, FQuat::MakeFromEuler(Rot));
}

void AITwinRealityData::GetOffset(FVector& Pos, FVector& Rot) const
{
	Pos = GetActorLocation() / 100.0;
	Rot = GetActorRotation().Euler();
}

