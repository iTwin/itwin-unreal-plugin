/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDynamicShadingProperty.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "UObject/NameTypes.h"
#include "ITwinFeatureID.h"

#include <array>
#include <memory>
#include <optional>
#include <vector>

class UTexture2D;
struct FUpdateTextureRegion2D;
struct FMaterialParameterInfo;
class UMaterialInstanceDynamic;

/// Handles creation and update of an Unreal texture that can be edited at runtime, to store a set of
/// properties usable from a material shader. Currently supported template combinations are:
///		* uint8 with 4 channels: uncompressed, ie lossless
///		* float with 1 or 4 channels: should use a high-quality low compression format
///
/// Bootstrapped from https://dev.epicgames.com/community/learning/tutorials/ow9v/...
///						.../unreal-engine-creating-a-runtime-editable-texture-in-c
template<typename DataType, int NumChannels>
class FITwinDynamicShadingProperty
{
public:
	FITwinDynamicShadingProperty(ITwinFeatureID const MaxAddressableFeatureID,
								 std::optional<std::array<DataType, NumChannels>> const& FillWithValue);
	~FITwinDynamicShadingProperty();

	uint32 GetTotalUsedPixels() const { return TotalUsedPixels; }
	/// For RGBA colors, note that the expected channel order is B, G, R, A.
	void SetPixel(int32 X, int32 Y, std::array<DataType, NumChannels> const& Value);
	void SetPixel(uint32 const Pixel, std::array<DataType, NumChannels> const& Value);
	std::array<DataType, NumChannels> GetPixel(uint32 const Pixel) const;
	void SetPixel(ITwinFeatureID const Pixel, std::array<DataType, NumChannels> const& Value);
	void FillWith(std::array<DataType, NumChannels> const& Value);///< Fill with a constant pixel
	/// Fill with a constant pixel, only for channels where the Mask bit is set, thus no whole-row copy
	/// optimization is possible: use the other FillPixel when the Mask is all true's.
	void FillWith(std::array<DataType, NumChannels> const& Value, std::array<bool, NumChannels> const Mask);
	void FillAllChannelsWith(DataType const Value);///< Fill all channels of all pixels with a constant value
	void SetPixels(std::vector<ITwinFeatureID> const& Pixels, std::array<DataType, NumChannels> const& Value);
	void SetPixelsAlpha(std::vector<ITwinFeatureID> const& Pixels, DataType const Value);
	void SetPixelsExceptAlpha(std::vector<ITwinFeatureID> const& Pixels,
							  std::array<DataType, NumChannels> const& Value);
	void SetAllPixelsAlpha(DataType const Value);
	void SetAllPixelsExceptAlpha(std::array<DataType, NumChannels> const& Value);

	/// Update Texture Object from Texture Data
	/// \return A flag telling whether the texture is "dirty", ie either the texture resource isn't even ready
	///		yet, and the call couldn't actually process the update request, or it is ready and an update
	///		render command was enqueued. The return value will be false as soon as the bNeedUpdate flag is 
	///		false, but in that case we'll assume it was updated previously and we won't even check whether the
	///		Texture RHI is still valid.
	bool UpdateTexture(/*bool bFreeData = false*/);
	void UpdateInMaterial(
		TWeakObjectPtr<UMaterialInstanceDynamic> const& MatPtr,
		FMaterialParameterInfo const& TextureAttachment) const;
	void UpdateInMaterials(
		std::vector<TWeakObjectPtr<UMaterialInstanceDynamic>> const& Materials,
		FMaterialParameterInfo const& TextureAttachment) const;

#if WITH_EDITORONLY_DATA
	bool WriteTextureToFile(FString const& FileName);
#endif

private:
	uint32 TotalUsedPixels = 0;///< Keep BEFORE TextureDimension!
	int32 TextureDimension = 0;///< Dimension of the square texture
	uint32 TextureDataBytesPerPixel = 0;
	uint32 TextureDataBytesPerRow = 0;
	uint32 TextureDataBytesTotal = 0;
	size_t TextureComponentsPerRow = 0; ///< Number of pixel components in each row
	std::vector<DataType> TextureData;
	bool bNeedUpdate = true;

	/// This could more logically have been an UTexture2DDynamic, but I don't see any advantage, whereas on
	/// the other hand, UTexture2DDynamic does not have UpdateTextureRegions, which would thus have to be
	/// duplicated.
	/// Texture is added to the garbage collector's root set (see AddToRoot/RemoveFromRoot) so that I think
	/// we don't have to hold it by a UPROPERTY, which would mean having this class a UCLASS/USTRUCT, and in
	/// turn all classes that hold them, etc.
	UTexture2D* Texture = nullptr;
	std::unique_ptr<FUpdateTextureRegion2D> TextureRegion;

	void InitializeTexture(std::optional<std::array<DataType, NumChannels>> const& FillWithValue);
};

// Note: all template combinations must have their specialization in the CPP

/// Careful with the channel order: BGRA here (compare with FITwinDynamicShadingABGR32fProperty)
using FITwinDynamicShadingBGRA8Property = FITwinDynamicShadingProperty<uint8, 4>;
/// Careful with the channel order: ABGR here (compare with FITwinDynamicShadingBGRA8fProperty)
using FITwinDynamicShadingABGR32fProperty = FITwinDynamicShadingProperty<float, 4>;
