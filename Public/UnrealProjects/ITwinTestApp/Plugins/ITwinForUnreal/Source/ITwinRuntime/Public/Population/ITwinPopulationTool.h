/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinPopulationTool.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

#include <Population/ITwinPopulationToolEnum.h>
#include <Templates/PimplPtr.h>
#include <ITwinInteractiveTool.h>
#include <Math/MathFwd.h>
#include <Engine/HitResult.h>
#include <Containers/Array.h>

#include <BeUtils/SplineSampling/SplineSampling.h>
#include <Components/SplineComponent.h>

#include "ITwinPopulationTool.generated.h"

class UObject;
class AStaticMeshActor;
class AITwinPopulation;
class AITwinDecorationHelper;
class AITwinSplineHelper;

class FUESplineCurve : public BeUtils::SplineCurve
{
public:
	FUESplineCurve(USplineComponent const& InSpline);
	virtual glm::dvec3 GetPositionAtCoord(value_type const& u) const override;
	virtual glm::dvec3 GetTangentAtCoord(value_type const& u) const override;
	virtual size_t PointCount(const bool /*accountForCyclicity*/) const override;
	virtual glm::dvec3 GetPositionAtIndex(size_t idx) const override;
	virtual bool IsCyclic() const override;

private:
	USplineComponent const& UESpline;
};


UCLASS()
class ITWINRUNTIME_API AITwinPopulationTool : public AITwinInteractiveTool
{
	GENERATED_BODY()

public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSingleInstanceAddedEvent);
	UPROPERTY()
	FSingleInstanceAddedEvent SingleInstanceAddedEvent;
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSelectionChangedEvent);
	UPROPERTY()
	FSelectionChangedEvent SelectionChangedEvent;
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FModeChangedEvent);
	UPROPERTY()
	FModeChangedEvent ModeChangedEvent;

	AITwinPopulationTool();

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	EPopulationToolMode GetMode() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetMode(EPopulationToolMode mode);

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	ETransformationMode GetTransformationMode() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetTransformationMode(ETransformationMode mode);

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	AITwinPopulation* GetSelectedPopulation() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	int32 GetSelectedInstanceIndex() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetSelectedPopulation(AITwinPopulation* population);

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetSelectedInstanceIndex(int32 instanceIndex);
	
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool HasSelectedPopulation() const;
	
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool HasSelectedInstance() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void DeleteSelectedInstance();

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool IsPopulationModeActivated() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	bool IsBrushModeActivated() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void StartBrushingInstances();

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void EndBrushingInstances();

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void ShowBrushSphere();

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void HideBrushSphere();

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	float GetBrushFlow() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetBrushFlow(float flow);

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	float GetBrushSize() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetBrushSize(float size);

	// Functions accessing the color variation of the selected instance
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	FLinearColor GetSelectionColorVariation() const;

	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetSelectionColorVariation(const FLinearColor& color);

	void SetDecorationHelper(AITwinDecorationHelper* decoHelper);

	bool DragActorInLevel(const FVector2D& screenPosition, const FString& assetPath);
	void ReleaseDraggedAssetInstance();
	void DestroyDraggedAssetInstance();

	void SetUsedAsset(const FString& assetPath, bool used);
	void ClearUsedAssets();

	/// Pre-load the given asset in a population.
	AITwinPopulation* PreLoadPopulation(const FString& AssetPath);

	/// Returns whether some instances can be added - ie. there is one (or more) selected assets.
	/// \param bOutAllowBrush Will be set to true if the paint brush is compatible with the selection.
	bool IsAdditionOfInstancesAllowed(bool& bOutAllowBrush) const;

	int32 GetInstanceCount(const FString& assetPath) const;

	bool GetForcePerpendicularToSurface() const;
	void SetForcePerpendicularToSurface(bool b);

	bool GetEnableOnRealityData() const;
	void SetEnableOnRealityData(bool b);

	bool GetIsEditingBrushSize() const;
	void SetIsEditingBrushSize(bool b);

	/// Sets the spline controlling the population.
	UFUNCTION(Category = "iTwin", BlueprintCallable)
	void SetSelectedSpline(AITwinSplineHelper* Spline);

	/// Populates the given spline.
	uint32 PopulateSpline(AITwinSplineHelper const& TargetSpline);

	class [[nodiscard]] FPickingContext
	{
	public:
		FPickingContext(AITwinPopulationTool& InTool, bool bRestrictPickingOnClipping);
		~FPickingContext();
	private:
		AITwinPopulationTool& Tool;
		const bool bRestrictPickingOnClipping_Old;
	};
	bool GetRestrictPickingOnClippingPrimitives() const;
	void RestrictPickingOnClippingPrimitives(bool bRestrictPickingOnClipping = true);

	/// Overridden from AITwinInteractiveTool
	virtual TUniquePtr<ISelectionRecord> MakeSelectionRecord() const override;
	virtual bool HasSameSelection(ISelectionRecord const& Selection) const override;
	virtual bool RestoreSelection(ISelectionRecord const& Selection) override;
	virtual TUniquePtr<IItemBackup> MakeSelectedItemBackup() const override;
	virtual bool RestoreItem(IItemBackup const& ItemBackup) override;

protected:
	/// Overridden from AActor
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/// Overridden from AITwinInteractiveTool
	virtual bool IsPopulationToolImpl() const override { return true; }
	virtual void SetEnabledImpl(bool bValue) override;
	virtual bool IsEnabledImpl() const override;
	virtual bool DoMouseClickActionImpl() override;
	virtual bool HasSelectionImpl() const override;
	virtual FTransform GetSelectionTransformImpl() const override;
	virtual void OnSelectionTransformStartedImpl() override;
	virtual void OnSelectionTransformCompletedImpl() override;
	virtual void SetSelectionTransformImpl(const FTransform& Transform) override;
	virtual void DeleteSelectionImpl() override;
	virtual void ResetToDefaultImpl() override;
	virtual bool StartInteractiveCreationImpl() override;
	virtual bool IsInteractiveCreationModeImpl() const override;

private:
	class FImpl;
	TPimplPtr<FImpl> Impl;

	UPROPERTY()
	TWeakObjectPtr<AITwinSplineHelper> SelectedSpline;
};

// ------------------------------------------

class ACesium3DTileset;

namespace ITwin
{
	ITWINRUNTIME_API void Gather3DMapTilesets(const UWorld* World, TArray<ACesium3DTileset*>& Out3DMapTilesets);
}