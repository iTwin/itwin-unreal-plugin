/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinDynamicShadingProperty.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "ITwinFeatureID.h"

#include "RHITypes.h"
#include "UObject/NameTypes.h"

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <vector>

class UTexture2D;
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
	using ThisType = FITwinDynamicShadingProperty<DataType, NumChannels>;

	FITwinDynamicShadingProperty(ITwinFeatureID const MaxAddressableFeatureID,
		std::optional<std::array<DataType, NumChannels>> const& FillWithValue,
		std::shared_ptr<ThisType> const& OwnerPtr);

public:
	~FITwinDynamicShadingProperty();

	static void Create(std::shared_ptr<ThisType>& OwnerPtr, ITwinFeatureID const MaxAddressableFeatureID,
					   std::optional<std::array<DataType, NumChannels>> const& FillWithValue);

	uint32 GetTotalUsedPixels() const { return TotalUsedPixels; }
	/// For RGBA colors, note that the expected channel order is B, G, R, A.
	void SetPixel(int32 X, int32 Y, std::array<DataType, NumChannels> const& Value);
	void SetPixel(uint32 const Pixel, std::array<DataType, NumChannels> const& Value);
	void SetPixel(uint32 const Pixel, std::array<DataType, NumChannels> const& Value,
				  std::array<bool, NumChannels> const& Mask);
	std::array<DataType, NumChannels> GetPixel(uint32 const Pixel) const;
	void SetPixel(ITwinFeatureID const Pixel, std::array<DataType, NumChannels> const& Value);
	void SetPixel(ITwinFeatureID const Pixel, std::array<DataType, NumChannels> const& Value,
				  std::array<bool, NumChannels> const& Mask);
	void FillWith(std::array<DataType, NumChannels> const& Value);///< Fill with a constant pixel
	/// Fill with a constant pixel, only for channels where the Mask bit is set, thus no whole-row copy
	/// optimization is possible: use the other FillPixel when the Mask is all true's.
	void FillWith(std::array<DataType, NumChannels> const& Value, std::array<bool, NumChannels> const Mask);
	void FillAllChannelsWith(DataType const Value);///< Fill all channels of all pixels with a constant value
	template<typename FeatureIDsCont>
	void SetPixels(FeatureIDsCont const& Pixels, std::array<DataType, NumChannels> const& Value);
	template<typename FeatureIDsCont>
	void SetPixels(FeatureIDsCont const& Pixels, std::array<DataType, NumChannels> const& Value,
				   std::array<bool, NumChannels> const& Mask);
	template<typename FeatureIDsCont>
	void SetPixelsAlpha(FeatureIDsCont const& Pixels, DataType const Value);
	template<typename FeatureIDsCont>
	void SetPixelsExceptAlpha(FeatureIDsCont const& Pixels, std::array<DataType, NumChannels> const& Value);
	void SetAllPixelsAlpha(DataType const Value);
	void SetAllPixelsExceptAlpha(std::array<DataType, NumChannels> const& Value);

	/// Update Texture Object from Texture Data
	/// \return A flag telling whether the texture is "dirty", ie either the texture resource isn't even ready
	///		yet, and the call couldn't actually process the update request, or it is ready and an update
	///		render command was enqueued. The return value will be false as soon as the asynchronous command
	///		was actually processed. In such case, we assume that the RHI texture is still valid.
	bool UpdateTexture();
	/// Returns whether we must wait for the first asynchronous update to finish.
	bool NeedToWaitForAsyncUpdate() const { return !AllowUpdatingMaterials(); }

	/// Link the dynamic texture to the given material instance. It has to be done just once for a given
	/// material, but it requires that the texture is up-to-date, and will return false if that's not the
	/// case. It may call UpdateTexture itself to trigger the initial update in the render thread, in case
	/// it wasn't done before or the call was inoperant at the time (possible if GetTexture2DRHI() returned
	/// null!).
	[[nodiscard]] bool SetupInMaterial(TWeakObjectPtr<UMaterialInstanceDynamic> const& MatPtr,
									   FMaterialParameterInfo const& TextureAttachment);
	template<typename MaterialsCont>
	[[nodiscard]] bool SetupInMaterials(MaterialsCont const& Materials,
										FMaterialParameterInfo const& TextureAttachment);

#if WITH_EDITORONLY_DATA
	bool WriteTextureToFile(FString const& FileName);
#endif

private:
	uint32 const TextureDataBytesPerPixel;
	uint32 const TotalUsedPixels; ///< Keep BEFORE TextureDimension!
	int32  const TextureDimension; ///< Dimension of the square texture
	uint32 const TextureDataBytesPerRow;
	uint32 const TextureDataBytesTotal;
	size_t const TextureComponentsPerRow; ///< Number of pixel components in each row

	using DataVec = std::vector<DataType>;
	DataVec TextureData;
	bool bNeedUpdate = true;
	/// Copy of TextureData used only for the asynchronous update of the Unreal texture.
	DataVec TextureDataTransferBuffer;
	// See comment above "TextureDataTransferBuffer = TextureData;" in UpdateTexture
	//bool bNeedCopyOnWrite = false;

	/// Number of (asynchronous) update task which are currently stacked in the render thread. The counter is
	/// decremented in the data cleanup callback, which is performed by the RHI thread.
	std::atomic_uint32_t UpdateTasksInProgress = 0;
	/// We need to wait before updating materials *only if* the texture has never been updated, according to
	/// our tests... The documentation is not very clear in Unreal about the subject :-(
	/// Therefore, as soon as we have updated the texture at least once, we will accept updating materials.
	std::atomic_bool bHasBeenUpdatedAtLeastOnce = false;

	/// This could more logically have been an UTexture2DDynamic, but I don't see any advantage, whereas on
	/// the other hand, UTexture2DDynamic does not have UpdateTextureRegions, which would thus have to be
	/// duplicated.
	/// Texture is added to the garbage collector's root set (see AddToRoot/RemoveFromRoot) so that I think
	/// we don't have to hold it by a UPROPERTY, which would mean having this class a UCLASS/USTRUCT, and in
	/// turn all classes that hold them, etc. (question: would a TStrongPtr work, too?)
	UTexture2D* Texture = nullptr;
	FUpdateTextureRegion2D const TextureRegion;

	/// Instances of this class need to persist until the last UpdateTexture message has been processed by the
	/// render (or RHI?) thread: this is done by copying the shared_ptr ensuring this instance's lifetime in
	/// the capture list of the clean-up lambda passed to said message. Otherwise we would have needed a mutex
	/// to synchronize the clean-up lambda with the destructor, and also avoid concurrency issues in the use of
	/// the Texture member.
	std::shared_ptr<FITwinDynamicShadingProperty<DataType, NumChannels>> const& OwnerPtr;

	void InitializeTexture(std::optional<std::array<DataType, NumChannels>> const& FillWithValue);

	/// Mark the texture for future update (should be called whenever we modify a pixel in TextureData...)
	void InvalidateTexture() { bNeedUpdate = true; }

	/// Returns whether the texture can be attached to a material instance: one can update material(s) with
	/// our texture only if the latter has been completely updated at least once.
	bool AllowUpdatingMaterials() const { return bHasBeenUpdatedAtLeastOnce.load(); }
};

// Note: all template combinations must have their specialization in the CPP

/// Careful with the channel order: BGRA here (compare with FITwinDynamicShadingABGR32fProperty)
using FITwinDynamicShadingBGRA8Property = FITwinDynamicShadingProperty<uint8, 4>;
/// Careful with the channel order: ABGR here (compare with FITwinDynamicShadingBGRA8fProperty)
using FITwinDynamicShadingABGR32fProperty = FITwinDynamicShadingProperty<float, 4>;
