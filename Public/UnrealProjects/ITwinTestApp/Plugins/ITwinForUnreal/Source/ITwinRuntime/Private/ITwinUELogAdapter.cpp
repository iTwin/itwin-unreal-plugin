/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinUELogAdapter.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include "ITwinUELogAdapter.h"

#if WITH_EDITOR

#include <ITwinServiceActor.h>


FITwinUELogAdapter::FITwinUELogAdapter(std::string name, Level sev)
	: Super(name, sev)
	, msgPrefix_(std::string("[") + name + "] ")
{

}

void FITwinUELogAdapter::DoLog(const std::string& msg, Level sev, const char* srcPath, const char* func, int line)
{
	std::string const fullMsg = msgPrefix_ + msg;
	switch (sev)
	{
	case Level::none:
		//UE_LOG(LogITwin, NoLogging, TEXT("%s"), UTF8_TO_TCHAR(fullMsg.c_str()));
		break;
	case Level::error:
		UE_LOG(LogITwin, Error, TEXT("%s"), UTF8_TO_TCHAR(fullMsg.c_str()));
		break;
	case Level::warning:
		UE_LOG(LogITwin, Warning, TEXT("%s"), UTF8_TO_TCHAR(fullMsg.c_str()));
		break;
	case Level::info:
		UE_LOG(LogITwin, Display, TEXT("%s"), UTF8_TO_TCHAR(fullMsg.c_str()));
		break;
	case Level::debug:
		UE_LOG(LogITwin, Log, TEXT("%s"), UTF8_TO_TCHAR(fullMsg.c_str()));
		break;
	default:
	case Level::verbose:
		UE_LOG(LogITwin, Verbose, TEXT("%s"), UTF8_TO_TCHAR(fullMsg.c_str()));
		break;
	}
}

#endif
