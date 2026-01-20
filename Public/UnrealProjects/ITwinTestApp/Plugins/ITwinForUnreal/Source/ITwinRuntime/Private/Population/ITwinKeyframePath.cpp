/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinKeyframePath.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <Population/ITwinKeyframePath.h>

#include <Engine/GameViewportClient.h>
#include <Engine/Level.h>
#include <Engine/LevelBounds.h>
#include <Engine/LocalPlayer.h>
#include <Engine/World.h>
#include <Engine/Engine.h>
#include "DrawDebugHelpers.h"
#include "Math/Plane.h"
#include "Kismet/GameplayStatics.h"
#include <SceneView.h>
#include <UnrealClient.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Visualization/KeyframeAnimator.h>
#include <Compil/AfterNonUnrealIncludes.h>

//#define VISSIM_DEMO

#ifdef VISSIM_DEMO
// VISSIM Test: makes unit conversion and offset animation.
class MyTransform : public AdvViz::SDK::Tools::IGCSTransform
{
public:
	// Unreal => SDK
	AdvViz::SDK::double3 PositionFromClient(const AdvViz::SDK::double3& v) override
	{
		AdvViz::SDK::double3 r = {
			(v[0] - translation_[0]) / unitScale_,
			(-v[1] - translation_[1]) / unitScale_,
			(v[2] - translation_[2]) / unitScale_,
		};
		return r;
	} 

	// SDK => Unreal
	AdvViz::SDK::double3 PositionToClient(const AdvViz::SDK::double3& v) override
	{
		AdvViz::SDK::double3 r = {
			v[0] * unitScale_ + translation_[0],
			-(v[1] * unitScale_ + translation_[1]),
			v[2] * unitScale_ + translation_[2],
		};
		return r;
	}

	AdvViz::SDK::dmat4x4 MatrixFromClient(const AdvViz::SDK::dmat4x4& m) override
	{
		return m;
	}
	AdvViz::SDK::dmat4x4 MatrixToClient(const AdvViz::SDK::dmat4x4& m) override
	{
		return m;
	}

	AdvViz::SDK::double4 translation_;
	double unitScale_ = 100.; // 100.0 => SDK is in meters
};
#endif

struct AITwinKeyframePath::FImpl
{
	float time_ = 0.0f;
	float nextTimeStep;
	std::shared_ptr<AdvViz::SDK::IKeyframeAnimator> keyframeAnimator;
	ALevelBounds* LevelBounds_ = nullptr;
	std::shared_ptr<FSceneViewFamilyContext> ViewFamily_ = nullptr;
	std::vector<AdvViz::SDK::BoundingBox> boundingBoxes_;

	FVector4 prevViewLoc_ = FVector4(FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX);
	FVector4 prevViewDir_ = FVector4(FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX);
	FMatrix prevFrustrumToWorld_;

};

AITwinKeyframePath::AITwinKeyframePath() : Impl(MakePimpl<FImpl>())
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
}

FSceneView* AITwinKeyframePath::GetSceneView()
{
	//reference: https://forums.unrealengine.com/t/perform-frustum-check/287524/6
	ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();
	if (LocalPlayer != nullptr && LocalPlayer->ViewportClient != nullptr && LocalPlayer->ViewportClient->Viewport)
	{
		Impl->ViewFamily_.reset(new FSceneViewFamilyContext(FSceneViewFamily::ConstructionValues(
			LocalPlayer->ViewportClient->Viewport,
			GetWorld()->Scene,
			LocalPlayer->ViewportClient->EngineShowFlags)
			.SetRealtimeUpdate(true)));

		FVector ViewLocation;
		FRotator ViewRotation;
		FSceneView* SceneView = LocalPlayer->CalcSceneView(Impl->ViewFamily_.get(), ViewLocation, ViewRotation, LocalPlayer->ViewportClient->Viewport);
		//note: SceneView is hold by ViewFamily_
		return SceneView;
	}

	return {};
}

FBox CalculateLevelBounds(ULevel* InLevel)
{
	FBox LevelBbox;
	if (InLevel)
	{
		// Iterate over all level actors
		for (int32 ActorIndex = 0; ActorIndex < InLevel->Actors.Num(); ++ActorIndex)
		{
			AActor* Actor = InLevel->Actors[ActorIndex];
			if (Actor && Actor->IsLevelBoundsRelevant())
			{
				FString AName = Actor->GetName();
				if (!AName.Contains(TEXT("Cesium3DTileset")))
					continue;
				// Sum up components bounding boxes
				FBox ActorBox = Actor->GetComponentsBoundingBox(true);
				if (ActorBox.IsValid)
				{
					LevelBbox += ActorBox;
				}
			}
		}
	}
	return LevelBbox;
}

inline double RayPlaneIntersection(const FVector& RayOrigin, const FVector& RayDirection, const FPlane& Plane)
{
	const FVector PlaneNormal = FVector(Plane.X, Plane.Y, Plane.Z);
	const FVector PlaneOrigin = PlaneNormal * Plane.W;
	const double Distance = FVector::DotProduct((PlaneOrigin - RayOrigin), PlaneNormal) / FVector::DotProduct(RayDirection, PlaneNormal);
	return Distance;
}

inline void GetNearVertices(const FMatrix& FrustumToWorld, std::array<FVector, 5> &Vertices)
{
	int counter = 0;
	for (uint32 Y = 0; Y < 2; Y++)
	{
		for (uint32 X = 0; X < 2; X++)
		{
			FVector4 UnprojectedVertex = FrustumToWorld.TransformFVector4(
				FVector4(
					(X ? -1.0f : 1.0f),
					(Y ? -1.0f : 1.0f),
					1.0f,
					1.0f
				)
			);
			Vertices[counter] = UnprojectedVertex / UnprojectedVertex.W;
			counter++;
		}
	}

	FVector4 UnprojectedVertex = FrustumToWorld.TransformFVector4(
		FVector4(0.0f,0.0f,1.0f,1.0f)
	);
	Vertices[counter] = UnprojectedVertex / UnprojectedVertex.W;
	counter++;
}

inline FVector4 GetMainAxis(const FVector4& v)
{
	FVector4 ret;
	if (std::fabs(v.X) > std::fabs(v.Y))
	{
		if (std::fabs(v.X) > std::fabs(v.Z))
			ret = FVector4(v.X ? 1.f : -1.f, 0.0f, 0.0f);
		else
			ret = FVector4(0.0f, 0.0f, v.Z ? 1.f : -1.f);
	}
	else
	{
		if (std::fabs(v.Y) > std::fabs(v.Z))
			ret = FVector4(0.0f, v.Y ? 1.f : -1.f, 0.0f);
		else
			ret = FVector4(0.0f, 0.0f, v.Z ? 1.f : -1.f);
	}
	return ret;
}

template<typename T>
inline UE::Math::TVector<T> RayPlaneIntersection(const UE::Math::TVector<T>& RayOrigin, const UE::Math::TVector<T>& RayDirection, const UE::Math::TPlane<T>& Plane, T &Distance)
{
	using TVector = UE::Math::TVector<T>;
	const TVector PlaneNormal = TVector(Plane.X, Plane.Y, Plane.Z);
	const TVector PlaneOrigin = PlaneNormal * Plane.W;

	Distance = TVector::DotProduct((PlaneOrigin - RayOrigin), PlaneNormal) / TVector::DotProduct(RayDirection, PlaneNormal);
	return RayOrigin + RayDirection * Distance;
}


void AITwinKeyframePath::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if(!FreezeTime)
		Impl->time_ += DeltaTime;

	if (Impl->time_ > LoopTime)
	{
		Impl->time_ = 0.0f;
		Impl->keyframeAnimator->OnResetTime();
	}

	if (GEngine && DisplayInfo)
	{
		static const int key = 0x4c2ce7ec;
		GEngine->AddOnScreenDebugMessage(key, 5.f, FColor::White, *FString::Printf(TEXT("KeyframeAnimation time: %0.3fs"), Impl->time_));
	}

	if (Impl->keyframeAnimator)
	{
		// retrieve visible path
		Impl->boundingBoxes_.clear();

		FBox LevelBbox = CalculateLevelBounds(GetLevel()); //Impl->LevelBounds_->GetComponentsBoundingBox();
		const FVector LevelBboxCenter = LevelBbox.GetCenter();
		const FVector LevelBboxExtent = LevelBbox.GetExtent();
		LevelBbox += LevelBboxCenter + FVector(1000000.f, 1000000.f, 0.f); // 10Km min, the extend is more or less the iModel extend. We want to allow animation outside - vehicles traffic.
		LevelBbox += LevelBboxCenter + FVector(-1000000.f, -1000000.f, 0.f);

		FSceneView* sceneView(GetSceneView());
		FBox viewBox;
		bool bCameraMove = false;

		if (sceneView)
		{
			FVector4 &viewLoc = Impl->prevViewLoc_;
			FVector4 &viewDir = Impl->prevViewDir_;
			FMatrix &frustrumToWorld = Impl->prevFrustrumToWorld_;
			if (!CameraFreeze)
			{
				bCameraMove = !sceneView->ViewLocation.Equals(viewLoc, 10.f * 100.0f);
				bCameraMove = bCameraMove || !sceneView->GetViewDirection().Equals(viewDir, 0.01f);
				viewLoc = sceneView->ViewLocation;
				viewDir = sceneView->GetViewDirection();
				frustrumToWorld = sceneView->ViewMatrices.GetInvViewProjectionMatrix();
			}

			std::array<FVector, 5> nearVertices;
			GetNearVertices(frustrumToWorld, nearVertices);
			std::array<FVector, 5> Directions;
			for (int i = 0; i < 5; ++i)
				Directions[i] = (nearVertices[i] - viewLoc).GetSafeNormal();

			if (CameraFreeze && DisplayBBox)
				for (int i = 0; i < 5; ++i)
					DrawDebugLine(GetWorld(), viewLoc, viewLoc + Directions[i] * 1000.0f, FColor::Yellow, false, -1.f, 0, 100.f);

			static const float distances[] = {
				10.0f, 30.0f, 100.f, 200.f, 800.f, 1600.f// in meters
			};

			std::array<FVector, 5> previousPos;
			for (int i = 0; i < 5; ++i)
				previousPos[i] = viewLoc;

			FVector normal = GetMainAxis(viewDir);
			for (auto d : distances)
			{
				FBox box;
				FPlane plane(viewLoc + viewDir * d * 100.f, normal);
				for (int i = 0; i < 5; ++i)
				{
					box += previousPos[i];
					double distance = 0.0f;
					FVector v = RayPlaneIntersection(FVector(viewLoc), Directions[i], plane, distance);
					if (distance < 0.0f || distance > 1e10f)
						v = FVector(viewLoc) + Directions[i] * d * 100.f; 
					box += v;
					if (CameraFreeze && DisplayBBox)
						DrawDebugLine(GetWorld(), previousPos[i], v, FColor::Green, false, -1.f, 0, 100.f);
					previousPos[i] = v;
				}
				if (DisplayBBox)
					DrawDebugBox(GetWorld(), box.GetCenter(), box.GetExtent(), FColor::Blue, false, -1.f, 0, 100.f);
				box = box.Overlap(LevelBbox);
				if (DisplayBBox)
					DrawDebugBox(GetWorld(), box.GetCenter(), box.GetExtent(), FColor::Red, false, -1.f, 0, 100.f);
				if (box.IsValid)
				{
					AdvViz::SDK::BoundingBox bb;
					bb.min = AdvViz::SDK::double3{ box.Min.X, box.Min.Y, box.Min.Z };
					bb.max = AdvViz::SDK::double3{ box.Max.X, box.Max.Y, box.Max.Z };
					Impl->boundingBoxes_.push_back(bb);
				}
			}
		}

		Impl->keyframeAnimator->Process(Impl->time_, Impl->boundingBoxes_, bCameraMove);
	}



}

void AITwinKeyframePath::BeginPlay()
{
	Super::BeginPlay();
	
	AActor *act = UGameplayStatics::GetActorOfClass(GetWorld(), ALevelBounds::StaticClass());
	if (!act)
		act = GetWorld()->SpawnActor<ALevelBounds>();
	Impl->LevelBounds_ = static_cast<ALevelBounds*>(act);
}

void AITwinKeyframePath::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void AITwinKeyframePath::SetKeyframeAnimator(std::shared_ptr<AdvViz::SDK::IKeyframeAnimator> &keyframeAnimator)
{
	Impl->keyframeAnimator = keyframeAnimator;

#ifdef VISSIM_DEMO
	auto trs = std::make_shared<MyTransform>();
// hardcoded values for the demo, should be remove
	trs->translation_[0] = 1200;
	trs->translation_[1] = -143900;
	trs->translation_[2] = -517;

	if (keyframeAnimator->GetAnimation())
	{
		auto lock(keyframeAnimator->GetAnimation()->GetAutoLock());
		lock.Get().SetGCSTransform(trs);
	}
#endif
}
