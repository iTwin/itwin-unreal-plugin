/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinBrushHelper.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Helpers/ITwinBrushHelper.h>

#include <Components/StaticMeshComponent.h>
#include <Engine/World.h>
#include <Engine/StaticMesh.h>
#include <Engine/StaticMeshActor.h>


#include <Engine/World.h>
#include <Materials/MaterialInstanceDynamic.h>
#include <UObject/Package.h>


#define BRUSH_MESH_INVERSE_RADIUS 6.25e-3f // = 1/160


class FITwinBrushHelper::FImpl
{
public:
	TObjectPtr<AStaticMeshActor> BrushSphere = nullptr;

	void InitBrushSphere(UWorld& World);
};


void FITwinBrushHelper::FImpl::InitBrushSphere(UWorld& World)
{
	// Create the brush sphere and material (like in FoliageEdMode.cpp in the UE source code)
	UMaterialInstance* BrushMaterial = LoadObject<UMaterialInstance>(
		nullptr, TEXT("/ITwinForUnreal/ITwin/Materials/MI_BrushMaterial.MI_BrushMaterial"), nullptr, LOAD_None, nullptr);
	UMaterialInstanceDynamic* BrushMID =
		UMaterialInstanceDynamic::Create(BrushMaterial, GetTransientPackage());
	check(BrushMID != nullptr);
	UStaticMesh* BrushSphereMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/ITwinForUnreal/ITwin/Meshes/BrushSphere.BrushSphere"), nullptr, LOAD_None, nullptr);
	BrushSphere = World.SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass());
	BrushSphere->SetMobility(EComponentMobility::Movable);
	BrushSphere->SetActorLocation(FVector(0));
	BrushSphere->SetActorHiddenInGame(true);
	UStaticMeshComponent* BrushSphereComp = BrushSphere->GetStaticMeshComponent();
	BrushSphereComp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	BrushSphereComp->SetCollisionObjectType(ECC_WorldDynamic);
	BrushSphereComp->SetStaticMesh(BrushSphereMesh);
	BrushSphereComp->SetMaterial(0, BrushMID);
	BrushSphereComp->SetAbsolute(true, true, true);
	BrushSphereComp->CastShadow = false;
}


FITwinBrushHelper::FITwinBrushHelper()
	: Impl(MakePimpl<FImpl>())
{
}

void FITwinBrushHelper::InitBrushSphere(UWorld* World)
{
	if (ensure(World))
	{
		Impl->InitBrushSphere(*World);
	}
}

void FITwinBrushHelper::ShowBrushSphere()
{
	if (Impl->BrushSphere)
	{
		Impl->BrushSphere->SetActorHiddenInGame(false);
	}
}

void FITwinBrushHelper::HideBrushSphere()
{
	if (Impl->BrushSphere)
	{
		Impl->BrushSphere->SetActorHiddenInGame(true);
	}
}


void FITwinBrushHelper::SetBrushFlow(float Flow)
{
	BrushFlow.UserFactor = Flow;
}

void FITwinBrushHelper::SetBrushComputedValue(float InComputedValue)
{
	BrushFlow.ComputedValue = InComputedValue;
}

void FITwinBrushHelper::SetBrushRadius(float Radius)
{
	BrushRadius = Radius;
}

FVector FITwinBrushHelper::GetBrushPosition() const
{
	if (ensure(Impl->BrushSphere))
	{
		return Impl->BrushSphere->GetActorLocation();
	}
	else
	{
		return FVector::ZeroVector;
	}
}

void FITwinBrushHelper::SetBrushPosition(const FVector& Position)
{
	if (Impl->BrushSphere)
	{
		FTransform tm;
		tm.SetTranslation(Position);
		tm.SetScale3D(FVector(BrushRadius * BRUSH_MESH_INVERSE_RADIUS));
		Impl->BrushSphere->SetActorTransform(tm);
	}
}

bool FITwinBrushHelper::StartBrushing(float GameTimeSinceCreation)
{
	if (!ensure(Impl->BrushSphere))
		return false;
	bIsBrushing = true;
	BrushLastTime = GameTimeSinceCreation;
	BrushLastPos = Impl->BrushSphere->GetActorLocation();
	return true;
}

void FITwinBrushHelper::EndBrushing()
{
	bIsBrushing = false;
}
