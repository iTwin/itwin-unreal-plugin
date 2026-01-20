/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinRuntimeTests.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

/*
//Example boiler plate
#include "CoreMinimal.h"
#include "CQTest.h"
#include <EngineUtils.h>
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Tests/AutomationCommon.h"
//#include "ITwinRuntime/Private/ITwinWebServices/ITwinWebServices.cpp"
#include "ITwinRuntime/Private/ITwinWebServices/ITwinAuthorizationManager.h"
#include <ITwinIModel.h>

//TEST_CLASS(MyFixtureName, GenerateTestDirectory)
TEST_CLASS(ItwinRuntimeTests, "Bentley.Runtime")
{
	//static variables setup in BEFORE_ALL and torn down in AFTER_ALL
	//const UWorld* World = GEngine->GetCurrentPlayWorld();
	UWorld* World = nullptr;
	//const UWorld* World = UGameplayStatics::OpenLevel();
	//Member variables shared between tests

	BEFORE_ALL()
	{
		
		//GEngine->GetWorldContextFromWorldChecked(World)
	}

	BEFORE_EACH()
	{
		//UGameplayStatics::OpenLevel(FName "/Game/Tests/Level.umap", bool true);
		AutomationOpenMap("/Game/Tests/Maps/ItwinLoadtestMap.ItwinLoadtestMap");
		World = GEngine->GetCurrentPlayWorld();
	}

	AFTER_EACH()
	{
		//delete if empty
	}

	AFTER_ALL()
	{
		//static method
		//delete if empty
	}

	TEST_METHOD(TestAllExpectedActorsExist)
	{
		FString ModelToLoad = "e72f78e8-795a-4036-8ef1-de06253c3cbf";
		World = GEngine->GetCurrentPlayWorld();
		auto* const IModel = World->SpawnActor<AITwinIModel>();
		
		IModel->LoadModel(ModelToLoad);
		//TArray<AActor*> ActorsToFind;
		//UGameplayStatics::GetAllActorsOfClass(World, AITwinIModel::StaticClass(), ActorsToFind);
	}
};
*/