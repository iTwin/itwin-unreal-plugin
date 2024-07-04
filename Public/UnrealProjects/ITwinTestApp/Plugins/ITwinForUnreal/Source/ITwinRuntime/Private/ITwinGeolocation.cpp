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
		LocatedGeoreference = Parent.GetWorld()->SpawnActor<AITwinCesiumGeoreference>();
#if WITH_EDITOR
		LocatedGeoreference->SetActorLabel(TEXT("Located Georeference"));
#endif
		LocatedGeoreference->AttachToActor(&Parent, FAttachmentTransformRules::KeepRelativeTransform);
		LocatedGeoreference->SetOriginPlacement(EITwinOriginPlacement::TrueOrigin); // Means "not yet inited".
	}
	{
		NonLocatedGeoreference = Parent.GetWorld()->SpawnActor<AITwinCesiumGeoreference>();
#if WITH_EDITOR
		NonLocatedGeoreference->SetActorLabel(TEXT("Non-Located Georeference"));
#endif
		NonLocatedGeoreference->AttachToActor(&Parent, FAttachmentTransformRules::KeepRelativeTransform);
		NonLocatedGeoreference->SetOriginPlacement(EITwinOriginPlacement::TrueOrigin);
	}
}
