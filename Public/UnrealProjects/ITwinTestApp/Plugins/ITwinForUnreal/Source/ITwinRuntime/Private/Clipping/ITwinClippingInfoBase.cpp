/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingInfoBase.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Clipping/ITwinClippingInfoBase.h>
#include <Clipping/ITwinClippingInfoBase.inl>

#include <Clipping/ITwinTileExcluderBase.h>
#include <ITwinTilesetAccess.h>
#include <Spline/ITwinSplineHelper.h>
#include <Spline/ITwinSplineTool.h>

#include <DrawDebugHelpers.h>


void FITwinClippingInfluenceInfo::SetInfluenceNone()
{
	bInfluenceAll = false;
	SpecificIDs.Reset();
}

//---------------------------------------------------------------------------------------
// struct FITwinClippingInfoBase
//---------------------------------------------------------------------------------------

FITwinClippingInfoBase::~FITwinClippingInfoBase()
{

}

void FITwinClippingInfoBase::ActivateEffectAtTilesetLevel(bool bActivate) const
{
	for (auto const& TileExcluder : TileExcluders)
	{
		if (TileExcluder.IsValid())
		{
			if (bActivate)
				TileExcluder->Activate(true);
			else
				TileExcluder->Deactivate();
		}
	}
}

void FITwinClippingInfoBase::SetEnabled(bool bInEnabled)
{
	bIsEnabled = bInEnabled;
	DoSetEnabled(bInEnabled);
}

void FITwinClippingInfoBase::DoSetEnabled(bool /*bInEnabled*/)
{

}

void FITwinClippingInfoBase::DoSetInvertEffect(bool /*bInvert*/)
{

}

void FITwinClippingInfoBase::SetInvertEffect(bool bInvert)
{
	DoSetInvertEffect(bInvert);
}

void FITwinClippingInfoBase::DeactivatePrimitiveInExcluder(UITwinTileExcluderBase& Excluder) const
{
	// The default behavior is to have one excluder per clipping primitive, hence we deactivate
	// the effect by deactivating the excluder globally.
	Excluder.Deactivate();
}

bool FITwinClippingInfoBase::NeedsCreateEdgeSplines() const
{
	return CountRequiredEdgeSplines() != EdgeSplines.Num();
}

void FITwinClippingInfoBase::CreateEdgeSplines(TWeakObjectPtr<AITwinSplineTool> const& SplineTool)
{
	if (EdgeSplines.Num() == CountRequiredEdgeSplines())
	{
		return; // Edge splines already created.
	}

	if (!ensure(SplineTool.IsValid()))
	{
		return;
	}

	auto const PreviousUsage = SplineTool->GetUsage();
	SplineTool->SetUsage(EITwinSplineUsage::EdgeDisplayHelper);

	// (Re)create the edge splines for the primitive.
	if (!EdgeSplines.IsEmpty())
	{
		EdgeSplines.Empty();
	}
	DoCreateEdgeSplines(EdgeSplines, *SplineTool);

	BE_ASSERT(EdgeSplines.Num() == CountRequiredEdgeSplines());

	SplineTool->SetUsage(PreviousUsage);
}

void FITwinClippingInfoBase::DoCreateEdgeSplines(TArray<TObjectPtr<AITwinSplineHelper>>& /*OutEdgeSplines*/,
	AITwinSplineTool& /*SplineTool*/)
{
	BE_ASSERT(CountRequiredEdgeSplines() == 0,
		"Derived class must override DoCreateEdgeSplines if it requires edge splines.");
}

void FITwinClippingInfoBase::UpdateEdgeSplinesTransform(FTransform const& InstanceTransform)
{
	for (auto& Spline : EdgeSplines)
	{
		if (Spline)
		{
			Spline->SetTransform(InstanceTransform, false /*bMarkSplineForSaving*/);
		}
	}
}

void FITwinClippingInfoBase::SetEdgeSplinesSelected(bool bSelected)
{
	for (auto& Spline : EdgeSplines)
	{
		if (Spline)
		{
			Spline->SetSelected(bSelected);
		}
	}
}


void FITwinClippingInfoBase::SetEdgeVisibility(bool bVisible)
{
	for (auto& Spline : EdgeSplines)
	{
		if (Spline)
		{
			Spline->SetActorHiddenInGame(!bVisible);
		}
	}
}

void FITwinClippingInfoBase::BeforeDestroy()
{
	// Deactivate the primitive in all tile excluders that are using it, so that they don't keep a reference
	// to it after it is destroyed.
	for (auto const& TileExcluder : TileExcluders)
	{
		if (TileExcluder.IsValid())
		{
			DeactivatePrimitiveInExcluder(*TileExcluder);
		}
	}

	// Make sure to destroy the edge splines before the box is destroyed, to avoid keeping ghosts of the box
	// edges in the scene after the box has been removed.
	for (auto& Spline : EdgeSplines)
	{
		if (Spline)
		{
			Spline->Destroy();
		}
	}
	EdgeSplines.Empty();
}

void FITwinClippingInfoBase::CopyGenericInfoFrom(FITwinClippingInfoBase const& Other)
{
	SetEnabled(Other.IsEnabled());
	SetInvertEffect(Other.GetInvertEffect());

	IModelInfluenceInfo = Other.IModelInfluenceInfo;
	RealityDataInfluenceInfo = Other.RealityDataInfluenceInfo;
	GlobalMapLayersInfluenceInfo = Other.GlobalMapLayersInfluenceInfo;

	InfluenceBoundingBox = Other.InfluenceBoundingBox;
	bNeedsUpdateBoundingBox = Other.bNeedsUpdateBoundingBox;

	SetSceneLinkId(Other.GetSceneLinkId());
}

bool FITwinClippingInfoBase::ShouldInfluenceModel(const ITwin::ModelLink& ModelIdentifier) const
{
	return IsEnabled() && DoesInfluenceModel(ModelIdentifier);
}

bool FITwinClippingInfoBase::IsUsingPerLayerTypeInfluence() const
{
	for (EITwinModelType LayerType : { EITwinModelType::GlobalMapLayer,
									   EITwinModelType::IModel,
									   EITwinModelType::RealityData })
	{
		if (GetInfluenceInfo(LayerType).bInfluenceAll)
		{
			return true;
		}
	}
	return false;
}

void FITwinClippingInfoBase::ConvertToPerLayerInfluence(const TMap<EITwinModelType, TSet<FString>>& InCurrentLayers)
{
	for (EITwinModelType LayerType : { EITwinModelType::GlobalMapLayer,
									   EITwinModelType::IModel,
									   EITwinModelType::RealityData })
	{
		FITwinClippingInfluenceInfo& InfluenceInfo = MutableInfluenceInfo(LayerType);
		if (InfluenceInfo.bInfluenceAll)
		{
			InfluenceInfo.bInfluenceAll = false;
			const TSet<FString>* CurrentLayers = InCurrentLayers.Find(LayerType);
			if (CurrentLayers)
			{
				InfluenceInfo.SpecificIDs = *CurrentLayers;
			}
		}
	}
}

bool FITwinClippingInfoBase::ShouldInfluenceFullModelType(EITwinModelType ModelType) const
{
	FITwinClippingInfluenceInfo const& InfluenceInfo = GetInfluenceInfo(ModelType);
	return InfluenceInfo.bInfluenceAll;
}

void FITwinClippingInfoBase::SetInfluenceFullModelType(EITwinModelType ModelType, bool bAll)
{
	FITwinClippingInfluenceInfo& InfluenceInfo = MutableInfluenceInfo(ModelType);
	if (InfluenceInfo.bInfluenceAll != bAll)
	{
		InfluenceInfo.bInfluenceAll = bAll;
		InvalidateInfluenceBoundingBox();
	}
}

void FITwinClippingInfoBase::SetInfluenceSpecificModel(const ITwin::ModelLink& ModelIdentifier,	bool bInfluence)
{
	if (DoesInfluenceModel(ModelIdentifier) == bInfluence)
	{
		return;
	}
	FITwinClippingInfluenceInfo& InfluenceInfo = MutableInfluenceInfo(ModelIdentifier.first);
	ensure(!InfluenceInfo.bInfluenceAll);
	if (bInfluence)
	{
		InfluenceInfo.SpecificIDs.FindOrAdd(ModelIdentifier.second);
	}
	else
	{
		InfluenceInfo.SpecificIDs.Remove(ModelIdentifier.second);
	}
	InvalidateInfluenceBoundingBox();
}

void FITwinClippingInfoBase::SetInfluenceNone()
{
	MutableInfluenceInfo(EITwinModelType::IModel).SetInfluenceNone();
	MutableInfluenceInfo(EITwinModelType::RealityData).SetInfluenceNone();
	MutableInfluenceInfo(EITwinModelType::GlobalMapLayer).SetInfluenceNone();
	InvalidateInfluenceBoundingBox();
}

FBox const& FITwinClippingInfoBase::GetInfluenceBoundingBox() const
{
	BE_ASSERT(!bNeedsUpdateBoundingBox);
	return InfluenceBoundingBox;
}

void FITwinClippingInfoBase::InvalidateInfluenceBoundingBox()
{
	bNeedsUpdateBoundingBox = true;
}

void FITwinClippingInfoBase::UpdateInfluenceBoundingBox(UWorld const* World)
{
	InfluenceBoundingBox.Init();

	bool const bInfluenceGoogleTileset = DoesInfluenceModel(ITwin::GetGoogleTilesetLink());
	FBox GoogleTilesetBox;

	// Bounding box enclosing all tilesets but the Google one.
	FBox SceneBoundingBox;

	int32 NumInfluencedTilesets = 0;
	bool bMissingBoundingBox = false;
	ITwin::IterateAllITwinTilesets([this,
									&GoogleTilesetBox,
									&SceneBoundingBox,
									&NumInfluencedTilesets,
									&bMissingBoundingBox](FITwinTilesetAccess const& TilesetAccess)
	{
		ITwin::ModelLink const ModelLink(TilesetAccess.GetModelLink());
		if (ModelLink.first == EITwinModelType::GlobalMapLayer)
		{
			// For Google tileset, we prefer using the bounding box enclosing all other models, if any.
			// So we store the tileset bounding box in a separate variable, and only use it if we don't
			// have any valid bounding box from other models.
			GoogleTilesetBox = TilesetAccess.GetBoundingBox();
		}
		else
		{
			bool const bDoesInfluenceModel = DoesInfluenceModel(ModelLink);
			if (bDoesInfluenceModel)
			{
				NumInfluencedTilesets++;
			}
			FBox const TilesetBox = TilesetAccess.GetBoundingBox();
			if (TilesetBox.IsValid)
			{
				SceneBoundingBox += TilesetBox;

				if (bDoesInfluenceModel)
				{
					InfluenceBoundingBox += TilesetBox;
				}
			}
			else if (bDoesInfluenceModel)
			{
				// The tileset is probably still being loaded => allow recomputing the influence bounding box
				// later when we have a valid one.
				bMissingBoundingBox = true;
			}
		}

	}, World);

	// If the cutout is set to influence nothing, use the scene bounding box by default:
	if (NumInfluencedTilesets == 0)
	{
		InfluenceBoundingBox = SceneBoundingBox;
	}

	// For scenes with only a Google tileset, use the tileset bounding box as influence bounding box.
	// This is not ideal as the Google tileset bounding box is huge...
	if (bInfluenceGoogleTileset && !InfluenceBoundingBox.IsValid && GoogleTilesetBox.IsValid)
	{
		// Scene with only a Google tileset: use the (huge) bounding box of the corresponding tileset.
		InfluenceBoundingBox = GoogleTilesetBox;
	}

	bNeedsUpdateBoundingBox = !InfluenceBoundingBox.IsValid || bMissingBoundingBox;

#if ENABLE_DRAW_DEBUG
	// Draw influence bounding box for a few seconds for debugging
	if (InfluenceBoundingBox.IsValid)
	{
		FVector Center, Extent;
		InfluenceBoundingBox.GetCenterAndExtents(Center, Extent);
		DrawDebugBox(World, Center, Extent, FColor::Green,
			/*bool bPersistent =*/ false, /*float LifeTime =*/ 30.f);
	}
#endif // ENABLE_DRAW_DEBUG
}

FBox const& FITwinClippingInfoBase::GetUpToDateInfluenceBoundingBox(UWorld const* World)
{
	if (bNeedsUpdateBoundingBox)
	{
		UpdateInfluenceBoundingBox(World);
	}
	return InfluenceBoundingBox;
}

void FITwinClippingInfoBase::SetSceneLinkId(AdvViz::SDK::RefID const& InSceneLinkId)
{
	SceneLinkId = InSceneLinkId;
}

void FITwinClippingInfoBase::RecordTileExcluder(UITwinTileExcluderBase* Excluder)
{
	if (Excluder)
	{
		BE_ASSERT(!TileExcluders.Contains(Excluder));
		TileExcluders.Add(Excluder);
	}
}
