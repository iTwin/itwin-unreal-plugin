/*--------------------------------------------------------------------------------------+
|
|     $Source: Log.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

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

#ifdef _WIN32
#include "plog/Appenders/DebugOutputAppender.h"
namespace SDK::Core::Tools::Internal
{
	template<int id>
	void AddVSDebugOutput(plog::Logger<id> &logger)
	{
		static plog::DebugOutputAppender<plog::TxtFormatter> debugOutAppender;
		logger.addAppender(&debugOutAppender);
	}
}
#else
namespace SDK::Core::Tools::Internal
{
	template<int id>
	void AddVSDebugOutput(plog::Logger<id>&){}
}
#endif 

namespace SDK::Core::Tools
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
		(*plog::get<PLOG_DEFAULT_INSTANCE_ID>()) += plog::Record(sev, func, line, srcPath, nullptr, PLOG_DEFAULT_INSTANCE_ID).ref() << "[" << name_ << "] " << msg;
	}

	template<>
	std::function<std::shared_ptr<ILog>(std::string, Level level)> Tools::Factory<ILog, std::string, Level>::newFct_ = [](std::string s, Level level) {
		std::shared_ptr<ILog> p(static_cast<ILog*>(new Log(s, level)));
		return p;
		};

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
		static bool logInitialised = false;
		if (!logInitialised)
		{
			logInitialised = true;
			std::filesystem::path tmpPath = std::filesystem::temp_directory_path();
			tmpPath /= "ITwinAdvViz/Logs";
			std::error_code ec;
			if (!std::filesystem::is_directory(tmpPath, ec))
			{
				std::filesystem::create_directories(tmpPath, ec);
			}
			tmpPath /= logBasename;
			static plog::RollingFileAppender<plog::TxtFormatter> fileAppender((const char*)tmpPath.u8string().c_str(),
				512 * 1024 /* max_fileSize*/,
				5 /* max_files*/);
			static plog::ConsoleAppender<plog::TxtFormatter> consoleAppender;

			auto &appender = plog::init(plog::verbose, &consoleAppender).addAppender(&fileAppender);
			Internal::AddVSDebugOutput(appender);
		}
	}

	static RWLockableObject<std::unordered_map<std::uint64_t, ILogPtr>, std::shared_mutex> g_logMap;

	ILogPtr GetLog(const std::uint64_t channel, const char* name)
	{
		{
			auto logMap1(g_logMap.GetRAutoLock());
			auto& logMap = logMap1.Get();

			auto it = logMap.find(channel);
			if (it != logMap.end())
				return it->second;
		}
		BE_ISSUE("Channel not created:", name);
		return {};
	}

	void CreateLogChannel(const char* channel, Level level)
	{
		auto logMap1(g_logMap.GetAutoLock());
		auto& logMap = logMap1.Get();

		auto hash = GenHash(channel);
		auto it = logMap.find(hash);
		if (it == logMap.end())
			logMap[hash] = ILog::New(channel, level);
	}


}

