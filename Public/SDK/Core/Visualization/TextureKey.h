/*--------------------------------------------------------------------------------------+
|
|     $Source: TextureKey.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/



#pragma once

#ifndef SDK_CPPMODULES
#	include <set>
#	include <string>
#	include <unordered_map>
#	ifndef MODULE_EXPORT
#		define MODULE_EXPORT
#	endif // !MODULE_EXPORT
#endif

MODULE_EXPORT namespace AdvViz::SDK
{
	enum class ETextureSource : uint8_t;


	/// Full identifier for a decoration or iTwin texture.
	struct TextureKey
	{
		std::string id;
		ETextureSource eSource;

		inline bool operator== (TextureKey const& other) const;
		inline bool operator< (TextureKey const& other) const;
	};

	using TextureKeySet = std::set<TextureKey>;
	using PerIModelTextureSet = std::unordered_map<std::string, AdvViz::SDK::TextureKeySet>; // iModelID -> TextureSet


	//-------------------------------------------------------------------------------------------------------
	// Inline implementation
	inline bool TextureKey::operator== (TextureKey const& other) const
	{
		return id == other.id && eSource == other.eSource;
	}
	inline bool TextureKey::operator< (TextureKey const& other) const
	{
		if (eSource < other.eSource)
			return true;
		if (eSource > other.eSource)
			return false;
		return id < other.id;
	}
}
