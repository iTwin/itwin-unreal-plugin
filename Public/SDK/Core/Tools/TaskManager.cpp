/*--------------------------------------------------------------------------------------+
|
|     $Source: TaskManager.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#include "TaskManager.h"
#include "Core/Tools/FactoryClassInternalHelper.h"

namespace AdvViz::SDK::Tools {

	DEFINEFACTORYGLOBALS(Task);
	
	bool Task::IsCompleted()
	{
		return true;
	}

	void Task::Wait(){
		return;
	};

	class TaskManager::Impl {
	};

	DEFINEFACTORYGLOBALS(TaskManager);

	TaskManager::TaskManager():impl_(new Impl())
	{
	}

	TaskManager::~TaskManager()
	{
	}

	std::shared_ptr<ITask> TaskManager::AddTask(const std::function<void()>& fct, EType /*type*/, EPriority /*priority*/)
	{
		fct();
		return std::shared_ptr<ITask>(ITask::New());
	}

	TaskManager::Impl& TaskManager::GetImpl()
	{
		return *impl_;
	}

	const TaskManager::Impl& TaskManager::GetImpl() const
	{
		return *impl_;
	}

	struct ITaskManagerGlobals
	{
		ITaskManager* instance_;
	};

	ITaskManager& GetTaskManager()
	{
		ITaskManagerGlobals &p = singleton<ITaskManagerGlobals>();
		if (!p.instance_)
			p.instance_ = ITaskManager::New();
		return *p.instance_;
	}
	
}
