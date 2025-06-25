/*--------------------------------------------------------------------------------------+
|
|     $Source: WorldSingleton.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include <ITwinServiceActor.h> // for LogITwin

template <typename ActorType>
class TWorldSingleton
{
public:
	ActorType* Get(UWorld* World)
	{
		if (World == nullptr)
			return nullptr;

		if (!Actor.IsValid())
		{
			for (TActorIterator<ActorType> It(World); It; ++It)
			{
				if (!Actor.IsValid())
				{
					Actor = *It;
				}
				else
				{
					UE_LOG(LogITwin, Error, TEXT("Found more than one actor of class %s"), *GetNameSafe(ActorType::StaticClass()));
				}
			}
			if (!Actor.IsValid())
			{
				FString ActorName = FString::Printf(TEXT("iTwin%s"), *GetNameSafe(ActorType::StaticClass()));
				FActorSpawnParameters param;
				param.Name = FName(*ActorName);
				Actor = World->SpawnActor<ActorType>(ActorType::StaticClass(), param);
			}
		}

		return Actor.Get();
	}

private:
	TWeakObjectPtr<ActorType> Actor;
};
