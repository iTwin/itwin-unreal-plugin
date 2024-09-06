/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinEnvironment.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#ifndef SDK_CPPMODULES
	#ifndef MODULE_EXPORT
		#define MODULE_EXPORT
	#endif // !MODULE_EXPORT
#endif

MODULE_EXPORT namespace SDK::Core
{
	enum class EITwinEnvironment : uint8_t
	{
		Prod,
		QA,
		Dev,
		Invalid,
	};

	namespace ITwinServerEnvironment
	{
		inline std::string ToString(EITwinEnvironment value)
		{
			switch (value)
			{
			case EITwinEnvironment::Prod: return "Prod";
			case EITwinEnvironment::QA: return "QA";
			case EITwinEnvironment::Dev: return "Dev";
			case EITwinEnvironment::Invalid: break;
			}
			return "<Invalid>";
		}

		inline std::string GetUrlPrefix(EITwinEnvironment value)
		{
			switch (value)
			{
			case EITwinEnvironment::Prod: return "";
			case EITwinEnvironment::QA: return "qa-";
			case EITwinEnvironment::Dev: return "dev-";
			case EITwinEnvironment::Invalid: break;
			}
			return "";
		}
	}
}
