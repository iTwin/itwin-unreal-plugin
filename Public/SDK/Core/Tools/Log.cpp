/*--------------------------------------------------------------------------------------+
|
|     $Source: Log.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <catch2/catch_session.hpp>
#include <array>
#include <filesystem>
#include <shared_mutex>
#define PLOG_ENABLE_WCHAR_INPUT 1
#define PLOG_CHAR_IS_UTF8 1
#include "Log.h"
#include "plog/Log.h"
#include "plog/Init.h"
#include "plog/Appenders/ConsoleAppender.h"
#include "plog/Appenders/RollingFileAppender.h"
#include "plog/Formatters/TxtFormatter.h"

#include "LockableObject.h"
#include "Assert.h"
#include "CrashInfo.h"
#include "../Singleton/singleton.h"

#ifdef _WIN32
#include "plog/Appenders/DebugOutputAppender.h"
namespace AdvViz::SDK::Tools::Internal
{
	template<int id>
	void AddVSDebugOutput(plog::Logger<id> &logger)
	{
		static plog::DebugOutputAppender<plog::TxtFormatter> debugOutAppender;
		logger.addAppender(&debugOutAppender);
	}
}
#else
namespace AdvViz::SDK::Tools::Internal
{
	template<int id>
	void AddVSDebugOutput(plog::Logger<id>&){}
}
#endif 

namespace AdvViz::SDK::Tools::Internal
{ 
	struct LogGlobals
	{
		plog::Logger<PLOG_DEFAULT_INSTANCE_ID>* plog_;
		RWLockableObject<std::unordered_map<std::uint64_t, ILogPtr>, std::shared_mutex> logMap;
		std::atomic<bool> logInitialized = false;
		LogGlobals()
		{
			static plog::Logger<PLOG_DEFAULT_INSTANCE_ID> logger(plog::verbose); // should be ok because LogGlobals() is called only once
			plog_ = plog::get<PLOG_DEFAULT_INSTANCE_ID>();
		}
	};

	LogGlobals& GetLogGlobals()
	{
		return singleton<LogGlobals>();
	}
}

namespace AdvViz::SDK::Tools
{
	static std::array<plog::Severity, int(Level::verbose) + 1> g_LevelToSeverity = {
		plog::Severity::none,
		plog::Severity::error,
		plog::Severity::warning,
		plog::Severity::info,
		plog::Severity::debug,
		plog::Severity::verbose
	};

	void Log::DoLog(const std::string& msg, Level lev, const char* srcPath, const char* func, int line)
	{
		plog::Severity sev(g_LevelToSeverity[int(lev)]);
		(*Internal::GetLogGlobals().plog_) += plog::Record(sev, func, line, srcPath, nullptr, PLOG_DEFAULT_INSTANCE_ID).ref() << "[" << name_ << "] " << msg;
	}

	template<>
	Tools::Factory<ILog, std::string, Level>::Globals::Globals()
	{
		newFct_ = [](std::string s, Level level){
				return static_cast<ILog*>(new Log(s, level));
		};
	}

	template<>
	Tools::Factory<ILog, std::string, Level>::Globals& Tools::Factory<ILog, std::string, Level>::GetGlobals()
	{
		return singleton<Tools::Factory<ILog, std::string, Level>::Globals>();
	}

	Log::Log(std::string name, Level lev):name_(name), level_(lev)
	{
	}
	
	Level Log::SetLevel(Level lev)
	{
		Level prevLev = level_;
		level_ = lev;
		return prevLev;
	}

	Level Log::GetLevel() const
	{
		return level_;
	}

	bool Log::Enabled(Level lev) const
	{
		return lev <= level_;
	}


	void InitLog(std::string const& logBasename)
	{
		if (!Internal::GetLogGlobals().logInitialized)
		{
			std::filesystem::path tmpPath = std::filesystem::temp_directory_path();
			tmpPath = tmpPath / "ITwinAdvViz" / "Logs";
			std::error_code ec;
			if (!std::filesystem::is_directory(tmpPath, ec))
			{
				std::filesystem::create_directories(tmpPath, ec);
			}
			tmpPath /= logBasename;
			if (GetCrashInfo())
				GetCrashInfo()->AddInfo("AdvVizSdkLogPath", (const char*)tmpPath.u8string().c_str());
			static plog::RollingFileAppender<plog::TxtFormatter> fileAppender((const char*)tmpPath.u8string().c_str(),
				512 * 1024 /* max_fileSize*/,
				5 /* max_files*/);
			static plog::ConsoleAppender<plog::TxtFormatter> consoleAppender;

			auto& appender = Internal::GetLogGlobals().plog_->addAppender(&consoleAppender).addAppender(&fileAppender);
			Internal::AddVSDebugOutput(appender);
			Internal::GetLogGlobals().logInitialized = true;
			CreateLogChannel("AdvVizSDK", Level::info);
		}
	}

	bool IsLogInitialized()
	{
		return Internal::GetLogGlobals().logInitialized;
	}

	ILogPtr GetLog(const std::uint64_t channel, const char* name)
	{
		{
			auto logMap1(Internal::GetLogGlobals().logMap.GetRAutoLock());
			auto& logMap = logMap1.Get();

			auto it = logMap.find(channel);
			if (it != logMap.end())
				return it->second;
		}
		BE_ISSUE("Channel not created:", name); UNUSED(name);
		return {};
	}

	void CreateLogChannel(const char* channel, Level level)
	{
		auto logMap1(Internal::GetLogGlobals().logMap.GetAutoLock());
		auto& logMap = logMap1.Get();

		auto hash = GenHash(channel);
		auto it = logMap.find(hash);
		if (it == logMap.end())
			logMap[hash] = std::shared_ptr<ILog>(ILog::New(channel, level));
	}

	void CreateAdvVizLogChannels()
	{
		CreateLogChannel("ITwinAPI", Level::info);
		CreateLogChannel("ITwinScene", Level::info);
		CreateLogChannel("ITwinDecoration", Level::info);
		CreateLogChannel("ITwinMaterial", Level::info);
		CreateLogChannel("App", Level::info);
		CreateLogChannel("AppUI", Level::info);
		CreateLogChannel("Timeline", Level::info);
		CreateLogChannel("http", Level::info);
		CreateLogChannel("json", Level::info);
		CreateLogChannel("keyframeAnim", Level::info);
	}
}

