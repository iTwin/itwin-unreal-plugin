/*--------------------------------------------------------------------------------------+
|
|     $Source: IHttpRouter.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#pragma once

#ifndef SDK_CPPMODULES
#	include <functional>
#	include <map>
#	include <memory>
#	include <string>
#	include <vector>
#	ifndef MODULE_EXPORT
#		define MODULE_EXPORT
#	endif // !MODULE_EXPORT
#endif

#include <Core/Tools/Tools.h>

MODULE_EXPORT namespace SDK::Core
{
	enum class EVerb : uint8_t;

	/// Abstraction for Unreal's IHttpRouter
	class IHttpRouter : public Tools::Factory<IHttpRouter>
	{
	public:
		IHttpRouter() = default;
		virtual ~IHttpRouter();

		using RequestHandlerCallback = std::function<void(std::map<std::string, std::string> const& queryParams, std::string& outHtmlText)>;

		struct RouteHandle
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

}
