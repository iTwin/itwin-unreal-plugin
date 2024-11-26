/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinUtilityLibrary.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinUtilityLibrary.h>
#include <ITwinRuntime/Private/Compil/SanitizedPlatformHeaders.h>
#include <ITwinWebServices/ITwinWebServices_Info.h>
#include <Kismet/KismetMathLibrary.h>
#include <CesiumGeospatial/LocalHorizontalCoordinateSystem.h>
#include <ITwinGeolocation.h>
#include <ITwinCesiumGeoreference.h>
#include <ITwinIModel.h>
#include <ITwinCesium3DTileset.h>
#include <GameFramework/Pawn.h>
#include <Engine/GameViewportClient.h>

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

/// [iModel spatial coords]->[ECEF space]
/// - If the iModel is geolocated, then the transform is given by the iModel.
/// - If the iModel is not geolocated, the mesh export service creates a hard-coded fake geolocation
///   by locating the center of the "project extents" at latitude & longitude 0.
///
/// Note: According to https://www.itwinjs.org/reference/core-common/imodels/eceflocationprops/,
/// we should use the Transform member if valid, then the xVector & yVector fields if valid,
/// otherwise use Origin & Orientation.
/// Here we use Origin & Orientation since they seem to be always valid.
/// TODO: follow order of precedence above.
FTransform UITwinUtilityLibrary::GetIModelToEcefTransform(const AITwinIModel* IModel)
{
	namespace CesiumGS = CesiumGeospatial;
	if (IModel->GetEcefLocation())
	{
		return FTransform(
			UITwinUtilityLibrary::ConvertRotator_ITwinToUnreal(IModel->GetEcefLocation()->Orientation),
			IModel->GetEcefLocation()->Origin);
	}
	else
	{
		return (IModel->GetProjectExtents()
				? FTransform(FRotator::ZeroRotator,
							 -0.5 * (IModel->GetProjectExtents()->Low + IModel->GetProjectExtents()->High))
				: FTransform())
			* UKismetMathLibrary::Conv_MatrixToTransform(UITwinUtilityLibrary::ConvertMatrix_GlmToUnreal(
				CesiumGS::LocalHorizontalCoordinateSystem(
					CesiumGS::Ellipsoid::WGS84.cartographicToCartesian(CesiumGS::Cartographic(0, 0)))
				.getLocalToEcefTransformation()));
	}
}

/// See GetIModelToEcefTransform
FTransform UITwinUtilityLibrary::GetEcefToIModelTransform(const AITwinIModel* IModel)
{
	namespace CesiumGS = CesiumGeospatial;
	if (IModel->GetEcefLocation())
	{
		return FTransform(
			UITwinUtilityLibrary::ConvertRotator_ITwinToUnreal(IModel->GetEcefLocation()->Orientation),
			IModel->GetEcefLocation()->Origin)
		.Inverse();
	}
	else
	{
		return UKismetMathLibrary::Conv_MatrixToTransform(ConvertMatrix_GlmToUnreal(
				CesiumGS::LocalHorizontalCoordinateSystem(
					CesiumGS::Ellipsoid::WGS84.cartographicToCartesian(CesiumGS::Cartographic(0, 0)))
				.getEcefToLocalTransformation()))
			* (IModel->GetProjectExtents()
				? FTransform(FRotator::ZeroRotator,
							 0.5 * (IModel->GetProjectExtents()->Low + IModel->GetProjectExtents()->High))
				: FTransform());
	}
}

FTransform UITwinUtilityLibrary::GetEcefToUnrealTransform(const AITwinIModel* IModel)
{
	// [ECEF space]->[Unreal space].
	return UKismetMathLibrary::Conv_MatrixToTransform(
		IModel->GetTileset()->GetGeoreference()->ComputeEarthCenteredEarthFixedToUnrealTransformation());
}

FTransform UITwinUtilityLibrary::GetUnrealToEcefTransform(const AITwinIModel* IModel)
{
	return UKismetMathLibrary::Conv_MatrixToTransform(
		IModel->GetTileset()->GetGeoreference()->ComputeUnrealToEarthCenteredEarthFixedTransformation());
}

FTransform UITwinUtilityLibrary::StandardizeAndFixAngles(FTransform Transform)
{
	// "Standardize" transform, otherwise camera (in case of saved view) orientation will be wrong for some
	// reason (clamping of rotations?).
	Transform = UKismetMathLibrary::Conv_MatrixToTransform(
		UKismetMathLibrary::Conv_TransformToMatrix(Transform));
	// Another fix for when saved view looks perfectly up or down (ie. pitch is +-90).
	// In this case, and although 3D apps (eg Design Review) generally do not allow to have a non-zero roll
	// angle, it may happen that we end up with a non-zero roll here.
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
	// RQ: replaced check by ensure here to avoid crash
	ensure(std::abs(Rotator.Roll) < 1e-5);
	return Transform;
}

FTransform UITwinUtilityLibrary::GetIModelToUnrealTransform(const AITwinIModel* IModel)
{
	return StandardizeAndFixAngles(GetIModelToEcefTransform(IModel) * GetEcefToUnrealTransform(IModel));
}

FTransform UITwinUtilityLibrary::GetSavedViewUnrealTransform(const AITwinIModel* IModel,
															 const FSavedView& SavedView)
{
	// Note: "iModel spatial coordinates" is the dedicated term for the native iModel cartesian coordinate
	// system (see https://www.itwinjs.org/learning/glossary/#spatial-coordinate-system and
	// https://www.itwinjs.org/learning/geolocation/), in which are expressed the globalOrigin & projectExtents
	// properties of iModels (https://www.itwinjs.org/reference/core-common/imodels/imodel/)
	//
	// We have to build a transform that converts from Unreal camera space to Unreal world space.
	// This is done by combining these transforms:
	// [Unreal camera space]->[IModel camera space]->[iModel spatial coords]->[ECEF space]->[Unreal space].
	auto Transform =
		// [Unreal camera space with camera pointing towards X+ (Unreal convention)]
		// ->[Unreal camera space with camera pointing towards Z- (ITwin convention)]
		FTransform(FRotator(-90, 0, -90))
		// [Left-handed]->[Right-handed], thus getting IModel camera space.
		* FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector(1, -1, 1))
		// [IModel camera space]->[iModel spatial coords]
		// Note: matrix built from camera angles transform from world space to camera space,
		// and we want to convert from camera space to world space, hence the inverse. 
		* FTransform(
			ConvertRotator_ITwinToUnreal(SavedView.Angles).GetInverse(),
			SavedView.Origin)
		* GetIModelToEcefTransform(IModel);
	return StandardizeAndFixAngles(Transform
		* GetEcefToUnrealTransform(IModel)
		// "Add" the manual transformation applied to the iModel tileset,
		// so that the camera position is consistent relative to the iModel.
		* IModel->GetTileset()->GetTransform());
}

void UITwinUtilityLibrary::GetIModelBaseFromUnrealTransform(const AITwinIModel* IModel,
	const FTransform& Transform, FVector& Location_ITwin, FRotator& Rotation_ITwin)
{
	auto ITwinTransform =
		FTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector(1, -1, 1))
		* FTransform(FRotator(0, 90, 90))
		* Transform * GetUnrealToEcefTransform(IModel)
		* GetEcefToIModelTransform(IModel);
	Location_ITwin = ITwinTransform.GetLocation();
	Rotation_ITwin = ITwinTransform.Rotator().GetInverse();
	Rotation_ITwin.Roll *= -1.0;
	//This seems to invert the matrix->transfo->matrix trick?
	Rotation_ITwin.Yaw += 180.0;
}

void UITwinUtilityLibrary::GetSavedViewFrustumFromUnrealTransform(const AITwinIModel* IModel, 
	const FTransform& Transform, FSavedView& SavedView)
{
	//0. Get current camera positon/direction
	APlayerController const* PlayerController = IModel->GetWorld()->GetFirstPlayerController();
	if (!PlayerController)
		return;
	FVector CamPosition = Transform.GetTranslation();
	FVector CamDir = Transform.GetUnitAxis(EAxis::X);
	ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	if (!LocalPlayer || !LocalPlayer->ViewportClient)
		return;
	//1. Find center (x,y) of the viewport screen in pixels
	FVector2D ViewportSize, ScreenPosition;
	LocalPlayer->ViewportClient->GetViewportSize(ViewportSize);
	float AspectRatio = (float)ViewportSize.X / ViewportSize.Y;
	ScreenPosition = 0.5 * ViewportSize;
	//2. Get world coords of center screen position
	FVector WorldLoc, WorldDir;
	if (!PlayerController->DeprojectScreenPositionToWorld(ScreenPosition.X, ScreenPosition.Y,
		WorldLoc, WorldDir))
		return;
	//3. Get coords of intersection between CamDir and IModel
	TArray<FHitResult> AllHits;
	FComponentQueryParams queryParams;
	queryParams.bReturnFaceIndex = true;
	FVector::FReal const TraceExtent = 1000 * 100 /*ie 1km*/;
	FVector const TraceStart = WorldLoc;
	FVector const TraceEnd = WorldLoc + WorldDir * TraceExtent;
	bool bFrontIntersectionFound = IModel->GetWorld()->LineTraceMultiByObjectType(AllHits, TraceStart, TraceEnd,
		FCollisionObjectQueryParams::AllObjects, queryParams);
	TArray<FHitResult> BackHits;
	bool bHasBackHits = IModel->GetWorld()->LineTraceMultiByObjectType(BackHits, TraceEnd, TraceStart,
		FCollisionObjectQueryParams::AllObjects, queryParams);
	if (bHasBackHits && !bFrontIntersectionFound)
		AllHits = BackHits;
	//4. Compute coords of the "origin" of the view frustum (left bottom point of the far plane)
	bool bIntersectionFound = bFrontIntersectionFound || bHasBackHits;
	FVector TargetPoint = bIntersectionFound? AllHits[0].ImpactPoint:WorldLoc;
	FVector Normal = -CamDir;
	ScreenPosition = ViewportSize * FVector2D(0, 1);
	PlayerController->DeprojectScreenPositionToWorld(ScreenPosition.X, ScreenPosition.Y,
		WorldLoc, WorldDir);
	FVector Inter = bIntersectionFound?
		FMath::LinePlaneIntersection(WorldLoc, WorldLoc + WorldDir * 10000.0, TargetPoint, Normal):WorldLoc;	
	float FocusDist_UE = FVector::Dist(TargetPoint, CamPosition);
	float FocusDist_ITwin = FocusDist_UE / 100.;
	SavedView.FocusDist = FocusDist_ITwin;
	SavedView.Extents = { 2.0 * FocusDist_ITwin,
		(2.0 * FocusDist_ITwin) / AspectRatio, FocusDist_ITwin };
	FRotator DummyRotator = FRotator::ZeroRotator;
	FVector FarBottomLeft;
	UITwinUtilityLibrary::GetIModelBaseFromUnrealTransform(IModel, 
		FTransform(Inter)*IModel->GetTileset()->GetTransform().Inverse(), 
		FarBottomLeft, DummyRotator);
	SavedView.FrustumOrigin = FarBottomLeft;
}

namespace
{
	APawn const* GetPlayerControllerPawn(const AITwinIModel* IModel)
	{
		UWorld* World = IModel->GetWorld();
		APlayerController const* Controller = World ? World->GetFirstPlayerController() : nullptr;
		APawn const* Pawn = Controller ? Controller->GetPawn() : nullptr;
		return Pawn;
	}
}

void UITwinUtilityLibrary::GetSavedViewFrustumFromPlayerController(const AITwinIModel* IModel,
																   FSavedView& SavedView)
{
	APlayerController const* PlayerController = IModel->GetWorld()->GetFirstPlayerController();
	if (!PlayerController)
		return;
	APawn const* Pawn = PlayerController->GetPawnOrSpectator();
	if (!Pawn)
		return;
	GetSavedViewFrustumFromUnrealTransform(IModel, Pawn->GetActorTransform(), SavedView);
}

FSavedView UITwinUtilityLibrary::GetSavedViewFromUnrealTransform(const AITwinIModel* IModel,
																 const FTransform& Transform)
{
	FSavedView SavedView;
	GetSavedViewFrustumFromUnrealTransform(IModel, Transform, SavedView);
	GetIModelBaseFromUnrealTransform(IModel,
		// "Remove" the manual transformation applied to the iModel tileset.
		// so that the camera position is consistent relative to the iModel.
		Transform*IModel->GetTileset()->GetTransform().Inverse(), 
		SavedView.Origin, SavedView.Angles);
	return SavedView;
}

bool UITwinUtilityLibrary::GetSavedViewFromPlayerController(const AITwinIModel* IModel,
															FSavedView& OutSavedView)
{
	APawn const* Pawn = GetPlayerControllerPawn(IModel);
	if (!Pawn)
		return false;
	const auto& Location_UE = Pawn->GetActorLocation();
	const auto& Rotation_UE = Pawn->GetActorRotation();
	auto Transform = FTransform(Rotation_UE, Location_UE);
	OutSavedView = GetSavedViewFromUnrealTransform(IModel, Transform);
	return true;
}