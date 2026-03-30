/*--------------------------------------------------------------------------------------+
|
|     $Source: TaskFinishMonitor.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/




#pragma once
#include <Core/Tools/Tools.h>

namespace AdvViz::SDK::Tools {

	template<typename TReturn>
	class TaskFinishMonitor
	{
	public:
		TaskFinishMonitor(std::function<void(TReturn const&)> onAllTasksFinished)
			: RemainingTasks(0), OnAllTasksFinished(onAllTasksFinished)
		{
		}
		void AddTask()
		{
			RemainingTasks.fetch_add(1, std::memory_order_relaxed);
		}
		void TaskFinished(TReturn const& ret)
		{
			if (RemainingTasks.fetch_sub(1, std::memory_order_acq_rel) == 1) //note:fetch_sub return old value
			{	// All tasks complete
				if (OnAllTasksFinished)
				{
					OnAllTasksFinished(ret);
				}
			}
		}

		TaskFinishMonitor(TaskFinishMonitor&&) = default;
		TaskFinishMonitor(const TaskFinishMonitor&) = default;
		TaskFinishMonitor& operator=(TaskFinishMonitor&&) = default;
		TaskFinishMonitor& operator=(const TaskFinishMonitor&) = default;

	private:
		std::atomic<size_t> RemainingTasks;
		std::function<void(TReturn const&)> OnAllTasksFinished;
	};
}