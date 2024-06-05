/*--------------------------------------------------------------------------------------+
|
|     $Source: ITwinGeolocation.cpp $
|
|  $Copyright: (c) 2024 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/

#include <ITwinGeolocation.h>
#include <Engine/World.h>

FITwinGeolocation::FITwinGeolocation(AActor& Parent)
{
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = &Parent;
		LocatedGeoreference.Reset(Parent.GetWorld()->SpawnActor<AITwinCesiumGeoreference>(SpawnParams));
#if WITH_EDITOR
		LocatedGeoreference->SetActorLabel(TEXT("Located Georeference"));
#endif
		LocatedGeoreference->AttachToActor(&Parent, FAttachmentTransformRules::KeepRelativeTransform);
		LocatedGeoreference->SetOriginPlacement(EOriginPlacement::TrueOrigin); // Means "not yet inited".
	}
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = &Parent;
		NonLocatedGeoreference.Reset(Parent.GetWorld()->SpawnActor<AITwinCesiumGeoreference>(SpawnParams));
#if WITH_EDITOR
		NonLocatedGeoreference->SetActorLabel(TEXT("Non-Located Georeference"));
#endif
		NonLocatedGeoreference->AttachToActor(&Parent, FAttachmentTransformRules::KeepRelativeTransform);
		NonLocatedGeoreference->SetOriginPlacement(EOriginPlacement::TrueOrigin);
	}
}
