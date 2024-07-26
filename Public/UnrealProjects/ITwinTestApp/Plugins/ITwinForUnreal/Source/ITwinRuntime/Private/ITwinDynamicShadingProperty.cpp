/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDynamicShadingProperty.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinDynamicShadingProperty.h"
#include <Compil/BeforeNonUnrealIncludes.h>
	#include <BeHeaders/Compil/AlwaysFalse.h>
#include <Compil/AfterNonUnrealIncludes.h>

#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialTypes.h"
#include "RHICommandList.h"
#include "Rendering/Texture2DResource.h"
#include "RenderingThread.h"

#include <cmath>

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

}

template<typename DataType, int NumChannels>
FITwinDynamicShadingProperty<DataType, NumChannels>::FITwinDynamicShadingProperty(
		ITwinFeatureID const MaxAddressableFeatureID,
		std::optional<std::array<DataType, NumChannels>> const& FillWithValue)
	: TotalUsedPixels(MaxAddressableFeatureID.value() + 1)
	, TextureDimension(static_cast<int32>(std::ceil(std::sqrt((double)TotalUsedPixels))))
{
	TextureDataBytesPerPixel = NumChannels * sizeof(DataType);
	TextureDataBytesPerRow = TextureDimension * TextureDataBytesPerPixel;
	TextureDataBytesTotal = TextureDimension * TextureDataBytesPerRow;
	TextureComponentsPerRow = NumChannels * (size_t)TextureDimension;
	// Yes, TextureData contains DataType values, not uint8!
	TextureData.resize(TextureDimension * TextureComponentsPerRow);
	TextureRegion = std::make_unique<FUpdateTextureRegion2D>(0, 0, 0, 0, TextureDimension, TextureDimension);
	InitializeTexture(FillWithValue);
}

template<typename DataType, int NumChannels>
FITwinDynamicShadingProperty<DataType, NumChannels>::~FITwinDynamicShadingProperty()
{
	if (Texture && Texture->IsValidLowLevel())
	{
		Texture->RemoveFromRoot();
	}
}

// Would need to UpdateTexture if return true, realloc texture if return false (and also reset flags, etc.)
// so I think it's too risky to allow resizing the textures, I'd rather try first to avoid the need for that.
// Dox would have been:
// Check whether the current texture can accomodate a new (normally higher!) capacity
// \return true when the current texture is large enough and the new capacity was accepted, false when
//		the texture is too small, so that the caller should create a new one
//template<typename DataType, int NumChannels>
//bool FITwinDynamicShadingProperty<DataType, NumChannels>::EnsureCapacity(
//	ITwinFeatureID const MaxAddressableFeatureID,
//	std::optional<std::array<DataType, NumChannels>> const& FillWithValue)
//{
//	if ((MaxAddressableFeatureID.value() + 1) <= (TextureDimension * TextureDimension))
//	{
//		for (uint32 Pixel = TotalUsedPixels; Pixel < (MaxAddressableFeatureID.value() + 1); ++Pixel)
//		{
//			SetPixel(Pixel, FillWithValue);
//		}
//		TotalUsedPixels = (MaxAddressableFeatureID.value() + 1);
//		return true;
//	}
//	else return false;
//}

template<typename DataType, int NumChannels>
void FITwinDynamicShadingProperty<DataType, NumChannels>::UpdateInMaterial(
	TWeakObjectPtr<UMaterialInstanceDynamic> const& MatPtr,
	FMaterialParameterInfo const& TextureAttachment) const
{
	if (MatPtr.IsValid())
		MatPtr->SetTextureParameterValueByInfo(TextureAttachment, Texture);
}

template<typename DataType, int NumChannels>
void FITwinDynamicShadingProperty<DataType, NumChannels>::UpdateInMaterials(
	std::vector<TWeakObjectPtr<UMaterialInstanceDynamic>> const& Materials,
	FMaterialParameterInfo const& TextureAttachment) const
{
	for (auto&& MatPtr : Materials)
	{
		if (MatPtr.IsValid())
			MatPtr->SetTextureParameterValueByInfo(TextureAttachment, Texture);
	}
}

template<typename DataType, int NumChannels>
void FITwinDynamicShadingProperty<DataType, NumChannels>::FillAllChannelsWith(DataType const Value)
{
	if constexpr (std::is_same_v<DataType, uint8>)
	{
		memset(&TextureData[0], Value, TotalUsedPixels);
	}
	else
	{
		// Fill first row
		{	DataType* TexPtr = &TextureData[0];
			for (int32 p = 0; p < TextureDimension; ++p)
			{
				for (int c = 0; c < NumChannels; ++c)
				{
					*TexPtr = Value;
					++TexPtr;
				}
			}
		}
		DataType* const SrcPtr = &TextureData[0];
		// Duplicate
		for (int32 row = 1; row < TextureDimension; ++row)
		{
			DataType* const DstPtr = SrcPtr + row * TextureComponentsPerRow;
			memcpy(DstPtr, SrcPtr, TextureDataBytesPerRow);
		}
	}
	bNeedUpdate = true;
}

template<typename DataType, int NumChannels>
void FITwinDynamicShadingProperty<DataType, NumChannels>::FillWith(
	std::array<DataType, NumChannels> const& Value)
{
	// Fill first row
	{	DataType* TexPtr = &TextureData[0];
		for (int32 p = 0; p < TextureDimension; ++p)
		{
			for (auto const ChanVal : Value)
			{
				*TexPtr = ChanVal;
				++TexPtr;
			}
		}
	}
	DataType* const SrcPtr = &TextureData[0];
	// Duplicate
	for (int32 row = 1; row < TextureDimension; ++row)
	{
		DataType* const DstPtr = SrcPtr + row * TextureComponentsPerRow;
		memcpy(DstPtr, SrcPtr, TextureDataBytesPerRow);
	}
	bNeedUpdate = true;
}

template<typename DataType, int NumChannels>
void FITwinDynamicShadingProperty<DataType, NumChannels>::FillWith(
	std::array<DataType, NumChannels> const& Value, std::array<bool, NumChannels> const Mask)
{
	DataType* TexPtr = &TextureData[0];
	for (uint32 p = 0; p < TotalUsedPixels; ++p)
	{
		for (int c = 0; c < NumChannels; ++c)
		{
			if (Mask[c])
			{
				*TexPtr = Value[c];
			}
			++TexPtr;
		}
	}
	bNeedUpdate = true;
}

template<typename DataType, int NumChannels>
void FITwinDynamicShadingProperty<DataType, NumChannels>::InitializeTexture(
	std::optional<std::array<DataType, NumChannels>> const& FillWithValue)
{
	Texture = UTexture2D::CreateTransient(TextureDimension, TextureDimension,
		Detail::GetTexturePixelFormat<DataType, NumChannels>());
	// OA had used "TC_VectorDisplacementmap", which is apparently uncompressed (see this admittedly old
	// post: https://forums.unrealengine.com/t/how-do-i-turn-off-compression-for-textures/31384/3)
	// In the linked discussion, a "forum veteran" says to use TC_EditorIcon instead.
	//Texture->CompressionSettings = TextureCompressionSettings::TC_VectorDisplacementmap;
	Texture->CompressionSettings = Detail::GetTextureCompressionSettings<DataType, NumChannels>();
	Texture->SRGB = 0;
#if WITH_EDITORONLY_DATA
	Texture->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
#endif
	Texture->Filter = TextureFilter::TF_Nearest;
	Texture->AddToRoot();
	Texture->UpdateResource();
	if (FillWithValue)
	{
		FillWith(*FillWithValue);
	}
	// Useless, TextureRHI is not yet created so it will just return
	//UpdateTexture();
}

template<typename DataType, int NumChannels>
bool FITwinDynamicShadingProperty<DataType, NumChannels>::UpdateTexture(/*bool bFreeData*/)
{
	if (Texture == nullptr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Dynamic Texture tried to Update Texture but it hasn't been initialized!"));
		return true;
	}
	if (!bNeedUpdate) return false;

	auto* TextureRHI = ((FTexture2DResource*)Texture->GetResource())->GetTexture2DRHI();
	// tested in UpdateTextureRegions too but bNeedUpdate requires this early exit, which needs to be toggled
	// as soon as the copy is made I think, not in DataCleanupFunc
	if (!TextureRHI) return true;

	bNeedUpdate = false;
	// Note: UpdateTextureRegions makes the copy of the source data itself
	Texture->UpdateTextureRegions(0/*Mip*/, 1/*NumRegions*/, TextureRegion.get(), TextureDataBytesPerRow,
		TextureDataBytesPerPixel, reinterpret_cast<uint8*>(&TextureData[0]));
	return true;
}

template<typename DataType, int NumChannels>
void FITwinDynamicShadingProperty<DataType, NumChannels>::SetPixel(uint32 const Pixel,
	std::array<DataType, NumChannels> const& Value)
{
	if (Pixel >= TotalUsedPixels)
	{
		check(false);
		return;
	}
	DataType* TexPtr = &TextureData[Pixel * NumChannels];
	for (auto&& ChanVal : Value)
	{
		*TexPtr = ChanVal;
		++TexPtr;
	}
	bNeedUpdate = true;
}

template<typename DataType, int NumChannels>
std::array<DataType, NumChannels> FITwinDynamicShadingProperty<DataType, NumChannels>::GetPixel(
	uint32 const Pixel) const
{
	if (Pixel >= TotalUsedPixels)
	{
		check(false);
		return {};
	}
	std::array<DataType, NumChannels> Ret;
	DataType const* TexPtr = &TextureData[Pixel * NumChannels];
	for (auto& ChanVal : Ret)
	{
		ChanVal = *TexPtr;
		++TexPtr;
	}
	return Ret;
}

template<typename DataType, int NumChannels>
void FITwinDynamicShadingProperty<DataType, NumChannels>::SetPixel(ITwinFeatureID const Pixel,
	std::array<DataType, NumChannels> const& Value)
{
	SetPixel(Pixel.value(), Value);
}

template<typename DataType, int NumChannels>
void FITwinDynamicShadingProperty<DataType, NumChannels>::SetPixel(int32 X, int32 Y,
	std::array<DataType, NumChannels> const& Value)
{
	if (X < 0 || Y < 0 || X >= TextureDimension || Y >= TextureDimension)
	{
		check(false);
		return;
	}
	DataType* TexPtr = &TextureData[((Y * TextureDimension) + X) * NumChannels];
	for (auto&& ChanVal : Value)
	{
		*TexPtr = ChanVal;
		++TexPtr;
	}
	bNeedUpdate = true;
}

template<typename DataType, int NumChannels>
void FITwinDynamicShadingProperty<DataType, NumChannels>::SetPixelsAlpha(
	std::vector<ITwinFeatureID> const& Pixels, DataType const Value)
{
	constexpr int Channel = Detail::GetTextureAlphaChannelIndex<DataType, NumChannels>();
	for (auto&& Pixel : Pixels)
	{
		TextureData[Pixel.value() * NumChannels + Channel] = Value;
	}
	bNeedUpdate = true;
}

template<typename DataType, int NumChannels>
void FITwinDynamicShadingProperty<DataType, NumChannels>::SetPixelsExceptAlpha(
	std::vector<ITwinFeatureID> const& Pixels, std::array<DataType, NumChannels> const& Value)
{
	constexpr int AlphaChan = Detail::GetTextureAlphaChannelIndex<DataType, NumChannels>();
	for (auto&& Pixel : Pixels)
	{
		for (int c = 0; c < NumChannels; ++c)
		{
			if (c != AlphaChan)
			{
				TextureData[Pixel.value() * NumChannels + c] = Value[c];
			}
		}
	}
	bNeedUpdate = true;
}

template<typename DataType, int NumChannels>
void FITwinDynamicShadingProperty<DataType, NumChannels>::SetAllPixelsExceptAlpha(
	std::array<DataType, NumChannels> const& Value)
{
	constexpr int AlphaChan = Detail::GetTextureAlphaChannelIndex<DataType, NumChannels>();
	DataType* TexPtr = &TextureData[0];
	for (uint32 p = 0; p < TotalUsedPixels; ++p)
	{
		for (int c = 0; c < NumChannels; ++c)
		{
			if (c != AlphaChan)
			{
				*TexPtr = Value[c];
			}
			++TexPtr;
		}
	}
	bNeedUpdate = true;
}

template<typename DataType, int NumChannels>
void FITwinDynamicShadingProperty<DataType, NumChannels>::SetAllPixelsAlpha(DataType const Value)
{
	DataType* TexPtr = (&TextureData[0]) + Detail::GetTextureAlphaChannelIndex<DataType, NumChannels>();
	for (uint32 Pixel = 0; Pixel < TotalUsedPixels; TexPtr += NumChannels)
	{
		*TexPtr = Value;
	}
	bNeedUpdate = true;
}

template<typename DataType, int NumChannels>
void FITwinDynamicShadingProperty<DataType, NumChannels>::SetPixels(
	std::vector<ITwinFeatureID> const& Pixels, std::array<DataType, NumChannels> const& Value)
{
	for (auto&& Pixel : Pixels)
	{
		SetPixel(Pixel, Value);
	}
	bNeedUpdate = true;
}

template class FITwinDynamicShadingProperty<uint8, 4>; // BGRA8
template class FITwinDynamicShadingProperty<float, 4>; // *A*BGR32f
