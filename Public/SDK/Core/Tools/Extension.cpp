/*--------------------------------------------------------------------------------------+
|
|     $Source: Extension.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------------+
|
|     $Source: Extension.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/
#include "Extension.h"
#include <memory>

namespace SDK::Core::Tools
{
	const std::shared_ptr<Extension>& ExtensionSupport::GetExtension(ExtensionId id)
	{
		auto it = extension_.find(id);
		if (it != extension_.end())
			return it->second;
		static std::shared_ptr<Extension> empty;
		return empty;
	}

	void ExtensionSupport::AddExtension(ExtensionId id, const std::shared_ptr<Extension>& extension)
	{
		extension_[id] = extension;
	}

	bool ExtensionSupport::HasExtension(ExtensionId id)
	{
		return extension_.find(id) != extension_.end();
	}

}