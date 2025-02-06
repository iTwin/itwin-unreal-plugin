/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPopulationTool.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <Population/ITwinPopulationTool.h>
#include <Population/ITwinPopulation.h>
#include <Decoration/ITwinDecorationHelper.h>
#include <ITwinGoogle3DTileset.h>
#include <ITwinRealityData.h>

#include <Engine/StaticMeshActor.h>
#include <Engine/GameViewportClient.h>
#include <Engine/EngineTypes.h>
#include <Engine/StaticMesh.h>
#include <EngineUtils.h> // for TActorIterator<>
#include <Materials/Material.h>
#include <Materials/MaterialInstanceDynamic.h>
#include <Slate/SceneViewport.h>
#include <Kismet/KismetSystemLibrary.h>
#include <Kismet/GameplayStatics.h>

#include <map>
#include <vector>

namespace ITwin
{
	inline bool Is3DMapTileset(const AITwinCesium3DTileset* tileset)
	{
		// Detect both Google3D tilesets and iTwin reality data.
		return IsGoogle3DTileset(tileset)
			||
			(
				tileset->Owner.Get() &&
				tileset->Owner->IsA(AITwinRealityData::StaticClass())
			);
	}

	void Gather3DMapTilesets(const UWorld* World, TArray<AITwinCesium3DTileset*>& Out3DMapTilesets)
	{
		GatherGoogle3DTilesets(World, Out3DMapTilesets);
		// Append iTwin Reality-Data tilesets.
		for (TActorIterator<AITwinRealityData> ItwRealDataIter(World); ItwRealDataIter; ++ItwRealDataIter)
		{
			if ((*ItwRealDataIter)->GetTileset())
			{
				Out3DMapTilesets.Push((*ItwRealDataIter)->GetMutableTileset());
			}
		}
	}
}

#define BRUSH_MESH_INVERSE_RADIUS 6.25e-3f // = 1/160

class AITwinPopulationTool::FImpl
{
public:
	AITwinPopulationTool& owner;
	AITwinDecorationHelper* decorationHelper = nullptr;

	bool enabled = false; // boolean used to switch on or off the population tool

	EPopulationToolMode toolMode = EPopulationToolMode::Select;
	ETransformationMode transformationMode = ETransformationMode::Move;

	AITwinPopulation* selectedPopulation = nullptr;
	int32 selectedInstanceIndex = -1;

	struct BrushFlow
	{
		float computedValue = 1.f;
		float userFactor = 1.f;
		float GetFlow() const { return computedValue * userFactor; }
	};
	AITwinPopulation* draggedAssetPopulation = nullptr;
	int32 draggedAssetInstanceIndex = -1;
	AStaticMeshActor* brushSphere = nullptr;
	float brushRadius = 1000.f; // radius in centimeters
	BrushFlow brushFlow; // number of added instances per m^2 per second.
	float brushLastTime = 0.f;
	FVector brushLastPos = FVector(0);
	float instancesScaleVariation = 0.2f;
	float instancesRotationVariation = UE_PI;
	bool forcePerpendicularToSurface = false;
	bool enableOnRealityData = false;
	bool isBrushingInstances = false;
	bool isEditingBrushSize = false;
	FTransform savedTransform = FTransform::Identity;
	bool savedTransformChanged = true;
	float savedAngleZ = 0.f;
	std::map<FString, bool> usedAssets;
	std::vector<AITwinPopulation*> editedPopulations;
	TArray<AActor*> allPopulations;

	// For the addition of an instance from the browser
	float draggingRotVar = 0.f;
	float draggingScaleVar = 1.f;

	FImpl(AITwinPopulationTool& inOwner);

	// Implementation of AITwinPopulationTool functions
	EPopulationToolMode GetMode() const;
	void SetMode(const EPopulationToolMode& action);
	ETransformationMode GetTransformationMode() const;
	void SetTransformationMode(const ETransformationMode& mode);
	AITwinPopulation* GetSelectedPopulation() const;
	void SetSelectedPopulation(AITwinPopulation* population);
	void SetSelectedInstanceIndex(int32 instanceIndex);
	bool HasSelectedPopulation() const;
	void DeleteSelectedInstance();
	bool IsPopulationModeActivated() const;
	bool IsBrushModeActivated() const;
	void StartBrushingInstances();
	void EndBrushingInstances();
	void ShowBrushSphere();
	void HideBrushSphere();
	void ComputeBrushFlow();
	float GetBrushFlow() const;
	void SetBrushFlow(float flow);
	float GetBrushSize() const;
	void SetBrushSize(float size);
	FTransform GetSelectionTransform() const;
	void SetSelectionTransform(const FTransform& transform);
	FLinearColor GetSelectionColorVariation() const;
	void SetSelectionColorVariation(const FLinearColor& c);
	void SetEnabled(bool value);
	bool IsEnabled() const;
	void ResetToDefault();
	void SetDecorationHelper(AITwinDecorationHelper* decoHelper);
	bool DragActorInLevel(const FVector2D& screenPosition, const FString& assetPath);
	void ReleaseDraggedAssetInstance();
	void DestroyDraggedAssetInstance();
	void SetUsedAsset(const FString& assetPath, bool used);
	void ClearUsedAssets();
	int32 GetInstanceCount(const FString& assetPath) const;
	bool GetForcePerpendicularToSurface() const;
	void SetForcePerpendicularToSurface(bool b);
	bool GetEnableOnRealityData() const;
	void SetEnableOnRealityData(bool b);
	bool GetIsEditingBrushSize() const;
	void SetIsEditingBrushSize(bool b);
	void DoMouseClickAction();
	void Tick(float DeltaTime);

	// Additional internal functions
	void InitBrushSphere();
	bool ComputeTransformFromHitResult(
		const FHitResult& hitResult, FTransform& transform,
		const AITwinPopulation* population, bool isDraggingInstance = false);
	FHitResult LineTraceFromMousePos();
	FVector LineTraceToSetBrushSize();
	void MultiLineTraceFromMousePos(int32 traceCount, std::vector<AITwinPopulation*> populations);
	bool AddSingleInstanceFromHitResult(const FHitResult& hitResult);
	void CollectEditedPopulations();
	void SetBrushPosition(const FVector& position);
	void UpdatePopulationsCollisionType() const;
	void UpdatePopulationsArray();
	void StartDragging(AITwinPopulation* population);
	void DeleteInstanceFromPopulation(AITwinPopulation*& population, int32& instanceIndex);
};

AITwinPopulationTool::FImpl::FImpl(AITwinPopulationTool& inOwner)
	: owner(inOwner)
{
}

EPopulationToolMode AITwinPopulationTool::FImpl::GetMode() const
{
	return toolMode;
}

void AITwinPopulationTool::FImpl::SetMode(const EPopulationToolMode& mode)
{
	if (!IsEnabled())
		return;

	toolMode = mode;

	UpdatePopulationsArray();
	UpdatePopulationsCollisionType();
}

ETransformationMode AITwinPopulationTool::FImpl::GetTransformationMode() const
{
	return transformationMode;
}

void AITwinPopulationTool::FImpl::SetTransformationMode(const ETransformationMode& mode)
{
	transformationMode = mode;
}

AITwinPopulation* AITwinPopulationTool::FImpl::GetSelectedPopulation() const
{
	return selectedPopulation;
}

void AITwinPopulationTool::FImpl::SetSelectedPopulation(AITwinPopulation* population)
{
	selectedPopulation = population;
	selectedInstanceIndex = -1;
}

void AITwinPopulationTool::FImpl::SetSelectedInstanceIndex(int32 instanceIndex)
{
	selectedInstanceIndex = instanceIndex;
}

bool AITwinPopulationTool::FImpl::HasSelectedPopulation() const
{
	return selectedPopulation != nullptr;
}

void AITwinPopulationTool::FImpl::DeleteSelectedInstance()
{
	DeleteInstanceFromPopulation(selectedPopulation, selectedInstanceIndex);
}

bool AITwinPopulationTool::FImpl::IsPopulationModeActivated() const
{
	return toolMode == EPopulationToolMode::Instantiate || IsBrushModeActivated();
}

bool AITwinPopulationTool::FImpl::IsBrushModeActivated() const
{
	return toolMode == EPopulationToolMode::InstantiateN ||
		   toolMode == EPopulationToolMode::RemoveInstances;
}

void AITwinPopulationTool::FImpl::StartBrushingInstances()
{
	if (!IsEnabled())
		return;

	isBrushingInstances = true;
	brushLastTime = owner.GetGameTimeSinceCreation();
	brushLastPos = brushSphere->GetActorLocation();
	
	if (editedPopulations.empty())
	{
		CollectEditedPopulations();
	}
}

void AITwinPopulationTool::FImpl::EndBrushingInstances()
{
	isBrushingInstances = false;
}

void AITwinPopulationTool::FImpl::ShowBrushSphere()
{
	if (brushSphere && IsEnabled())
	{
		brushSphere->SetActorHiddenInGame(false);
	}
}

void AITwinPopulationTool::FImpl::HideBrushSphere()
{
	if (brushSphere)
	{
		brushSphere->SetActorHiddenInGame(true);
	}
}

void AITwinPopulationTool::FImpl::ComputeBrushFlow()
{
	// Compute an appropriate brushFlow for the selected actor that may be instantiated.
	if (selectedPopulation)
	{
		FVector selSize = Cast<AITwinPopulation>(selectedPopulation)->mesh->GetBounds().GetBox().GetSize();
		float selArea = selSize.X * selSize.Y * 1e-4f; // convert cm^2 to m^2
		// Limit the value to 1 instance per m^2 per second (it's enough for characters
		// which are the smallest assets at the moment).
		brushFlow.computedValue = selArea > 1.f ? 1.f/selArea : 1.f;
	}
}

float AITwinPopulationTool::FImpl::GetBrushFlow() const
{
	return brushFlow.userFactor;
}

void AITwinPopulationTool::FImpl::SetBrushFlow(float flow)
{
	if (IsBrushModeActivated())
	{
		brushFlow.userFactor = flow;
	}
}

float AITwinPopulationTool::FImpl::GetBrushSize() const
{
	return brushRadius;
}

void AITwinPopulationTool::FImpl::SetBrushSize(float size)
{
	if (IsBrushModeActivated())
	{
		isEditingBrushSize = true;
		brushRadius = size;
	}
}

FTransform AITwinPopulationTool::FImpl::GetSelectionTransform() const
{
	if (selectedPopulation && selectedInstanceIndex >= 0)
	{
		return selectedPopulation->GetInstanceTransform(selectedInstanceIndex);
	}

	return FTransform();
}

void AITwinPopulationTool::FImpl::SetSelectionTransform(const FTransform& transform)
{
	if (selectedPopulation && selectedInstanceIndex >= 0)
	{
		selectedPopulation->SetInstanceTransform(selectedInstanceIndex, transform);
		if (!selectedPopulation->IsRotationVariationEnabled())
		{
			savedTransform = transform;
			savedTransformChanged = true;
		}
	}
}

FLinearColor AITwinPopulationTool::FImpl::GetSelectionColorVariation() const
{
	FLinearColor color(0.5, 0.5, 0.5);

	if (selectedPopulation && selectedInstanceIndex >= 0)
	{
		FVector v = selectedPopulation->GetInstanceColorVariation(selectedInstanceIndex);

		color.R = v.X + 0.5;
		color.G = v.Y + 0.5;
		color.B = v.Z + 0.5;
	}

	return color;
}

void AITwinPopulationTool::FImpl::SetSelectionColorVariation(const FLinearColor& c)
{
	if (selectedPopulation && selectedInstanceIndex >= 0)
	{
		FVector v(c.R - 0.5, c.G - 0.5, c.B - 0.5);
		selectedPopulation->SetInstanceColorVariation(selectedInstanceIndex, v);
	}
}

void AITwinPopulationTool::FImpl::SetEnabled(bool value)
{
	if (value != enabled)
	{
		enabled = value;

		UpdatePopulationsArray();
		UpdatePopulationsCollisionType();

		if (!enabled)
		{
			SetSelectedPopulation(nullptr);
		}

		if (IsBrushModeActivated())
		{
			if (enabled)
			{
				ShowBrushSphere();
			}
			else
			{
				HideBrushSphere();
			}
		}
	}
}

bool AITwinPopulationTool::FImpl::IsEnabled() const
{
	return enabled && decorationHelper && decorationHelper->IsPopulationEnabled();
}

void AITwinPopulationTool::FImpl::ResetToDefault()
{
	toolMode = EPopulationToolMode::Select;
	transformationMode = ETransformationMode::Move;
	usedAssets.clear();
	editedPopulations.clear();
	forcePerpendicularToSurface = false;
	enableOnRealityData = false;
}

void AITwinPopulationTool::FImpl::SetDecorationHelper(AITwinDecorationHelper* decoHelper)
{
	decorationHelper = decoHelper;
}

bool AITwinPopulationTool::FImpl::DragActorInLevel(const FVector2D& screenPosition, const FString& assetPath)
{
	if (!IsEnabled())
	{
		return false;
	}
	UGameViewportClient* gameViewportClient = owner.GetWorld()->GetGameViewport();
	if (!gameViewportClient)
	{
		return false;
	}
	FSceneViewport* sceneViewport = gameViewportClient->GetGameViewport();
	if (!sceneViewport)
	{
		return false;
	}

	// The conversion from absolute to local coordinates below is done like
	// in FSceneViewport::UpdateCachedCursorPos.
	const FGeometry& cachedGeom = sceneViewport->GetCachedGeometry();
	FVector2D localPixelMousePos = cachedGeom.AbsoluteToLocal(screenPosition);
	localPixelMousePos.X = FMath::Clamp(localPixelMousePos.X * cachedGeom.Scale,
		(double)TNumericLimits<int32>::Min(), (double)TNumericLimits<int32>::Max());
	localPixelMousePos.Y = FMath::Clamp(localPixelMousePos.Y * cachedGeom.Scale,
		(double)TNumericLimits<int32>::Min(), (double)TNumericLimits<int32>::Max());

	APlayerController* playerController = owner.GetWorld()->GetFirstPlayerController();

	FVector traceStart, traceEnd, traceDir;
	if (UGameplayStatics::DeprojectScreenToWorld(
		playerController, localPixelMousePos, traceStart, traceDir))
	{
		if (draggedAssetPopulation == nullptr)
		{
			StartDragging(decorationHelper->GetOrCreatePopulation(assetPath));
		}

		// Do the intersection test to place the instance
		FHitResult hitResult;
		traceEnd = traceStart + traceDir * 1e8f;

		UKismetSystemLibrary::LineTraceSingle(
			&owner, traceStart, traceEnd, ETraceTypeQuery::TraceTypeQuery1, true,
			allPopulations, EDrawDebugTrace::None, hitResult, true);

		FTransform instTransform;
		if (!ComputeTransformFromHitResult(hitResult, instTransform, draggedAssetPopulation, true))
		{
			instTransform.SetTranslation(traceStart + traceDir * 1000);
		}
		
		if (draggedAssetInstanceIndex == -1)
		{
			draggedAssetPopulation->AddInstance(instTransform);
			draggedAssetInstanceIndex = draggedAssetPopulation->GetNumberOfInstances() - 1;
		}
		else
		{
			draggedAssetPopulation->SetInstanceTransform(draggedAssetInstanceIndex, instTransform);
		}

		return true;
	}
	return false;
}

void AITwinPopulationTool::FImpl::ReleaseDraggedAssetInstance()
{ 
	draggedAssetPopulation = nullptr;
	draggedAssetInstanceIndex = -1;
}

void AITwinPopulationTool::FImpl::DestroyDraggedAssetInstance()
{ 
	DeleteInstanceFromPopulation(draggedAssetPopulation, draggedAssetInstanceIndex);
}

void AITwinPopulationTool::FImpl::SetUsedAsset(const FString& assetPath, bool b)
{
	usedAssets[assetPath] = b;

	// Empty the vector of edited populations so that it is updated the next time
	// instances will be added.
	editedPopulations.clear();
}

void AITwinPopulationTool::FImpl::ClearUsedAssets()
{
	usedAssets.clear();
	editedPopulations.clear();
}

int32 AITwinPopulationTool::FImpl::GetInstanceCount(const FString& assetPath) const
{
	return IsEnabled() ? decorationHelper->GetPopulationInstanceCount(assetPath) : 0;
}

bool AITwinPopulationTool::FImpl::GetForcePerpendicularToSurface() const
{
	return forcePerpendicularToSurface;
}

void AITwinPopulationTool::FImpl::SetForcePerpendicularToSurface(bool b)
{ 
	forcePerpendicularToSurface = b;
}

bool AITwinPopulationTool::FImpl::GetEnableOnRealityData() const
{ 
	return enableOnRealityData;
}

void AITwinPopulationTool::FImpl::SetEnableOnRealityData(bool b)
{
	enableOnRealityData = b;

	if (enableOnRealityData)
	{
		TArray<AITwinCesium3DTileset*> tilesets;
		ITwin::Gather3DMapTilesets(owner.GetWorld(), tilesets);
		for (auto tileset : tilesets)
		{
			tileset->SetCreatePhysicsMeshes(true);
		}
	}
}

bool AITwinPopulationTool::FImpl::GetIsEditingBrushSize() const
{ 
	return isEditingBrushSize;
}

void AITwinPopulationTool::FImpl::SetIsEditingBrushSize(bool b)
{ 
	isEditingBrushSize = b;
}

void AITwinPopulationTool::FImpl::DoMouseClickAction()
{
	FHitResult hitResult = LineTraceFromMousePos();

	AActor* hitActor = hitResult.GetActor();

	SetSelectedPopulation(nullptr);

	if (hitActor)
	{
		if (hitActor->IsA(AITwinPopulation::StaticClass()) &&
			hitResult.Item >= 0 && toolMode == EPopulationToolMode::Select)
		{
			SetSelectedPopulation(Cast<AITwinPopulation>(hitActor));
			SetSelectedInstanceIndex(hitResult.Item);
		}
		else if (toolMode == EPopulationToolMode::Instantiate ||
				 toolMode == EPopulationToolMode::InstantiateN)
		{
			AddSingleInstanceFromHitResult(hitResult);
		}
	}
}

void AITwinPopulationTool::FImpl::Tick(float DeltaTime)
{
	if (enabled && IsBrushModeActivated())
	{
		// Place the brush sphere.
		FHitResult hitResult;
		if (isEditingBrushSize)
		{
			SetBrushPosition(LineTraceToSetBrushSize());
		}
		else
		{
			hitResult = LineTraceFromMousePos();
			if (hitResult.GetActor())
			{
				SetBrushPosition(hitResult.Location);
			}
		}

		// Add/remove instances in the brush zone.
		if (hitResult.GetActor() && isBrushingInstances)
		{
			if (!editedPopulations.empty() && toolMode == EPopulationToolMode::InstantiateN)
			{
				float currentTime = owner.GetGameTimeSinceCreation();
				float brushDeltaTime = currentTime - brushLastTime;
				float brushRadiusInMeters = brushRadius * 1e-2f;
				float brushDiskArea = brushRadiusInMeters * brushRadiusInMeters * UE_PI;
				float traceCount = brushFlow.GetFlow() * brushDeltaTime * brushDiskArea;
				int32 traceCountInt = static_cast<int32>(traceCount);

				if(traceCountInt > 0)
				{
					MultiLineTraceFromMousePos(traceCountInt, editedPopulations);

					brushLastTime = currentTime;
					brushLastPos = brushSphere->GetActorLocation();
				}
			}
			else if (toolMode == EPopulationToolMode::RemoveInstances)
			{
				TArray<AActor*> actorsToIgnore;
				TArray<FHitResult> hitResults;

				if (UKismetSystemLibrary::SphereTraceMulti(
						&owner, hitResult.Location, hitResult.Location, brushRadius,
						ETraceTypeQuery::TraceTypeQuery1, false, actorsToIgnore,
						EDrawDebugTrace::None, hitResults, true))
				{
					std::map<AITwinPopulation*, TArray<int32>> hitsByPopulation;
					for (auto& hitRes : hitResults)
					{
						AActor* hitActor = hitRes.GetActor();
						if (hitActor && hitActor->IsA(AITwinPopulation::StaticClass()) && hitRes.Item >= 0)
						{
							hitsByPopulation[Cast<AITwinPopulation>(hitActor)].AddUnique(hitRes.Item);
						}
					}

					for (auto& hits : hitsByPopulation)
					{
						hits.second.Sort([](const int32& a, const int32& b) {return a > b;});
						hits.first->RemoveInstances(hits.second);
					}
				}
			}
		}
	}
}

void AITwinPopulationTool::FImpl::InitBrushSphere()
{
	// Create the brush sphere and material (like in FoliageEdMode.cpp in the UE source code)
	UMaterial* brushMaterial = LoadObject<UMaterial>(
		nullptr, TEXT("/ITwinForUnreal/ITwin/Materials/BrushMaterial.BrushMaterial"), nullptr, LOAD_None, nullptr);
	UMaterialInstanceDynamic* brushMID = 
		UMaterialInstanceDynamic::Create(brushMaterial, GetTransientPackage());
	check(brushMID != nullptr);
	UStaticMesh* brushSphereMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/ITwinForUnreal/ITwin/Meshes/BrushSphere.BrushSphere"), nullptr, LOAD_None, nullptr);
	brushSphere = owner.GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass());
	brushSphere->SetMobility(EComponentMobility::Movable);
	brushSphere->SetActorLocation(FVector(0));
	brushSphere->SetActorHiddenInGame(true);
	UStaticMeshComponent* brushSphereComp = brushSphere->GetStaticMeshComponent();
	brushSphereComp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	brushSphereComp->SetCollisionObjectType(ECC_WorldDynamic);
	brushSphereComp->SetStaticMesh(brushSphereMesh);
	brushSphereComp->SetMaterial(0, brushMID);
	brushSphereComp->SetAbsolute(true, true, true);
	brushSphereComp->CastShadow = false;
}

bool AITwinPopulationTool::FImpl::ComputeTransformFromHitResult(
	const FHitResult& hitResult, FTransform& transform,
	const AITwinPopulation* population, bool isDraggingInstance /*= false*/)
{
	AActor* hitActor = hitResult.GetActor();

	if (!hitActor ||
		(!enableOnRealityData && hitActor->IsA(AITwinCesium3DTileset::StaticClass()) &&
		 ITwin::Is3DMapTileset(Cast<AITwinCesium3DTileset>(hitActor))))
	{
		return false;
	}

	FMatrix hitMat(FMatrix::Identity);

	float rotVar = 0.f;
	if (!population->IsRotationVariationEnabled())
	{
		if (savedTransformChanged)
		{
			FVector eulerAngles = savedTransform.GetRotation().Euler();
			savedAngleZ = FMath::DegreesToRadians(static_cast<float>(eulerAngles.Z));
			savedTransformChanged = false;
		}
		rotVar = savedAngleZ;
	}
	else if (isDraggingInstance)
	{
		rotVar = draggingRotVar;
	}
	else if (instancesRotationVariation != 0.f)
	{
		rotVar = FMath::FRandRange(
			-instancesRotationVariation, instancesRotationVariation);
	}

	if (population->IsPerpendicularToSurface() || forcePerpendicularToSurface)
	{
		FVector sZ = hitResult.Normal;
		FVector sX = FVector::XAxisVector;
		if (sX.Dot(sZ) > 0.8f)
		{
			sX = FVector::YAxisVector;
		}
		FVector sY = sZ ^ sX;
		sY.Normalize();
		sX = sY ^ sZ;
		hitMat = FMatrix(sX, sY, sZ, FVector(0.f));

		if (rotVar != 0.f)
		{
			FQuat hitQuat(hitMat);
			hitQuat = FQuat(sZ, rotVar) * hitQuat;
			hitMat = hitQuat.ToMatrix();
		}
	}
	else if (rotVar != 0.f)
	{
		FQuat hitQuat(FVector::ZAxisVector, rotVar);
		hitMat = hitQuat.ToMatrix();
	}
	hitMat.SetOrigin(FVector(hitResult.Location));

	if (population->IsScaleVariationEnabled() && instancesScaleVariation > 0.f)
	{
		float scaleVar = 0.f;
		if (isDraggingInstance)
		{
			scaleVar = draggingScaleVar;
		}
		else
		{
			scaleVar = FMath::FRandRange(-instancesScaleVariation, instancesScaleVariation);
		}

		hitMat = hitMat.ApplyScale(1.f + scaleVar);
	}

	transform.SetFromMatrix(hitMat);

	return true;
}

FHitResult AITwinPopulationTool::FImpl::LineTraceFromMousePos()
{
	FHitResult hitResult;

	APlayerController* playerController = owner.GetWorld()->GetFirstPlayerController();

	if (!playerController)
		return hitResult;

	FVector traceStart, traceEnd, traceDir;
	if (!playerController->DeprojectMousePositionToWorld(traceStart, traceDir))
		return hitResult;

	traceEnd = traceStart + traceDir * 1e8f;

	
	TArray<AActor*> actorsToIgnore;
	if (toolMode == EPopulationToolMode::RemoveInstances ||	draggedAssetPopulation)
	{
		// When erasing instances, collisions are enabled. When dragging an instance from the
		// browser, collisions may be enabled depending on the current mode. Existing populations
		// must be explicitly ignored here so that the brush sphere is placed like when painting
		// instances (it avoids rapid jumps).
		actorsToIgnore = allPopulations;
	}

	UKismetSystemLibrary::LineTraceSingle(
		&owner, traceStart, traceEnd, ETraceTypeQuery::TraceTypeQuery1, true,
		actorsToIgnore, EDrawDebugTrace::None, hitResult, true);

	return hitResult;
}

FVector AITwinPopulationTool::FImpl::LineTraceToSetBrushSize()
{
	APlayerController* playerController = owner.GetWorld()->GetFirstPlayerController();

	if (!playerController)
		return FVector(0, 0, 0);

	int32 width, height;
	playerController->GetViewportSize(width, height);

	FVector traceStart, traceEnd, traceDir;
	FVector2D screenPos(width*0.5, height*0.5);

	if (!UGameplayStatics::DeprojectScreenToWorld(playerController, screenPos, traceStart, traceDir))
		return FVector(0, 0, 0);

	traceEnd = traceStart + traceDir * 1e8f;

	TArray<AActor*> actorsToIgnore;
	FHitResult hitResult;

	UKismetSystemLibrary::LineTraceSingle(
		&owner, traceStart, traceEnd, ETraceTypeQuery::TraceTypeQuery1, true,
		actorsToIgnore, EDrawDebugTrace::None, hitResult, true);

	if (hitResult.GetActor())
	{
		return hitResult.Location;
	}
	else
	{
		return traceStart + traceDir * 1e4f;
	}
}

void AITwinPopulationTool::FImpl::MultiLineTraceFromMousePos(
	int32 traceCount, std::vector<AITwinPopulation*> populations)
{
	APlayerController* playerController = owner.GetWorld()->GetFirstPlayerController();

	if (!playerController)
		return;

	FVector traceStart, traceEnd, traceDir;
	if (!playerController->DeprojectMousePositionToWorld(traceStart, traceDir))
		return;

	const ULocalPlayer* LP = playerController->GetLocalPlayer();

	if (!LP || !LP->ViewportClient)
		return;

	FSceneViewProjectionData projectionData;
	if (!LP->GetProjectionData(LP->ViewportClient->Viewport, projectionData))
		return;

	FMatrix inverseViewRotMat = projectionData.ViewRotationMatrix.Inverse();
	FVector camRight, camUp, camForward;
	inverseViewRotMat.GetUnitAxes(camRight, camUp, camForward);

	// Build a basis
	FVector vZ = traceDir;
	vZ.Normalize();
	FVector vY = camUp;
	FVector vX = vY ^ vZ;
	vZ = vX ^ vY;

	// Estimate the normal of the intersection between the brush sphere and the scene.
	FVector brushPos = brushSphere->GetActorLocation();
	int32 numCircles = 3;
	float radiusStep = brushRadius/static_cast<float>(numCircles);
	
	FVector averageNormal(0);
	TArray<AActor*> actorsToIgnore;

	for (int32 c = 1; c <= numCircles; ++c)
	{
		float currentRadius = radiusStep*static_cast<float>(c);

		int32 numSamples = static_cast<int32>(UE_TWO_PI * static_cast<float>(c));
		float angleStep = UE_TWO_PI / static_cast<float>(numSamples);

		FVector diskAverageNormal(0);
		for (int32 s = 0; s < numSamples; ++s)
		{
			float currentAngle = angleStep*static_cast<float>(s);

			// Compute traceEnd (on the apparent disk of the brush)
			traceEnd = brushPos + (vX*cosf(currentAngle) + vY*sinf(currentAngle))*currentRadius;
			traceDir = (traceEnd - traceStart);
			traceDir.Normalize();
			traceEnd += (traceDir*brushRadius);

			FHitResult hitResult;
			UKismetSystemLibrary::LineTraceSingle(
				&owner, traceStart, traceEnd, ETraceTypeQuery::TraceTypeQuery1, false,
				actorsToIgnore, EDrawDebugTrace::None, hitResult, true);

			if (!hitResult.GetActor())
				continue;

			if ((hitResult.Location - brushPos).Length() > brushRadius)
				continue;

			diskAverageNormal += hitResult.Normal;
		}

		if (diskAverageNormal.Normalize(1e-6))
		{
			averageNormal += diskAverageNormal;
		}
	}

	if (!averageNormal.Normalize(1e-6))
		return;

	traceDir = -averageNormal;

	// Build a new basis with the average normal as Z
	vZ = averageNormal;
	vX = camRight - (camRight*averageNormal)*averageNormal;
	if (!vX.Normalize(1e-6f))
		return;
	vY = vZ ^ vX;
	if (!vY.Normalize(1e-6f))
		return;

	// Increase the value of traceCount to compensate for the test below
	// which checks if random coordinates are inside the brush disk.
	traceCount = static_cast<int32>(static_cast<float>(traceCount) * 4.f/UE_PI);

	double brushPosStep = 1./static_cast<double>(traceCount);

	for (int32 i = 1; i <= traceCount; ++i)
	{
		float rx = FMath::FRandRange(-1.f, 1.f);
		float ry = FMath::FRandRange(-1.f, 1.f);
		if (sqrtf(rx*rx + ry*ry) > 1.f)
		{
			continue;
		}

		FHitResult hitResult;
		double t = static_cast<double>(i)*brushPosStep;
		FVector interpolatedBrushPos = brushLastPos*(1.-t) + brushPos*t;

		// Compute traceEnd
		FVector diskPos = interpolatedBrushPos + (vX*rx + vY*ry)*brushRadius;
		traceEnd = diskPos - (averageNormal*brushRadius*2.f);
		traceStart = diskPos + averageNormal*brushRadius;

		UKismetSystemLibrary::LineTraceSingle(
			&owner, traceStart, traceEnd, ETraceTypeQuery::TraceTypeQuery1, false,
			actorsToIgnore, EDrawDebugTrace::None, hitResult, true);

		if (!hitResult.GetActor())
		{
			continue;
		}

		float distToBrush = (hitResult.Location - interpolatedBrushPos).Length();
		if (distToBrush > brushRadius)
		{
			continue;
		}

		int32 popIndex = populations.size() > 1 ?
			FMath::RandRange((int32)0, (int32)populations.size() - 1) : 0;
		AITwinPopulation* population = populations[popIndex];

		FTransform transform;
		if (ComputeTransformFromHitResult(hitResult, transform, population))
		{
			population->AddInstance(transform);
		}
	}
}

bool AITwinPopulationTool::FImpl::AddSingleInstanceFromHitResult(const FHitResult& hitResult)
{
	if (editedPopulations.empty())
	{
		CollectEditedPopulations();
	}

	if (!editedPopulations.empty())
	{
		int32 popIndex = editedPopulations.size() > 1 ?
			FMath::RandRange((int32)0, (int32)editedPopulations.size() - 1) : 0;
		FTransform tm;
		if (ComputeTransformFromHitResult(hitResult, tm, editedPopulations[popIndex]))
		{
			editedPopulations[popIndex]->AddInstance(tm);
			return true;
		}
	}

	return false;
}

void AITwinPopulationTool::FImpl::CollectEditedPopulations()
{
	if (!decorationHelper)
	{
		return;
	}

	editedPopulations.clear();

	for (auto& asset : usedAssets)
	{
		if (asset.second)
		{
			editedPopulations.push_back(decorationHelper->GetOrCreatePopulation(asset.first));
		}
	}
}

void AITwinPopulationTool::FImpl::SetBrushPosition(const FVector& position)
{
	if (brushSphere)
	{
		FTransform tm;
		tm.SetTranslation(position);
		tm.SetScale3D(FVector(brushRadius * BRUSH_MESH_INVERSE_RADIUS));
		brushSphere->SetActorTransform(tm);
	}
}

void AITwinPopulationTool::FImpl::UpdatePopulationsCollisionType() const
{
	ECollisionEnabled::Type collisionType = ECollisionEnabled::NoCollision;

	if (enabled &&
		(toolMode == EPopulationToolMode::Select ||
		 toolMode == EPopulationToolMode::RemoveInstances))
	{
		collisionType = ECollisionEnabled::QueryOnly;
	}

	for (auto actor : allPopulations)
	{
		Cast<AITwinPopulation>(actor)->meshComp->SetCollisionEnabled(collisionType);
	}
}

void AITwinPopulationTool::FImpl::UpdatePopulationsArray()
{
	allPopulations.Empty();
	UGameplayStatics::GetAllActorsOfClass(
		owner.GetWorld(), AITwinPopulation::StaticClass(), allPopulations);
}

void AITwinPopulationTool::FImpl::StartDragging(AITwinPopulation* population)
{
	draggedAssetPopulation = population;
	draggingRotVar = FMath::FRandRange(-instancesRotationVariation, instancesRotationVariation);
	draggingScaleVar = FMath::FRandRange(-instancesScaleVariation, instancesScaleVariation);
	UpdatePopulationsArray();
	UpdatePopulationsCollisionType();
}

void AITwinPopulationTool::FImpl::DeleteInstanceFromPopulation(
	AITwinPopulation*& population, int32& instanceIndex)
{
	if (population)
	{
		if (instanceIndex >= 0)
		{
			population->RemoveInstance(instanceIndex);
		}
		if (population->GetNumberOfInstances() == 0)
		{
			population->Destroy();
		}
		population = nullptr;
		instanceIndex = -1;
	}
}

// -----------------------------------------------------------------------------
//                            AITwinPopulationTool

AITwinPopulationTool::AITwinPopulationTool()
	:Impl(MakePimpl<FImpl>(*this))
{
	PrimaryActorTick.bCanEverTick = true; // needed for the brush
}

EPopulationToolMode AITwinPopulationTool::GetMode() const
{
	return Impl->GetMode();
}

void AITwinPopulationTool::SetMode(const EPopulationToolMode& mode)
{
	Impl->SetMode(mode);
}

ETransformationMode AITwinPopulationTool::GetTransformationMode() const
{
	return Impl->GetTransformationMode();
}

void AITwinPopulationTool::SetTransformationMode(const ETransformationMode& mode)
{
	Impl->SetTransformationMode(mode);
}

AITwinPopulation* AITwinPopulationTool::GetSelectedPopulation() const
{
	return Impl->GetSelectedPopulation();
}

void AITwinPopulationTool::SetSelectedPopulation(AITwinPopulation* population)
{
	Impl->SetSelectedPopulation(population);
}

void AITwinPopulationTool::SetSelectedInstanceIndex(int32 instanceIndex)
{
	Impl->SetSelectedInstanceIndex(instanceIndex);
}

bool AITwinPopulationTool::HasSelectedPopulation() const
{
	return Impl->HasSelectedPopulation();
}

void AITwinPopulationTool::DeleteSelectedInstance()
{
	Impl->DeleteSelectedInstance();
}

bool AITwinPopulationTool::IsPopulationModeActivated() const
{
	return Impl->IsPopulationModeActivated();
}

bool AITwinPopulationTool::IsBrushModeActivated() const
{
	return Impl->IsBrushModeActivated();
}

void AITwinPopulationTool::StartBrushingInstances()
{
	Impl->StartBrushingInstances();
}

void AITwinPopulationTool::EndBrushingInstances()
{
	Impl->EndBrushingInstances();
}

void AITwinPopulationTool::ShowBrushSphere()
{
	Impl->ShowBrushSphere();
}

void AITwinPopulationTool::HideBrushSphere()
{
	Impl->HideBrushSphere();
}

void AITwinPopulationTool::ComputeBrushFlow()
{
	Impl->ComputeBrushFlow();
}

float AITwinPopulationTool::GetBrushFlow() const
{
	return Impl->GetBrushFlow();
}

void AITwinPopulationTool::SetBrushFlow(float flow)
{
	Impl->SetBrushFlow(flow);
}

float AITwinPopulationTool::GetBrushSize() const
{
	return Impl->GetBrushSize();
}

void AITwinPopulationTool::SetBrushSize(float size)
{
	Impl->SetBrushSize(size);
}

FTransform AITwinPopulationTool::GetSelectionTransform() const
{
	return Impl->GetSelectionTransform();
}

void AITwinPopulationTool::SetSelectionTransform(const FTransform& transform)
{
	Impl->SetSelectionTransform(transform);
}

FLinearColor AITwinPopulationTool::GetSelectionColorVariation() const
{
	return Impl->GetSelectionColorVariation();
}

void AITwinPopulationTool::SetSelectionColorVariation(const FLinearColor& color)
{
	Impl->SetSelectionColorVariation(color);
}

void AITwinPopulationTool::SetEnabled(bool value)
{
	Impl->SetEnabled(value);
}

bool AITwinPopulationTool::IsEnabled() const
{
	return Impl->IsEnabled();
}

void AITwinPopulationTool::ResetToDefault()
{
	Impl->ResetToDefault();
}

void AITwinPopulationTool::SetDecorationHelper(AITwinDecorationHelper* decoHelper)
{
	Impl->SetDecorationHelper(decoHelper);
}

bool AITwinPopulationTool::DragActorInLevel(const FVector2D& screenPosition, const FString& assetPath)
{
	return Impl->DragActorInLevel(screenPosition, assetPath);
}

void AITwinPopulationTool::ReleaseDraggedAssetInstance()
{ 
	Impl->ReleaseDraggedAssetInstance();
}

void AITwinPopulationTool::DestroyDraggedAssetInstance()
{ 
	Impl->DestroyDraggedAssetInstance();
}

void AITwinPopulationTool::SetUsedAsset(const FString& assetPath, bool used)
{
	Impl->SetUsedAsset(assetPath, used);
}

void AITwinPopulationTool::ClearUsedAssets()
{
	Impl->ClearUsedAssets();
}

int32 AITwinPopulationTool::GetInstanceCount(const FString& assetPath) const
{
	return Impl->GetInstanceCount(assetPath);
}

bool AITwinPopulationTool::GetForcePerpendicularToSurface() const
{
	return Impl->GetForcePerpendicularToSurface();
}

void AITwinPopulationTool::SetForcePerpendicularToSurface(bool b)
{ 
	Impl->SetForcePerpendicularToSurface(b);
}

bool AITwinPopulationTool::GetEnableOnRealityData() const
{ 
	return Impl->GetEnableOnRealityData();
}

void AITwinPopulationTool::SetEnableOnRealityData(bool b)
{
	Impl->SetEnableOnRealityData(b);
}

bool AITwinPopulationTool::GetIsEditingBrushSize() const
{ 
	return Impl->GetIsEditingBrushSize();
}

void AITwinPopulationTool::SetIsEditingBrushSize(bool b)
{ 
	Impl->SetIsEditingBrushSize(b);
}

void AITwinPopulationTool::DoMouseClickAction()
{
	Impl->DoMouseClickAction();
}

void AITwinPopulationTool::BeginPlay()
{
	AActor::BeginPlay();
	Impl->InitBrushSphere();
}

void AITwinPopulationTool::Tick(float DeltaTime)
{
	Impl->Tick(DeltaTime);
}
