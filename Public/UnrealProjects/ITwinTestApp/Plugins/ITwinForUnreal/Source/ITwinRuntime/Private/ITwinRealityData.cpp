/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinRealityData.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinRealityData.h>

#include <IncludeCesium3DTileset.h>
#include <ITwinGeolocation.h>
#include <ITwinServerConnection.h>
#include <ITwinSetupMaterials.h>
#include <ITwinIModel.h>
#include <ITwinTilesetAccess.h>
#include <ITwinTilesetAccess.inl>
#include <Clipping/ITwinClippingCustomPrimitiveDataHelper.h>

#include <Dom/JsonObject.h>
#include <Dom/JsonValue.h>
#include <Engine/World.h>
#include <HttpModule.h>
#include <Interfaces/IHttpResponse.h>
#include <ITwinWebServices/ITwinWebServices.h>
#include <Serialization/JsonReader.h>
#include <Serialization/JsonSerializer.h>
#include "Decoration/ITwinDecorationHelper.h"
#include <Kismet/GameplayStatics.h>
#include <EngineUtils.h> // for TActorIterator<>
#include <ITwinUtilityLibrary.h>



class AITwinRealityData::FTilesetAccess : public FITwinTilesetAccess
{
public:
	FTilesetAccess(AITwinRealityData* InRealityData);
	virtual TUniquePtr<FITwinTilesetAccess> Clone() const override;

	virtual ITwin::ModelDecorationIdentifier GetDecorationKey() const override;
	virtual AITwinDecorationHelper* GetDecorationHelper() const override;

private:
	TWeakObjectPtr<AITwinRealityData> RealityData;
};


class AITwinRealityData::FImpl
{
public:
	AITwinRealityData& Owner;
	double Latitude = 0;
	double Longitude = 0;
	AITwinDecorationHelper* DecorationPersistenceMgr = nullptr;
	uint32 TilesetLoadedCount = 0;
	TStrongObjectPtr<UITwinClippingCustomPrimitiveDataHelper> ClippingHelper;

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
		const auto Tileset = Owner.GetWorld()->SpawnActor<ACesium3DTileset>(SpawnParams);
#if WITH_EDITOR
		Tileset->SetActorLabel(Owner.GetActorLabel() + TEXT(" tileset"));
#endif
		Tileset->AttachToActor(&Owner, FAttachmentTransformRules::KeepRelativeTransform);
		if (ClippingHelper)
		{
			Tileset->SetLifecycleEventReceiver(ClippingHelper.Get());
		}
		Tileset->SetCreatePhysicsMeshes(true);// true since azdev#1737290
		Tileset->SetTilesetSource(ETilesetSource::FromUrl);
		Tileset->SetUrl(Info.MeshUrl);

		if (Info.bGeolocated)
		{
			Owner.bGeolocated = true;
			Latitude = 0.5 * (Info.ExtentNorthEast.Latitude + Info.ExtentSouthWest.Latitude);
			Longitude = 0.5 * (Info.ExtentNorthEast.Longitude + Info.ExtentSouthWest.Longitude);
			if (Geoloc->GeoReference->GetOriginPlacement() == EOriginPlacement::TrueOrigin
				|| Geoloc->bCanBypassCurrentLocation)
			{
				Geoloc->bCanBypassCurrentLocation = false;
				// Common geolocation is not yet inited, use the location of this reality data.
				Geoloc->GeoReference->SetOriginPlacement(EOriginPlacement::CartographicOrigin);
				Geoloc->GeoReference->SetOriginLatitude(Latitude);
				Geoloc->GeoReference->SetOriginLongitude(Longitude);
				Geoloc->GeoReference->SetOriginHeight(0);
				Geoloc->bNeedElevationEvaluation = true;

				// update decoration geo-reference
				AITwinDecorationHelper* DecoHelper = Cast<AITwinDecorationHelper>(UGameplayStatics::GetActorOfClass(Owner.GetWorld(), AITwinDecorationHelper::StaticClass()));
				if (DecoHelper)
				{
					FVector latLongHeight(Latitude, Longitude, 0);
					DecoHelper->SetDecoGeoreference(latLongHeight);
				}
			}
			Tileset->SetGeoreference(Geoloc->GeoReference.Get());
		}
		else
			Tileset->SetGeoreference(Geoloc->LocalReference.Get());
		// Make use of our own materials (important for packaged version!)
		ITwin::SetupMaterials(FTilesetAccess(&Owner));

		TilesetLoadedCount = 0;
		Tileset->OnTilesetLoaded.AddDynamic(&Owner, &AITwinRealityData::OnTilesetLoaded);
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

AITwinRealityData::~AITwinRealityData()
{
	Impl->ClippingHelper.Reset();
}

void AITwinRealityData::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Impl->ClippingHelper.Reset();
	Super::EndPlay(EndPlayReason);
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
	if (CheckServerConnection() != AdvViz::SDK::EITwinAuthStatus::Success)
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

const ACesium3DTileset* AITwinRealityData::GetTileset() const
{
	return ITwin::TGetTileset<ACesium3DTileset const>(*this);
}

ACesium3DTileset* AITwinRealityData::GetMutableTileset()
{
	return ITwin::TGetTileset<ACesium3DTileset>(*this);
}


AITwinRealityData::FTilesetAccess::FTilesetAccess(AITwinRealityData* InRealityData)
	: FITwinTilesetAccess(InRealityData)
	, RealityData(InRealityData)
{

}

TUniquePtr<FITwinTilesetAccess> AITwinRealityData::FTilesetAccess::Clone() const
{
	return MakeUnique<FTilesetAccess>(RealityData.Get());
}

ITwin::ModelDecorationIdentifier AITwinRealityData::FTilesetAccess::GetDecorationKey() const
{
	return std::make_pair(EITwinModelType::RealityData,
		RealityData.IsValid() ? RealityData->RealityDataId : FString());
}

AITwinDecorationHelper* AITwinRealityData::FTilesetAccess::GetDecorationHelper() const
{
	if (!RealityData.IsValid())
		return nullptr;
	if (!RealityData->Impl->DecorationPersistenceMgr)
	{
		RealityData->Impl->FindPersistenceMgr();
	}
	return RealityData->Impl->DecorationPersistenceMgr;
}

TUniquePtr<FITwinTilesetAccess> AITwinRealityData::MakeTilesetAccess()
{
	return MakeUnique<FTilesetAccess>(this);
}


void AITwinRealityData::OnTilesetLoaded()
{
	// Read comment in AITwinIModel::OnTilesetLoaded
	if (Impl->TilesetLoadedCount == 0)
	{
		this->OnRealityDataLoaded.Broadcast(true, RealityDataId);
	}
	Impl->TilesetLoadedCount++;
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
		Geoloc->GeoReference->SetOriginPlacement(EOriginPlacement::CartographicOrigin);
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

UITwinClippingCustomPrimitiveDataHelper* AITwinRealityData::GetClippingHelper() const
{
	return Impl->ClippingHelper.Get();
}

bool AITwinRealityData::MakeClippingHelper()
{
	if (RealityDataId.IsEmpty())
		return false;

	Impl->ClippingHelper =
		TStrongObjectPtr<UITwinClippingCustomPrimitiveDataHelper>(NewObject<UITwinClippingCustomPrimitiveDataHelper>(this));
	Impl->ClippingHelper->SetModelIdentifier(
		std::make_pair(EITwinModelType::RealityData, RealityDataId));

	ACesium3DTileset* Tileset = GetMutableTileset();
	if (Tileset)
	{
		// Connect mesh creation callback
		Tileset->SetLifecycleEventReceiver(Impl->ClippingHelper.Get());
	}
	return true;
}

CesiumGeometry::OrientedBoundingBox GetOrientedBoundingBox(ACesium3DTileset* Tileset)
{
	const Cesium3DTilesSelection::Tile* pRootTile = Tileset->GetTileset()->getRootTile();
	const ACesiumGeoreference* pGeoreference = Tileset->ResolveGeoreference();

	if (pRootTile && pGeoreference) {
		return Cesium3DTilesSelection::getOrientedBoundingBoxFromBoundingVolume(
			pRootTile->getBoundingVolume(),
			pGeoreference->GetEllipsoid()->GetNativeEllipsoid());
	}
	return CesiumGeometry::OrientedBoundingBox(glm::dvec3(0, 0, 0), glm::dmat3(1));
}


FBox GetUnrealAxisAlignBoundingBox(ACesium3DTileset* Tileset)
{
	const ACesiumGeoreference* pGeoreference = Tileset->ResolveGeoreference();
	CesiumGeometry::OrientedBoundingBox obb = GetOrientedBoundingBox(Tileset);

	std::array<glm::dvec3, 8> corners = { glm::dvec3(1.,1.,1.),  glm::dvec3(-1.,1.,1.),  glm::dvec3(-1.,-1.,1.),  glm::dvec3(1.,-1.,1.),
										 glm::dvec3(1.,1.,-1.), glm::dvec3(-1.,1.,-1.), glm::dvec3(-1.,-1.,-1.), glm::dvec3(1.,-1.,-1.)
	};

	FBox box;
	for (const glm::dvec3& corner : corners) {
		glm::dvec3 cornerPos = obb.getCenter() + obb.getHalfAxes() * corner;
		FVector cornerPos2(cornerPos.x, cornerPos.y, cornerPos.z);
		box += pGeoreference->TransformEarthCenteredEarthFixedPositionToUnreal(
			cornerPos2);
	}
	return box;
}

void AITwinRealityData::ZoomOnRealityData()
{
	ACesium3DTileset* Tileset = GetMutableTileset();
	if (Tileset)
	{
		FBox TilesetBBox = UITwinUtilityLibrary::GetUnrealAxisAlignBoundingBox(Tileset);
		// hack around extravagant project extents: limit half size to 10km: it looks big but there is a x0.2
		// empirical ratio in FImpl::ZoomOn already...
		double const MaxHalfSize = 10'000 * 100;
		FVector Ctr, HalfSize;
		TilesetBBox.GetCenterAndExtents(Ctr, HalfSize);
		if (TilesetBBox.IsValid && HalfSize.GetAbsMax() < MaxHalfSize)
		{
			UITwinUtilityLibrary::ZoomOn(TilesetBBox, GetWorld());
		}
		else
		{
			double Ratio = MaxHalfSize;
			if (std::abs(HalfSize.X) >= MaxHalfSize)
				Ratio /= std::abs(HalfSize.X);
			else if (std::abs(HalfSize.Y) >= MaxHalfSize)
				Ratio /= std::abs(HalfSize.Y);
			else
				Ratio /= std::abs(HalfSize.Z);
			UITwinUtilityLibrary::ZoomOn(FBox(Ctr - Ratio * HalfSize, Ctr + Ratio * HalfSize), GetWorld());
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs FCmd_ZoomOnRealityData(
	TEXT("cmd.ZoomOnRealityData"),
	TEXT("Refresh all iModel tilesets."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			TActorIterator<AITwinRealityData> RealityDataIter(World);
			if (!RealityDataIter)
				return;
			(*RealityDataIter)->ZoomOnRealityData();
		}));
