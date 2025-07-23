/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinTilesetAccess.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#include <ITwinTilesetAccess.h>
#include <ITwinGoogle3DTileset.h>

#include "ITwinTilesetAccess.inl"
#include <Decoration/ITwinDecorationHelper.h>
#include <Math/UEMathExts.h>
#include <CesiumPolygonRasterOverlay.h>

namespace ITwin
{
	static constexpr double IMODEL_BEST_SCREENSPACE_ERROR = 1.0;
	static constexpr double IMODEL_WORST_SCREENSPACE_ERROR = 100.0;
	// Google 3D's memory usage grows exponentially with SSE because of the openendedness of the tileset
	// and the lack of occlusion culling or accounting for the "shape" of the tile's geometry (mostly flat
	// in constructible areas...) to compare with the target SSE.
	// *** The RAM usage is extremely sensitive to this "best" SSE value ***
	// An SSE of 11 was found to have a 1.5GB footprint, but an SSE of 5 used almost 4GB,
	// and an SSE of 1 more than 10GB!! (and when starting up directly at 100%, no tiles were showing
	// for some reason, as if some kind of bottleneck prevented even the coarsest LODs to show...)
	const double GOOGLE3D_BEST_SCREENSPACE_ERROR = 8.0;
	static constexpr double GOOGLE3D_WORST_SCREENSPACE_ERROR = 100.0;

	//! Adjust the tileset quality, given a percentage (value in range [0;1])
	void SetTilesetQuality(ACesium3DTileset& Tileset, float QualityValue)
	{
		double const NormalizedQuality = std::clamp(QualityValue, 0.f, 1.f);
		double const ScreenspaceError = (nullptr != Cast<AITwinGoogle3DTileset>(&Tileset))
			? (NormalizedQuality * GOOGLE3D_BEST_SCREENSPACE_ERROR
				+ (1.0 - NormalizedQuality) * GOOGLE3D_WORST_SCREENSPACE_ERROR)
			: (NormalizedQuality * IMODEL_BEST_SCREENSPACE_ERROR
				+ (1.0 - NormalizedQuality) * IMODEL_WORST_SCREENSPACE_ERROR);
		Tileset.SetMaximumScreenSpaceError(ScreenspaceError);
	}

	//! Returns the tileset quality as a percentage (value in range [0;1])
	float GetTilesetQuality(ACesium3DTileset const& Tileset)
	{
		bool const bForGoogle3DTiles = (nullptr != Cast<AITwinGoogle3DTileset>(&Tileset));
		auto const BestSSE =
			bForGoogle3DTiles ? GOOGLE3D_BEST_SCREENSPACE_ERROR : IMODEL_BEST_SCREENSPACE_ERROR;
		auto const WorstSSE =
			bForGoogle3DTiles ? GOOGLE3D_WORST_SCREENSPACE_ERROR : IMODEL_WORST_SCREENSPACE_ERROR;
		double const ScreenspaceError = std::clamp(Tileset.MaximumScreenSpaceError, BestSSE, WorstSSE);
		return static_cast<float>((ScreenspaceError - WorstSSE) / (BestSSE - WorstSSE));
	}

	UCesiumPolygonRasterOverlay* GetCutoutOverlay(ACesium3DTileset const& Tileset)
	{
		return Tileset.FindComponentByClass<UCesiumPolygonRasterOverlay>();
	}

	void InitCutoutOverlay(ACesium3DTileset& Tileset)
	{
		if (GetCutoutOverlay(Tileset) != nullptr)
			return;

		// Instantiate a UCesiumPolygonRasterOverlay component, which can then be populated with
		// polygons to enable cutout (ACesiumCartographicPolygon)
		auto RasterOverlay = NewObject<UCesiumPolygonRasterOverlay>(&Tileset,
			UCesiumPolygonRasterOverlay::StaticClass(),
			FName(*(Tileset.GetActorNameOrLabel() + TEXT("_RasterOverlay"))),
			RF_Transactional);
		RasterOverlay->OnComponentCreated();
		Tileset.AddInstanceComponent(RasterOverlay);
		RasterOverlay->RegisterComponent();
	}
}


FITwinTilesetAccess::FITwinTilesetAccess(AActor& TilesetOwnerActor)
	: TilesetOwner(TilesetOwnerActor)
{
}

FITwinTilesetAccess::~FITwinTilesetAccess()
{

}

const ACesium3DTileset* FITwinTilesetAccess::GetTileset() const
{
	return ITwin::TGetTileset<ACesium3DTileset const>(TilesetOwner);
}

ACesium3DTileset* FITwinTilesetAccess::GetMutableTileset()
{
	return ITwin::TGetTileset<ACesium3DTileset>(TilesetOwner);
}

bool FITwinTilesetAccess::HasTileset() const
{
	return GetTileset() != nullptr;
}

// Visibility
void FITwinTilesetAccess::HideTileset(bool bHide)
{
	ACesium3DTileset* Tileset = GetMutableTileset();
	if (!Tileset)
		return;

	Tileset->SetActorHiddenInGame(bHide);

	AITwinDecorationHelper* DecoHelper = GetDecorationHelper();
	if (DecoHelper)
	{
		auto const DecoKey = GetDecorationKey();
		ITwinSceneInfo SceneInfo = DecoHelper->GetSceneInfo(DecoKey);
		if (!SceneInfo.Visibility.has_value() || *SceneInfo.Visibility != !bHide)
		{
			SceneInfo.Visibility = !bHide;
			DecoHelper->SetSceneInfo(DecoKey, SceneInfo);
		}
	}
}

bool FITwinTilesetAccess::IsTilesetHidden() const
{
	ACesium3DTileset const* Tileset = GetTileset();
	if (!Tileset)
		return false;
	return Tileset->IsHidden();
}

float FITwinTilesetAccess::GetTilesetQuality() const
{
	ACesium3DTileset const* Tileset = GetTileset();
	if (Tileset)
	{
		return ITwin::GetTilesetQuality(*Tileset);
	}
	else
	{
		return 0.f;
	}
}

void FITwinTilesetAccess::SetTilesetQuality(float Value)
{
	ACesium3DTileset* Tileset = GetMutableTileset();
	if (!Tileset)
		return;

	ITwin::SetTilesetQuality(*Tileset, Value);

	AITwinDecorationHelper* DecoHelper = GetDecorationHelper();
	if (DecoHelper)
	{
		auto const DecoKey = GetDecorationKey();
		ITwinSceneInfo SceneInfo = DecoHelper->GetSceneInfo(DecoKey);
		if (!SceneInfo.Quality.has_value() || fabs(*SceneInfo.Quality - Value) > 1e-5)
		{
			SceneInfo.Quality = Value;
			DecoHelper->SetSceneInfo(DecoKey, SceneInfo);
		}
	}
}

void FITwinTilesetAccess::GetModelOffset(FVector& Pos, FVector& Rot) const
{
	Pos = TilesetOwner.GetActorLocation() / 100.0;
	Rot = TilesetOwner.GetActorRotation().Euler();
}

void FITwinTilesetAccess::SetModelOffset(const FVector& Pos, const FVector& Rot)
{
	TilesetOwner.SetActorLocationAndRotation(Pos, FQuat::MakeFromEuler(Rot));
	// SetActorLocationAndRotation already calls that, and only when needed (SetModelOffset gets called from
	// Carrot UI even when merely clicking into then away from the offset edit fields!)
	//OnIModelOffsetChanged();

	AITwinDecorationHelper* DecoHelper = GetDecorationHelper();
	if (DecoHelper)
	{
		auto const DecoKey = GetDecorationKey();
		ITwinSceneInfo SceneInfo = DecoHelper->GetSceneInfo(DecoKey);
		if (!SceneInfo.Offset.has_value() || !SceneInfo.Offset->Equals(TilesetOwner.GetTransform()))
		{
			SceneInfo.Offset = TilesetOwner.GetTransform();
			DecoHelper->SetSceneInfo(DecoKey, SceneInfo);
		}
	}
}

void FITwinTilesetAccess::OnModelOffsetLoaded()
{

}

void FITwinTilesetAccess::ApplyLoadedInfo(ITwinSceneInfo const& SceneInfo, bool bIsModelFullyLoaded)
{
	if (SceneInfo.Offset)
	{
		// Beware we can call this twice for iModels: first during scene loading, and then when the model is
		// fully loaded.
		if (bIsModelFullyLoaded)
		{
			// Avoid doing a costly refresh if nothing has changed
			if (!FITwinMathExts::StrictlyEqualTransforms(TilesetOwner.GetActorTransform(), *SceneInfo.Offset))
			{
				TilesetOwner.SetActorTransform(*SceneInfo.Offset, false, nullptr, ETeleportType::TeleportPhysics);
				OnModelOffsetLoaded();
			}
		}
		else
		{
			// The model is not yet loaded => just update the actor transformation.
			TilesetOwner.SetActorTransform(*SceneInfo.Offset, true);
		}
	}
	if (SceneInfo.Visibility)
		HideTileset(!*SceneInfo.Visibility);
	if (SceneInfo.Quality)
		SetTilesetQuality(*SceneInfo.Quality);
}
