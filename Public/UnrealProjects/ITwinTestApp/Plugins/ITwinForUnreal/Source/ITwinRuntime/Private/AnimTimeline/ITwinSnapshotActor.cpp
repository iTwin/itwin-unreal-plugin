/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinSnapshotActor.cpp $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include "AnimTimeline/ITwinSnapshotActor.h"
////#include "Timeline/TimelineUtils.h"
//
//#include <Kismet/GameplayStatics.h>
//#include <Engine/TextureRenderTarget2D.h>
//#include <Engine/Texture2D.h>
//#include <Engine/SceneCapture2D.h>
//#include <Components/SceneCaptureComponent2D.h>
//
//namespace
//{
//	UTexture2D* GetScreenshotAsTexture(USceneCaptureComponent2D* pCaptureComp, UObject* pObj)
//	{
//		pCaptureComp->CaptureScene();
//
//		UTexture2D* pTexture2D;
//
//		pTexture2D = pCaptureComp->TextureTarget->ConstructTexture2D(pObj, "screenshot_texture", EObjectFlags::RF_NoFlags, CTF_DeferCompression); // only in editor mode?
//		pCaptureComp->TextureTarget->CompressionSettings = TextureCompressionSettings::TC_VectorDisplacementmap;
//#if WITH_EDITORONLY_DATA
//		pTexture2D->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
//#endif
//		pTexture2D->SRGB = 1;
//		pTexture2D->UpdateResource();
//
//		return pTexture2D;
//	}
//}
//
//// Sets default values
//AITwinSnapshotActor::AITwinSnapshotActor()
//{
// 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
//	PrimaryActorTick.bCanEverTick = false;
//
//	auto pCaptureComp = GetCaptureComponent2D();
//	pCaptureComp->ProjectionType = ECameraProjectionMode::Type::Perspective;
//	pCaptureComp->FOVAngle = 90.0f;
//	pCaptureComp->CaptureSource = ESceneCaptureSource::SCS_FinalColorHDR;
//	pCaptureComp->CompositeMode = ESceneCaptureCompositeMode::SCCM_Overwrite;
//	pCaptureComp->bCaptureOnMovement = true;
//	pCaptureComp->bCaptureEveryFrame = true;
//	pCaptureComp->bAlwaysPersistRenderingState = true;
//	pCaptureComp->MaxViewDistanceOverride = -1.0f;
//	pCaptureComp->bAutoActivate = true;
//	pCaptureComp->DetailMode = EDetailMode::DM_High;
//
//	pRenderTarget_ = NewObject<UTextureRenderTarget2D>();
//	//renderTarget_ = UKismetRenderingLibrary::CreateRenderTarget2D(this, 1024, 1024, ETextureRenderTargetFormat::RTF_RGBA32f, FLinearColor::Black, false);
//	//pRenderTarget->InitAutoFormat(256, 256);
//	pRenderTarget_->InitCustomFormat(256, 256, EPixelFormat::PF_R8G8B8A8, true);//ETextureRenderTargetFormat::RTF_RGBA8, EPixelFormat::PF_FloatRGBA
//	pRenderTarget_->UpdateResourceImmediate();
//	//pRenderTarget->ClearColor = FLinearColor(0.f, 0.f, 0.f, 1.f);
//	pCaptureComp->TextureTarget = pRenderTarget_;
//
//	//rootComponent_ = CreateDefaultSubobject<USceneComponent>(TEXT("CaptureRootComponent"));
//	//cameraComponent_ = CreateDefaultSubobject<UCameraComponent>(TEXT("CaptureViewportCamera"));
//	//cameraComponent_->SetupAttachment(rootComponent_);
//	//captureComponent_ = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("CaptureComponent"));
//	//captureComponent_->SetupAttachment(cameraComponent_);
//	//renderTarget_ = NewObject<UTextureRenderTarget2D>();
//	//renderTarget_->InitCustomFormat(256, 256, EPixelFormat::PF_B8G8R8A8, true);  // some testing with EPixelFormat::PF_FloatRGBA, true=force Linear Gamma
//	//renderTarget_->UpdateResourceImmediate();
//	//captureComponent_->TextureTarget = renderTarget_;
//
////	pPlaneMesh_ = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SceneViewPlane"));
////	pPlaneMesh_->SetupAttachment(RootComponent);
//	//static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneAsset(TEXT("/Game/StarterContent/Shapes/Shape_Plane.Shape_Plane"));
//	//if (PlaneAsset.Succeeded())
//	//{
//	//	pPlaneMesh_->SetStaticMesh(PlaneAsset.Object);
//	//	pPlaneMesh_->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));
//	//	pPlaneMesh_->SetRelativeRotation(FRotator(90.0f, 0.0f, 270.0f));
//	//}
//}
//
//UTexture2D* AITwinSnapshotActor::CreateNewTextureFromColorArray(int32 SizeX, int32 SizeY, EPixelFormat PixelFormat, const TArray<FColor> & ColorBuffer)
//{
//	UTexture2D* pTexture2D = UTexture2D::CreateTransient(SizeX, SizeY, PixelFormat);
//#if WITH_EDITORONLY_DATA
//	pTexture2D->MipGenSettings = TMGS_NoMipmaps;
//#endif
//	pTexture2D->SRGB = 0;
//	//pTexture2D->CompressionSettings = TextureCompressionSettings::TC_VectorDisplacementmap; //TC_EditorIcon
//
//	TUniquePtr<FTexturePlatformData> pTexturePlatformData = MakeUnique<FTexturePlatformData>();
//	pTexturePlatformData->SizeX = SizeX;
//	pTexturePlatformData->SizeY = SizeY;
//	pTexturePlatformData->PixelFormat = PixelFormat;
//	FTexture2DMipMap* pMip = new FTexture2DMipMap();
//	pTexturePlatformData->Mips.Add(pMip);
//	pMip->SizeX = SizeX;
//	pMip->SizeY = SizeY;
//	pMip->BulkData.Lock(LOCK_READ_WRITE);
//	auto texSize = SizeX * SizeY * 4;
//	uint8* pTextureData = (uint8*)(pMip->BulkData.Realloc(texSize));
//
//	// UImage expects pMip pixels in RGB order, and it looks like FColor is BGR, so we can't do memory copy
//	//FMemory::Memcpy(pTextureData, ColorBuffer.GetData(), texSize); 
//	//{
//	//	uint8* scr = (uint8*)ColorBuffer.GetData();
//	//	UE_LOG(LogTemp, Warning, TEXT("Debug texture: ColorBuffer[0]=(%d, %d, %d, %d), ColorBuffer[0].R=%d,G=%d,B=%d"), scr[0], scr[1], scr[2], scr[3], ColorBuffer[0].R, ColorBuffer[0].G, ColorBuffer[0].B);
//	//}
//	for (auto i(0); i < texSize / 4; i++)
//	{
//		pTextureData[4 * i]		= ColorBuffer[i].R;
//		pTextureData[4 * i + 1] = ColorBuffer[i].G;
//		pTextureData[4 * i + 2] = ColorBuffer[i].B;
//		pTextureData[4 * i + 3] = 255;
//	}
//
//	pMip->BulkData.Unlock();
//	//FMemory::Memcpy(pMips[0], buffer.GetData(), buffer.Num() * 4);
//	pTexture2D->SetPlatformData(pTexturePlatformData.Release());
//	//pTexture2D->PlatformData->Mips[0].BulkData.Unlock();
//
//	// Apply Texture changes to GPU memory
//	pTexture2D->UpdateResource();
//
//	return pTexture2D;
//}
//
//UTexture2D* AITwinSnapshotActor::CreateNewTexture(const TArray<FColor>& ColorBuffer)
//{
//	return CreateNewTextureFromColorArray(GetCaptureComponent2D()->TextureTarget->SizeX, GetCaptureComponent2D()->TextureTarget->SizeY, GetCaptureComponent2D()->TextureTarget->GetFormat(), ColorBuffer);
//}
//
//void AITwinSnapshotActor::GetSnapshotAtLocation(TArray<FColor> &ColorBuffer, FVector pos, FRotator rot)
//{
//	// Capture scene
//	SetActorLocationAndRotation(pos, rot);
//	GetCaptureComponent2D()->CaptureScene();
//
//	// Read the pixels from the RenderTarget and store them in a FColor array
//	FRenderTarget* RenderTarget = GetCaptureComponent2D()->TextureTarget->GameThread_GetRenderTargetResource();
//	RenderTarget->ReadPixels(ColorBuffer);
//}
//
//void AITwinSnapshotActor::GetSnapshot(TArray<FColor> &ColorBuffer)
//{
//	// Capture scene in its current state
//	FVector pos;
//	FRotator rot;
//	UTimelineUtils::GetCurrentView(GetWorld(), pos, rot);
//	GetSnapshotAtLocation(ColorBuffer, pos, rot);
//}
//
//void AITwinSnapshotActor::GetSnapshotSizeAndFormat(int32& SizeX, int32& SizeY, EPixelFormat& PixelFormat) const
//{
//	//if (SizeX)
//	SizeX = GetCaptureComponent2D()->TextureTarget->SizeX;
//	//if (SizeY)
//	SizeY = GetCaptureComponent2D()->TextureTarget->SizeY;
//	//if (PixelFormat)
//	PixelFormat = GetCaptureComponent2D()->TextureTarget->GetFormat();
//}
//
//
//// Called when the game starts or when spawned
//void AITwinSnapshotActor::BeginPlay()
//{
//	Super::BeginPlay();	
//}
//
//// Called every frame
//void AITwinSnapshotActor::Tick(float DeltaTime)
//{
//	Super::Tick(DeltaTime);
//}
