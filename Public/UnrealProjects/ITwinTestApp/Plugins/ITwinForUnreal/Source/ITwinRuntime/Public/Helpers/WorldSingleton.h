/*--------------------------------------------------------------------------------------+
|
|     $Source: WorldSingleton.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include "CoreMinimal.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include <ITwinRuntime/Private/Compil/BeforeNonUnrealIncludes.h>
#	include <Core/Tools/Log.h>
#include <ITwinRuntime/Private/Compil/AfterNonUnrealIncludes.h>

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
					BE_LOGE("ITwin", "Found more than one actor of class "
						<< TCHAR_TO_UTF8(*GetNameSafe(ActorType::StaticClass())));
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
