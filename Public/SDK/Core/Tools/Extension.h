/*--------------------------------------------------------------------------------------+
|
|     $Source: Extension.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <memory>
#include <unordered_map>
#include "TypeId.h"

namespace SDK::Core::Tools
{

	// Extension base class
	class Extension {
	public:
		virtual ~Extension() {}
	};

	class ExtensionSupport {
	public:
		template<typename T>
		const std::shared_ptr<T> GetExtension()
		{
			auto it = extension_.find(T::GetTypeId());
			if (it != extension_.end())
				return std::static_pointer_cast<T>(it->second);
			return {};
		}

		template<typename T>
		void AddExtension(const std::shared_ptr<T>& extension)
		{
			extension_[T::GetTypeId()] = extension;
		}

		template<typename T>
		bool HasExtension()
		{
			return extension_.find(T::GetTypeId()) != extension_.end();
		}

		template<typename T>
		void RemoveExtension()
		{
			extension_.erase(T::GetTypeId());
		}

		virtual ~ExtensionSupport() {}

	private:
		std::unordered_map<std::uint64_t, std::shared_ptr<Extension>> extension_;
	};



}