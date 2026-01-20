/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinMatMLPredictionEnums.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#ifndef SDK_CPPMODULES
#	ifndef MODULE_EXPORT
#		define MODULE_EXPORT
#	endif // !MODULE_EXPORT
#endif


MODULE_EXPORT namespace AdvViz::SDK
{

	/// Should be synchronized with EITwinMaterialPredictionStatus! (in the future, we may find a way to do
	/// it automatically...)
	enum class EITwinMatMLPredictionStatus : uint8_t
	{
		Unknown,
		NoAuth,
		InProgress,
		Failed,
		Complete,
		Validated, /* ie. validated by a human person */
	};

}
