/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSplineHelper.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <GameFramework/Actor.h>
#include <UObject/ObjectMacros.h>
#include <Engine/Blueprint.h>
#include <vector>
#include "ITwinSplineHelper.generated.h"

class USceneComponent;
class USplineComponent;
class USplineMeshComponent;
class UStaticMeshComponent;
class UITwinCesiumGlobeAnchorComponent;
class AITwinCesiumCartographicPolygon;

UENUM(BlueprintType)
enum class ETangentMode : uint8 {
	Linear = 0,
	Smooth = 1,
	Custom = 2
};

UCLASS()
class ITWINRUNTIME_API AITwinSplineHelper : public AActor
{
	GENERATED_BODY()

public:
	AITwinSplineHelper();

	USplineComponent* GetSplineComponent() const { return splineComponent; }
	void SetSplineComponent(USplineComponent* splineComp);
	int32 GetNumberOfSplinePoints() const;

	void Initialize(USplineComponent* splineComp, const ETangentMode& mode);
	void SetTangentMode(const ETangentMode& mode);
	void InitMeshComponent(UStaticMeshComponent* meshComp, UStaticMesh* mesh);
	void AddAllMeshComponents();
	void AddMeshComponentsForPoint(int32 pointIndex);
	void UpdateMeshComponentsForPoint(int32 pointIndex);
	int32 FindPointIndexFromMeshComponent(UStaticMeshComponent* meshComp) const;

	AITwinCesiumCartographicPolygon* GetCartographicPolygon() const;
	void SetCartographicPolygon(AITwinCesiumCartographicPolygon* polygon);

	void SetTransform(const FTransform& NewTransform);

	FVector GetLocationAtSplinePoint(int32 pointIndex) const;
	void SetLocationAtSplinePoint(int32 pointIndex, const FVector& location);

	void DeletePoint(int32 pointIndex);
	void DuplicatePoint(int32 pointIndex);
	void DuplicatePoint(int32& pointIndex, FVector& newWorldPosition);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cesium")
	UITwinCesiumGlobeAnchorComponent* GlobeAnchor;

private:
	UStaticMesh* splineMesh = nullptr;
	UStaticMesh* pointMesh = nullptr;
	USplineComponent* splineComponent = nullptr;

	std::vector<UStaticMeshComponent*> pointMeshComponents;
	std::vector<USplineMeshComponent*> splineMeshComponents;

	ETangentMode tangentMode = ETangentMode::Custom;

	// Optional pointer to a cartographic polygon (can be left null)
	AITwinCesiumCartographicPolygon* cartographicPolygon = nullptr;
};