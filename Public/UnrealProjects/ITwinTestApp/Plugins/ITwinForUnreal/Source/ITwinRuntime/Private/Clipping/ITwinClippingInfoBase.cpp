/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingInfoBase.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Clipping/ITwinClippingInfoBase.h>

#include <Clipping/ITwinTileExcluderBase.h>
#include <ITwinTilesetAccess.h>

#include <Compil/BeforeNonUnrealIncludes.h>
#	include <BeHeaders/Compil/EnumSwitchCoverage.h>
#include <Compil/AfterNonUnrealIncludes.h>


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

inline FITwinClippingInfluenceInfo& FITwinClippingInfoBase::MutableInfluenceInfo(EITwinModelType ModelType)
{
	switch (ModelType)
	{
	BE_UNCOVERED_ENUM_ASSERT_AND_FALLTHROUGH(
	case EITwinModelType::AnimationKeyframe:
	case EITwinModelType::Scene:
	case EITwinModelType::Invalid:)

	case EITwinModelType::GlobalMapLayer: return GlobalMapLayersInfluenceInfo;
	case EITwinModelType::IModel: return IModelInfluenceInfo;
	case EITwinModelType::RealityData: return RealityDataInfluenceInfo;
	}
}

inline FITwinClippingInfluenceInfo const& FITwinClippingInfoBase::GetInfluenceInfo(EITwinModelType ModelType) const
{
	return const_cast<FITwinClippingInfoBase*>(this)->MutableInfluenceInfo(ModelType);
}

bool FITwinClippingInfoBase::ShouldInfluenceModel(const ITwin::ModelLink& ModelIdentifier) const
{
	if (IsEnabled())
	{
		FITwinClippingInfluenceInfo const& InfluenceInfo = GetInfluenceInfo(ModelIdentifier.first);
		return InfluenceInfo.bInfluenceAll || InfluenceInfo.SpecificIDs.Contains(ModelIdentifier.second);
	}
	else
	{
		return false;
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
	if (ShouldInfluenceModel(ModelIdentifier) == bInfluence)
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

void FITwinClippingInfoBase::UpdateInfluenceBoundingBox(UWorld* World)
{
	InfluenceBoundingBox.Init();
	bool bSetInvalidBBox = false;
	ITwin::IterateAllITwinTilesets([this, &bSetInvalidBBox](FITwinTilesetAccess const& TilesetAccess)
	{
		if (ShouldInfluenceModel(TilesetAccess.GetModelLink()))
		{
			FBox const TilesetBox = TilesetAccess.GetBoundingBox();
			if (TilesetBox.IsValid)
			{
				InfluenceBoundingBox += TilesetBox;
			}
			else
			{
				// If the tileset bounding box is not valid, we set the influence box to invalid, which
				// is equivalent to an infinite box.
				bSetInvalidBBox = true;
			}
		}
	}, World);
	if (bSetInvalidBBox)
		InfluenceBoundingBox.Init();
	bNeedsUpdateBoundingBox = false;
}
