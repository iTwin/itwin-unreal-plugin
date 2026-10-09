/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinClippingToolApi.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <Clipping/ITwinClippingEnums.h>
#include <ITwinModelType.h>
#include <Containers/Set.h>
#include <Math/Transform.h>

#include <optional>
#include <utility>

enum class ETransformationMode : uint8;

namespace AdvViz::SDK
{
	class RefID;
}

/// Abstract view of AITwinClippingTool, restricted to what the iTwin Studio cross-language
/// layer (see ClippingItsProxy.cpp) actually needs.
///
/// This exists so that the cross-language layer can be exercised without a UWorld, without
/// spawning the tool actor, and without loading the ClippingBox/ClippingPlane population
/// assets. Production code must keep using AITwinClippingTool directly; only the cross-lang
/// entry points go through this interface.
///
/// \par Effect addressing
/// An effect is addressed by a (type, index) pair, where the index is its position in the
/// list of effects of that type. Indices are NOT stable: removing an effect shifts the
/// indices of the following ones. Any identifier that must survive across calls (typically
/// an id sent to iTwin Studio) has to be built from #GetEffectId instead.
///
/// \par The bTriggeredFromITS convention
/// Commands that can originate either from the 3D viewport or from the iTwin Studio panel
/// take a bTriggeredFromITS flag. When true, the tool must not notify iTwin Studio back
/// about the change: the panel already knows, and echoing it would cause a feedback loop.
/// The cross-language entry points therefore always pass true.
///
/// \par No default arguments
/// None of the methods below declare default arguments, on purpose: default arguments on
/// virtual functions are bound statically, so an override silently redefining them is a
/// classic source of bugs. Call sites pass every argument explicitly.
class IITwinClippingToolApi
{
public:
	/// Identifies an effect by its primitive type and its index within that type.
	using FEffectIdentifier = std::pair<EITwinClippingPrimitiveType, int32>;

	virtual ~IITwinClippingToolApi() = default;

	//-----------------------------------------------------------------------------------
	// Queries
	//-----------------------------------------------------------------------------------

	/// Return the number of clipping effects for the given primitive type.
	virtual int32 NumEffects(EITwinClippingPrimitiveType Type) const = 0;

	/// Returns the unique identifier of an effect from its index.
	/// \return An invalid RefID if the index is out of range, or if the effect has not been
	/// assigned an identifier yet (which happens for effects not yet persisted in the scene).
	/// Callers must test AdvViz::SDK::RefID::IsValid() before using the result.
	virtual AdvViz::SDK::RefID GetEffectId(EITwinClippingPrimitiveType EffectType,
		int32 EffectIndex) const = 0;

	/// Returns the index of a given effect from its unique identifier.
	/// \return INDEX_NONE if no effect of that type carries this identifier, which typically
	/// means the effect has been removed since the identifier was handed out.
	virtual int32 GetEffectIndex(EITwinClippingPrimitiveType EffectType,
		AdvViz::SDK::RefID const& RefID) const = 0;

	/// Returns a pair identifying the selected effect, if any.
	/// \return std::nullopt when nothing is selected.
	virtual std::optional<FEffectIdentifier> GetSelectedEffect() const = 0;

	/// Return whether the given effect is enabled.
	/// A disabled effect keeps all its settings (influence, transform, invert flag) but stops
	/// cutting anything out.
	virtual bool IsEffectEnabled(EITwinClippingPrimitiveType EffectType, int32 Index) const = 0;

	/// Return whether the given effect is inverted, ie. whether it cuts out what is outside
	/// the primitive instead of what is inside it.
	virtual bool GetInvertEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex) const = 0;

	/// Return the identifiers of the individual layers of the given type that this effect
	/// influences.
	/// Beware this only reports layers influenced *individually*: if the effect influences the
	/// whole layer type at once, the returned set may be empty even though every layer of that
	/// type is in fact influenced.
	virtual TSet<FString> GetInfluencedSpecificModels(EITwinClippingPrimitiveType EffectType,
		int32 EffectIndex, EITwinModelType LayerType) const = 0;

	/// Return whether the given effect does influence the model specified by the given identifier.
	/// Beware that it does not take the enabled state of the effect into account: if the effect
	/// is disabled, this will still return true if the effect would influence the model.
	virtual bool DoesEffectInfluenceModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		const ITwin::ModelLink& ModelIdentifier) const = 0;

	/// Return the placement of the given effect, both as a raw Unreal transform and as
	/// geographic coordinates.
	/// \param OutTransform The effect transform in world coordinates. Its rotation is what the
	/// iTwin Studio panel exposes as the three Euler angles.
	/// \param OutLatitude, OutLongitude, OutElevation The geographic position of the effect center.
	/// \return False if the effect index is out of range, or if the position could not be
	/// converted to geographic coordinates (typically when no geo-reference is available).
	/// The output parameters are then left untouched.
	virtual bool GetEffectTransform(EITwinClippingPrimitiveType EffectType, int32 Index,
		FTransform& OutTransform, double& OutLatitude, double& OutLongitude,
		double& OutElevation) const = 0;

	/// Return the index of the selected polygon point, if any (if a cutout polygon point is
	/// selected) and if yes, fills its coordinates (latitude and longitude).
	/// If no polygon is selected, or if none of its points is selected, INDEX_NONE is returned.
	virtual int32 GetSelectedPolygonPointInfo(double& OutLatitude, double& OutLongitude) const = 0;

	/// Whether something is currently listening to changes of the effect list, ie. whether the
	/// tool has been connected to the iTwin Studio proxy (see UClippingItsProxy::ConnectClippingTool).
	/// Used to detect a mis-wired proxy, which would otherwise silently stop refreshing the panel.
	virtual bool HasEffectListListener() const = 0;

	//-----------------------------------------------------------------------------------
	// Commands
	//-----------------------------------------------------------------------------------

	/// Initiate the interactive creation of a new effect.
	/// This does not create the effect immediately: it puts the tool in a mode where the next
	/// user interaction in the viewport places the new primitive. The creation can be abandoned,
	/// in which case the tool notifies iTwin Studio through the creation-aborted event.
	/// \return True if the creation mode could be entered, false if the tool is not in a state
	/// that allows it (typically because the maximum number of primitives of that type is reached).
	virtual bool StartInteractiveEffectCreation(EITwinClippingPrimitiveType Type) = 0;

	/// Remove an individual clipping primitive. Returns true if the effect was actually removed.
	/// \param bTriggeredFromITS See the class-level note on this convention.
	/// Beware this shifts the indices of the effects following the removed one.
	virtual bool RemoveEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex,
		bool bTriggeredFromITS) = 0;

	/// Select the effect of given type and index.
	/// \param bEnterIsolationMode When true, we enter isolation mode, by hiding all the other proxies.
	/// \return True if the effect could be selected.
	virtual bool SelectEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex,
		bool bEnterIsolationMode) = 0;

	/// Reset current selection to none.
	/// \param bExitIsolationMode When true, and if there was currently an isolation mode, we exit
	/// it by restoring the normal visibility of effect proxies.
	virtual void DeSelectAll(bool bExitIsolationMode) = 0;

	/// Switches the given effect on or off.
	virtual void EnableEffect(EITwinClippingPrimitiveType EffectType, int32 Index, bool bInEnabled) = 0;

	/// Enable or disable all effects, of all primitive types at once.
	virtual void EnableAllEffects(bool bInEnabled) = 0;

	/// Invert the given effect, ie. make it cut out what is outside the primitive instead of what
	/// is inside it. Setting the flag to the value it already has is a no-op.
	virtual void SetInvertEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex, bool bInvert) = 0;

	/// Set whether the given effect should influence the model specified by the given identifier.
	/// This addresses one individual layer; it is not meant to be used while the effect influences
	/// the whole layer type.
	virtual void SetEffectInfluenceModel(EITwinClippingPrimitiveType EffectType, int32 EffectIndex,
		const ITwin::ModelLink& ModelIdentifier, bool bInfluence) = 0;

	/// Move the given effect to the given geographic position, keeping its current orientation.
	/// \param InLatitude, InLongitude, InElevation The new position of the effect center.
	/// \param bTriggeredFromITS See the class-level note on this convention.
	virtual void SetEffectLocation(EITwinClippingPrimitiveType EffectType, int32 Index,
		double InLatitude, double InLongitude, double InElevation, bool bTriggeredFromITS) = 0;

	/// Reorient the given effect, keeping its current position.
	/// \param InRotX, InRotY, InRotZ The new orientation, as Euler angles in degrees, in the same
	/// convention as the angles returned by #GetEffectTransform.
	/// \param bTriggeredFromITS See the class-level note on this convention.
	virtual void SetEffectRotation(EITwinClippingPrimitiveType EffectType, int32 Index,
		double InRotX, double InRotY, double InRotZ, bool bTriggeredFromITS) = 0;

	/// Modify the location of the selected cutout polygon point, if any.
	/// \param PolygonIndex Index of the polygon effect owning the point.
	/// \param PointIndex Index of the point inside that polygon.
	/// Only meaningful for EITwinClippingPrimitiveType::Polygon effects.
	virtual void SetPolygonPointLocation(int32 PolygonIndex, int32 PointIndex,
		double Latitude, double Longitude) = 0;

	/// Set the transformation mode (for selection gizmo), ie. whether the gizmo translates,
	/// rotates or scales the selected effect.
	virtual void SetTransformationMode(ETransformationMode Mode) = 0;

	/// Zoom in on the effect of given type and index.
	/// Does nothing if the effect index is out of range.
	virtual void ZoomOnEffect(EITwinClippingPrimitiveType Type, int32 PrimitiveIndex) = 0;

	/// Change the view camera so that the cutout polygons can be edited from top, framing all of
	/// them at once.
	/// Named differently from AITwinClippingTool::OnOverviewCamera, which is a UFUNCTION taking an
	/// optional spline argument and must therefore not be turned into an interface override
	/// (UHT does not cope well with defaulted arguments on virtual functions).
	virtual void SetOverviewCamera() = 0;

	/// Deactivate the cutout tool. This also aborts any cutout creation, if any.
	virtual void Deactivate() = 0;
};
