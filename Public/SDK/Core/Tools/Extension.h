/*--------------------------------------------------------------------------------------+
|
|     $Source: Extension.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <memory>
#include <unordered_map>
#include "fnv1ahash.h"

namespace SDK::Core::Tools
{
	// Extension base class
	// each extension class needs to define a typeid like this:
	// enum ETypeid : std::uint64_t {value = Tools::GenHash("MyExtension")};
	class Extension{
	public:
		virtual ~Extension() {}
	};

	class ExtensionSupport {
	public:
		template<typename T>
		const std::shared_ptr<T> GetExtension()
		{
			auto it = extension_.find(T::ETypeid::value);
			if (it != extension_.end())
				return std::static_pointer_cast<T>(it->second);
			return {};
		}

		template<typename T>
		void AddExtension(const std::shared_ptr<T>& extension)
		{
			extension_[T::ETypeid::value] = extension;
		}

		template<typename T>
		bool HasExtension()
		{
			return extension_.find(T::ETypeid::value) != extension_.end();
		}

		template<typename T>
		void RemoveExtension()
		{
			extension_.erase(T::ETypeid::value);
		}

		virtual ~ExtensionSupport() {}

	private:
		std::unordered_map<std::uint64_t, std::shared_ptr<Extension>> extension_;
	};


	inline constexpr std::uint64_t GenHash(const char* txt)
	{
		return Internal::hash_64_fnv1a_const(txt);
	}

}