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

		/// Tells the router what to do with the route after a request was handled.
		enum class ERouteDisposition : uint8_t
		{
			/// The request was not the one we are waiting for (or more are expected): the route
			/// must stay bound, and the listener must keep running.
			KeepBound,

			/// The exchange is concluded: the route must be unbound and the listener stopped.
			Unbind
		};

		/// Handles one request received on the bound route.
		/// @param queryParams   The query parameters of the received request.
		/// @param outHtmlText   The HTML page to return to the caller (typically a web browser).
		/// @return Whether the route should be unbound now - see ERouteDisposition.
		using RequestHandlerCallback = std::function<ERouteDisposition(std::map<std::string, std::string> const& queryParams, std::string& outHtmlText)>;

		struct ADVVIZ_LINK RouteHandle
		{
			virtual ~RouteHandle();
			/// Returns true as long as the route is actually bound. Becomes false once the
			/// implementation has unbound it.
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

		/**
		 * Releases a route previously bound by BindRoute, without waiting for any request to
		 * come in. Typically used to cancel a pending authorization.
		 * The handle remembers everything needed to unbind (port included), so the caller does
		 * not have to keep track of the binding parameters.
		 * On success, routeHandle->IsValid() returns false afterwards.
		 * @param  routeHandle  The handle filled by BindRoute.
		 * @return True if the route was bound and has been released, false otherwise (including
		 *         when it had already been released by the request handler).
		 */
		virtual bool UnbindRoute(RouteHandlePtr const& routeHandle) const = 0;
	};

	template<>
	ADVVIZ_LINK Tools::Factory<IHttpRouter>::Globals::Globals();

}
