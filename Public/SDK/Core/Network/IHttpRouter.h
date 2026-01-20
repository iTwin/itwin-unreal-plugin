/*--------------------------------------------------------------------------------------+
|
|     $Source: IHttpRouter.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#pragma once

#	include <functional>
#	include <map>
#	include <memory>
#	include <string>
#	include <vector>
#include "Network_api.h"

#include <Core/Tools/Tools.h>

namespace AdvViz::SDK
{
	enum class ADVVIZ_LINK EVerb : uint8_t;

	/// Abstraction for Unreal's IHttpRouter
	class ADVVIZ_LINK IHttpRouter : public Tools::Factory<IHttpRouter>
	{
	public:
		IHttpRouter() = default;
		virtual ~IHttpRouter();

		using RequestHandlerCallback = std::function<void(std::map<std::string, std::string> const& queryParams, std::string& outHtmlText)>;

		struct ADVVIZ_LINK RouteHandle
		{
			virtual ~RouteHandle();
			virtual bool IsValid() const = 0;
		};

		using RouteHandlePtr = std::shared_ptr<RouteHandle>;
		virtual RouteHandlePtr MakeRouteHandler() const = 0;

		/**
		 * Binds the caller-supplied Uri to the caller-supplied handler
		*  @param  redirectUriEndpoint   The respective http path to bind
		*  @param  eVerb  The respective HTTP verbs to bind
		*  @param  requestHandlerCB    The caller-defined closure to execute when the binding is invoked
		*  @return  True on success, false otherwise.
		*/
		virtual bool BindRoute(
			RouteHandlePtr& routeHandle,
			int port, std::string const& redirectUriEndpoint, EVerb eVerb,
			RequestHandlerCallback const& requestHandlerCB) const = 0;
	};

	template<>
	ADVVIZ_LINK Tools::Factory<IHttpRouter>::Globals::Globals();

}
