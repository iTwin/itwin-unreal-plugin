/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDynamicShadingProperty.inl $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "ITwinDynamicShadingProperty.h"

#include <Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/Compil/AlwaysFalse.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include <Engine/Texture2D.h>
#include <Engine/TextureDefines.h>
#include <PixelFormat.h>
#include <UObject/Object.h>

namespace Detail {

template<typename DataType, int NumChannels>
constexpr TextureCompressionSettings GetTextureCompressionSettings()
{
	if constexpr (std::is_same_v<DataType, uint8>)
	{
		static_assert(NumChannels == 4);// no uncompressed/lossless TC_* for RGB or Grayscale??
		return TC_EditorIcon;
	}
	else if constexpr (std::is_same_v<DataType, float>)
	{
		if constexpr (NumChannels == 4)
			return TC_HDR_F32;
		else if constexpr (NumChannels == 1)
			return TC_SingleFloat;
		else static_assert(always_false_v<DataType>,
			"unlisted combination in GetTextureCompressionSettings<float>!");
	}
	else static_assert(always_false_v<DataType>, "unlisted combination in GetTextureCompressionSettings!");
}

/// Note: implementation goes with GetTextureAlphaChannelIndex
template<typename DataType, int NumChannels>
constexpr EPixelFormat GetTexturePixelFormat()
{
	if constexpr (std::is_same_v<DataType, uint8>)
	{
		static_assert(NumChannels == 4);// see GetTextureCompressionSettings
		// there is an RGBA8 but the default is BGRA8 which is usually the native layout I think
		return PF_B8G8R8A8;
	}
	else if constexpr (std::is_same_v<DataType, float>)
	{
		if constexpr (NumChannels == 4)
			return PF_A32B32G32R32F; // ABGR, there is no BGRA32! But see GetTextureAlphaChannelIndex...
		else if constexpr (NumChannels == 1)
			return PF_R32_FLOAT;
		else static_assert(always_false_v<DataType>,
			"unlisted combination in GetTexturePixelFormat<float>!");
	}
	else static_assert(always_false_v<DataType>, "unlisted combination in GetTexturePixelFormat!");
}

/// Note: implementation goes with GetTexturePixelFormat
template<typename DataType, int NumChannels>
constexpr int GetTextureAlphaChannelIndex()
{
	if constexpr (std::is_same_v<DataType, uint8>)
	{
		static_assert(NumChannels == 4);// see GetTextureCompressionSettings
		return 3;
	}
	else if constexpr (std::is_same_v<DataType, float>)
	{
		if constexpr (NumChannels == 4)
			// Clearly alpha is still index 3 and not 0, despite the pixel format name
			// (see GetTexturePixelFormat()), otherwise EnsurePlaneEquation would have needed to swap the
			// channels, but in practice it does not.
			//return 0;
			return 3;
		else if constexpr (NumChannels == 1)
			static_assert(always_false_v<DataType>, "No use setting alpha on single-channel texture!");
		else static_assert(always_false_v<DataType>,
			"unlisted combination in GetTextureAlphaChannelIndex<float>!");
	}
	else static_assert(always_false_v<DataType>, "unlisted combination in GetTextureAlphaChannelIndex!");
}

} // ns Detail

template<typename DataType, int NumChannels>
template<typename MaterialsCont>
bool FITwinDynamicShadingProperty<DataType, NumChannels>::SetupInMaterials(
	MaterialsCont const& Materials,
	FMaterialParameterInfo const& TextureAttachment)
{
	if (!AllowUpdatingMaterials())
	{
		if (!UpdateTasksInProgress) // this means TextureRHI was nullptr
			UpdateTexture();
		return false;
	}
	if (!ensure(IsValid(Texture)))
		return false;
	for (auto&& MatPtr : Materials)
	{
		if (MatPtr.IsValid())
			MatPtr->SetTextureParameterValueByInfo(TextureAttachment, Texture);
	}
	return true;
}

template<typename DataType, int NumChannels>
template<typename FeatureIDsCont>
void FITwinDynamicShadingProperty<DataType, NumChannels>::SetPixelsAlpha(
	FeatureIDsCont const& Pixels, DataType const Value)
{
	if (Pixels.empty()) [[unlikely]]
		return;
	constexpr int Channel = Detail::GetTextureAlphaChannelIndex<DataType, NumChannels>();
	for (auto&& Pixel : Pixels)
	{
		TextureData[Pixel.value() * NumChannels + Channel] = Value;
	}
	InvalidateTexture();
}

template<typename DataType, int NumChannels>
template<typename FeatureIDsCont>
void FITwinDynamicShadingProperty<DataType, NumChannels>::SetPixelsExceptAlpha(
	FeatureIDsCont const& Pixels, std::array<DataType, NumChannels> const& Value)
{
	if (Pixels.empty()) [[unlikely]]
		return;
	constexpr int AlphaChan = Detail::GetTextureAlphaChannelIndex<DataType, NumChannels>();
	for (auto&& Pixel : Pixels)
	{
		for (int c = 0; c < NumChannels; ++c)
		{
			if (c != AlphaChan) [[likely]]
			{
				TextureData[Pixel.value() * NumChannels + c] = Value[c];
			}
		}
	}
	InvalidateTexture();
}

template<typename DataType, int NumChannels>
template<typename FeatureIDsCont>
void FITwinDynamicShadingProperty<DataType, NumChannels>::SetPixels(
	FeatureIDsCont const& Pixels, std::array<DataType, NumChannels> const& Value)
{
	if (Pixels.empty()) [[unlikely]]
		return;
	for (auto&& Pixel : Pixels)
	{
		SetPixel(Pixel, Value);
	}
	InvalidateTexture();
}

template<typename DataType, int NumChannels>
template<typename FeatureIDsCont>
void FITwinDynamicShadingProperty<DataType, NumChannels>::SetPixels(
	FeatureIDsCont const& Pixels, std::array<DataType, NumChannels> const& Value,
	std::array<bool, NumChannels> const& Mask)
{
	if (Pixels.empty()) [[unlikely]]
		return;
	for (auto&& Pixel : Pixels)
	{
		SetPixel(Pixel, Value, Mask);
	}
	InvalidateTexture();
}
