/*--------------------------------------------------------------------------------------+
|
|     $Source: TestWorldHelpers.h $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#pragma once

#include <CoreMinimal.h>

#if WITH_TESTS || WITH_DEV_AUTOMATION_TESTS

#include <Engine/Engine.h>
#include <Engine/World.h>
#include <EngineUtils.h> // for TActorIterator

namespace ITwinTests
{
	/// Minimal RAII world so we can spawn a real AAtmosphereActor (its components are
	/// created in the constructor, so a CDO is not enough for component-level asserts).
	/// BeginPlay() is deliberately NOT called: it loads /Game assets and binds widget
	/// delegates, neither of which is available in a headless automation run.
	class [[nodiscard]] FScopedTestWorld
	{
	public:
		FScopedTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false,
				TEXT("CarrotTestWorld"));
			if (World && GEngine)
			{
				FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
				Context.SetCurrentWorld(World);
			}
		}

		~FScopedTestWorld()
		{
			if (World)
			{
				// Actors we manually dispatched BeginPlay on must get EndPlay too, otherwise
				// their teardown never runs. AITwinTimelineActor releases its level sequence
				// player there; leaving it alive keeps the world's tick manager - and hence
				// the world itself - referenced, which GC then reports as a leak.
				for (TActorIterator<AActor> It(World); It; ++It)
				{
					It->Destroy();
				}

				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}
				World->DestroyWorld(/*bInformEngineOfWorld*/ false);
				World = nullptr;

				// The world is only collected once the last reference is gone; without an
				// explicit pass the next test's world creation reports it as "not cleaned up".
				CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			}
		}

		FScopedTestWorld(const FScopedTestWorld&) = delete;
		FScopedTestWorld& operator=(const FScopedTestWorld&) = delete;


		template <typename T>
		T* SpawnActorOfClass() const
		{
			if (!World)
			{
				return nullptr;
			}
			FActorSpawnParameters Params;
			Params.ObjectFlags |= RF_Transient;
			return World->SpawnActor<T>(T::StaticClass(), Params);
		}

		/// Spawns an actor and runs its BeginPlay. Needed for actors that build their
		/// state there rather than in the constructor (e.g. AITwinTimelineActor, whose
		/// pimpl is created in BeginPlay and dereferenced unguarded afterwards).
		/// Note: UWorld::BeginPlay() is NOT sufficient here - it defers to the authority
		/// game mode, which a bare CreateWorld() test world does not have.
		template <typename T>
		T* SpawnActorOfClassAndBeginPlay() const
		{
			T* Actor = SpawnActorOfClass<T>();
			if (Actor)
			{
				Actor->DispatchBeginPlay();
			}
			return Actor;
		}

		/// Actors whose state is built in BeginPlay (e.g. AITwinTimelineActor, whose pimpl is
		/// created there and dereferenced unguarded afterwards) need this.
		void DispatchBeginPlay() const
		{
			if (World)
			{
				World->InitializeActorsForPlay(FURL());
				World->BeginPlay();
			}
		}

		UWorld* Get() const { return World; }

	private:
		UWorld* World = nullptr;
	};
}

#endif // WITH_TESTS || WITH_DEV_AUTOMATION_TESTS
