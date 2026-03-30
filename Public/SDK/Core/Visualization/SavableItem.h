/*--------------------------------------------------------------------------------------+
|
|     $Source: SavableItem.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Core/Visualization/RefID.h>
#include <Core/Visualization/SaveStatus.h>

MODULE_EXPORT namespace AdvViz::SDK
{

	/// Interface for an item which can be saved asynchronously on the server, with callback executed in the
	/// main thread.
	class ISavableItemStatusHolder
	{
	public:
		virtual ~ISavableItemStatusHolder() {}

		virtual ESaveStatus GetSaveStatus() const = 0;
		virtual void SetSaveStatus(ESaveStatus status) = 0;

		inline bool ShouldSave() const { return GetSaveStatus() == ESaveStatus::ShouldSave; }
		inline void SetShouldSave(bool value) { SetSaveStatus(value ? ESaveStatus::ShouldSave : ESaveStatus::Done); }
		inline void InvalidateDB() { SetShouldSave(true); }

		inline void OnStartSave() {
			SetSaveStatus(ESaveStatus::InProgress);
		}
		inline void OnSaved() {
			// If the data has been invalidated since the request was started, keep it dirty: it will be
			// saved again afterwards.
			if (GetSaveStatus() == ESaveStatus::InProgress) {
				SetSaveStatus(ESaveStatus::Done);
			}
		}
	};

	/// Interface for an item which can be saved on the server and invalidated individually.
	class ISavableItem : public ISavableItemStatusHolder
	{
	public:
		virtual ~ISavableItem() {}

		// Unique id (at runtime), and possibly holds data base identifier.
		virtual const RefID& GetId() const = 0;
		virtual void SetId(const RefID& id) = 0;

		inline bool HasDBIdentifier() const { return GetId().HasDBIdentifier(); }
		inline std::string const& GetDBIdentifier() const { return GetId().GetDBIdentifier(); }

		/// Update only the identifier on DB (not the internal, unique ID).
		inline void SetDBIdentifier(std::string const& IDServer) {
			RefID refid = GetId();
			refid.SetDBIdentifier(IDServer);
			SetId(refid);
		}
	};

	class SavableItemWithoutID : public ISavableItem
	{
	public:
		ESaveStatus GetSaveStatus() const final { return saveStatus_; }
		void SetSaveStatus(ESaveStatus status) final { saveStatus_ = status; }

		const RefID& GetId() const override {
			BE_ISSUE("no RefID for this type");
			static const RefID refId;
			return refId;
		}
		void SetId(const RefID& /*id*/) override { BE_ISSUE("no RefID for this type"); }

	private:
		ESaveStatus saveStatus_ = ESaveStatus::NeverSaved;
	};

	class SavableItemWithID : public SavableItemWithoutID
	{
	public:
		const RefID& GetId() const final { return refId_; }
		void SetId(const RefID& id) override { refId_ = id; }

	private:
		RefID refId_;
	};

}
