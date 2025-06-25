/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPopulationWithPathExt.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "ITwinPopulation.h"

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <SDK/Core/Tools/Tools.h>
#	include <SDK/Core/Visualization/KeyframeAnimation.h>
#include <Compil/AfterNonUnrealIncludes.h>

class FITwinPopulationWithPathExt : public AdvViz::SDK::Tools::Extension
							  , public AdvViz::SDK::Tools::TypeId<FITwinPopulationWithPathExt>
							  , public std::enable_shared_from_this<FITwinPopulationWithPathExt>
{
public:
	FITwinPopulationWithPathExt();
	~FITwinPopulationWithPathExt();

	void UpdatePopulationInstances();
	void InstanceToUpdateColor(size_t instIndex, FVector color);
	void InstanceToUpdateTransForm(size_t instIndex, const FTransform& Trans);

	AITwinPopulation* population_ = nullptr;

	// properties are separated for performance reason
	AdvViz::SDK::Tools::RWLockableObject<std::unordered_map<size_t/*instanceIndex*/, FTransform>, std::shared_mutex> instancesToUpdateTr_;
	AdvViz::SDK::Tools::RWLockableObject<std::unordered_map<size_t/*instanceIndex*/, FVector>, std::shared_mutex> instancesToUpdateColor_;
};

