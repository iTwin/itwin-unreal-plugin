/*--------------------------------------------------------------------------------------+
|
|     $Source: RefID.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once


#ifndef SDK_CPPMODULES
#	include <string>
#	include <unordered_map>
#	ifndef MODULE_EXPORT
#		define MODULE_EXPORT
#	endif // !MODULE_EXPORT
#endif

MODULE_EXPORT namespace AdvViz::SDK
{
	// Key properties:
	// 
	// - A default initialized reference identifier is different from any
	//   identifier already existing.
	// - A copy of an identifier will compare equal to the original.
	// - An identifier read from a stream will not compare equal to any
	//   already existing identifier.
	// - If two identifier are equal, if they are written, then read from
	//   a stream, the two read identifier will still compare equal, but
	//   will not be equal to the original identifiers.
	class RefID
	{
	public:
		// Makes a unique identifier.
		RefID()
		: RefID(NextID())
		{}

		static constexpr uint64_t INVALID_ID = static_cast<uint64_t>(-1);

		static RefID Invalid() { return RefID(INVALID_ID); }

		using DBToIDMap = std::unordered_map<std::string, uint64_t>;

		//! Instantiates a reference ID from an identifier retrieved from the server.
		//! The map classIDMap should depend on the class of the identified item, as the db identifier is
		//! usually relative to a given table on a server.
		static RefID FromDBIdentifier(std::string const& strId, DBToIDMap& classIDMap);

		//! Variant of FromDBIdentifier returning an invalid RefID if the server identifier has never been
		//! met in the loading.
		static RefID FindFromDBIdentifier(std::string const& strId, DBToIDMap const& classIDMap);

		bool operator<(RefID const& other) const { return id_ < other.id_; }

		bool operator==(RefID const& other) const
		{
			// only -1 is always different
			return (id_ == INVALID_ID || other.id_ == INVALID_ID) ? false : id_ == other.id_;
		}
		bool operator!=(RefID const& other) const { return !(*this == other); }

		bool HasDBIdentifier() const { return !db_Identifier_.empty(); }
		std::string const& GetDBIdentifier() const { return db_Identifier_; }
		void SetDBIdentifier(std::string const& idOnServer);

		void Reset() { id_ = NextID(); }
		bool IsValid() const { return id_ != INVALID_ID; }

		// For Hash computations.
		uint64_t ID() const { return id_; }

		// Only useful when dealing with cross-lang conversions.
		static RefID FromUInt64(uint64_t id) { return RefID(id); }

	private:
		static uint64_t NextID();

		RefID(uint64_t id)
		: id_(id)
		{}

		/// Identifier valid in current session.
		uint64_t id_;
		/// Identifier in the persistence system.
		/// (such persistence is typically achieved through a database in a cloud service, hence the 'db' prefix)
		std::string db_Identifier_;
	};
}

inline std::size_t hash_value(AdvViz::SDK::RefID const& ref_id)
{
	return static_cast<size_t>(ref_id.ID());
}

namespace std
{
	template<>
	struct hash<AdvViz::SDK::RefID>
	{
		std::size_t operator()(AdvViz::SDK::RefID const& f) const
		{
			return hash_value(f);
		}
	};
}

