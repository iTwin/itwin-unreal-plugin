/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAnimPathManager.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Population/ITwinAnimPathManager.h>
#include <Population/ITwinPopulationTool.h>
#include <Population/ITwinKeyframePath.h>
#include <Population/ITwinPopulation.h>
#include <Population/ITwinPopulationWithPathExt.h>
#include <Spline/ITwinSplineHelper.h>
#include <BeUtils/SplineSampling/SplineSampling.h>
#include <Components/SplineComponent.h>
#include <ITwinIModel.h>
#include <ITwinRealityData.h>
#include <EngineUtils.h>

#include <set>
#include <unordered_map>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Visualization/Instance.h>
#	include <SDK/Core/Visualization/InstancesGroup.h>
#	include <SDK/Core/Visualization/InstancesManager.h>
#   include "SDK/Core/Visualization/KeyframeAnimator.h"
#   include "SDK/Core/Visualization/PathAnimation.h"
#	include <SDK/Core/Visualization/Spline.h>
#	include <SDK/Core/Tools/Log.h>
#	include <SDK/Core/Tools/TypeId.h>
#	include <SDK/Core/Tools/Extension.h>
#include <Compil/AfterNonUnrealIncludes.h>


namespace ITwin
{
	bool FindHeight(UWorld* World, const FVector& InPos, float& OutHeight, FVector& OutNormal)
	{
		// Raycast from above to below the point
		FVector Start = InPos + FVector(0, 0, 1000); // start high above
		FVector End = InPos - FVector(0, 0, 10000); // cast far below

		FHitResult HitResult;
		FCollisionQueryParams Params(NAME_None, false, nullptr);
		Params.bReturnPhysicalMaterial = false;

		bool bHit = World->LineTraceSingleByChannel(
			HitResult,
			Start,
			End,
			ECC_Visibility, // or create a custom channel if needed
			Params
		);

#if 0//WITH_EDITOR
		// Optional: visualize the trace in editor
		DrawDebugLine(World, Start, End, FColor::Green, false, 2.0f, 0, 1.0f);
		if (bHit)
		{
			DrawDebugPoint(World, HitResult.ImpactPoint, 12.0f, FColor::Red, false, 2.0f);
		}
#endif

		if (bHit)
		{
			// Using GetOwner() because the hit actor is actually the cesium tileset
			AActor* HitTilesetOwner = nullptr;
			if (HitResult.HasValidHitObjectHandle())
				if (AActor* HitTileset = HitResult.GetActor())
					HitTilesetOwner = HitTileset->GetOwner(); // may be null or sth else than an iModel of course
			if (Cast<AITwinIModel>(HitTilesetOwner) || Cast<AITwinRealityData>(HitTilesetOwner))
			{
				OutHeight = HitResult.ImpactPoint.Z;
				OutNormal = HitResult.ImpactNormal.GetSafeNormal();
				return true;
			}
		}

		// Default fallback if nothing was hit
		OutHeight = InPos.Z;
		OutNormal = FVector::UpVector;
		return false;
	}
/*
	class AnimPath
	{
		public:
			AnimPath(){}
			TObjectPtr<AITwinSplineHelper> Spline_;
			TObjectPtr<AITwinKeyframePath> KeyFrames_;
			TObjectPtr<AITwinPopulation> Population_; // TODO: std::unordered_map<std::pair<AITwinPopulation*,int32>, params per instance> or create extension in population
			int32 InstanceIndex_ = -1;
			std::vector<glm::dvec3> Positions_;
			TArray<FTransform> BakedKeyframes_;
			float fTotalLength_ = 0.f; //in cm
			float fTotalTime_ = 0.f;
			float fDeltaTime_ = 1/60.f;
			float fCurTime_ = 0.f;
			//TObjectPtr<AdvViz::SDK::IInstance> Instance_;
			//TArray<AdvViz::SDK::IInstance> Instances_;
			FTransform InitTransform_;
			float fSpeed_ = 1388.9f; //in cm/s (=50km/h)
			float fOffsetX = 0.f;
			float fOffsetY = 0.f;
			bool bLoop_ = true;
			bool bEnabled_ = false;

			int32 GetKeyframeIndex(float Time)
			{
				if (BakedKeyframes_.Num() == 0)
					return -1;
				int32 Index = FMath::FloorToInt(Time / fDeltaTime_);
				return FMath::Clamp(Index, 0, BakedKeyframes_.Num() - 2); // -2 to allow interpolation with next frame
			}

			void BakeSpline(UWorld* World)
			{
				BakedKeyframes_.Empty();

				if (!Population_ || !Spline_ || fSpeed_ <= 0.0f || fDeltaTime_ <= 0.0f)
					return;

				auto UESpline = Spline_->GetSplineComponent();

				fTotalLength_ = UESpline->GetSplineLength(); // in cm
				BE_LOGI("App", "Processing animation spline of length " << fTotalLength_);
				if (fTotalLength_ < 0.01f)
					return;
				fTotalTime_ = fTotalLength_ / fSpeed_;

				InitTransform_ = Population_->GetInstanceTransform(InstanceIndex_);

				float DistanceStep = fSpeed_ * fDeltaTime_;
				float CurrentDistance = 0.0f;

				while (CurrentDistance <= fTotalLength_)
				{
					// Position along spline at given distance
					FVector SplineLocation = UESpline->GetLocationAtDistanceAlongSpline(CurrentDistance, ESplineCoordinateSpace::World);
					FVector SplineTangent = UESpline->GetTangentAtDistanceAlongSpline(CurrentDistance, ESplineCoordinateSpace::World).GetSafeNormal();

					// Ground height and normal
					float GroundZ = 0.0f;
					FVector GroundNormal = FVector::UpVector;

					FVector TracePosition = SplineLocation + FVector(0, 0, 500); // trace from above
					if (FindHeight(World, TracePosition, GroundZ, GroundNormal))
					{
						SplineLocation.Z = GroundZ;
					}

					// Build orientation
					FVector Forward = SplineTangent;
					FVector Up = GroundNormal;
					FVector Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();
					FVector AlignedForward = FVector::CrossProduct(Right, Up).GetSafeNormal();

					// World orientation based on spline and surface
					FMatrix Basis(AlignedForward, Right, Up, FVector::ZeroVector);
					FQuat WorldRotation = FQuat(Basis);

					// Apply alignment fix if needed (Y+ to X+ correction)
					FQuat AlignmentFix = FQuat(FVector::UpVector, PI / 2); // or +PI/2 depending on model
					FQuat FinalRotation = WorldRotation * AlignmentFix;

					FTransform Keyframe(FinalRotation, SplineLocation);
					BakedKeyframes_.Add(Keyframe);

					CurrentDistance += DistanceStep;
				}
			}

		private:
	};*/
}

class BakedKeyFrames
{
public:
	TArray<FTransform> transforms;
	float fTotalLength = 0.f;
	float fTotalTime = 0.f;
	float fDeltaTime = 1/60.f;
	bool isReady = false;
	
	void MarkForUpdate()
	{
		isReady = false;
		transforms.Empty();
	}
	
	bool NeedsUpdate() const
	{
		return !isReady;
	}

	void BakeSpline(UWorld* World, const AdvViz::SDK::RefID& SplineId, float InSpeed, float InDeltaTime = 1/60.f)
	{
		isReady = false;
		transforms.Empty();

		auto AnimSpline = [World, SplineId]() -> AITwinSplineHelper*
			{
				for (TActorIterator<AITwinSplineHelper> It(World); It; ++It)
					if (It->GetAVizSpline()->GetId() == SplineId) return *It;
				return nullptr;
			}();

		if (!AnimSpline || InSpeed <= 0.0f)
			return;

		auto UESpline = AnimSpline->GetSplineComponent();

		fTotalLength = UESpline->GetSplineLength(); // in cm
		BE_LOGI("App", "Processing animation spline of length " << fTotalLength);
		if (fTotalLength < 0.01f)
			return;
		fTotalTime = fTotalLength / InSpeed;

		//InitTransform_ = Population_->GetInstanceTransform(InstanceIndex_);
		fDeltaTime = InDeltaTime;
		float DistanceStep = InSpeed * fDeltaTime;
		float CurrentDistance = 0.0f;

		while (CurrentDistance <= fTotalLength)
		{
			// Position along spline at given distance
			FVector SplineLocation = UESpline->GetLocationAtDistanceAlongSpline(CurrentDistance, ESplineCoordinateSpace::World);
			FVector SplineTangent = UESpline->GetTangentAtDistanceAlongSpline(CurrentDistance, ESplineCoordinateSpace::World).GetSafeNormal();

			// Ground height and normal
			float GroundZ = 0.0f;
			FVector GroundNormal = FVector::UpVector;

			FVector TracePosition = SplineLocation + FVector(0, 0, 500); // trace from above
			if (ITwin::FindHeight(World, TracePosition, GroundZ, GroundNormal))
			{
				SplineLocation.Z = GroundZ;
			}

			// Build orientation
			FVector Forward = SplineTangent;
			FVector Up = GroundNormal;
			FVector Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();
			FVector AlignedForward = FVector::CrossProduct(Right, Up).GetSafeNormal();

			// World orientation based on spline and surface
			FMatrix Basis(AlignedForward, Right, Up, FVector::ZeroVector);
			FQuat WorldRotation = FQuat(Basis);

			// Apply alignment fix if needed (Y+ to X+ correction)
			FQuat AlignmentFix = FQuat(FVector::UpVector, PI / 2); // or +PI/2 depending on model
			FQuat FinalRotation = WorldRotation * AlignmentFix;

			FTransform Keyframe(FinalRotation, SplineLocation);
			transforms.Add(Keyframe);

			CurrentDistance += DistanceStep;
		}
		isReady = true;
	}

	int32 GetKeyframeIndex(float Time)
	{
		if (!isReady || transforms.Num() == 0)
			return -1;
		int32 Index = FMath::FloorToInt(Time / fDeltaTime);
		return FMath::Clamp(Index, 0, transforms.Num() - 2); // -2 to allow interpolation with next frame
	}

	FTransform GetTransform(float Time)
	{
		if (!isReady)
			return FTransform();
		auto idx = GetKeyframeIndex(Time);
		return (idx >= 0) ? transforms[idx] : FTransform();
	}
};


void InstanceWithSplinePathExt::UpdateInstance(float DeltaTime)
{
	if (!keyFrames_ || keyFrames_->NeedsUpdate())
		return;
	curTime_ += DeltaTime;
	if (curTime_ > keyFrames_->fTotalTime)
	{
		if (pathInfo_->IsLooping())
			curTime_ = 0.f;
		else
			return;
	}
	Population_->SetInstanceTransformUEOnly(InstanceIdx_, keyFrames_->GetTransform(curTime_));
}


class AITwinAnimPathManager::FImpl
{
public:
	AITwinAnimPathManager& Owner;
	//std::vector<std::shared_ptr<ITwin::AnimPath>> AnimPaths; // old
	std::shared_ptr<AdvViz::SDK::IPathAnimator> PathAnimatorPtr;
	std::unordered_map<AdvViz::SDK::RefID, std::shared_ptr<BakedKeyFrames>> TransformCacheMap; // spline Ref ID to FTransform (add xmY offsets for traffic path)
	float fDeltaTime = 1/60.f;

	FImpl(AITwinAnimPathManager& InOwner) : Owner(InOwner)
	{}

	SharedPathInfo GetAnimPathInfo(AITwinPopulation* Population, int32 InstanceIdx)
	{
		if (Population)
		{
			if (auto InstancePtr = Population->GetAVizInstance(InstanceIdx))
			{
				if (auto animPathExt = InstancePtr->GetExtension<InstanceWithSplinePathExt>())
				{
					return animPathExt->pathInfo_;
					//return PathAnimatorPtr->GetAnimationPathInfo(animPathExt->pathInfo_->GetId());
				}
			}
		}
		return SharedPathInfo();
	}
	
	void RemoveAnimPathInfo(AITwinPopulation* Population, int32 InstanceIdx)
	{
		if (Population)
		{
			if (auto InstancePtr = Population->GetAVizInstance(InstanceIdx))
			{
				if (auto animPathExt = InstancePtr->GetExtension<InstanceWithSplinePathExt>())
				{
					PathAnimatorPtr->RemoveAnimationPathInfo(animPathExt->pathInfo_->GetId());
					InstancePtr->RemoveAnimPathId();
				}
			}
		}
	}

	SharedPathInfo AddNewAnimPathInfo(AITwinPopulation* Population, int32 InstanceIdx, const AITwinSplineHelper* AnimSpline)
	{
		if (Population)
		{
			if (auto InstancePtr = Population->GetAVizInstance(InstanceIdx))
			{
				if (auto animPathExt = InstancePtr->GetExtension<InstanceWithSplinePathExt>())
					return animPathExt->pathInfo_;

				auto NewPathInfo = PathAnimatorPtr->AddAnimationPathInfo();
				NewPathInfo->SetSplineId(AnimSpline->GetAVizSpline()->GetId());
				NewPathInfo->SetIsEnabled(true);
				NewPathInfo->SetIsLooping(false);
				NewPathInfo->SetSpeed(1388.9f); //in cm/s (=50km/h)
				NewPathInfo->SetOffsetX(0);
				NewPathInfo->SetOffsetY(0);
				NewPathInfo->SetStartTime(0);

				InstancePtr->SetAnimPathId(NewPathInfo->GetId());

				std::shared_ptr<InstanceWithSplinePathExt> animPathExt = std::make_shared<InstanceWithSplinePathExt>(NewPathInfo, Population, InstanceIdx);
				InstancePtr->AddExtension(animPathExt);
				NewPathInfo->AddExtension(animPathExt);

				return NewPathInfo;
			}
		}
		return SharedPathInfo();
	}

	//std::shared_ptr<ITwin::AnimPath> GetAnimPath(AITwinPopulation* Population, int32 InstanceIdx)
	//
	//	for (auto ap : AnimPaths)
	//	{
	//		if (ap->Population_.Get() == Population && ap->InstanceIndex_ == InstanceIdx)
	//			return ap;
	//	}
	//	return nullptr;
	//}

	void MarkForUpdate(AITwinPopulation* Population, int32 InstanceIdx)
	{
		if (Population)
		{
			if (auto InstancePtr = Population->GetAVizInstance(InstanceIdx))
			{
				if (auto AnimPathExt = InstancePtr->GetExtension<InstanceWithSplinePathExt>())
				{
					AnimPathExt->keyFrames_->MarkForUpdate();
					AnimPathExt->curTime_ = 0.f;
				}
			}
		}
	}

	void BakeAll(bool bForceUpdate)
	{
		std::set<AdvViz::SDK::RefID> AnimPathIds;
		std::set<AdvViz::SDK::RefID> SplineIds;
		PathAnimatorPtr->GetAnimationPathIds(AnimPathIds);
		for (auto id : AnimPathIds)
		{
			if (auto AnimPathInfo = PathAnimatorPtr->GetAnimationPathInfo(id))
			{
				if (auto AnimPathExt = AnimPathInfo->GetExtension<InstanceWithSplinePathExt>())
				{
					auto SplineId = AnimPathInfo->GetSplineId();
					auto it = TransformCacheMap.find(SplineId);
					if (it == TransformCacheMap.end())
					{
						it = TransformCacheMap.emplace(SplineId, std::make_shared<BakedKeyFrames>()).first;
						AnimPathExt->SetBakedKeyFramesPtr(it->second);
					}
					else if (bForceUpdate && SplineIds.find(SplineId) == SplineIds.end())
						it->second->MarkForUpdate();

					SplineIds.insert(SplineId);

					if (it->second->NeedsUpdate())
						it->second->BakeSpline(Owner.GetWorld(), AnimPathInfo->GetSplineId(), AnimPathInfo->GetSpeed());

					AnimPathExt->curTime_ = 0.f;
				}
			}
		}
	}

	void UpdateAll(float DeltaTime)
	{
		std::set<AdvViz::SDK::RefID> AnimPathIds;
		PathAnimatorPtr->GetAnimationPathIds(AnimPathIds);
		for (auto id : AnimPathIds)
		{
			if (auto AnimPathInfo = PathAnimatorPtr->GetAnimationPathInfo(id))
			{
				if (auto animPathExt = AnimPathInfo->GetExtension<InstanceWithSplinePathExt>())
				{
					animPathExt->UpdateInstance(DeltaTime);
				}
			}
		}
	}

	//AITwinSplineHelper* GetSpline(AITwinPopulation* Population, int32 InstanceIdx)
	//{
	//	for (auto ap : AnimPaths)
	//	{
	//		if (ap->Population_.Get() == Population && ap->InstanceIndex_ == InstanceIdx)
	//			return ap->Spline_.Get();
	//	}
	//	return nullptr;
	//}

	//void SetSpline(AITwinPopulation* Population, int32 InstanceIdx, AITwinSplineHelper* Spline)
	//{
	//	auto AnimPathPtr = std::make_shared<ITwin::AnimPath>();
	//	AnimPathPtr->Population_ = Population;
	//	AnimPathPtr->InstanceIndex_ = InstanceIdx;
	//	AnimPathPtr->Spline_ = TObjectPtr<AITwinSplineHelper>(Spline);
	//	AnimPaths.push_back(AnimPathPtr);
	//}
};


AITwinAnimPathManager::AITwinAnimPathManager()
	: Impl(MakePimpl<FImpl>(*this))
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

void AITwinAnimPathManager::BeginPlay()
{
	Super::BeginPlay();

	SetActorTickEnabled(false);
}

void AITwinAnimPathManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	Impl->UpdateAll(DeltaTime);

	//for (auto anim : Impl->AnimPaths)
	//{
	//	if (!anim->bEnabled_ || anim->BakedKeyframes_.Num() == 0) //if (!anim->Spline_ || anim->fTotalTime_ <=0)
	//		continue;
	//	anim->fCurTime_ += DeltaTime;
	//	if (anim->fCurTime_ > anim->fTotalTime_)
	//		if (!anim->bLoop_)
	//			continue;
	//		else
	//			anim->fCurTime_ = 0.f;
	//	auto i = anim->GetKeyframeIndex(anim->fCurTime_);
	//	//auto UESpline = anim->Spline_->GetSplineComponent();
	//	//const float SplineTime = anim->fCurTime_ * anim->fTotalLength_ / anim->fTotalTime_;
	//	//auto const Pos_World = UESpline->GetLocationAtTime(SplineTime, ESplineCoordinateSpace::World);
	//	//FTransform tm;
	//	//tm.SetTranslation(Pos_World);
	//	anim->Population_->SetInstanceTransformUEOnly(anim->InstanceIndex_, anim->BakedKeyframes_[i]);
	//}
}

void AITwinAnimPathManager::MarkForUpdate(AITwinPopulation *Population, int32 InstanceIdx)
{
	Impl->MarkForUpdate(Population, InstanceIdx);
}

std::shared_ptr<AdvViz::SDK::IAnimationPathInfo> AITwinAnimPathManager::GetAnimPathInfo(AITwinPopulation *Population, int32 InstanceIdx)
{
	return Impl->GetAnimPathInfo(Population, InstanceIdx);
}

std::shared_ptr<AdvViz::SDK::IAnimationPathInfo> AITwinAnimPathManager::AddNewAnimPathInfo(AITwinPopulation* Population, int32 InstanceIdx, const AITwinSplineHelper* AnimSpline)
{
	return Impl->AddNewAnimPathInfo(Population, InstanceIdx, AnimSpline);
}

void AITwinAnimPathManager::RemoveAnimPathInfo(AITwinPopulation* Population, int32 InstanceIdx)
{
	Impl->RemoveAnimPathInfo(Population, InstanceIdx);
}

//AITwinSplineHelper* AITwinAnimPathManager::GetSpline(AITwinPopulation *Population,int32 InstanceIdx)
//{
//	return Impl->GetSpline(Population, InstanceIdx);
//}
//
//void AITwinAnimPathManager::SetSpline(AITwinPopulation* Population, int32 InstanceIdx, AITwinSplineHelper* Spline)
//{
//	Impl->SetSpline(Population, InstanceIdx, Spline);
//}

void AITwinAnimPathManager::PlayAnimation(bool bPLay)
{
	if (bPLay)
		Impl->BakeAll(true);
	SetActorTickEnabled(bPLay);
}

void AITwinAnimPathManager::SetPathAnimator(const std::shared_ptr<AdvViz::SDK::IPathAnimator>& InPathAnimator)
{
	Impl->PathAnimatorPtr = InPathAnimator;
}

void AITwinAnimPathManager::SetSpeed(AITwinPopulation* Population, int32 InstanceIdx, float InSpeed)
{
	if (auto AnimPathPtr = Impl->GetAnimPathInfo(Population, InstanceIdx))
	{
		AnimPathPtr->SetSpeed(InSpeed);
	}
}

float AITwinAnimPathManager::GetSpeed(AITwinPopulation* Population, int32 InstanceIdx)
{
	if (auto AnimPathPtr = Impl->GetAnimPathInfo(Population, InstanceIdx))
	{
		return (float)AnimPathPtr->GetSpeed();
	}
	return 0.f;
}

void AITwinAnimPathManager::SetLooping(AITwinPopulation* Population, int32 InstanceIdx, bool InEnable)
{
	if (auto AnimPathPtr = Impl->GetAnimPathInfo(Population, InstanceIdx))
	{
		AnimPathPtr->SetIsLooping(InEnable);
	}
}

bool AITwinAnimPathManager::IsLooping(AITwinPopulation* Population, int32 InstanceIdx)
{
	if (auto AnimPathPtr = Impl->GetAnimPathInfo(Population, InstanceIdx))
	{
		return AnimPathPtr->IsLooping();
	}
	return false;
}

void AITwinAnimPathManager::SetEnabled(AITwinPopulation* Population, int32 InstanceIdx, bool InEnable)
{
	if (auto AnimPathPtr = Impl->GetAnimPathInfo(Population, InstanceIdx))
	{
		AnimPathPtr->SetIsEnabled(InEnable);
	}
}

bool AITwinAnimPathManager::IsEnabled(AITwinPopulation* Population, int32 InstanceIdx)
{
	if (auto AnimPathPtr = Impl->GetAnimPathInfo(Population, InstanceIdx))
	{
		return AnimPathPtr->IsEnabled();
	}
	return false;
}
