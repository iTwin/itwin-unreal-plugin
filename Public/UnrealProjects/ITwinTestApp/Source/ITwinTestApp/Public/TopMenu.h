/*--------------------------------------------------------------------------------------+
|
|     $Source: TopMenu.h $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <GameFramework/Actor.h>
#include <ITwinIModel3DInfo.h>
#include <TopMenu.generated.h>

class UTopMenuWidgetImpl;
class UITwinWebServices;

UCLASS()
class ITWINTESTAPP_API ATopMenu: public AActor
{
	GENERATED_BODY()
protected:
	void BeginPlay() override;
public:
	UFUNCTION(BlueprintCallable)
	void UpdateElementId(bool bVisible, const FString& ElementId);
	UFUNCTION(BlueprintCallable)
	void GetAllSavedViews();
	UFUNCTION(BlueprintCallable)
	void SetIModelInfo(const FString& InITwinId, const FString& InIModelId, const FITwinIModel3DInfo& IModelInfo);
	UFUNCTION(BlueprintCallable)
	void ZoomOnIModel();
private:
	UPROPERTY()
	UTopMenuWidgetImpl* UI;
	UPROPERTY()
	UITwinWebServices* ITwinWebService;
	UPROPERTY()
	FString ITwinId;
	UPROPERTY()
	FString IModelId;
	UPROPERTY()
	FITwinIModel3DInfo IModel3dInfo;
	UFUNCTION(BlueprintCallable)
	void OnSavedViews(bool bSuccess, FSavedViewInfos SavedViews);
	UFUNCTION(BlueprintCallable)
	void SavedViewSelected(FString DisplayName, FString Value);
	UFUNCTION(BlueprintCallable)
	void GetSavedView(bool bSuccess, FSavedView SavedView, FSavedViewInfo SavedViewInfo);
	UFUNCTION(BlueprintCallable)
	void OnZoom();
	UFUNCTION(BlueprintCallable)
	void StartCameraMovementToSavedView(float& OutBlendTime, ACameraActor*& Actor, FTransform& Transform, const FSavedView& SavedView, float BlendTime);
	UFUNCTION(BlueprintCallable)
	void EndCameraMovement(ACameraActor* Actor, const FTransform& Transform);
	UFUNCTION(BlueprintCallable)
	void ITwinRotationToUE(FRotator& UERotation, const FRotator& ITwinRotation);
	UFUNCTION(BlueprintCallable)
	void ITwinPositionToUE(FVector& UEPos, const FVector& ITwinPos, const FVector& ModelOrigin);
};
