/*--------------------------------------------------------------------------------------+
|
|     $Source: TaskManager.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once
#include <Core/Tools/Tools.h>

namespace AdvViz::SDK::Tools {

	class ADVVIZ_LINK ITask : public Tools::Factory<ITask>, public Tools::ExtensionSupport
	{
	public:
		virtual bool IsCompleted() = 0;
		virtual void Wait() = 0;
	};
	
	class ADVVIZ_LINK Task : public ITask, public Tools::TypeId<Task>
	{
	public:
		// return always true
		bool IsCompleted() override;
		// nothing to wait
		void Wait() override;
	};

	class ADVVIZ_LINK ITaskManager : public Tools::Factory<ITaskManager>, public Tools::ExtensionSupport
	{
	public:
		enum class EType {
			background,
			foreground,
			main
		};
		enum class EPriority {
			low,
			normal,
			high
		};

		virtual std::shared_ptr<ITask> AddTask(const std::function<void()> &fct, EType type = EType::foreground, EPriority priority = EPriority::normal) = 0;
	};

	
	class ADVVIZ_LINK TaskManager : public ITaskManager, public Tools::TypeId<TaskManager>
	{
	public:
		TaskManager();
		~TaskManager();

		// need to be implement by host
		// current implementation does nothing, just run fct. 
		std::shared_ptr<ITask> AddTask(const std::function<void()>& fct, EType type = EType::foreground, EPriority priority = EPriority::normal) override;

		using Tools::TypeId<TaskManager>::GetTypeId;
		std::uint64_t GetDynTypeId() const override { return GetTypeId(); }
		bool IsTypeOf(std::uint64_t i) const override { return (i == GetTypeId()) || TaskManager::IsTypeOf(i); }

		class Impl;
		Impl& GetImpl();
		const Impl& GetImpl() const;
	private:
		const std::unique_ptr<Impl> impl_;
	};

	ADVVIZ_LINK ITaskManager& GetTaskManager();

	template<typename TContainer>
	inline void WaitTasks(TContainer& t)
	{
		for (auto& i : t)
			i->Wait();
	}

	template<typename TContainer>
	inline bool AreTasksCompleted(TContainer& t)
	{
		auto it = t.rbegin();
		auto itEnd = t.rend();
		for (;it !=  itEnd; ++it)
			if (!it->IsCompleted())
				return false;
		return true;
	}

}