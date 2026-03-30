/*--------------------------------------------------------------------------------------+
|
|     $Source: SavableItemManager.inl $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Core/Visualization/SavableItemManager.h>
#include <Core/Visualization/AsyncHttp.inl>
#include <Core/Network/http.h>

namespace AdvViz::SDK
{
	template <typename TSavable>
	struct SavableItemJsonHelper
	{

	};

	template <typename TItemManager, typename TSavable>
	void TAsyncSaveItems(TItemManager& manager,
		std::string const& decorationUrl,
		std::vector< TSharedLockableDataPtr<TSavable> > const& items,
		std::shared_ptr<AsyncRequestGroupCallback> callbackPtr)
	{
		auto sep = decorationUrl.rfind('/');
		const std::string genericItemName = (sep == std::string::npos)
			? decorationUrl
			: decorationUrl.substr(sep + 1);

		using SJsonIds = SavableItemManager::SJsonIds;

		using TJsonItemVec = typename SavableItemJsonHelper<TSavable>::JsonVec;
		TJsonItemVec jInPost, jInPut;
		std::vector<RefID> newItemIds;
		std::vector<RefID> updatedItemIds;

		// Sort items for requests (addition/update)
		SavableItemJsonHelper<TSavable> jsonConverter;
		for (auto const& itemPtr : items)
		{
			auto item = itemPtr->GetAutoLock();
			if (!item->HasDBIdentifier())
			{
				jsonConverter.AppendItem(jInPost, *item);

				newItemIds.push_back(item->GetId());
				item->OnStartSave();
			}
			else if (item->ShouldSave())
			{
				jsonConverter.AppendItem(jInPut, *item);

				updatedItemIds.push_back(item->GetId());
				item->OnStartSave();
			}
		}

		// Post (new items)
		if (!newItemIds.empty())
		{
			AsyncPostJsonJBody<SJsonIds>(manager.GetHttp(), callbackPtr,
				[pManager = &manager /* valid because of validity test on callbackPtr */,
				 genericItemName,
				 newItemIds](long httpCode,
							 const Tools::TSharedLockableData<SJsonIds>& joutPtr)
			{
				const bool bSuccess = (httpCode == 200 || httpCode == 201);
				if (bSuccess)
				{
					auto unlockedJout = joutPtr->GetAutoLock();
					SJsonIds& jOutPost = unlockedJout.Get();
					if (newItemIds.size() == jOutPost.ids.size())
					{
						for (size_t i = 0; i < newItemIds.size(); ++i)
						{
							RefID itemId = newItemIds[i];
							// Update the DB identifier only.
							itemId.SetDBIdentifier(jOutPost.ids[i]);

							auto itemPtr = pManager->GetItemById(itemId);
							if (itemPtr)
							{
								auto item = itemPtr->GetAutoLock();
								item->SetId(itemId);
								item->OnSaved();
							}
						}
					}
					else
					{
						BE_ISSUE("mismatch count while saving", genericItemName,
							newItemIds.size(), jOutPost.ids.size());
					}
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Saving new " << genericItemName << " failed. "
						<< "Http status: " << httpCode);
				}
				return bSuccess;
			},
				decorationUrl,
				jInPost);
		}

		// Put (updated items)
		if (!updatedItemIds.empty())
		{
			struct SJsonItemOutUpd
			{
				int64_t numUpdated = 0;
			};
			AsyncPutJsonJBody<SJsonItemOutUpd>(manager.GetHttp(), callbackPtr,
				[pManager = &manager /* valid because of validity test on callbackPtr */, 
				 genericItemName,
				 updatedItemIds](long httpCode,
								 const Tools::TSharedLockableData<SJsonItemOutUpd>& joutPtr)
			{
				const bool bSuccess = (httpCode == 200 || httpCode == 201);
				if (bSuccess)
				{
					auto unlockedJout = joutPtr->GetAutoLock();
					SJsonItemOutUpd& jOutPut = unlockedJout.Get();
					if (updatedItemIds.size() == static_cast<size_t>(jOutPut.numUpdated))
					{
						for (RefID const& itemId : updatedItemIds)
						{
							auto itemPtr = pManager->GetItemById(itemId);
							if (itemPtr)
							{
								auto item = itemPtr->GetAutoLock();
								item->OnSaved();
							}
						}
					}
					else
					{
						BE_ISSUE("mismatch count while updating", genericItemName,
							updatedItemIds.size(), jOutPut.numUpdated);
					}
				}
				else
				{
					BE_LOGW("ITwinDecoration", "Updating " << genericItemName << " failed. "
						"Http status: " << httpCode);
				}
				return bSuccess;
			},
				decorationUrl,
				jInPut);
		}
	}


	template <typename TItemManager, typename TSavable>
	void TAsyncDeleteItems(TItemManager& manager,
		std::string const& decorationUrl,
		std::vector< TSharedLockableDataPtr<TSavable> >& removedItems,
		std::shared_ptr<AsyncRequestGroupCallback> callbackPtr)
	{
		auto sep = decorationUrl.rfind('/');
		const std::string genericItemName = (sep == std::string::npos)
			? decorationUrl
			: decorationUrl.substr(sep + 1);

		using SJsonIds = SavableItemManager::SJsonIds;

		SJsonIds jIn;
		std::vector<RefID> deletedItemIds;
		jIn.ids.reserve(removedItems.size());
		deletedItemIds.reserve(removedItems.size());
		for (auto const& itemPtr : removedItems)
		{
			auto item = itemPtr->GetRAutoLock();
			auto const& itemId = item->GetId();
			deletedItemIds.push_back(itemId);
			if (itemId.HasDBIdentifier())
				jIn.ids.push_back(itemId.GetDBIdentifier());
		}

		if (jIn.ids.empty())
		{
			removedItems.clear();
			return;
		}

		AsyncDeleteJsonNoOutput(manager.GetHttp(), callbackPtr,
			[pManager = &manager /* valid because of validity test on callbackPtr */,
			 genericItemName,
			 deletedItemIds](long httpCode)
		{
			const bool bSuccess = (httpCode == 200 || httpCode == 201 || httpCode == 204 /* No-Content*/);
			if (bSuccess)
			{
				for (RefID const& deletedId : deletedItemIds)
				{
					pManager->OnItemDeletedOnDB(deletedId);
				}
			}
			else
			{
				BE_LOGW("ITwinDecoration", "Deleting " << genericItemName << " failed. "
					<< "Http status: " << httpCode);
			}
			return bSuccess;
		},
			decorationUrl,
			jIn);
	}

}
