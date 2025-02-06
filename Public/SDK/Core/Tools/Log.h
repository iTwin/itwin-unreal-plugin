/*--------------------------------------------------------------------------------------+
|
|     $Source: Log.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "FactoryClass.h"
#include "Extension.h"
#include "Hash.h"
#include <string>
#include <memory>
#include <sstream>

namespace SDK::Core::Tools
{
	enum class Level
	{
		none = 0,
		error = 1,
		warning = 2,
		info = 3,
		debug = 4,
		verbose = 5
	};

	// Note: string are utf8

	class ILog: public Tools::Factory<ILog, std::string /*name*/, Level>
	{
	public:
		virtual void DoLog(const std::string& msg, Level sev, const char* srcPath, const char* func, int line) = 0;
		virtual bool Enabled(Level lev) const = 0;
		// set new level and return previous level
		virtual Level SetLevel(Level) = 0;
		virtual Level GetLevel() const = 0;
	};

	typedef std::shared_ptr<ILog> ILogPtr;

	class Log : public Tools::ExtensionSupport, public ILog
	{
	public:
		Log(std::string name, Level sev);

		void DoLog(const std::string &msg, Level sev, const char* srcPath, const char* func, int line) override;
		bool Enabled(Level lev) const override;
		Level SetLevel(Level) override;
		Level GetLevel() const override;

	private:
		std::string name_;
		Level level_;
	};

	void InitLog(std::string const& logBasename);
	void CreateLogChannel(const char* name, Level level = Level::info);
	// Initialize the channels used inside the Visualization library.
	void CreateAdvVizLogChannels();

	ILogPtr GetLog(const std::uint64_t channel, const char* name);
}

#define BE_GETLOG(channel) (SDK::Core::Tools::GetLog(GenHashCT(channel), channel))

#define BE_LOG(lev, channel, message) { \
	static auto l_log = BE_GETLOG(channel); \
	if(l_log && l_log->Enabled(lev)) { \
		std::ostringstream log_stream; \
		log_stream << message; \
		l_log->DoLog(log_stream.str(), lev, __FILE__, __FUNCTION__, __LINE__); } \
	}

#define BE_LOGV(channel, message) BE_LOG(SDK::Core::Tools::Level::verbose, channel, message)
#define BE_LOGD(channel, message) BE_LOG(SDK::Core::Tools::Level::debug, channel, message)
#define BE_LOGI(channel, message) BE_LOG(SDK::Core::Tools::Level::info, channel, message)
#define BE_LOGW(channel, message) BE_LOG(SDK::Core::Tools::Level::warning, channel, message)
#define BE_LOGE(channel, message) BE_LOG(SDK::Core::Tools::Level::error, channel, message)

