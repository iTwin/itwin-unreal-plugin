/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinUtilityLibrary.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinUtilityLibrary.h>
#include <ITwinRuntime/Private/Compil/SanitizedPlatformHeaders.h>
#include <iTwinWebServices/ITwinWebServices_Info.h>
#include <Kismet/KismetMathLibrary.h>
#include <CesiumGeospatial/LocalHorizontalCoordinateSystem.h>
#include <ITwinGeolocation.h>
#include <ITwinCesiumGeoreference.h>
#include <ITwinIModel.h>
#include <ITwinCesium3DTileset.h>
#include <GameFramework/Pawn.h>

FRotator UITwinUtilityLibrary::ConvertRotator_ITwinToUnreal(const FRotator& ITwinRotator)
{
	// Roll must be negated.
	// This has been deduced from comparing the implementations of the functions that transform angles to matrix:
	// - iTwin: YawPitchRollAngles.toMatrix3d()
	// - Unreal: TRotationTranslationMatrix ctor
	// With this adjustment, both functions return same matrix.
	return {ITwinRotator.Pitch, ITwinRotator.Yaw, -ITwinRotator.Roll};
}

FMatrix UITwinUtilityLibrary::ConvertMatrix_GlmToUnreal(const glm::dmat4& m)
{
	return FMatrix(
		FVector(m[0].x, m[0].y, m[0].z),
		FVector(m[1].x, m[1].y, m[1].z),
		FVector(m[2].x, m[2].y, m[2].z),
		FVector(m[3].x, m[3].y, m[3].z));
}

FTransform UITwinUtilityLibrary::GetSavedViewUnrealTransform(const AITwinIModel* IModel, const FSavedView& SavedView)
{
	// We have to build a transform that converts from Unreal camera space to Unreal world space.
	// This is done by combining these transforms:
	// [Unreal camera space]->[ITwin camera space]->[ITwin world space]->[ECEF space]->[Unreal space].
	auto Transform =
		// [Unreal camera space with camera pointing towards X+ (Unreal convention)]
		// ->[Unreal camera space with camera pointing towards Z- (ITwin convention)]
		FTransform(FRotator(-90, 0, -90))
		// [Left-handed]->[Right-handed], thus getting ITwin camera space.
		*FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector(1,-1,1))
		// [ITwin camera space]->[ITwin world space]
		// Note: matrix built from camera angles transform from world space to camera space,
		// and we want to convert from camera space to world space, hence the inverse. 
		*FTransform(FRotator(SavedView.Angles.Pitch, SavedView.Angles.Yaw, -SavedView.Angles.Roll).GetInverse(),
			SavedView.Origin)
		// [ITwin world space]->[ECEF space]
		// - If the iModel is geolocated, then the transform is given by the iModel.
		// - If the iModel is not geolocated, the mesh export service creates a hard-coded fake geolocation
		//   by locating the center of the "project extents" at latitude & longitude 0.
		*(IModel->GetEcefLocation() ?
			// Note: According to https://www.itwinjs.org/reference/core-common/imodels/eceflocationprops/,
			// we should use the Transform member if valid, then the xVector & yVector fields if valid,
			// otherwise use Origin & Orientation.
			// Here we use Origin & Orientation since they seem to be always valid.
			// TODO: follow order of precedence above.
			FTransform(ConvertRotator_ITwinToUnreal(IModel->GetEcefLocation()->Orientation), IModel->GetEcefLocation()->Origin) :
			// See comment above: center of the "project extents" is located at latitude & longitude 0.
			(IModel->GetProjectExtents() ?
				FTransform(FRotator::ZeroRotator, -0.5*(IModel->GetProjectExtents()->Low+IModel->GetProjectExtents()->High)):
				FTransform())
			*UKismetMathLibrary::Conv_MatrixToTransform(ConvertMatrix_GlmToUnreal(
				CesiumGeospatial::LocalHorizontalCoordinateSystem(
					CesiumGeospatial::Ellipsoid::WGS84.cartographicToCartesian(CesiumGeospatial::Cartographic(0,0)))
					.getLocalToEcefTransformation())))
		// [ECEF space]->[Unreal space].
		*UKismetMathLibrary::Conv_MatrixToTransform(
			IModel->GetTileset()->GetGeoreference()->ComputeEarthCenteredEarthFixedToUnrealTransformation());
	// "Standardize" transform, otherwise camera orientation will be wrong for some reason (clamping of rotations?).
	Transform =  UKismetMathLibrary::Conv_MatrixToTransform(UKismetMathLibrary::Conv_TransformToMatrix(Transform));
	// Another fix for when saved view looks perfectly up or down (ie. pitch is +-90).
	// In this case, and although 3D apps (eg Design Review) generally do not allow to have a non-zero roll angle,
	// it may happen that we end up with a non-zero roll here.
	// Indeed, when pitch is +-90, yaw and roll have same effect (this is the "gimbal lock" phenomenon).
	// Problem is, Unreal does not correctly handle case when camera roll is non-zero, for example
	// camera roll may just be ignored, leading to incorrect orientation.
	// So, what we do is just add the roll to the yaw and reset the roll.
	// Also, even after this fix is done, having the pitch at +-90 may still cause issues, as if Unreal
	// introduces a non-zero roll which completely breaks manual orientation using the mouse.
	// So as a workaround we slightly offset the pitch.
	auto Rotator = Transform.Rotator();
	constexpr auto MaxPitch = 90-1e-5;
	if (std::abs(Rotator.Pitch) > MaxPitch)
	{
		// Saved view is looking perfectly up or down.
		// Offset pitch as explained above.
		Rotator.Pitch = std::clamp(Rotator.Pitch, -MaxPitch, MaxPitch);
		// Transfer roll to yaw as explained above.
		Rotator.Yaw += Rotator.Roll;
		Rotator.Roll = 0;
		Transform.SetRotation(FQuat(Rotator));
	}
	// If this assert is triggered, this means the saved view has a non-null roll but is not looking
	// perfectly up or down. This case is not handled.
	check(std::abs(Rotator.Roll) < 1e-5);
	return Transform;
}

FSavedView UITwinUtilityLibrary::GetSavedViewFromUnrealTransform(const AITwinIModel* IModel, const FTransform& Transform)
{
	FSavedView SavedView;
	const auto& Location_UE = Transform.GetLocation();
	const auto& Rotation_UE = Transform.GetRotation().Rotator();
	auto& Location_ITwin = SavedView.Origin;
	auto& Rotation_ITwin = SavedView.Angles;
	// Convert rotation to iTwin
	Rotation_ITwin = Rotation_UE;
	FRotator const rot(-90.0, -90.0, 0.0);
	Rotation_ITwin = UKismetMathLibrary::ComposeRotators(rot.GetInverse(), Rotation_ITwin);
	Rotation_ITwin = Rotation_ITwin.GetInverse();
	Rotation_ITwin.Yaw *= -1.0;
	// Convert position to iTwin
	auto ITwinTransform = UKismetMathLibrary::Conv_MatrixToTransform(UKismetMathLibrary::Conv_TransformToMatrix(Transform));
	ITwinTransform *= UKismetMathLibrary::Conv_MatrixToTransform(
		IModel->GetTileset()->GetGeoreference()->ComputeUnrealToEarthCenteredEarthFixedTransformation());
	ITwinTransform *= UKismetMathLibrary::Conv_MatrixToTransform(ConvertMatrix_GlmToUnreal(
		CesiumGeospatial::LocalHorizontalCoordinateSystem(
			CesiumGeospatial::Ellipsoid::WGS84.cartographicToCartesian(CesiumGeospatial::Cartographic(0, 0)))
		.getEcefToLocalTransformation()));
	ITwinTransform *= FTransform(FRotator::ZeroRotator, 0.5 * (IModel->GetProjectExtents()->Low + IModel->GetProjectExtents()->High));
	Location_ITwin = ITwinTransform.GetLocation();
	return SavedView;
}

bool UITwinUtilityLibrary::GetSavedViewFromPlayerController(const AITwinIModel* IModel, FSavedView& OutSavedView)
{
	UWorld* World = IModel->GetWorld();
	APlayerController const* Controller = World ? World->GetFirstPlayerController() : nullptr;
	APawn const* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Pawn)
		return false;
	const auto& Location_UE = Pawn->GetActorLocation();
	const auto& Rotation_UE = Pawn->GetActorRotation();
	auto Transform = FTransform(Rotation_UE, Location_UE);
	OutSavedView = GetSavedViewFromUnrealTransform(IModel, Transform);
	return true;
}