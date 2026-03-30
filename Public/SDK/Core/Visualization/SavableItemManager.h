/*--------------------------------------------------------------------------------------+
|
|     $Source: SavableItemManager.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace AdvViz::SDK
{
	class Http;
	class ISavableItem;
	class RefID;
	class AsyncRequestGroupCallback;

	/// Manages the asynchronous saving/deletion of generic items on the server.
	class SavableItemManager
	{
	public:
		// Some structures used in I/O functions
		struct SJsonIds { std::vector<std::string> ids; };


		SavableItemManager();
		virtual ~SavableItemManager();

		std::shared_ptr<Http> const& GetHttp() const { return http_; }
		void SetHttp(std::shared_ptr<Http> const& http) { http_ = http; }

		virtual void OnItemDeletedOnDB(RefID const& deletedId) = 0;

	protected:
		std::shared_ptr< std::atomic_bool > isThisValid_;

	private:
		std::shared_ptr<Http> http_;
	};
}
