/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSnapshotActor.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
//#include "GameFramework/Actor.h"
//#include <Engine/SceneCapture2D.h>
//#include "ITwinSnapshotActor.generated.h"
//
//
//class UMaterial;
//class UMaterialInterface;
//class UTexture;
//
//UCLASS()
//class AITwinSnapshotActor : public ASceneCapture2D
//{
//	GENERATED_BODY()
//	
//public:	
//	// Sets default values for this actor's properties
//	AITwinSnapshotActor();
//	
//	//static AITwinSnapshotActor* Get();
//	
//	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LRT Variables")
//	UTextureRenderTarget2D* pRenderTarget_ = nullptr;
//	
//	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LRT Variables")
//	UStaticMeshComponent* pPlaneMesh_ = nullptr;
//	UMaterial* pMat_ = nullptr;
//	UMaterialInterface* pMatInst_ = nullptr;
//	UTexture* pTexture_ = nullptr;
//
//protected:
//	// Called when the game starts or when spawned
//	virtual void BeginPlay() override;
//
//public:	
//	// Called every frame
//	virtual void Tick(float DeltaTime) override;
//
//	//UFUNCTION()
//	//UTexture2D* GetSnapshotTexture(bool debug = false);
//	
//	UFUNCTION()
//	static UTexture2D* CreateNewTextureFromColorArray(int32 SizeX, int32 SizeY, EPixelFormat PixelFormat, const TArray<FColor>& ColorBuffer);
//	UFUNCTION()
//	UTexture2D* CreateNewTexture(const TArray<FColor>& ColorBuffer);
//	UFUNCTION()
//	void GetSnapshot(TArray<FColor>& ColorBuffer);
//	UFUNCTION()
//	void GetSnapshotAtLocation(TArray<FColor>& ColorBuffer, FVector pos, FRotator rot);
//	//UFUNCTION()
//	void GetSnapshotSizeAndFormat(int32& SizeX, int32& SizeY, EPixelFormat& PixelFormat) const;
//};
