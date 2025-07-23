/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDynamicShadingProperty.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "ITwinDynamicShadingProperty.h"

#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialTypes.h"
#include "Rendering/Texture2DResource.h"
#include "RenderingThread.h"
#include "RHICommandList.h"

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <Core/Tools/Log.h>
#include <Compil/AfterNonUnrealIncludes.h>

// For debugging...
#define ITWIN_SAVE_DYNTEX_TO_FILE() 0

#if ITWIN_SAVE_DYNTEX_TO_FILE()
#include "ImageUtils.h"
#endif

#include "ITwinDynamicShadingProperty.inl"

#include <array>
#include <cmath>

/*static*/
template<typename DataType, int NumChannels>
void FITwinDynamicShadingProperty<DataType, NumChannels>::Create(
	std::shared_ptr<ThisType>& OwnerPtr,
	ITwinFeatureID const MaxAddressableFeatureID,
	std::optional<std::array<DataType, NumChannels>> const& FillWithValue)
{
	// Private constructor is inaccessible to std::make_shared...
	//OwnerPtr = std::make_shared<ThisType>(MaxAddressableFeatureID, FillWithValue, OwnerPtr);
	OwnerPtr.reset(new ThisType(MaxAddressableFeatureID, FillWithValue, OwnerPtr));
}

template<typename DataType, int NumChannels>
FITwinDynamicShadingProperty<DataType, NumChannels>::FITwinDynamicShadingProperty(
		ITwinFeatureID const MaxAddressableFeatureID,
		std::optional<std::array<DataType, NumChannels>> const& FillWithValue,
		std::shared_ptr<ThisType> const& InOwnerPtr)
	: TextureDataBytesPerPixel(NumChannels * sizeof(DataType))
	// Don't create 1x1 textures: see azdev#1559500
	, TotalUsedPixels(std::max(4u, MaxAddressableFeatureID.value() + 1))
	, TextureDimension(static_cast<int32>(std::ceil(std::sqrt((double)TotalUsedPixels))))
	, TextureDataBytesPerRow(TextureDimension * TextureDataBytesPerPixel)
	, TextureDataBytesTotal(TextureDimension * TextureDataBytesPerRow)
	, TextureComponentsPerRow(NumChannels * (size_t)TextureDimension)
	, TextureDataTransferBuffer(TextureDimension * TextureComponentsPerRow, DataType(0))
	, TextureRegion(0, 0, 0, 0, TextureDimension, TextureDimension)
	, OwnerPtr(InOwnerPtr)
{
	// Yes, TextureData contains DataType values, not uint8!
	TextureData.resize(TextureDimension * TextureComponentsPerRow, DataType(0));
	InitializeTexture(FillWithValue);
}

template<typename DataType, int NumChannels>
FITwinDynamicShadingProperty<DataType, NumChannels>::~FITwinDynamicShadingProperty()
{
	if (IsValid(Texture) && Texture->IsValidLowLevel())
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
bool FITwinDynamicShadingProperty<DataType, NumChannels>::SetupInMaterial(
	TWeakObjectPtr<UMaterialInstanceDynamic> const& MatPtr,
	FMaterialParameterInfo const& TextureAttachment)
{
	return SetupInMaterials(std::array<TWeakObjectPtr<UMaterialInstanceDynamic>, 1>{ MatPtr },
							TextureAttachment);
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
	InvalidateTexture();
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
	InvalidateTexture();
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
	InvalidateTexture();
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
bool FITwinDynamicShadingProperty<DataType, NumChannels>::UpdateTexture()
{
	if (!IsValid(Texture))
	{
		BE_LOGW("ITwinRender", "Dynamic Texture's update attempt while Texture isn't init'd or already GC'd!");
		return true;
	}
	if (!bNeedUpdate)
	{
		return false;
	}
	auto* TextureRHI = ((FTexture2DResource*)Texture->GetResource())->GetTexture2DRHI();
	// tested in UpdateTextureRegions too but bNeedUpdate requires this early exit
	if (!TextureRHI)
	{
		return true;
	}

	bNeedUpdate = false;
	// Note: UpdateTextureRegions passes the data *pointer* to RHIUpdateTexture2D (in a deferred manner, of
	// course, since the function is called on the render thread). Only RHIUpdateTexture2D copies the data!
	// => thus we need to copy TextureData in a second buffer, so that we do not modify the values while they
	// are read by the render thread / RHI thread.
	// Also note we protect the data from deletion by copying the shared pointers of both the transfer buffer
	// and "this" instance in the lambda capture list below. The 'Texture' and 'TextureRegion' members are
	// thus protected because FITwinSceneTile::Unload no longer destroys the texture if update messages are
	// still pending.
	std::shared_ptr<DataVec> TransferBuffer;
	if (UpdateTasksInProgress > 0)
	{
		// Here we need to allocate another buffer, since TextureDataTransferBuffer is already in use...
		// Note that TextureRegion is never modified, so it can be shared by all tasks without problem.
		TransferBuffer = std::make_shared<DataVec>();
		*TransferBuffer = TextureData;
	}
	else
	{
		// I wanted to avoid needless copies by swapping vectors instead, along with a bNeedCopyOnWrite flag
		// so that only in that case write methods would copy TextureDataTransferBuffer back into TextureData
		// before writing. But to actually avoid the copy (when the update message is handled before any new
		// write is attempted to the texture), we would also need to swap back the vectors in the
		// clean-up function, which is not possible safely since we use no mutex to synchronize it with the
		// game thread execution flow.
		// We could do it in the future if we have to use a mutex for some other imperious reason, or if it is
		// found preferable performance-wise.
		TextureDataTransferBuffer = TextureData;
	}
	// Use the cleanup function (executed by the RHI thread) to decrement this counter when the update is done
	UpdateTasksInProgress++;

	Texture->UpdateTextureRegions(0/*Mip*/, 1/*NumRegions*/, &TextureRegion,
		TextureDataBytesPerRow, TextureDataBytesPerPixel,
		reinterpret_cast<uint8*>(TransferBuffer ? TransferBuffer->data() : TextureDataTransferBuffer.data()),
		[ this, ThisOwnerPtr = this->OwnerPtr/*extend "this" lifetime until end of clean-up lambda*/,
		  ShTransferBuffer = TransferBuffer ]
		(uint8* /*SrcData*/, const FUpdateTextureRegion2D* /*Regions*/)
		{
			// Just testing *IsValidLambda (now removed) was not thread-safe: the CPU might have switched to the
			// game thread just after the test, and proceed to destroying "this"! So the whole instance had to
			// become a shared object so that its lifetime could be extended as long as needed without having to
			// synchronize this lambda and the destructor.
			ensureMsgf(UpdateTasksInProgress.load() > 0, TEXT("Mismatch in task counter"));
			UpdateTasksInProgress--;
			bHasBeenUpdatedAtLeastOnce = true;
		});
	return true;
}

template<typename DataType, int NumChannels>
void FITwinDynamicShadingProperty<DataType, NumChannels>::SetPixel(uint32 const Pixel,
	std::array<DataType, NumChannels> const& Value)
{
	if (Pixel >= TotalUsedPixels) [[unlikely]]
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
	InvalidateTexture();
}

template<typename DataType, int NumChannels>
void FITwinDynamicShadingProperty<DataType, NumChannels>::SetPixel(uint32 const Pixel,
	std::array<DataType, NumChannels> const& Value, std::array<bool, NumChannels> const& Mask)
{
	if (Pixel >= TotalUsedPixels) [[unlikely]]
	{
		check(false);
		return;
	}
	DataType* TexPtr = &TextureData[Pixel * NumChannels];
	for (int c = 0; c < NumChannels; ++c)
	{
		if (Mask[c])
		{
			*TexPtr = Value[c];
		}
		++TexPtr;
	}
	InvalidateTexture();
}

template<typename DataType, int NumChannels>
std::array<DataType, NumChannels> FITwinDynamicShadingProperty<DataType, NumChannels>::GetPixel(
	uint32 const Pixel) const
{
	if (Pixel >= TotalUsedPixels) [[unlikely]]
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
void FITwinDynamicShadingProperty<DataType, NumChannels>::SetPixel(ITwinFeatureID const Pixel,
	std::array<DataType, NumChannels> const& Value, std::array<bool, NumChannels> const& Mask)
{
	SetPixel(Pixel.value(), Value, Mask);
}

template<typename DataType, int NumChannels>
void FITwinDynamicShadingProperty<DataType, NumChannels>::SetPixel(int32 X, int32 Y,
	std::array<DataType, NumChannels> const& Value)
{
	if (X < 0 || Y < 0 || X >= TextureDimension || Y >= TextureDimension) [[unlikely]]
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
	InvalidateTexture();
}

template<typename DataType, int NumChannels>
void FITwinDynamicShadingProperty<DataType, NumChannels>::SetAllPixelsExceptAlpha(
	std::array<DataType, NumChannels> const& Value)
{
	std::array<bool, NumChannels> Mask;
	std::fill(Mask.begin(), Mask.end(), true);
	Mask[Detail::GetTextureAlphaChannelIndex<DataType, NumChannels>()] = false;
	FillWith(Value, Mask);
}

template<typename DataType, int NumChannels>
void FITwinDynamicShadingProperty<DataType, NumChannels>::SetAllPixelsAlpha(DataType const Value)
{
	DataType* TexPtr = (&TextureData[0]) + Detail::GetTextureAlphaChannelIndex<DataType, NumChannels>();
	for (uint32 Pixel = 0; Pixel < TotalUsedPixels; TexPtr += NumChannels)
	{
		*TexPtr = Value;
	}
	InvalidateTexture();
}


#if WITH_EDITORONLY_DATA

template<typename DataType, int NumChannels>
bool FITwinDynamicShadingProperty<DataType, NumChannels>::WriteTextureToFile(FString const& FileName)
{
#if ITWIN_SAVE_DYNTEX_TO_FILE()
	if (!IsValid(Texture))
		return false;
	if (bNeedUpdate)
	{
		UpdateTexture();
	}
	while (UpdateTasksInProgress > 0)
	{
		FPlatformProcess::Sleep(0.05f);
	}
	Texture->UpdateResource();
	FTexture2DMipMap* MM = &Texture->GetPlatformData()->Mips[0];
	int w = MM->SizeX;
	int h = MM->SizeY;
	TArray64<uint8> MipData;
	MipData.InsertZeroed(0, w * h * 4);

	FByteBulkData* RawImageData = &MM->BulkData;
	{
		uint8 const* FormatedImageData = static_cast<uint8 const*>(RawImageData->Lock(LOCK_READ_ONLY));
		for (uint8& dst : MipData)
		{
			dst = *FormatedImageData;
			FormatedImageData++;
		}
		RawImageData->Unlock();
	}

	FImage Image;
	Image.RawData = MoveTemp(MipData);
	Image.SizeX = w;
	Image.SizeY = h;
	Image.NumSlices = 1;
	Image.Format = ERawImageFormat::BGRA8;
	Image.GammaSpace = EGammaSpace::sRGB;

	return FImageUtils::SaveImageByExtension(*FileName, Image);
#else
	return false;
#endif
}

#endif // WITH_EDITORONLY_DATA

template class FITwinDynamicShadingProperty<uint8, 4>; // BGRA8
template class FITwinDynamicShadingProperty<float, 4>; // *A*BGR32f
