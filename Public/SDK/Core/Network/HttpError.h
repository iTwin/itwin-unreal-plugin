/*--------------------------------------------------------------------------------------+
|
|     $Source: HttpError.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <string>

namespace AdvViz::SDK {

	/// Generic error for any Http request: contains a human-readable error and a potential status code.
	struct HttpError
	{
		/**
		 * @brief A human-readable explanation of what failed.
		 */
		std::string message = "";
		/**
		 * @brief Status code returned by the server, if any.
		 */
		long httpCode = -1;
	};

}
