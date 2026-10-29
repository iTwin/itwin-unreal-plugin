/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinWebServicesSubsystem.cpp $
|
|  $Copyright: (c) 2026 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinWebServices/ITwinWebServicesSubsystem.h>
#include <ITwinWebServices/ITwinWebServices.h>

#include <Engine/World.h>

bool UITwinWebServicesSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Not needed for cook/commandlets that never load a gameplay map.
	return !IsRunningCommandlet();
}

void UITwinWebServicesSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	WorldCleanupDelegate = FWorldDelegates::OnWorldCleanup.AddWeakLambda(this,
		[](UWorld* World, bool /*bSessionEnded*/, bool bCleanupResources)
	{
		if (!World || !bCleanupResources)
			return;

		switch (World->WorldType)
		{
		case EWorldType::Game:
		case EWorldType::PIE:
		case EWorldType::Editor:
			break;
		default:
			// EditorPreview, GamePreview, GameRPC, Inactive, None: thumbnail /
			// material / blueprint preview worlds, not real sessions.
			return;
		}

		UITwinWebServices::ResetPreferredEnvironment();
	});
}

void UITwinWebServicesSubsystem::Deinitialize()
{
	if (WorldCleanupDelegate.IsValid())
	{
		FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupDelegate);
		WorldCleanupDelegate.Reset();
	}
	Super::Deinitialize();
}
