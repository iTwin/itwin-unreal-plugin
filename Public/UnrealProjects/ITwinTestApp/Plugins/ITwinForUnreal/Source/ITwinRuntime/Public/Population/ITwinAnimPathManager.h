/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinAnimPathManager.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Tools/Tools.h>
#	include <SDK/Core/Visualization/Instance.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

#include <GameFramework/Actor.h>
#include <ITwinAnimPathManager.generated.h>

class AITwinSplineHelper;
class AITwinPopulation;
namespace AdvViz::SDK
{
	class IAnimationPathInfo;
	class IPathAnimator;
}
class BakedKeyFrames;

typedef std::shared_ptr<AdvViz::SDK::IAnimationPathInfo> SharedPathInfo;
typedef std::shared_ptr<AdvViz::SDK::IPathAnimator> SharedPathAnimator;
typedef std::shared_ptr<AdvViz::SDK::IInstance> SharedInstance;

class InstanceWithSplinePathExt : public AdvViz::SDK::Tools::Extension, public AdvViz::SDK::Tools::TypeId<InstanceWithSplinePathExt>, public std::enable_shared_from_this<InstanceWithSplinePathExt>
{
public:
	InstanceWithSplinePathExt(const SharedPathInfo& info, AITwinPopulation* Population, int32 InstanceIdx)
		: pathInfo_(info), Population_(Population), InstanceIdx_(InstanceIdx)
	{}

	using AdvViz::SDK::Tools::TypeId<InstanceWithSplinePathExt>::GetTypeId;

	SharedPathInfo pathInfo_;
	std::shared_ptr<BakedKeyFrames> keyFrames_;
	AITwinPopulation* Population_ = nullptr;
	int32 InstanceIdx_ = -1;
	float curTime_ = 0.f;

	void UpdateInstance(float DeltaTime);
	void SetBakedKeyFramesPtr(const std::shared_ptr<BakedKeyFrames>& keyFrames){ keyFrames_ = keyFrames; }
};


UCLASS()
class ITWINRUNTIME_API AITwinAnimPathManager : public AActor
{
	GENERATED_BODY()
protected:
	virtual void BeginPlay() override;

public:	
	AITwinAnimPathManager();
	virtual void Tick(float DeltaTime) override;

	void PlayAnimation(bool bPLay);

	void MarkForUpdate(AITwinPopulation* Population, int32 InstanceIdx);

	SharedPathInfo GetAnimPathInfo(AITwinPopulation* Population, int32 InstanceIdx);
	SharedPathInfo AddNewAnimPathInfo(AITwinPopulation* Population, int32 InstanceIdx, const AITwinSplineHelper* AnimSpline);
	void RemoveAnimPathInfo(AITwinPopulation* Population, int32 InstanceIdx);

	//AITwinSplineHelper* GetSpline(AITwinPopulation* Population, int32 InstanceIdx);
	//void SetSpline(AITwinPopulation* Population, int32 InstanceIdx, AITwinSplineHelper* Spline);

	void SetSpeed(AITwinPopulation* Population, int32 InstanceIdx, float InSpeed);
	float GetSpeed(AITwinPopulation* Population, int32 InstanceIdx);
	void SetLooping(AITwinPopulation* Population, int32 InstanceIdx, bool InEnable);
	bool IsLooping(AITwinPopulation* Population, int32 InstanceIdx);
	void SetEnabled(AITwinPopulation* Population, int32 InstanceIdx, bool InEnable);
	bool IsEnabled(AITwinPopulation* Population, int32 InstanceIdx);

	void SetPathAnimator(const SharedPathAnimator &InPathAnimator);

private:
	class FImpl;
	TPimplPtr<FImpl> Impl;
};
