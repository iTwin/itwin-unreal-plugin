/*--------------------------------------------------------------------------------------+
|
|     $Source: UEAdvVizTask.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include "CoreMinimal.h"
#include "Tasks/Task.h"

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <Core/Tools/TaskManager.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

class FUETask : public AdvViz::SDK::Tools::Task, AdvViz::SDK::Tools::TypeId<FUETask>
{
public:
	FUETask() {};
	UE::Tasks::FTask Task_;

	bool IsCompleted() override  { return Task_.IsCompleted(); }
	void Wait() override { Task_.Wait(); }
};

class FUETaskManager : public AdvViz::SDK::Tools::TaskManager, AdvViz::SDK::Tools::TypeId<FUETaskManager>
{
public:
	FUETaskManager() {};
	static void Init()
	{
		AdvViz::SDK::Tools::TaskManager::SetNewFct([]() {
			return static_cast<AdvViz::SDK::Tools::TaskManager*>(new FUETaskManager);
			});
	}

	UE::Tasks::ETaskPriority GetUETaskPriority(EType type, EPriority priority)
	{
		switch (type)
		{
		case EType::background:
			{
				switch (priority)
				{
				case EPriority::low: return UE::Tasks::ETaskPriority::BackgroundLow;
				case EPriority::normal: return UE::Tasks::ETaskPriority::BackgroundNormal;
				case EPriority::high: return UE::Tasks::ETaskPriority::BackgroundHigh;
				}
				break;
			}
		case EType::foreground:
			{
				switch (priority)
				{
				case EPriority::low: return UE::Tasks::ETaskPriority::Normal; // there is no low
				case EPriority::normal: return UE::Tasks::ETaskPriority::Normal;
				case EPriority::high: return UE::Tasks::ETaskPriority::High;
				}
				break;
			}
		case EType::main:
			{
				return UE::Tasks::ETaskPriority::Normal;
			}
		}
		return UE::Tasks::ETaskPriority::Normal;
	}
	

	std::shared_ptr<AdvViz::SDK::Tools::ITask> AddTask(const std::function<void()>& fct, EType type, EPriority priority)
	{
		FUETask* p = new FUETask();
		p->Task_ = UE::Tasks::Launch(UE_SOURCE_LOCATION, [fct]() {fct();}, GetUETaskPriority(type, priority));
		return std::shared_ptr<AdvViz::SDK::Tools::ITask>(static_cast<AdvViz::SDK::Tools::ITask*>(p));
	}

	using AdvViz::SDK::Tools::TypeId<FUETaskManager>::GetTypeId;
	std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
	bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || FUETaskManager::IsTypeOf(i); }

};
